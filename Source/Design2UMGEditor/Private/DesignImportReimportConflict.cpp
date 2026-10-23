// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"
#include "DesignImportGeneration.h"

#include "Design2UMGAssetRegistryAccess.h"
#include "DesignImportDiagnosticReport.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "Animation/WidgetAnimation.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "WidgetBlueprint.h"
#include "DesignWidgetBlueprintUtils.h"

namespace DesignImportPipeline
{
	namespace
	{
		void CollectSubtreeWidgets(UWidget* SubtreeRoot, TFunctionRef<void(UWidget*)> Visitor)
		{
			Visitor(SubtreeRoot);
			UWidgetTree::ForWidgetAndChildren(SubtreeRoot, Visitor);
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
			for (const UWidgetAnimation* Animation : WBP->Animations)
			{
				if (!Animation || UMGPrototypeAnimationBuilder::IsGeneratedNavigationAnimation(Animation)) continue;
				for (const FWidgetAnimationBinding& Binding : Animation->GetBindings())
				{
					// UE resolves both widget and panel-slot animation targets through WidgetName.
					// AnimationGuid identifies the movie-scene binding, not a widget-tree object.
					if (!Binding.bIsRootWidget && SubtreeNames.Contains(Binding.WidgetName.ToString())) return true;
				}
			}
			return false;
		}

		/**
		 * Detect Blueprint variable references before deleting widgets.
		 * Use the engine's IsVariableUsed query, matching the DeleteWidgets deletion path.
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

	/** Delete an owned subtree, remove its names from ownership, and return those names for diagnostics. */
	TSet<FString> DeleteD2UOwnedSubtreeSilently(
		UWidgetBlueprint* WBP, UWidget* SubtreeRoot, TSet<FString>& InOutUpdatedD2UOwned)
	{
		TSet<FString> RemovedNames;
		CollectSubtreeWidgets(SubtreeRoot, [&RemovedNames](UWidget* RemovedWidget)
		{
			RemovedNames.Add(RemovedWidget->GetFName().ToString());
		});
		DesignWidgetBlueprintUtils::EnsureWidgetVariableGuids(WBP);
		DesignWidgetBlueprintUtils::DeleteSubtreeSilently(WBP, SubtreeRoot);
		for (const FString& RemovedName : RemovedNames)
		{
			InOutUpdatedD2UOwned.Remove(RemovedName);
		}
		return RemovedNames;
	}

	/**
	 * Keep a subtree that contains live manifest instances until their move/refresh plans run.
	 * ExcludeWidgetName omits the root being replaced or removed.
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
		Result.Commit.AssetPath = Session.Request.TargetAssetPath;
		if (Session.ImageResult) Result.ImageResult = *Session.ImageResult;
		Result.bHasDegradation = Result.ImageResult.HasFailures();
		Result.Diagnostics = MoveTemp(PriorDiagnostics);
		// Include ini validation diagnostics in the early-exit report so a configuration error remains visible
		// when it coincides with an asset-level conflict.
		Result.ComponentMappingDiagnostics = Session.GenerationSettings.ComponentMapping.ValidationDiagnostics;
		const FString TargetPackage = FPackageName::ObjectPathToPackageName(Session.Request.TargetAssetPath);
		AddReimportDiagnostic(
			Result.Diagnostics,
			ReimportDiagnosticKinds::FullConflict,
			FString::Printf(
				TEXT("Target %s preserved untouched: %s"),
				*TargetPackage,
				*Reason));
		Result.bFullConflict = true;
		Result.Commit.Status = EDesign2UMGCommitStatus::Conflict;
		Result.bSucceeded = false;

		if (!Session.Request.bGenerateResolutionCopy)
		{
			if (Session.Document) DesignImportDiagnosticReport::WriteReimport(
				*Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics, &Result.Commit, &Result.ImageResult);
			return Result;
		}

		const FString PackageDir = FPaths::GetPath(TargetPackage);
		const FString CopyName = FPaths::GetBaseFilename(TargetPackage) + TEXT("_Resolution");
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		if (Design2UMGAssetRegistryAccess::GetAssetByObjectPath(
			ARM.Get(), FSoftObjectPath(PackageDir / CopyName + TEXT(".") + CopyName)).IsValid())
		{
			AddReimportDiagnostic(
				Result.Diagnostics,
				ReimportDiagnosticKinds::ResolutionCopy,
				FString::Printf(
					TEXT("Resolution copy skipped: %s already exists"),
					*(PackageDir / CopyName)));
			if (Session.Document) DesignImportDiagnosticReport::WriteReimport(
				*Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics, &Result.Commit, &Result.ImageResult);
			return Result;
		}

		Session.Data.WidgetName = CopyName;
		FDesign2UMGGenerationOutput GenerationOutput;
		DesignImportGeneration::GenerateWidgetBlueprint(
			Session.Data, PackageDir, &Session.GenerationSettings, &GenerationOutput);
		Result.ResolutionCopyCommit = GenerationOutput.Commit;
		if (Result.ResolutionCopyCommit->Status == EDesign2UMGCommitStatus::Succeeded && Result.bHasDegradation)
			Result.ResolutionCopyCommit->Status = EDesign2UMGCommitStatus::SucceededWithDegradation;
		if (GenerationOutput.Commit.Asset == EDesign2UMGFileState::Saved)
		{
			Result.ResolutionCopyPath = GenerationOutput.Commit.AssetPath;
			AddReimportDiagnostic(
				Result.Diagnostics,
				ReimportDiagnosticKinds::ResolutionCopy,
				FString::Printf(
					TEXT("Resolution copy saved at %s (original untouched, metadata %s); reason: %s"),
					*Result.ResolutionCopyPath,
					GenerationOutput.Commit.Manifest == EDesign2UMGFileState::Saved ? TEXT("saved") : TEXT("not updated"),
					*Reason));
		}
		else
		{
			AddReimportDiagnostic(
				Result.Diagnostics,
				ReimportDiagnosticKinds::ResolutionCopy,
				FString::Printf(TEXT("Resolution copy generation failed for %s"), *(PackageDir / CopyName)));
		}
		// The copy-generation chain's mapping diagnostics (ini validation, hits, the copy's own sidecar write failure) land in the report.
		Result.ComponentMappingDiagnostics.Append(GenerationOutput.MappingDiagnostics);
		if (Session.Document) DesignImportDiagnosticReport::WriteReimport(
			*Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics, &Result.Commit, &Result.ImageResult);
		return Result;
	}
}
