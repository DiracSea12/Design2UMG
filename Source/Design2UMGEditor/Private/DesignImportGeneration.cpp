// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportGeneration.h"
#include "Design2UMGComponentBlueprints.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotifications.h"
#include "DesignBlueprintRenderPreparation.h"
#include "DesignComponentMapping.h"
#include "DesignImportDefaults.h"
#include "DesignImageResourceLimits.h"
#include "FigmaComponentDependencies.h"
#include "UMGWidgetRenderer.h"
#include "UMGSchemaSourceExtensions.h"
#include "Design2UMGSettings.h"

#include "Blueprint/UserWidget.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "DesignImportGeneration"

namespace
{
	FString ResolveImportOutputPath(const FString& OutputPath)
	{
		return OutputPath.IsEmpty() ? FString(DesignImportDefaults::PenOutputPath) : OutputPath;
	}

	FIntPoint ResolveOutputRenderSize(UWidgetBlueprint* WidgetBlueprint)
	{
		const TOptional<FVector2D> DesignTimeSize = FUMGWidgetRenderer::GetBlueprintDesignTimeSize(WidgetBlueprint);
		if (DesignTimeSize.IsSet())
		{
			return FIntPoint(
				FMath::CeilToInt(DesignTimeSize->X),
				FMath::CeilToInt(DesignTimeSize->Y));
		}

		UE_LOG(LogDesign2UMG, Error,
			TEXT("DesignImportGeneration: missing Blueprint DesignTimeSize; source design size is required"));
		return FIntPoint::ZeroValue;
	}

	FIntPoint ResolveSourceWidgetTreeRenderSize(const FUMGWidgetData& Data)
	{
		return FIntPoint(
			FMath::Max(0, FMath::CeilToInt(Data.RootWidget.Bounds.W)),
			FMath::Max(0, FMath::CeilToInt(Data.RootWidget.Bounds.H)));
	}

	bool SavePngBytesToFile(const TArray<uint8>& PngData, const FString& OutputPngFilePath)
	{
		if (OutputPngFilePath.IsEmpty())
		{
			return false;
		}

		const FString OutputDir = FPaths::GetPath(OutputPngFilePath);
		if (!OutputDir.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*OutputDir, true);
		}

