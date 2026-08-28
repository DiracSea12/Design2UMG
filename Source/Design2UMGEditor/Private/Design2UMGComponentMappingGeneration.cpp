// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGComponentMappingGeneration.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UMGSchemaSourceExtensions.h"
#include "UObject/UnrealType.h"

namespace
{
	// mapped 命中节点的子树不会展开生成：给全部后代打「子树内部」标记，
	// 挂在这些节点上的设计交互同样不生效（ReportInertPrototypeBindings 据此输出诊断）。
	void MarkMappedSubtreeRecursive(const FUMGWidgetNode& Node, FDesign2UMGComponentMappingBuildState& BuildState)
	{
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			BuildState.AddMappedSubtreeNodeId(Child.Id);
			MarkMappedSubtreeRecursive(Child, BuildState);
		}
	}

	void ApplyMappingRecursive(
		FUMGWidgetNode& Node,
		const FString& DocumentScope,
		const DesignComponentMapping::FComponentMappingTable& Table,
		FDesign2UMGComponentMappingBuildState& BuildState)
	{
		// 已被外层 mapped 命中吞并的子树不再参与匹配：命中也不会有实例（生成链不展开），
		// 发出 mapping_hit 只会让用户以为内层组件接入了项目 WBP（诊断虚报）。
		if (!BuildState.IsInsideMappedSubtree(Node.Id) && Node.HasSourceData())
		{
			DesignComponentMapping::FDesignComponentIdentity Identity;
			if (Node.Source().bMappedComponentCandidate
				&& DesignComponentMapping::TryResolveFigmaComponentIdentity(Node.Source(), DocumentScope, Identity))
			{
				bool bMatchedAtSetLevel = false;
				const DesignComponentMapping::FComponentMappingEntry* Entry = Table.Match(Identity, bMatchedAtSetLevel);
				if (Entry)
				{
					// 命中：把 mapping Key 写进 IR 边车，生成阶段据此接入项目 WBP 实例。
					Node.MutableSource().MappedComponentKey = Entry->Key;
					BuildState.AddMappedNodeId(Node.Id, bMatchedAtSetLevel);
					MarkMappedSubtreeRecursive(Node, BuildState);
					const FString& MatchedIdentity = bMatchedAtSetLevel
						? Identity.BuildSetKey()
						: Identity.BuildVariantKey();
					BuildState.AddDiagnostic(
						TEXT("mapping_hit"),
						FString::Printf(
							TEXT("Mapped component '%s' (identity %s) -> %s%s"),
							*Node.Name,
							*MatchedIdentity,
							*Entry->WidgetClass,
							bMatchedAtSetLevel ? TEXT(" (component-set level match)") : TEXT("")));
				}
			}
		}

		for (FUMGWidgetNode& Child : Node.Children)
		{
			ApplyMappingRecursive(Child, DocumentScope, Table, BuildState);
		}
	}

	// NUMBER 值无损文本化：从低精度往上试，取第一个能精确还原回同一 double 的表示。
	// %g 默认 6 位有效数字会把设计值静默舍入；固定 %.17g 虽无损但会把 9.99 拉长成
	// 17 位噪音。最短可还原表示既不丢精度，也不污染诊断与 manifest 里的 value 文本。
	FString NumberToLosslessString(const double NumberValue)
	{
		for (int32 Precision = 1; Precision < 17; ++Precision)
		{
			const FString Candidate = FString::Printf(TEXT("%.*g"), Precision, NumberValue);
			double ParsedBack = 0.0;
			if (LexTryParseString(ParsedBack, *Candidate) && ParsedBack == NumberValue)
			{
				return Candidate;
			}
		}
		return FString::Printf(TEXT("%.17g"), NumberValue);
	}

	// 从 componentProperties JSON（{name:{type,value}}）里取 Source 键的 value 字段。
	bool TryGetComponentPropertyValue(const FString& PropertiesJson, const FString& SourceKey, FString& OutValue)
	{
		if (PropertiesJson.IsEmpty())
		{
			return false;
		}

		TSharedPtr<FJsonObject> PropertiesObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
		if (!FJsonSerializer::Deserialize(Reader, PropertiesObject) || !PropertiesObject.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		if (!PropertiesObject->TryGetObjectField(SourceKey, EntryObject) || !EntryObject || !(*EntryObject).IsValid())
		{
			return false;
		}

		if ((*EntryObject)->TryGetStringField(TEXT("value"), OutValue))
		{
			return true;
		}
		double NumberValue = 0.0;
		if ((*EntryObject)->TryGetNumberField(TEXT("value"), NumberValue))
		{
			OutValue = NumberToLosslessString(NumberValue);
			return true;
		}
		bool BoolValue = false;
		if ((*EntryObject)->TryGetBoolField(TEXT("value"), BoolValue))
		{
			OutValue = BoolValue ? TEXT("true") : TEXT("false");
			return true;
		}
		return false;
	}

	// 组件集实例的 variant 值落在 variantProperties（{name:value-string} 平铺映射）里，同属组件属性来源。
	bool TryGetVariantPropertyValue(const FString& VariantPropertiesJson, const FString& SourceKey, FString& OutValue)
	{
		if (VariantPropertiesJson.IsEmpty())
		{
			return false;
		}

		TSharedPtr<FJsonObject> PropertiesObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(VariantPropertiesJson);
		if (!FJsonSerializer::Deserialize(Reader, PropertiesObject) || !PropertiesObject.IsValid())
		{
			return false;
		}
		return PropertiesObject->TryGetStringField(SourceKey, OutValue);
	}

	// 组件主体文本的子树回退：深度优先找组件内部第一个带非空文本的 TEXT 子节点。
	// 典型 Figma 数据里按钮 label 几乎都挂在实例内部 TEXT 上，而不是实例节点自身。
	void FindFirstTextChildRecursive(const FUMGWidgetNode& Node, FString& OutText)
	{
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (Child.Type == TEXT("TextBlock"))
			{
				const FString ChildText = Child.Text.Text.Get(FString());
				if (!ChildText.IsEmpty())
				{
					OutText = ChildText;
					return;
				}
			}
			FindFirstTextChildRecursive(Child, OutText);
			if (!OutText.IsEmpty())
			{
				return;
			}
		}
	}

	// 按声明的取值来源解析设计值：ComponentProperty 取 componentProperties 键值（缺省回退 variantProperties），
	// ComponentText 取组件主体文本（节点自身文本，若无则取内部首个 TEXT 子节点的文本）。
	bool TryResolveComponentInputValue(
		const FUMGWidgetNode& Node,
		const DesignComponentMapping::FComponentInputDeclaration& Input,
		FString& OutValue,
		FString& OutError)
	{
		if (Input.ValueKind == DesignComponentMapping::EComponentInputValueKind::ComponentText)
		{
			OutValue = Node.Text.Text.Get(FString());
			if (OutValue.IsEmpty())
			{
				FindFirstTextChildRecursive(Node, OutValue);
			}
			if (OutValue.IsEmpty())
			{
				OutError = TEXT("component has no text content (node text and internal TEXT nodes are empty)");
				return false;
			}
			return true;
		}

		FString ComponentPropertiesJson;
		FString VariantPropertiesJson;
		if (Node.HasSourceData())
		{
			ComponentPropertiesJson = Node.Source().ComponentPropertiesJson;
			VariantPropertiesJson = Node.Source().VariantPropertiesJson;
		}
		if (TryGetComponentPropertyValue(ComponentPropertiesJson, Input.Source, OutValue))
		{
			return true;
		}
		if (TryGetVariantPropertyValue(VariantPropertiesJson, Input.Source, OutValue))
		{
			return true;
		}
		OutError = FString::Printf(
			TEXT("component property '%s' not found in componentProperties/variantProperties"),
			*Input.Source);
		return false;
	}

	bool ParseBoolText(const FString& Value, bool& OutBool)
	{
		if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1"))
		{
			OutBool = true;
			return true;
		}
		if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value == TEXT("0"))
		{
			OutBool = false;
			return true;
		}
		return false;
	}

	// 反射写入：先解析目标属性，再按声明类型严格匹配，类型不匹配即失败（诊断 + 跳过该 input）。
	// 名称解析（通用规则）：先按声明名精确查找；未命中且声明类型为 Bool 时，按 UE 官方
	// bool 属性命名惯例（b 前缀）重试一次（如 spec 示例 IsEnabled -> bIsEnabled）。
	// 类的属性链（PropertyLink）由 Link 按「含父类」的迭代重建，WBP 类与原生类的继承属性都能按名命中。
	bool TryWriteInputValue(
		UUserWidget* Instance,
		const DesignComponentMapping::FComponentInputDeclaration& Input,
		const FString& Value,
		FString& OutError)
	{
		UClass* InstanceClass = Instance->GetClass();
		FProperty* Property = InstanceClass->FindPropertyByName(FName(*Input.Target));
		if (!Property && Input.Type == DesignComponentMapping::EComponentInputType::Bool)
		{
			const FString BoolTarget = FString(TEXT("b")) + Input.Target;
			Property = InstanceClass->FindPropertyByName(FName(*BoolTarget));
		}
		if (!Property)
		{
			OutError = FString::Printf(TEXT("target property '%s' not found on %s"), *Input.Target, *InstanceClass->GetName());
			return false;
		}

		using namespace DesignComponentMapping;
		switch (Input.Type)
		{
		case EComponentInputType::Text:
			if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
			{
				TextProperty->SetPropertyValue_InContainer(Instance, FText::FromString(Value));
				return true;
			}
			break;
		case EComponentInputType::Bool:
			if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				bool BoolValue = false;
				if (!ParseBoolText(Value, BoolValue))
				{
					OutError = FString::Printf(TEXT("value '%s' is not a bool"), *Value);
					return false;
				}
				BoolProperty->SetPropertyValue_InContainer(Instance, BoolValue);
				return true;
			}
			break;
		case EComponentInputType::Name:
			if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
			{
				NameProperty->SetPropertyValue_InContainer(Instance, FName(Value));
				return true;
			}
			break;
		case EComponentInputType::Float:
			if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
			{
				double DoubleValue = 0.0;
				if (!LexTryParseString(DoubleValue, *Value))
				{
					OutError = FString::Printf(TEXT("value '%s' is not a float"), *Value);
					return false;
				}
				DoubleProperty->SetPropertyValue_InContainer(Instance, DoubleValue);
				return true;
			}
			if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
			{
				double DoubleValue = 0.0;
				if (!LexTryParseString(DoubleValue, *Value))
				{
					OutError = FString::Printf(TEXT("value '%s' is not a float"), *Value);
					return false;
				}
				FloatProperty->SetPropertyValue_InContainer(Instance, static_cast<float>(DoubleValue));
				return true;
			}
			break;
		default:
			break;
		}

		OutError = FString::Printf(
			TEXT("target property '%s' (%s) does not match declared input type '%s'"),
			*Input.Target,
			*Property->GetClass()->GetName(),
			*GetComponentInputTypeName(Input.Type));
		return false;
	}
}

