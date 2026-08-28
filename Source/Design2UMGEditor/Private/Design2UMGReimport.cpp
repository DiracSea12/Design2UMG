// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportPipeline.h"
#include "DesignImportDiagnosticReport.h"
#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGGenerationManifest.h"
#include "UMGSchemaSourceExtensions.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGLog.h"
#include "Design2UMGPrototypeNavigationTagHelper.h"
#include "Design2UMGSettings.h"
#include "DesignImportPlanning.h"
#include "UMGGenerator.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGValueParsing.h"
#include "UMGWidgetUtils.h"
#include "WidgetLayoutAdapter.h"
#include "WidgetResourceBinder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ContentWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"

namespace DesignImportPipeline
{
	namespace
	{
		// ---------------------------------------------------------------------------
		// 小工具
		// ---------------------------------------------------------------------------

		void AddReimportDiagnostic(
			TArray<FDesign2UMGMappingDiagnostic>& Diagnostics,
			const TCHAR* Kind,
			const FString& Message)
		{
			FDesign2UMGMappingDiagnostic Diagnostic;
			Diagnostic.Kind = Kind;
			Diagnostic.Message = Message;
			Diagnostics.Add(MoveTemp(Diagnostic));
			UE_LOG(LogDesign2UMG, Log, TEXT("Reimport[%s]: %s"), Kind, *Message);
		}

		/** 「真实设计节点」= 有源身份（SourceNodeId 非空）；合成包装层/直通层没有。 */
		bool HasRealSourceIdentity(const FUMGWidgetNode& Node)
		{
			return Node.HasSourceData() && !Node.Source().SourceNodeId.IsEmpty();
		}

		/** 与导入侧一致的 design 身份键（唯一真源 FUMGWidgetNode::GetStableDesignNodeId）。 */
		FString GetDesignNodeId(const FUMGWidgetNode& Node)
		{
			return Node.GetStableDesignNodeId();
		}

		const FUMGWidgetNode* FindNodeByDesignId(const FUMGWidgetNode& Node, const FString& DesignNodeId)
		{
			if (GetDesignNodeId(Node) == DesignNodeId)
			{
				return &Node;
			}
			for (const FUMGWidgetNode& Child : Node.Children)
			{
				if (const FUMGWidgetNode* Found = FindNodeByDesignId(Child, DesignNodeId))
				{
					return Found;
				}
			}
			return nullptr;
		}

		/** 在 Root 子树里找 Target 的父节点（按节点指针比较）。 */
		const FUMGWidgetNode* FindParentNode(const FUMGWidgetNode& Root, const FUMGWidgetNode& Target)
		{
			for (const FUMGWidgetNode& Child : Root.Children)
			{
				if (&Child == &Target)
				{
					return &Root;
				}
				if (const FUMGWidgetNode* Found = FindParentNode(Child, Target))
				{
					return Found;
				}
			}
			return nullptr;
		}

		/** IR 节点是不是「可实例化的 mapped 形态」：命中 mapping Key 且映射类当前能加载。 */
		bool IsMappedKind(
			const FUMGWidgetNode& Node,
			const FDesign2UMGComponentMappingConfig& MappingConfig,
			TMap<FString, bool>& MappedKindCache)
		{
			if (!Node.HasSourceData() || Node.Source().MappedComponentKey.IsEmpty())
			{
				return false;
			}

			const FString& Key = Node.Source().MappedComponentKey;
			if (bool* Cached = MappedKindCache.Find(Key))
			{
				return *Cached;
			}

			bool bMappedKind = false;
			if (const DesignComponentMapping::FComponentMappingEntry* Entry =
				MappingConfig.Table.FindEntryByKey(Key))
			{
				UClass* WidgetClass = FSoftClassPath(Entry->WidgetClass).TryLoadClass<UUserWidget>();
				bMappedKind = WidgetClass != nullptr && WidgetClass->IsChildOf<UUserWidget>();
			}
			MappedKindCache.Add(Key, bMappedKind);
			return bMappedKind;
		}

		bool IsMappedCandidateNode(const FUMGWidgetNode& Node)
		{
			return Node.HasSourceData() && !Node.Source().MappedComponentKey.IsEmpty();
		}

		/**
		 * 生成链补建新节点用的剪枝：把子树里已有的 manifest 实例节点剔掉。
		 * 否则「设计新增容器里装着旧节点」的 move 场景会先建一份重复 widget，
		 * 再由 reparent 搬旧 widget，出现双份。被剔掉的节点由 reparent 阶段挂回。
		 */
		void PruneManifestInstanceChildren(
			FUMGWidgetNode& Node,
			const TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>& InstancesById)
		{
			for (FUMGWidgetNode& Child : Node.Children)
			{
				PruneManifestInstanceChildren(Child, InstancesById);
			}
			Node.Children.RemoveAll([&InstancesById](const FUMGWidgetNode& Child)
			{
				return InstancesById.Contains(GetDesignNodeId(Child));
			});
		}

		/**
		 * 保存前置可写性检查（Perforce 未 checkout 的目标场景）：
		 * 文件存在且只读属性命中 = 不可写；文件不存在视为可写（保存时会新建，
		 * 没有可预判的只读状态）。必须前置拦截：引擎 SavePackage 对只读 uasset 走
		 * appError fatal，编辑器直接崩，无法在产品内变成诊断 + 回滚。
		 * 写探测（「追加模式打开后立即关闭」：不创建文件、不改内容，只验证写句柄
		 * 可得）仅对 sidecar 启用（bAllowWriteProbe）：uasset 已被本进程从磁盘加载时
		 * （reimport 主流程的常态），引擎在文件上持有的读句柄不带写共享，OpenWrite
		 * 探测必然失败——假阳性会拦掉本可成功的保存（引擎保存路径自身能写成功）。
		 * uasset 真正的外部锁 / IO 失败由 SavePackage 失败路径兜底（FailSaveWithRollback 已有）。
		 */
		bool IsFileWritableForSave(const FString& InPath, bool bAllowWriteProbe, FString& OutReason)
		{
			if (!IFileManager::Get().FileExists(*InPath))
			{
				return true;
			}
			if (IFileManager::Get().IsReadOnly(*InPath))
			{
				OutReason = FString::Printf(TEXT("'%s' is read-only"), *InPath);
				return false;
			}
			if (!bAllowWriteProbe)
			{
				return true;
			}
			IFileHandle* Probe = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(
				*InPath, /*bAppend=*/true, /*bAllowRead=*/false);
			if (!Probe)
			{
				OutReason = FString::Printf(TEXT("write probe failed on '%s'"), *InPath);
				return false;
			}
			delete Probe;
			return true;
		}

		/** 项目绑定签名：交互触发类型 -> UWidget 委托属性名（与生成链绑定的 runtime 委托一致）。 */
		FName GetInteractionDelegateProperty(const FUMGPrototypeNavigationBinding& Binding)
		{
			if (Binding.UsesDragTrigger())
			{
				return TEXT("OnButtonDragDetected");
			}
			if (Binding.UsesHoverTrigger())
			{
				return TEXT("OnHovered");
			}
			if (Binding.UsesPressTrigger())
			{
				return TEXT("OnPressed");
			}
			if (Binding.UsesKeyboardTrigger())
			{
				// 键盘交互由 D2U 扩展在 UserWidget 层接管；项目侧存在任一键盘事件绑定即视为已占位。
				return Binding.UsesKeyUpTrigger() ? TEXT("OnKeyUp") : TEXT("OnKeyDown");
			}
			return TEXT("OnClicked");
		}

		/** 同一交互是否已被项目绑定（WBP Bindings 挂了函数）。键盘触发匹配任意对象的键盘事件绑定。 */
		bool HasProjectBindingForInteraction(
			const UWidgetBlueprint* WBP,
			const FUMGPrototypeNavigationBinding& Binding)
		{
			const FName PropertyName = GetInteractionDelegateProperty(Binding);
			// 触发控件在 Bindings 里登记的是 widget FName（生成链对节点 id 做过 slug 清洗与
			// 数字前缀处理），不能拿原始节点 id（Figma 真实 id 含 ':' 等）直接比对；
			// 按同一清洗规则得到 base 名再匹配，与嵌套同名匹配的语义一致。
			const FString TriggerBaseName = FUMGWidgetUtils::SanitizeWidgetName(
				Binding.TriggerWidgetName, TEXT("Button")).ToString();
			for (const FDelegateEditorBinding& EditorBinding : WBP->Bindings)
			{
				if (Binding.UsesKeyboardTrigger())
				{
					if (EditorBinding.PropertyName == PropertyName)
					{
						return true;
					}
				}
				else if (FUMGWidgetUtils::MatchesWidgetBaseName(EditorBinding.ObjectName, TriggerBaseName)
					&& EditorBinding.PropertyName == PropertyName)
				{
					return true;
				}
			}
			return false;
		}

		void CollectSubtreeWidgets(UWidget* SubtreeRoot, TFunctionRef<void(UWidget*)> Visitor)
		{
			Visitor(SubtreeRoot);
			if (UPanelWidget* Panel = Cast<UPanelWidget>(SubtreeRoot))
			{
				for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
				{
					if (UWidget* Child = Panel->GetChildAt(Index))
					{
						CollectSubtreeWidgets(Child, Visitor);
					}
				}
			}
		}

		/** 子树里是否有任何 widget 被项目绑定引用（WBP Bindings 按 ObjectName 引用）。 */
		bool HasDeveloperBindingInSubtree(const UWidgetBlueprint* WBP, UWidget* SubtreeRoot)
		{
			if (!WBP || !SubtreeRoot)
			{
				return false;
			}

			TSet<FString> SubtreeNames;
			CollectSubtreeWidgets(SubtreeRoot, [&SubtreeNames](UWidget* Widget)
			{
				SubtreeNames.Add(Widget->GetFName().ToString());
			});

			for (const FDelegateEditorBinding& EditorBinding : WBP->Bindings)
			{
				if (SubtreeNames.Contains(EditorBinding.ObjectName))
				{
					return true;
				}
			}
			return false;
		}

		/**
		 * 子树里是否有任何 widget 被蓝图事件图表当变量引用（Get/Set 节点）。
		 * UWidget 默认 bIsVariable=true，用户无需绑定就能在 graph 里引用 D2U 控件；
		 * 引擎 DeleteWidgets(DeleteSilently) 内部按 FBlueprintEditorUtils::IsVariableUsed
		 * 的判定直接 RemoveVariableNodes——这里用同款查询做前置判定，保证判定信号
		 * 与删除路径实际移除的东西对称。
		 */
		bool HasGraphVariableReferenceInSubtree(const UWidgetBlueprint* WBP, UWidget* SubtreeRoot)
		{
			if (!WBP || !SubtreeRoot)
			{
				return false;
			}
			bool bReferenced = false;
			CollectSubtreeWidgets(SubtreeRoot, [&bReferenced, WBP](UWidget* Widget)
			{
				if (FBlueprintEditorUtils::IsVariableUsed(WBP, Widget->GetFName()))
				{
					bReferenced = true;
				}
			});
			return bReferenced;
		}

		/**
		 * 安全删除/身份转换的前置判定：子树必须纯 D2U-owned。
		 * 「项目内容」信号 = WBP Bindings 引用了子树内任何 widget，子树存在
		 * manifest.d2uOwned 名单之外的 widget（项目后加的子控件），或子树内
		 * widget 被蓝图 graph 当变量引用（静默删除会连带移除用户的蓝图节点）。
		 */
		bool IsSubtreePurelyD2UOwned(
			const UWidgetBlueprint* WBP,
			UWidget* SubtreeRoot,
			const TSet<FString>& D2UOwned,
			FString& OutReason)
		{
			if (HasDeveloperBindingInSubtree(WBP, SubtreeRoot))
			{
				OutReason = FString::Printf(
					TEXT("subtree of '%s' has developer bindings in the blueprint"),
					*SubtreeRoot->GetName());
				return false;
			}

			if (HasGraphVariableReferenceInSubtree(WBP, SubtreeRoot))
			{
				OutReason = FString::Printf(
					TEXT("subtree of '%s' is referenced by blueprint graphs (variable nodes); deleting it would remove project blueprint nodes"),
					*SubtreeRoot->GetName());
				return false;
			}

			TSet<FString> Unowned;
			CollectSubtreeWidgets(SubtreeRoot, [&Unowned, &D2UOwned](UWidget* Widget)
			{
				if (!D2UOwned.Contains(Widget->GetFName().ToString()))
				{
					Unowned.Add(Widget->GetFName().ToString());
				}
			});
			if (Unowned.Num() > 0)
			{
				OutReason = FString::Printf(
					TEXT("subtree of '%s' contains widgets outside the generation record (%s)"),
					*SubtreeRoot->GetName(),
					*FString::Join(Unowned.Array(), TEXT(", ")));
				return false;
			}
			return true;
		}