		return FFileHelper::SaveArrayToFile(PngData, *OutputPngFilePath);
	}

	bool RenderWidgetTreeToPngFile(
		UWidgetBlueprint* WidgetBlueprint,
		const FString& OutputPngFilePath,
		FIntPoint* OutRenderedSize = nullptr)
	{
		if (!WidgetBlueprint)
		{
			return false;
		}

		TArray<uint8> PngData;
		FIntPoint ActualSize = ResolveOutputRenderSize(WidgetBlueprint);
		if (!FUMGWidgetRenderer::RenderWidgetTreeToPng(WidgetBlueprint, PngData, ActualSize))
		{
			return false;
		}

		if (!SavePngBytesToFile(PngData, OutputPngFilePath))
		{
			return false;
		}

		if (OutRenderedSize)
		{
			*OutRenderedSize = ActualSize;
		}

		UE_LOG(LogDesign2UMG, Log,
			TEXT("DesignImportGeneration: WidgetTree PNG saved: %s (%dx%d)"),
			*OutputPngFilePath, ActualSize.X, ActualSize.Y);
		return true;
	}

	int32 CountWidgets(const FUMGWidgetNode& Node)
	{
		int32 Count = 1;
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			Count += CountWidgets(Child);
		}
		return Count;
	}

	bool ShouldUseInteractiveEditorWork(const FFeedbackContext* ProgressFeedback = nullptr)
	{
		return GIsEditor && (ProgressFeedback != nullptr || !FApp::IsUnattended());
	}

	// Interactive generation scope: guards the slow task against reentry.
	// Generation deliberately does NOT open an editor transaction: the import
	// saves the generated asset to disk (like a Content Browser asset import,
	// which is not undoable either). A live transaction would let Ctrl+Z roll
	// the in-memory state back while the saved .uasset keeps the import, leaving
	// memory and disk inconsistent.
	struct FInteractiveGenerationScope
	{
		explicit FInteractiveGenerationScope(const FFeedbackContext* ProgressFeedback)
			: bEnabled(ShouldUseInteractiveEditorWork(ProgressFeedback))
		{
			if (!bEnabled)
			{
				return;
			}

			++Depth;
		}

		~FInteractiveGenerationScope()
		{
			if (bEnabled && Depth > 0)
			{
				--Depth;
			}
		}

		bool IsOwner() const
		{
			return bEnabled && Depth == 1;
		}

	private:
		static int32 Depth;
		bool bEnabled = false;
	};

	int32 FInteractiveGenerationScope::Depth = 0;

	bool BeginInteractiveGenerationProgress(
		TUniquePtr<FScopedSlowTask>& OutSlowTask,
		const TCHAR* Context,
		FFeedbackContext* ProgressFeedback)
	{
		OutSlowTask.Reset();
		if (!ShouldUseInteractiveEditorWork(ProgressFeedback))
		{
			return true;
		}

		OutSlowTask = MakeUnique<FScopedSlowTask>(
			5.0f,
			FText::Format(
				LOCTEXT("GenerateProgress", "Design2UMG: Generating Widget Blueprint ({0})"),
				FText::FromString(Context ? FString(Context) : FString(TEXT("Import")))),
			true,
			ProgressFeedback ? *ProgressFeedback : *GWarn);
		OutSlowTask->MakeDialog(true);
		OutSlowTask->EnterProgressFrame(1.0f, LOCTEXT("GenerateProgressPrepare", "Preparing import"));
		return !OutSlowTask->ShouldCancel();
	}

	bool AdvanceInteractiveGenerationProgress(
		FScopedSlowTask* SlowTask,
		const float Work,
		const FText& StepText,
		const TCHAR* CancelContext)
	{
		if (!SlowTask)
		{
			return true;
		}

		if (SlowTask->ShouldCancel())
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("DesignImportGeneration: %s cancelled before step"),
				CancelContext ? CancelContext : TEXT("generation"));
			FDesign2UMGNotification::Warning(LOCTEXT("GenerationCancelled", "Design2UMG: Generation cancelled"));
			return false;
		}

		SlowTask->EnterProgressFrame(Work, StepText);
		if (SlowTask->ShouldCancel())
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("DesignImportGeneration: %s cancelled after step"),
				CancelContext ? CancelContext : TEXT("generation"));
			FDesign2UMGNotification::Warning(LOCTEXT("GenerationCancelled", "Design2UMG: Generation cancelled"));
			return false;
		}

		return true;
	}

	void NoteInteractiveCancellationAfterMutation(
		FScopedSlowTask* SlowTask,
		const TCHAR* CancelContext,
		bool& bCancelRequestedAfterMutation)
	{
		if (!SlowTask || !SlowTask->ShouldCancel() || bCancelRequestedAfterMutation)
		{
			return;
		}

		bCancelRequestedAfterMutation = true;
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("DesignImportGeneration: %s cancellation requested after asset mutation; completing generation to avoid partial state"),
			CancelContext ? CancelContext : TEXT("generation"));
	}

	void AdvanceInteractiveGenerationProgressAfterMutation(
		FScopedSlowTask* SlowTask,
		const float Work,
		const FText& StepText,
		const TCHAR* CancelContext,
		bool& bCancelRequestedAfterMutation)
	{
		NoteInteractiveCancellationAfterMutation(SlowTask, CancelContext, bCancelRequestedAfterMutation);
		if (!SlowTask)
		{
			return;
		}
		SlowTask->EnterProgressFrame(Work, StepText);
		NoteInteractiveCancellationAfterMutation(SlowTask, CancelContext, bCancelRequestedAfterMutation);
	}

	void FinishInteractiveGenerationProgress(TUniquePtr<FScopedSlowTask>& SlowTask)
	{
		SlowTask.Reset();
	}

	// Progress handle handed to a generation core: advances the slow task and tracks
	// cancellation requests raised after the asset has been mutated.
	struct FGenerationProgress
	{
		FScopedSlowTask* SlowTask = nullptr;
		const TCHAR* CancelContext = nullptr;
		bool bCancelRequestedAfterMutation = false;

		bool Advance(const float Work, const FText& StepText) const
		{
			return AdvanceInteractiveGenerationProgress(SlowTask, Work, StepText, CancelContext);
		}

		void AdvanceAfterMutation(const float Work, const FText& StepText)
		{
			AdvanceInteractiveGenerationProgressAfterMutation(
				SlowTask, Work, StepText, CancelContext, bCancelRequestedAfterMutation);
		}
	};

	// Shared scaffolding for the public generation entry points: interactive
	// generation scope, slow-task begin/prepare step and teardown. The
	// entry-point-specific build/render/save sequence lives in Core; every Core
	// failure path leaves teardown to this runner.
	template <typename TResult, typename CoreFn>
	TResult RunInteractiveGeneration(
		const TCHAR* Label,
		const TResult FailureResult,
		bool& bOutCancelAfterMutation,
		FFeedbackContext* ProgressFeedback,
		CoreFn&& Core)
	{
		bOutCancelAfterMutation = false;

		const FString CancelContext = FString(Label) + TEXT(" generation");
		FInteractiveGenerationScope GenerationScope(ProgressFeedback);
		TUniquePtr<FScopedSlowTask> SlowTask;
		if (GenerationScope.IsOwner()
			&& !BeginInteractiveGenerationProgress(SlowTask, Label, ProgressFeedback))
		{
			FinishInteractiveGenerationProgress(SlowTask);
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("DesignImportGeneration: %s generation cancelled before asset mutation"), Label);
			return FailureResult;
		}

		if (!AdvanceInteractiveGenerationProgress(
			SlowTask.Get(),
			0.0f,
			LOCTEXT("GenerateProgressPrepare", "Preparing import"),
			*CancelContext))
		{
			FinishInteractiveGenerationProgress(SlowTask);
			return FailureResult;
		}

		FGenerationProgress Progress;
		Progress.SlowTask = SlowTask.Get();
		Progress.CancelContext = *CancelContext;
		const TResult Result = Core(Progress);
		FinishInteractiveGenerationProgress(SlowTask);
		bOutCancelAfterMutation = Progress.bCancelRequestedAfterMutation;
		return Result;
	}

	void NotifyImportSummary(const FUMGWidgetData& Data, const double ElapsedSeconds)
	{
		if (Data.Warnings.Num() > 0)
		{
			FDesign2UMGNotification::Warning(FText::Format(
				LOCTEXT("ImportSummaryWarnings", "Design2UMG: {0} warnings"),
				FText::AsNumber(Data.Warnings.Num())));
			return;
		}

		FDesign2UMGNotification::Success(FText::Format(
			LOCTEXT("ImportSummaryOk", "Design2UMG: {0} widgets ({1}s)"),
			FText::AsNumber(CountWidgets(Data.RootWidget)),
			FText::FromString(FString::Printf(TEXT("%.1f"), ElapsedSeconds))));
	}

	void NotifyGenerationCompletedAfterCancel()
	{
		FDesign2UMGNotification::Warning(LOCTEXT(
			"GenerationCancelCompleted",
			"Design2UMG: Cancel requested after asset creation; saved generated output to avoid partial state"));
	}

	// Build-stage output handed to a pipeline stage: the unsaved blueprint plus
	// its generation output (manifest payload + mapping diagnostics) and the source
	// prototype navigation data (stages hand it to SaveWidgetBlueprint for graph wiring).
	struct FBuiltBlueprint
	{
		UWidgetBlueprint* WidgetBlueprint = nullptr;
		FDesign2UMGGenerationOutput GenerationOutput;
		FUMGPrototypeNavigationData PrototypeNavigation;
	};

	// Differentiated stage of GenerateBlueprintAndPng: render the PNG from the
	// transient, unsaved blueprint first, then save the blueprint only after a
	// successful render (or as a rescue save on render failure so the broken
	// state can be inspected).
	bool BuildTransientPreviewThenSave(
		const FUMGWidgetData& Data,
		FBuiltBlueprint&& Built,
		FGenerationProgress& Progress,
		FString* OutGeneratedAssetPath,
		FString* OutRenderPngPath,
		FDesign2UMGGenerationOutput* OutGenerationOutput)
	{
		UWidgetBlueprint* WBP = Built.WidgetBlueprint;
		FDesign2UMGGenerationOutput GenerationOutput = MoveTemp(Built.GenerationOutput);

		FIntPoint Size = ResolveSourceWidgetTreeRenderSize(Data);
		const FString PngName = WBP->GetName();
		const FString OutPng = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (PngName + TEXT(".png"));
		IFileManager::Get().Delete(*OutPng, false, true);
		Progress.AdvanceAfterMutation(0.5f, LOCTEXT("GenerateProgressRender", "Rendering generated asset"));
		if (!RenderWidgetTreeToPngFile(WBP, OutPng, &Size))
		{
			IFileManager::Get().Delete(*OutPng, false, true);
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportGeneration: Widget PNG render failed"));
			// Save the Blueprint even when rendering fails so the issue can be inspected.
			FUMGGenerator::SaveWidgetBlueprint(WBP, &GenerationOutput, &Built.PrototypeNavigation);
			if (GenerationOutput.Commit.Asset == EDesign2UMGFileState::Saved && OutGeneratedAssetPath)
			{
				*OutGeneratedAssetPath = WBP->GetPathName();
			}
			// Asset and sidecar are already on disk: GenerationOutput (with all mapping/manifest diagnostics) must
			// still be moved out, or the import report and the disk artifacts disagree (report missing sections while the asset exists).
			if (OutGenerationOutput)
			{
				*OutGenerationOutput = MoveTemp(GenerationOutput);
			}
			return false;
		}

		// Save the Blueprint after successful rendering.
		Progress.AdvanceAfterMutation(0.5f, LOCTEXT("GenerateProgressSave", "Saving generated assets"));
		if (!FUMGGenerator::SaveWidgetBlueprint(WBP, &GenerationOutput, &Built.PrototypeNavigation).IsSuccess())
		{
			if (OutGeneratedAssetPath && GenerationOutput.Commit.Asset == EDesign2UMGFileState::Saved)
				*OutGeneratedAssetPath = GenerationOutput.Commit.AssetPath;
			if (OutGenerationOutput)
			{
				*OutGenerationOutput = MoveTemp(GenerationOutput);
			}
			IFileManager::Get().Delete(*OutPng, false, true);
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportGeneration: Widget Blueprint compile or save failed after PNG render"));
			return false;
		}
		if (OutGenerationOutput)
		{
			*OutGenerationOutput = MoveTemp(GenerationOutput);
		}
		Progress.AdvanceAfterMutation(1.0f, LOCTEXT("GenerateProgressDone", "Finishing import"));
		if (OutGeneratedAssetPath)
		{
			*OutGeneratedAssetPath = WBP->GetPathName();
		}
		if (OutRenderPngPath)
		{
			*OutRenderPngPath = OutPng;
		}
		UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportGeneration: Rendered PNG saved: %s (%dx%d)"), *OutPng, Size.X, Size.Y);
		return true;
	}

	// Differentiated stage of GenerateBlueprintAndCompiledPng: save (compile)
	// the blueprint first, then reload it from disk and render the compiled
	// asset to the requested PNG path.
	bool SaveThenRenderCompiled(
		FBuiltBlueprint&& Built,
		FGenerationProgress& Progress,
		const FString& OutputPngFilePath,
		FString* OutGeneratedAssetPath,
		FIntPoint* OutRenderedSize,
		FDesign2UMGGenerationOutput* OutGenerationOutput,
		FString* OutError,
		const FBlueprintRenderPreparationOptions* RenderPreparation,
		TStrongObjectPtr<UUserWidget>* OutRenderedWidget,
		TSharedPtr<SWidget>* OutRenderedSlateWidget)
	{
		UWidgetBlueprint* WidgetBlueprint = Built.WidgetBlueprint;
		FDesign2UMGGenerationOutput GenerationOutput = MoveTemp(Built.GenerationOutput);

		Progress.AdvanceAfterMutation(0.5f, LOCTEXT("GenerateProgressSave", "Saving generated assets"));
		if (!FUMGGenerator::SaveWidgetBlueprint(WidgetBlueprint, &GenerationOutput, &Built.PrototypeNavigation).IsSuccess())
		{
			if (OutGeneratedAssetPath && GenerationOutput.Commit.Asset == EDesign2UMGFileState::Saved)
				*OutGeneratedAssetPath = GenerationOutput.Commit.AssetPath;
			// Build-time diagnostics (mapping validation/hits) move out with GenerationOutput; they are not lost on the failure path.
			if (OutGenerationOutput)
			{
				*OutGenerationOutput = MoveTemp(GenerationOutput);
			}
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportGeneration: Widget Blueprint compile or save failed before compiled PNG render"));
			return false;
		}
		if (OutGenerationOutput)
		{
			*OutGenerationOutput = MoveTemp(GenerationOutput);
		}
		if (!OutputPngFilePath.IsEmpty())
		{
			IFileManager::Get().Delete(*OutputPngFilePath, false, true);
		}

		if (OutGeneratedAssetPath)
		{
			*OutGeneratedAssetPath = WidgetBlueprint->GetPathName();
		}

		if (!FApp::IsUnattended())
		{
			Progress.AdvanceAfterMutation(0.5f, LOCTEXT("GenerateProgressRender", "Rendering generated asset"));
			const FString TreeRenderPath = FPaths::Combine(
				FPaths::GetPath(OutputPngFilePath),
				FPaths::GetBaseFilename(OutputPngFilePath) + TEXT(".widgettree.png"));
			RenderWidgetTreeToPngFile(WidgetBlueprint, TreeRenderPath);
		}

		FString PreparationError;
		WidgetBlueprint = DesignBlueprintRenderPreparation::ReloadWidgetBlueprintForRender(
			WidgetBlueprint,
			RenderPreparation,
			&PreparationError);
		if (!WidgetBlueprint)
		{
			if (!PreparationError.IsEmpty())
			{
				UE_LOG(LogDesign2UMG, Error,
					TEXT("DesignImportGeneration: aborting compiled PNG render because preparation failed: %s"),
					*PreparationError);
				if (OutError)
				{
					*OutError = PreparationError;
				}
			}
			return false;
		}

		Progress.AdvanceAfterMutation(1.0f, LOCTEXT("GenerateProgressRender", "Rendering generated asset"));
		FString RenderError;
		const bool bCompiledRendered = DesignImportGeneration::RenderCompiledBlueprintToPngFile(
			WidgetBlueprint,
			OutputPngFilePath,
			OutRenderedSize,
			&RenderError,
			TOptional<FVector2D>(),
			TOptional<FVector2D>(),
			OutRenderedWidget,
			OutRenderedSlateWidget);
		if (!bCompiledRendered)
		{
			if (OutError && !RenderError.IsEmpty())
			{
				*OutError = RenderError;
			}
			IFileManager::Get().Delete(*OutputPngFilePath, false, true);
		}
		else
		{
			Progress.AdvanceAfterMutation(0.5f, LOCTEXT("GenerateProgressDone", "Finishing import"));
		}
		return bCompiledRendered;
	}

	// Shared blueprint/PNG build and cancellation lifecycle.
	// The supplied stage owns save/render ordering; bLogBuildFailure selects error reporting.
	bool RunBlueprintAndPngPipeline(
		const TCHAR* Label,
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings,
		const bool bLogBuildFailure,
		const bool bPersistIconFontMappingChanges,
		FDesign2UMGGenerationOutput* OutGenerationOutput,
		TFunctionRef<bool(FBuiltBlueprint&& Built, FGenerationProgress& Progress)> Stage)
	{
		bool bCancelAfterMutation = false;
		const bool bOk = RunInteractiveGeneration(
			Label,
			false,
			bCancelAfterMutation,
			nullptr,
			[&](FGenerationProgress& Progress) -> bool
			{
				if (!Progress.Advance(1.0f, LOCTEXT("GenerateProgressBuild", "Building Widget Blueprint")))
				{
					return false;
				}

				const FString FinalPath = ResolveImportOutputPath(OutputPath);
				DesignImportGeneration::ApplyPendingIconFontMappingChanges(Data, bPersistIconFontMappingChanges);
				FBuiltBlueprint Built;
				Built.WidgetBlueprint = DesignImportGeneration::BuildWidgetBlueprint(
					Data, FinalPath, GenerationSettings, &Built.GenerationOutput, &Built.PrototypeNavigation);
				if (!Built.WidgetBlueprint)
				{
					if (OutGenerationOutput)
					{
						*OutGenerationOutput = MoveTemp(Built.GenerationOutput);
					}
					if (bLogBuildFailure)
					{
						UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportGeneration: Widget Blueprint build failed"));
					}
					return false;
				}

				return Stage(MoveTemp(Built), Progress);
			});

		if (bOk && bCancelAfterMutation)
		{
			NotifyGenerationCompletedAfterCancel();
		}
		return bOk;
	}
}

