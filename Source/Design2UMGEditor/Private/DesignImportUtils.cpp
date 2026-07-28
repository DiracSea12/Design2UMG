// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportUtils.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotifications.h"
#include "Automation/Design2UMGAutomationRender.h"
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
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "DesignImportUtils"

namespace
{
	const TCHAR* ImportUtilsFallbackOutputPath = TEXT("/Game/Design2UMG/PenConvert_Output");

	FString ResolveImportOutputPath(const FString& OutputPath)
	{
		return OutputPath.IsEmpty() ? FString(ImportUtilsFallbackOutputPath) : OutputPath;
	}

	FIntPoint ResolveOutputRenderSize(UWidgetBlueprint* WidgetBlueprint)
	{
		if (WidgetBlueprint && WidgetBlueprint->GeneratedClass)
		{
			if (const UUserWidget* CDO = Cast<UUserWidget>(WidgetBlueprint->GeneratedClass->GetDefaultObject()))
			{
				const FVector2D DesignTimeSize = CDO->DesignTimeSize;
				if (DesignTimeSize.X > 1.0f && DesignTimeSize.Y > 1.0f)
				{
					const FIntPoint DesignRenderSize(
						FMath::CeilToInt(DesignTimeSize.X),
						FMath::CeilToInt(DesignTimeSize.Y));
					return DesignRenderSize;
				}
			}
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

	UWidgetBlueprint* ReloadWidgetBlueprintForRender(
		UWidgetBlueprint* WidgetBlueprint,
		FString* OutAutomationPreparationError = nullptr)
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

	struct FInteractiveGenerationScope
	{
		explicit FInteractiveGenerationScope(const TCHAR* Context)
		{
			if (!ShouldUseInteractiveEditorWork())
			{
				return;
			}

			++Depth;
			if (Depth == 1)
			{
				Transaction = BeginTransaction(Context);
			}
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
			return Transaction.IsValid();
		}

	private:
		static TUniquePtr<FScopedTransaction> BeginTransaction(const TCHAR* Context)
		{
			const FText Description = FText::Format(
				LOCTEXT("GenerateTransaction", "Design2UMG Generate Widget Blueprint ({0})"),
				FText::FromString(Context ? FString(Context) : FString(TEXT("Import"))));
			return MakeUnique<FScopedTransaction>(Description);
		}

		static int32 Depth;
		TUniquePtr<FScopedTransaction> Transaction;
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

	bool BeginInteractiveGenerationProgress(FScopedSlowTask*& OutSlowTask, const TCHAR* Context)
	{
		OutSlowTask = nullptr;
		if (!ShouldUseInteractiveEditorWork())
		{
			return true;
		}

		OutSlowTask = new FScopedSlowTask(
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

	void FinishInteractiveGenerationProgress(FScopedSlowTask*& SlowTask)
	{
		if (!SlowTask)
		{
			return;
		}

		delete SlowTask;
		SlowTask = nullptr;
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

		if (Design2UMGAutomationRender::IsCaseLocalAutomationImportedFontPath(Pair.Value))
		{
			const TSoftObjectPtr<UFont>* ExistingTransient = Settings->IconFontAssetMap.Find(Pair.Key);
			if (ExistingTransient
				&& Design2UMGAutomationRender::IsCaseLocalAutomationImportedFontPath(ExistingTransient->ToString()))
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
	}
}

UWidgetBlueprint* DesignImportUtils::GenerateBlueprint(
	const FUMGWidgetData& Data,
	const FString& OutputPath,
	const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings)
{
	FInteractiveGenerationScope TransactionScope(TEXT("Blueprint"));
	FScopedSlowTask* SlowTask = nullptr;
	if (TransactionScope.IsOwner() && !BeginInteractiveGenerationProgress(SlowTask, TEXT("Blueprint")))
	{
		FinishInteractiveGenerationProgress(SlowTask);
		UE_LOG(LogDesign2UMG, Warning, TEXT("DesignImportUtils: Blueprint generation cancelled before asset mutation"));
		return nullptr;
	}

	if (!AdvanceInteractiveGenerationProgress(
		SlowTask,
		0.0f,
		LOCTEXT("GenerateProgressPrepare", "Preparing import"),
		TEXT("Blueprint generation")))
	{
		FinishInteractiveGenerationProgress(SlowTask);
		return nullptr;
	}

	const double StartTime = FPlatformTime::Seconds();
	const FString FinalPath = ResolveImportOutputPath(OutputPath);

	if (!AdvanceInteractiveGenerationProgress(
		SlowTask,
		1.0f,
		LOCTEXT("GenerateProgressBuild", "Building Widget Blueprint"),
		TEXT("Blueprint generation")))
	{
		FinishInteractiveGenerationProgress(SlowTask);
		return nullptr;
	}
	ApplyPendingIconFontMappingChanges(Data);
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(Data, FinalPath, GenerationSettings);
	if (!WBP)
	{
		FinishInteractiveGenerationProgress(SlowTask);
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget Blueprint build failed"));
		FDesign2UMGNotification::Error(LOCTEXT("GenFailed", "Design2UMG: Widget Blueprint generation failed"));
		return nullptr;
	}

	bool bCancelRequestedAfterMutation = false;
	AdvanceInteractiveGenerationProgressAfterMutation(
		SlowTask,
		1.0f,
		LOCTEXT("GenerateProgressSave", "Saving generated assets"),
		TEXT("Blueprint generation"),
		bCancelRequestedAfterMutation);
	FUMGGenerator::SaveWidgetBlueprint(WBP);
	UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportUtils: Generated %s"), *WBP->GetPathName());

	AdvanceInteractiveGenerationProgressAfterMutation(
		SlowTask,
		1.0f,
		LOCTEXT("GenerateProgressDone", "Finishing import"),
		TEXT("Blueprint generation"),
		bCancelRequestedAfterMutation);
	FinishInteractiveGenerationProgress(SlowTask);
	if (bCancelRequestedAfterMutation)
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
	const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings)
{
	FInteractiveGenerationScope TransactionScope(TEXT("Blueprint and PNG"));
	FScopedSlowTask* SlowTask = nullptr;
	if (TransactionScope.IsOwner() && !BeginInteractiveGenerationProgress(SlowTask, TEXT("Blueprint and PNG")))
	{
		FinishInteractiveGenerationProgress(SlowTask);
		UE_LOG(LogDesign2UMG, Warning, TEXT("DesignImportUtils: Blueprint and PNG generation cancelled before asset mutation"));
		return false;
	}

	if (!AdvanceInteractiveGenerationProgress(
		SlowTask,
		0.0f,
		LOCTEXT("GenerateProgressPrepare", "Preparing import"),
		TEXT("Blueprint and PNG generation")))
	{
		FinishInteractiveGenerationProgress(SlowTask);
		return false;
	}

	const FString FinalPath = ResolveImportOutputPath(OutputPath);

	// Build first without saving immediately, then render PNG from a transient instance and save the Blueprint at the end.
	if (!AdvanceInteractiveGenerationProgress(
		SlowTask,
		1.0f,
		LOCTEXT("GenerateProgressBuild", "Building Widget Blueprint"),
		TEXT("Blueprint and PNG generation")))
	{
		FinishInteractiveGenerationProgress(SlowTask);
		return false;
	}
	ApplyPendingIconFontMappingChanges(Data);
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(Data, FinalPath, GenerationSettings);
	if (!WBP)
	{
		FinishInteractiveGenerationProgress(SlowTask);
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget Blueprint build failed"));
		return false;
	}
	if (OutGeneratedAssetPath)
	{
		*OutGeneratedAssetPath = WBP->GetPathName();
	}

	bool bCancelRequestedAfterMutation = false;
	FIntPoint Size = ResolveSourceWidgetTreeRenderSize(Data);
	const FString PngName = WBP ? WBP->GetName() : FPaths::GetBaseFilename(FinalPath);
	const FString OutPng = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (PngName + TEXT(".png"));
	if (OutRenderPngPath)
	{
		*OutRenderPngPath = OutPng;
	}
	AdvanceInteractiveGenerationProgressAfterMutation(
		SlowTask,
		0.5f,
		LOCTEXT("GenerateProgressRender", "Rendering generated asset"),
		TEXT("Blueprint and PNG generation"),
		bCancelRequestedAfterMutation);
	if (!RenderWidgetTreeToPngFile(WBP, OutPng, &Size))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget PNG render failed"));
		// Save the Blueprint even when rendering fails so the issue can be inspected.
		FUMGGenerator::SaveWidgetBlueprint(WBP);
		FinishInteractiveGenerationProgress(SlowTask);
		return false;
	}

	// Save the Blueprint after successful rendering.
	AdvanceInteractiveGenerationProgressAfterMutation(
		SlowTask,
		0.5f,
		LOCTEXT("GenerateProgressSave", "Saving generated assets"),
		TEXT("Blueprint and PNG generation"),
		bCancelRequestedAfterMutation);
	FUMGGenerator::SaveWidgetBlueprint(WBP);
	AdvanceInteractiveGenerationProgressAfterMutation(
		SlowTask,
		1.0f,
		LOCTEXT("GenerateProgressDone", "Finishing import"),
		TEXT("Blueprint and PNG generation"),
		bCancelRequestedAfterMutation);
	if (OutGeneratedAssetPath)
	{
		*OutGeneratedAssetPath = WBP->GetPathName();
	}
	if (OutRenderPngPath)
	{
		*OutRenderPngPath = OutPng;
	}
	UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportUtils: Rendered PNG saved: %s (%dx%d)"), *OutPng, Size.X, Size.Y);
	FinishInteractiveGenerationProgress(SlowTask);
	if (bCancelRequestedAfterMutation)
	{
		NotifyGenerationCompletedAfterCancel();
	}
	return true;
}

bool DesignImportUtils::RenderCompiledBlueprintToPngFile(
	UWidgetBlueprint* WidgetBlueprint,
	const FString& OutputPngFilePath,
	FIntPoint* OutRenderedSize)
{
	if (!WidgetBlueprint)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: RenderCompiledBlueprintToPngFile invalid WidgetBlueprint"));
		return false;
	}

	TArray<uint8> PngData;
	FIntPoint ActualSize = ResolveOutputRenderSize(WidgetBlueprint);
	if (!FUMGWidgetUtils::RenderWidgetToPng(WidgetBlueprint, PngData, ActualSize))
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
		TEXT("DesignImportUtils: Compiled PNG saved: %s (%dx%d)"),
		*OutputPngFilePath, ActualSize.X, ActualSize.Y);
	return true;
}

bool DesignImportUtils::GenerateBlueprintAndCompiledPng(
	const FUMGWidgetData& Data,
	const FString& OutputPath,
	const FString& OutputPngFilePath,
	FString* OutGeneratedAssetPath,
	FIntPoint* OutRenderedSize,
	const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings)
{
	FInteractiveGenerationScope TransactionScope(TEXT("Compiled PNG"));
	FScopedSlowTask* SlowTask = nullptr;
	if (TransactionScope.IsOwner() && !BeginInteractiveGenerationProgress(SlowTask, TEXT("Compiled PNG")))
	{
		FinishInteractiveGenerationProgress(SlowTask);
		UE_LOG(LogDesign2UMG, Warning, TEXT("DesignImportUtils: Compiled PNG generation cancelled before asset mutation"));
		return false;
	}

	if (!AdvanceInteractiveGenerationProgress(
		SlowTask,
		0.0f,
		LOCTEXT("GenerateProgressPrepare", "Preparing import"),
		TEXT("Compiled PNG generation")))
	{
		FinishInteractiveGenerationProgress(SlowTask);
		return false;
	}

	if (!AdvanceInteractiveGenerationProgress(
		SlowTask,
		1.0f,
		LOCTEXT("GenerateProgressBuild", "Building Widget Blueprint"),
		TEXT("Compiled PNG generation")))
	{
		FinishInteractiveGenerationProgress(SlowTask);
		return false;
	}

	const FString FinalPath = ResolveImportOutputPath(OutputPath);
	ApplyPendingIconFontMappingChanges(Data);
	UWidgetBlueprint* WidgetBlueprint = FUMGGenerator::BuildWidgetBlueprint(Data, FinalPath, GenerationSettings);
	if (!WidgetBlueprint)
	{
		FinishInteractiveGenerationProgress(SlowTask);
		return false;
	}

	bool bCancelRequestedAfterMutation = false;
	AdvanceInteractiveGenerationProgressAfterMutation(
		SlowTask,
		0.5f,
		LOCTEXT("GenerateProgressSave", "Saving generated assets"),
		TEXT("Compiled PNG generation"),
		bCancelRequestedAfterMutation);
	FUMGGenerator::SaveWidgetBlueprint(WidgetBlueprint);

	if (OutGeneratedAssetPath)
	{
		*OutGeneratedAssetPath = WidgetBlueprint->GetPathName();
	}

	{
		if (!FApp::IsUnattended())
		{
			AdvanceInteractiveGenerationProgressAfterMutation(
				SlowTask,
				0.5f,
				LOCTEXT("GenerateProgressRender", "Rendering generated asset"),
				TEXT("Compiled PNG generation"),
				bCancelRequestedAfterMutation);
			const FString TreeRenderPath = FPaths::Combine(
				FPaths::GetPath(OutputPngFilePath),
				FPaths::GetBaseFilename(OutputPngFilePath) + TEXT(".widgettree.png"));
			RenderWidgetTreeToPngFile(WidgetBlueprint, TreeRenderPath);
		}
	}

	FString AutomationPreparationError;
	WidgetBlueprint = ReloadWidgetBlueprintForRender(WidgetBlueprint, &AutomationPreparationError);
	if (!WidgetBlueprint)
	{
		if (!AutomationPreparationError.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Error,
				TEXT("DesignImportUtils: aborting compiled PNG render because shared automation prep failed: %s"),
				*AutomationPreparationError);
		}
		FinishInteractiveGenerationProgress(SlowTask);
		return false;
	}

	AdvanceInteractiveGenerationProgressAfterMutation(
		SlowTask,
		1.0f,
		LOCTEXT("GenerateProgressRender", "Rendering generated asset"),
		TEXT("Compiled PNG generation"),
		bCancelRequestedAfterMutation);
	const bool bRendered = RenderCompiledBlueprintToPngFile(
		WidgetBlueprint,
		OutputPngFilePath,
		OutRenderedSize);
	if (bRendered)
	{
		AdvanceInteractiveGenerationProgressAfterMutation(
			SlowTask,
			0.5f,
			LOCTEXT("GenerateProgressDone", "Finishing import"),
			TEXT("Compiled PNG generation"),
			bCancelRequestedAfterMutation);
	}
	FinishInteractiveGenerationProgress(SlowTask);
	if (bRendered && bCancelRequestedAfterMutation)
	{
		NotifyGenerationCompletedAfterCancel();
	}
	return bRendered;
}

#undef LOCTEXT_NAMESPACE





