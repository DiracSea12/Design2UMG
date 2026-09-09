// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportUtils.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotifications.h"
#include "Automation/Design2UMGAutomationRender.h"
#include "Design2UMGEditorImportOrchestrator.h"
#include "UMGGenerator.h"
#include "UMGWidgetUtils.h"
#include "Design2UMGSettings.h"

#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Misc/ScopedSlowTask.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "DesignImportUtils"

namespace
{
	FString ResolveImportOutputPath(const FString& OutputPath)
	{
		return OutputPath.IsEmpty() ? FString(Design2UMGEditorImportOrchestrator::DefaultPenOutputPath) : OutputPath;
	}

	FIntPoint ResolveOutputRenderSize(UWidgetBlueprint* WidgetBlueprint)
	{
		const TOptional<FVector2D> DesignTimeSize = FUMGWidgetUtils::GetBlueprintDesignTimeSize(WidgetBlueprint);
		if (DesignTimeSize.IsSet())
		{
			return FIntPoint(
				FMath::CeilToInt(DesignTimeSize->X),
				FMath::CeilToInt(DesignTimeSize->Y));
		}

		UE_LOG(LogDesign2UMG, Error,
			TEXT("DesignImportUtils: missing Blueprint DesignTimeSize; source design size is required"));
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
		if (!FUMGWidgetUtils::RenderWidgetTreeToPng(WidgetBlueprint, PngData, ActualSize))
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
			TEXT("DesignImportUtils: WidgetTree PNG saved: %s (%dx%d)"),
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

	bool ShouldUseInteractiveEditorWork()
	{
		return GIsEditor && !FApp::IsUnattended();
	}

	// Interactive generation scope: guards the slow task against reentry.
	// Generation deliberately does NOT open an editor transaction: the import
	// saves the generated asset to disk (like a Content Browser asset import,
	// which is not undoable either). A live transaction would let Ctrl+Z roll
	// the in-memory state back while the saved .uasset keeps the import, leaving
	// memory and disk inconsistent.
	struct FInteractiveGenerationScope
	{
		explicit FInteractiveGenerationScope(const TCHAR* Context)
		{
			if (!ShouldUseInteractiveEditorWork())
			{
				return;
			}

			++Depth;
		}

		~FInteractiveGenerationScope()
		{
			if (ShouldUseInteractiveEditorWork() && Depth > 0)
			{
				--Depth;
			}
		}

		bool IsOwner() const
		{
			return ShouldUseInteractiveEditorWork() && Depth == 1;
		}

	private:
		static int32 Depth;
	};

	int32 FInteractiveGenerationScope::Depth = 0;

#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
	DesignImportUtils::EAutomationGenerationCancelPoint GAutomationGenerationCancelPoint =
		DesignImportUtils::EAutomationGenerationCancelPoint::None;
	bool GAutomationGenerationCancelArmed = false;

	bool IsAutomationCancelPoint(const DesignImportUtils::EAutomationGenerationCancelPoint CancelPoint)
	{
		return GAutomationGenerationCancelArmed && GAutomationGenerationCancelPoint == CancelPoint;
	}
#endif

	bool BeginInteractiveGenerationProgress(TUniquePtr<FScopedSlowTask>& OutSlowTask, const TCHAR* Context)
	{
		OutSlowTask.Reset();
		if (!ShouldUseInteractiveEditorWork())
		{
			return true;
		}

		OutSlowTask = MakeUnique<FScopedSlowTask>(
			3.0f,
			FText::Format(
				LOCTEXT("GenerateProgress", "Design2UMG: Generating Widget Blueprint ({0})"),
				FText::FromString(Context ? FString(Context) : FString(TEXT("Import")))));
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
#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
		if (IsAutomationCancelPoint(DesignImportUtils::EAutomationGenerationCancelPoint::BeforeBuild))
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("DesignImportUtils: %s cancelled before mutation by automation seam"),
				CancelContext ? CancelContext : TEXT("generation"));
			FDesign2UMGNotification::Warning(LOCTEXT("GenerationCancelled", "Design2UMG: Generation cancelled"));
			return false;
		}
#endif

		if (!SlowTask)
		{
			return true;
		}