namespace DesignImportGeneration
{
	void ApplyPendingIconFontMappingChanges(
		const FUMGWidgetData& Data,
		const bool bPersistChanges)
	{
		if (!bPersistChanges || !Data.IconFontMappingChanges.HasChanges())
		{
			return;
		}

		UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
		if (!Settings)
		{
			return;
		}

		bool bModified = false;
		for (const FString& Family : Data.IconFontMappingChanges.Removals)
		{
			if (Family.IsEmpty())
			{
				continue;
			}

			if (Settings->IconFontAssetMap.Remove(Family) > 0)
			{
				bModified = true;
				UE_LOG(LogDesign2UMG, Display, TEXT("Removed stale icon font mapping for '%s'"), *Family);
			}
		}

		for (const TPair<FString, FString>& Pair : Data.IconFontMappingChanges.Upserts)
		{
			if (Pair.Key.IsEmpty() || Pair.Value.IsEmpty())
			{
				continue;
			}

			const TSoftObjectPtr<UFont>* Existing = Settings->IconFontAssetMap.Find(Pair.Key);
			if (Existing && Existing->ToString() == Pair.Value)
			{
				continue;
			}

			Settings->IconFontAssetMap.Add(Pair.Key, TSoftObjectPtr<UFont>(FSoftObjectPath(Pair.Value)));
			bModified = true;
			UE_LOG(LogDesign2UMG, Display, TEXT("Persisted icon font mapping '%s' -> '%s'"), *Pair.Key, *Pair.Value);
		}

		if (bModified)
		{
			Settings->SaveConfig();
			// The import mutates project settings behind the user's back; make the
			// persistence visible so the change is never silent.
			FDesign2UMGNotification::Info(LOCTEXT(
				"IconFontMappingSaved",
				"Design2UMG: Icon font mapping updated and saved to project settings"));
		}
	}

