// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "DesignImportPipeline.h"
#include "DesignPreviewMaterializationService.h"
#include "PreviewUserWidget.h"
#include "SDesign2UMGPreviewWindow.h"
#include "UMGJsonSerializer.h"
#include "Design2UMGNotifications.h"
#include "Design2UMGGenerationManifest.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/ScopeExit.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Editor.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Layout/WidgetPath.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Async/TaskGraphInterfaces.h"

#if WITH_AUTOMATION_WORKER
struct FDesign2UMGPreviewWindowTestAccess
{
	static FString Status(const SDesign2UMGPreviewWindow& Window) { return Window.GenerationStatus.ToString(); }
	static void UnbindConfirmation(SDesign2UMGPreviewWindow& Window) { Window.OnConfirmed.Unbind(); }
	static void Confirm(SDesign2UMGPreviewWindow& Window) { Window.OnGenerateClicked(); }
	static TSharedPtr<SMultiLineEditableTextBox> GetJsonEditor(const SDesign2UMGPreviewWindow& Window)
	{
		return Window.JsonEditor;
	}

	static TSharedPtr<SEditableTextBox> GetOutputPathBox(const SDesign2UMGPreviewWindow& Window)
	{
		return Window.OutputPathBox;
	}

	static TSharedPtr<SBox> GetPreviewContainer(const SDesign2UMGPreviewWindow& Window)
	{
		return Window.UMGPreviewContainer;
	}

	static bool CanConfirmLatestSnapshot(const SDesign2UMGPreviewWindow& Window)
	{
		return Window.CanConfirmLatestSnapshot();
	}

	static bool LatestApprovedSnapshotHasMaterializationSettings(const SDesign2UMGPreviewWindow& Window)
	{
		return Window.LatestApprovedSnapshot.IsSet()
			&& Window.LatestApprovedSnapshot->MaterializationSettingsSnapshot.IsSet();
	}

	static int32 GetLatestApprovedSnapshotMaterializationOverrideCount(const SDesign2UMGPreviewWindow& Window)
	{
		return LatestApprovedSnapshotHasMaterializationSettings(Window)
			? Window.LatestApprovedSnapshot->MaterializationSettingsSnapshot->ClassOverrides.Num()
			: 0;
	}
};

namespace
{
	FString PreviewWindowLabel(const TCHAR* Key)
	{
		return FText::FromStringTable(TEXT("Design2UMGPreviewWindow"), Key).ToString();
	}

	TSharedPtr<SWidget> FindWidgetOfType(const TSharedRef<SWidget>& Widget, const FName TypeName)
	{
		if (Widget->GetType() == TypeName)
		{
			return Widget;
		}
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
			{
				if (TSharedPtr<SWidget> Found = FindWidgetOfType(Children->GetChildAt(ChildIndex), TypeName))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}

	TSharedRef<SDesign2UMGPreviewWindow> GetPreviewWindowContent(SWindow& Window)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		return StaticCastSharedRef<SDesign2UMGPreviewWindow>(ConstCastSharedRef<SWidget>(Window.GetContent()));
#else
		return StaticCastSharedRef<SDesign2UMGPreviewWindow>(Window.GetContent());
#endif
	}

	constexpr TCHAR PreviewHarnessAnalyzerWidgetName[] = TEXT("WBP_SpyPreviewHarness");
	constexpr TCHAR PreviewHarnessAnalyzerRootName[] = TEXT("SpyPreviewHarnessRoot");

	FString ExtractFirstWidgetText(const FUMGWidgetNode& WidgetNode)
	{
		if (WidgetNode.Text.Text.IsSet() && !WidgetNode.Text.Text.GetValue().IsEmpty())
		{
			return WidgetNode.Text.Text.GetValue();
		}

		for (const FUMGWidgetNode& Child : WidgetNode.Children)
		{
			const FString ChildText = ExtractFirstWidgetText(Child);
			if (!ChildText.IsEmpty())
			{
				return ChildText;
			}
		}

		return FString();
	}

	FString ReadPreviewUserWidgetText(const UUserWidget* PreviewWidget)
	{
		if (!PreviewWidget || !PreviewWidget->WidgetTree)
		{
			return FString();
		}

		const UTextBlock* RootTextBlock = Cast<UTextBlock>(PreviewWidget->WidgetTree->RootWidget);
		return RootTextBlock ? RootTextBlock->GetText().ToString() : FString();
	}

	TSharedPtr<STextBlock> FindTextBlockByText(const TSharedRef<SWidget>& Widget, const FString& ExpectedText)
	{
		if (Widget->GetType() == STextBlock::StaticWidgetClass().GetWidgetType())
		{
			const TSharedRef<STextBlock> TextBlock = StaticCastSharedRef<STextBlock>(Widget);
			if (TextBlock->GetText().ToString() == ExpectedText)
			{
				return TextBlock;
			}
		}

		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
			{
				if (TSharedPtr<STextBlock> Found = FindTextBlockByText(Children->GetChildAt(ChildIndex), ExpectedText))
				{
					return Found;
				}
			}
		}

