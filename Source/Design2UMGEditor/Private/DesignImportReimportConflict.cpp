// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"

#include "DesignImportDiagnosticReport.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"

namespace DesignImportPipeline
{
	namespace
	{
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

	namespace
	{
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
}
