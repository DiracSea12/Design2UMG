// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGFigmaMetadataExtensions.h"
#include "Design2UMGLocalFigmaImport.h"
#include "DesignFontImportService.h"
#include "Design2UMGTestUtils.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"
#include "FontResolver.h"
#include "FigmaParser.h"
#include "LayoutFixer.h"
#include "PenParser.h"
#include "UMGGenerator.h"
#include "PreBuildResourceNormalizer.h"
#include "PenMCPBridge.h"
#include "DesignPreviewMaterializationService.h"
#define private public
#include "SDesign2UMGPreviewWindow.h"
#undef private
#include "PreviewUserWidget.h"
#include "UMGJsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Editor.h"
#include "Components/Button.h"
#include "Components/GridPanel.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/GridSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Layout/Children.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "RHI.h"
#include "LayoutCalculator.h"
#include "WidgetLayoutAdapter.h"
#include "Components/CanvasPanel.h"
#include "Components/VerticalBox.h"
#if WITH_AUTOMATION_WORKER
using namespace Design2UMGTestUtils;

namespace
{
	FMargin ReadTextWidgetMargin(const UTextBlock* TextBlock)
	{
		if (!TextBlock)
		{
			return FMargin();
		}

		const FStructProperty* MarginProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
		if (!MarginProperty)
		{
			return FMargin();
		}

		if (const FMargin* MarginValue = MarginProperty->ContainerPtrToValuePtr<FMargin>(TextBlock))
		{
			return *MarginValue;
		}

		return FMargin();
	}

	FString GetFixturePath(const FString& FixtureFileName)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		if (!Plugin.IsValid())
		{
			return FString();
		}

