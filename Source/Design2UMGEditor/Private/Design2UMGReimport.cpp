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
		// Small utilities
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

		/** "Real design node" = has a source identity (non-empty SourceNodeId); synthetic wrapper/pass-through layers do not. */
		bool HasRealSourceIdentity(const FUMGWidgetNode& Node)
		{
			return Node.HasSourceData() && !Node.Source().SourceNodeId.IsEmpty();
		}

		/** The same design identity key as the import side (single source of truth: FUMGWidgetNode::GetStableDesignNodeId). */
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

		/** Find Target's parent within the Root subtree (compared by node pointer). */
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

		/** Whether the IR node is in the "instantiable mapped form": hit a mapping Key and the mapped class is currently loadable. */
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
		 * Pruning used when the generation chain rebuilds nodes: strip manifest-instance nodes already inside the subtree.
		 * Otherwise the move case "a newly added design container holding old nodes" first builds a duplicate widget,
		 * then reparent moves the old widget in - resulting in two copies. Stripped nodes are rehung by the reparent stage.
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
		 * Pre-save writability check (the Perforce-unchecked-out target scenario):
		 * file exists and the read-only attribute is set = not writable; a missing file counts as writable (the save
		 * creates it then; there is no predictable read-only state). Interception must happen up front: the engine's
		 * SavePackage on a read-only uasset hits a fatal appError - the editor simply crashes; it cannot become a
		 * diagnostic + rollback inside the product. The write probe ("open in append mode, then close": creates no
		 * file, changes no content, only verifies a write handle is obtainable) is enabled for the sidecar only
		 * (bAllowWriteProbe): once the uasset is loaded from disk by this process (the reimport main path's normal
		 * state), the engine's read handle on the file has no write sharing, so an OpenWrite probe necessarily fails - a false positive would block saves that could succeed (the engine save path itself can write).
		 * Real external locks / IO failures on the uasset are caught by the SavePackage failure path (already in FailSaveWithRollback).
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

		/** Project binding signature: interaction trigger type -> UWidget delegate property name (same runtime delegates the generation chain binds). */
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
				// Keyboard interactions are taken over by the D2U extension at the UserWidget level; any existing keyboard event binding on the project side counts as occupied.
				return Binding.UsesKeyUpTrigger() ? TEXT("OnKeyUp") : TEXT("OnKeyDown");
			}
			return TEXT("OnClicked");
		}

		/** Whether the interaction is already project-bound (the WBP Bindings carry a function). Keyboard triggers match any object's keyboard event binding. */
		bool HasProjectBindingForInteraction(
			const UWidgetBlueprint* WBP,
			const FUMGPrototypeNavigationBinding& Binding)
		{
			const FName PropertyName = GetInteractionDelegateProperty(Binding);
			// The trigger control is registered in Bindings by widget FName (the generation chain slug-cleans node ids and
			// handles digit prefixes); the raw node id (real Figma ids contain ':' etc.) cannot be compared directly;
			// derive the base name with the same cleaning rules and match on it, consistent with the nested same-name semantics.
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

		/** Whether any widget in the subtree is referenced by project bindings (WBP Bindings reference by ObjectName). */
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
		 * Whether any widget in the subtree is referenced as a variable by blueprint event graphs (Get/Set nodes).
		 * UWidget defaults to bIsVariable=true, so users can reference D2U widgets in graphs without bindings;
		 * the engine's DeleteWidgets(DeleteSilently) internally removes variable nodes per
		 * FBlueprintEditorUtils::IsVariableUsed - the same query is used here as the up-front check so the signal
		 * stays symmetric with what the deletion path actually removes.
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
		 * Pre-check for safe delete / identity conversion: the subtree must be purely D2U-owned.
		 * "Project content" signals = WBP Bindings referencing any widget in the subtree, widgets in the subtree
		 * outside the manifest.d2uOwned list (project-added children), or widgets in the subtree referenced as
		 * variables by blueprint graphs (silent deletion would also remove the user's blueprint nodes).
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
		 * The safe whole-delete shared by 9a (removed) and 9b (kind-changed replacing the old form):
		 * collect subtree widget names -> delete silently -> remove them from the owned list. Returns the subtree name
		 *set (for diagnostics to name / later cleanup). Both sites must stay semantically identical - consolidated here to prevent parallel evolution.
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
		 * Second conservative gate before the 9a/9b whole delete: does the subtree still hold manifest instance widgets
		 * "alive in this round's update plan" (Matched/KindChanged - design nodes still present in the new source)?
		 * Whole-deleting such a subtree destroys the live instances with it; reparent/refresh can never recover them
		 * by widgetName afterwards (design nodes lost from the asset + manifest ghost records without widgets).
		 * So, exactly like with project content, the subtree root is kept conservatively and instances take their own move/refresh plans.
		 * ExcludeWidgetName excludes the subtree root being processed itself (replacing/deleting it is this round's very intent).
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
		// Update plan
		// ---------------------------------------------------------------------------

		enum class EReimportPlanAction : uint8
		{
			Matched,      // designNodeId present on both sides with the same kind -> update D2U-owned properties/layout (incl. the reparent decision)
			KindChanged,  // present on both sides but the mapped<->generated kind flipped -> handled by the identity-conversion rules
			Removed,      // in the manifest but not in the new IR -> safe-delete decision
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
		 * Reimport session state shared across stages: the common parameter carrier of the early-exit functions and
		 * HandleFullConflict (mirroring the apply-stage FReimportApplyContext so the Document/Request/
		 * Data/GenerationSettings quartet is not repeated verbatim in every early-exit signature and every
		 * HandleFullConflict call site). GenerationSettings is available after settings-snapshot resolution (step 1).
		 */
		struct FReimportSession
		{
			FDesignDocument& Document;
			const FDesignReimportRequest& Request;
			FUMGWidgetData& Data;
			const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings;
		};

		/**
		 * Full conflict: keep the asset + diagnostics; generate a sibling copy only on explicit user choice (original asset and manifest untouched).
		 * PriorDiagnostics carries the diagnostics already produced by the matching stage (scope matched etc.): the spec
		 * requires source/instance/ownership match classification to complete first, and those diagnostics must return with the result, never dropped.
		 */
		FDesignReimportResult HandleFullConflict(
			const FString& Reason,
			const FReimportSession& Session,
			TArray<FDesign2UMGMappingDiagnostic>&& PriorDiagnostics)
		{
			FDesignReimportResult Result;
			Result.Diagnostics = MoveTemp(PriorDiagnostics);
			// ini validation diagnostics were produced during snapshot resolution (step 1); the early-exit report must carry
			// them, or they vanish from the report when a config error stacks onto an asset-level conflict.
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
			// The copy-generation chain's mapping diagnostics (ini validation, hits, the copy's own sidecar write failure) land in the report.
			Result.ComponentMappingDiagnostics.Append(GenerationOutput.MappingDiagnostics);
			DesignImportDiagnosticReport::WriteReimport(
				Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
			return Result;
		}

		// ---------------------------------------------------------------------------
		// Apply stage
		// ---------------------------------------------------------------------------

		struct FReimportApplyContext
		{
			UWidgetBlueprint* WBP = nullptr;
			UWidgetTree* WidgetTree = nullptr;
			UPanelWidget* HostPanel = nullptr;
			FIntPoint DesignSize = FIntPoint::ZeroValue;
			/** Set view of the manifest.d2uOwned list (data source of the delete/move ownership decisions). */
			const TSet<FString>* OwnedNames = nullptr;
			const TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>* InstancesById = nullptr;
			FDesign2UMGComponentMappingBuildState* MappingBuildState = nullptr;
			TArray<FDesign2UMGMappingDiagnostic>* Diagnostics = nullptr;
			TArray<FDesign2UMGMappedInstanceRecord>* UpdatedMappedRecords = nullptr;
			TArray<FDesign2UMGGeneratedInstanceRecord>* UpdatedGeneratedRecords = nullptr;
			/** Widgets newly created this round (designNodeId -> widget); referenced when resolving reparent targets. */
			TMap<FString, UWidget*>* NewlyCreatedWidgets = nullptr;
			/**
			 * Move-candidate pool of the nested-structure diff (collected by the main-flow pre-scan): widgets inside the owned
			 * list, not manifest instances, and no longer expected by the new design to stay in their original container.
			 * The container-subtree diff consumes move sources from the pool by deterministic same-name; unconsumed ones go to leftover-deletion diagnostics (pure-D2U silently kept).
			 */
			TArray<UWidget*>* NestedMoveCandidates = nullptr;
			/** The 9a-10 shared owned-list evolution (initial = manifest.d2uOwned, updated as deletions/additions happen). */
			TSet<FString>* UpdatedD2UOwned = nullptr;
			/** Conflict-kept list (9a/9b keeps; 10 backfills manifest records). */
			TArray<FString>* KeepAsIsWidgetNames = nullptr;
			/** Manifest instance widget names still alive in this round's update plan (the second gate of the 9a/9b whole delete). */
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
		 * Mapped Class change conflict check (shared by the 9e plan stage and the RefreshMappedInstance walk):
		 * the instance's actual class != the manifest-recorded class, or the class the mapping currently points at !=
		 *the recorded class - either is a conflict (the project replaced the instance / the ini changed the target). A currently unfindable mapping entry compares as an empty string.
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
		 * The mapped instance's own visual properties (visibility/opacity) are written with the generic properties at
		 * import, but reimport does not refresh them per the "mapped interior belongs to the project" boundary.
		 * Divergences between the declared design value and the instance's current state are named in a report; properties already overwritten by an explicit mapping input are skipped - the declared value is that property's true design intent, and it is already refreshed.
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
			// Walk-side guard for the Mapped Class change conflict: if the manifest-recorded mapped instance's class
			// mismatches the record, or the mapping now points at another class, do not refresh (the 4.4 conflict
			// semantics are owned by the plan stage's reimport_mapped_class_conflict diagnostic; staying silent here avoids duplicate diagnostics).
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

			// Within one round, the same instance may be refreshed twice - once by the 9e update plan, once by the parent
			//instance's nested diff (both routes reach mapped instances under a switcher host). The dedup check runs
			// before side effects: the second arrival skips outright, avoiding duplicate slot/inputs application and
			// duplicate mapping_input_applied diagnostics (the manifest records one entry, one refresh diagnostic).
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

			// mapped instances update only the outer layout + explicit inputs (direct overwrite); the interior is untouched.
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
		 * Move source-side ownership check (spec R3 "both endpoints are D2U-owned"; the top-level reparent
		 * and nested moves share one rule):
		 * 1) the source parent container must be D2U-owned (the host root or an owned container) - a developer hanging a node
		 *    under a project container has taken it over; moving it away would break the project-side structure;
		 * 2) the source subtree must be purely D2U-owned (list check + binding check, the same rule as the top-level safe delete).
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

		/** Result of MatchNestedChildren: a deterministic pairing of design children with existing owned child widgets. */
		struct FNestedChildrenMatch
		{
			struct FMatchedPair
			{
				const FUMGWidgetNode* Child = nullptr;
				UWidget* Widget = nullptr;
			};
			/** Paired (direct manifest-identity match, or the k-th same-named design child to the k-th suffixed existing widget). */
			TArray<FMatchedPair> Matched;
			/** Design has it, no local owned candidate (a nested add or the target end of a nested move). */
			TArray<const FUMGWidgetNode*> UnmatchedDesignChildren;
			/** Locally owned, no longer expected by the design (a nested-delete leftover or the source end of a nested move). */
			TArray<UWidget*> UnmatchedOwnedWidgets;
		};

		/**
		 * Whether a design child is a "synthetic wrapper layer": an intermediate layer the converter inserted to carry
		 * layout/visuals, not a real node of the design. Two shapes:
		 * 1) layers without a source identity (a canvas container's <name>_inner, the content layer of a prototype trigger-button wrapper, ...);
		 * 2) pass-through wrappers (SizeBox etc.): the wrapper copies its only child's source identity (same design id);
		 *    semantically the wrapper and its content are one design node.
		 * Wrapper layers do not become their own matching tier: on pairing failure, matching continues by piercing to the
		 * content, so nested same-name matching never breaks on synthetic layers (lost moves, duplicate copies, missing conflict diagnostics).
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
		 * Manifest instances pair directly by record first (a single-child attempt within one MatchNestedChildren round):
		 * the mapped instance's widget name comes from the import-time design name, but the same-named node on the
		 * reimport-time IR side may be a wrapper form whose name the converter rewrote (e.g. trigger nodes named by id
		 * under a switcher document) - same-name matching fails. Identity must be decided by the manifest record, or
		 * an existing instance gets mistaken for an addition and rebuilt. Returns true = terminally handled (paired, or
		 * the recorded widget hangs under another parent and the reparent stage takes over); returns false = no usable record, fall to same-base-name matching.
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
		 * Same-base-name matching (a single-child attempt within one MatchNestedChildren round): the k-th same-named
		 * (base name) design child pairs with the k-th suffixed existing widget, consistent with the import-time
		 * SanitizeWidgetName + _N dedup generation order. Returns whether pairing succeeded;
		 * exhausted candidates are classified as unmatched by the caller.
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
		 * Deterministic same-name pairing of the container subtree (count only, no mutation). The executing walk
		 * (RefreshChildWidgets) and the pre-scan (CollectExpectedNestedNames) share this one computation so both passes decide identically.
		 */
		FNestedChildrenMatch MatchNestedChildren(const FUMGWidgetNode& Node, UPanelWidget* Panel, const FReimportApplyContext& Ctx)
		{
			FNestedChildrenMatch Out;

			// Only child widgets inside the owned list participate in the pairing; project-side additions do not.
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

			// Design children start from the direct children; after each round, still-unpaired synthetic wrapper layers are
			// flattened by content into the next round (wrapper chains are finite, so it converges). Paired widgets leave
			// the candidate pool immediately; no cross-round double pairing.
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

				// Flatten round: still-unpaired synthetic wrapper layers are replaced by their content children for pairing.
				// Content-less synthetic layers (justify spacers etc.) stay unmatched and take the regular rebuild path.
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
		 * Pre-scan: simulates 9e's nested recursive refresh and collects the owned widget names "still expected by the
		 * new design to stay in their original container". Owned non-instance widgets outside this set = not expected
		 * by the design (delete leftovers or nested move sources); they enter the move-candidate pool.
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
				// mapped candidates do not expand their subtree (RefreshMappedInstance does not recurse into children).
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
		 * Nested move decision and execution (the unmatched-child path of RefreshChildWidgets): when the candidate
		 * pool holds a same-named owned subtree no longer expected by the design elsewhere, move it over wholesale
		 * (identity preserved). mapped candidates never move (instantiation semantics; rebuilt directly).
		 * Returns true = handled (move succeeded / move rejected / move failed and rehung - no rebuild either way);
		 * returns false = no candidate in the pool; the generation chain rebuilds.
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
				// After the move, refresh properties/slots and the whole subtree per the new design node (including nested diagnostics).
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
				// Defensive: if it cannot be added back, rehang under the original parent (the source side was already judged D2U-owned); never leave an orphan widget.
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
		 * Design-added nested nodes go through the generation-chain rebuild (the unmatched-child path of
		 * RefreshChildWidgets): mapped candidates instantiate as usual and their names enter the new d2uOwned;
		 * manifest instances already inside the subtree are pruned (rehung by reparent).
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
		 * Property refresh of matched generated subtrees: pair new IR nodes with existing widgets by "deterministic naming".
		 * Only widgets inside the owned list are touched (project-side additions/renames are left alone);
		 * nested subtrees the design dropped keep their current state (diagnostics handled by the main flow 9g:
		 * project content reports a remove conflict; pure D2U is silently kept);
		 * design-added nested nodes first consult the nested move candidates (spec R3: move the widget and preserve
		 * identity when both endpoints are D2U-owned; conflict and no copy when the source side holds project content); only then does the generation chain rebuild.
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

			// Only top-level generation relations (hung under the host root) enter the manifest instances; nested nodes only refresh properties.
			if (Ctx.HostPanel && Widget->GetParent() == Ctx.HostPanel)
			{
				FDesign2UMGGeneratedInstanceRecord Record;
				Record.DesignNodeId = GetDesignNodeId(Node);
				Record.WidgetName = Widget->GetFName().ToString();
				Ctx.UpdatedGeneratedRecords->Add(MoveTemp(Record));
			}
			// Property-refresh diagnostics are as granular as the top level: nested D2U-owned node refreshes are also named per node.
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

			// Double-trigger conflict detection (update-plan stage): the same interaction already has a project binding -> the D2U binding yields; the project behavior stays.
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

			// Design interactions on a mapped identity are inert (the same boundary rule as at import); the specific entries also
			// reach the reimport report (reimport does not read BuildState's generation-time diagnostics; they must be carried out explicitly).
			// mapping_-prefixed diagnostics belong solely to the componentMapping section (the QA classification contract):
			// here they go into BuildState only, the out-param is nullptr, and the main flow later collects BuildState
			// diagnostics into Result.ComponentMappingDiagnostics - never into the reimport section.
			Design2UMGComponentMappingGeneration::ReportInertPrototypeBindings(
				FilteredNavigationData, *Ctx.MappingBuildState, nullptr);

			switch (GenerationSettings.PrototypeNavigationGenerationMode)
			{
			case EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension:
				// Refresh binding data only; do not rebuild transition-animation assets (avoid copying one animation per reimport).
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
		// Reimport stage functions: the main flow (Reimport() below) orchestrates them in steps 1)-11)
		// ---------------------------------------------------------------------------

		/** Steps 1)-3): settings snapshot / re-parse the source / locate the target asset + read the manifest; failure exits early as a full conflict. */
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
			// 1) Resolve the settings snapshot (same origin as the import path: ini validation diagnostics enter the generation chain with the snapshot).
			SettingsSnapshot =
				Request.SettingsSnapshot.IsSet()
					? Request.SettingsSnapshot.GetValue()
					: FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
			const FReimportSession Session{ Document, Request, Data, SettingsSnapshot.GenerationSettings };

			// 2) Re-parse the source (image parsing + Convert into the new IR).
			FImportAnalysisInput AnalysisInput;
			AnalysisInput.bResolveImages = true;
			AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
			const FImportAnalysisResult AnalysisResult = AnalyzeImport(Document, AnalysisInput);
			Data = AnalysisResult.Data;

			// 3) Locate the target asset + read the manifest.
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

		/** Steps 4)-5): DocumentScope matching, source-root location and host continuation; failure exits early as a full conflict. */
		TOptional<FDesignReimportResult> MatchReimportScopeAndRoot(
			const FReimportSession& Session,
			const FDesign2UMGGenerationManifestSidecar& Manifest,
			FDesignReimportResult& Result,
			const FUMGWidgetNode*& ScopeRoot,
			bool& bPrototypeHostContinued)
		{
			const FUMGWidgetData& Data = Session.Data;
			// 4) DocumentScope matching: a mismatch of provider or scope = unclear relationship, full conflict.
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

			// 5) Locate the manifest-recorded sourceRoot in the new IR (the page/root-node relation).
			// An empty sourceRoot identity = the synthesized switcher host root of a single-page interactive document (only
			// multi-page documents get a fingerprint token id; see the PrototypePages.Num() > 1 branch of BuildAnalysis) -
			// this is the current identity rule, not legacy-sidecar compatibility: on empty, the new IR root simply is the scope.
			ScopeRoot = Manifest.SourceRootNodeId.IsEmpty()
				? &Data.RootWidget
				: FindNodeByDesignId(Data.RootWidget, Manifest.SourceRootNodeId);
			// Host continuation: the synthesized host root id of a multi-page prototype is a content fingerprint of the page set (BuildPrototypeInternalTokenHash =
			// CRC32 of each page's "id|name;" concatenation); adding/removing pages necessarily rewrites it, but a content fingerprint is not the document identity -
			// provider + documentScope already matched in step 4, and when the new source is itself still a multi-page prototype, this is the same
			// host's page-set evolution, not an "unclear relationship": do not reject on a differing fingerprint; continue the existing structure
			// diff with the new source's actual host root as the scope (added pages go through added, removed pages through safe delete), and record the new fingerprint in the manifest on success.
			// The decision is narrowed to "old and new hosts are both switcher bodies": a record id with the _root suffix = an overlay wrapper root;
			// the new root's design id must be exactly the new source's SwitcherWidgetId (the switcher body's synthesized root). The
			// overlay wrapper root's non-instance children are synthetic layers (switcher/overlay hosts), not pages - admitting them would
			// hang them at the wrong level as top-level additions; a host-form flip (overlay appearing/disappearing) keeps the conservative
			// full-conflict semantics just like the single-page downgrade (the latter is the settled behavior of slice C CASE-011).
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

		/** Steps 6)-7): Component Mapping two-tier match (on an IR copy) + update-plan construction. */
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
			// 6) Component Mapping: two-tier match on the IR copy before generation (hit Keys go into the copy; the source data stays untouched).
			PreparedScopeRoot = *ScopeRoot;
			Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
				PreparedScopeRoot, GenerationSettings.ComponentMapping, Data, MappingBuildState);

			// 7) Update plan: manifest instances <-> new IR nodes paired by designNodeId.
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

			// Instance widget names still alive in this round's update plan (Matched/KindChanged = still present in the design-side new source).
			// Used as the second conservative gate before the 9a/9b subtree whole delete, preventing live instances from being deleted along the way (see the function comment).
			for (const FReimportPlanEntry& Entry : Plan)
			{
				if (Entry.Action != EReimportPlanAction::Removed)
				{
					AliveInstanceWidgetNames.Add(Entry.ManifestRecord->WidgetName);
				}
			}
		}

		/** Step 8): structural sanity check; failure exits early as a full conflict. */
		TOptional<FDesignReimportResult> CheckReimportStructureViability(
			const FReimportSession& Session,
			UWidgetBlueprint* WBP,
			const FDesign2UMGGenerationManifestSidecar& Manifest,
			const FUMGWidgetNode& PreparedScopeRoot,
			FDesignReimportResult& Result,
			FIntPoint& DesignSize)
		{
			// 8) Structural sanity: target tree missing / not even one manifest instance locatable = project-side restructure, full conflict.
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

		/** Step 9) transaction prelude: open the editor transaction + add RF_Transactional to the existing tree; returns the transaction index (INDEX_NONE = no transaction). */
		int32 BeginReimportApplyTransaction(UWidgetBlueprint* WBP)
		{
			// Note: FScopedTransaction::Cancel only discards the transaction record, it does not roll objects back; a real
			// rollback requires an explicit Undo after End. Bare Begin/End is used here so the save-failure path can
			// restore the asset's original state. Nesting defense: editor transactions nest by ref-count - if an outer
			// transaction is already active, the one we Begin is not an independent undo entry (End only decrements the
			// count), and UndoTransaction after End would undo an unrelated user transaction further down the undo stack.
			// So no own transaction is opened while an outer transaction is active (Modify snapshots still enter the outer transaction, undone by it as a whole).
			const bool bCanOwnTransaction = GEditor && GEditor->CanTransact() && !GEditor->IsTransactionActive();
			const int32 TransactionIndex = bCanOwnTransaction
				? GEditor->BeginTransaction(
					TEXT("Design2UMG Reimport"),
					NSLOCTEXT("Design2UMG", "SafeReimportTransaction", "Design2UMG Safe Reimport"),
					WBP)
				: INDEX_NONE;

			// Widgets built by the generation chain lack RF_Transactional by default; the transaction captures no Modify
			// snapshot, so a save-failure rollback could not restore properties (the UMG editor's own delete/edit flows add the flag explicitly, same as here).
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
			// 9a) removed: safe-delete decision (delete only when purely D2U-owned; keep + conflict diagnostics when project content is present).
			for (const FReimportPlanEntry& Entry : Plan)
			{
				if (Entry.Action != EReimportPlanAction::Removed)
				{
					continue;
				}

				UWidget* Widget = Ctx.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
				if (!Widget)
				{
					// Already gone from the asset: only remove it from the new manifest records.
					Ctx.UpdatedD2UOwned->Remove(Entry.ManifestRecord->WidgetName);
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_removed"),
						FString::Printf(
							TEXT("Design node '%s' (%s) removed from source; widget already absent from the tree"),
							*Entry.ManifestRecord->WidgetName, *Entry.ManifestRecord->DesignNodeId));
					continue;
				}

				// Conservative gate 1 (design-side signal): the subtree holds instances alive this round -> no whole delete.
				// Common scenario: the design deletes the old container and moves its content into a new one; whole-deleting the
				// old container destroys the content instances, and reparent can never recover them afterwards. Keep the subtree
				// root so the instances take their own move plans and rehang under the new container (identity-preserving move).
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
				// Besides the subtree root, the diagnostic also names the other widget names inside the subtree: in the
				// single-page empty-root scenario the deleted manifest instance is a host synthetic layer (<root>_inner, no
				// source node id) - reporting only it would lose the identities of the actually deleted design nodes; widget
				// names derive deterministically from design node names and are a usable design identity outside the manifest.
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
			// 9b) kind-changed: the old subtree is replaced with the new form only when purely D2U-owned; otherwise kept as a conflict.
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

				// Conservative gate (design-side signal): the old subtree holds other instances alive this round -> no whole-delete replace.
				// A whole delete would destroy them, and the pruned new form would not rebuild them either -> the instances are truly lost.
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

				// The new-form subtree likewise prunes already-present manifest instances (rehung by reparent; not generated twice).
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
			// 9c) added: new top-level purely generated nodes (including new mapped instances) hang under the host root
			//     first, so the later reparent can pair nodes the design moved into new containers under them.
			//     When the scope root itself is a manifest instance (the host-root form changed between imports, e.g. a
			//     synthesized host appears/disappears as interactions change), its children belong to that instance's nested
			//     refresh scope (handled by the 9e recursion) and must not hang under the host root as "host-root top-level additions".
			if (Ctx.HostPanel && !Ctx.InstancesById->Contains(GetDesignNodeId(PreparedScopeRoot)))
			{
				for (const FUMGWidgetNode& Child : PreparedScopeRoot.Children)
				{
					if (Ctx.InstancesById->Contains(GetDesignNodeId(Child)))
					{
						continue;
					}

					// Prune manifest instances already inside the subtree (they are rehung by reparent, not generated twice).
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

		/** Result of the reparent target-parent resolution. */
		struct FReparentTargetResolution
		{
			UPanelWidget* TargetPanel = nullptr;
			bool bUnresolvable = false;
			bool bAlreadyInPlace = false;
		};

		/** The widget's current parent shares the base with the IR parent node (import naming rules) = the design never asked to move it. */
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
		 * Reparent target-parent resolution (walk up from the IR direct parent):
		 * - wrapper nodes inserted by the converter (no source node id) are skipped outright;
		 * - a real design node must resolve onto a manifest instance or a widget created this round; otherwise the boundary
		 *   is unclear (bUnresolvable, conflict-kept); reaching the scope root = rehang under the host root;
		 * - current parent and IR parent sharing the base (synthetic-layer exemption / resolution-failure exemption share the check) = no move asked.
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

			// A synthetic direct parent layer (inner etc., no source node id) pairs with the current parent by name: without
			// this check first, the target would resolve to the real node above the synthetic layer (typically a
			// single-content Border container) whose content slot the synthetic layer already occupies -> false move conflicts.
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
					// The target parent is a purely generated node created this round: likewise clearly D2U-owned.
					AncestorWidget = *NewlyCreated;
				}

				if (AncestorWidget)
				{
					if (UPanelWidget* Panel = Cast<UPanelWidget>(AncestorWidget))
					{
						Resolution.TargetPanel = Panel;
					}
					// Resolved to a widget but not a panel (a content widget): keep walking up.
				}
				else if (bRealDesignNode)
				{
					// A real design node with unclear ownership: boundary unclear -> conflict-kept.
					Resolution.bUnresolvable = true;
				}
				AncestorNode = FindParentNode(ScopeRoot, *AncestorNode);
			}

			// No-conflict exception: resolution failed, but the widget's current parent is exactly the deterministic counterpart
			// of the IR direct parent (same-base-name hit) - reporting "reparent skipped" would contradict the actual behavior, so let it pass silently.
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
			// 9d) reparent decision for matched (move/reparent only when both ends are clearly D2U-owned).
			for (const FReimportPlanEntry& Entry : Plan)
			{
				if (Entry.Action != EReimportPlanAction::Matched || !Ctx.HostPanel)
				{
					continue;
				}

				// The scope root itself is this instance (the host-root-form-change scenario): it is the new design's page
				// root, so "move elsewhere" has no meaning; skip reparent (9e refreshes its nested content as usual).
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
					// Source-side ownership check (the same rule as nested moves): the widget currently hangs under a
					// project-side container = the developer has taken over its placement; the design-side reparent must not drag it back - conflict-kept + diagnostic.
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

					// The target is a single-content widget whose slot is already occupied (AddChild would reject it and create an
					// orphan): boundary unclear -> conflict-kept; the structure is untouched.
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
						// After the move, drop the new slot first; the full refresh (properties/inputs/manifest record) is done
						// uniformly by 9e (this node is a Matched entry, so 9e finds it by widgetName for sure; refreshing here
						// too would write the mapped record twice into the new manifest).
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
						// Defensive: if it cannot be added back, rehang under the host root; never leave an orphan widget.
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
			// 9d-2) nested-structure diff pre-scan: collect widgets "inside the owned list, not manifest instances, and no
			//       longer expected by the new design to stay in their original container" as the nested move-candidate pool.
			//       9e's container-subtree diff consumes move sources from the pool by deterministic same-name; unconsumed ones go to 9g's leftover-deletion diagnostics.
			TArray<UWidget*> NestedMoveCandidates;
			{
				TSet<FString> ExpectedNames;
				if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* ScopeRootRecord =
					Ctx.InstancesById->Find(GetDesignNodeId(PreparedScopeRoot)))
				{
					// The scope root itself is an instance: the expected set starts from that instance widget's children.
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
						// Recurse into instance children only; non-instance children are subtrees newly created by 9c this round (brand new, no leftover semantics).
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
			// 9e) matched: update D2U-owned properties/layout/slot/inputs; the Mapped Class change is intercepted here.
			for (const FReimportPlanEntry& Entry : Plan)
			{
				if (Entry.Action != EReimportPlanAction::Matched)
				{
					continue;
				}

				UWidget* Widget = Ctx.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
				if (!Widget)
				{
					// node_missing was already recorded in 9d and the manifest entry backfilled.
					continue;
				}

				// Mapped Class change = high-risk structural change: no replace, no update; conflict diagnostics (reporting old and new classes).
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
			// 9g) nested-delete leftover diagnostics: owned subtrees in the candidate pool unconsumed by moves = nested nodes
			//     the new design no longer expects. With project content (project children / developer bindings) -> kept +
			//     conflict diagnostic naming them (spec R3 "Removed node contains project-side content" is not top-level-limited);
			//     purely D2U nested subtrees keep their current state with no diagnostic (nested deletion is outside the safe-delete scope; the accepted granularity).
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

		/** Step 10): assemble the post-reimport manifest payload (including preview-size refresh and GUID backfill). */
		FDesign2UMGGenerationOutput BuildReimportManifestPayload(
			const FDesign2UMGGenerationManifestSidecar& Manifest,
			bool bPrototypeHostContinued,
			const FUMGWidgetData& Data,
			FReimportApplyContext& Ctx)
		{
			// 10) Assemble the post-reimport manifest payload.
			FDesign2UMGGenerationOutput GenerationOutput;
			GenerationOutput.Manifest.Provider = Manifest.Provider;
			GenerationOutput.Manifest.DocumentScope = Manifest.DocumentScope;
			// Under host continuation the source-root identity is refreshed from the new source (the new page-set
			// fingerprint), so the next reimport benchmarks against it; the recording rule matches the import side:
			GenerationOutput.Manifest.SourceRootNodeId = bPrototypeHostContinued
				? GetDesignNodeId(Data.RootWidget)
				: Manifest.SourceRootNodeId;
			GenerationOutput.Manifest.SourceRootName = bPrototypeHostContinued
				? Data.RootWidget.Name
				: Manifest.SourceRootName;
			GenerationOutput.Manifest.MappedInstances = MoveTemp(*Ctx.UpdatedMappedRecords);
			GenerationOutput.Manifest.TopLevelGenerated = MoveTemp(*Ctx.UpdatedGeneratedRecords);

			// Instances kept as-is (conflicts/location failures): backfill their original records into the new manifest.
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

			// Instances newly created by this round's generation chain (top-level added / kind-changed new forms / nested new mapped) enter the new manifest.
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

			// New d2uOwned = (old set - deleted names) U names created this round; then drop names no longer present in the tree.
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
		 * Asset finalization at the tail of step 10) (asset-side side effects unrelated to manifest assembly):
		 * refresh the CDO preview size when the design root size changed (written back by SaveWidgetBlueprint after compiling);
		 * backfill missing variable GUIDs of generation-chain-created widgets with the compiler's own deterministic rule.
		 */
		void FinalizeReimportedAssetState(FReimportApplyContext& Ctx)
		{
			if (UUserWidget* CDO = Cast<UUserWidget>(
				Ctx.WBP->GeneratedClass ? Ctx.WBP->GeneratedClass->GetDefaultObject() : nullptr))
			{
				// The CDO lacks RF_Transactional by default (the WidgetTree got it above; same reasoning here):
				// without the flag, Modify never enters the transaction buffer, and a save-failure rollback / user undo would leave the preview size at the new value.
				CDO->SetFlags(RF_Transactional);
				CDO->Modify();
				CDO->DesignTimeSize = FVector2D(Ctx.DesignSize.X, Ctx.DesignSize.Y);
				CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
			}

			// Widgets newly created by this round's generation chain (plus test injections via ConstructWidget) have no
			// variable GUID, and the compiler ensures on missing entries. Backfill with the compiler's own deterministic
			// rule; existing GUIDs stay untouched. Modify first to capture the GUID map's pre-transaction state: on a
			// save-failure rollback the map restores with the asset, or the post-rollback recompile would trip the compiler's ensure on a "deleted widget's leftover GUID".
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

		/** Step 11): writability pre-check + compile & save + failure rollback (FailSaveWithRollback moved with this block). */
		FDesignReimportResult SaveReimportedAsset(
			const FReimportSession& Session,
			const FString& TargetPackage,
			UWidgetBlueprint* WBP,
			FDesign2UMGGenerationOutput& GenerationOutput,
			int32 TransactionIndex,
			FDesignReimportResult& Result)
		{
			// 11) Pre-save writability pre-check + compile + save + write the new manifest.
			// Unified save-failure exit: transaction rollback + one recompile pulling the generated class back in line with the template tree + a reimport_save_failed diagnostic.
			const auto FailSaveWithRollback = [&](const FString& Reason)
			{
				// The rollback state is reported as the transaction ownership really is: with no own transaction (transacting
				// unavailable or nested inside an active outer transaction), no End/Undo happens; the disk is untouched and the in-memory changes stay in place.
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
				// The rollback restores only the template tree; class objects produced by compiling do not roll back with it -
				// one recompile pulls the generated class back into the state of the (restored) template tree.
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
				FKismetEditorUtilities::CompileBlueprint(
					WBP,
					EBlueprintCompileOptions::SkipDefaultObjectValidation | EBlueprintCompileOptions::SkipFiBSearchMetaUpdate);
				Result.bSucceeded = false;
				// Reimport finished: per-node diagnostics land in the structured report (the same exit as import; see design.md sections 0/6).
				DesignImportDiagnosticReport::WriteReimport(
				Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
			};

			// 11a) Pre-check: do not enter the save flow when the target file is not writable. The uasset checks only its
			// read-only attribute (a write probe would be falsely killed by the engine's read handle on the in-process
			// loaded state; see IsFileWritableForSave); the sidecar is our own JSON that the engine never loads, so no
			// read-handle false positives exist - keep the read-only + write-probe double check to prevent the half-update
			// "uasset updated + sidecar write failed". Perforce-unchecked-out target scenario: a read-only uasset makes the
			// engine's SavePackage hit a fatal appError (the editor crashes outright; no product-grade diagnostic possible). Intercept it: the save never happens (the disk stays as-is).
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

			// 11b) Compile + save + write the new manifest; failure = transaction rollback, asset keeps its original state, manifest not updated.
			if (!FUMGGenerator::SaveWidgetBlueprint(WBP, &GenerationOutput))
			{
				FailSaveWithRollback(FString::Printf(
					TEXT("Save/compile failed for %s"),
					*TargetPackage));
				return Result;
			}

			// 11c) Sidecar write-failure propagation: uasset on disk but manifest not updated = asset and record out of
			//      sync, which must not be reported as success (IO failures the pre-check cannot catch, like a full disk, land
			//      here). The disk already holds the new asset: memory is not rolled back (that would desync memory and disk); the transaction concludes normally.
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
			// Reimport finished: per-node diagnostics land in the structured report (the same exit as import; see design.md sections 0/6).
			DesignImportDiagnosticReport::WriteReimport(
				Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
			return Result;
		}

	}

	// ---------------------------------------------------------------------------
	// Reimport main flow
	// ---------------------------------------------------------------------------

	FDesignReimportResult Reimport(FDesignDocument& Document, const FDesignReimportRequest& Request)
	{
		FDesignReimportResult Result;

		// 1)-3) Settings snapshot / re-parse the source / locate the target asset + read the manifest.
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

		// 4)-5) Scope matching, source-root location and host continuation.
		const FUMGWidgetNode* ScopeRoot = nullptr;
		bool bPrototypeHostContinued = false;
		if (TOptional<FDesignReimportResult> Early = MatchReimportScopeAndRoot(
			Session, Manifest, Result, ScopeRoot, bPrototypeHostContinued))
		{
			return MoveTemp(*Early);
		}

		// 6)-8) Component Mapping two-tier match, update-plan construction, structural sanity check.
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

		// 9) Apply the update plan inside an editor transaction.
		const int32 TransactionIndex = BeginReimportApplyTransaction(WBP);
		TArray<FDesign2UMGMappedInstanceRecord> FinalMappedRecords;
		TArray<FDesign2UMGGeneratedInstanceRecord> FinalGeneratedRecords;
		TSet<FString> UpdatedD2UOwned(Manifest.D2UOwned);
		const TSet<FString> ManifestOwnedNames(Manifest.D2UOwned);
		TArray<FString> KeepAsIsWidgetNames;
		// Widgets newly created by this reimport round (designNodeId -> widget), used by reparent target resolution:
		// new containers (design-added nodes) are also clearly D2U-owned.
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

		// 9d-2) Nested-structure diff pre-scan (the move-candidate pool).
		TArray<UWidget*> NestedMoveCandidates = CollectNestedMoveCandidates(PreparedScopeRoot, Ctx);
		Ctx.NestedMoveCandidates = &NestedMoveCandidates;

		// 9e)-9g) Property updates / nested-delete leftover diagnostics (step comments live with their functions).
		ApplyMatchedPlanEntries(Plan, Ctx, GenerationSettings);
		// 9f) Prototype binding refresh (ordinary generated widgets, by stable design identity; double-trigger yields).
		ApplyPrototypeRefresh(Data, GenerationSettings, Ctx);
		ReportNestedRemovalLeftovers(Ctx);

		// Mapping diagnostics exit (aligned with the import path's UMGGenerator assembly): the ini validation
		// diagnostics + this round's BuildState-collected match/fallback diagnostics go into
		// ComponentMappingDiagnostics, written by the report into the componentMapping section; previously the reimport report lacked this exit, hiding config errors like class-load failures.
		Result.ComponentMappingDiagnostics = GenerationSettings.ComponentMapping.ValidationDiagnostics;
		Result.ComponentMappingDiagnostics.Append(MappingBuildState.GetDiagnostics());

		// 10) Assemble the post-reimport manifest payload + asset finalization (CDO preview size, variable GUIDs).
		FDesign2UMGGenerationOutput GenerationOutput = BuildReimportManifestPayload(
			Manifest, bPrototypeHostContinued, Data, Ctx);
		FinalizeReimportedAssetState(Ctx);

		// 11) Writability pre-check + save + FailSaveWithRollback.
		return SaveReimportedAsset(Session, TargetPackage, WBP, GenerationOutput, TransactionIndex, Result);
	}
}