bool FDesign2UMGComponentMappingBuildState::IsMappedNode(const FUMGWidgetNode& Node) const
{
	return Node.HasSourceData() && !Node.Source().MappedComponentKey.IsEmpty();
}

void FDesign2UMGComponentMappingBuildState::RecordCreatedWidget(
	const FUMGWidgetNode& Node,
	const UWidget* CreatedWidget,
	const UWidget* ParentWidget)
{
	if (!CreatedWidget)
	{
		return;
	}

	// 根 widget 本身不作为 instance 记录（manifest 的 sourceRoot 单独描述它）。
	if (IsRootWidget(CreatedWidget))
	{
		return;
	}

	// 所有权记录：本次创建的每个 widget（含嵌套叶子）都记名，
	// manifest 的 d2uOwned 以此为数据源，reimport 用它判定删除/移动边界。
	CreatedWidgetNames.Add(CreatedWidget->GetFName().ToString());

	// mapped 实例在 TryCreateMappedWidget 里已登记（含 inputs 回填），这里不重复记录。
	if (IsMappedNode(Node))
	{
		return;
	}

	// 只记录页面宿主根 widget 的直属子节点作为顶层生成关系；叶子控件不逐个记录。
	// 转换器插入的包装节点（SizeBox/Button wrap 等）没有源数据边车，designNodeId 回退到 IR Id
	// （IR Id 对包装节点也是确定性再生的，reimport 可按 designNodeId + widgetName 找回）。
	if (ParentWidget && IsRootWidget(ParentWidget))
	{
		FDesign2UMGGeneratedInstanceRecord Record;
		Record.DesignNodeId = Node.GetStableDesignNodeId();
		Record.WidgetName = CreatedWidget->GetFName().ToString();
		TopLevelGenerated.Add(MoveTemp(Record));
	}
}