		if (SlowTask->ShouldCancel())
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("DesignImportUtils: %s cancelled before step"),
				CancelContext ? CancelContext : TEXT("generation"));
			FDesign2UMGNotification::Warning(LOCTEXT("GenerationCancelled", "Design2UMG: Generation cancelled"));
			return false;
		}

		SlowTask->EnterProgressFrame(Work, StepText);
		if (SlowTask->ShouldCancel())
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("DesignImportUtils: %s cancelled after step"),
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
#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
		if (IsAutomationCancelPoint(DesignImportUtils::EAutomationGenerationCancelPoint::AfterBuildBeforeSave))
		{
			if (!bCancelRequestedAfterMutation)
			{
				bCancelRequestedAfterMutation = true;
				UE_LOG(LogDesign2UMG, Warning,
					TEXT("DesignImportUtils: %s cancellation requested after asset mutation by automation seam; completing generation to avoid partial state"),
					CancelContext ? CancelContext : TEXT("generation"));
			}
			return;
		}
#endif

		if (!SlowTask || !SlowTask->ShouldCancel() || bCancelRequestedAfterMutation)
		{
			return;
		}

		bCancelRequestedAfterMutation = true;
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("DesignImportUtils: %s cancellation requested after asset mutation; completing generation to avoid partial state"),
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
		CoreFn&& Core)
	{
		bOutCancelAfterMutation = false;

		const FString CancelContext = FString(Label) + TEXT(" generation");
		FInteractiveGenerationScope GenerationScope(Label);
		TUniquePtr<FScopedSlowTask> SlowTask;
		if (GenerationScope.IsOwner() && !BeginInteractiveGenerationProgress(SlowTask, Label))
		{
			FinishInteractiveGenerationProgress(SlowTask);
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("DesignImportUtils: %s generation cancelled before asset mutation"), Label);
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
	// its generation output (manifest payload + mapping diagnostics).
	struct FBuiltBlueprint
	{
		UWidgetBlueprint* WidgetBlueprint = nullptr;
		FDesign2UMGGenerationOutput GenerationOutput;
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
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget PNG render failed"));
			// Save the Blueprint even when rendering fails so the issue can be inspected.
			if (FUMGGenerator::SaveWidgetBlueprint(WBP, &GenerationOutput) && OutGeneratedAssetPath)
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
		if (!FUMGGenerator::SaveWidgetBlueprint(WBP, &GenerationOutput))
		{
			IFileManager::Get().Delete(*OutPng, false, true);
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget Blueprint compile or save failed after PNG render"));
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
		UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportUtils: Rendered PNG saved: %s (%dx%d)"), *OutPng, Size.X, Size.Y);
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
		FString* OutError)
	{
		UWidgetBlueprint* WidgetBlueprint = Built.WidgetBlueprint;
		FDesign2UMGGenerationOutput GenerationOutput = MoveTemp(Built.GenerationOutput);

		Progress.AdvanceAfterMutation(0.5f, LOCTEXT("GenerateProgressSave", "Saving generated assets"));
		if (!FUMGGenerator::SaveWidgetBlueprint(WidgetBlueprint, &GenerationOutput))
		{
			// Build-time diagnostics (mapping validation/hits) move out with GenerationOutput; they are not lost on the failure path.
			if (OutGenerationOutput)
			{
				*OutGenerationOutput = MoveTemp(GenerationOutput);
			}
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget Blueprint compile or save failed before compiled PNG render"));
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

		FString AutomationPreparationError;
		WidgetBlueprint = DesignImportUtils::ReloadWidgetBlueprintForRender(WidgetBlueprint, &AutomationPreparationError);
		if (!WidgetBlueprint)
		{
			if (!AutomationPreparationError.IsEmpty())
			{
				UE_LOG(LogDesign2UMG, Error,
					TEXT("DesignImportUtils: aborting compiled PNG render because shared automation prep failed: %s"),
					*AutomationPreparationError);
			}
			return false;
		}

		Progress.AdvanceAfterMutation(1.0f, LOCTEXT("GenerateProgressRender", "Rendering generated asset"));
		FString RenderError;
		const bool bCompiledRendered = DesignImportUtils::RenderCompiledBlueprintToPngFile(
			WidgetBlueprint,
			OutputPngFilePath,
			OutRenderedSize,
			&RenderError);
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

	// Shared skeleton of the two blueprint+PNG generation pipelines: the
	// interactive generation scope and slow task, the common build stage
	// (progress step -> pending icon font mapping changes -> unsaved blueprint
	// build) and the cancel-after-mutation tail. The save/render ordering
	// lives in the named pipeline stage. Only the build-failure diagnostics
	// differ between the pipelines (the transient-preview path logs, the
	// compiled path leaves error reporting to its caller), so that policy is
	// the one explicit flag.
	bool RunBlueprintAndPngPipeline(
		const TCHAR* Label,
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings,
		const bool bLogBuildFailure,
		TFunctionRef<bool(FBuiltBlueprint&& Built, FGenerationProgress& Progress)> Stage)
	{
		bool bCancelAfterMutation = false;
		const bool bOk = RunInteractiveGeneration(
			Label,
			false,
			bCancelAfterMutation,
			[&](FGenerationProgress& Progress) -> bool
			{
				if (!Progress.Advance(1.0f, LOCTEXT("GenerateProgressBuild", "Building Widget Blueprint")))
				{
					return false;
				}

				const FString FinalPath = ResolveImportOutputPath(OutputPath);
				DesignImportUtils::ApplyPendingIconFontMappingChanges(Data);
				FBuiltBlueprint Built;
				Built.WidgetBlueprint = FUMGGenerator::BuildWidgetBlueprint(Data, FinalPath, GenerationSettings, &Built.GenerationOutput);
				if (!Built.WidgetBlueprint)
				{
					if (bLogBuildFailure)
					{
						UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget Blueprint build failed"));
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

	FString NormalizeFontAssetPackagePath(const FString& PackagePath)
	{
		FString Normalized = PackagePath.TrimStartAndEnd();
		Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Normalized.EndsWith(TEXT("/")))
		{
			Normalized = Normalized.LeftChop(1);
		}
		return Normalized;
	}
}

bool DesignImportUtils::IsHttpUrl(const FString& Value)
{
	return Value.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
		|| Value.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
}

bool DesignImportUtils::IsCaseLocalAutomationImportedFontPath(const FString& FontAssetPath)
{
	const FString NormalizedPath = NormalizeFontAssetPackagePath(FontAssetPath);
	return NormalizedPath.Contains(TEXT("/Design2UMG/Automation/"), ESearchCase::CaseSensitive)
		&& NormalizedPath.Contains(TEXT("/ImportedFonts/"), ESearchCase::CaseSensitive)
		&& !NormalizedPath.Equals(TEXT("/Game/Design2UMG/Automation/ImportedFonts"), ESearchCase::CaseSensitive)
		&& !NormalizedPath.StartsWith(TEXT("/Game/Design2UMG/Automation/ImportedFonts/"), ESearchCase::CaseSensitive);
}

// Blueprint generation.

#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
void DesignImportUtils::SetAutomationGenerationCancelPoint(const EAutomationGenerationCancelPoint CancelPoint)
{
	GAutomationGenerationCancelPoint = CancelPoint;
	GAutomationGenerationCancelArmed = CancelPoint != EAutomationGenerationCancelPoint::None;
}

void DesignImportUtils::ResetAutomationGenerationCancelPoint()
{
	GAutomationGenerationCancelPoint = EAutomationGenerationCancelPoint::None;
	GAutomationGenerationCancelArmed = false;
}
#endif

UWidgetBlueprint* DesignImportUtils::ReloadWidgetBlueprintForRender(
	UWidgetBlueprint* WidgetBlueprint,
	FString* OutAutomationPreparationError)
{
	if (!WidgetBlueprint)
	{
		return nullptr;
	}

	const FString ObjectPath = WidgetBlueprint->GetPathName();
	const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
	if (PackageName.IsEmpty())
	{
		return WidgetBlueprint;
	}

	if (Design2UMGAutomationRender::IsAutomationRenderActive())
	{
		FString PrepareError;
		if (!Design2UMGAutomationRender::PrepareGeneratedAssetForAutomationRender(
			ObjectPath,
			true,
			PrepareError))
		{
			UE_LOG(LogDesign2UMG, Error,
				TEXT("DesignImportUtils: bootstrap render shared automation prep failed for %s: %s"),
				*ObjectPath,
				*PrepareError);
			if (OutAutomationPreparationError)
			{
				*OutAutomationPreparationError = PrepareError;
			}
			return nullptr;
		}
		else if (UWidgetBlueprint* ReloadedBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath))
		{
			return ReloadedBlueprint;
		}
	}

	UPackage* Package = FindPackage(nullptr, *PackageName);
	if (!Package)
	{
		Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		if (!Package)
		{
			return WidgetBlueprint;
		}
	}

	// Reload would discard unsaved in-memory edits to this blueprint (ResetLoaders +
	// ReloadPackage + CollectGarbage reload from disk). Editor edits mark the package
	// dirty, so a dirty package means the in-memory version must be kept and rendered.
	if (Package->IsDirty())
	{
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("DesignImportUtils: %s has unsaved changes; skipping pre-render reload and rendering the in-memory version"),
			*PackageName);
		return WidgetBlueprint;
	}

	FlushAsyncLoading();
	ResetLoaders(static_cast<UObject*>(Package));
	if (!ReloadPackage(Package, LOAD_None))
	{
		return WidgetBlueprint;
	}

	FlushRenderingCommands();
	CollectGarbage(RF_NoFlags, true);
	if (UWidgetBlueprint* ReloadedBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath))
	{
		return ReloadedBlueprint;
	}

	return WidgetBlueprint;
}

void DesignImportUtils::ApplyPendingIconFontMappingChanges(const FUMGWidgetData& Data)
{
	if (!Data.IconFontMappingChanges.HasChanges())
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

		if (DesignImportUtils::IsCaseLocalAutomationImportedFontPath(Pair.Value))
		{
			const TSoftObjectPtr<UFont>* ExistingTransient = Settings->IconFontAssetMap.Find(Pair.Key);
			if (ExistingTransient
				&& DesignImportUtils::IsCaseLocalAutomationImportedFontPath(ExistingTransient->ToString()))
			{
				Settings->IconFontAssetMap.Remove(Pair.Key);
				bModified = true;
				UE_LOG(LogDesign2UMG, Display, TEXT("Removed transient automation icon font mapping for '%s'"), *Pair.Key);
			}

			UE_LOG(LogDesign2UMG, Display,
				TEXT("Skip persisting transient automation icon font mapping '%s' -> '%s'"),
				*Pair.Key,
				*Pair.Value);
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

UWidgetBlueprint* DesignImportUtils::GenerateBlueprint(
	const FUMGWidgetData& Data,
	const FString& OutputPath,
	const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings,
	FDesign2UMGGenerationOutput* OutGenerationOutput)
{
	double StartTime = 0.0;
	bool bCancelAfterMutation = false;
	UWidgetBlueprint* const WBP = RunInteractiveGeneration<UWidgetBlueprint*>(
		TEXT("Blueprint"),
		nullptr,
		bCancelAfterMutation,
		[&](FGenerationProgress& Progress) -> UWidgetBlueprint*
		{
			StartTime = FPlatformTime::Seconds();
			const FString FinalPath = ResolveImportOutputPath(OutputPath);

			if (!Progress.Advance(1.0f, LOCTEXT("GenerateProgressBuild", "Building Widget Blueprint")))
			{
				return nullptr;
			}
			ApplyPendingIconFontMappingChanges(Data);
			FDesign2UMGGenerationOutput GenerationOutput;
			UWidgetBlueprint* BuiltWBP = FUMGGenerator::BuildWidgetBlueprint(Data, FinalPath, GenerationSettings, &GenerationOutput);
			if (!BuiltWBP)
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget Blueprint build failed"));
				FDesign2UMGNotification::Error(LOCTEXT("GenFailed", "Design2UMG: Widget Blueprint generation failed"));
				return nullptr;
			}

			Progress.AdvanceAfterMutation(1.0f, LOCTEXT("GenerateProgressSave", "Saving generated assets"));
			if (!FUMGGenerator::SaveWidgetBlueprint(BuiltWBP, &GenerationOutput))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget Blueprint compile or save failed"));
				FDesign2UMGNotification::Error(LOCTEXT("SaveFailed", "Design2UMG: Widget Blueprint compile or save failed"));
				return nullptr;
			}
			if (OutGenerationOutput)
			{
				*OutGenerationOutput = MoveTemp(GenerationOutput);
			}
			UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportUtils: Generated %s"), *BuiltWBP->GetPathName());

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

bool DesignImportUtils::GenerateBlueprintAndPng(
	const FUMGWidgetData& Data,
	const FString& OutputPath,
	FString* OutGeneratedAssetPath,
	FString* OutRenderPngPath,
	const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings,
	FDesign2UMGGenerationOutput* OutGenerationOutput)
{
	if (OutGeneratedAssetPath)
	{
		OutGeneratedAssetPath->Empty();
	}
	if (OutRenderPngPath)
	{
		OutRenderPngPath->Empty();
	}

	return RunBlueprintAndPngPipeline(
		TEXT("Blueprint and PNG"),
		Data,
		OutputPath,
		GenerationSettings,
		true,
		[&](FBuiltBlueprint&& Built, FGenerationProgress& Progress) -> bool
		{
			return BuildTransientPreviewThenSave(
				Data,
				MoveTemp(Built),
				Progress,
				OutGeneratedAssetPath,
				OutRenderPngPath,
				OutGenerationOutput);
		});
}

bool DesignImportUtils::RenderCompiledBlueprintToPngFile(
	UWidgetBlueprint* WidgetBlueprint,
	const FString& OutputPngFilePath,
	FIntPoint* OutRenderedSize,
	FString* OutError,
	const TOptional<FVector2D>& RenderSizeOverride,
	const TOptional<FVector2D>& RootExportOffset)
{
	if (!WidgetBlueprint)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: RenderCompiledBlueprintToPngFile invalid WidgetBlueprint"));
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
	if (!FUMGWidgetUtils::RenderWidgetToPng(WidgetBlueprint, PngData, ActualSize, OutError, RenderSizeOverride, RootExportOffset))
	{
		UE_LOG(LogDesign2UMG, Error,
			TEXT("DesignImportUtils: RenderCompiledBlueprintToPngFile failed for %s"),
			*WidgetBlueprint->GetPathName());
		return false;
	}

	if (!SavePngBytesToFile(PngData, OutputPngFilePath))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Failed to save compiled PNG: %s"), *OutputPngFilePath);
		return false;
	}

	if (OutRenderedSize)
	{
		*OutRenderedSize = ActualSize;
	}

	UE_LOG(LogDesign2UMG, Log,
		TEXT("DesignImportUtils: Rendered PNG saved: %s (%dx%d, compiled blueprint)"),
		*OutputPngFilePath, ActualSize.X, ActualSize.Y);
	return true;
}

bool DesignImportUtils::GenerateBlueprintAndCompiledPng(
	const FUMGWidgetData& Data,
	const FString& OutputPath,
	const FString& OutputPngFilePath,
	FString* OutGeneratedAssetPath,
	FIntPoint* OutRenderedSize,
	const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings,
	FDesign2UMGGenerationOutput* OutGenerationOutput,
	FString* OutError)
{
	if (OutGeneratedAssetPath)
	{
		OutGeneratedAssetPath->Empty();
	}
	if (OutRenderedSize)
	{
		*OutRenderedSize = FIntPoint::ZeroValue;
	}
	if (OutError)
	{
		OutError->Reset();
	}

	return RunBlueprintAndPngPipeline(
		TEXT("Compiled PNG"),
		Data,
		OutputPath,
		GenerationSettings,
		false,
		[&](FBuiltBlueprint&& Built, FGenerationProgress& Progress) -> bool
		{
			return SaveThenRenderCompiled(
				MoveTemp(Built),
				Progress,
				OutputPngFilePath,
				OutGeneratedAssetPath,
				OutRenderedSize,
				OutGenerationOutput,
				OutError);
		});
}

#undef LOCTEXT_NAMESPACE