	UWidgetBlueprint* BuildWidgetBlueprint(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings,
		FDesign2UMGGenerationOutput* OutGenerationOutput,
		FUMGPrototypeNavigationData* OutNavigationData)
	{
		DesignImageResources::FImageOperationBudget Budget(DesignImageResources::GetGeneratedTextureLimits());

		if (WidgetData.HasSourceDocumentData()
			&& WidgetData.SourceDocument().Provider == DesignComponentMapping::ProviderFigma
			&& WidgetData.SourceDocument().ComponentDependenciesJson.IsEmpty()
			&& (!GenerationSettings || !GenerationSettings->ComponentBlueprints))
		{
			const FDesign2UMGGenerationSettingsSnapshot Effective = GenerationSettings ? *GenerationSettings
				: FDesign2UMGGenerationSettingsSnapshot::FromSettings(*UDesign2UMGSettings::Get());
			TArray<FString> Unresolved;
			TFunction<void(const FUMGWidgetNode&)> Collect = [&](const FUMGWidgetNode& Node)
			{
				if (Node.HasSourceData() && Node.Source().SourceNodeType == TEXT("INSTANCE"))
				{
					DesignComponentMapping::FDesignComponentIdentity Identity;
					bool AtSet = false;
					if (DesignComponentMapping::TryResolveFigmaComponentIdentity(Node.Source(),
						WidgetData.SourceDocument().DocumentScope, Identity)
						&& Effective.ComponentMapping.Table.Match(Identity, AtSet)) return;
					Unresolved.AddUnique(Identity.ComponentKey.IsEmpty() ? Node.Id : Identity.ComponentKey);
				}
				for (const auto& Child : Node.Children) Collect(Child);
			};
			Collect(WidgetData.RootWidget);
			if (!Unresolved.IsEmpty())
			{
				const FString Error = TEXT("Missing component dependency data for: ") + FString::Join(Unresolved, TEXT(", "));
				UE_LOG(LogDesign2UMG, Error, TEXT("Component import failed: %s"), *Error);
				if (OutGenerationOutput) OutGenerationOutput->MappingDiagnostics.Add({TEXT("component_generation_failed"), Error});
				return nullptr;
			}
		}
		if (WidgetData.HasSourceDocumentData()
			&& !WidgetData.SourceDocument().ComponentDependenciesJson.IsEmpty()
			&& (!GenerationSettings || !GenerationSettings->ComponentBlueprints))
		{
			FigmaComponentDependencies::FCatalog Catalog;
			FString BundleError;
			const bool bParsed = FigmaComponentDependencies::Parse(
				WidgetData.SourceDocument().ComponentDependenciesJson, Catalog, BundleError);
			TFunction<bool(const FUMGWidgetNode&)> HasInstance = [&](const FUMGWidgetNode& Node)
			{
				if (Node.HasSourceData() && Node.Source().SourceNodeType == TEXT("INSTANCE")) return true;
				for (const auto& Child : Node.Children) if (HasInstance(Child)) return true;
				return false;
			};
			// Empty exporter metadata is not an instruction to replace an ordinary page.
			// Malformed bundles and unresolved instances still take the diagnostic path.
			// Definitions outside the selected tree do not change ordinary import ownership.
			if (!bParsed || HasInstance(WidgetData.RootWidget))
			{
				FDesign2UMGGenerationOutput LocalGenerationOutput;
				return FDesign2UMGComponentBlueprintContext::Build(
					WidgetData, PackagePath, GenerationSettings,
					OutGenerationOutput ? OutGenerationOutput : &LocalGenerationOutput, OutNavigationData, nullptr, &Budget);
			}
		}
		return FUMGGenerator::BuildWidgetBlueprint(
			WidgetData, PackagePath, GenerationSettings, OutGenerationOutput, OutNavigationData, nullptr, &Budget);
	}