namespace
{
	// 按声明逐 input 写入：解析设计值 -> 反射写入，单个失败只跳过并诊断。
	// 导入路径与 reimport 路径共用，区别只在诊断/记录的落点。
	void ApplyMappedComponentInputsInternal(
		UUserWidget* Instance,
		const FUMGWidgetNode& Node,
		const DesignComponentMapping::FComponentMappingEntry& Entry,
		FDesign2UMGMappedInstanceRecord* Record,
		TArray<FDesign2UMGMappingDiagnostic>& Diagnostics)
	{
		for (const DesignComponentMapping::FComponentInputDeclaration& Input : Entry.Inputs)
		{
			FDesign2UMGMappedInputRecord InputRecord;
			InputRecord.Source = Input.Source;
			InputRecord.Target = Input.Target;
			InputRecord.ValueKind = DesignComponentMapping::GetComponentInputValueKindName(Input.ValueKind);

			FString Value;
			FString Error;
			const bool bResolved = TryResolveComponentInputValue(Node, Input, Value, Error);
			const bool bWritten = bResolved && TryWriteInputValue(Instance, Input, Value, Error);
			if (Record)
			{
				Record->Inputs.Add(InputRecord);
			}

			FDesign2UMGMappingDiagnostic Diagnostic;
			if (bWritten)
			{
				// inputs 属 D2U-owned：直接写入最新设计值，不做项目侧当前值比对、不记基线。
				Diagnostic.Kind = TEXT("mapping_input_applied");
				Diagnostic.Message = FString::Printf(
					TEXT("Input '%s' -> %s.%s (%s) overwritten with design value '%s'"),
					*Input.Source,
					*Instance->GetClass()->GetName(),
					*Input.Target,
					*DesignComponentMapping::GetComponentInputTypeName(Input.Type),
					*Value);
			}
			else
			{
				Diagnostic.Kind = TEXT("mapping_input_skipped");
				Diagnostic.Message = FString::Printf(
					TEXT("Input '%s' -> %s.%s skipped: %s; other inputs and the mapping remain active"),
					*Input.Source,
					*Instance->GetClass()->GetName(),
					*Input.Target,
					*Error);
			}
			Diagnostics.Add(MoveTemp(Diagnostic));
		}
	}
}