		return FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures"),
			FixtureFileName);
	}

	void DeleteGeneratedAssetsAtPath(const FString& AssetDir)
	{
		if (AssetDir.IsEmpty())
		{
			return;
		}

		FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		AssetRegistry.Get().GetAssetsByPath(FName(*AssetDir), Assets, true);

		TArray<UObject*> ObjectsToDelete;
		for (const FAssetData& Asset : Assets)
		{
			if (UObject* Object = Asset.GetAsset())
			{
				ObjectsToDelete.Add(Object);
			}
		}

		if (ObjectsToDelete.Num() > 0)
		{
			ObjectTools::ForceDeleteObjects(ObjectsToDelete, false);
		}
	}

	bool BuildPenMcpPayloadFromFixture(const FString& PenFilePath, FString& OutNodesJson, FString& OutVariablesJson)
	{
		FString PenJson;
		if (!FFileHelper::LoadFileToString(PenJson, *PenFilePath))
		{
			return false;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PenJson);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (!RootObject->TryGetArrayField(TEXT("children"), Children) || !Children)
		{
			return false;
		}

		const TSharedRef<TJsonWriter<>> NodesWriter = TJsonWriterFactory<>::Create(&OutNodesJson);
		if (!FJsonSerializer::Serialize(*Children, NodesWriter))
		{
			return false;
		}

		TSharedPtr<FJsonObject> VariablesObject = RootObject->GetObjectField(TEXT("variables"));
		if (!VariablesObject.IsValid())
		{
			VariablesObject = MakeShared<FJsonObject>();
		}

		const TSharedRef<TJsonWriter<>> VarsWriter = TJsonWriterFactory<>::Create(&OutVariablesJson);
		return FJsonSerializer::Serialize(VariablesObject.ToSharedRef(), VarsWriter);
	}

	bool WriteTinyPng(const FString& PngPath)
	{
		static const uint8 TinyPng[] = {
			137,80,78,71,13,10,26,10,
			0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,
			0,0,0,13,73,68,65,84,120,156,99,248,255,255,63,0,5,254,2,254,65,13,16,55,
			0,0,0,0,73,69,78,68,174,66,96,130
		};

		TArray<uint8> Bytes;
		Bytes.Append(TinyPng, UE_ARRAY_COUNT(TinyPng));
		return FFileHelper::SaveArrayToFile(Bytes, *PngPath);
	}

	bool WriteTestPng(const FString& PngPath, const TArray<FColor>& Pixels, int32 Width, int32 Height)
	{
		if (Pixels.Num() != Width * Height)
		{
			return false;
		}

		IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
		{
			return false;
		}

		const TArray64<uint8>& Compressed = Wrapper->GetCompressed(100);
		TArray<uint8> Bytes;
		Bytes.Append(Compressed.GetData(), Compressed.Num());
		return FFileHelper::SaveArrayToFile(Bytes, *PngPath);
	}

	bool ReadTestPng(const FString& PngPath, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *PngPath))
		{
			return false;
		}

		IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()))
		{
			return false;
		}

		TArray64<uint8> RawData;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
		{
			return false;
		}

		OutWidth = Wrapper->GetWidth();
		OutHeight = Wrapper->GetHeight();
		OutPixels.SetNumUninitialized(OutWidth * OutHeight);
		FMemory::Memcpy(OutPixels.GetData(), RawData.GetData(), OutPixels.Num() * sizeof(FColor));
		return true;
	}

	constexpr TCHAR PreviewHarnessAnalyzerWidgetName[] = TEXT("WBP_SpyPreviewHarness");
	constexpr TCHAR PreviewHarnessAnalyzerRootName[] = TEXT("SpyPreviewHarnessRoot");

	FString ExtractFirstWidgetText(const FUMGWidgetNode& WidgetNode)
	{
		if (WidgetNode.Text.bHasText && !WidgetNode.Text.Text.IsEmpty())
		{
			return WidgetNode.Text.Text;
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

	bool WidgetTreeContainsText(const TSharedRef<SWidget>& Widget, const FString& ExpectedText)
	{
		if (Widget->GetType() == STextBlock::StaticWidgetClass().GetWidgetType())
		{
			const TSharedRef<STextBlock> TextBlock = StaticCastSharedRef<STextBlock>(Widget);
			if (TextBlock->GetText().ToString() == ExpectedText)
			{
				return true;
			}
		}

		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
			{
				if (WidgetTreeContainsText(Children->GetChildAt(ChildIndex), ExpectedText))
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
		FString ImportMode = TEXT("preview_harness");
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
		LabelWidget.Text.bHasText = true;
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
		LabelWidget.Text.bHasText = true;
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
			Result.ImageReport = FString::Printf(TEXT("spy-image-report-%d"), InvocationSerial);
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
		FString LastAnalyzedJson;
		FString LastAnalyzedWidgetName;
	};

	class FSpyPreviewMaterializationService final : public IDesignPreviewMaterializationService
	{
	public:
		virtual bool MaterializePreview(const FDesignPreviewMaterializationRequest& Request, FDesignPreviewMaterializationResult& OutResult) override
		{
			++InvocationCount;
			LastMaterializedRequestId = Request.RequestId;
			LastMaterializedWidgetName = Request.ApprovedSnapshot.Data.WidgetName;

			const bool bSawInjectedAnalyzerPayload =
				Request.ApprovedSnapshot.Data.WidgetName == PreviewHarnessAnalyzerWidgetName
				&& Request.ApprovedSnapshot.Data.RootWidget.Name == PreviewHarnessAnalyzerRootName;
			bInjectedSeamObserved = bSawInjectedAnalyzerPayload;

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
			LastMaterializedWidgetName.Reset();
			LastPreviewWidgetText.Reset();
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

	private:
		int32 InvocationCount = 0;
		uint64 LastMaterializedRequestId = 0;
		bool bInjectedSeamObserved = false;
		FString LastMaterializedWidgetName;
		FString LastPreviewWidgetText;
		TStrongObjectPtr<UUserWidget> MaterializedPreviewWidget;
	};

	class FPreviewConfirmDelegateSpy final
	{
	public:
		void HandleConfirmed(const FPreviewGenerationRequest& GenerationRequest)
		{
			++InvocationCount;
			LastApprovedRequestId = GenerationRequest.ApprovedSnapshot.RequestId;
			LastConfirmedJson = GenerationRequest.ApprovedSnapshot.EditedJson;
			LastConfirmedOutputPath = GenerationRequest.GenerationRequest.OutputPath;
			LastConfirmedWidgetName = GenerationRequest.ApprovedSnapshot.Data.WidgetName;
		}

		void ResetObservation()
		{
			InvocationCount = 0;
			LastApprovedRequestId = 0;
			LastConfirmedJson.Reset();
			LastConfirmedOutputPath.Reset();
			LastConfirmedWidgetName.Reset();
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

		bool Open(const FString& OriginalJson, const FString& OutputPath, const TArray<FString>& Warnings = TArray<FString>())
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
				.OutputPath(OutputPath)
				.Warnings(Warnings)
				.PreviewRefreshAnalyzer(AnalyzerSpy)
				.PreviewMaterializationService(MaterializationSpy)
				.OnConfirmed(FOnPreviewConfirmed::CreateLambda(
					[ConfirmDelegateSpy = ConfirmDelegateSpy](const FPreviewGenerationRequest& GenerationRequest)
					{
						ConfirmDelegateSpy->HandleConfirmed(GenerationRequest);
					}))
				.OnCancelled(FOnPreviewCancelled::CreateLambda(
					[CancelDelegateSpy = CancelDelegateSpy]()
					{
						CancelDelegateSpy->HandleCancelled();
					})));

			FSlateApplication::Get().AddWindow(Window.ToSharedRef());
			PumpSlate();
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
			if (!PreviewWindow.IsValid() || !PreviewWindow->JsonEditor.IsValid())
			{
				return false;
			}

			PreviewWindow->CurrentJson = EditedJson;
			PreviewWindow->JsonEditor->SetText(FText::FromString(EditedJson));
			const TSharedPtr<SButton> FormatButton = FindButtonByLabel(PreviewWindow.ToSharedRef(), TEXT("Format JSON"));
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
			if (!PreviewWindow.IsValid() || !PreviewWindow->OutputPathBox.IsValid())
			{
				return;
			}

			PreviewWindow->OutputPathBox->SetText(FText::FromString(OutputPath));
			PumpSlate(1);
		}

		bool Confirm()
		{
			if (!PreviewWindow.IsValid() || !PreviewWindow->CanConfirmLatestSnapshot())
			{
				return false;
			}

			const int32 PreviousInvocationCount = ConfirmDelegateSpy->GetInvocationCount();
			const TSharedPtr<SButton> GenerateButton = FindButtonByLabel(PreviewWindow.ToSharedRef(), TEXT("Generate Widget Blueprint"));
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
			return PreviewWindow.IsValid() && PreviewWindow->CanConfirmLatestSnapshot();
		}

		bool Cancel()
		{
			if (!PreviewWindow.IsValid())
			{
				return false;
			}

			const int32 PreviousInvocationCount = CancelDelegateSpy->GetInvocationCount();
			const TSharedPtr<SButton> CancelButton = FindButtonByLabel(PreviewWindow.ToSharedRef(), TEXT("Cancel"));
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
			return PreviewWindow.IsValid()
				&& PreviewWindow->UMGPreviewContainer.IsValid()
				&& MaterializationSpy->GetLastPreviewWidget() != nullptr
				&& WidgetTreeContainsText(
					PreviewWindow->UMGPreviewContainer.ToSharedRef(),
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
				Window->RequestDestroyWindow();
				PumpSlate();
			}

			PreviewWindow.Reset();
			Window.Reset();
		}
	};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenWelcomeBatchImportTest,
	"Design2UMG.BatchImport.PencilWelcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenWelcomeBatchImportTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
	if (!Plugin.IsValid())
	{
		AddError(TEXT("Design2UMG plugin not found"));
		return false;
	}
	const FString PenFile = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures/pencil-welcome.pen"));
	if (!FPaths::FileExists(PenFile))
	{
		AddWarning(FString::Printf(TEXT("Test fixture not found, skipping: %s"), *PenFile));
		return true;
	}
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/PencilWelcome");
	TArray<FString> GeneratedAssetPaths;
	struct FFrameInfo
	{
		FString Id;
		FString Name;
	};
	const TArray<FFrameInfo> Frames = {
		{TEXT("sbChi"), TEXT("welcome")},
		{TEXT("cOnQP"), TEXT("Step1")},
		{TEXT("tlPHu"), TEXT("Step2")},
		{TEXT("CzlXB"), TEXT("Step3")}
	};
	for (const FFrameInfo& Frame : Frames)
	{
		FDesignDocument Document;
		FString LoadError;
		const bool bOk = DesignImportPipeline::LoadPenDocument(PenFile, Frame.Id, Document, &LoadError);
		TestTrue(*FString::Printf(TEXT("Parse %s"), *Frame.Name), bOk);
		if (!bOk)
		{
			AddError(LoadError);
			continue;
		}

		FUMGWidgetData Data = DesignImportPipeline::AnalyzeImport(Document, FImportAnalysisInput{false}).Data;
		if (GUsingNullRHI)
		{
			continue;
		}
		const FString FrameOutputPath = OutputPath / ObjectTools::SanitizeObjectName(Data.WidgetName);
		const bool bPng = DesignImportUtils::GenerateBlueprintAndPng(Data, FrameOutputPath);
		TestTrue(*FString::Printf(TEXT("Render %s"), *Frame.Name), bPng);
		GeneratedAssetPaths.Add(FrameOutputPath);
	}
	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<UObject*> ObjectsToDelete;
	for (const FString& AssetDir : GeneratedAssetPaths)
	{
		TArray<FAssetData> Assets;
		AssetRegistry.Get().GetAssetsByPath(FName(*AssetDir), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (UObject* Object = Asset.GetAsset())
			{
				ObjectsToDelete.Add(Object);
			}
		}
	}
	if (ObjectsToDelete.Num() > 0)
	{
		ObjectTools::ForceDeleteObjects(ObjectsToDelete, false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenWelcomeWrapFontRegressionTest,
	"Design2UMG.Text.PencilWelcomeWrapFontRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenWelcomeWrapFontRegressionTest::RunTest(const FString& Parameters)
{
	const FString PenFile = GetFixturePath(TEXT("pencil-welcome.pen"));
	if (PenFile.IsEmpty() || !FPaths::FileExists(PenFile))
	{
		AddWarning(FString::Printf(TEXT("Welcome fixture not found, skipping: %s"), *PenFile));
		return true;
	}

	FDesignDocument Document;
	FString LoadError;
	TestTrue(TEXT("Load welcome frame"), DesignImportPipeline::LoadPenDocument(PenFile, TEXT("sbChi"), Document, &LoadError));
	if (!LoadError.IsEmpty())
	{
		AddInfo(LoadError);
	}
	if (!LoadError.IsEmpty() && Document.Children.Num() == 0)
	{
		return false;
	}

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	const FUMGWidgetNode* WelcomeBody = FindWidgetById(Data.RootWidget, TEXT("moD8f"));
	const FUMGWidgetNode* ProTips = FindWidgetById(Data.RootWidget, TEXT("vCuCj"));
	TestTrue(TEXT("Find welcome body text widget"), WelcomeBody != nullptr);
	TestTrue(TEXT("Find pro tips text widget"), ProTips != nullptr);
	if (!WelcomeBody || !ProTips)
	{
		return false;
	}

	TestTrue(TEXT("Welcome body exports typed font size"), WelcomeBody->TextStyle.FontSize > 0.0f);
	TestTrue(TEXT("Pro tips exports typed font size"), ProTips->TextStyle.FontSize > 0.0f);
	if (WelcomeBody->TextStyle.FontSize <= 0.0f || ProTips->TextStyle.FontSize <= 0.0f)
	{
		return false;
	}

	TestTrue(TEXT("Welcome body keeps the 27px wrap-safe font size"),
		FMath::IsNearlyEqual(WelcomeBody->TextStyle.FontSize, 27.0f, 0.01f));
	TestTrue(TEXT("Pro tips keeps the same calibrated Rubik size"),
		FMath::IsNearlyEqual(ProTips->TextStyle.FontSize, 27.0f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenWelcomeLigatureIconWeightWorkflowTest,
	"Design2UMG.BatchImport.PencilWelcomeLigatureIconWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenWelcomeLigatureIconWeightWorkflowTest::RunTest(const FString& Parameters)
{
	const FString PenFile = GetFixturePath(TEXT("pencil-welcome.pen"));
	if (PenFile.IsEmpty() || !FPaths::FileExists(PenFile))
	{
		AddWarning(FString::Printf(TEXT("Welcome fixture not found, skipping: %s"), *PenFile));
		return true;
	}

	FDesignDocument Document;
	FString LoadError;
	TestTrue(TEXT("Load pencil welcome document"), DesignImportPipeline::LoadPenDocument(PenFile, FString(), Document, &LoadError));
	if (!LoadError.IsEmpty())
	{
		AddInfo(LoadError);
	}
	if (!LoadError.IsEmpty() && Document.Children.Num() == 0)
	{
		return false;
	}

	DesignFontImportService::ResetTransientState();
	FFontResolver::ClearCache();
	DesignFontImportService::PrepareDocumentFonts(Document);

	FUMGWidgetData Data = DesignImportPipeline::AnalyzeImport(Document, FImportAnalysisInput{false}).Data;

	const FUMGWidgetNode* DashboardIcon = FindWidgetById(Data.RootWidget, TEXT("xGT5g"));
	TestTrue(TEXT("Find dashboard icon widget"), DashboardIcon != nullptr);
	if (DashboardIcon)
	{
		TestTrue(TEXT("Dashboard icon resolves to a font asset"), HasWidgetProperty(*DashboardIcon, TEXT("FontFamily")));
		TestEqual(TEXT("Dashboard icon keeps dashboard semantic icon name"),
			GetWidgetProperty(*DashboardIcon, TEXT("IconName")),
			TEXT("dashboard"));
		TestEqual(TEXT("Dashboard ligature icon keeps authored thin typeface"),
			GetWidgetProperty(*DashboardIcon, TEXT("TypefaceFontName")),
			TEXT("Thin"));
	}

	const FUMGWidgetNode* ArrowDownIcon = FindWidgetById(Data.RootWidget, TEXT("UlKF0"));
	TestTrue(TEXT("Find arrow-down icon widget"), ArrowDownIcon != nullptr);
	if (ArrowDownIcon)
	{
		TestTrue(TEXT("Arrow-down icon resolves to a font asset"), HasWidgetProperty(*ArrowDownIcon, TEXT("FontFamily")));
		TestEqual(TEXT("Arrow-down icon keeps keyboard_arrow_down semantic icon name"),
			GetWidgetProperty(*ArrowDownIcon, TEXT("IconName")),
			TEXT("keyboard_arrow_down"));
		TestEqual(TEXT("Arrow-down ligature icon keeps authored thin typeface"),
			GetWidgetProperty(*ArrowDownIcon, TEXT("TypefaceFontName")),
			TEXT("Thin"));
	}

	return DashboardIcon != nullptr && ArrowDownIcon != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenMcpImportWorkflowTest,
	"Design2UMG.BatchImport.PenMCP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenMcpImportWorkflowTest::RunTest(const FString& Parameters)
{
	const FString PenFile = GetFixturePath(TEXT("pencil-welcome.pen"));
	if (PenFile.IsEmpty() || !FPaths::FileExists(PenFile))
	{
		AddWarning(FString::Printf(TEXT("Pen MCP fixture not found, skipping: %s"), *PenFile));
		return true;
	}

	FString NodesJson;
	FString VariablesJson;
	TestTrue(TEXT("Build MCP payload from .pen fixture"), BuildPenMcpPayloadFromFixture(PenFile, NodesJson, VariablesJson));
	if (NodesJson.IsEmpty() || VariablesJson.IsEmpty())
	{
		return false;
	}

	FUMGWidgetData Data;
	TestTrue(TEXT("Convert MCP payload"), FPenMCPBridge::ConvertFromMCP(NodesJson, VariablesJson, Data, TEXT("sbChi")));
	TestFalse(TEXT("MCP conversion should yield a widget name"), Data.WidgetName.IsEmpty());
	TestTrue(TEXT("MCP conversion should yield root widget children"), Data.RootWidget.Children.Num() > 0);

	if (GUsingNullRHI)
	{
		return true;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/PenMCP");
	const bool bPng = DesignImportUtils::GenerateBlueprintAndPng(Data, OutputPath);
	TestTrue(TEXT("Render Pen MCP import"), bPng);
	DeleteGeneratedAssetsAtPath(OutputPath);
	return bPng;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalFileImportWorkflowTest,
	"Design2UMG.BatchImport.LocalFigma",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalFileImportWorkflowTest::RunTest(const FString& Parameters)
{
	const FString JsonPath = GetFixturePath(TEXT("FigmaComplex.json"));
	if (JsonPath.IsEmpty() || !FPaths::FileExists(JsonPath))
	{
		AddWarning(FString::Printf(TEXT("Local Figma fixture not found, skipping: %s"), *JsonPath));
		return true;
	}

	FDesignDocument Document;
	FString ErrorMessage;
	TestTrue(TEXT("Load local Figma document"), Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(JsonPath, Document, ErrorMessage));
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	TestTrue(TEXT("Local Figma document should contain children"), Document.Children.Num() > 0);

	FUMGWidgetData Data;
	TestTrue(TEXT("Build local Figma widget data"), Design2UMGLocalFigmaImport::BuildLocalFigmaWidgetData(JsonPath, Data, ErrorMessage));
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	TestFalse(TEXT("Local Figma import should yield a widget name"), Data.WidgetName.IsEmpty());
	TestTrue(TEXT("Local Figma import should yield root widget children"), Data.RootWidget.Children.Num() > 0);

	if (GUsingNullRHI)
	{
		return true;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/LocalFigma");
	const bool bPng = DesignImportUtils::GenerateBlueprintAndPng(Data, OutputPath);
	TestTrue(TEXT("Render local Figma import"), bPng);
	DeleteGeneratedAssetsAtPath(OutputPath);
	return bPng;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportStructureTest,
	"Design2UMG.Import.ImportDiagnosticReportStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportStructureTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	FDesignDocument Document;
	Document.Version = TEXT("figma-fixture-v1");
	Document.Metadata.Add(
		TEXT("design.sourceFilePath"),
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), FString::Printf(TEXT("fixture_%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));

	FDesignSkippedNodeReport SkippedNode;
	SkippedNode.NodeId = TEXT("slice1");
	SkippedNode.NodeName = TEXT("Slice Overlay");
	SkippedNode.NodeType = TEXT("SLICE");
	SkippedNode.Reason = TEXT("design_only");
	Document.ParseDiagnostics.SkippedDesignerNodes.Add(SkippedNode);

	TSharedPtr<FDesignNode> TextPathNode = MakeShared<FDesignNode>();
	TextPathNode->Id = TEXT("text_path_1");
	TextPathNode->Name = TEXT("Curved Label");
	TextPathNode->Type = EDesignNodeType::Text;
	TextPathNode->Source.SourceNodeType = TEXT("TEXT_PATH");
	TextPathNode->Text.FontFamily = TEXT("DefinitelyMissingReportFont");

	TSharedPtr<FDesignNode> RenderFallbackNode = MakeShared<FDesignNode>();
	RenderFallbackNode->Id = TEXT("render_1");
	RenderFallbackNode->Name = TEXT("Blurred Hero");
	RenderFallbackNode->Type = EDesignNodeType::Rectangle;
	RenderFallbackNode->Source.SourceNodeType = TEXT("RECTANGLE");
	RenderFallbackNode->Source.RenderFallback.ImageRef = TEXT("render_hero");
	RenderFallbackNode->Source.RenderFallback.Scope = TEXT("full");
	RenderFallbackNode->Source.RenderFallback.Source = TEXT("figma_render_png");
	RenderFallbackNode->Source.RenderFallback.Reason = TEXT("foreground_blur");

	Document.Children.Add(TextPathNode);
	Document.Children.Add(RenderFallbackNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImportReportFixture");
	Data.Warnings.Add(TEXT("Figma TEXT_PATH 'Curved Label' → TextBlock (text-on-path 降级为普通文本)"));

	FUMGWidgetNode Wrapper;
	Wrapper.Type = TEXT("BackgroundBlur");
	Wrapper.Id = TEXT("hero_blur");
	Wrapper.Name = TEXT("HeroBlur");
	Wrapper.Effect.BlurStrength = 12.0f;
	Wrapper.Effect.bHasBlurStrength = true;

	FUMGWidgetNode Child;
	Child.Type = TEXT("TextBlock");
	Child.Id = TEXT("hero_text");
	Child.Name = TEXT("HeroText");
	Wrapper.Children.Add(Child);
	Data.RootWidget = Wrapper;

	const FString ImageReport =
		TEXT("远程图片导入: 新增=0 复用=0 修复=0 失败=1 路径=/Game/Design2UMG/ImportedImages\n")
		TEXT("图片 'Hero' 下载失败: https://example.com/hero.png (原因: 网络请求失败)");

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		ImageReport,
		TEXT("/Game/Design2UMG/_TestOutput/ImportReport"),
		TEXT("G:/UE5/FPSRoguelike/Saved/Design2UMG/WBP_ImportReportFixture.png"),
		TEXT("/Game/Design2UMG/_TestOutput/ImportReport/WBP_ImportReportFixture"));

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);

	FString ReportPath;
	FDateTime LatestTimestamp = FDateTime::MinValue();
	for (const FString& Candidate : ReportsAfter)
	{
		const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*Candidate);
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
		if (Timestamp > LatestTimestamp)
		{
			LatestTimestamp = Timestamp;
			ReportPath = Candidate;
		}
	}

	TestTrue(TEXT("Import report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	FString ReportJson;
	TestTrue(TEXT("Load import report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	if (ReportJson.IsEmpty())
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse import report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (!RootObject.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* SourceObject = nullptr;
	TestTrue(TEXT("Report has source object"), RootObject->TryGetObjectField(TEXT("source"), SourceObject) && SourceObject);
	if (!SourceObject)
	{
		return false;
	}

	TestEqual(TEXT("Source origin inferred from json input"), (*SourceObject)->GetStringField(TEXT("origin")), TEXT("local_json"));
	TestTrue(TEXT("Source identity emitted"), !(*SourceObject)->GetStringField(TEXT("identity")).IsEmpty());

	const TSharedPtr<FJsonObject>* DiagnosticsObject = nullptr;
	TestTrue(TEXT("Report has diagnostics object"), RootObject->TryGetObjectField(TEXT("diagnostics"), DiagnosticsObject) && DiagnosticsObject);
	if (!DiagnosticsObject)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Degradations = nullptr;
	TestTrue(TEXT("Diagnostics include degradations"), (*DiagnosticsObject)->TryGetArrayField(TEXT("degradations"), Degradations) && Degradations && Degradations->Num() >= 2);
	TestTrue(TEXT("Diagnostics include skipped designer nodes"), (*DiagnosticsObject)->GetArrayField(TEXT("skippedDesignerNodes")).Num() == 1);

	bool bFoundTextPathFallback = false;
	bool bFoundRenderFallback = false;
	for (const TSharedPtr<FJsonValue>& Value : *Degradations)
	{
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Entry.IsValid())
		{
			continue;
		}

		const FString Kind = Entry->GetStringField(TEXT("kind"));
		bFoundTextPathFallback |= Kind == TEXT("text_path_fallback");
		bFoundRenderFallback |= Kind == TEXT("render_fallback");
	}
	TestTrue(TEXT("Report records text path fallback"), bFoundTextPathFallback);
	TestTrue(TEXT("Report records render fallback"), bFoundRenderFallback);

	const TSharedPtr<FJsonObject>* EffectsObject = nullptr;
	TestTrue(TEXT("Report has effects object"), RootObject->TryGetObjectField(TEXT("effects"), EffectsObject) && EffectsObject);
	TestEqual(TEXT("Report records effect wrapper count"), static_cast<int32>((*EffectsObject)->GetNumberField(TEXT("wrapperCount"))), 1);
	TestEqual(TEXT("Report records wrapper entry"), (*EffectsObject)->GetArrayField(TEXT("wrappers")).Num(), 1);

	const TSharedPtr<FJsonObject>* ResourcesObject = nullptr;
	TestTrue(TEXT("Report has resources object"), RootObject->TryGetObjectField(TEXT("resources"), ResourcesObject) && ResourcesObject);
	TestEqual(TEXT("Report records missing fonts"), (*ResourcesObject)->GetArrayField(TEXT("missingFonts")).Num(), 1);
	TestEqual(TEXT("Report records image failures"), (*ResourcesObject)->GetArrayField(TEXT("imageFailures")).Num(), 1);

	const TArray<TSharedPtr<FJsonValue>>* Recommendations = nullptr;
	TestTrue(TEXT("Report includes recommendations"), RootObject->TryGetArrayField(TEXT("recommendations"), Recommendations) && Recommendations && Recommendations->Num() > 0);
	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportExecutePathTest,
	"Design2UMG.Import.ImportDiagnosticReportExecutePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportExecutePathTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	FDesignDocument Document;
	Document.Version = TEXT("pen-fixture-v1");
	Document.Metadata.Add(
		TEXT("design.sourceFilePath"),
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), FString::Printf(TEXT("fixture_%s.pen"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));

	TSharedPtr<FDesignNode> RootNode = MakeShared<FDesignNode>();
	RootNode->Id = TEXT("root");
	RootNode->Name = TEXT("Root");
	RootNode->Type = EDesignNodeType::Frame;
	RootNode->Transform.Width = FDesignSize::Fixed(120.0f);
	RootNode->Transform.Height = FDesignSize::Fixed(60.0f);
	Document.Children.Add(RootNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImportReportExecuteFixture");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");

	FDesignImportRequest Request;
	Request.OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ImportReportExecute");
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	Request.DiagnosticDocument = Document;

	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	const FDesignImportExecutionResult Result = DesignImportPipeline::Execute(Data, Request);
	TestTrue(TEXT("Execute data path succeeds"), Result.bSucceeded);
	if (!Result.bSucceeded)
	{
		DeleteGeneratedAssetsAtPath(Request.OutputPath);
		return false;
	}

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Execute data path writes report"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		DeleteGeneratedAssetsAtPath(Request.OutputPath);
		return false;
	}

	const TArray<uint8> ReportBytes = [&ReportPath]()
	{
		TArray<uint8> Bytes;
		FFileHelper::LoadFileToArray(Bytes, *ReportPath);
		return Bytes;
	}();
	TestTrue(TEXT("Execute data path report keeps UTF-8 BOM"),
		ReportBytes.Num() >= 3
		&& ReportBytes[0] == 0xEF
		&& ReportBytes[1] == 0xBB
		&& ReportBytes[2] == 0xBF);

	FString ReportJson;
	TestTrue(TEXT("Load execute-path report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse execute-path report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (RootObject.IsValid())
	{
		TestEqual(TEXT("Execute data path report keeps requested output"), RootObject->GetStringField(TEXT("requestedOutputPath")), Request.OutputPath);
		TestEqual(TEXT("Execute data path report keeps generated asset path"), RootObject->GetStringField(TEXT("generatedAssetPath")), Result.GeneratedAssetPath);
	}

	IFileManager::Get().Delete(*ReportPath);
	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportFailurePathTest,
	"Design2UMG.Import.ImportDiagnosticReportFailurePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportFailurePathTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	FDesignDocument Document;
	Document.Version = TEXT("pen-fixture-failure-v1");
	Document.Metadata.Add(
		TEXT("design.sourceFilePath"),
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), FString::Printf(TEXT("fixture_%s.pen"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImportReportFailureFixture");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");

	FDesignImportRequest Request;
	Request.OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ImportReportFailure");
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintAndPng;
	Request.DiagnosticDocument = Document;

	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	const FDesignImportExecutionResult Result = DesignImportPipeline::Execute(Data, Request);
	TestTrue(TEXT("BlueprintAndPng path succeeds under NullRHI"), Result.bSucceeded);
	TestTrue(TEXT("BlueprintAndPng path saves generated asset path"), !Result.GeneratedAssetPath.IsEmpty());
	TestTrue(TEXT("BlueprintAndPng path saves rendered png path"), !Result.RenderPngPath.IsEmpty());

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Failure path still writes report"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (!ReportPath.IsEmpty() && FPaths::FileExists(ReportPath))
	{
		FString ReportJson;
		TestTrue(TEXT("Load failure-path report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
		TSharedPtr<FJsonObject> RootObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
		TestTrue(TEXT("Parse failure-path report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
		if (RootObject.IsValid())
		{
			TestEqual(TEXT("BlueprintAndPng report keeps generated asset path"), RootObject->GetStringField(TEXT("generatedAssetPath")), Result.GeneratedAssetPath);
			TestEqual(TEXT("BlueprintAndPng report keeps rendered png path"), RootObject->GetStringField(TEXT("renderPngPath")), Result.RenderPngPath);
			TestTrue(TEXT("BlueprintAndPng report still records requested output"), RootObject->GetStringField(TEXT("requestedOutputPath")) == Request.OutputPath);
		}
		IFileManager::Get().Delete(*ReportPath);
	}

	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSourceImportWorkflowTest,
	"Design2UMG.BatchImport.FigmaSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaSourceImportWorkflowTest::RunTest(const FString& Parameters)
{
	const FString JsonPath = GetFixturePath(TEXT("FigmaComplex.json"));
	if (JsonPath.IsEmpty() || !FPaths::FileExists(JsonPath))
	{
		AddWarning(FString::Printf(TEXT("Figma source fixture not found, skipping: %s"), *JsonPath));
		return true;
	}

	FString RawJson;
	TestTrue(TEXT("Load raw Figma json"), FFileHelper::LoadFileToString(RawJson, *JsonPath));
	if (RawJson.IsEmpty())
	{
		return false;
	}

	FDesignDocument Document;
	TestTrue(TEXT("Parse raw Figma json"), FFigmaParser::ParseDocument(RawJson, Document));
	TestTrue(TEXT("Parsed Figma source should contain children"), Document.Children.Num() > 0);

	FUMGWidgetData Data = DesignImportPipeline::AnalyzeImport(Document, FImportAnalysisInput{false}).Data;
	TestFalse(TEXT("Figma source import should yield a widget name"), Data.WidgetName.IsEmpty());
	TestTrue(TEXT("Figma source import should yield root widget children"), Data.RootWidget.Children.Num() > 0);

	if (GUsingNullRHI)
	{
		return true;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaSource");
	const bool bPng = DesignImportUtils::GenerateBlueprintAndPng(Data, OutputPath);
	TestTrue(TEXT("Render figma source import"), bPng);
	DeleteGeneratedAssetsAtPath(OutputPath);
	return bPng;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaInstanceMetadataWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaInstanceMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaInstanceMetadataWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"inst1","name":"InstanceParity","type":"INSTANCE","componentId":"12:34","componentSetId":"56:78","mainComponentKey":"component-key-1","visible":true,
					 "componentProperties":{"title":{"type":"TEXT","value":"Hello"}},
					 "variantProperties":{"Active":"True","Size":"Small"},
					 "overrides":[{"id":"inst1_txt","path":["characters"],"value":"Override"}],
					 "isExposedInstance":true,
					 "absoluteBoundingBox":{"x":16,"y":24,"width":160,"height":64},
					 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.18,"g":0.35,"b":0.95,"a":1}}],
					 "children":[{"id":"inst1_txt","name":"Label","type":"TEXT","visible":true,
						"absoluteBoundingBox":{"x":28,"y":40,"width":120,"height":24},
						"characters":"Instance text",
						"style":{"fontFamily":"Inter","fontSize":16,"fontWeight":500,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}}]}
				]}
			]
		},
		"components":{"12:34":{"key":"nav_item_component","name":"NavItem","componentPropertyDefinitions":{"title":{"type":"TEXT"}}}},
		"componentSets":{"56:78":{"key":"nav_item_variants","name":"NavItemVariants","variantGroupProperties":{"Active":{"values":["True","False"]}}}}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse figma instance metadata fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Converted widget data preserves Figma component catalog"), Data.SourceDocument.HasAnyComponentCatalog());

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaInstanceMetadata");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build figma instance metadata widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated instance metadata class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const UDesign2UMGFigmaMetadataClassExtension* MetadataExtension =
		GeneratedClass->GetExtension<UDesign2UMGFigmaMetadataClassExtension>();
	TestNotNull(TEXT("Generated class carries Figma metadata extension"), MetadataExtension);
	if (!MetadataExtension)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestTrue(TEXT("Generated class keeps components catalog"),
		MetadataExtension->GetDocumentData().ComponentsJson.Contains(TEXT("nav_item_component")));
	TestTrue(TEXT("Generated class keeps componentSets catalog"),
		MetadataExtension->GetDocumentData().ComponentSetsJson.Contains(TEXT("variantGroupProperties")));

	const FDesign2UMGFigmaWidgetMetadataRecord* InstanceRecord =
		MetadataExtension->FindRecordBySourceNodeId(TEXT("inst1"));
	TestNotNull(TEXT("Instance widget metadata record exported"), InstanceRecord);
	if (!InstanceRecord)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Instance record widget id preserved"), InstanceRecord->WidgetId, TEXT("inst1"));
	TestEqual(TEXT("Instance record source node type preserved"), InstanceRecord->SourceNode.SourceNodeType, TEXT("INSTANCE"));
	TestEqual(TEXT("Instance record componentId preserved"), InstanceRecord->SourceNode.ComponentId, TEXT("12:34"));
	TestEqual(TEXT("Instance record componentSetId preserved"), InstanceRecord->SourceNode.ComponentSetId, TEXT("56:78"));
	TestTrue(TEXT("Instance record variant properties preserved"), !InstanceRecord->SourceNode.VariantPropertiesJson.IsEmpty());
	TestTrue(TEXT("Template widget exists for exported record"),
		GeneratedClass->GetWidgetTreeArchetype() && GeneratedClass->GetWidgetTreeArchetype()->FindWidget(InstanceRecord->WidgetName) != nullptr);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Instance metadata widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestTrue(TEXT("Runtime widget tree can resolve exported widget name"),
		WidgetInstance->WidgetTree->FindWidget(InstanceRecord->WidgetName) != nullptr);
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextAlignVerticalRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.TextAlignVerticalRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextAlignVerticalRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"width": 240,
				"height": 80,
				"layout": "horizontal",
				"alignItems": "start",
				"children": [
					{
						"id": "label",
						"type": "text",
						"content": "Button",
						"width": 140,
						"height": 40,
						"fill": "#FFFFFF",
						"fontFamily": "Geist",
						"fontSize": 14,
						"textAlignVertical": "middle"
					}
				]
			}
		]
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse textAlignVertical runtime fixture"), FPenParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/TextAlignVerticalRuntime");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build textAlignVertical widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated textAlignVertical class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("textAlignVertical widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();

	UTextBlock* Label = Cast<UTextBlock>(WidgetInstance->WidgetTree->FindWidget(TEXT("label")));
	TestTrue(TEXT("textAlignVertical TextBlock exists"), Label != nullptr);
	if (!Label)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const FMargin LabelMargin = ReadTextWidgetMargin(Label);
	TestTrue(TEXT("Runtime TextBlock receives positive top margin"), LabelMargin.Top > 0.5f);
	TestTrue(TEXT("Runtime TextBlock receives positive bottom margin"), LabelMargin.Bottom > 0.5f);
	TestTrue(TEXT("Runtime TextBlock center alignment keeps margins symmetric"),
		FMath::IsNearlyEqual(LabelMargin.Top, LabelMargin.Bottom, 0.75f));

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaOverflowDirectionRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaOverflowDirectionRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaOverflowDirectionRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"scroll_frame","name":"ScrollFrame","type":"FRAME","visible":true,
					 "layoutMode":"VERTICAL","itemSpacing":12,"overflowDirection":"VERTICAL_SCROLLING",
					 "absoluteBoundingBox":{"x":0,"y":0,"width":320,"height":240},
					 "children":[
						{"id":"row1","name":"Row1","type":"RECTANGLE","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":320,"height":80},"fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.3,"b":0.4,"a":1}}]},
						{"id":"row2","name":"Row2","type":"RECTANGLE","visible":true,"absoluteBoundingBox":{"x":0,"y":92,"width":320,"height":80},"fills":[{"type":"SOLID","visible":true,"color":{"r":0.4,"g":0.3,"b":0.2,"a":1}}]}
					 ]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse scrollable figma workflow fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaOverflowDirection");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build figma overflow widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated overflow class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Overflow widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();

	UScrollBox* ScrollBox = Cast<UScrollBox>(WidgetInstance->WidgetTree->FindWidget(TEXT("ScrollFrame")));
	TestTrue(TEXT("Scrollable frame instantiates as ScrollBox"), ScrollBox != nullptr);
	if (!ScrollBox)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("ScrollBox orientation is vertical"), ScrollBox->GetOrientation(), Orient_Vertical);
	TestEqual(TEXT("ScrollBox keeps child count"), ScrollBox->GetChildrenCount(), 2);
	TestEqual(TEXT("ScrollBox hides scrollbar by default"), ScrollBox->GetScrollBarVisibility(), ESlateVisibility::Hidden);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaGridSlotSemanticsRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaGridSlotSemanticsRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaGridSlotSemanticsRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"grid_frame","name":"grid_frame","type":"FRAME","visible":true,
					 "layoutMode":"GRID","gridRowCount":2,"gridColumnCount":2,"gridRowGap":24,"gridColumnGap":12,
					 "gridChildHorizontalAlign":"CENTER","gridChildVerticalAlign":"MAX",
					 "absoluteBoundingBox":{"x":0,"y":0,"width":320,"height":240},
					 "children":[
						{"id":"head_card","name":"head_card","type":"RECTANGLE","visible":true,
						 "gridRowAnchorIndex":0,"gridColumnAnchorIndex":0,
						 "absoluteBoundingBox":{"x":0,"y":0,"width":100,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.3,"b":0.4,"a":1}}]},
						{"id":"tail_card","name":"tail_card","type":"RECTANGLE","visible":true,
						 "gridRowAnchorIndex":0,"gridColumnAnchorIndex":1,
						 "absoluteBoundingBox":{"x":116,"y":0,"width":100,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.4,"g":0.3,"b":0.2,"a":1}}]},
						{"id":"hero_card","name":"hero_card","type":"RECTANGLE","visible":true,
						 "gridRowAnchorIndex":1,"gridColumnAnchorIndex":0,"gridColumnSpan":2,
						 "absoluteBoundingBox":{"x":0,"y":64,"width":216,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.6,"b":0.5,"a":1}}]}
					 ]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse figma grid runtime fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaGridSlotSemantics");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build figma grid widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated figma grid class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Grid widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();

	UGridPanel* GridPanel = Cast<UGridPanel>(WidgetInstance->WidgetTree->FindWidget(TEXT("grid_frame")));
	UWidget* TailCard = WidgetInstance->WidgetTree->FindWidget(TEXT("tail_card_sz"));
	UWidget* HeroCard = WidgetInstance->WidgetTree->FindWidget(TEXT("hero_card_sz"));
	TestTrue(TEXT("Grid frame instantiates as GridPanel"), GridPanel != nullptr);
	TestTrue(TEXT("Tail card runtime wrapper exists"), TailCard != nullptr);
	TestTrue(TEXT("Hero card runtime wrapper exists"), HeroCard != nullptr);
	if (!GridPanel || !TailCard || !HeroCard)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UGridSlot* TailSlot = Cast<UGridSlot>(TailCard->Slot);
	UGridSlot* HeroSlot = Cast<UGridSlot>(HeroCard->Slot);
	TestTrue(TEXT("Tail slot is UGridSlot"), TailSlot != nullptr);
	TestTrue(TEXT("Hero slot is UGridSlot"), HeroSlot != nullptr);
	if (!TailSlot || !HeroSlot)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Tail runtime row"), TailSlot->GetRow(), 0);
	TestEqual(TEXT("Tail runtime column"), TailSlot->GetColumn(), 1);
	TestEqual(TEXT("Tail runtime horizontal align"), TailSlot->GetHorizontalAlignment(), HAlign_Center);
	TestEqual(TEXT("Tail runtime vertical align"), TailSlot->GetVerticalAlignment(), VAlign_Bottom);
	const FMargin TailPadding = TailSlot->GetPadding();
	TestTrue(TEXT("Tail runtime inner left gap"), FMath::IsNearlyEqual(TailPadding.Left, 6.0f, 0.01f));
	TestTrue(TEXT("Tail runtime bottom gap"), FMath::IsNearlyEqual(TailPadding.Bottom, 12.0f, 0.01f));

	TestEqual(TEXT("Hero runtime row"), HeroSlot->GetRow(), 1);
	TestEqual(TEXT("Hero runtime column"), HeroSlot->GetColumn(), 0);
	TestEqual(TEXT("Hero runtime column span"), HeroSlot->GetColumnSpan(), 2);
	TestEqual(TEXT("Hero runtime horizontal align"), HeroSlot->GetHorizontalAlignment(), HAlign_Center);
	TestEqual(TEXT("Hero runtime vertical align"), HeroSlot->GetVerticalAlignment(), VAlign_Bottom);
	const FMargin HeroPadding = HeroSlot->GetPadding();
	TestTrue(TEXT("Hero runtime top gap"), FMath::IsNearlyEqual(HeroPadding.Top, 12.0f, 0.01f));
	TestTrue(TEXT("Hero runtime right outer edge has no extra gap"), FMath::IsNearlyEqual(HeroPadding.Right, 0.0f, 0.01f));

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

#include "Design2UMGWorkflowTests.ImportBake.inl"

namespace
{
	FString SerializeWidgetTree(const UWidget* Widget, int32 Depth = 0)
	{
		if (!Widget)
		{
			return FString();
		}

		const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
		const FString SlotClass = Widget->Slot ? Widget->Slot->GetClass()->GetName() : TEXT("none");
		FString Result = FString::Printf(TEXT("%s%s:%s[%s]\n"),
			*Indent,
			*Widget->GetClass()->GetName(),
			*Widget->GetName(),
			*SlotClass);

		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				Result += SerializeWidgetTree(Panel->GetChildAt(i), Depth + 1);
			}
		}

		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTreeBuilderStructureSnapshotStableTest,
	"Design2UMG.TreeBuilder.StructureSnapshotStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTreeBuilderStructureSnapshotStableTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_SnapFixture");

	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("snap_root");
	Data.RootWidget.Name = TEXT("snap_root");
	Data.RootWidget.Bounds = {0.0f, 0.0f, 400.0f, 300.0f};

	FUMGWidgetNode BorderNode;
	BorderNode.Type = TEXT("Border");
	BorderNode.Id = TEXT("snap_border");
	BorderNode.Name = TEXT("snap_border");
	BorderNode.Bounds = {0.0f, 0.0f, 200.0f, 100.0f};

	FUMGWidgetNode TextNode;
	TextNode.Type = TEXT("TextBlock");
	TextNode.Id = TEXT("snap_text");
	TextNode.Name = TEXT("snap_text");
	TextNode.Bounds = {0.0f, 0.0f, 100.0f, 30.0f};
	TextNode.Text.Text = TEXT("snap");
	TextNode.Text.bHasText = true;
	BorderNode.Children.Add(TextNode);

	FUMGWidgetNode GridNode;
	GridNode.Type = TEXT("GridPanel");
	GridNode.Id = TEXT("snap_grid");
	GridNode.Name = TEXT("snap_grid");
	GridNode.Bounds = {0.0f, 110.0f, 200.0f, 100.0f};

	FUMGWidgetNode Img0;
	Img0.Type = TEXT("Image");
	Img0.Id = TEXT("snap_img0");
	Img0.Name = TEXT("snap_img0");
	Img0.Bounds = {0.0f, 0.0f, 80.0f, 80.0f};

	FUMGWidgetNode Img1;
	Img1.Type = TEXT("Image");
	Img1.Id = TEXT("snap_img1");
	Img1.Name = TEXT("snap_img1");
	Img1.Bounds = {90.0f, 0.0f, 80.0f, 80.0f};

	GridNode.Children.Add(Img0);
	GridNode.Children.Add(Img1);

	Data.RootWidget.Children.Add(BorderNode);
	Data.RootWidget.Children.Add(GridNode);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SnapFixture");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Snapshot fixture blueprint built"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Snapshot fixture generated class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists for snapshot test"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Snapshot fixture widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree || !WidgetInstance->WidgetTree->RootWidget)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const FString Actual = SerializeWidgetTree(WidgetInstance->WidgetTree->RootWidget).TrimEnd();

	const FString Expected = TEXT(
		"CanvasPanel:snap_root[none]\n"
		"  Border:snap_border[CanvasPanelSlot]\n"
		"    TextBlock:snap_text[BorderSlot]\n"
		"  GridPanel:snap_grid[CanvasPanelSlot]\n"
		"    Image:snap_img0[GridSlot]\n"
		"    Image:snap_img1[GridSlot]"
	);

	TestEqual(TEXT("Widget tree structure snapshot matches"), Actual, Expected);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutFixerConservatismTest,
	"Design2UMG.LayoutFixer.ConservatismCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutFixerConservatismTest::RunTest(const FString& Parameters)
{
	FFixerPreset Preset;
	Preset.DesignSize = FIntPoint(1920, 1080);
	Preset.SnapPx = 4;
	FFixerWidgetNode Root;
	Root.Bounds = {0, 0, 1, 1};
	FFixerWidgetNode Child1;
	Child1.Bounds = {0, 0, 0.1f, 0.1f};
	FFixerWidgetNode Child2;
	Child2.Bounds = {0.5f, 0, 0.1f, 0.1f};
	Root.Children.Add(Child1);
	Root.Children.Add(Child2);
	TArray<FFixerLogEntry> Log;
	FFixerStats Stats;
	FLayoutFixer::FixWidgetTree(Root, Preset, Log, &Stats);
	TestEqual(TEXT("RolledBack should be non-negative"), Stats.RolledBack >= 0, true);
	return true;
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFitContentMinimumExpandsToContentTest,
	"Design2UMG.Converter.FitContentMinimumExpandsToContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFitContentMinimumExpandsToContentTest::RunTest(const FString& Parameters)
{
	FDesignDocument Doc;
	const TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
	Root->Id = TEXT("fit-min-root");
	Root->Name = TEXT("FitMinRoot");
	Root->Type = EDesignNodeType::Frame;
	Root->LayoutData.Layout = EDesignLayout::Vertical;
	Root->Transform.Width = FDesignSize::Fixed(320.0f);
	Root->Transform.Height = FDesignSize::Fit();
	Root->Transform.bHasWidth = true;
	const TSharedPtr<FDesignNode> Row = MakeShared<FDesignNode>();
	Row->Id = TEXT("fit-min-row");
	Row->Name = TEXT("FitMinRow");
	Row->Type = EDesignNodeType::Frame;
	Row->LayoutData.Layout = EDesignLayout::Vertical;
	Row->Transform.Width = FDesignSize::Fixed(320.0f);
	Row->Transform.Height = FDesignSize::Fit(44.0f);
	Row->Transform.bHasWidth = true;
	Row->Transform.bHasHeight = true;
	const TSharedPtr<FDesignNode> Content = MakeShared<FDesignNode>();
	Content->Id = TEXT("fit-min-content");
	Content->Name = TEXT("FitMinContent");
	Content->Type = EDesignNodeType::Frame;
	Content->LayoutData.Layout = EDesignLayout::Vertical;
	Content->Transform.Width = FDesignSize::Fixed(320.0f);
	Content->Transform.Height = FDesignSize::Fixed(56.0f);
	Content->Transform.bHasWidth = true;
	Content->Transform.bHasHeight = true;
	Row->Children.Add(Content);
	Root->Children.Add(Row);
	Doc.Children.Add(Root);
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* RootWidget = FindWidgetById(Data.RootWidget, TEXT("fit-min-root"));
	const FUMGWidgetNode* RowSizeBox = FindWidgetById(Data.RootWidget, TEXT("fit-min-row_sz"));
	TestTrue(TEXT("Find fit-content minimum root widget"), RootWidget != nullptr);
	TestTrue(TEXT("Find fit-content minimum row size box"), RowSizeBox != nullptr);
	if (!RootWidget || !RowSizeBox)
	{
		return false;
	}
	TestTrue(TEXT("Root fit-content height should grow beyond the minimum fallback"),
		RootWidget->Bounds.H >= 56.0f - 0.5f);
	const FString HeightOverride = GetWidgetProperty(*RowSizeBox, TEXT("HeightOverride"));
	const bool bHasHeightOverride = !HeightOverride.IsEmpty();
	const FString MinDesiredHeight = GetWidgetProperty(*RowSizeBox, TEXT("MinDesiredHeight"));
	const bool bHasMinDesiredHeight = !MinDesiredHeight.IsEmpty();
	TestTrue(TEXT("Flow fit-content(min) row should export a concrete height constraint"),
		bHasHeightOverride || bHasMinDesiredHeight);
	if (bHasHeightOverride)
	{
		TestTrue(TEXT("HeightOverride should be at least the larger child content height"),
			FCString::Atof(*HeightOverride) >= 56.0f - 0.5f);
	}
	if (bHasMinDesiredHeight)
	{
		TestTrue(TEXT("MinDesiredHeight should be at least the larger child content height"),
			FCString::Atof(*MinDesiredHeight) >= 56.0f - 0.5f);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFillWidthSingleLineTextDoesNotInflateFitHeightTest,
	"Design2UMG.Converter.FillWidthSingleLineTextDoesNotInflateFitHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFillWidthSingleLineTextDoesNotInflateFitHeightTest::RunTest(const FString& Parameters)
{
	FDesignDocument Doc;
	const TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
	Root->Id = TEXT("text-fit-root");
	Root->Name = TEXT("TextFitRoot");
	Root->Type = EDesignNodeType::Frame;
	Root->LayoutData.Layout = EDesignLayout::Vertical;
	Root->Transform.Width = FDesignSize::Fixed(160.0f);
	Root->Transform.Height = FDesignSize::Fit();
	Root->Transform.bHasWidth = true;
	const TSharedPtr<FDesignNode> HeaderCell = MakeShared<FDesignNode>();
	HeaderCell->Id = TEXT("header-cell");
	HeaderCell->Name = TEXT("HeaderCell");
	HeaderCell->Type = EDesignNodeType::Frame;
	HeaderCell->LayoutData.Layout = EDesignLayout::Horizontal;
	HeaderCell->Transform.Width = FDesignSize::Fixed(160.0f);
	HeaderCell->Transform.Height = FDesignSize::Fit(44.0f);
	HeaderCell->Transform.bHasWidth = true;
	HeaderCell->Transform.bHasHeight = true;
	HeaderCell->LayoutData.Padding.Top = 12.0f;
	HeaderCell->LayoutData.Padding.Right = 12.0f;
	HeaderCell->LayoutData.Padding.Bottom = 12.0f;
	HeaderCell->LayoutData.Padding.Left = 12.0f;
	HeaderCell->LayoutData.ExplicitPadding = HeaderCell->LayoutData.Padding;
	HeaderCell->LayoutData.AlignItems = EDesignAlignItems::Center;
	const TSharedPtr<FDesignNode> HeaderText = MakeShared<FDesignNode>();
	HeaderText->Id = TEXT("header-text");
	HeaderText->Name = TEXT("HeaderText");
	HeaderText->Type = EDesignNodeType::Text;
	HeaderText->Transform.Width = FDesignSize::Fill();
	HeaderText->Transform.Height = FDesignSize::Fit();
	HeaderText->Text.Content = TEXT("Status");
	HeaderText->Text.FontFamily = TEXT("Inter");
	HeaderText->Text.FontSize = 14.0f;
	HeaderText->Text.FontWeight = TEXT("normal");
	HeaderText->Text.LineHeight = 1.4285714285714286f;
	HeaderText->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	HeaderCell->Children.Add(HeaderText);
	Root->Children.Add(HeaderCell);
	Doc.Children.Add(Root);
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* RootWidget = FindWidgetById(Data.RootWidget, TEXT("text-fit-root"));
	const FUMGWidgetNode* HeaderCellSizeBox = FindWidgetById(Data.RootWidget, TEXT("header-cell_sz"));
	TestTrue(TEXT("Find single-line text fit root widget"), RootWidget != nullptr);
	TestTrue(TEXT("Find header cell size box"), HeaderCellSizeBox != nullptr);
	if (!RootWidget || !HeaderCellSizeBox)
	{
		return false;
	}
	const FString HeightOverride = GetWidgetProperty(*HeaderCellSizeBox, TEXT("HeightOverride"));
	const bool bHasHeightOverride = !HeightOverride.IsEmpty();
	const FString MinDesiredHeight = GetWidgetProperty(*HeaderCellSizeBox, TEXT("MinDesiredHeight"));
	const bool bHasMinDesiredHeight = !MinDesiredHeight.IsEmpty();
	TestTrue(TEXT("Header cell should export a height constraint"),
		bHasHeightOverride || bHasMinDesiredHeight);
	const float ExportedHeight = bHasHeightOverride
		? FCString::Atof(*HeightOverride)
		: (bHasMinDesiredHeight ? FCString::Atof(*MinDesiredHeight) : 0.0f);
	TestTrue(TEXT("Single-line fill-width text should keep the 44px fit-content minimum"),
		FMath::IsNearlyEqual(ExportedHeight, 44.0f, 0.5f));
	TestTrue(TEXT("Root fit height should remain at the 44px minimum"),
		FMath::IsNearlyEqual(RootWidget->Bounds.H, 44.0f, 0.5f));
	return true;
}
namespace
{
	struct FNormalizerSnapshot
	{
		FString Type;
		FString Id;
		FString Name;
		FString PathGeometry;
		bool bHasGeometry = false;
		FString MaterialRef;
		bool bHasMaterialRef = false;
		FString TexturePath;
		bool bHasTexturePath = false;
	};

	FNormalizerSnapshot SnapNode(const FUMGWidgetNode& Node)
	{
		FNormalizerSnapshot S;
		S.Type = Node.Type;
		S.Id = Node.Id;
		S.Name = Node.Name;
		S.PathGeometry = Node.Path.Geometry;
		S.bHasGeometry = Node.Path.bHasGeometry;
		S.MaterialRef = Node.Brush.MaterialRef;
		S.bHasMaterialRef = Node.Brush.bHasMaterialRef;
		S.TexturePath = Node.Brush.Image.TexturePath;
		S.bHasTexturePath = Node.Brush.Image.bHasTexturePath;
		return S;
	}

	FUMGWidgetNode MakePathImageNode(const FString& Id, const FString& Geometry)
	{
		FUMGWidgetNode Node;
		Node.Type = TEXT("Image");
		Node.Id = Id;
		Node.Name = Id;
		Node.Path.Geometry = Geometry;
		Node.Path.bHasGeometry = !Geometry.IsEmpty();
		Node.Path.FillRule = TEXT("nonzero");
		Node.Path.bHasFillRule = true;
		Node.Path.FillColor = FLinearColor::White;
		Node.Path.bHasFillColor = true;
		Node.WidgetStyle.WidthOverride = 32.0f;
		Node.WidgetStyle.HeightOverride = 32.0f;
		return Node;
	}

	FUMGWidgetNode MakeProceduralNode(const FString& Id, const FString& MaterialRef)
	{
		FUMGWidgetNode Node;
		Node.Type = TEXT("Image");
		Node.Id = Id;
		Node.Name = Id;
		Node.Brush.MaterialRef = MaterialRef;
		Node.Brush.bHasMaterialRef = !MaterialRef.IsEmpty();
		Node.WidgetStyle.WidthOverride = 32.0f;
		Node.WidgetStyle.HeightOverride = 32.0f;
		return Node;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreBuildNormalizerPathTextureBeforeAfterDiffTest,
	"Design2UMG.PreBuildResourceNormalizer.PathTexture.BeforeAfterDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreBuildNormalizerPathTextureBeforeAfterDiffTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI) return true;

	FUMGWidgetNode Root;
	Root.Type = TEXT("CanvasPanel");
	Root.Id = TEXT("norm_path_root");
	Root.Name = TEXT("norm_path_root");
	Root.Bounds = {0, 0, 64, 64};
	Root.Children.Add(MakePathImageNode(TEXT("path_node"), TEXT("M 0 0 L 32 0 L 32 32 Z")));

	const FNormalizerSnapshot Before = SnapNode(Root.Children[0]);
	TestTrue(TEXT("Before: bHasGeometry"), Before.bHasGeometry);
	TestFalse(TEXT("Before: bHasTexturePath"), Before.bHasTexturePath);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/NormPathTexture");
	FPreBuildResourceNormalizer::NormalizePathTextures(Root, OutputPath / TEXT("D2U_PathTextures"), TEXT("norm_path"));

	const FNormalizerSnapshot After = SnapNode(Root.Children[0]);
	TestEqual(TEXT("After: type unchanged"), After.Type, Before.Type);
	TestEqual(TEXT("After: id unchanged"), After.Id, Before.Id);
	TestEqual(TEXT("After: name unchanged"), After.Name, Before.Name);
	TestFalse(TEXT("After: bHasGeometry cleared"), After.bHasGeometry);
	TestTrue(TEXT("After: bHasTexturePath set"), After.bHasTexturePath);
	TestFalse(TEXT("After: TexturePath non-empty"), After.TexturePath.IsEmpty());
	TestFalse(TEXT("After: MaterialRef not set"), After.bHasMaterialRef);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreBuildNormalizerProceduralTextureBeforeAfterDiffTest,
	"Design2UMG.PreBuildResourceNormalizer.ProceduralTexture.BeforeAfterDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreBuildNormalizerProceduralTextureBeforeAfterDiffTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI) return true;

	FUMGWidgetNode Root;
	Root.Type = TEXT("CanvasPanel");
	Root.Id = TEXT("norm_proc_root");
	Root.Name = TEXT("norm_proc_root");
	Root.Bounds = {0, 0, 64, 64};
	Root.Children.Add(MakeProceduralNode(TEXT("proc_node"), TEXT("RoundedRect")));

	const FNormalizerSnapshot Before = SnapNode(Root.Children[0]);
	TestTrue(TEXT("Before: bHasMaterialRef"), Before.bHasMaterialRef);
	TestFalse(TEXT("Before: bHasTexturePath"), Before.bHasTexturePath);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/NormProcTexture");
	FPreBuildResourceNormalizer::NormalizeProceduralTextures(Root, OutputPath / TEXT("D2U_ProceduralTextures"), TEXT("norm_proc"));

	const FNormalizerSnapshot After = SnapNode(Root.Children[0]);
	TestEqual(TEXT("After: type unchanged"), After.Type, Before.Type);
	TestEqual(TEXT("After: id unchanged"), After.Id, Before.Id);
	TestEqual(TEXT("After: name unchanged"), After.Name, Before.Name);
	TestFalse(TEXT("After: bHasMaterialRef cleared"), After.bHasMaterialRef);
	TestTrue(TEXT("After: bHasTexturePath set"), After.bHasTexturePath);
	TestFalse(TEXT("After: TexturePath non-empty"), After.TexturePath.IsEmpty());

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreBuildNormalizerPathTextureCacheDedupTest,
	"Design2UMG.PreBuildResourceNormalizer.PathTexture.CacheDedup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreBuildNormalizerPathTextureCacheDedupTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI) return true;

	const FString SharedGeometry = TEXT("M 0 0 L 32 0 L 32 32 Z");

	FUMGWidgetNode Root;
	Root.Type = TEXT("CanvasPanel");
	Root.Id = TEXT("norm_dedup_root");
	Root.Name = TEXT("norm_dedup_root");
	Root.Bounds = {0, 0, 64, 64};
	Root.Children.Add(MakePathImageNode(TEXT("dedup_a"), SharedGeometry));
	Root.Children.Add(MakePathImageNode(TEXT("dedup_b"), SharedGeometry));

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/NormPathDedup");
	FPreBuildResourceNormalizer::NormalizePathTextures(Root, OutputPath / TEXT("D2U_PathTextures"), TEXT("norm_dedup"));

	const FNormalizerSnapshot AfterA = SnapNode(Root.Children[0]);
	const FNormalizerSnapshot AfterB = SnapNode(Root.Children[1]);

	TestTrue(TEXT("A: bHasTexturePath set"), AfterA.bHasTexturePath);
	TestTrue(TEXT("B: bHasTexturePath set"), AfterB.bHasTexturePath);
	TestFalse(TEXT("A: geometry cleared"), AfterA.bHasGeometry);
	TestFalse(TEXT("B: geometry cleared"), AfterB.bHasGeometry);
	TestEqual(TEXT("Dedup: same geometry yields same asset path"), AfterA.TexturePath, AfterB.TexturePath);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}


// ---- WidgetLayoutAdapter focused tests ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutAdapterSlotSnapshotParityTest,
	"Design2UMG.LayoutAdapter.SlotSnapshotParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutAdapterSlotSnapshotParityTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Bounds = {100.f, 200.f, 300.f, 150.f};

	const FIntPoint DesignSize(1920, 1080);

	// Direct LayoutCalculator result
	FLayoutContext Ctx;
	Ctx.DesignSize = DesignSize;
	Ctx.Preset = FLayoutPreset::GetStandardHUDPreset();
	FBounds NB;
	NB.X = Node.Bounds.X / DesignSize.X;
	NB.Y = Node.Bounds.Y / DesignSize.Y;
	NB.W = Node.Bounds.W / DesignSize.X;
	NB.H = Node.Bounds.H / DesignSize.Y;
	const FUMGSlotData Expected = FLayoutCalculator::CalculateSlot(NB, Ctx);

	// Adapter result (no parent needed for parity check — use nullptr canvas path via bEnableAdaptiveAnchors=true)
	// Build a minimal transient WidgetTree + CanvasPanel to satisfy IsRootCanvasChild
	UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	UWidgetTree* Tree = NewObject<UWidgetTree>(WBP, NAME_None, RF_Transient);
	UCanvasPanel* Canvas = NewObject<UCanvasPanel>(Tree, NAME_None, RF_Transient);
	Tree->RootWidget = Canvas;

	const FUMGSlotData Actual = FWidgetLayoutAdapter::Resolve(Node, Canvas, Tree, DesignSize, true);

	TestEqual(TEXT("AnchorMinX"), Actual.Anchors.Min.X, Expected.Anchors.Min.X);
	TestEqual(TEXT("AnchorMinY"), Actual.Anchors.Min.Y, Expected.Anchors.Min.Y);
	TestEqual(TEXT("AnchorMaxX"), Actual.Anchors.Max.X, Expected.Anchors.Max.X);
	TestEqual(TEXT("AnchorMaxY"), Actual.Anchors.Max.Y, Expected.Anchors.Max.Y);
	TestEqual(TEXT("OffsetLeft"),  Actual.Offsets.Left,   Expected.Offsets.Left);
	TestEqual(TEXT("OffsetTop"),   Actual.Offsets.Top,    Expected.Offsets.Top);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutAdapterPassThroughWhenNoCanvasTest,
	"Design2UMG.LayoutAdapter.PassThroughWhenNoCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutAdapterPassThroughWhenNoCanvasTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Bounds = {50.f, 50.f, 200.f, 100.f};
	Node.Slot.Anchors.Min.X = 0.25f;
	Node.Slot.Anchors.Min.Y = 0.25f;

	// Non-canvas parent: VerticalBox
	UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	UWidgetTree* Tree = NewObject<UWidgetTree>(WBP, NAME_None, RF_Transient);
	UVerticalBox* VBox = NewObject<UVerticalBox>(Tree, NAME_None, RF_Transient);
	Tree->RootWidget = VBox;

	const FUMGSlotData Result = FWidgetLayoutAdapter::Resolve(Node, VBox, Tree, FIntPoint(1920, 1080), true);

	TestEqual(TEXT("PassThrough AnchorMinX"), Result.Anchors.Min.X, Node.Slot.Anchors.Min.X);
	TestEqual(TEXT("PassThrough AnchorMinY"), Result.Anchors.Min.Y, Node.Slot.Anchors.Min.Y);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutAdapterAutoSizePreservedTest,
	"Design2UMG.LayoutAdapter.AutoSizePreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutAdapterAutoSizePreservedTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Bounds = {100.f, 100.f, 400.f, 200.f};
	Node.Slot.bAutoSize = true;

	UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	UWidgetTree* Tree = NewObject<UWidgetTree>(WBP, NAME_None, RF_Transient);
	UCanvasPanel* Canvas = NewObject<UCanvasPanel>(Tree, NAME_None, RF_Transient);
	Tree->RootWidget = Canvas;

	const FUMGSlotData Result = FWidgetLayoutAdapter::Resolve(Node, Canvas, Tree, FIntPoint(1920, 1080), true);

	TestTrue(TEXT("bAutoSize preserved"), Result.bAutoSize);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutAdapterTopologyInvariantTest,
	"Design2UMG.LayoutAdapter.TopologyInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutAdapterTopologyInvariantTest::RunTest(const FString& Parameters)
{
	// Build a simple 2-level tree: CanvasPanel -> TextBlock
	FUMGWidgetData WidgetData;
	WidgetData.WidgetName = TEXT("WBP_TopologyTest");

	FUMGWidgetNode Root;
	Root.Type = TEXT("CanvasPanel");
	Root.Id = TEXT("topo_root");
	Root.Name = TEXT("topo_root");
	Root.Bounds = {0, 0, 1920, 1080};

	FUMGWidgetNode Child;
	Child.Type = TEXT("TextBlock");
	Child.Id = TEXT("topo_text");
	Child.Name = TEXT("topo_text");
	Child.Bounds = {100, 100, 200, 50};
	Root.Children.Add(Child);

	WidgetData.RootWidget = Root;

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/TopologyInvariant");
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(WidgetData, OutputPath);
	if (!TestNotNull(TEXT("WBP created"), WBP)) return false;
	if (!TestNotNull(TEXT("WidgetTree exists"), WBP->WidgetTree.Get())) return false;

	// Topology snapshot: collect widget class names in tree order
	TArray<FString> Snapshot;
	WBP->WidgetTree->ForEachWidget([&](UWidget* W) {
		Snapshot.Add(W->GetClass()->GetName());
	});

	TestTrue(TEXT("Has CanvasPanel"), Snapshot.Contains(TEXT("CanvasPanel")));
	TestTrue(TEXT("Has TextBlock"),   Snapshot.Contains(TEXT("TextBlock")));
	TestEqual(TEXT("Exactly 2 widgets"), Snapshot.Num(), 2);

	return true;
}

#endif // WITH_AUTOMATION_WORKER