	UWidgetBlueprint* GenerateWidgetBlueprint(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings,
		FDesign2UMGGenerationOutput* OutGenerationOutput)
	{
		FDesign2UMGGenerationOutput GenerationOutput;
		FUMGPrototypeNavigationData NavigationData;
		UWidgetBlueprint* Blueprint = BuildWidgetBlueprint(
			WidgetData, PackagePath, GenerationSettings, &GenerationOutput, &NavigationData);
		const FString BuiltPath = GetPathNameSafe(Blueprint);
		if (Blueprint && !FUMGGenerator::SaveWidgetBlueprint(Blueprint, &GenerationOutput, &NavigationData).IsSuccess())
		{
			GenerationOutput.MappingDiagnostics.Add({TEXT("generation_save_failed"),
				FString::Printf(TEXT("Asset batch save failed for '%s'; inspect dependency and recovery diagnostics"), *BuiltPath)});
			Blueprint = nullptr;
		}
		if (OutGenerationOutput)
		{
			*OutGenerationOutput = MoveTemp(GenerationOutput);
		}
		return Blueprint;
	}

	UWidgetBlueprint* GenerateBlueprint(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings,
		FDesign2UMGGenerationOutput* OutGenerationOutput,
		FFeedbackContext* ProgressFeedback,
		const bool bPersistIconFontMappingChanges)
	{
		double StartTime = 0.0;
		bool bCancelAfterMutation = false;
		UWidgetBlueprint* const WBP = RunInteractiveGeneration<UWidgetBlueprint*>(
			TEXT("Blueprint"),
			nullptr,
			bCancelAfterMutation,
			ProgressFeedback,
			[&](FGenerationProgress& Progress) -> UWidgetBlueprint*
			{
				StartTime = FPlatformTime::Seconds();
				const FString FinalPath = ResolveImportOutputPath(OutputPath);

				if (!Progress.Advance(1.0f, LOCTEXT("GenerateProgressBuild", "Building Widget Blueprint")))
				{
					return nullptr;
				}
				ApplyPendingIconFontMappingChanges(Data, bPersistIconFontMappingChanges);
				FDesign2UMGGenerationOutput GenerationOutput;
				FUMGPrototypeNavigationData NavigationData;
				UWidgetBlueprint* BuiltWBP = DesignImportGeneration::BuildWidgetBlueprint(
					Data, FinalPath, GenerationSettings, &GenerationOutput, &NavigationData);
				if (!BuiltWBP)
				{
					if (OutGenerationOutput)
					{
						*OutGenerationOutput = MoveTemp(GenerationOutput);
					}
					UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportGeneration: Widget Blueprint build failed"));
					FDesign2UMGNotification::Error(LOCTEXT("GenFailed", "Design2UMG: Widget Blueprint generation failed"));
					return nullptr;
				}

				Progress.AdvanceAfterMutation(1.0f, LOCTEXT("GenerateProgressSave", "Saving generated assets"));
				if (!FUMGGenerator::SaveWidgetBlueprint(BuiltWBP, &GenerationOutput, &NavigationData).IsSuccess())
				{
					if (OutGenerationOutput)
					{
						*OutGenerationOutput = MoveTemp(GenerationOutput);
					}
					UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportGeneration: Widget Blueprint compile or save failed"));
					FDesign2UMGNotification::Error(LOCTEXT("SaveFailed", "Design2UMG: Widget Blueprint compile or save failed"));
					return nullptr;
				}
				if (OutGenerationOutput)
				{
					*OutGenerationOutput = MoveTemp(GenerationOutput);
				}
				UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportGeneration: Generated %s"), *BuiltWBP->GetPathName());

				Progress.AdvanceAfterMutation(1.0f, LOCTEXT("GenerateProgressDone", "Finishing import"));
				return BuiltWBP;
			});

		if (!WBP)
		{
			return nullptr;
		}
		if (bCancelAfterMutation)
		{
			NotifyGenerationCompletedAfterCancel();
		}
		else
		{
			NotifyImportSummary(Data, FPlatformTime::Seconds() - StartTime);
		}
		return WBP;
	}