namespace Design2UMGComponentMappingGeneration
{
	void ApplyMappingToWidgetTreeIR(
		FUMGWidgetNode& RootNode,
		const FDesign2UMGComponentMappingConfig& Config,
		const FUMGWidgetData& WidgetData,
		FDesign2UMGComponentMappingBuildState& BuildState)
	{
		if (!Config.HasMappings())
		{
			return;
		}

		FString Provider;
		FString DocumentScope;
		FString DocumentScopeOrigin;
		if (WidgetData.HasSourceDocumentData())
		{
			const FUMGSourceDocumentData& SourceDocument = WidgetData.SourceDocument();
			Provider = SourceDocument.Provider;
			DocumentScope = SourceDocument.DocumentScope;
			DocumentScopeOrigin = SourceDocument.DocumentScopeOrigin;
		}

		if (!Provider.Equals(DesignComponentMapping::ProviderFigma, ESearchCase::IgnoreCase))
		{
			// Pencil / 未知来源不进 mapping 匹配主路径。
			BuildState.AddDiagnostic(
				TEXT("mapping_skipped"),
				FString::Printf(
					TEXT("Component mapping config ignored for provider '%s'; only figma documents are matched in v1"),
					*Provider));
			return;
		}

		if (!DocumentScope.IsEmpty())
		{
			BuildState.AddDiagnostic(
				TEXT("mapping_scope"),
				FString::Printf(
					TEXT("Component mapping active: provider=figma documentScope=%s (source: %s)"),
					*DocumentScope,
					*DocumentScopeOrigin));
		}
		else
		{
			BuildState.AddDiagnostic(
				TEXT("mapping_scope"),
				TEXT("Component mapping could not resolve a document scope (no file key and no import source identifier); no node can match"));
			return;
		}

		ApplyMappingRecursive(RootNode, DocumentScope, Config.Table, BuildState);
	}