		/**
		 * 9a（removed）与 9b（kind-changed 替换旧形态）共用的安全整删：
		 * 收集子树 widget 名 → 静默删除 → 从 owned 名单移除。返回子树名集合
		 *（供诊断点名 / 后续收尾使用）。两处语义必须一致，收口防平行演化。
		 */
		TSet<FString> DeleteD2UOwnedSubtreeSilently(
			UWidgetBlueprint* WBP, UWidget* SubtreeRoot, TSet<FString>& InOutUpdatedD2UOwned)
		{
			TSet<FString> RemovedNames;
			CollectSubtreeWidgets(SubtreeRoot, [&RemovedNames](UWidget* RemovedWidget)
			{
				RemovedNames.Add(RemovedWidget->GetFName().ToString());
			});
			FWidgetBlueprintEditorUtils::DeleteWidgets(
				WBP, { SubtreeRoot }, FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
			for (const FString& RemovedName : RemovedNames)
			{
				InOutUpdatedD2UOwned.Remove(RemovedName);
			}
			return RemovedNames;
		}

		/**
		 * 9a/9b 整删前的第二道保守判定：子树里是否还挂着「本轮更新计划中仍存活」的
		 * manifest 实例 widget（Matched/KindChanged，即设计端新源里还存在的设计节点）。
		 * 整删这种子树会把存活实例连带销毁；之后 reparent/refresh 按 widgetName 再也
		 * 找不回它们（设计节点在资产里丢失 + manifest 出现无 widget 的幽灵记录）。
		 * 因此必须像含项目内容一样保守保留子树根，让实例走自己的 move/refresh 计划。
		 * ExcludeWidgetName 排除正在被处理的子树根本身（它的替换/删除就是本次意图）。
		 */
		bool CollectLiveInstancesInSubtree(
			UWidget* SubtreeRoot,
			const TSet<FString>& AliveInstanceWidgetNames,
			const FString& ExcludeWidgetName,
			TArray<FString>& OutLiveNames)
		{
			CollectSubtreeWidgets(SubtreeRoot, [&AliveInstanceWidgetNames, &ExcludeWidgetName, &OutLiveNames](UWidget* Widget)
			{
				const FString WidgetName = Widget->GetFName().ToString();
				if (WidgetName != ExcludeWidgetName && AliveInstanceWidgetNames.Contains(WidgetName))
				{
					OutLiveNames.AddUnique(WidgetName);
				}
			});
			return OutLiveNames.Num() > 0;
		}

		// ---------------------------------------------------------------------------
		// 更新计划
		// ---------------------------------------------------------------------------

		enum class EReimportPlanAction : uint8
		{
			Matched,      // designNodeId 两边都在且 kind 一致 -> 更新 D2U-owned 属性/布局（含 reparent 判定）
			KindChanged,  // 两边都在但 mapped<->generated 性质变化 -> 按身份转换规则处理
			Removed,      // manifest 有、新 IR 没有 -> 安全删除判定
		};

		struct FReimportPlanEntry
		{
			const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* ManifestRecord = nullptr;
			const FUMGWidgetNode* NewNode = nullptr;
			EReimportPlanAction Action = EReimportPlanAction::Matched;
			bool bNewKindMapped = false;
		};

		FDesign2UMGMappedInstanceRecord ManifestRecordToMappedRecord(
			const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record)
		{
			FDesign2UMGMappedInstanceRecord Mapped;
			Mapped.DesignNodeId = Record.DesignNodeId;
			Mapped.WidgetName = Record.WidgetName;
			Mapped.MappedKey = Record.MappedKey;
			Mapped.MappedClass = Record.MappedClass;
			Mapped.bMatchedAtSetLevel = Record.bMatchedAtSetLevel;
			Mapped.Inputs = Record.Inputs;
			return Mapped;
		}

		/**
		 * 跨阶段共享的 reimport 会话状态：早退函数与 HandleFullConflict 的公共入参
		 * 载体（对齐应用阶段 FReimportApplyContext 的做法，避免 Document/Request/
		 * Data/GenerationSettings 四件套在每个早退签名与每个 HandleFullConflict
		 * 调用点逐字重复）。GenerationSettings 在设置快照解析（步骤 1）后可用。
		 */
		struct FReimportSession
		{
			FDesignDocument& Document;
			const FDesignReimportRequest& Request;
			FUMGWidgetData& Data;
			const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings;
		};

		/**
		 * 全量冲突：保留资产 + 诊断；用户显式选择时生成并列副本（原资产与原 manifest 不动）。
		 * PriorDiagnostics 携带匹配阶段已产生的诊断（scope matched 等）：spec 要求先完成
		 * source/instance/ownership 匹配分类，这些诊断必须随结果一起返回，不能被丢弃。
		 */
		FDesignReimportResult HandleFullConflict(
			const FString& Reason,
			const FReimportSession& Session,
			TArray<FDesign2UMGMappingDiagnostic>&& PriorDiagnostics)
		{
			FDesignReimportResult Result;
			Result.Diagnostics = MoveTemp(PriorDiagnostics);
			// ini 校验诊断在快照解析（步骤 1）时已产生；早退报告必须带上，否则配置错误
			// 叠加资产级冲突时会从报告中消失。
			Result.ComponentMappingDiagnostics = Session.GenerationSettings.ComponentMapping.ValidationDiagnostics;
			const FString TargetPackage = FPackageName::ObjectPathToPackageName(Session.Request.TargetAssetPath);
			AddReimportDiagnostic(
				Result.Diagnostics,
				TEXT("reimport_full_conflict"),
				FString::Printf(
					TEXT("Target %s preserved untouched: %s"),
					*TargetPackage,
					*Reason));
			Result.bFullConflict = true;
			Result.bSucceeded = false;

			if (!Session.Request.bGenerateResolutionCopy)
			{
				DesignImportDiagnosticReport::WriteReimport(
					Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
				return Result;
			}

			const FString PackageDir = FPaths::GetPath(TargetPackage);
			const FString CopyName = FPaths::GetBaseFilename(TargetPackage) + TEXT("_Resolution");
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			if (ARM.Get().GetAssetByObjectPath(FSoftObjectPath(PackageDir / CopyName + TEXT(".") + CopyName)).IsValid())
			{
				AddReimportDiagnostic(
					Result.Diagnostics,
					TEXT("reimport_resolution_copy"),
					FString::Printf(
						TEXT("Resolution copy skipped: %s already exists"),
						*(PackageDir / CopyName)));
				DesignImportDiagnosticReport::WriteReimport(
					Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
				return Result;
			}

			Session.Data.WidgetName = CopyName;
			FDesign2UMGGenerationOutput GenerationOutput;
			UWidgetBlueprint* CopyBlueprint = FUMGGenerator::GenerateWidgetBlueprint(
				Session.Data, PackageDir, &Session.GenerationSettings, &GenerationOutput);
			if (CopyBlueprint)
			{
				Result.ResolutionCopyPath = CopyBlueprint->GetPathName();
				AddReimportDiagnostic(
					Result.Diagnostics,
					TEXT("reimport_resolution_copy"),
					FString::Printf(
						TEXT("Resolution copy generated at %s (original untouched); reason: %s"),
						*Result.ResolutionCopyPath,
						*Reason));
			}
			else
			{
				AddReimportDiagnostic(
					Result.Diagnostics,
					TEXT("reimport_resolution_copy"),
					FString::Printf(TEXT("Resolution copy generation failed for %s"), *(PackageDir / CopyName)));
			}
			// 副本生成链的 mapping 诊断（ini 校验、命中、副本自身 sidecar 写失败）随报告落盘。
			Result.ComponentMappingDiagnostics.Append(GenerationOutput.MappingDiagnostics);
			DesignImportDiagnosticReport::WriteReimport(
				Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
			return Result;
		}

		// ---------------------------------------------------------------------------
		// 应用阶段
		// ---------------------------------------------------------------------------

		struct FReimportApplyContext
		{
			UWidgetBlueprint* WBP = nullptr;
			UWidgetTree* WidgetTree = nullptr;
			UPanelWidget* HostPanel = nullptr;
			FIntPoint DesignSize = FIntPoint::ZeroValue;
			/** manifest.d2uOwned 名单的集合视图（删除/移动所有权判定的数据源）。 */
			const TSet<FString>* OwnedNames = nullptr;
			const TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>* InstancesById = nullptr;
			FDesign2UMGComponentMappingBuildState* MappingBuildState = nullptr;
			TArray<FDesign2UMGMappingDiagnostic>* Diagnostics = nullptr;
			TArray<FDesign2UMGMappedInstanceRecord>* UpdatedMappedRecords = nullptr;
			TArray<FDesign2UMGGeneratedInstanceRecord>* UpdatedGeneratedRecords = nullptr;
			/** 本轮新建 widget（designNodeId -> widget），reparent 目标端解析可引用。 */
			TMap<FString, UWidget*>* NewlyCreatedWidgets = nullptr;
			/**
			 * 嵌套结构 diff 的 move 候选池（主流程预扫描收集）：owned 名单内、非 manifest
			 * 实例、且新设计不再期望留在原容器的 widget。容器子树 diff 按确定性同名从池里
			 * 消费 move 源；未被消费的走删除残留诊断（纯 D2U 静默保留）。
			 */
			TArray<UWidget*>* NestedMoveCandidates = nullptr;
			/** 9a-10 共用的 owned 名单演变（初始 = manifest.d2uOwned，随删除/新增更新）。 */
			TSet<FString>* UpdatedD2UOwned = nullptr;
			/** 冲突保留名单（9a/9b 保留、10 回补 manifest 记录）。 */
			TArray<FString>* KeepAsIsWidgetNames = nullptr;
			/** 本轮更新计划中仍存活的 manifest 实例 widget 名（9a/9b 整删二道防线）。 */
			const TSet<FString>* AliveInstanceWidgetNames = nullptr;
		};

		void ApplySlotForNode(UWidget* Widget, const FUMGWidgetNode& Node, const FReimportApplyContext& Ctx)
		{
			if (!Widget || !Widget->Slot)
			{
				return;
			}
			if (UPanelWidget* ParentPanel = Cast<UPanelWidget>(Widget->GetParent()))
			{
				ParentPanel->Modify();
				Widget->Slot->Modify();
				const FUMGSlotData SlotData = FWidgetLayoutAdapter::Resolve(
					Node, ParentPanel, Ctx.WidgetTree, Ctx.DesignSize, false);
				FUMGWidgetUtils::ApplySlotProperties(Widget->Slot, SlotData);
			}
		}

		/**
		 * Mapped Class 变化冲突判定（9e 计划阶段与 RefreshMappedInstance walk 侧共用）：
		 * 实例实际类 != manifest 记录类，或映射当前指向的类 != 记录类，任一即冲突
		 *（项目替换了实例 / ini 改了指向）。当前映射条目查不到按空串参与比较。
		 */
		bool IsMappedClassConflict(
			const UWidget* Instance,
			const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record,
			const FString& CurrentMappedClass)
		{
			return Instance->GetClass()->GetPathName() != Record->MappedClass
				|| CurrentMappedClass != Record->MappedClass;
		}

		/**
		 * mapped 实例自身视觉属性（可见性/透明度）导入时随通用属性写入，但 reimport 按
		 * 「mapped 内部归项目」边界不刷新。设计声明值与实例现状出现分歧时点名报告；
		 * 已被显式 mapping input 覆写的属性跳过——声明值才是该属性的设计真意，且已刷新。
		 */
		void ReportMappedOwnAttrDivergence(
			const UUserWidget* Instance,
			const FUMGWidgetNode& Node,
			const TArray<FDesign2UMGMappedInputRecord>& InputRecords,
			TArray<FDesign2UMGMappingDiagnostic>& Diagnostics)
		{
			const auto bInputTargets = [&InputRecords](const TCHAR* PropertyName)
			{
				return InputRecords.ContainsByPredicate(
					[&PropertyName](const FDesign2UMGMappedInputRecord& Record)
					{
						return Record.Target == PropertyName;
					});
			};

			TArray<FString> Divergences;
			if (Node.WidgetStyle.Visibility.IsSet() && !bInputTargets(TEXT("Visibility")))
			{
				const ESlateVisibility DesignVisibility =
					FUMGValueParser::ParseVisibility(Node.WidgetStyle.Visibility.GetValue());
				if (Instance->GetVisibility() != DesignVisibility)
				{
					const UEnum* const VisibilityEnum = StaticEnum<ESlateVisibility>();
					Divergences.Add(FString::Printf(
						TEXT("visibility project='%s' design='%s'"),
						*VisibilityEnum->GetNameStringByValue(static_cast<int64>(Instance->GetVisibility())),
						*VisibilityEnum->GetNameStringByValue(static_cast<int64>(DesignVisibility))));
				}
			}
			if (Node.Render.RenderOpacity.IsSet() && !bInputTargets(TEXT("RenderOpacity"))
				&& !FMath::IsNearlyEqual(Instance->GetRenderOpacity(), Node.Render.RenderOpacity.GetValue()))
			{
				Divergences.Add(FString::Printf(
					TEXT("renderOpacity project=%.3f design=%.3f"),
					Instance->GetRenderOpacity(), Node.Render.RenderOpacity.GetValue()));
			}

			if (Divergences.Num() > 0)
			{
				AddReimportDiagnostic(Diagnostics, TEXT("reimport_mapped_attr_kept"),
					FString::Printf(
						TEXT("Mapped instance '%s' (%s) own visual attributes kept as project-side, not refreshed: %s"),
						*Instance->GetName(), *GetDesignNodeId(Node), *FString::Join(Divergences, TEXT("; "))));
			}
		}

		void RefreshMappedInstance(
			UUserWidget* Instance,
			const FUMGWidgetNode& Node,
			FReimportApplyContext& Ctx)
		{
			// Mapped Class 变化冲突的 walk 侧守卫：manifest 记录的 mapped 实例如果实例类与
			// 记录不一致、或映射当前指向别的类，就不刷新（4.4 冲突语义由计划阶段的
			// reimport_mapped_class_conflict 诊断负责，这里静默让位避免重复诊断）。
			if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* RecordPtr =
				Ctx.InstancesById->Find(GetDesignNodeId(Node)))
			{
				const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record = *RecordPtr;
				const DesignComponentMapping::FComponentMappingEntry* CurrentEntry =
					Ctx.MappingBuildState->FindEntryForKey(Node.Source().MappedComponentKey);
				const FString CurrentMappedClass = CurrentEntry ? CurrentEntry->WidgetClass : FString();
				if (Record->Kind == Design2UMGManifestKinds::Mapped
					&& IsMappedClassConflict(Instance, Record, CurrentMappedClass))
				{
					return;
				}
			}

			// 同一轮里同一实例可能被 9e 更新计划与父实例的嵌套对照各刷新一次
			//（switcher 宿主下的 mapped 实例两种途径都会到达）。去重判定放在副作用
			// 之前：第二次到达直接跳过，避免 slot/inputs 重复应用与重复的
			// mapping_input_applied 诊断（manifest 只落一条记录、只报一次刷新诊断）。
			const FString ProspectiveDesignNodeId = GetDesignNodeId(Node);
			const FString ProspectiveWidgetName = Instance->GetFName().ToString();
			if (Ctx.UpdatedMappedRecords->ContainsByPredicate(
				[&ProspectiveDesignNodeId, &ProspectiveWidgetName](const FDesign2UMGMappedInstanceRecord& Existing)
				{
					return Existing.DesignNodeId == ProspectiveDesignNodeId
						&& Existing.WidgetName == ProspectiveWidgetName;
				}))
			{
				return;
			}

			// mapped 实例只更新外层布局 + 显式 inputs（直接覆写），内部完全不动。
			Instance->Modify();
			ApplySlotForNode(Instance, Node, Ctx);

			const DesignComponentMapping::FComponentMappingEntry* Entry =
				Ctx.MappingBuildState->FindEntryForKey(Node.Source().MappedComponentKey);
			if (!Entry)
			{
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_input_skipped"),
					FString::Printf(
						TEXT("Mapping entry for key '%s' disappeared on reimport; inputs of '%s' left as-is"),
						*Node.Source().MappedComponentKey,
						*Instance->GetName()));
				return;
			}

			TArray<FDesign2UMGMappedInputRecord> InputRecords;
			Design2UMGComponentMappingGeneration::ApplyMappedComponentInputsToInstance(
				Instance, Node, *Entry, &InputRecords, *Ctx.Diagnostics);

			ReportMappedOwnAttrDivergence(Instance, Node, InputRecords, *Ctx.Diagnostics);

			FDesign2UMGMappedInstanceRecord Record;
			Record.DesignNodeId = ProspectiveDesignNodeId;
			Record.WidgetName = ProspectiveWidgetName;
			Record.MappedKey = Entry->Key;
			Record.MappedClass = Entry->WidgetClass;
			Record.bMatchedAtSetLevel = Ctx.MappingBuildState->IsMappedAtSetLevel(Node.Id);
			Record.Inputs = MoveTemp(InputRecords);

			Ctx.UpdatedMappedRecords->Add(MoveTemp(Record));

			AddReimportDiagnostic(
				*Ctx.Diagnostics, TEXT("reimport_updated"),
				FString::Printf(
					TEXT("Mapped instance '%s' (%s) refreshed: outer layout + declared inputs only; internals untouched"),
					*ProspectiveWidgetName, *ProspectiveDesignNodeId));
		}

		void RefreshNodeByKind(const FUMGWidgetNode& Node, UWidget* Widget, FReimportApplyContext& Ctx);

		/**
		 * move 的源端所有权判定（spec R3「both endpoints are D2U-owned」，顶层 reparent
		 * 与嵌套 move 共用同一规则）：
		 * 1) 源端父容器必须 D2U-owned（宿主根或 owned 名单内容器）——开发者把节点挂到
		 *    项目容器下 = 接管了它，挪走会破坏项目侧结构；
		 * 2) 源端子树必须纯 D2U-owned（名单判定 + 绑定检查，与顶层安全删除同一套规则）。
		 */
		bool IsMoveSourceOwned(const FReimportApplyContext& Ctx, UWidget* MoveSource, FString& OutReason)
		{
			UPanelWidget* SourceParent = MoveSource->GetParent();
			if (!SourceParent
				|| (SourceParent != Ctx.HostPanel
					&& !Ctx.OwnedNames->Contains(SourceParent->GetFName().ToString())))
			{
				OutReason = FString::Printf(
					TEXT("source sits under project-side container '%s'"),
					SourceParent ? *SourceParent->GetName() : TEXT("(none)"));
				return false;
			}
			return IsSubtreePurelyD2UOwned(Ctx.WBP, MoveSource, *Ctx.OwnedNames, OutReason);
		}

		/** MatchNestedChildren 的结果：设计子节点与既有 owned 子 widget 的确定性对照。 */
		struct FNestedChildrenMatch
		{
			struct FMatchedPair
			{
				const FUMGWidgetNode* Child = nullptr;
				UWidget* Widget = nullptr;
			};
			/** 配对成功（manifest 身份直配，或第 k 个同名设计子节点对后缀序第 k 个既有 widget）。 */
			TArray<FMatchedPair> Matched;
			/** 设计有、本地无对应 owned 候选（嵌套新增或嵌套 move 的目标端）。 */
			TArray<const FUMGWidgetNode*> UnmatchedDesignChildren;
			/** 本地 owned、设计不再期望（嵌套删除残留或嵌套 move 的源端）。 */
			TArray<UWidget*> UnmatchedOwnedWidgets;
		};

		/**
		 * 设计子节点是否「合成包装层」：转换器为承接布局/视觉而插入的中间层，
		 * 不是设计稿里的真实节点。两种形态：
		 * 1) 无源节点身份的层（canvas 容器的 <name>_inner、prototype 触发按钮包装的内容层等）；
		 * 2) 直通包装层（SizeBox 等）：包装自身复制了唯一子节点的源身份（design id 相同），
		 *    语义上包装与内容就是同一个设计节点。
		 * 包装层不落地为独立匹配层：配对失败时按内容穿透继续匹配，
		 * 避免嵌套同名匹配断在合成层（move 丢失、重复建副本、无冲突诊断）。
		 */
		bool IsSyntheticWrapperNode(const FUMGWidgetNode& Node)
		{
			if (!HasRealSourceIdentity(Node))
			{
				return true;
			}
			return Node.Children.Num() == 1
				&& GetDesignNodeId(Node) == GetDesignNodeId(Node.Children[0]);
		}

		/**
		 * manifest 实例优先按记录直配（MatchNestedChildren 单轮内的单个子节点尝试）：
		 * mapped 实例的 widget 名来自导入期设计名，但 reimport 期 IR 侧同名节点可能是
		 * 转换器改写过名字的包装形态（如 switcher 文档下触发节点以 id 命名），同名匹配
		 * 对不上——身份判定必须以 manifest 记录为准，否则会把已存在的实例当新增再建一份。
		 * 返回 true = 已终态处理（配对成功，或记录 widget 挂在别的父下由 reparent 阶段接管）；
		 * 返回 false = 无可用记录，落到同名 base 匹配。
		 */
		bool TryMatchChildByManifestRecord(
			const FUMGWidgetNode& Child,
			UPanelWidget* Panel,
			const FReimportApplyContext& Ctx,
			TArray<UWidget*>& OwnedChildren,
			FNestedChildrenMatch& Out)
		{
			const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* RecordPtr =
				Ctx.InstancesById->Find(GetDesignNodeId(Child));
			if (!RecordPtr)
			{
				return false;
			}
			UWidget* Existing = Ctx.WidgetTree->FindWidget(FName(*(*RecordPtr)->WidgetName));
			if (!Existing)
			{
				return false;
			}
			if (Existing->GetParent() != Panel)
			{
				return true;
			}
			if (OwnedChildren.Contains(Existing))
			{
				Out.Matched.Add({ &Child, Existing });
				OwnedChildren.RemoveSingle(Existing);
				return true;
			}
			return false;
		}

		/**
		 * 同名 base 匹配（MatchNestedChildren 单轮内的单个子节点尝试）：第 k 个同名
		 * （base 名）设计子节点对到后缀序第 k 个既有 widget，与导入期
		 * SanitizeWidgetName + _N 去重的生成顺序一致。返回是否配对成功；
		 * 候选耗尽时由调用方归入本轮 unmatched。
		 */
		bool TryMatchChildByBaseName(
			const FUMGWidgetNode& Child,
			TMap<FString, int32>& BaseConsumed,
			TArray<UWidget*>& OwnedChildren,
			FNestedChildrenMatch& Out)
		{
			const FString BaseName = FUMGWidgetUtils::SanitizeWidgetName(Child.Name, Child.Type).ToString();
			const int32 Consumed = BaseConsumed.FindOrAdd(BaseName);
			BaseConsumed[BaseName] = Consumed + 1;

			TArray<UWidget*> Candidates;
			for (UWidget* OwnedChild : OwnedChildren)
			{
				if (FUMGWidgetUtils::MatchesWidgetBaseName(OwnedChild->GetFName().ToString(), BaseName))
				{
					Candidates.Add(OwnedChild);
				}
			}
			Candidates.Sort([&BaseName](const UWidget& A, const UWidget& B)
			{
				return FUMGWidgetUtils::GetWidgetNameSuffixRank(A.GetFName().ToString(), BaseName)
					< FUMGWidgetUtils::GetWidgetNameSuffixRank(B.GetFName().ToString(), BaseName);
			});

			if (Consumed < Candidates.Num())
			{
				Out.Matched.Add({ &Child, Candidates[Consumed] });
				OwnedChildren.RemoveSingle(Candidates[Consumed]);
				return true;
			}
			return false;
		}

		/**
		 * 容器子树的确定性同名对照（只算不动）。执行遍历（RefreshChildWidgets）与
		 * 预扫描（CollectExpectedNestedNames）共用这一份计算，保证两遍判定一致。
		 */
		FNestedChildrenMatch MatchNestedChildren(const FUMGWidgetNode& Node, UPanelWidget* Panel, const FReimportApplyContext& Ctx)
		{
			FNestedChildrenMatch Out;

			// 只认 owned 名单里的子 widget 参与对照；项目侧新增的不参与。
			TArray<UWidget*> OwnedChildren;
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (UWidget* Child = Panel->GetChildAt(Index))
				{
					if (Ctx.OwnedNames->Contains(Child->GetFName().ToString()))
					{
						OwnedChildren.Add(Child);
					}
				}
			}

			// 设计子节点从直接子节点起步；每轮配对后，把仍未配对的合成包装层按内容展平
			// 进下一轮（包装链深度有限，必然收敛）。已配对的 widget 立即从候选池移除，
			// 跨轮不会重复配对。
			TArray<const FUMGWidgetNode*> PendingChildren;
			for (const FUMGWidgetNode& Child : Node.Children)
			{
				PendingChildren.Add(&Child);
			}

			while (true)
			{
				TArray<const FUMGWidgetNode*> RoundUnmatched;
				TMap<FString, int32> BaseConsumed;
				for (const FUMGWidgetNode* Child : PendingChildren)
				{
					if (!TryMatchChildByManifestRecord(*Child, Panel, Ctx, OwnedChildren, Out)
						&& !TryMatchChildByBaseName(*Child, BaseConsumed, OwnedChildren, Out))
					{
						RoundUnmatched.Add(Child);
					}
				}

				// 展平轮：未配对的合成包装层换成它的内容子节点再配对。
				// 无内容的合成层（justify spacer 等）保持 unmatched，走原有补建路径。
				TArray<const FUMGWidgetNode*> NextPending;
				bool bFlattened = false;
				for (const FUMGWidgetNode* Child : RoundUnmatched)
				{
					if (IsSyntheticWrapperNode(*Child) && Child->Children.Num() > 0)
					{
						for (const FUMGWidgetNode& GrandChild : Child->Children)
						{
							NextPending.Add(&GrandChild);
						}
						bFlattened = true;
					}
					else
					{
						NextPending.Add(Child);
					}
				}
				if (!bFlattened)
				{
					Out.UnmatchedDesignChildren = MoveTemp(NextPending);
					break;
				}
				PendingChildren = MoveTemp(NextPending);
			}
			Out.UnmatchedOwnedWidgets = MoveTemp(OwnedChildren);
			return Out;
		}

		/**
		 * 预扫描：模拟 9e 的嵌套递归刷新，收集「新设计仍期望留在原容器」的 owned
		 * widget 名。不在集合内的 owned 非实例 widget = 设计未期望（删除残留或
		 * 嵌套 move 源），进 move 候选池。
		 */
		void CollectExpectedNestedNames(
			const FUMGWidgetNode& Node,
			UPanelWidget* Panel,
			const FReimportApplyContext& Ctx,
			TSet<FString>& OutExpected)
		{
			const FNestedChildrenMatch Match = MatchNestedChildren(Node, Panel, Ctx);
			for (const FNestedChildrenMatch::FMatchedPair& Pair : Match.Matched)
			{
				OutExpected.Add(Pair.Widget->GetFName().ToString());
				// mapped 候选不展开子树（RefreshMappedInstance 不递归 children）。
				if (!IsMappedCandidateNode(*Pair.Child))
				{
					if (UPanelWidget* ChildPanel = Cast<UPanelWidget>(Pair.Widget))
					{
						CollectExpectedNestedNames(*Pair.Child, ChildPanel, Ctx, OutExpected);
					}
				}
			}
		}

		/**
		 * 嵌套 move 判定与执行（RefreshChildWidgets 的 unmatched 子节点路径）：
		 * 候选池里有别处已不被设计期望的同名 owned 子树时，优先整体挪过来
		 * （identity 保持）。mapped 候选不走 move（实例化语义，直接补建）。
		 * 返回 true = 已处理（挪动成功/挪动被拒/挪动失败回挂，均不再补建）；
		 * 返回 false = 池中无候选，走生成链补建。
		 */
		bool TryNestedMoveChild(
			const FUMGWidgetNode& Child,
			const FString& BaseName,
			UPanelWidget* Panel,
			UWidget* ParentWidget,
			FReimportApplyContext& Ctx)
		{
			UWidget* MoveSource = nullptr;
			if (!IsMappedCandidateNode(Child) && Ctx.NestedMoveCandidates)
			{
				for (int32 Index = 0; Index < Ctx.NestedMoveCandidates->Num(); ++Index)
				{
					UWidget* Candidate = (*Ctx.NestedMoveCandidates)[Index];
					if (FUMGWidgetUtils::MatchesWidgetBaseName(Candidate->GetFName().ToString(), BaseName))
					{
						MoveSource = Candidate;
						Ctx.NestedMoveCandidates->RemoveAt(Index);
						break;
					}
				}
			}
			if (!MoveSource)
			{
				return false;
			}

			FString ConflictReason;
			if (!IsMoveSourceOwned(Ctx, MoveSource, ConflictReason))
			{
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_move_conflict"),
					FString::Printf(
						TEXT("Nested move of '%s' into '%s' skipped: %s; widget kept in place, no copy created"),
						*MoveSource->GetName(), *ParentWidget->GetName(), *ConflictReason));
				return true;
			}

			UPanelWidget* OldParent = MoveSource->GetParent();
			if (OldParent)
			{
				OldParent->Modify();
			}
			MoveSource->Modify();
			Panel->Modify();
			Ctx.WidgetTree->RemoveWidget(MoveSource);
			if (Panel->AddChild(MoveSource))
			{
				// 挪动后按新设计节点刷新属性/slot 与整棵子树（含嵌套诊断）。
				RefreshNodeByKind(Child, MoveSource, Ctx);
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_moved"),
					FString::Printf(
						TEXT("Widget '%s' moved from '%s' under '%s' (nested, both endpoints D2U-owned); widget identity preserved"),
						*MoveSource->GetName(),
						OldParent ? *OldParent->GetName() : TEXT("(none)"),
						*ParentWidget->GetName()));
			}
			else
			{
				// 防御：加不回去就挂回原父（源端已判定 D2U-owned），绝不允许孤儿 widget。
				if (OldParent)
				{
					OldParent->AddChild(MoveSource);
				}
				else if (Ctx.HostPanel)
				{
					Ctx.HostPanel->AddChild(MoveSource);
				}
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_move_conflict"),
					FString::Printf(
						TEXT("Nested move of '%s' into '%s' failed to attach; widget restored under its previous parent"),
						*MoveSource->GetName(), *ParentWidget->GetName()));
			}
			return true;
		}

		/**
		 * 设计新增的嵌套节点走生成链补建（RefreshChildWidgets 的 unmatched 子节点路径）：
		 * mapped 候选照常实例化，名字进新 d2uOwned；
		 * 子树里已有的 manifest 实例剪掉（由 reparent 挂回）。
		 */
		void CreateNestedChildWidget(
			const FUMGWidgetNode& Child,
			UPanelWidget* Panel,
			UWidget* ParentWidget,
			FReimportApplyContext& Ctx)
		{
			FUMGWidgetNode PrunedChild = Child;
			PruneManifestInstanceChildren(PrunedChild, *Ctx.InstancesById);
			UWidget* CreatedWidget = FUMGWidgetUtils::CreateWidgetRecursive(
				Ctx.WidgetTree, PrunedChild, Panel, nullptr, nullptr, &Ctx.DesignSize, Ctx.MappingBuildState);
			if (CreatedWidget)
			{
				if (Ctx.NewlyCreatedWidgets)
				{
					Ctx.NewlyCreatedWidgets->Add(GetDesignNodeId(Child), CreatedWidget);
				}
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_added"),
					FString::Printf(
						TEXT("New design node '%s' (%s) added under '%s'"),
						*Child.Name, *GetDesignNodeId(Child), *ParentWidget->GetName()));
			}
		}

		/**
		 * matched 生成子树的属性刷新：按「确定性命名」把新 IR 节点对到既有 widget。
		 * 只动 owned 名单内的 widget（项目侧新增/改名的不碰）；
		 * 设计消失的嵌套子树保留现状（诊断由主流程 9g 统一处理：含项目内容报
		 * remove 冲突，纯 D2U 静默保留）；
		 * 设计新增的嵌套节点先查嵌套 move 候选（spec R3：两端 D2U-owned 时挪 widget
		 * 保 identity、源端含项目内容时冲突不建副本），查不到才走生成链补建。
		 */
		void RefreshChildWidgets(const FUMGWidgetNode& Node, UWidget* Widget, FReimportApplyContext& Ctx)
		{
			UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
			if (!Panel)
			{
				return;
			}

			const FNestedChildrenMatch Match = MatchNestedChildren(Node, Panel, Ctx);
			for (const FNestedChildrenMatch::FMatchedPair& Pair : Match.Matched)
			{
				RefreshNodeByKind(*Pair.Child, Pair.Widget, Ctx);
			}

			for (const FUMGWidgetNode* Child : Match.UnmatchedDesignChildren)
			{
				const FString BaseName = FUMGWidgetUtils::SanitizeWidgetName(Child->Name, Child->Type).ToString();
				if (!TryNestedMoveChild(*Child, BaseName, Panel, Widget, Ctx))
				{
					CreateNestedChildWidget(*Child, Panel, Widget, Ctx);
				}
			}
		}

		void RefreshNodeByKind(const FUMGWidgetNode& Node, UWidget* Widget, FReimportApplyContext& Ctx)
		{
			if (IsMappedCandidateNode(Node))
			{
				if (UUserWidget* MappedInstance = Cast<UUserWidget>(Widget))
				{
					RefreshMappedInstance(MappedInstance, Node, Ctx);
					return;
				}
			}

			Widget->Modify();
			ApplySlotForNode(Widget, Node, Ctx);
			FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);
			if (UImage* ImageWidget = Cast<UImage>(Widget))
			{
				FWidgetResourceBinder::BindImageResource(ImageWidget, Node, nullptr);
			}
			RefreshChildWidgets(Node, Widget, Ctx);

			// 顶层生成关系（挂在宿主根下）才进 manifest 实例；嵌套节点只刷新属性。
			if (Ctx.HostPanel && Widget->GetParent() == Ctx.HostPanel)
			{
				FDesign2UMGGeneratedInstanceRecord Record;
				Record.DesignNodeId = GetDesignNodeId(Node);
				Record.WidgetName = Widget->GetFName().ToString();
				Ctx.UpdatedGeneratedRecords->Add(MoveTemp(Record));
			}
			// 属性刷新诊断与顶层同粒度：嵌套 D2U-owned 节点的刷新也逐节点点名。
			AddReimportDiagnostic(
				*Ctx.Diagnostics, TEXT("reimport_updated"),
				FString::Printf(
					TEXT("D2U-owned node '%s' (%s) refreshed from design (layout/slot/visual/text)"),
					*Node.Name, *GetDesignNodeId(Node)));
		}

		void ApplyPrototypeRefresh(
			FUMGWidgetData& Data,
			const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings,
			FReimportApplyContext& Ctx)
		{
			if (GenerationSettings.PrototypeNavigationGenerationMode == EDesign2UMGPrototypeNavigationGenerationMode::Disabled
				|| !Data.HasPrototypeNavigationData())
			{
				return;
			}

			FUMGPrototypeNavigationData FilteredNavigationData = Data.PrototypeNavigation();

			// 双触发冲突检测（更新计划阶段）：同一交互已有项目绑定 -> D2U 绑定让位，保留项目行为。
			TArray<FUMGPrototypeNavigationBinding> KeptBindings;
			for (const FUMGPrototypeNavigationBinding& Binding : FilteredNavigationData.ClickBindings)
			{
				if (HasProjectBindingForInteraction(Ctx.WBP, Binding))
				{
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_prototype_conflict"),
						FString::Printf(
							TEXT("Interaction (trigger '%s', type '%s') already bound by the project; project behavior kept, D2U prototype binding yields"),
							*Binding.TriggerWidgetName,
							*Binding.TriggerType));
					continue;
				}
				KeptBindings.Add(Binding);
			}
			FilteredNavigationData.ClickBindings = MoveTemp(KeptBindings);

			// mapped 身份上的设计交互不生效（与导入期同一条边界规则）；专门条目同时
			// 进 reimport 报告（reimport 不读 BuildState 的生成期诊断，须显式带出）。
			// mapping_ 前缀诊断的单一归属是 componentMapping 章节（QA 分类契约）：这里只走
			// BuildState，出参传 nullptr，主流程随后统一把 BuildState 诊断收进
			// Result.ComponentMappingDiagnostics，不落 reimport 章节。
			Design2UMGComponentMappingGeneration::ReportInertPrototypeBindings(
				FilteredNavigationData, *Ctx.MappingBuildState, nullptr);

			switch (GenerationSettings.PrototypeNavigationGenerationMode)
			{
			case EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension:
				// 只刷新绑定数据，不重建转场动画资产（避免每次 reimport 复制一份动画）。
				UMGPrototypeAnimationBuilder::ConfigureNavigationExtension(Ctx.WBP, FilteredNavigationData);
				break;
			case EDesign2UMGPrototypeNavigationGenerationMode::TagHelper:
				Design2UMGPrototypeNavigationTagHelper::ConfigureMetadata(Ctx.WBP, FilteredNavigationData);
				break;
			default:
				break;
			}
			AddReimportDiagnostic(
				*Ctx.Diagnostics, TEXT("reimport_prototype_refreshed"),
				FString::Printf(
					TEXT("D2U prototype bindings refreshed by stable design identity (%d binding(s) active)"),
					FilteredNavigationData.ClickBindings.Num()));
		}

		// ---------------------------------------------------------------------------
		// Reimport 阶段函数：主流程（下方 Reimport()）按步骤 1)-11) 顺序编排这些函数
		// ---------------------------------------------------------------------------

		/** 步骤 1)-3)：设置快照 / 重新解析源 / 定位目标资产 + 读 manifest；失败即全量冲突早退。 */
		TOptional<FDesignReimportResult> ResolveReimportSourceAndManifest(
			FDesignDocument& Document,
			const FDesignReimportRequest& Request,
			FDesignReimportResult& Result,
			FDesign2UMGImportSettingsSnapshot& SettingsSnapshot,
			FUMGWidgetData& Data,
			FString& TargetPackage,
			UWidgetBlueprint*& WBP,
			FDesign2UMGGenerationManifestSidecar& Manifest)
		{
			// 1) 解析设置快照（与导入路径同源：ini 校验诊断随快照进生成链）。
			SettingsSnapshot =
				Request.SettingsSnapshot.IsSet()
					? Request.SettingsSnapshot.GetValue()
					: FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
			const FReimportSession Session{ Document, Request, Data, SettingsSnapshot.GenerationSettings };

			// 2) 重新解析源（图片解析 + Convert 出新 IR）。
			FImportAnalysisInput AnalysisInput;
			AnalysisInput.bResolveImages = true;
			AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
			const FImportAnalysisResult AnalysisResult = AnalyzeImport(Document, AnalysisInput);
			Data = AnalysisResult.Data;

			// 3) 定位目标资产 + 读 manifest。
			TargetPackage = FPackageName::ObjectPathToPackageName(Request.TargetAssetPath);
			WBP = LoadObject<UWidgetBlueprint>(
				nullptr, *(TargetPackage + TEXT(".") + FPaths::GetBaseFilename(TargetPackage)));
			if (!WBP)
			{
				return HandleFullConflict(
					FString::Printf(TEXT("target asset failed to load: %s"), *TargetPackage),
					Session, MoveTemp(Result.Diagnostics));
			}

			FString ManifestError;
			if (!Design2UMGGenerationManifest::TryLoadSidecarForAsset(TargetPackage, Manifest, ManifestError))
			{
				return HandleFullConflict(ManifestError, Session, MoveTemp(Result.Diagnostics));
			}
			return {};
		}

		/** 步骤 4)-5)：DocumentScope 匹配、源根定位与宿主延续；失败即全量冲突早退。 */
		TOptional<FDesignReimportResult> MatchReimportScopeAndRoot(
			const FReimportSession& Session,
			const FDesign2UMGGenerationManifestSidecar& Manifest,
			FDesignReimportResult& Result,
			const FUMGWidgetNode*& ScopeRoot,
			bool& bPrototypeHostContinued)
		{
			const FUMGWidgetData& Data = Session.Data;
			// 4) DocumentScope 匹配：provider + scope 任一不一致 = 关系不清，全量冲突。
			const FString NewProvider = Data.HasSourceDocumentData() ? Data.SourceDocument().Provider : FString();
			const FString NewScope = Data.HasSourceDocumentData() ? Data.SourceDocument().DocumentScope : FString();
			if (!NewProvider.Equals(Manifest.Provider, ESearchCase::IgnoreCase)
				|| !NewScope.Equals(Manifest.DocumentScope, ESearchCase::IgnoreCase))
			{
				return HandleFullConflict(
					FString::Printf(
						TEXT("document scope mismatch: manifest provider='%s' scope='%s', new source provider='%s' scope='%s'"),
						*Manifest.Provider, *Manifest.DocumentScope, *NewProvider, *NewScope),
					Session, MoveTemp(Result.Diagnostics));
			}

			// 5) 在新 IR 里定位 manifest 记录的 sourceRoot（页面/根节点关系）。
			// sourceRoot 身份为空 = 单页交互文档的合成 switcher 宿主根（只有多页才发
			// 指纹 token id，见 BuildAnalysis 的 PrototypePages.Num() > 1 分支）——这是
			// 现行身份规则而非旧 sidecar 兼容，空值时直接以新 IR 根为 scope。
			ScopeRoot = Manifest.SourceRootNodeId.IsEmpty()
				? &Data.RootWidget
				: FindNodeByDesignId(Data.RootWidget, Manifest.SourceRootNodeId);
			// 宿主延续：多页原型的合成宿主根 id 是页集合内容指纹（BuildPrototypeInternalTokenHash =
			// CRC32(每页 "id|name;" 串接)），增/删页必然改写指纹，但内容指纹不是文档身份——
			// provider + documentScope 已在第 4 步匹配，且新源本身仍是多页原型时，这是同一
			// 宿主的页集合演化而非「关系不清晰」：不因指纹不同拒绝，以新源实际宿主根为 scope
			// 继续既有结构 diff（增页走 added、删页走安全删除），成功后 manifest 记录新指纹。
			// 判定收窄到「新旧宿主同为 switcher 本体」：记录 id 带 _root 后缀 = overlay 包裹根，
			// 新根 design id 必须正是新源的 SwitcherWidgetId（= switcher 本体合成根）。overlay
			// 包裹根的非实例 children 是合成层（switcher/overlay host）而非页面，混入会当成
			// 顶层新增挂错层级；宿主形态翻转（overlay 出现/消失）与降级单页一样维持全量冲突
			// 的保守语义（后者为切片 C CASE-011 已定性行为）。
			bPrototypeHostContinued = !ScopeRoot
				&& Manifest.SourceRootNodeId.StartsWith(Design2UMGPrototypeTokens::SwitcherRootPrefix())
				&& !Manifest.SourceRootNodeId.EndsWith(Design2UMGPrototypeTokens::OverlayRootSuffix())
				&& Data.HasPrototypeNavigationData()
				&& Data.PrototypeNavigation().IsValid()
				&& Data.PrototypeNavigation().SwitcherWidgetId == GetDesignNodeId(Data.RootWidget);
			if (bPrototypeHostContinued)
			{
				ScopeRoot = &Data.RootWidget;
				AddReimportDiagnostic(
					Result.Diagnostics, TEXT("reimport_prototype_host_continued"),
					FString::Printf(
						TEXT("Prototype host root identity renewed: recorded '%s' not found in the new source (page-set content fingerprint changed); provider/scope still match and the new source remains a multi-page prototype, continuing on the new host root '%s'"),
						*Manifest.SourceRootNodeId, *GetDesignNodeId(Data.RootWidget)));
			}
			if (!ScopeRoot)
			{
				return HandleFullConflict(
					FString::Printf(
						TEXT("recorded source root '%s' (%s) not found in the new source"),
						*Manifest.SourceRootName, *Manifest.SourceRootNodeId),
					Session, MoveTemp(Result.Diagnostics));
			}

			AddReimportDiagnostic(
				Result.Diagnostics, TEXT("reimport_scope_matched"),
				FString::Printf(
					TEXT("Source matched: provider=%s documentScope=%s sourceRoot=%s"),
					*Manifest.Provider, *Manifest.DocumentScope, *Manifest.SourceRootNodeId));
			return {};
		}

		/** 步骤 6)-7)：Component Mapping 两级匹配（IR 副本）+ 更新计划构建。 */
		void BuildReimportUpdatePlan(
			const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings,
			const FUMGWidgetData& Data,
			const FDesign2UMGGenerationManifestSidecar& Manifest,
			const FUMGWidgetNode* ScopeRoot,
			FUMGWidgetNode& PreparedScopeRoot,
			FDesign2UMGComponentMappingBuildState& MappingBuildState,
			TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>& InstancesById,
			TArray<FReimportPlanEntry>& Plan,
			TSet<FString>& AliveInstanceWidgetNames)
		{
			// 6) Component Mapping：生成前对 IR 副本做两级匹配（命中 Key 写进副本，不动源数据）。
			PreparedScopeRoot = *ScopeRoot;
			Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
				PreparedScopeRoot, GenerationSettings.ComponentMapping, Data, MappingBuildState);

			// 7) 更新计划：manifest 实例 <-> 新 IR 节点按 designNodeId 对照。
			TMap<FString, bool> MappedKindCache;
			for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Manifest.Instances)
			{
				InstancesById.Add(Record.DesignNodeId, &Record);

				FReimportPlanEntry Entry;
				Entry.ManifestRecord = &Record;
				Entry.NewNode = FindNodeByDesignId(PreparedScopeRoot, Record.DesignNodeId);
				Entry.bNewKindMapped = Entry.NewNode
					? IsMappedKind(*Entry.NewNode, GenerationSettings.ComponentMapping, MappedKindCache)
					: false;

				if (!Entry.NewNode)
				{
					Entry.Action = EReimportPlanAction::Removed;
				}
				else if (Entry.bNewKindMapped != (Record.Kind == Design2UMGManifestKinds::Mapped))
				{
					Entry.Action = EReimportPlanAction::KindChanged;
				}
				else
				{
					Entry.Action = EReimportPlanAction::Matched;
				}
				Plan.Add(Entry);
			}

			// 本轮更新计划中仍存活的实例 widget 名（Matched/KindChanged = 设计端新源里还在）。
			// 9a/9b 整删子树前用它做第二道保守判定，防止把存活实例连带删掉（见函数注释）。
			for (const FReimportPlanEntry& Entry : Plan)
			{
				if (Entry.Action != EReimportPlanAction::Removed)
				{
					AliveInstanceWidgetNames.Add(Entry.ManifestRecord->WidgetName);
				}
			}
		}

		/** 步骤 8)：结构健全性检查；失败即全量冲突早退。 */
		TOptional<FDesignReimportResult> CheckReimportStructureViability(
			const FReimportSession& Session,
			UWidgetBlueprint* WBP,
			const FDesign2UMGGenerationManifestSidecar& Manifest,
			const FUMGWidgetNode& PreparedScopeRoot,
			FDesignReimportResult& Result,
			FIntPoint& DesignSize)
		{
			// 8) 结构健全性：目标树缺失 / 连一个 manifest 实例都定位不到 = 项目侧重构，全量冲突。
			if (!WBP->WidgetTree || !WBP->WidgetTree->RootWidget)
			{
				return HandleFullConflict(
					TEXT("target widget tree is missing a root widget"),
					Session, MoveTemp(Result.Diagnostics));
			}
			int32 LocatedInstances = 0;
			for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Manifest.Instances)
			{
				LocatedInstances += WBP->WidgetTree->FindWidget(FName(*Record.WidgetName)) ? 1 : 0;
			}
			if (LocatedInstances == 0)
			{
				return HandleFullConflict(
					TEXT("none of the recorded instances could be located in the target widget tree (project restructure suspected)"),
					Session, MoveTemp(Result.Diagnostics));
			}

			DesignSize = FIntPoint(
				FMath::CeilToInt(PreparedScopeRoot.Bounds.W),
				FMath::CeilToInt(PreparedScopeRoot.Bounds.H));
			if (DesignSize.X <= 1 || DesignSize.Y <= 1)
			{
				return HandleFullConflict(
					TEXT("new source did not provide a valid root design size"),
					Session, MoveTemp(Result.Diagnostics));
			}
			return {};
		}

		/** 步骤 9) 事务前置：开编辑器事务 + 给既有树补 RF_Transactional；返回事务索引（INDEX_NONE = 无事务）。 */
		int32 BeginReimportApplyTransaction(UWidgetBlueprint* WBP)
		{
			// 注意：FScopedTransaction::Cancel 只丢弃事务记录、不回滚对象；要真正回滚必须
			// End 之后显式 Undo。这里用裸 Begin/End 管理，保存失败路径才能恢复资产原状态。
			// 嵌套防御：编辑器事务是引用计数嵌套——若外部已有活动事务，我们 Begin 出来的
			// 不是独立 undo 条目（End 只递减计数），此时 End 后 UndoTransaction 撤销的是
			// undo 栈里上一个无关的用户事务。因此外部事务活动期间不开自己的事务（Modify
			// 快照照常进入外部事务，由外部事务统一撤销）。
			const bool bCanOwnTransaction = GEditor && GEditor->CanTransact() && !GEditor->IsTransactionActive();
			const int32 TransactionIndex = bCanOwnTransaction
				? GEditor->BeginTransaction(
					TEXT("Design2UMG Reimport"),
					NSLOCTEXT("Design2UMG", "SafeReimportTransaction", "Design2UMG Safe Reimport"),
					WBP)
				: INDEX_NONE;

			// 生成链建的 widget 默认不带 RF_Transactional，事务捕获不到 Modify 快照，
			// 保存失败时的回滚就还原不了属性（与 UMG 编辑器自己的删除/编辑流程一致需要显式补标记）。
			if (WBP->WidgetTree)
			{
				WBP->WidgetTree->SetFlags(RF_Transactional);
				WBP->WidgetTree->ForEachWidget([](UWidget* TreeWidget)
				{
					TreeWidget->SetFlags(RF_Transactional);
				});
			}
			return TransactionIndex;
		}

		void ApplyRemovedPlanEntries(
			const TArray<FReimportPlanEntry>& Plan,
			FReimportApplyContext& Ctx)
		{
			// 9a) removed：安全删除判定（纯 D2U-owned 才删，含项目内容保留 + 冲突诊断）。
			for (const FReimportPlanEntry& Entry : Plan)
			{
				if (Entry.Action != EReimportPlanAction::Removed)
				{
					continue;
				}

				UWidget* Widget = Ctx.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
				if (!Widget)
				{
					// 资产里已经不存在：只从新 manifest 记录中移除。
					Ctx.UpdatedD2UOwned->Remove(Entry.ManifestRecord->WidgetName);
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_removed"),
						FString::Printf(
							TEXT("Design node '%s' (%s) removed from source; widget already absent from the tree"),
							*Entry.ManifestRecord->WidgetName, *Entry.ManifestRecord->DesignNodeId));
					continue;
				}

				// 保守判定 1（设计侧信号）：子树里挂着本轮仍存活的实例 -> 不能整删。
				// 常见场景：设计删掉旧容器、把内容挪进新容器；整删旧容器会连带销毁
				// 内容实例，之后 reparent 就再也找不回它们。保留子树根，让实例走
				// 自己的 move 计划挂到新容器下（identity-preserving move）。
				TArray<FString> LiveNames;
				if (CollectLiveInstancesInSubtree(
					Widget, *Ctx.AliveInstanceWidgetNames, Entry.ManifestRecord->WidgetName, LiveNames))
				{
					Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_remove_conflict"),
						FString::Printf(
							TEXT("Design removed node '%s' (%s) preserved: its subtree still hosts live design instance(s) (%s); they keep their widgets and follow their own update plan"),
							*Entry.ManifestRecord->WidgetName,
							*Entry.ManifestRecord->DesignNodeId,
							*FString::Join(LiveNames, TEXT(", "))));
					continue;
				}

				FString ConflictReason;
				if (!IsSubtreePurelyD2UOwned(Ctx.WBP, Widget, *Ctx.OwnedNames, ConflictReason))
				{
					Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_remove_conflict"),
						FString::Printf(
							TEXT("Design removed node '%s' (%s) preserved: %s"),
							*Entry.ManifestRecord->WidgetName,
							*Entry.ManifestRecord->DesignNodeId,
							*ConflictReason));
					continue;
				}

				const TSet<FString> RemovedNames = DeleteD2UOwnedSubtreeSilently(Ctx.WBP, Widget, *Ctx.UpdatedD2UOwned);
				// 诊断除子树根外同时点名子树内的其余 widget 名：单页空 root 场景下被删的
				// manifest 实例是宿主合成层（<root>_inner，无源节点 id），只报它会丢掉
				// 真正被删的设计节点身份；widget 名由设计节点名确定性派生，是 manifest
				// 之外可用的设计身份。
				TArray<FString> SubtreeNames = RemovedNames.Array();
				SubtreeNames.Remove(Entry.ManifestRecord->WidgetName);
				SubtreeNames.Sort();
				const FString SubtreeSuffix = SubtreeNames.Num() > 0
					? FString::Printf(TEXT("; removed subtree widgets: %s"), *FString::Join(SubtreeNames, TEXT(", ")))
					: FString();
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_removed"),
					FString::Printf(
						TEXT("Purely D2U-owned node '%s' (%s) removed through the safe delete flow%s"),
						*Entry.ManifestRecord->WidgetName,
						*Entry.ManifestRecord->DesignNodeId,
						*SubtreeSuffix));
			}
		}

		void ApplyKindChangedPlanEntries(
			const TArray<FReimportPlanEntry>& Plan,
			FReimportApplyContext& Ctx)
		{
			// 9b) kind-changed：旧子树纯 D2U-owned 才替换为新形态，否则冲突保留。
			for (const FReimportPlanEntry& Entry : Plan)
			{
				if (Entry.Action != EReimportPlanAction::KindChanged)
				{
					continue;
				}

				UWidget* OldWidget = Ctx.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
				if (!OldWidget)
				{
					Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_node_missing"),
						FString::Printf(
							TEXT("Recorded widget '%s' (%s) not found; kind change left as-is"),
							*Entry.ManifestRecord->WidgetName, *Entry.ManifestRecord->DesignNodeId));
					continue;
				}

				FString ConflictReason;
				if (!IsSubtreePurelyD2UOwned(Ctx.WBP, OldWidget, *Ctx.OwnedNames, ConflictReason))
				{
					Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_kind_conflict"),
						FString::Printf(
							TEXT("Kind change of '%s' (%s -> %s) preserved as-is: %s"),
							*Entry.ManifestRecord->WidgetName,
							*Entry.ManifestRecord->Kind,
							Entry.bNewKindMapped ? Design2UMGManifestKinds::Mapped : Design2UMGManifestKinds::Generated,
							*ConflictReason));
					continue;
				}

				UPanelWidget* ParentPanel = Cast<UPanelWidget>(OldWidget->GetParent());
				if (!ParentPanel)
				{
					Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_kind_conflict"),
						FString::Printf(
							TEXT("Kind change of '%s' skipped: parent is not a panel"),
							*Entry.ManifestRecord->WidgetName));
					continue;
				}

				// 保守判定（设计侧信号）：旧子树里挂着本轮仍存活的其它实例 -> 不能整删替换。
				// 整删会把它们连带销毁，而新形态剪枝后又不会重建它们 -> 实例真丢失。
				TArray<FString> LiveNames;
				if (CollectLiveInstancesInSubtree(
					OldWidget, *Ctx.AliveInstanceWidgetNames, Entry.ManifestRecord->WidgetName, LiveNames))
				{
					Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_kind_conflict"),
						FString::Printf(
							TEXT("Kind change of '%s' (%s -> %s) preserved as-is: old subtree still hosts live design instance(s) (%s)"),
							*Entry.ManifestRecord->WidgetName,
							*Entry.ManifestRecord->Kind,
							Entry.bNewKindMapped ? Design2UMGManifestKinds::Mapped : Design2UMGManifestKinds::Generated,
							*FString::Join(LiveNames, TEXT(", "))));
					continue;
				}

				DeleteD2UOwnedSubtreeSilently(Ctx.WBP, OldWidget, *Ctx.UpdatedD2UOwned);

				// 新形态子树同样剪掉已有的 manifest 实例（由 reparent 挂回，不重复生成）。
				FUMGWidgetNode PrunedNewForm = *Entry.NewNode;
				PruneManifestInstanceChildren(PrunedNewForm, *Ctx.InstancesById);
				UWidget* NewWidget = FUMGWidgetUtils::CreateWidgetRecursive(
					Ctx.WidgetTree, PrunedNewForm, ParentPanel, nullptr, nullptr, &Ctx.DesignSize, Ctx.MappingBuildState);
				if (NewWidget)
				{
					ApplySlotForNode(NewWidget, PrunedNewForm, Ctx);
					Ctx.NewlyCreatedWidgets->Add(Entry.ManifestRecord->DesignNodeId, NewWidget);
				}
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_kind_changed"),
					FString::Printf(
						TEXT("Node '%s' (%s) transitioned %s -> %s; old D2U-owned subtree replaced with the new form"),
						*Entry.ManifestRecord->WidgetName,
						*Entry.ManifestRecord->DesignNodeId,
						*Entry.ManifestRecord->Kind,
						Entry.bNewKindMapped ? Design2UMGManifestKinds::Mapped : Design2UMGManifestKinds::Generated));
			}
		}

		void ApplyAddedTopLevelNodes(
			const FUMGWidgetNode& PreparedScopeRoot,
			FReimportApplyContext& Ctx)
		{
			// 9c) added：新顶层纯生成节点（含新 mapped 实例）先挂进宿主根，
			//     这样后续 reparent 能把设计上挪进新容器的节点对到它下面。
			//     scope 根自身是 manifest 实例时（宿主根形态在两次导入间变化，如交互
			//     增删导致合成宿主出现/消失），它的 children 属于该实例内部的嵌套刷新
			//     范围（9e 递归处理），不能当「宿主根顶层新增」挂进宿主根。
			if (Ctx.HostPanel && !Ctx.InstancesById->Contains(GetDesignNodeId(PreparedScopeRoot)))
			{
				for (const FUMGWidgetNode& Child : PreparedScopeRoot.Children)
				{
					if (Ctx.InstancesById->Contains(GetDesignNodeId(Child)))
					{
						continue;
					}

					// 剪掉子树里已有的 manifest 实例（它们由 reparent 挂回，不重复生成）。
					FUMGWidgetNode PrunedChild = Child;
					PruneManifestInstanceChildren(PrunedChild, *Ctx.InstancesById);
					UWidget* NewWidget = FUMGWidgetUtils::CreateWidgetRecursive(
						Ctx.WidgetTree, PrunedChild, Ctx.HostPanel, nullptr, nullptr, &Ctx.DesignSize, Ctx.MappingBuildState);
					if (NewWidget)
					{
						Ctx.NewlyCreatedWidgets->Add(GetDesignNodeId(Child), NewWidget);
						AddReimportDiagnostic(
							*Ctx.Diagnostics, TEXT("reimport_added"),
							FString::Printf(
								TEXT("New design node '%s' (%s) added under the page host root"),
								*Child.Name, *GetDesignNodeId(Child)));
					}
				}
			}
		}

		/** reparent 目标父端解析结果。 */
		struct FReparentTargetResolution
		{
			UPanelWidget* TargetPanel = nullptr;
			bool bUnresolvable = false;
			bool bAlreadyInPlace = false;
		};

		/** widget 当前挂的父与 IR 父节点同 base（导入命名规则）= 设计本来就没要挪它。 */
		bool WidgetParentMatchesNodeBase(const UWidget* Widget, const FUMGWidgetNode& ParentNode)
		{
			if (!Widget->GetParent())
			{
				return false;
			}
			const FString ParentBaseName = FUMGWidgetUtils::SanitizeWidgetName(
				ParentNode.Name, ParentNode.Type).ToString();
			return FUMGWidgetUtils::MatchesWidgetBaseName(
				Widget->GetParent()->GetFName().ToString(), ParentBaseName);
		}

		/**
		 * reparent 目标父端解析（从 IR 直接父节点向上走）：
		 * - 转换器插入的包装节点（无源节点 id）直接跳过；
		 * - 真实设计节点必须落到 manifest 实例或本轮新建的 widget 上，否则边界不清
		 *   （bUnresolvable，冲突保留）；走到 scope 根 = 挂回宿主根；
		 * - 当前父与 IR 父同 base（合成层豁免 / 解析失败豁免共用判定）= 没要挪。
		 */
		FReparentTargetResolution ResolveReparentTarget(
			const FUMGWidgetNode& ScopeRoot,
			const FUMGWidgetNode& NewNode,
			UWidget* Widget,
			FReimportApplyContext& Ctx)
		{
			FReparentTargetResolution Resolution;
			const FUMGWidgetNode* ImmediateParentNode = FindParentNode(ScopeRoot, NewNode);
			const FUMGWidgetNode* AncestorNode = ImmediateParentNode;

			// 合成直接父层（inner 等，无源节点 id）与当前父同名对应：不先判这一步，
			// 目标会解析到合成层之上的真实节点（典型如单内容 Border 容器），内容位
			// 已被合成层占用 -> 误报 move 冲突。
			if (ImmediateParentNode
				&& !HasRealSourceIdentity(*ImmediateParentNode)
				&& WidgetParentMatchesNodeBase(Widget, *ImmediateParentNode))
			{
				Resolution.bAlreadyInPlace = true;
				return Resolution;
			}

			while (AncestorNode && !Resolution.TargetPanel && !Resolution.bUnresolvable)
			{
				if (AncestorNode == &ScopeRoot)
				{
					Resolution.TargetPanel = Ctx.HostPanel;
					break;
				}

				const bool bRealDesignNode = HasRealSourceIdentity(*AncestorNode);
				UWidget* AncestorWidget = nullptr;
				if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* ParentRecord =
					Ctx.InstancesById->Find(GetDesignNodeId(*AncestorNode)))
				{
					AncestorWidget = Ctx.WidgetTree->FindWidget(FName(*(*ParentRecord)->WidgetName));
				}
				else if (UWidget* const* NewlyCreated = Ctx.NewlyCreatedWidgets->Find(GetDesignNodeId(*AncestorNode)))
				{
					// 目标父端是本轮新建的纯生成节点：同样明确 D2U-owned。
					AncestorWidget = *NewlyCreated;
				}

				if (AncestorWidget)
				{
					if (UPanelWidget* Panel = Cast<UPanelWidget>(AncestorWidget))
					{
						Resolution.TargetPanel = Panel;
					}
					// 解析到 widget 但不是面板（内容控件）：继续上溯。
				}
				else if (bRealDesignNode)
				{
					// 真实设计节点但归属不明：边界不清 -> 冲突保留。
					Resolution.bUnresolvable = true;
				}
				AncestorNode = FindParentNode(ScopeRoot, *AncestorNode);
			}

			// 无冲突例外：解析失败但 widget 当前挂的就是 IR 直接父节点的确定性对应
			// widget（同名 base 命中）——报「跳过 reparent」与实际行为矛盾，静默放过。
			if (Resolution.bUnresolvable
				&& ImmediateParentNode
				&& WidgetParentMatchesNodeBase(Widget, *ImmediateParentNode))
			{
				Resolution.bAlreadyInPlace = true;
			}
			return Resolution;
		}

		void ApplyReparentPlanEntries(
			const TArray<FReimportPlanEntry>& Plan,
			const FUMGWidgetNode& PreparedScopeRoot,
			FReimportApplyContext& Ctx)
		{
			// 9d) matched 的 reparent 判定（move/reparent 仅限两端都明确 D2U-owned）。
			for (const FReimportPlanEntry& Entry : Plan)
			{
				if (Entry.Action != EReimportPlanAction::Matched || !Ctx.HostPanel)
				{
					continue;
				}

				// scope 根自身就是本实例（宿主根形态变化场景）：它是新设计的页面根，
				// 不存在「挪到别处」的语义，跳过 reparent（9e 照常刷新其嵌套内容）。
				if (Entry.NewNode == &PreparedScopeRoot)
				{
					continue;
				}

				UWidget* Widget = Ctx.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
				if (!Widget)
				{
					Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_node_missing"),
						FString::Printf(
							TEXT("Recorded widget '%s' (%s) not found in the target tree; left as-is"),
							*Entry.ManifestRecord->WidgetName, *Entry.ManifestRecord->DesignNodeId));
					continue;
				}

				const FReparentTargetResolution Resolution = ResolveReparentTarget(
					PreparedScopeRoot, *Entry.NewNode, Widget, Ctx);
				if (Resolution.bAlreadyInPlace)
				{
					continue;
				}
				if (!Resolution.TargetPanel || Resolution.bUnresolvable)
				{
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_move_conflict"),
						FString::Printf(
							TEXT("Reparent of '%s' skipped: new design parent is not clearly D2U-owned; structure preserved"),
							*Entry.ManifestRecord->WidgetName));
					continue;
				}
				UPanelWidget* TargetPanel = Resolution.TargetPanel;

				if (TargetPanel && Widget->GetParent() != TargetPanel)
				{
					// 源端所有权检查（与嵌套 move 同一规则）：widget 当前挂在项目侧容器下
					// = 开发者已接管它的摆放，设计侧 reparent 不拽回，冲突保留 + 诊断。
					FString SourceConflictReason;
					if (!IsMoveSourceOwned(Ctx, Widget, SourceConflictReason))
					{
						AddReimportDiagnostic(
							*Ctx.Diagnostics, TEXT("reimport_move_conflict"),
							FString::Printf(
								TEXT("Reparent of '%s' skipped: %s; structure preserved"),
								*Entry.ManifestRecord->WidgetName, *SourceConflictReason));
						continue;
					}

					// 目标是单内容控件且内容位已被占用（AddChild 会拒绝并造成孤儿）：
					// 边界不清 -> 冲突保留，不动结构。
					const bool bTargetSingleContentOccupied =
						Cast<UContentWidget>(TargetPanel) && TargetPanel->GetChildrenCount() > 0;
					if (bTargetSingleContentOccupied)
					{
						AddReimportDiagnostic(
							*Ctx.Diagnostics, TEXT("reimport_move_conflict"),
							FString::Printf(
								TEXT("Reparent of '%s' skipped: target '%s' is a single-content widget whose slot is already occupied; structure preserved"),
								*Entry.ManifestRecord->WidgetName,
								*TargetPanel->GetName()));
						continue;
					}

					if (UPanelWidget* OldPanel = Widget->GetParent())
					{
						OldPanel->Modify();
					}
					Widget->Modify();
					TargetPanel->Modify();
					Ctx.WidgetTree->RemoveWidget(Widget);
					if (TargetPanel->AddChild(Widget))
					{
						// 挪动后先落新 slot；完整刷新（属性/inputs/manifest 记录）由 9e 统一做
						// （本节点是 Matched 条目，9e 按 widgetName 一定找得到它；这里重复刷
						// 会把 mapped 记录写两份进新 manifest）。
						ApplySlotForNode(Widget, *Entry.NewNode, Ctx);
						AddReimportDiagnostic(
							*Ctx.Diagnostics, TEXT("reimport_moved"),
							FString::Printf(
								TEXT("Widget '%s' reparented under '%s' (both endpoints D2U-owned); widget identity preserved"),
								*Entry.ManifestRecord->WidgetName,
								*TargetPanel->GetName()));
					}
					else
					{
						// 防御：加不回去就挂回宿主根，绝不允许孤儿 widget。
						if (Ctx.HostPanel)
						{
							Ctx.HostPanel->AddChild(Widget);
						}
						AddReimportDiagnostic(
							*Ctx.Diagnostics, TEXT("reimport_move_conflict"),
							FString::Printf(
								TEXT("Reparent of '%s' to '%s' failed to attach; widget restored under the page host root"),
								*Entry.ManifestRecord->WidgetName,
								*TargetPanel->GetName()));
					}
				}
			}
		}

		TArray<UWidget*> CollectNestedMoveCandidates(
			const FUMGWidgetNode& PreparedScopeRoot,
			FReimportApplyContext& Ctx)
		{
			// 9d-2) 嵌套结构 diff 预扫描：收集「owned 名单内、非 manifest 实例、且新设计
			//       不再期望留在原容器」的 widget，作为嵌套 move 候选池。9e 的容器子树
			//       diff 按确定性同名从池里消费 move 源；未被消费的走 9g 的删除残留诊断。
			TArray<UWidget*> NestedMoveCandidates;
			{
				TSet<FString> ExpectedNames;
				if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* ScopeRootRecord =
					Ctx.InstancesById->Find(GetDesignNodeId(PreparedScopeRoot)))
				{
					// scope 根自身是实例：期望集合从该实例 widget 的子级算起。
					if (UPanelWidget* ScopeRootPanel = Cast<UPanelWidget>(
						Ctx.WidgetTree->FindWidget(FName(*(*ScopeRootRecord)->WidgetName))))
					{
						CollectExpectedNestedNames(PreparedScopeRoot, ScopeRootPanel, Ctx, ExpectedNames);
					}
				}
				else
				{
					for (const FUMGWidgetNode& Child : PreparedScopeRoot.Children)
					{
						// 只递归实例 child；非实例 child 是 9c 本轮新建的子树（全新，无残留语义）。
						if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* ChildRecord =
							Ctx.InstancesById->Find(GetDesignNodeId(Child)))
						{
							if (UPanelWidget* ChildPanel = Cast<UPanelWidget>(
								Ctx.WidgetTree->FindWidget(FName(*(*ChildRecord)->WidgetName))))
							{
								CollectExpectedNestedNames(Child, ChildPanel, Ctx, ExpectedNames);
							}
						}
					}
				}

				TSet<FString> InstanceWidgetNames;
				for (const TPair<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>& Pair : *Ctx.InstancesById)
				{
					InstanceWidgetNames.Add(Pair.Value->WidgetName);
				}

				const TSet<FString>& CreatedThisRun = Ctx.MappingBuildState->GetCreatedWidgetNames();
				Ctx.WBP->WidgetTree->ForEachWidget([&](UWidget* TreeWidget)
				{
					const FString WidgetName = TreeWidget->GetFName().ToString();
					if (TreeWidget == Ctx.WBP->WidgetTree->RootWidget
						|| !Ctx.OwnedNames->Contains(WidgetName)
						|| InstanceWidgetNames.Contains(WidgetName)
						|| CreatedThisRun.Contains(WidgetName)
						|| ExpectedNames.Contains(WidgetName))
					{
						return;
					}
					NestedMoveCandidates.Add(TreeWidget);
				});
			}
			return NestedMoveCandidates;
		}

		void ApplyMatchedPlanEntries(
			const TArray<FReimportPlanEntry>& Plan,
			FReimportApplyContext& Ctx,
			const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings)
		{
			// 9e) matched：更新 D2U-owned 属性/布局/slot/inputs；Mapped Class 变化在这里拦截。
			for (const FReimportPlanEntry& Entry : Plan)
			{
				if (Entry.Action != EReimportPlanAction::Matched)
				{
					continue;
				}

				UWidget* Widget = Ctx.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
				if (!Widget)
				{
					// node_missing 已在 9d 里记录并补留 manifest。
					continue;
				}

				// Mapped Class 变化 = 高风险结构变化：不替换、不更新，冲突诊断（报新旧类）。
				if (Entry.bNewKindMapped && Entry.ManifestRecord->Kind == Design2UMGManifestKinds::Mapped)
				{
					const DesignComponentMapping::FComponentMappingEntry* NewEntry =
						GenerationSettings.ComponentMapping.Table.FindEntryByKey(Entry.NewNode->Source().MappedComponentKey);
					const FString NewMappedClass = NewEntry ? NewEntry->WidgetClass : FString();
					if (IsMappedClassConflict(Widget, Entry.ManifestRecord, NewMappedClass))
					{
						Ctx.UpdatedMappedRecords->Add(ManifestRecordToMappedRecord(*Entry.ManifestRecord));
						AddReimportDiagnostic(
							*Ctx.Diagnostics, TEXT("reimport_mapped_class_conflict"),
							FString::Printf(
								TEXT("Mapped class change on '%s' (%s): instance kept as '%s', manifest recorded '%s', mapping now points to '%s'; not replaced in v1"),
								*Entry.ManifestRecord->WidgetName,
								*Entry.ManifestRecord->DesignNodeId,
								*Widget->GetClass()->GetPathName(),
								*Entry.ManifestRecord->MappedClass,
								*NewMappedClass));
						continue;
					}
				}

				RefreshNodeByKind(*Entry.NewNode, Widget, Ctx);
			}
		}

		void ReportNestedRemovalLeftovers(FReimportApplyContext& Ctx)
		{
			// 9g) 嵌套删除残留诊断：候选池里未被 move 消费的 owned 子树 = 新设计已不再
			//     期望的嵌套节点。含项目内容（项目子控件 / 开发者绑定）→ 保留 + 冲突诊断
			//     点名（spec R3「Removed node contains project-side content」无顶层限定）；
			//     纯 D2U 嵌套子树保留现状、不加诊断（嵌套删除不是安全删除范围，已验收粒度）。
			for (UWidget* Orphan : *Ctx.NestedMoveCandidates)
			{
				FString ConflictReason;
				if (!IsSubtreePurelyD2UOwned(Ctx.WBP, Orphan, *Ctx.OwnedNames, ConflictReason))
				{
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_remove_conflict"),
						FString::Printf(
							TEXT("Design-removed nested node '%s' preserved: %s"),
							*Orphan->GetName(), *ConflictReason));
				}
			}
		}

		/** 步骤 10)：组装 reimport 后的 manifest 载荷（含预览尺寸刷新与 GUID 补齐）。 */
		FDesign2UMGGenerationOutput BuildReimportManifestPayload(
			const FDesign2UMGGenerationManifestSidecar& Manifest,
			bool bPrototypeHostContinued,
			const FUMGWidgetData& Data,
			FReimportApplyContext& Ctx)
		{
			// 10) 组装 reimport 后的 manifest 载荷。
			FDesign2UMGGenerationOutput GenerationOutput;
			GenerationOutput.Manifest.Provider = Manifest.Provider;
			GenerationOutput.Manifest.DocumentScope = Manifest.DocumentScope;
			// 宿主延续时源根身份按新源刷新（新页集合指纹），下次 reimport 以新指纹为基准；
			// 记录规则与导入侧一致：有源节点 id 用源 id，合成根用 IR Id。
			GenerationOutput.Manifest.SourceRootNodeId = bPrototypeHostContinued
				? GetDesignNodeId(Data.RootWidget)
				: Manifest.SourceRootNodeId;
			GenerationOutput.Manifest.SourceRootName = bPrototypeHostContinued
				? Data.RootWidget.Name
				: Manifest.SourceRootName;
			GenerationOutput.Manifest.MappedInstances = MoveTemp(*Ctx.UpdatedMappedRecords);
			GenerationOutput.Manifest.TopLevelGenerated = MoveTemp(*Ctx.UpdatedGeneratedRecords);

			// 保留现状的实例（冲突/定位失败）：按原记录补回新 manifest。
			for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Manifest.Instances)
			{
				if (!Ctx.KeepAsIsWidgetNames->Contains(Record.WidgetName))
				{
					continue;
				}
				if (Record.Kind == Design2UMGManifestKinds::Mapped)
				{
					GenerationOutput.Manifest.MappedInstances.Add(ManifestRecordToMappedRecord(Record));
				}
				else
				{
					FDesign2UMGGeneratedInstanceRecord Kept;
					Kept.DesignNodeId = Record.DesignNodeId;
					Kept.WidgetName = Record.WidgetName;
					GenerationOutput.Manifest.TopLevelGenerated.Add(MoveTemp(Kept));
				}
			}

			// 本轮生成链新建的实例（顶层 added / kind-changed 新形态 / 嵌套新增 mapped）进新 manifest。
			for (const FDesign2UMGMappedInstanceRecord& NewMapped : Ctx.MappingBuildState->GetMappedInstances())
			{
				if (!GenerationOutput.Manifest.MappedInstances.ContainsByPredicate(
					[&NewMapped](const FDesign2UMGMappedInstanceRecord& M) { return M.WidgetName == NewMapped.WidgetName; }))
				{
					GenerationOutput.Manifest.MappedInstances.Add(NewMapped);
				}
			}
			for (const FDesign2UMGGeneratedInstanceRecord& NewGenerated : Ctx.MappingBuildState->GetTopLevelGenerated())
			{
				if (!GenerationOutput.Manifest.TopLevelGenerated.ContainsByPredicate(
					[&NewGenerated](const FDesign2UMGGeneratedInstanceRecord& G) { return G.WidgetName == NewGenerated.WidgetName; }))
				{
					GenerationOutput.Manifest.TopLevelGenerated.Add(NewGenerated);
				}
			}

			// 新 d2uOwned =（旧集 - 已删除名）∪ 本轮创建名；再清掉树里已不存在的名字。
			for (const FString& CreatedName : Ctx.MappingBuildState->GetCreatedWidgetNames())
			{
				(*Ctx.UpdatedD2UOwned).Add(CreatedName);
			}
			if (Ctx.WBP->WidgetTree)
			{
				for (const FString& OwnedName : (*Ctx.UpdatedD2UOwned).Array())
				{
					if (!Ctx.WBP->WidgetTree->FindWidget(FName(*OwnedName)))
					{
						(*Ctx.UpdatedD2UOwned).Remove(OwnedName);
					}
				}
			}
			GenerationOutput.Manifest.D2UOwnedNames = (*Ctx.UpdatedD2UOwned).Array();
			GenerationOutput.Manifest.D2UOwnedNames.Sort();

			return GenerationOutput;
		}

		/**
		 * 步骤 10) 尾的资产终态化（与 manifest 组装无关的资产侧副作用）：
		 * 设计稿根尺寸变化时刷新 CDO 预览尺寸（SaveWidgetBlueprint 编译后回写）；
		 * 生成链新建 widget 缺变量 GUID 时按编译器同款确定性规则补齐。
		 */
		void FinalizeReimportedAssetState(FReimportApplyContext& Ctx)
		{
			if (UUserWidget* CDO = Cast<UUserWidget>(
				Ctx.WBP->GeneratedClass ? Ctx.WBP->GeneratedClass->GetDefaultObject() : nullptr))
			{
				// CDO 默认不带 RF_Transactional（WidgetTree 上面刚补过，这里同理）：
				// 不补标志则 modify 不入事务缓冲，保存失败的回滚/用户 undo 后预览尺寸残留新值。
				CDO->SetFlags(RF_Transactional);
				CDO->Modify();
				CDO->DesignTimeSize = FVector2D(Ctx.DesignSize.X, Ctx.DesignSize.Y);
				CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
			}

			// 生成链本轮新建的 widget（以及测试直接 ConstructWidget 的注入）没有变量 GUID，
			// 编译器对缺失项会 ensure。按编译器同款确定性规则补齐，既有 GUID 不动。
			// 先 Modify 捕获 GUID 映射的事务前状态：保存失败回滚时映射随资产一起还原，
			// 否则回滚后的重编译会因「已删 widget 残留 GUID」触发编译器 ensure。
			if (Ctx.WBP->WidgetTree)
			{
				Ctx.WBP->Modify();
				Ctx.WBP->WidgetTree->ForEachWidget([&Ctx](UWidget* TreeWidget)
				{
					const FName WidgetName = TreeWidget->GetFName();
					if (!Ctx.WBP->WidgetVariableNameToGuidMap.Contains(WidgetName))
					{
						Ctx.WBP->WidgetVariableNameToGuidMap.Add(
							WidgetName, FGuid::NewDeterministicGuid(TreeWidget->GetPathName()));
					}
				});
			}
		}

		/** 步骤 11)：可写性 pre-check + 编译保存 + 失败回滚（FailSaveWithRollback 随本块搬移）。 */
		FDesignReimportResult SaveReimportedAsset(
			const FReimportSession& Session,
			const FString& TargetPackage,
			UWidgetBlueprint* WBP,
			FDesign2UMGGenerationOutput& GenerationOutput,
			int32 TransactionIndex,
			FDesignReimportResult& Result)
		{
			// 11) 保存前可写性 pre-check + 编译 + 保存 + 写新 manifest。
			// 保存失败统一出口：事务回滚 + 重编译把生成类拉回与模板树一致 + reimport_save_failed 诊断。
			const auto FailSaveWithRollback = [&](const FString& Reason)
			{
				// 回滚状态按实际事务归属如实陈述：无自有事务时（transacting 不可用或
				// 嵌套在外部活动事务里）不 End/Undo，磁盘未动、内存改动留在原地。
				FString RollbackNote;
				if (TransactionIndex != INDEX_NONE)
				{
					GEditor->EndTransaction();
					GEditor->UndoTransaction();
					RollbackNote = TEXT("; transaction rolled back, asset keeps its pre-reimport state, manifest not updated");
				}
				else
				{
					RollbackNote = TEXT("; save not completed and no reimport-owned editor transaction was open (transacting unavailable or nested inside an active transaction); asset on disk keeps its previous state, in-memory changes remain, manifest not updated");
				}
				AddReimportDiagnostic(Result.Diagnostics, TEXT("reimport_save_failed"), Reason + RollbackNote);
				// 回滚只还原模板树；编译产生的类对象不随事务回滚，
				// 重新编译一次把生成类拉回与（已还原的）模板树一致的状态。
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
				FKismetEditorUtilities::CompileBlueprint(
					WBP,
					EBlueprintCompileOptions::SkipDefaultObjectValidation | EBlueprintCompileOptions::SkipFiBSearchMetaUpdate);
				Result.bSucceeded = false;
				// reimport 结束：逐节点诊断落入结构化报告（与导入同一出口，见 design.md 第 0/6 节）。
				DesignImportDiagnosticReport::WriteReimport(
				Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
			};

			// 11a) pre-check：目标文件不可写时不进保存流程。uasset 只查只读属性
			// （写探测会被本进程加载态的引擎读句柄误杀，见 IsFileWritableForSave）；
			// sidecar 是我们自己的 JSON、引擎从不加载，无读句柄假阳性来源，保留
			// 只读 + 写探测双检查，防「uasset 已更新 + sidecar 写失败」的半更新。
			// Perforce 未 checkout 的目标场景：uasset 只读会让引擎 SavePackage 走 appError
			// fatal（编辑器直接崩，无法产品化诊断）。命中即拦截，保存完全不发生（磁盘保持原状）。
			{
				FString WritabilityBlockers;
				FString Reason;
				if (!IsFileWritableForSave(
					FPackageName::LongPackageNameToFilename(TargetPackage, FPackageName::GetAssetPackageExtension()),
					/*bAllowWriteProbe=*/false, Reason))
				{
					WritabilityBlockers = Reason;
				}
				if (!IsFileWritableForSave(
					Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage),
					/*bAllowWriteProbe=*/true, Reason))
				{
					if (!WritabilityBlockers.IsEmpty())
					{
						WritabilityBlockers += TEXT("; ");
					}
					WritabilityBlockers += Reason;
				}
				if (!WritabilityBlockers.IsEmpty())
				{
					FailSaveWithRollback(FString::Printf(
						TEXT("Pre-save writability check failed (%s); save not attempted"),
						*WritabilityBlockers));
					return Result;
				}
			}

			// 11b) 编译 + 保存 + 写新 manifest；失败 = 事务回滚、资产保持原状态、manifest 不更新。
			if (!FUMGGenerator::SaveWidgetBlueprint(WBP, &GenerationOutput))
			{
				FailSaveWithRollback(FString::Printf(
					TEXT("Save/compile failed for %s"),
					*TargetPackage));
				return Result;
			}

			// 11c) sidecar 写失败传导：uasset 已落盘但 manifest 未更新 = 资产与记录不一致，
			//      不能当成功汇报（pre-check 拦不住的 IO 失败如磁盘满在这里兜底）。
			//      此时磁盘已是新资产：不回滚内存（回滚会造成内存/磁盘不一致），事务正常收尾。
			const bool bManifestWriteFailed = GenerationOutput.MappingDiagnostics.ContainsByPredicate(
				[](const FDesign2UMGMappingDiagnostic& Diagnostic)
				{
					return Diagnostic.Kind == Design2UMGManifestKinds::ManifestWriteFailedDiagnostic;
				});
			if (bManifestWriteFailed)
			{
				for (const FDesign2UMGMappingDiagnostic& Diagnostic : GenerationOutput.MappingDiagnostics)
				{
					if (Diagnostic.Kind == Design2UMGManifestKinds::ManifestWriteFailedDiagnostic)
					{
						Result.Diagnostics.Add(Diagnostic);
					}
				}
				if (TransactionIndex != INDEX_NONE)
				{
					GEditor->EndTransaction();
				}
				AddReimportDiagnostic(
					Result.Diagnostics, TEXT("reimport_save_failed"),
					FString::Printf(
						TEXT("Asset %s was saved but its generation manifest could not be written; the asset and the manifest may be inconsistent"),
						*TargetPackage));
				Result.bSucceeded = false;
				DesignImportDiagnosticReport::WriteReimport(
				Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
				return Result;
			}

			if (TransactionIndex != INDEX_NONE)
			{
				GEditor->EndTransaction();
			}

			Result.UpdatedBlueprint = WBP;
			Result.bSucceeded = true;
			AddReimportDiagnostic(
				Result.Diagnostics, TEXT("reimport_completed"),
				FString::Printf(
					TEXT("Reimport of %s applied (manifest updated)"),
					*TargetPackage));
			// reimport 结束：逐节点诊断落入结构化报告（与导入同一出口，见 design.md 第 0/6 节）。
			DesignImportDiagnosticReport::WriteReimport(
				Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
			return Result;
		}

	}

	// ---------------------------------------------------------------------------
	// Reimport 主流程
	// ---------------------------------------------------------------------------

	FDesignReimportResult Reimport(FDesignDocument& Document, const FDesignReimportRequest& Request)
	{
		FDesignReimportResult Result;

		// 1)-3) 设置快照 / 重新解析源 / 定位目标资产 + 读 manifest。
		FDesign2UMGImportSettingsSnapshot SettingsSnapshot;
		FUMGWidgetData Data;
		FString TargetPackage;
		UWidgetBlueprint* WBP = nullptr;
		FDesign2UMGGenerationManifestSidecar Manifest;
		if (TOptional<FDesignReimportResult> Early = ResolveReimportSourceAndManifest(
			Document, Request, Result, SettingsSnapshot, Data, TargetPackage, WBP, Manifest))
		{
			return MoveTemp(*Early);
		}
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings = SettingsSnapshot.GenerationSettings;
		const FReimportSession Session{ Document, Request, Data, GenerationSettings };

		// 4)-5) scope 匹配、源根定位与宿主延续。
		const FUMGWidgetNode* ScopeRoot = nullptr;
		bool bPrototypeHostContinued = false;
		if (TOptional<FDesignReimportResult> Early = MatchReimportScopeAndRoot(
			Session, Manifest, Result, ScopeRoot, bPrototypeHostContinued))
		{
			return MoveTemp(*Early);
		}

		// 6)-8) Component Mapping 两级匹配、更新计划构建、结构健全性检查。
		FUMGWidgetNode PreparedScopeRoot;
		FDesign2UMGComponentMappingBuildState MappingBuildState(GenerationSettings.ComponentMapping);
		TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*> InstancesById;
		TArray<FReimportPlanEntry> Plan;
		TSet<FString> AliveInstanceWidgetNames;
		BuildReimportUpdatePlan(
			GenerationSettings, Data, Manifest, ScopeRoot,
			PreparedScopeRoot, MappingBuildState, InstancesById, Plan, AliveInstanceWidgetNames);
		FIntPoint DesignSize = FIntPoint::ZeroValue;
		if (TOptional<FDesignReimportResult> Early = CheckReimportStructureViability(
			Session, WBP, Manifest, PreparedScopeRoot, Result, DesignSize))
		{
			return MoveTemp(*Early);
		}

		// 9) 编辑器事务内应用更新计划。
		const int32 TransactionIndex = BeginReimportApplyTransaction(WBP);
		TArray<FDesign2UMGMappedInstanceRecord> FinalMappedRecords;
		TArray<FDesign2UMGGeneratedInstanceRecord> FinalGeneratedRecords;
		TSet<FString> UpdatedD2UOwned(Manifest.D2UOwned);
		const TSet<FString> ManifestOwnedNames(Manifest.D2UOwned);
		TArray<FString> KeepAsIsWidgetNames;
		// 本轮 reimport 新建的 widget（designNodeId -> widget），reparent 的目标端解析用：
		// 新容器（设计新增节点）也是明确 D2U-owned 的。
		TMap<FString, UWidget*> NewlyCreatedWidgets;

		FReimportApplyContext Ctx;
		Ctx.WBP = WBP;
		Ctx.WidgetTree = WBP->WidgetTree;
		Ctx.HostPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget);
		Ctx.DesignSize = DesignSize;
		Ctx.OwnedNames = &ManifestOwnedNames;
		Ctx.InstancesById = &InstancesById;
		Ctx.MappingBuildState = &MappingBuildState;
		Ctx.Diagnostics = &Result.Diagnostics;
		Ctx.UpdatedMappedRecords = &FinalMappedRecords;
		Ctx.UpdatedGeneratedRecords = &FinalGeneratedRecords;
		Ctx.NewlyCreatedWidgets = &NewlyCreatedWidgets;
		Ctx.UpdatedD2UOwned = &UpdatedD2UOwned;
		Ctx.KeepAsIsWidgetNames = &KeepAsIsWidgetNames;
		Ctx.AliveInstanceWidgetNames = &AliveInstanceWidgetNames;
		MappingBuildState.SetRootWidget(WBP->WidgetTree->RootWidget);

		// 9a)-9d) removed / kind-changed / added / reparent。
		ApplyRemovedPlanEntries(Plan, Ctx);
		ApplyKindChangedPlanEntries(Plan, Ctx);
		ApplyAddedTopLevelNodes(PreparedScopeRoot, Ctx);
		ApplyReparentPlanEntries(Plan, PreparedScopeRoot, Ctx);

		// 9d-2) 嵌套结构 diff 预扫描（move 候选池）。
		TArray<UWidget*> NestedMoveCandidates = CollectNestedMoveCandidates(PreparedScopeRoot, Ctx);
		Ctx.NestedMoveCandidates = &NestedMoveCandidates;

		// 9e)-9g) 属性更新 / 嵌套删除残留诊断（步骤注释随对应函数）。
		ApplyMatchedPlanEntries(Plan, Ctx, GenerationSettings);
		// 9f) Prototype 绑定刷新（普通生成控件，按稳定设计身份；双触发让位）。
		ApplyPrototypeRefresh(Data, GenerationSettings, Ctx);
		ReportNestedRemovalLeftovers(Ctx);

		// mapping 诊断出口（对齐导入路径 UMGGenerator 的组装）：ini 校验诊断 + 本轮
		// BuildState 收集的匹配/回退诊断进 ComponentMappingDiagnostics，由报告写进
		// componentMapping 章节；此前 reimport 报告缺失该出口，类加载失败等配置错误不可见。
		Result.ComponentMappingDiagnostics = GenerationSettings.ComponentMapping.ValidationDiagnostics;
		Result.ComponentMappingDiagnostics.Append(MappingBuildState.GetDiagnostics());

		// 10) 组装 reimport 后的 manifest 载荷 + 资产终态化（CDO 预览尺寸、变量 GUID）。
		FDesign2UMGGenerationOutput GenerationOutput = BuildReimportManifestPayload(
			Manifest, bPrototypeHostContinued, Data, Ctx);
		FinalizeReimportedAssetState(Ctx);

		// 11) 可写性 pre-check + 保存 + FailSaveWithRollback。
		return SaveReimportedAsset(Session, TargetPackage, WBP, GenerationOutput, TransactionIndex, Result);
	}
}