	FArtifactGenerationResult GenerateBlueprintAndPng(
		const FUMGWidgetData& Data,
		const FArtifactGenerationRequest& Request,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings)
	{
		FArtifactGenerationResult Result;
		FDesign2UMGGenerationOutput GenerationOutput;
		Result.bSucceeded = RunBlueprintAndPngPipeline(
			TEXT("Blueprint and PNG"), Data, Request.OutputPath, GenerationSettings, true,
			Request.Generation.bPersistIconFontMappingChanges, &GenerationOutput,
			[&](FBuiltBlueprint&& Built, FGenerationProgress& Progress) -> bool
			{
				return BuildTransientPreviewThenSave(Data, MoveTemp(Built), Progress,
					&Result.GeneratedAssetPath, &Result.RenderPngPath, &GenerationOutput);
			});
		Result.Commit = GenerationOutput.Commit;
		Result.ComponentMappingDiagnostics = MoveTemp(GenerationOutput.MappingDiagnostics);
		Result.Warnings = Data.Warnings;
		return Result;
	}

	bool RenderCompiledBlueprintToPngFile(
		UWidgetBlueprint* WidgetBlueprint,
		const FString& OutputPngFilePath,
		FIntPoint* OutRenderedSize,
		FString* OutError,
		const TOptional<FVector2D>& RenderSizeOverride,
		const TOptional<FVector2D>& RootExportOffset,
		TStrongObjectPtr<UUserWidget>* OutRenderedWidget,
		TSharedPtr<SWidget>* OutRenderedSlateWidget)
	{
		if (!WidgetBlueprint)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportGeneration: RenderCompiledBlueprintToPngFile invalid WidgetBlueprint"));
			return false;
		}