	UUserWidget* TryCreateMappedWidget(
		UWidgetTree* WidgetTree,
		const FUMGWidgetNode& Node,
		const FName& WidgetName,
		FDesign2UMGComponentMappingBuildState& BuildState)
	{
		const DesignComponentMapping::FComponentMappingEntry* Entry =
			BuildState.FindEntryForKey(Node.Source().MappedComponentKey);
		if (!Entry)
		{
			BuildState.AddDiagnostic(
				TEXT("mapping_class_unavailable"),
				FString::Printf(
					TEXT("Mapping entry for key '%s' disappeared; node '%s' falls back to normal conversion"),
					*Node.Source().MappedComponentKey,
					*Node.Name));
			return nullptr;
		}

		const FSoftClassPath WidgetClassPath(Entry->WidgetClass);
		UClass* WidgetClass = WidgetClassPath.TryLoadClass<UUserWidget>();
		if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
		{
			BuildState.AddDiagnostic(
				TEXT("mapping_class_unavailable"),
				FString::Printf(
					TEXT("Mapped class '%s' failed to load for node '%s'; falling back to normal conversion"),
					*Entry->WidgetClass,
					*Node.Name));
			return nullptr;
		}

		// 项目 WBP 类实例直接挂进 WidgetTree（与编辑器里手动拖入 WBP 实例等价）；
		// 外层布局（位置/尺寸/slot）仍由生成链正常处理，组件内部子树不展开。
		UUserWidget* MappedInstance = WidgetTree->ConstructWidget<UUserWidget>(WidgetClass, WidgetName);
		if (!MappedInstance)
		{
			BuildState.AddDiagnostic(
				TEXT("mapping_class_unavailable"),
				FString::Printf(
					TEXT("Failed to construct mapped widget instance of '%s' for node '%s'; falling back to normal conversion"),
					*Entry->WidgetClass,
					*Node.Name));
			return nullptr;
		}

		FDesign2UMGMappedInstanceRecord Record;
		Record.DesignNodeId = Node.GetStableDesignNodeId();
		Record.WidgetName = WidgetName.ToString();
		Record.MappedKey = Entry->Key;
		Record.MappedClass = Entry->WidgetClass;
		Record.bMatchedAtSetLevel = BuildState.IsMappedAtSetLevel(Node.Id);
		BuildState.RecordMappedInstance(Record);
		return MappedInstance;
	}