		return nullptr;
	}

	bool WidgetTreeContainsText(const TSharedRef<SWidget>& Widget, const FString& ExpectedText)
	{
		return FindTextBlockByText(Widget, ExpectedText).IsValid();
	}

	bool WidgetTreeContainsTextFragment(const TSharedRef<SWidget>& Widget, const FString& Fragment)
	{
		if (Widget->GetType() == STextBlock::StaticWidgetClass().GetWidgetType()
			&& StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(Fragment))
		{
			return true;
		}
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
			{
				if (WidgetTreeContainsTextFragment(Children->GetChildAt(ChildIndex), Fragment))
				{
					return true;
				}
			}
		}
		return false;
	}

	TSharedPtr<SButton> FindButtonByLabel(const TSharedRef<SWidget>& Widget, const FString& ExpectedText)
	{
		if (Widget->GetType() == SButton::StaticWidgetClass().GetWidgetType()
			&& WidgetTreeContainsText(Widget, ExpectedText))
		{
			return StaticCastSharedRef<SButton>(Widget);
		}

		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
			{
				if (TSharedPtr<SButton> FoundButton = FindButtonByLabel(Children->GetChildAt(ChildIndex), ExpectedText))
				{
					return FoundButton;
				}
			}
		}

		return nullptr;
	}

	struct FPreviewHarnessTelemetry
	{
		bool bPreviewUiOpened = false;
		bool bPreviewWindowClosed = false;
	};

	FString MakePreviewHarnessJson(const FString& LabelText)
	{
		FUMGWidgetData Data;
		Data.WidgetName = TEXT("WBP_PreviewHarnessFixture");
		Data.RootWidget.Type = TEXT("CanvasPanel");
		Data.RootWidget.Id = TEXT("preview_root");
		Data.RootWidget.Name = TEXT("PreviewRoot");
		Data.RootWidget.Bounds = { 0.0f, 0.0f, 320.0f, 180.0f };

		FUMGWidgetNode LabelWidget;
		LabelWidget.Type = TEXT("TextBlock");
		LabelWidget.Id = TEXT("preview_label");
		LabelWidget.Name = TEXT("PreviewLabel");
		LabelWidget.Bounds = { 24.0f, 24.0f, 160.0f, 32.0f };
		LabelWidget.Text.Text = LabelText;
		LabelWidget.TextStyle.FontSize = 18.0f;
		Data.RootWidget.Children.Add(LabelWidget);

		return FUMGJsonSerializer::Serialize(Data);
	}

	FUMGWidgetData MakeInjectedPreviewAnalysisData(const FString& LabelText)
	{
		FUMGWidgetData Data;
		Data.WidgetName = PreviewHarnessAnalyzerWidgetName;
		Data.RootWidget.Type = TEXT("CanvasPanel");
		Data.RootWidget.Id = TEXT("spy_preview_root");
		Data.RootWidget.Name = PreviewHarnessAnalyzerRootName;
		Data.RootWidget.Bounds = { 0.0f, 0.0f, 420.0f, 220.0f };

		FUMGWidgetNode LabelWidget;
		LabelWidget.Type = TEXT("TextBlock");
		LabelWidget.Id = TEXT("spy_preview_label");
		LabelWidget.Name = TEXT("SpyPreviewLabel");
		LabelWidget.Bounds = { 16.0f, 16.0f, 220.0f, 32.0f };
		LabelWidget.Text.Text = LabelText;
		LabelWidget.TextStyle.FontSize = 20.0f;
		Data.RootWidget.Children.Add(LabelWidget);
		return Data;
	}

	class FSpyPreviewRefreshAnalyzer final : public IDesignPreviewRefreshAnalyzer
	{
	public:
		virtual FPreviewRefreshResult AnalyzePreview(const FPreviewRefreshInput& Input) override
		{
			++InvocationCount;
			LastAnalyzedRequestId = Input.RequestId;
			LastAnalyzedJson = Input.EditedJson;

			FPreviewRefreshResult Result;
			Result.RequestId = Input.RequestId;
			Result.EditedJson = Input.EditedJson;

			if (bFailNextRequest)
			{
				bFailNextRequest = false;
				Result.ParseError = FailureMessage;
				LastAnalyzedWidgetName.Reset();
				return Result;
			}

			++InvocationSerial;
			Result.Data = MakeInjectedPreviewAnalysisData(FString::Printf(TEXT("SpyAnalyzed-%d"), InvocationSerial));
			Result.ImageResult = NextImageResult.IsSet() ? NextImageResult.GetValue() : Input.InitialImageResult;
			Result.bSucceeded = true;

			LastAnalyzedWidgetName = Result.Data.WidgetName;
			bInjectedSeamObserved = (Result.Data.WidgetName == PreviewHarnessAnalyzerWidgetName);
			return Result;
		}

		void ResetObservation()
		{
			InvocationCount = 0;
			InvocationSerial = 0;
			LastAnalyzedRequestId = 0;
			bInjectedSeamObserved = false;
			LastAnalyzedJson.Reset();
			LastAnalyzedWidgetName.Reset();
			bFailNextRequest = false;
			FailureMessage = TEXT("Injected preview failure");
		}

		void FailNextRequest(const FString& InFailureMessage)
		{
			bFailNextRequest = true;
			FailureMessage = InFailureMessage;
		}
		void SetNextImageResult(const FDesignImageImportResult& InResult)
		{
			NextImageResult = InResult;
		}

		int32 GetInvocationCount() const
		{
			return InvocationCount;
		}

		bool DidObserveInjectedSeam() const
		{
			return bInjectedSeamObserved;
		}

		const FString& GetLastAnalyzedJson() const
		{
			return LastAnalyzedJson;
		}

		const FString& GetLastAnalyzedWidgetName() const
		{
			return LastAnalyzedWidgetName;
		}

		uint64 GetLastAnalyzedRequestId() const
		{
			return LastAnalyzedRequestId;
		}

	private:
		int32 InvocationCount = 0;
		int32 InvocationSerial = 0;
		uint64 LastAnalyzedRequestId = 0;
		bool bInjectedSeamObserved = false;
		bool bFailNextRequest = false;
		FString FailureMessage = TEXT("Injected preview failure");
		TOptional<FDesignImageImportResult> NextImageResult;
		FString LastAnalyzedJson;
		FString LastAnalyzedWidgetName;
	};

	class FSpyPreviewMaterializationService final : public IDesignPreviewMaterializationService
	{
	public:
		FString ForcedErrorMessage;

		virtual bool MaterializePreview(const FDesignPreviewMaterializationRequest& Request, FDesignPreviewMaterializationResult& OutResult) override
		{
			++InvocationCount;
			LastMaterializedRequestId = Request.RequestId;
			LastMaterializedWidgetName = Request.ApprovedSnapshot.Data.WidgetName;
			bSawMaterializationSettingsSnapshot = Request.ApprovedSnapshot.MaterializationSettingsSnapshot.IsSet();
			LastMaterializationSnapshotOverrideCount =
				Request.ApprovedSnapshot.MaterializationSettingsSnapshot.IsSet()
					? Request.ApprovedSnapshot.MaterializationSettingsSnapshot->ClassOverrides.Num()
					: 0;

			const bool bSawInjectedAnalyzerPayload =
				Request.ApprovedSnapshot.Data.WidgetName == PreviewHarnessAnalyzerWidgetName
				&& Request.ApprovedSnapshot.Data.RootWidget.Name == PreviewHarnessAnalyzerRootName;
			bInjectedSeamObserved = bSawInjectedAnalyzerPayload;
			if (!ForcedErrorMessage.IsEmpty())
			{
				Reset();
				OutResult = FDesignPreviewMaterializationResult();
				OutResult.RequestId = Request.RequestId;
				OutResult.ErrorMessage = ForcedErrorMessage;
				return false;
			}

			UPreviewUserWidget* NewPreviewWidget = NewObject<UPreviewUserWidget>(
				GetTransientPackage(),
				UPreviewUserWidget::StaticClass(),
				NAME_None,
				RF_Transient);
			if (!NewPreviewWidget)
			{
				return false;
			}

			NewPreviewWidget->WidgetTree = NewObject<UWidgetTree>(NewPreviewWidget, UWidgetTree::StaticClass(), NAME_None, RF_Transient);
			if (!NewPreviewWidget->WidgetTree)
			{
				return false;
			}

			UTextBlock* RootText = NewPreviewWidget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SpyPreviewWidget"));
			if (!RootText)
			{
				return false;
			}

			const FString DisplayText = bSawInjectedAnalyzerPayload
				? FString::Printf(TEXT("SpyMaterialized:%s"), *ExtractFirstWidgetText(Request.ApprovedSnapshot.Data.RootWidget))
				: TEXT("UnexpectedBypass");
			LastPreviewWidgetText = DisplayText;
			RootText->SetText(FText::FromString(DisplayText));
			NewPreviewWidget->WidgetTree->RootWidget = RootText;

			MaterializedPreviewWidget.Reset(NewPreviewWidget);
			OutResult = FDesignPreviewMaterializationResult();
			OutResult.bSucceeded = true;
			OutResult.RequestId = Request.RequestId;
			OutResult.PreviewDesignSize = FVector2D(
				FMath::Max(1.0f, Request.ApprovedSnapshot.Data.RootWidget.Bounds.W),
				FMath::Max(1.0f, Request.ApprovedSnapshot.Data.RootWidget.Bounds.H));
			OutResult.MaterializedWidgetName = Request.ApprovedSnapshot.Data.WidgetName;
			return true;
		}

		virtual UUserWidget* GetMaterializedPreviewWidget() const override
		{
			return MaterializedPreviewWidget.Get();
		}

		virtual void Reset() override
		{
			if (MaterializedPreviewWidget.Get())
			{
				if (MaterializedPreviewWidget->WidgetTree)
				{
					MaterializedPreviewWidget->WidgetTree->RootWidget = nullptr;
				}
				MaterializedPreviewWidget.Reset();
			}
		}

		UUserWidget* GetLastPreviewWidget() const
		{
			return MaterializedPreviewWidget.Get();
		}

		void ResetObservation()
		{
			InvocationCount = 0;
			LastMaterializedRequestId = 0;
			bInjectedSeamObserved = false;
			bSawMaterializationSettingsSnapshot = false;
			LastMaterializedWidgetName.Reset();
			LastPreviewWidgetText.Reset();
			LastMaterializationSnapshotOverrideCount = 0;
		}

		int32 GetInvocationCount() const
		{
			return InvocationCount;
		}

		bool DidObserveInjectedSeam() const
		{
			return bInjectedSeamObserved;
		}

		const FString& GetLastMaterializedWidgetName() const
		{
			return LastMaterializedWidgetName;
		}

		const FString& GetLastPreviewWidgetText() const
		{
			return LastPreviewWidgetText;
		}

		uint64 GetLastMaterializedRequestId() const
		{
			return LastMaterializedRequestId;
		}

		bool SawMaterializationSettingsSnapshot() const
		{
			return bSawMaterializationSettingsSnapshot;
		}

		int32 GetLastMaterializationSnapshotOverrideCount() const
		{
			return LastMaterializationSnapshotOverrideCount;
		}

	private:
		int32 InvocationCount = 0;
		uint64 LastMaterializedRequestId = 0;
		bool bInjectedSeamObserved = false;
		bool bSawMaterializationSettingsSnapshot = false;
		FString LastMaterializedWidgetName;
		FString LastPreviewWidgetText;
		int32 LastMaterializationSnapshotOverrideCount = 0;
		TStrongObjectPtr<UUserWidget> MaterializedPreviewWidget;
	};

	class FPreviewConfirmDelegateSpy final
	{
	public:
		FArtifactGenerationResult Result;
		FPreviewConfirmDelegateSpy()
		{
			Result.bSucceeded = true;
			Result.Commit.Status = EDesign2UMGCommitStatus::Succeeded;
		}
		void HandleConfirmed(const FPreviewGenerationRequest& GenerationRequest)
		{
			++InvocationCount;
			LastApprovedRequestId = GenerationRequest.ApprovedSnapshot.RequestId;
			LastConfirmedJson = GenerationRequest.ApprovedSnapshot.EditedJson;
			LastConfirmedOutputPath = GenerationRequest.GenerationRequest.OutputPath;
			LastConfirmedWidgetName = GenerationRequest.ApprovedSnapshot.Data.WidgetName;
			LastConfirmedImageResult = GenerationRequest.ApprovedSnapshot.ImageResult;
		}

		void ResetObservation()
		{
			InvocationCount = 0;
			LastApprovedRequestId = 0;
			LastConfirmedJson.Reset();
			LastConfirmedOutputPath.Reset();
			LastConfirmedWidgetName.Reset();
			LastConfirmedImageResult = FDesignImageImportResult();
		}

		int32 GetInvocationCount() const
		{
			return InvocationCount;
		}

		const FString& GetLastConfirmedJson() const
		{
			return LastConfirmedJson;
		}

		const FString& GetLastConfirmedOutputPath() const
		{
			return LastConfirmedOutputPath;
		}

		const FString& GetLastConfirmedWidgetName() const
		{
			return LastConfirmedWidgetName;
		}

		const FDesignImageImportResult& GetLastConfirmedImageResult() const
		{
			return LastConfirmedImageResult;
		}

		uint64 GetLastApprovedRequestId() const
		{
			return LastApprovedRequestId;
		}

	private:
		int32 InvocationCount = 0;
		uint64 LastApprovedRequestId = 0;
		FString LastConfirmedJson;
		FString LastConfirmedOutputPath;
		FString LastConfirmedWidgetName;
		FDesignImageImportResult LastConfirmedImageResult;
	};

	class FPreviewCancelDelegateSpy final
	{
	public:
		void HandleCancelled()
		{
			++InvocationCount;
		}

		void ResetObservation()
		{
			InvocationCount = 0;
		}

		int32 GetInvocationCount() const
		{
			return InvocationCount;
		}

	private:
		int32 InvocationCount = 0;
	};

	struct FPreviewWindowHarness
	{
		TSharedPtr<FPreviewHarnessTelemetry> Telemetry = MakeShared<FPreviewHarnessTelemetry>();
		TSharedPtr<FSpyPreviewRefreshAnalyzer> AnalyzerSpy = MakeShared<FSpyPreviewRefreshAnalyzer>();
		TSharedPtr<FSpyPreviewMaterializationService> MaterializationSpy = MakeShared<FSpyPreviewMaterializationService>();
		TSharedPtr<FPreviewConfirmDelegateSpy> ConfirmDelegateSpy = MakeShared<FPreviewConfirmDelegateSpy>();
		TSharedPtr<FPreviewCancelDelegateSpy> CancelDelegateSpy = MakeShared<FPreviewCancelDelegateSpy>();
		TSharedPtr<SWindow> Window;
		TSharedPtr<SDesign2UMGPreviewWindow> PreviewWindow;

		~FPreviewWindowHarness()
		{
			Close();
		}

		void PumpSlate(int32 TickCount = 2)
		{
			if (!FSlateApplication::IsInitialized())
			{
				return;
			}

			for (int32 TickIndex = 0; TickIndex < TickCount; ++TickIndex)
			{
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
				FSlateApplication::Get().PumpMessages();
				FSlateApplication::Get().Tick(ESlateTickType::All);
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			}
		}

		bool Open(
			const FString& OriginalJson,
			const FString& OutputPath,
			const TArray<FString>& Warnings = TArray<FString>(),
			const TOptional<FDesignPreviewMaterializationSettingsSnapshot>& InitialMaterializationSettingsSnapshot =
				TOptional<FDesignPreviewMaterializationSettingsSnapshot>(),
			const bool bPumpAfterOpen = true,
			const FDesignImageImportResult& InitialImageResult = FDesignImageImportResult())
		{
			if (!FSlateApplication::IsInitialized())
			{
				return false;
			}

			Window = SNew(SWindow)
				.Title(FText::FromString(TEXT("Design2UMG Preview Harness")))
				.ClientSize(FVector2D(1200.0f, 720.0f))
				.SupportsMinimize(false)
				.SupportsMaximize(true);
			Window->GetOnWindowClosedEvent().AddLambda(
				[Telemetry = Telemetry](const TSharedRef<SWindow>&)
				{
					Telemetry->bPreviewWindowClosed = true;
				});

			Window->SetContent(
				SAssignNew(PreviewWindow, SDesign2UMGPreviewWindow)
				.OriginalJson(OriginalJson)
				.InitialImageResult(InitialImageResult)
				.OutputPath(OutputPath)
				.Warnings(Warnings)
				.InitialMaterializationSettingsSnapshot(InitialMaterializationSettingsSnapshot)
				.PreviewRefreshAnalyzer(AnalyzerSpy)
				.PreviewMaterializationService(MaterializationSpy)
				.OnConfirmed(FOnPreviewConfirmed::CreateLambda(
					[ConfirmDelegateSpy = ConfirmDelegateSpy](const FPreviewGenerationRequest& GenerationRequest)
					{
						ConfirmDelegateSpy->HandleConfirmed(GenerationRequest);
						return ConfirmDelegateSpy->Result;
					}))
				.OnCancelled(FOnPreviewCancelled::CreateLambda(
					[CancelDelegateSpy = CancelDelegateSpy]()
					{
						CancelDelegateSpy->HandleCancelled();
					})));

			FSlateApplication::Get().AddWindow(Window.ToSharedRef());
			if (bPumpAfterOpen)
			{
				PumpSlate();
			}
			Telemetry->bPreviewUiOpened = IsWindowAttached();
			return PreviewWindow.IsValid();
		}

		void ResetObservedTelemetry()
		{
			Telemetry->bPreviewWindowClosed = false;
			AnalyzerSpy->ResetObservation();
			MaterializationSpy->ResetObservation();
			ConfirmDelegateSpy->ResetObservation();
			CancelDelegateSpy->ResetObservation();
		}

		bool RequestRefresh(const FString& EditedJson)
		{
			if (!PreviewWindow.IsValid())
			{
				return false;
			}

			const TSharedPtr<SMultiLineEditableTextBox> JsonEditor = FDesign2UMGPreviewWindowTestAccess::GetJsonEditor(*PreviewWindow);
			if (!JsonEditor.IsValid())
			{
				return false;
			}

			JsonEditor->SetText(FText::FromString(EditedJson));
			const TSharedPtr<SButton> FormatButton = FindButtonByLabel(PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("FormatJson")));
			if (!FormatButton.IsValid())
			{
				return false;
			}

			FormatButton->SimulateClick();
			PumpSlate(4);
			return DidObserveInjectedPreviewRefresh();
		}

		void SetOutputPath(const FString& OutputPath)
		{
			if (!PreviewWindow.IsValid())
			{
				return;
			}

			const TSharedPtr<SEditableTextBox> OutputPathBox = FDesign2UMGPreviewWindowTestAccess::GetOutputPathBox(*PreviewWindow);
			if (!OutputPathBox.IsValid())
			{
				return;
			}

			OutputPathBox->SetText(FText::FromString(OutputPath));
			PumpSlate(1);
		}

		bool Confirm()
		{
			if (!PreviewWindow.IsValid() || !FDesign2UMGPreviewWindowTestAccess::CanConfirmLatestSnapshot(*PreviewWindow))
			{
				return false;
			}

			const int32 PreviousInvocationCount = ConfirmDelegateSpy->GetInvocationCount();
			const TSharedPtr<SButton> GenerateButton = FindButtonByLabel(PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("Generate")));
			if (!GenerateButton.IsValid())
			{
				return false;
			}

			GenerateButton->SimulateClick();
			PumpSlate(6);
			return ConfirmDelegateSpy->GetInvocationCount() == PreviousInvocationCount + 1;
		}

		bool CanConfirm() const
		{
			return PreviewWindow.IsValid()
				&& FDesign2UMGPreviewWindowTestAccess::CanConfirmLatestSnapshot(*PreviewWindow);
		}

		bool Cancel()
		{
			if (!PreviewWindow.IsValid())
			{
				return false;
			}

			const int32 PreviousInvocationCount = CancelDelegateSpy->GetInvocationCount();
			const TSharedPtr<SButton> CancelButton = FindButtonByLabel(PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("Cancel")));
			if (!CancelButton.IsValid())
			{
				return false;
			}

			CancelButton->SimulateClick();
			PumpSlate(6);
			return CancelDelegateSpy->GetInvocationCount() == PreviousInvocationCount + 1;
		}

		bool HasPreviewWidget() const
		{
			return MaterializationSpy->GetLastPreviewWidget() != nullptr;
		}

		bool IsWindowAttached() const
		{
			if (Telemetry->bPreviewWindowClosed)
			{
				return false;
			}

			return FSlateApplication::IsInitialized()
				&& PreviewWindow.IsValid()
				&& FSlateApplication::Get().FindWidgetWindow(PreviewWindow.ToSharedRef()).IsValid();
		}

		bool PreviewWidgetMatchesSpyMaterialization() const
		{
			if (!PreviewWindow.IsValid())
			{
				return false;
			}

			const TSharedPtr<SBox> PreviewContainer = FDesign2UMGPreviewWindowTestAccess::GetPreviewContainer(*PreviewWindow);
			return PreviewContainer.IsValid()
				&& MaterializationSpy->GetLastPreviewWidget() != nullptr
				&& WidgetTreeContainsText(
					StaticCastSharedRef<SWidget>(PreviewContainer.ToSharedRef()),
					MaterializationSpy->GetLastPreviewWidgetText());
		}

		FString GetPreviewWidgetText() const
		{
			return ReadPreviewUserWidgetText(MaterializationSpy->GetLastPreviewWidget());
		}

		bool DidObserveInjectedPreviewRefresh() const
		{
			const FString PreviewText = GetPreviewWidgetText();
			return HasPreviewWidget()
				&& AnalyzerSpy->GetInvocationCount() > 0
				&& MaterializationSpy->GetInvocationCount() > 0
				&& AnalyzerSpy->DidObserveInjectedSeam()
				&& MaterializationSpy->DidObserveInjectedSeam()
				&& PreviewWidgetMatchesSpyMaterialization()
				&& PreviewText == MaterializationSpy->GetLastPreviewWidgetText()
				&& PreviewText.StartsWith(TEXT("SpyMaterialized:SpyAnalyzed-"));
		}

		void Close()
		{
			if (Window.IsValid())
			{
				FSlateApplication::Get().DestroyWindowImmediately(Window.ToSharedRef());
			}

			PreviewWindow.Reset();
			Window.Reset();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewWindowTest,
	"Design2UMG.PreviewWindow.Initialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewWindowTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}

	FPreviewWindowHarness Harness;
	TestTrue(TEXT("Preview harness opens window"),
		Harness.Open(MakePreviewHarnessJson(TEXT("Initial Preview")), TEXT("/Game/Test")));
	TestTrue(TEXT("Preview window instantiated"), Harness.PreviewWindow.IsValid());
	TestTrue(TEXT("Preview UI is marked open"), Harness.Telemetry->bPreviewUiOpened);
	TestEqual(TEXT("Initial open runs analyzer once"), Harness.AnalyzerSpy->GetInvocationCount(), 1);
	TestEqual(TEXT("Initial open runs materialization once"), Harness.MaterializationSpy->GetInvocationCount(), 1);
	TestTrue(TEXT("Initial open uses injected analyzer seam"), Harness.AnalyzerSpy->DidObserveInjectedSeam());
	TestTrue(TEXT("Initial open uses injected materialization seam"), Harness.MaterializationSpy->DidObserveInjectedSeam());
	TestTrue(TEXT("Preview widget is materialized on initial open"), Harness.HasPreviewWidget());
	TestTrue(TEXT("Initial open adopts spy materialized widget"), Harness.PreviewWidgetMatchesSpyMaterialization());
	TestTrue(TEXT("Initial open proves preview refresh used injected seam"), Harness.DidObserveInjectedPreviewRefresh());
	TestEqual(TEXT("Initial open preview text comes from injected seam"), Harness.GetPreviewWidgetText(), TEXT("SpyMaterialized:SpyAnalyzed-1"));
	TestTrue(TEXT("Preview window is attached to Slate"), Harness.IsWindowAttached());
	const UUserWidget* InitialWidget = Harness.MaterializationSpy->GetLastPreviewWidget();
	TSharedPtr<SButton> ScaleButton = FindButtonByLabel(Harness.PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("PreviewActualSize")));
	if (!TestTrue(TEXT("Preview starts fitted with an original-size action"), ScaleButton.IsValid())) return false;
	ScaleButton->SimulateClick();
	ScaleButton = FindButtonByLabel(Harness.PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("FitPreview")));
	if (!TestTrue(TEXT("Original-size view offers fit to window"), ScaleButton.IsValid())) return false;
	TestTrue(TEXT("Original-size view retains preview content"), Harness.PreviewWidgetMatchesSpyMaterialization());
	ScaleButton->SimulateClick();
	TestTrue(TEXT("Fit view retains preview content"), Harness.PreviewWidgetMatchesSpyMaterialization());
	TestTrue(TEXT("Scale changes reuse the same preview widget"), Harness.MaterializationSpy->GetLastPreviewWidget() == InitialWidget);
	TestEqual(TEXT("Scale changes do not rematerialize the preview"), Harness.MaterializationSpy->GetInvocationCount(), 1);
	TestTrue(TEXT("Scale changes retain the approved generation snapshot"), FDesign2UMGPreviewWindowTestAccess::CanConfirmLatestSnapshot(*Harness.PreviewWindow));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewWindowManyWarningsLayoutTest,
	"Design2UMG.PreviewWindow.ManyWarningsLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewWindowManyWarningsLayoutTest::RunTest(const FString& Parameters)
{
	TArray<FString> Warnings;
	for (int32 WarningIndex = 0; WarningIndex < 400; ++WarningIndex)
	{
		Warnings.Add(FString::Printf(TEXT("Node 'shared-node': 告警 %d，完整详情应可滚动查看。"), WarningIndex));
	}
	FDesignImageImportResult ImageResult;
	for (int32 ImageIndex = 0; ImageIndex < 48; ++ImageIndex)
	{
		FDesignImageDiagnostic Diagnostic;
		Diagnostic.Outcome = EDesignImageDiagnosticOutcome::Failed;
		Diagnostic.Category = EDesignImageDiagnosticCategory::UnresolvedReference;
		Diagnostic.Message = FString::Printf(TEXT("图片告警 %d，保留原始原因。"), ImageIndex);
		ImageResult.AddDiagnostic(MoveTemp(Diagnostic));
	}
	FPreviewWindowHarness Harness;
	if (!TestTrue(TEXT("Preview opens with many diagnostics"), Harness.Open(
		MakePreviewHarnessJson(TEXT("Many warnings")), TEXT("/Game/Test"), Warnings,
		TOptional<FDesignPreviewMaterializationSettingsSnapshot>(), true, ImageResult)))
	{
		return false;
	}
	const FText SummaryPattern = FText::FromStringTable(TEXT("Design2UMGPreviewWindow"), TEXT("ConversionWarningsSummary"));
	TArray<FString> SummaryParameters;
	FText::GetFormatPatternParameters(SummaryPattern, SummaryParameters);
	TestTrue(TEXT("Summary template retains the warning-count parameter"), SummaryParameters.Contains(TEXT("0")));
	const FText WarningCount = FText::AsNumber(Warnings.Num());
	const FString ExpectedSummary = TEXT("转换告警：400 条");
	TestTrue(TEXT("Summary counts warnings, not distinct nodes"),
		WidgetTreeContainsText(Harness.PreviewWindow.ToSharedRef(), ExpectedSummary));
	const TSharedPtr<STextBlock> Summary = FindTextBlockByText(Harness.PreviewWindow.ToSharedRef(), ExpectedSummary);
	TestTrue(TEXT("Displayed summary contains the input warning count"), Summary.IsValid() && Summary->GetText().ToString().Contains(WarningCount.ToString()));
	for (const FString& Warning : Warnings)
	{
		TestTrue(TEXT("Every conversion warning is retained"), WidgetTreeContainsTextFragment(Harness.PreviewWindow.ToSharedRef(), Warning));
	}
	for (const FDesignImageDiagnostic& Diagnostic : ImageResult.Diagnostics)
	{
		TestTrue(TEXT("Every image failure is retained, including beyond 32"),
			WidgetTreeContainsTextFragment(Harness.PreviewWindow.ToSharedRef(), Diagnostic.Message));
	}
	const TSharedPtr<SScrollBox> Diagnostics = StaticCastSharedPtr<SScrollBox>(FindWidgetOfType(Harness.PreviewWindow.ToSharedRef(), TEXT("SScrollBox")));
	const TSharedPtr<SWidget> FontTool = FindWidgetOfType(Harness.PreviewWindow.ToSharedRef(), TEXT("SPencilFontTool"));
	const TSharedPtr<SButton> Generate = FindButtonByLabel(Harness.PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("Generate")));
	const TSharedPtr<SButton> Cancel = FindButtonByLabel(Harness.PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("Cancel")));
	const TSharedPtr<SButton> Format = FindButtonByLabel(Harness.PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("FormatJson")));
	const TSharedPtr<SButton> Scale = FindButtonByLabel(Harness.PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("PreviewActualSize")));
	const TSharedPtr<STextBlock> MainTitle = FindTextBlockByText(Harness.PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("PreviewTitle")));
	const TSharedPtr<STextBlock> PreviewTitle = FindTextBlockByText(Harness.PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("UMGPreviewFit")));
	const TSharedPtr<STextBlock> JsonTitle = FindTextBlockByText(Harness.PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("JsonEditor")));
	if (!TestTrue(TEXT("Diagnostics have a scroll container"), Diagnostics.IsValid())
		|| !TestTrue(TEXT("Font tool remains present"), FontTool.IsValid())
		|| !TestTrue(TEXT("Generate remains present"), Generate.IsValid())
		|| !TestTrue(TEXT("Cancel remains present"), Cancel.IsValid())
		|| !TestTrue(TEXT("JSON format action remains present"), Format.IsValid())
		|| !TestTrue(TEXT("Preview scale action remains present"), Scale.IsValid())
		|| !TestTrue(TEXT("Main title remains present"), MainTitle.IsValid())
		|| !TestTrue(TEXT("Preview title remains present"), PreviewTitle.IsValid())
		|| !TestTrue(TEXT("JSON title remains present"), JsonTitle.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("All conversion warnings are inside the scroll container"), WidgetTreeContainsTextFragment(Diagnostics.ToSharedRef(), Warnings.Last()));
	TestTrue(TEXT("All image failures are inside the scroll container"), WidgetTreeContainsTextFragment(Diagnostics.ToSharedRef(), ImageResult.Diagnostics.Last().Message));
	TestTrue(TEXT("Image summary retains the complete failure count"),
		WidgetTreeContainsTextFragment(Harness.PreviewWindow.ToSharedRef(), TEXT("图片解析失败：48 张")));
	for (const TCHAR* Key : { TEXT("ImageDiagnosticItem"), TEXT("GenerationRecoveryStates"), TEXT("GenerationAssetPath"), TEXT("PreviewWidgetMissing") })
	{
		const FString Template = PreviewWindowLabel(Key);
		TestTrue(FString::Printf(TEXT("%s contains an expanded newline"), Key), Template.Contains(TEXT("\n")));
		TestFalse(FString::Printf(TEXT("%s contains no residual backslash"), Key), Template.Contains(TEXT("\\")));
	}
	TSharedPtr<FSlateVirtualUserHandle> VirtualUserHandle;
	int32 VirtualUserIndex = 0;
	do
	{
		VirtualUserHandle = FSlateApplication::Get().FindOrCreateVirtualUser(VirtualUserIndex++);
	}
	while (!VirtualUserHandle.IsUnique());
	const int32 UserIndex = VirtualUserHandle->GetUserIndex();
	const TSharedPtr<FSlateUser> VirtualUser = FSlateApplication::Get().GetUser(UserIndex);
	if (!TestTrue(TEXT("Pointer routing owns an independent virtual Slate user"), VirtualUser.IsValid()
		&& VirtualUser->IsVirtualUser() && UserIndex != FSlateApplication::Get().GetCursorUser()->GetUserIndex()))
	{
		return false;
	}
	const auto LocateControl = [this, UserIndex](FPreviewWindowHarness& TargetHarness, const TSharedRef<SWidget>& Control)
	{
		bool bInteractive = TestTrue(TEXT("Interactive control is enabled and visible"), Control->IsEnabled() && Control->GetVisibility().IsVisible());
		const FGeometry& Geometry = Control->GetCachedGeometry();
		const FVector2D Center = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
		const TArray<TSharedRef<SWindow>> Windows{ TargetHarness.Window.ToSharedRef() };
		FWidgetPath Path = FSlateApplication::Get().LocateWindowUnderMouse(Center, Windows, false, UserIndex);
		bInteractive &= TestTrue(TEXT("Native hit path reaches the actual control"), Path.IsValid() && Path.ContainsWidget(&Control.Get()));
		if (Path.IsValid())
		{
			bInteractive &= TestTrue(TEXT("Native hit path belongs only to the target test window"), Path.GetWindow() == TargetHarness.Window.ToSharedRef());
			for (int32 WidgetIndex = 0; WidgetIndex < Path.Widgets.Num(); ++WidgetIndex)
			{
				const TSharedRef<SWidget>& Ancestor = Path.Widgets[WidgetIndex].Widget;
				bInteractive &= TestTrue(TEXT("Hit path ancestors are enabled and visible"), Ancestor->IsEnabled() && Ancestor->GetVisibility().IsVisible());
			}
		}
		return bInteractive ? Path : FWidgetPath();
	};
	const auto ClickControl = [this, &LocateControl, &VirtualUser, UserIndex](FPreviewWindowHarness& TargetHarness, const TSharedRef<SWidget>& Control)
	{
		const FWidgetPath Path = LocateControl(TargetHarness, Control);
		if (!Path.IsValid())
		{
			return false;
		}
		const FGeometry& Geometry = Control->GetCachedGeometry();
		const FVector2D Center = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
		const TSet<FKey> PressedButtons{ EKeys::LeftMouseButton };
		const TSet<FKey> ReleasedButtons;
		const FPointerEvent Move(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, ReleasedButtons, FKey(), 0.0f, FModifierKeysState());
		const FPointerEvent Press(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, PressedButtons, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
		const FPointerEvent Release(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, ReleasedButtons, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
		FSlateApplication::Get().RoutePointerMoveEvent(Path, Move, true);
		const bool bDownHandled = FSlateApplication::Get().RoutePointerDownEvent(Path, Press).IsEventHandled();
		const FWidgetPath CapturePath = VirtualUser->GetCaptorPath(FSlateApplication::CursorPointerIndex);
		const bool bTargetCapture = TestTrue(TEXT("Pointer capture is confined to the target test window and control"),
			CapturePath.IsValid() && CapturePath.GetWindow() == TargetHarness.Window.ToSharedRef() && CapturePath.ContainsWidget(&Control.Get()));
		const bool bUpHandled = FSlateApplication::Get().RoutePointerUpEvent(Path, Release).IsEventHandled();
		const bool bCaptureReleased = TestFalse(TEXT("Virtual pointer capture is released after pointer up"), VirtualUser->HasCapture(FSlateApplication::CursorPointerIndex));
		TargetHarness.PumpSlate(6);
		return bDownHandled && bUpHandled && bTargetCapture && bCaptureReleased;
	};
	for (const FVector2D Size : { FVector2D(1200, 720), FVector2D(900, 620) })
	{
		// Resize takes desktop pixels; the layout contract is stated in Slate units.
		Harness.Window->Resize(Size * Harness.Window->GetDPIScaleFactor());
		Harness.PumpSlate(6);
		TestTrue(TEXT("Diagnostics height stays bounded"), Diagnostics->GetCachedGeometry().GetLocalSize().Y > 0
			&& Diagnostics->GetCachedGeometry().GetLocalSize().Y <= 160.0f);
		TestTrue(TEXT("Preview retains usable height"), FDesign2UMGPreviewWindowTestAccess::GetPreviewContainer(*Harness.PreviewWindow)->GetCachedGeometry().GetLocalSize().Y >= 120.0f);
		TestTrue(TEXT("JSON retains usable height"), FDesign2UMGPreviewWindowTestAccess::GetJsonEditor(*Harness.PreviewWindow)->GetCachedGeometry().GetLocalSize().Y >= 120.0f);
		const FGeometry& WindowGeometry = Harness.PreviewWindow->GetCachedGeometry();
		const auto BoundsInWindow = [&WindowGeometry](const TSharedRef<SWidget>& Widget)
		{
			const FGeometry& Geometry = Widget->GetCachedGeometry();
			const FVector2D TopLeft = WindowGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D::ZeroVector));
			const FVector2D BottomRight = WindowGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize()));
			return FSlateRect(TopLeft.X, TopLeft.Y, BottomRight.X, BottomRight.Y);
		};
		for (const TSharedRef<SWidget>& Control : {
			StaticCastSharedRef<SWidget>(FontTool.ToSharedRef()),
			StaticCastSharedRef<SWidget>(FDesign2UMGPreviewWindowTestAccess::GetOutputPathBox(*Harness.PreviewWindow).ToSharedRef()),
			StaticCastSharedRef<SWidget>(Generate.ToSharedRef()), StaticCastSharedRef<SWidget>(Cancel.ToSharedRef()),
			StaticCastSharedRef<SWidget>(Format.ToSharedRef()), StaticCastSharedRef<SWidget>(Scale.ToSharedRef()),
			StaticCastSharedRef<SWidget>(MainTitle.ToSharedRef()), StaticCastSharedRef<SWidget>(PreviewTitle.ToSharedRef()),
			StaticCastSharedRef<SWidget>(JsonTitle.ToSharedRef()) })
		{
			const FGeometry& Geometry = Control->GetCachedGeometry();
			const FVector2D TopLeft = WindowGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D::ZeroVector));
			const FVector2D BottomRight = WindowGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize()));
			TestTrue(TEXT("Tools and bottom controls remain inside the window with positive area"),
				Geometry.GetLocalSize().GetMin() > 0 && TopLeft.GetMin() >= 0
				&& BottomRight.X <= WindowGeometry.GetLocalSize().X + 1
				&& BottomRight.Y <= WindowGeometry.GetLocalSize().Y + 1);
		}
		const FSlateRect MainBounds = BoundsInWindow(MainTitle.ToSharedRef());
		const FSlateRect PreviewBounds = BoundsInWindow(PreviewTitle.ToSharedRef());
		const FSlateRect JsonBounds = BoundsInWindow(JsonTitle.ToSharedRef());
		TestTrue(TEXT("Main title stays above both panel headers"), MainBounds.Bottom <= PreviewBounds.Top && MainBounds.Bottom <= JsonBounds.Top);
		TestTrue(TEXT("Preview title does not overlap the scale button"), PreviewBounds.Right <= BoundsInWindow(Scale.ToSharedRef()).Left);
		TestTrue(TEXT("JSON title does not overlap the format button"), JsonBounds.Right <= BoundsInWindow(Format.ToSharedRef()).Left);
		for (const TSharedRef<SWidget>& Control : {
			StaticCastSharedRef<SWidget>(FDesign2UMGPreviewWindowTestAccess::GetOutputPathBox(*Harness.PreviewWindow).ToSharedRef()),
			StaticCastSharedRef<SWidget>(Generate.ToSharedRef()), StaticCastSharedRef<SWidget>(Cancel.ToSharedRef()),
			StaticCastSharedRef<SWidget>(Format.ToSharedRef()), StaticCastSharedRef<SWidget>(Scale.ToSharedRef()) })
		{
			LocateControl(Harness, Control);
		}
		TestFalse(TEXT("Output path remains editable"), FDesign2UMGPreviewWindowTestAccess::GetOutputPathBox(*Harness.PreviewWindow)->IsReadOnly());
		TestTrue(TEXT("Long diagnostics require scrolling"), Diagnostics->GetScrollOffsetOfEnd() > 0);
		Diagnostics->ScrollToEnd();
		Harness.PumpSlate(6);
		TestTrue(TEXT("Last diagnostic can be reached by scrolling"),
			FMath::IsNearlyEqual(Diagnostics->GetScrollOffset(), Diagnostics->GetScrollOffsetOfEnd(), 1.0f));
	}
	TestTrue(TEXT("Many warnings do not invalidate the approved snapshot"), Harness.CanConfirm());
	const bool bCancelClicked = ClickControl(Harness, Cancel.ToSharedRef());
	TestTrue(TEXT("Cancel still dispatches after scrolling diagnostics"), bCancelClicked && Harness.CancelDelegateSpy->GetInvocationCount() == 1);
	TestEqual(TEXT("Cancellation does not dispatch generation"), Harness.ConfirmDelegateSpy->GetInvocationCount(), 0);
	TestFalse(TEXT("Native cancellation closes the warning window"), Harness.IsWindowAttached());

	FPreviewWindowHarness ConfirmationHarness;
	if (!TestTrue(TEXT("Independent many-warning confirmation window opens"), ConfirmationHarness.Open(
		MakePreviewHarnessJson(TEXT("Many warnings confirmation")), TEXT("/Game/Test"), Warnings)))
	{
		return false;
	}
	TestTrue(TEXT("Confirmation window still contains all conversion warnings"), WidgetTreeContainsTextFragment(ConfirmationHarness.PreviewWindow.ToSharedRef(), Warnings.Last()));
	const TSharedPtr<SEditableTextBox> OutputPath = FDesign2UMGPreviewWindowTestAccess::GetOutputPathBox(*ConfirmationHarness.PreviewWindow);
	TestFalse(TEXT("Confirmation output path is editable"), OutputPath->IsReadOnly());
	TestTrue(TEXT("Output path accepts native pointer routing"), ClickControl(ConfirmationHarness, OutputPath.ToSharedRef()));
	const FString ConfirmedPath = TEXT("/Game/Design2UMG/_TestOutput/ManyWarningsConfirmed");
	ConfirmationHarness.SetOutputPath(ConfirmedPath);
	const TSharedPtr<SButton> ConfirmButton = FindButtonByLabel(ConfirmationHarness.PreviewWindow.ToSharedRef(), PreviewWindowLabel(TEXT("Generate")));
	if (!TestTrue(TEXT("Independent confirmation action exists"), ConfirmButton.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("Many-warning confirmation accepts native pointer routing"), ClickControl(ConfirmationHarness, ConfirmButton.ToSharedRef()));
	TestEqual(TEXT("Many-warning confirmation dispatches the existing delegate once"), ConfirmationHarness.ConfirmDelegateSpy->GetInvocationCount(), 1);
	TestEqual(TEXT("Many-warning confirmation forwards the edited output path"), ConfirmationHarness.ConfirmDelegateSpy->GetLastConfirmedOutputPath(), ConfirmedPath);
	TestTrue(TEXT("Many-warning confirmation forwards an approved snapshot"), ConfirmationHarness.ConfirmDelegateSpy->GetLastApprovedRequestId() > 0);
	TestEqual(TEXT("Confirmation does not dispatch cancellation"), ConfirmationHarness.CancelDelegateSpy->GetInvocationCount(), 0);
	TestFalse(TEXT("Native confirmation closes its window"), ConfirmationHarness.IsWindowAttached());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewWindowHarnessRefreshConfirmTest,
	"Design2UMG.PreviewWindow.HarnessRefreshConfirm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewWindowHarnessRefreshConfirmTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}

	FPreviewWindowHarness Harness;
	TestTrue(TEXT("Preview harness opens window"),
		Harness.Open(MakePreviewHarnessJson(TEXT("Original Label")), TEXT("/Game/Design2UMG/_TestOutput/PreviewHarness")));
	if (!Harness.PreviewWindow.IsValid())
	{
		return false;
	}

	Harness.ResetObservedTelemetry();
	const FString EditedJson = MakePreviewHarnessJson(TEXT("Edited Label"));
	const FString LatestEditedJson = MakePreviewHarnessJson(TEXT("Edited Label Latest"));
	TestTrue(TEXT("First refresh request rebuilds preview"), Harness.RequestRefresh(EditedJson));
	TestTrue(TEXT("Refresh path does not dispatch generation"), Harness.ConfirmDelegateSpy->GetInvocationCount() == 0);
	TestTrue(TEXT("Second refresh request rebuilds preview"), Harness.RequestRefresh(LatestEditedJson));
	Harness.SetOutputPath(TEXT("/Game/Design2UMG/_TestOutput/PreviewHarnessConfirmed"));

	TestTrue(TEXT("Confirm request dispatches preview callback"), Harness.Confirm());
	TestEqual(TEXT("Analysis invocation count"), Harness.AnalyzerSpy->GetInvocationCount(), 2);
	TestEqual(TEXT("Preview materialization invocation count"), Harness.MaterializationSpy->GetInvocationCount(), 2);
	TestEqual(TEXT("Confirm delegate invocation count"), Harness.ConfirmDelegateSpy->GetInvocationCount(), 1);
	TestEqual(TEXT("Cancel delegate invocation count stays zero"), Harness.CancelDelegateSpy->GetInvocationCount(), 0);
	TestTrue(TEXT("Refresh uses injected analyzer seam"), Harness.AnalyzerSpy->DidObserveInjectedSeam());
	TestTrue(TEXT("Refresh uses injected materialization seam"), Harness.MaterializationSpy->DidObserveInjectedSeam());
	TestTrue(TEXT("Refresh adopts spy materialized widget"), Harness.PreviewWidgetMatchesSpyMaterialization());
	TestTrue(TEXT("Refresh proves preview refresh used injected seam"), Harness.DidObserveInjectedPreviewRefresh());
	TestEqual(TEXT("Refresh preview text comes from latest injected seam"), Harness.GetPreviewWidgetText(), TEXT("SpyMaterialized:SpyAnalyzed-2"));
	TestEqual(TEXT("Analyzer sees the latest edited json"), Harness.AnalyzerSpy->GetLastAnalyzedJson(), LatestEditedJson);
	TestEqual(TEXT("Analyzer reports the injected widget payload"),
		Harness.AnalyzerSpy->GetLastAnalyzedWidgetName(),
		FString(PreviewHarnessAnalyzerWidgetName));
	TestEqual(TEXT("Materialization receives the injected widget payload"),
		Harness.MaterializationSpy->GetLastMaterializedWidgetName(),
		FString(PreviewHarnessAnalyzerWidgetName));
	TestEqual(TEXT("Confirm forwards latest approved json"), Harness.ConfirmDelegateSpy->GetLastConfirmedJson(), LatestEditedJson);
	TestEqual(TEXT("Confirm forwards edited output path"),
		Harness.ConfirmDelegateSpy->GetLastConfirmedOutputPath(),
		TEXT("/Game/Design2UMG/_TestOutput/PreviewHarnessConfirmed"));
	TestEqual(TEXT("Confirm forwards latest approved request id"),
		Harness.ConfirmDelegateSpy->GetLastApprovedRequestId(),
		Harness.AnalyzerSpy->GetLastAnalyzedRequestId());
	TestEqual(TEXT("Confirm forwards latest approved widget payload"),
		Harness.ConfirmDelegateSpy->GetLastConfirmedWidgetName(),
		FString(PreviewHarnessAnalyzerWidgetName));
	TestEqual(TEXT("Materialization tracks latest request id"),
		Harness.MaterializationSpy->GetLastMaterializedRequestId(),
		Harness.AnalyzerSpy->GetLastAnalyzedRequestId());
	TestTrue(TEXT("Preview window close is observed after confirm"), Harness.Telemetry->bPreviewWindowClosed);
	TestFalse(TEXT("Preview window detaches after confirm"), Harness.IsWindowAttached());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewWindowInitialImageDiagnosticsReachApprovedSnapshotTest,
	"Design2UMG.PreviewWindow.InitialImageDiagnosticsReachApprovedSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewWindowInitialImageDiagnosticsReachApprovedSnapshotTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}

	FDesignImageImportResult InitialImageResult;
	FDesignImageDiagnostic SourceFailure;
	SourceFailure.Outcome = EDesignImageDiagnosticOutcome::Failed;
	SourceFailure.Category = EDesignImageDiagnosticCategory::UnresolvedReference;
	SourceFailure.Reason = TEXT("Source image reference was unresolved before preview");
	SourceFailure.Message = TEXT("Source image A failed before preview");
	InitialImageResult.AddDiagnostic(MoveTemp(SourceFailure));

	FPreviewWindowHarness Harness;
	TestTrue(TEXT("Preview harness opens with initial image diagnostics"),
		Harness.Open(
			MakePreviewHarnessJson(TEXT("Initial image failure")),
			TEXT("/Game/Design2UMG/_TestOutput/PreviewInitialImageFailure"),
			TArray<FString>{ TEXT("Unrelated conversion warning") },
			TOptional<FDesignPreviewMaterializationSettingsSnapshot>(),
			true,
			InitialImageResult));
	if (!Harness.PreviewWindow.IsValid())
	{
		return false;
	}

	TestTrue(TEXT("Initial image failure appears in the warning area"),
		WidgetTreeContainsTextFragment(Harness.PreviewWindow.ToSharedRef(), TEXT("Source image A failed before preview")));
	TestTrue(TEXT("Conversion warning remains separately visible"),
		WidgetTreeContainsTextFragment(Harness.PreviewWindow.ToSharedRef(), TEXT("Unrelated conversion warning")));

	FDesignImageImportResult CurrentImageResult;
	Harness.AnalyzerSpy->SetNextImageResult(CurrentImageResult);
	TestTrue(TEXT("Refresh with no image failures completes"),
		Harness.RequestRefresh(MakePreviewHarnessJson(TEXT("Image source changed"))));
	TestFalse(TEXT("Retired failure disappears from the actual window"),
		WidgetTreeContainsTextFragment(Harness.PreviewWindow.ToSharedRef(), TEXT("Source image A failed before preview")));
	TestTrue(TEXT("Conversion warning is unchanged after image refresh"),
		WidgetTreeContainsTextFragment(Harness.PreviewWindow.ToSharedRef(), TEXT("Unrelated conversion warning")));

	FDesignImageDiagnostic NewFailure;
	NewFailure.Outcome = EDesignImageDiagnosticOutcome::Failed;
	NewFailure.Category = EDesignImageDiagnosticCategory::Download;
	NewFailure.Message = TEXT("Source image B failed on refresh");
	CurrentImageResult.AddDiagnostic(MoveTemp(NewFailure));
	Harness.AnalyzerSpy->SetNextImageResult(CurrentImageResult);
	TestTrue(TEXT("Refresh with a new image failure completes"),
		Harness.RequestRefresh(MakePreviewHarnessJson(TEXT("New image failure"))));
	TestTrue(TEXT("Current failure appears in the actual window"),
		WidgetTreeContainsTextFragment(Harness.PreviewWindow.ToSharedRef(), TEXT("Source image B failed on refresh")));
	TestFalse(TEXT("Old failure remains absent"),
		WidgetTreeContainsTextFragment(Harness.PreviewWindow.ToSharedRef(), TEXT("Source image A failed before preview")));

	TestTrue(TEXT("Confirmed preview dispatches the approved snapshot"), Harness.Confirm());
	const FDesignImageImportResult& ConfirmedImageResult =
		Harness.ConfirmDelegateSpy->GetLastConfirmedImageResult();
	TestEqual(TEXT("The approved snapshot keeps the current source failure"), ConfirmedImageResult.FailedCount, 1);
	if (TestEqual(TEXT("The approved snapshot keeps one image diagnostic"),
		ConfirmedImageResult.Diagnostics.Num(), 1))
	{
		TestEqual(
			TEXT("The approved diagnostic remains a source-resolution failure"),
			static_cast<uint8>(ConfirmedImageResult.Diagnostics[0].Category),
			static_cast<uint8>(EDesignImageDiagnosticCategory::Download));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewWindowHarnessRefreshFailureBlocksConfirmTest,
	"Design2UMG.PreviewWindow.HarnessRefreshFailureBlocksConfirm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewWindowHarnessRefreshFailureBlocksConfirmTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}

	FPreviewWindowHarness Harness;
	TestTrue(TEXT("Preview harness opens window"),
		Harness.Open(MakePreviewHarnessJson(TEXT("Failure Original")), TEXT("/Game/Design2UMG/_TestOutput/PreviewHarnessFailure")));
	if (!Harness.PreviewWindow.IsValid())
	{
		return false;
	}

	Harness.ResetObservedTelemetry();
	Harness.AnalyzerSpy->FailNextRequest(TEXT("Injected refresh failure"));
	TestFalse(TEXT("Refresh failure does not build preview"), Harness.RequestRefresh(MakePreviewHarnessJson(TEXT("Failure Edited"))));
	TestEqual(TEXT("Analysis invocation count"), Harness.AnalyzerSpy->GetInvocationCount(), 1);
	TestEqual(TEXT("Preview materialization invocation count stays zero"), Harness.MaterializationSpy->GetInvocationCount(), 0);
	TestFalse(TEXT("Confirm remains blocked after failed refresh"), Harness.CanConfirm());
	TestFalse(TEXT("Confirm helper reports blocked confirm"), Harness.Confirm());
	TestEqual(TEXT("Confirm delegate invocation count stays zero"), Harness.ConfirmDelegateSpy->GetInvocationCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewWindowCarriesExplicitMaterializationSnapshotTest,
	"Design2UMG.PreviewWindow.CarriesExplicitMaterializationSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewWindowCarriesExplicitMaterializationSnapshotTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}

	FPreviewWindowHarness Harness;
	FDesign2UMGGenerationSettingsSnapshot GenerationSnapshot;
	FWidgetClassOverride& OverrideRule = GenerationSnapshot.ClassOverrides.AddDefaulted_GetRef();
	OverrideRule.NamePattern = TEXT("PreviewHarness");
	OverrideRule.WidgetClass = UTextBlock::StaticClass();
	FDesignPreviewMaterializationSettingsSnapshot MaterializationSnapshot;
	MaterializationSnapshot.ClassOverrides = GenerationSnapshot.ClassOverrides;

	TestTrue(TEXT("Preview harness opens window with explicit initial snapshot"),
		Harness.Open(
			MakePreviewHarnessJson(TEXT("Snapshot Original")),
			TEXT("/Game/Design2UMG/_TestOutput/PreviewHarnessSnapshot"),
			TArray<FString>(),
			MaterializationSnapshot,
			false));
	Harness.PumpSlate();

	if (!Harness.PreviewWindow.IsValid())
	{
		return false;
	}

	TestTrue(TEXT("Initial preview materialization receives explicit snapshot"),
		Harness.MaterializationSpy->SawMaterializationSettingsSnapshot());
	TestEqual(TEXT("Initial preview materialization keeps override count"),
		Harness.MaterializationSpy->GetLastMaterializationSnapshotOverrideCount(),
		1);

	Harness.ResetObservedTelemetry();
	TestTrue(TEXT("Refresh after scope reuse still rebuilds preview"),
		Harness.RequestRefresh(MakePreviewHarnessJson(TEXT("Snapshot Edited"))));
	TestTrue(TEXT("Refresh keeps explicit materialization snapshot after scope exits"),
		Harness.MaterializationSpy->SawMaterializationSettingsSnapshot());
	TestEqual(TEXT("Refresh keeps the captured override count"),
		Harness.MaterializationSpy->GetLastMaterializationSnapshotOverrideCount(),
		1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewWindowPipelineCarriesExplicitMaterializationSnapshotTest,
	"Design2UMG.PreviewWindow.PipelineCarriesExplicitMaterializationSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewWindowPipelineCarriesExplicitMaterializationSnapshotTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}

	auto PumpSlate = []()
	{
		for (int32 TickIndex = 0; TickIndex < 2; ++TickIndex)
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FSlateApplication::Get().PumpMessages();
			FSlateApplication::Get().Tick(ESlateTickType::All);
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		}
	};

	FDesign2UMGImportSettingsSnapshot SettingsSnapshot;
	FWidgetClassOverride& OverrideRule = SettingsSnapshot.GenerationSettings.ClassOverrides.AddDefaulted_GetRef();
	OverrideRule.NamePattern = TEXT("PipelinePreviewHarness");
	OverrideRule.WidgetClass = UTextBlock::StaticClass();

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_PipelinePreviewHarness");
	Data.RootWidget.Type = TEXT("TextBlock");
	Data.RootWidget.Id = TEXT("pipeline_preview_root");
	Data.RootWidget.Name = TEXT("PipelinePreviewHarness_Content");
	Data.RootWidget.Bounds = { 0.0f, 0.0f, 180.0f, 40.0f };
	Data.RootWidget.Text.Text = TEXT("Pipeline Preview");

	FDesignImportRequest Request;
	Request.OutputPath = TEXT("/Game/Design2UMG/_TestOutput/PipelinePreviewHarness");
	Request.PreviewWindowTitle = FText::FromString(TEXT("Pipeline Preview Harness"));
	Request.bUsePreview = true;
	Request.bResolveImages = false;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	Request.SettingsSnapshot = SettingsSnapshot;

	const FDesignImportExecutionResult Result = DesignImportPipeline::Execute(Data, Request);
	TestTrue(TEXT("Pipeline preview opens a preview window"), Result.bPreviewOpened);
	if (!Result.bPreviewOpened)
	{
		return false;
	}

	PumpSlate();

	const TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	TestTrue(TEXT("Active top-level window exists after pipeline preview open"), ActiveWindow.IsValid());
	if (!ActiveWindow.IsValid())
	{
		return false;
	}

	TSharedRef<SDesign2UMGPreviewWindow> PreviewWindow = GetPreviewWindowContent(*ActiveWindow);
	TestTrue(TEXT("Pipeline preview stores explicit materialization snapshot on first refresh"),
		FDesign2UMGPreviewWindowTestAccess::LatestApprovedSnapshotHasMaterializationSettings(PreviewWindow.Get()));
	TestEqual(TEXT("Pipeline preview keeps override count through dialog seam"),
		FDesign2UMGPreviewWindowTestAccess::GetLatestApprovedSnapshotMaterializationOverrideCount(PreviewWindow.Get()),
		1);

	const TSharedPtr<SWindow> PreviewSWindow =
		FSlateApplication::Get().FindWidgetWindow(PreviewWindow);
	TestTrue(TEXT("Pipeline preview resolves its owning window for cleanup"), PreviewSWindow.IsValid());
	if (!PreviewSWindow.IsValid())
	{
		return false;
	}

	FSlateApplication::Get().DestroyWindowImmediately(PreviewSWindow.ToSharedRef());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewWindowBudgetFailureMessageTest,
	"Design2UMG.PreviewWindow.BudgetFailureMessage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewWindowBudgetFailureMessageTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddError(TEXT("Slate is required to verify the preview failure message"));
		return false;
	}
	FPreviewWindowHarness Harness;
	if (!TestTrue(TEXT("Preview opens"), Harness.Open(MakePreviewHarnessJson(TEXT("Before limit")),
		TEXT("/Game/Design2UMG/_TestOutput/PreviewBudgetMessage"))))
	{
		return false;
	}
	TestNotNull(TEXT("The initial preview has a widget to release"), Harness.MaterializationSpy->GetMaterializedPreviewWidget());
	const FString FailureDetails = TEXT("设计稿太大，导入时的图片资源占用超过处理上限。请拆分画板或减少大图片后再导入。\n")
		TEXT("Node 'LargePath': Total decoded image data exceeds the operation limit (536870912 bytes)");
	Harness.MaterializationSpy->ForcedErrorMessage = FailureDetails;
	Harness.RequestRefresh(MakePreviewHarnessJson(TEXT("Over limit")));
	const FString ExpectedMessage = FString::Printf(
		TEXT("Design2UMG：导入未完成。\n根节点：%s (), CanvasPanel, 420 × 220\n原因：%s"),
		PreviewHarnessAnalyzerRootName, *FailureDetails);
	TestEqual(TEXT("The failure status matches the complete independent failure message"),
		FDesign2UMGPreviewWindowTestAccess::Status(*Harness.PreviewWindow), ExpectedMessage);
	TestTrue(TEXT("The complete failure status remains visible after the dialog closes"),
		WidgetTreeContainsText(FDesign2UMGPreviewWindowTestAccess::GetPreviewContainer(*Harness.PreviewWindow).ToSharedRef(), ExpectedMessage));
	TestTrue(TEXT("The visible failure preserves the complete budget reason and actionable explanation"),
		WidgetTreeContainsTextFragment(FDesign2UMGPreviewWindowTestAccess::GetPreviewContainer(*Harness.PreviewWindow).ToSharedRef(), FailureDetails));
	TestFalse(TEXT("Failed preview cannot be confirmed"),
		FDesign2UMGPreviewWindowTestAccess::CanConfirmLatestSnapshot(*Harness.PreviewWindow));
	TestNull(TEXT("Failed preview releases the old widget"), Harness.MaterializationSpy->GetMaterializedPreviewWidget());
	Harness.MaterializationSpy->ForcedErrorMessage.Reset();
	Harness.RequestRefresh(MakePreviewHarnessJson(TEXT("Within limit")));
	TestTrue(TEXT("Corrected preview can be confirmed"),
		FDesign2UMGPreviewWindowTestAccess::CanConfirmLatestSnapshot(*Harness.PreviewWindow));
	TestNotNull(TEXT("Corrected preview restores a materialized widget"), Harness.MaterializationSpy->GetMaterializedPreviewWidget());
	TestFalse(TEXT("Corrected preview removes the failure message"),
		WidgetTreeContainsText(FDesign2UMGPreviewWindowTestAccess::GetPreviewContainer(*Harness.PreviewWindow).ToSharedRef(), ExpectedMessage));
	TestFalse(TEXT("Corrected preview removes the budget reason and actionable explanation"),
		WidgetTreeContainsTextFragment(FDesign2UMGPreviewWindowTestAccess::GetPreviewContainer(*Harness.PreviewWindow).ToSharedRef(), FailureDetails));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewWindowHarnessCancelTest,
	"Design2UMG.PreviewWindow.HarnessCancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewWindowHarnessCancelTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}

	FPreviewWindowHarness Harness;
	TestTrue(TEXT("Preview harness opens window"),
		Harness.Open(MakePreviewHarnessJson(TEXT("Cancel Original")), TEXT("/Game/Design2UMG/_TestOutput/PreviewHarnessCancel")));
	if (!Harness.PreviewWindow.IsValid())
	{
		return false;
	}

	Harness.ResetObservedTelemetry();
	TestTrue(TEXT("Cancel flow can refresh before cancellation"),
		Harness.RequestRefresh(MakePreviewHarnessJson(TEXT("Cancel Edited"))));
	TestTrue(TEXT("Cancel request dispatches preview cancel callback"), Harness.Cancel());
	TestEqual(TEXT("Analysis invocation count"), Harness.AnalyzerSpy->GetInvocationCount(), 1);
	TestEqual(TEXT("Preview materialization invocation count"), Harness.MaterializationSpy->GetInvocationCount(), 1);
	TestEqual(TEXT("Confirm delegate invocation count stays zero"), Harness.ConfirmDelegateSpy->GetInvocationCount(), 0);
	TestEqual(TEXT("Cancel delegate invocation count"), Harness.CancelDelegateSpy->GetInvocationCount(), 1);
	TestTrue(TEXT("Cancel path still used injected analyzer seam"), Harness.AnalyzerSpy->DidObserveInjectedSeam());
	TestTrue(TEXT("Cancel path still used injected materialization seam"), Harness.MaterializationSpy->DidObserveInjectedSeam());
	TestTrue(TEXT("Cancel path adopts spy materialized widget"), Harness.PreviewWidgetMatchesSpyMaterialization());
	TestTrue(TEXT("Cancel refresh proves preview refresh used injected seam"), Harness.DidObserveInjectedPreviewRefresh());
	TestEqual(TEXT("Cancel refresh preview text comes from injected seam"), Harness.GetPreviewWidgetText(), TEXT("SpyMaterialized:SpyAnalyzed-1"));
	TestTrue(TEXT("Preview UI remained open for interactive flow"), Harness.Telemetry->bPreviewUiOpened);
	TestTrue(TEXT("Preview window close is observed after cancel"), Harness.Telemetry->bPreviewWindowClosed);
	TestFalse(TEXT("Preview window detaches after cancel"), Harness.IsWindowAttached());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewGenerationFailureKeepsWindowTest,
	"Design2UMG.PreviewWindow.GenerationFailureKeepsWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewGenerationFailureKeepsWindowTest::RunTest(const FString& Parameters)
{
	for (const EDesign2UMGCommitStatus Status : {
		EDesign2UMGCommitStatus::NotCommitted, EDesign2UMGCommitStatus::Conflict,
		EDesign2UMGCommitStatus::CommittedWithMetadataFailure, EDesign2UMGCommitStatus::RollbackIncomplete,
		EDesign2UMGCommitStatus::Succeeded })
	{
		FPreviewWindowHarness Harness;
		const FString Json = MakePreviewHarnessJson(TEXT("Keep my edits"));
		if (!TestTrue(TEXT("Open preview"), Harness.Open(Json, TEXT("/Game/D2U_PreviewRetry")))) return false;
		Harness.ConfirmDelegateSpy->Result.bSucceeded = false;
		Harness.ConfirmDelegateSpy->Result.Commit.Status = Status;
		Harness.ConfirmDelegateSpy->Result.ErrorMessage = TEXT("Requested operation failed");
		Harness.ConfirmDelegateSpy->Result.Commit.AssetPath = TEXT("/Game/D2U_PreviewRetry/WBP_Result");
		TestTrue(TEXT("Confirmation executes"), Harness.Confirm());
		TestTrue(TEXT("Failure keeps the window attached"), Harness.IsWindowAttached());
		const FString StatusText = FDesign2UMGPreviewWindowTestAccess::Status(*Harness.PreviewWindow);
		TestTrue(TEXT("Failure reason is visible"), StatusText.Contains(TEXT("Requested operation failed")));
		TestTrue(TEXT("Known asset path is visible"), StatusText.Contains(TEXT("/Game/D2U_PreviewRetry/WBP_Result")));
		if (Status == EDesign2UMGCommitStatus::RollbackIncomplete)
			TestTrue(TEXT("Recovery exposes memory state"), StatusText.Contains(TEXT("内存状态：未更改")));
		if (Status == EDesign2UMGCommitStatus::CommittedWithMetadataFailure)
			TestTrue(TEXT("Partial commit is not reported as no asset"), StatusText.Contains(TEXT("资源已保存")));
		TestTrue(TEXT("Failure preserves approved snapshot"), Harness.CanConfirm());
		TestEqual(TEXT("Failure preserves edited JSON"), Harness.ConfirmDelegateSpy->GetLastConfirmedJson(), Json);
		if (Harness.IsWindowAttached())
		{
			Harness.SetOutputPath(TEXT("/Game/D2U_PreviewRetryNewPath"));
			Harness.ConfirmDelegateSpy->Result.bSucceeded = true;
			Harness.ConfirmDelegateSpy->Result.Commit.Status = EDesign2UMGCommitStatus::Succeeded;
			TestTrue(TEXT("Retry executes"), Harness.Confirm());
			TestEqual(TEXT("Retry uses changed path"), Harness.ConfirmDelegateSpy->GetLastConfirmedOutputPath(), FString(TEXT("/Game/D2U_PreviewRetryNewPath")));
			TestFalse(TEXT("Successful retry closes"), Harness.IsWindowAttached());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewUnboundConfirmationTest,
    "Design2UMG.PreviewWindow.UnboundConfirmation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewUnboundConfirmationTest::RunTest(const FString& Parameters)
{
    FPreviewWindowHarness Harness;
    if (!Harness.Open(MakePreviewHarnessJson(TEXT("Unbound")), TEXT("/Game/D2U_Unbound"))) return false;
    FDesign2UMGPreviewWindowTestAccess::UnbindConfirmation(*Harness.PreviewWindow);
    FDesign2UMGPreviewWindowTestAccess::Confirm(*Harness.PreviewWindow);
    Harness.PumpSlate();
    TestTrue(TEXT("Unbound callback leaves window open"), Harness.IsWindowAttached());
    TestTrue(TEXT("Unbound callback leaves snapshot ready"), Harness.CanConfirm());
    TestTrue(TEXT("No operation is reported"), FDesign2UMGPreviewWindowTestAccess::Status(*Harness.PreviewWindow).Contains(TEXT("未开始生成")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewPipelineGenerationRetryTest,
    "Design2UMG.PreviewWindow.PipelineGenerationRetry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewPipelineGenerationRetryTest::RunTest(const FString& Parameters)
{
    FPreviewWindowHarness Pump;
    FUMGWidgetData Data;
    Data.WidgetName = TEXT("WBP_PreviewPipelineRetry");
    Data.RootWidget.Type = TEXT("TextBlock");
    Data.RootWidget.Id = TEXT("root");
    Data.RootWidget.Name = TEXT("Content");
    Data.RootWidget.Bounds = {0, 0, 180, 40};
    Data.RootWidget.Text.Text = TEXT("Keep this text");
    FDesignImportRequest Request;
    Request.OutputPath = TEXT("/Game/Design2UMG/_TestOutput/PreviewPipelineRetry_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Request.bResolveImages = false;
    Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
    Request.SettingsSnapshot = FDesign2UMGImportSettingsSnapshot();
    const auto Opened = DesignImportPipeline::Execute(Data, Request);
    if (!TestTrue(TEXT("Real pipeline opens preview"), Opened.bPreviewOpened)) return false;
    Pump.PumpSlate(6);
    TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow();
    if (!TestTrue(TEXT("Pipeline window exists"), Window.IsValid())) return false;
    auto Preview = GetPreviewWindowContent(*Window);
    bool bClosed = false;
    Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([&bClosed](const TSharedRef<SWindow>&) { bClosed = true; }));
    const FString RetryPath = Request.OutputPath + TEXT("_Retry");
    const FString PackageName = Request.OutputPath / Data.WidgetName;
    const FString ManifestPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(PackageName);
    ON_SCOPE_EXIT
    {
        // Only the unique test-created directory is removed, never an existing asset file.
        IFileManager::Get().DeleteDirectory(*ManifestPath, false, false);
        if (!bClosed) FSlateApplication::Get().DestroyWindowImmediately(Window.ToSharedRef());
        Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Request.OutputPath);
        Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(RetryPath);
    };
    if (!TestTrue(TEXT("Snapshot ready"), FDesign2UMGPreviewWindowTestAccess::CanConfirmLatestSnapshot(*Preview))) return false;
    // A directory where the manifest file must go provokes a real write failure.
    if (!TestTrue(TEXT("Create metadata obstruction"), IFileManager::Get().MakeDirectory(*ManifestPath, true))) return false;
    AddExpectedError(TEXT("GenerationManifest: failed to finalize manifest"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("LogFileManager: Error moving file"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("DesignImportGeneration: Widget Blueprint compile or save failed"), EAutomationExpectedErrorFlags::Contains, 1);
    FDesign2UMGPreviewWindowTestAccess::Confirm(*Preview);
    Pump.PumpSlate(6);
    TestFalse(TEXT("Actual metadata failure retains window"), bClosed);
    TestTrue(TEXT("Actual partial commit is shown"), FDesign2UMGPreviewWindowTestAccess::Status(*Preview).Contains(TEXT("资源已保存")));
    TestNotNull(TEXT("Asset really exists despite metadata failure"), LoadObject<UWidgetBlueprint>(nullptr, *(PackageName + TEXT(".") + Data.WidgetName)));
    TestTrue(TEXT("Failure retains confirmed snapshot"), FDesign2UMGPreviewWindowTestAccess::CanConfirmLatestSnapshot(*Preview));
    IFileManager::Get().DeleteDirectory(*ManifestPath, false, false);
    FDesign2UMGPreviewWindowTestAccess::GetOutputPathBox(*Preview)->SetText(FText::FromString(RetryPath));
    FDesign2UMGPreviewWindowTestAccess::Confirm(*Preview);
    Pump.PumpSlate(6);
    const FString RetryManifest = Design2UMGGenerationManifest::BuildSidecarPathForAsset(RetryPath / Data.WidgetName);
    TestTrue(TEXT("Retry writes metadata at changed output path"), IFileManager::Get().FileExists(*RetryManifest));
    TestTrue(TEXT("Actual success closes window"), bClosed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewDegradedSuccessTest,
    "Design2UMG.PreviewWindow.DegradedSuccess", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewDegradedSuccessTest::RunTest(const FString& Parameters)
{
    FPreviewWindowHarness Harness;
    if (!Harness.Open(MakePreviewHarnessJson(TEXT("Degraded")), TEXT("/Game/D2U_Degraded"))) return false;
    FString ShownWarning;
    bool bWarningBeforeClose = false;
    auto PreviousHandler = FDesign2UMGNotification::RegisterWarningHandler(
        FDesign2UMGMessageNotificationHandler::CreateLambda([&](const FText& Message)
        {
            ShownWarning = Message.ToString();
            bWarningBeforeClose = Harness.IsWindowAttached();
        }));
    ON_SCOPE_EXIT { FDesign2UMGNotification::RegisterWarningHandler(MoveTemp(PreviousHandler)); };
    Harness.ConfirmDelegateSpy->Result.bSucceeded = true;
    Harness.ConfirmDelegateSpy->Result.Commit.Status = EDesign2UMGCommitStatus::SucceededWithDegradation;
    Harness.ConfirmDelegateSpy->Result.Warnings.Add(TEXT("Some vector effects were approximated."));
    TestTrue(TEXT("Degraded generation executes"), Harness.Confirm());
    TestTrue(TEXT("Actual warning reaches notification"), ShownWarning.Contains(TEXT("vector effects were approximated")));
    TestTrue(TEXT("Warning is emitted before closing"), bWarningBeforeClose);
    TestFalse(TEXT("Degraded success closes after notification"), Harness.IsWindowAttached());
    return true;
}

#endif // WITH_AUTOMATION_WORKER