		// Canvas calibration: an explicit override pins the render surface to the
		// authoritative reference size (compare-scope only); layout stays untouched.
		FIntPoint ActualSize = FIntPoint::ZeroValue;
		if (RenderSizeOverride.IsSet())
		{
			const FVector2D OverrideSize = RenderSizeOverride.GetValue();
			ActualSize = FIntPoint(FMath::RoundToInt(OverrideSize.X), FMath::RoundToInt(OverrideSize.Y));
			if (ActualSize.X <= 0 || ActualSize.Y <= 0)
			{
				if (OutError)
				{
					*OutError = FString::Printf(
						TEXT("Render size override %dx%d is not positive"), ActualSize.X, ActualSize.Y);
				}
				return false;
			}
		}
		else
		{
			ActualSize = ResolveOutputRenderSize(WidgetBlueprint);
		}

		TArray<uint8> PngData;
		if (!FUMGWidgetRenderer::RenderWidgetToPng(
			WidgetBlueprint,
			PngData,
			ActualSize,
			OutError,
			RenderSizeOverride,
			RootExportOffset,
			OutRenderedWidget,
			OutRenderedSlateWidget))
		{
			UE_LOG(LogDesign2UMG, Error,
				TEXT("DesignImportGeneration: RenderCompiledBlueprintToPngFile failed for %s"),
				*WidgetBlueprint->GetPathName());
			return false;
		}