	void ApplyMappedComponentInputs(
		UUserWidget* Instance,
		const FUMGWidgetNode& Node,
		FDesign2UMGComponentMappingBuildState& BuildState)
	{
		const DesignComponentMapping::FComponentMappingEntry* Entry =
			BuildState.FindEntryForKey(Node.Source().MappedComponentKey);
		if (!Entry || Entry->Inputs.Num() == 0)
		{
			return;
		}

		FDesign2UMGMappedInstanceRecord* Record =
			BuildState.FindMappedInstanceByWidgetName(Instance->GetFName().ToString());

		TArray<FDesign2UMGMappingDiagnostic> Diagnostics;
		ApplyMappedComponentInputsInternal(Instance, Node, *Entry, Record, Diagnostics);
		for (FDesign2UMGMappingDiagnostic& Diagnostic : Diagnostics)
		{
			BuildState.AddDiagnostic(MoveTemp(Diagnostic.Kind), MoveTemp(Diagnostic.Message));
		}
	}

	void ApplyMappedComponentInputsToInstance(
		UUserWidget* Instance,
		const FUMGWidgetNode& Node,
		const DesignComponentMapping::FComponentMappingEntry& Entry,
		TArray<FDesign2UMGMappedInputRecord>* OutInputRecords,
		TArray<FDesign2UMGMappingDiagnostic>& OutDiagnostics)
	{
		FDesign2UMGMappedInstanceRecord ScratchRecord;
		FDesign2UMGMappedInstanceRecord* Record = OutInputRecords ? &ScratchRecord : nullptr;
		ApplyMappedComponentInputsInternal(Instance, Node, Entry, Record, OutDiagnostics);
		if (OutInputRecords)
		{
			*OutInputRecords = MoveTemp(ScratchRecord.Inputs);
		}
	}

	void ReportInertPrototypeBindings(
		const FUMGPrototypeNavigationData& NavigationData,
		FDesign2UMGComponentMappingBuildState& BuildState,
		TArray<FDesign2UMGMappingDiagnostic>* OutDiagnostics)
	{
		// 同一条诊断既进 BuildState（导入报告出口）也进可选 OutDiagnostics（reimport 报告出口）。
		const auto ReportBoth = [&BuildState, OutDiagnostics](const TCHAR* Kind, const FString& Message)
		{
			BuildState.AddDiagnostic(Kind, Message);
			if (OutDiagnostics)
			{
				FDesign2UMGMappingDiagnostic Diagnostic;
				Diagnostic.Kind = Kind;
				Diagnostic.Message = Message;
				OutDiagnostics->Add(MoveTemp(Diagnostic));
			}
		};

		for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
		{
			// 触发点在 mapped 实例本体与在其内部子树是两种位置，消息分开说清；
			// 两者都不会生成绑定，都需要「不生效」诊断。
			if (BuildState.IsMappedNodeId(Binding.TriggerWidgetId)
				|| BuildState.IsMappedNodeId(Binding.TriggerWidgetName))
			{
				ReportBoth(
					TEXT("mapping_prototype_inert"),
					FString::Printf(
						TEXT("Design interaction (trigger '%s', type '%s') attached to mapped component is inert this phase: mapped WBP interactions are project-owned"),
						*Binding.TriggerWidgetName,
						*Binding.TriggerType));
			}
			else if (BuildState.IsInsideMappedSubtree(Binding.TriggerWidgetId)
				|| BuildState.IsInsideMappedSubtree(Binding.TriggerWidgetName))
			{
				ReportBoth(
					TEXT("mapping_prototype_inert"),
					FString::Printf(
						TEXT("Design interaction (trigger '%s', type '%s') inside a mapped component's unexpanded subtree is inert this phase: the subtree is replaced by the project-owned mapped WBP instance"),
						*Binding.TriggerWidgetName,
						*Binding.TriggerType));
			}
		}
	}
}