		if (!SavePngBytesToFile(PngData, OutputPngFilePath))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportGeneration: Failed to save compiled PNG: %s"), *OutputPngFilePath);
			return false;
		}

		if (OutRenderedSize)
		{
			*OutRenderedSize = ActualSize;
		}

		UE_LOG(LogDesign2UMG, Log,
			TEXT("DesignImportGeneration: Rendered PNG saved: %s (%dx%d, compiled blueprint)"),
			*OutputPngFilePath, ActualSize.X, ActualSize.Y);
		return true;
	}

	FArtifactGenerationResult GenerateBlueprintAndCompiledPng(
		const FUMGWidgetData& Data,
		const FArtifactGenerationRequest& Request,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings)
	{
		FArtifactGenerationResult Result;
		FDesign2UMGGenerationOutput GenerationOutput;
		Result.bSucceeded = RunBlueprintAndPngPipeline(
			TEXT("Compiled PNG"), Data, Request.OutputPath, GenerationSettings, false,
			Request.Generation.bPersistIconFontMappingChanges, &GenerationOutput,
			[&](FBuiltBlueprint&& Built, FGenerationProgress& Progress) -> bool
			{
				return SaveThenRenderCompiled(MoveTemp(Built), Progress, Request.Generation.CompiledPngFilePath,
					&Result.GeneratedAssetPath, &Result.RenderedSize, &GenerationOutput, &Result.ErrorMessage,
					Request.Generation.RenderPreparation.IsSet() ? &Request.Generation.RenderPreparation.GetValue() : nullptr,
					&Result.RenderedWidget, &Result.RenderedSlateWidget);
			});
		Result.Commit = GenerationOutput.Commit;
		Result.ComponentMappingDiagnostics = MoveTemp(GenerationOutput.MappingDiagnostics);
		Result.Warnings = Data.Warnings;
		if (Result.bSucceeded) Result.RenderPngPath = Request.Generation.CompiledPngFilePath;
		return Result;
	}

}

#undef LOCTEXT_NAMESPACE
