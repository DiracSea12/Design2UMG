// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGFigmaMetadataExtensions.h"
#include "Design2UMGLocalFigmaImport.h"
#include "Design2UMGPrototypeNavigationExtensions.h"
#include "DesignFontImportService.h"
#include "Design2UMGTestUtils.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"
#include "FontResolver.h"
#include "FigmaParser.h"
#include "LayoutFixer.h"
#include "PenParser.h"
#include "UMGGenerator.h"
#include "PenMCPBridge.h"
#include "SDesign2UMGPreviewWindow.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Editor.h"
#include "Components/Button.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/Overlay.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "RHI.h"
#if WITH_AUTOMATION_WORKER
using namespace Design2UMGTestUtils;

extern bool GDesign2UMGPrototypeNavigationCaptureOpenUrl;
extern FString GDesign2UMGPrototypeNavigationLastOpenedUrl;

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

	struct FScopedPrototypeOpenUrlCapture
	{
		FScopedPrototypeOpenUrlCapture()
		{
			bSavedCapture = GDesign2UMGPrototypeNavigationCaptureOpenUrl;
			SavedLastOpenedUrl = GDesign2UMGPrototypeNavigationLastOpenedUrl;
			GDesign2UMGPrototypeNavigationCaptureOpenUrl = true;
			GDesign2UMGPrototypeNavigationLastOpenedUrl.Reset();
		}

		~FScopedPrototypeOpenUrlCapture()
		{
			GDesign2UMGPrototypeNavigationCaptureOpenUrl = bSavedCapture;
			GDesign2UMGPrototypeNavigationLastOpenedUrl = SavedLastOpenedUrl;
		}

		bool bSavedCapture = false;
		FString SavedLastOpenedUrl;
	};

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

	UWidgetSwitcher* FindPrototypeSwitcherWidget(UUserWidget* WidgetInstance, const FUMGWidgetData& Data)
	{
		if (!WidgetInstance || !WidgetInstance->WidgetTree || Data.PrototypeNavigation.SwitcherWidgetName.IsEmpty())
		{
			return nullptr;
		}

		return Cast<UWidgetSwitcher>(WidgetInstance->WidgetTree->FindWidget(FName(*Data.PrototypeNavigation.SwitcherWidgetName)));
	}

	UOverlay* FindPrototypeOverlayHostWidget(UUserWidget* WidgetInstance, const FUMGWidgetData& Data)
	{
		if (!WidgetInstance || !WidgetInstance->WidgetTree || Data.PrototypeNavigation.OverlayHostWidgetName.IsEmpty())
		{
			return nullptr;
		}

		return Cast<UOverlay>(WidgetInstance->WidgetTree->FindWidget(FName(*Data.PrototypeNavigation.OverlayHostWidgetName)));
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

	void TickCoreTickerFor(const float TotalSeconds, const float StepSeconds = 1.0f / 60.0f)
	{
		float Elapsed = 0.0f;
		while (Elapsed < TotalSeconds)
		{
			const float CurrentStep = FMath::Min(StepSeconds, TotalSeconds - Elapsed);
			FTSTicker::GetCoreTicker().Tick(CurrentStep);
			Elapsed += CurrentStep;
		}
	}

	UWidgetAnimation* FindGeneratedAnimation(
		const UWidgetBlueprintGeneratedClass* GeneratedClass,
		const TCHAR* AnimationName)
	{
		if (!GeneratedClass || !AnimationName)
		{
			return nullptr;
		}

		const FName TargetName(AnimationName);
		for (UWidgetAnimation* Animation : GeneratedClass->Animations)
		{
			if (!Animation)
			{
				continue;
			}

			if (Animation->GetFName() == TargetName
				|| Animation->GetDisplayLabel().Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return Animation;
			}
		}

		return nullptr;
	}
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

		FUMGWidgetData Data;
		DesignImportPipeline::ConvertDocument(Document, Data);
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
	TestFalse(TEXT("Welcome body no longer writes legacy font size"), HasLegacyWidgetProperty(*WelcomeBody, TEXT("FontSize")));
	TestFalse(TEXT("Pro tips no longer writes legacy font size"), HasLegacyWidgetProperty(*ProTips, TEXT("FontSize")));
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

	FUMGWidgetData Data;
	DesignImportPipeline::ConvertDocument(Document, Data);

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

	FDesignFigmaSkippedNodeReport SkippedNode;
	SkippedNode.NodeId = TEXT("slice1");
	SkippedNode.NodeName = TEXT("Slice Overlay");
	SkippedNode.NodeType = TEXT("SLICE");
	SkippedNode.Reason = TEXT("design_only");
	Document.FigmaDiagnostics.SkippedDesignerNodes.Add(SkippedNode);

	TSharedPtr<FDesignNode> TextPathNode = MakeShared<FDesignNode>();
	TextPathNode->Id = TEXT("text_path_1");
	TextPathNode->Name = TEXT("Curved Label");
	TextPathNode->Type = EDesignNodeType::Text;
	TextPathNode->FigmaSourceNodeType = TEXT("TEXT_PATH");
	TextPathNode->FontFamily = TEXT("DefinitelyMissingReportFont");

	TSharedPtr<FDesignNode> RenderFallbackNode = MakeShared<FDesignNode>();
	RenderFallbackNode->Id = TEXT("render_1");
	RenderFallbackNode->Name = TEXT("Blurred Hero");
	RenderFallbackNode->Type = EDesignNodeType::Rectangle;
	RenderFallbackNode->FigmaSourceNodeType = TEXT("RECTANGLE");
	RenderFallbackNode->FigmaRenderFallback.ImageRef = TEXT("render_hero");
	RenderFallbackNode->FigmaRenderFallback.Scope = TEXT("full");
	RenderFallbackNode->FigmaRenderFallback.Source = TEXT("figma_render_png");
	RenderFallbackNode->FigmaRenderFallback.Reason = TEXT("foreground_blur");

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
	RootNode->Width = FDesignSize::Fixed(120.0f);
	RootNode->Height = FDesignSize::Fixed(60.0f);
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
	Request.DiagnosticDocument = &Document;

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
	Request.DiagnosticDocument = &Document;

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

	FUMGWidgetData Data;
	DesignImportPipeline::ConvertDocument(Document, Data);
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
	TestTrue(TEXT("Converted widget data preserves Figma component catalog"), Data.FigmaDocument.HasAnyComponentCatalog());

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
	TestEqual(TEXT("Instance record source node type preserved"), InstanceRecord->FigmaNode.SourceNodeType, TEXT("INSTANCE"));
	TestEqual(TEXT("Instance record componentId preserved"), InstanceRecord->FigmaNode.ComponentId, TEXT("12:34"));
	TestEqual(TEXT("Instance record componentSetId preserved"), InstanceRecord->FigmaNode.ComponentSetId, TEXT("56:78"));
	TestTrue(TEXT("Instance record variant properties preserved"), !InstanceRecord->FigmaNode.VariantPropertiesJson.IsEmpty());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeNavigationRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeNavigationRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeNavigationRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"1:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"cta_button","name":"CTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE","transition":{"type":"DISSOLVE","duration":120,"easing":{"type":"EASE_IN_AND_OUT"}}}]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"PageTwo","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"page2_title","name":"Done","type":"TEXT","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":48,"width":120,"height":32},
						 "characters":"Done",
						 "style":{"fontFamily":"Inter","fontSize":24,"fontWeight":600,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype navigation fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Prototype navigation data available"), Data.PrototypeNavigation.IsValid());
	if (!Data.PrototypeNavigation.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeNavigation");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype navigation widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated class exists"), GeneratedClass != nullptr);
	const UDesign2UMGPrototypeNavigationClassExtension* RuntimeExtension =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>() : nullptr;
	TestTrue(TEXT("Prototype runtime class extension attached"), RuntimeExtension != nullptr);
	if (!GeneratedClass || !RuntimeExtension)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetAnimation* DissolveOutgoingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_out"));
	UWidgetAnimation* DissolveIncomingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_in"));
	TestTrue(TEXT("Prototype dissolve outgoing animation generated"), DissolveOutgoingAnimation != nullptr);
	TestTrue(TEXT("Prototype dissolve incoming animation generated"), DissolveIncomingAnimation != nullptr);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();
	WidgetInstance->SetKeyboardFocus();

	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* TriggerButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("cta_button")));
	TestTrue(TEXT("Prototype switcher exists on widget instance"), Switcher != nullptr);
	TestTrue(TEXT("Prototype trigger button exists on widget instance"), TriggerButton != nullptr);
	if (!Switcher || !TriggerButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Prototype initial page index"), Switcher->GetActiveWidgetIndex(), 0);
	const float InitialOpacity = Switcher->GetRenderOpacity();
	TriggerButton->OnClicked.Broadcast();
	TickCoreTickerFor(0.03f);
	TestEqual(TEXT("Prototype dissolve keeps old page before halfway"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Prototype dissolve lowers opacity during transition"), Switcher->GetRenderOpacity() < InitialOpacity - 0.01f);
	TickCoreTickerFor(0.04f);
	TestEqual(TEXT("Prototype dissolve switches page after halfway"), Switcher->GetActiveWidgetIndex(), 1);
	TickCoreTickerFor(0.08f);
	TestEqual(TEXT("Prototype dissolve finishes on target page"), Switcher->GetActiveWidgetIndex(), 1);
	TestTrue(TEXT("Prototype dissolve restores opacity"), FMath::IsNearlyEqual(Switcher->GetRenderOpacity(), InitialOpacity, 0.001f));
	TestTrue(TEXT("Prototype dissolve restores translation"), Switcher->GetRenderTransform().Translation.IsNearlyZero());

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeSpecialActionsRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeSpecialActionsRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeSpecialActionsRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	FScopedPrototypeOpenUrlCapture OpenUrlCapture;

	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"1:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"overlay_button","name":"Overlay","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"OVERLAY","transition":{"type":"DISSOLVE","duration":120,"easing":{"type":"EASE_IN_AND_OUT"}}}]}],
						 "children":[]},
						{"id":"link_button","name":"Link","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":120,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.7,"b":0.5,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"URL","url":"https://example.com/help"}]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"OverlayPage","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"back_button","name":"Back","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.8,"g":0.3,"b":0.2,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"BACK"}]}],
						 "children":[]}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype special actions fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Prototype special action data available"), Data.PrototypeNavigation.IsValid());
	if (!Data.PrototypeNavigation.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeSpecialActions");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype special action widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetAnimation* OverlayOutgoingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_out"));
	UWidgetAnimation* OverlayIncomingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_in"));
	TestTrue(TEXT("Overlay outgoing animation generated"), OverlayOutgoingAnimation != nullptr);
	TestTrue(TEXT("Overlay incoming animation generated"), OverlayIncomingAnimation != nullptr);
	TestTrue(TEXT("Prototype special action generated class exports transition animations"),
		GeneratedClass->Animations.Num() >= 2);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype special action widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();

	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UOverlay* OverlayHost = FindPrototypeOverlayHostWidget(WidgetInstance, Data);
	UButton* OverlayButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("overlay_button")));
	UButton* BackButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("back_button")));
	UButton* LinkButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("link_button")));
	TestTrue(TEXT("Prototype special action switcher exists"), Switcher != nullptr);
	TestTrue(TEXT("Prototype overlay host exists"), OverlayHost != nullptr);
	TestTrue(TEXT("Prototype overlay trigger exists"), OverlayButton != nullptr);
	TestTrue(TEXT("Prototype back trigger exists"), BackButton != nullptr);
	TestTrue(TEXT("Prototype open link trigger exists"), LinkButton != nullptr);
	if (!Switcher || !OverlayHost || !OverlayButton || !BackButton || !LinkButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Prototype special action initial page index"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Overlay host starts collapsed"), OverlayHost->GetVisibility() == ESlateVisibility::Collapsed);
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay keeps base page active"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Overlay host becomes visible"), OverlayHost->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);
	TestTrue(TEXT("Overlay host receives overlay page"), OverlayHost->GetChildrenCount() == 1);
	const auto IsDescendantOf = [](const UWidget* Widget, const UPanelWidget* ExpectedParent)
	{
		const UWidget* Current = Widget;
		while (Current)
		{
			const UPanelWidget* Parent = Current->GetParent();
			if (!Parent)
			{
				return false;
			}
			if (Parent == ExpectedParent)
			{
				return true;
			}
			Current = Parent;
		}
		return false;
	};
	TestTrue(TEXT("Back button now lives under overlay host subtree"), IsDescendantOf(BackButton, OverlayHost));

	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back keeps base page active after closing overlay"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Overlay host collapses after back"), OverlayHost->GetVisibility() == ESlateVisibility::Collapsed);
	TestTrue(TEXT("Overlay page removed from overlay host"), OverlayHost->GetChildrenCount() == 0);
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back with empty history is a no-op"), Switcher->GetActiveWidgetIndex(), 0);

	LinkButton->OnClicked.Broadcast();
	TestEqual(TEXT("Open link keeps current page"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("Open link captures URL through runtime hook"),
		GDesign2UMGPrototypeNavigationLastOpenedUrl,
		TEXT("https://example.com/help"));

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeHoverAndPressRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeHoverAndPressRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeHoverAndPressRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"1:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"hover_button","name":"HoverCTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_HOVER"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"PageTwo","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"press_button","name":"PressBack","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.8,"g":0.3,"b":0.2,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_PRESS"},"actions":[{"type":"BACK"}]}],
						 "children":[]}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype hover/press fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Prototype hover/press data available"), Data.PrototypeNavigation.IsValid());
	if (!Data.PrototypeNavigation.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeHoverAndPress");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype hover/press widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated hover/press class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype hover/press widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();

	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* HoverButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("hover_button")));
	UButton* PressButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("press_button")));
	TestTrue(TEXT("Prototype hover/press switcher exists"), Switcher != nullptr);
	TestTrue(TEXT("Prototype hover trigger exists"), HoverButton != nullptr);
	TestTrue(TEXT("Prototype press trigger exists"), PressButton != nullptr);
	if (!Switcher || !HoverButton || !PressButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Prototype hover/press initial page index"), Switcher->GetActiveWidgetIndex(), 0);
	HoverButton->OnHovered.Broadcast();
	TestEqual(TEXT("Hover trigger switches to target page"), Switcher->GetActiveWidgetIndex(), 1);
	PressButton->OnPressed.Broadcast();
	TestEqual(TEXT("Press trigger walks back to previous page"), Switcher->GetActiveWidgetIndex(), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeAdvancedTriggersRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeAdvancedTriggersRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeAdvancedTriggersRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"1:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"drag_button","name":"DragCTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":40,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.7,"b":0.5,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_DRAG"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]}],
						 "children":[]},
						{"id":"key_down_hotspot","name":"KeyDownHotspot","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":104,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.3,"g":0.3,"b":0.8,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_KEY_DOWN","keyCodes":["ENTER"]},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]}],
						 "children":[]},
						{"id":"conditional_false_button","name":"ConditionalFalse","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":168,"width":140,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.8,"g":0.4,"b":0.1,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[
							{"type":"SET_VARIABLE","variableId":"var:isPrimary","variableValue":{"resolvedType":"BOOLEAN","value":false}},
							{"type":"CONDITIONAL","conditionalBlocks":[
								{"condition":{"type":"VARIABLE_ALIAS","id":"var:isPrimary"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]},
								{"condition":{"resolvedType":"BOOLEAN","value":true},"actions":[{"type":"NODE","destinationId":"3:1","navigation":"NAVIGATE"}]}
							]}
						 ]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"PageTwo","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"key_up_hotspot","name":"KeyUpHotspot","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":40,"width":100,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.7,"b":0.4,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_KEY_UP","keyCodes":["ESCAPE"]},"actions":[{"type":"BACK"}]}],
						 "children":[]}
					]},
					{"id":"3:1","name":"PageThree","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"page_three_title","name":"PageThreeTitle","type":"TEXT","visible":true,
						 "absoluteBoundingBox":{"x":24,"y":32,"width":140,"height":28},
						 "characters":"Fallback","style":{"fontSize":20}}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype advanced trigger fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Prototype advanced trigger data available"), Data.PrototypeNavigation.IsValid());
	if (!Data.PrototypeNavigation.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeAdvancedTriggers");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype advanced trigger widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated advanced trigger class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype advanced trigger widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();

	UDesign2UMGPrototypeNavigationUserWidgetExtension* UserExtension =
		WidgetInstance->GetExtension<UDesign2UMGPrototypeNavigationUserWidgetExtension>();
	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* DragButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("drag_button")));
	UButton* ConditionalFalseButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("conditional_false_button")));
	TestNotNull(TEXT("Prototype user extension exists"), UserExtension);
	TestTrue(TEXT("Prototype switcher exists"), Switcher != nullptr);
	TestTrue(TEXT("Prototype drag trigger exists"), DragButton != nullptr);
	TestTrue(TEXT("Prototype conditional trigger exists"), ConditionalFalseButton != nullptr);
	if (!UserExtension || !Switcher || !DragButton || !ConditionalFalseButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	auto FindBindingByTrigger = [&](const TCHAR* TriggerId) -> const FUMGPrototypeNavigationBinding*
	{
		return Data.PrototypeNavigation.ClickBindings.FindByPredicate(
			[&](const FUMGPrototypeNavigationBinding& Binding)
			{
				return Binding.TriggerWidgetId == TriggerId;
			});
	};

	const FUMGPrototypeNavigationBinding* DragBinding = FindBindingByTrigger(TEXT("drag_button"));
	TestNotNull(TEXT("Drag binding available for runtime test"), DragBinding);
	if (!DragBinding)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Advanced trigger initial page index"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Drag trigger binds native drag delegate"), DragButton->OnButtonDragDetected.IsBound());
	UserExtension->HandleNavigationRequest(*DragBinding);
	TestEqual(TEXT("Drag trigger navigates to second page"), Switcher->GetActiveWidgetIndex(), 1);

	TestTrue(TEXT("Keyboard key-up trigger handles Escape"), UserExtension->HandleKeyboardInput(EKeys::Escape, false));
	TestEqual(TEXT("Keyboard key-up trigger walks back to page one"), Switcher->GetActiveWidgetIndex(), 0);

	TestTrue(TEXT("Keyboard key-down trigger handles Enter"), UserExtension->HandleKeyboardInput(EKeys::Enter, true));
	TestEqual(TEXT("Keyboard key-down trigger navigates to page two"), Switcher->GetActiveWidgetIndex(), 1);
	TestTrue(TEXT("Keyboard key-up trigger handles Escape again"), UserExtension->HandleKeyboardInput(EKeys::Escape, false));
	TestEqual(TEXT("Keyboard key-up trigger returns to page one again"), Switcher->GetActiveWidgetIndex(), 0);

	ConditionalFalseButton->OnClicked.Broadcast();
	TestEqual(TEXT("Conditional false branch navigates to third page"), Switcher->GetActiveWidgetIndex(), 2);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeSinglePageOpenLinkRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeSinglePageOpenLinkRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeSinglePageOpenLinkRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	FScopedPrototypeOpenUrlCapture OpenUrlCapture;

	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"link_button","name":"Link","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":120,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.7,"b":0.5,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"URL","url":"https://example.com/single"}]}],
						 "children":[]}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse single-page open-link fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Single-page open-link navigation data available"), Data.PrototypeNavigation.IsValid());
	TestFalse(TEXT("Single-page open-link does not require switcher"), Data.PrototypeNavigation.RequiresSwitcher());
	if (!Data.PrototypeNavigation.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeSinglePageOpenLink");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build single-page open-link widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Single-page open-link widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();

	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* LinkButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("link_button")));
	TestTrue(TEXT("Single-page open-link trigger exists"), LinkButton != nullptr);
	TestTrue(TEXT("Single-page open-link does not synthesize switcher"), Switcher == nullptr);
	if (!LinkButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	LinkButton->OnClicked.Broadcast();
	TestEqual(TEXT("Single-page open-link captures URL through runtime hook"),
		GDesign2UMGPrototypeNavigationLastOpenedUrl,
		TEXT("https://example.com/single"));

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeSlideTransitionRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeSlideTransitionRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeSlideTransitionRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"1:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"cta_button","name":"CTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE","transition":{"type":"PUSH","direction":"LEFT","duration":160,"easing":{"type":"EASE_OUT"}}}]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"PageTwo","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"page2_title","name":"Done","type":"TEXT","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":48,"width":120,"height":32},
						 "characters":"Done",
						 "style":{"fontFamily":"Inter","fontSize":24,"fontWeight":600,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype slide fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Prototype slide data available"), Data.PrototypeNavigation.IsValid());
	if (!Data.PrototypeNavigation.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeSlideTransition");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype slide widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated slide class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetAnimation* SlideOutgoingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_out"));
	UWidgetAnimation* SlideIncomingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_in"));
	TestTrue(TEXT("Slide outgoing animation generated"), SlideOutgoingAnimation != nullptr);
	TestTrue(TEXT("Slide incoming animation generated"), SlideIncomingAnimation != nullptr);
	TestTrue(TEXT("Generated slide class exports transition animations"),
		GeneratedClass->Animations.Num() >= 2);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype slide widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();

	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* TriggerButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("cta_button")));
	TestTrue(TEXT("Prototype slide switcher exists"), Switcher != nullptr);
	TestTrue(TEXT("Prototype slide trigger button exists"), TriggerButton != nullptr);
	if (!Switcher || !TriggerButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TriggerButton->OnClicked.Broadcast();
	TickCoreTickerFor(0.04f);
	TestEqual(TEXT("Prototype slide keeps old page before halfway"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Prototype slide updates translation during transition"),
		!FMath::IsNearlyZero(Switcher->GetRenderTransform().Translation.X, 0.5f));
	TickCoreTickerFor(0.06f);
	TestEqual(TEXT("Prototype slide switches to target page"), Switcher->GetActiveWidgetIndex(), 1);
	TickCoreTickerFor(0.10f);
	TestEqual(TEXT("Prototype slide finishes on target page"), Switcher->GetActiveWidgetIndex(), 1);
	TestTrue(TEXT("Prototype slide restores translation"), Switcher->GetRenderTransform().Translation.IsNearlyZero());
	TestTrue(TEXT("Prototype slide restores opacity"), FMath::IsNearlyEqual(Switcher->GetRenderOpacity(), 1.0f, 0.001f));

	WidgetInstance->RemoveFromParent();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTargetNodeAndImageMappingWorkflowTest,
	"Design2UMG.BatchImport.FigmaTargetNodeAndImageMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTargetNodeAndImageMappingWorkflowTest::RunTest(const FString& Parameters)
{
	const FString FixtureJsonPath = GetFixturePath(TEXT("FigmaComplex.json"));
	if (FixtureJsonPath.IsEmpty() || !FPaths::FileExists(FixtureJsonPath))
	{
		AddWarning(FString::Printf(TEXT("Figma fixture not found, skipping: %s"), *FixtureJsonPath));
		return true;
	}

	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaTargetNode"));
	const FString TempImagesDir = FPaths::Combine(TempRoot, TEXT("images"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempImagesDir, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	TestTrue(TEXT("Copy figma fixture to temp location"), IFileManager::Get().Copy(*TempJsonPath, *FixtureJsonPath, true, true) == COPY_OK);
	TestTrue(TEXT("Write local avatar image"), WriteTinyPng(FPaths::Combine(TempImagesDir, TEXT("img_avatar_001.png"))));
	TestTrue(TEXT("Write local card image"), WriteTinyPng(FPaths::Combine(TempImagesDir, TEXT("img_card_bg_002.png"))));

	FDesignDocument Document;
	FString ErrorMessage;
	TestTrue(TEXT("Load targeted local Figma document"), Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage, TEXT("50:7")));
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}

	TestEqual(TEXT("Targeted document should keep one root child"), Document.Children.Num(), 1);
	if (Document.Children.Num() != 1 || !Document.Children[0].IsValid())
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	TestEqual(TEXT("Targeted root id preserved"), Document.Children[0]->Id, TEXT("50:7"));

	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Document, ImageRefs);
	TestEqual(TEXT("Filtered node keeps one mapped image ref"), ImageRefs.Num(), 1);
	TestTrue(TEXT("Mapped image ref should become local png path"),
		ImageRefs.Contains(FPaths::Combine(TempImagesDir, TEXT("img_card_bg_002.png"))));

	FUMGWidgetData Data;
	DesignImportPipeline::ConvertDocument(Document, Data);
	TestFalse(TEXT("Filtered figma target yields widget name"), Data.WidgetName.IsEmpty());

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalLuminosityBakeWorkflowTest,
	"Design2UMG.BatchImport.FigmaLocalLuminosityBake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalLuminosityBakeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaLuminosityBake"));
	const FString TempImagesDir = FPaths::Combine(TempRoot, TEXT("images"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempImagesDir, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	const FString SourcePngPath = FPaths::Combine(TempImagesDir, TEXT("img_lum.png"));
	const TArray<FColor> Pixels = {
		FColor(192, 192, 192, 255),
		FColor(224, 224, 224, 255),
		FColor(128, 128, 128, 128),
		FColor(255, 255, 255, 0),
	};
	TestTrue(TEXT("Write test png"), WriteTestPng(SourcePngPath, Pixels, 2, 2));

	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "lum_node",
					"name": "LumNode",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 80},
					"absoluteRenderBounds": {"x": 0, "y": 0, "width": 120, "height": 80},
					"fills": [
						{"type": "SOLID", "visible": true, "blendMode": "NORMAL", "color": {"r": 0.9254902, "g": 0.9019608, "b": 0.94117647, "a": 1}},
						{"type": "IMAGE", "visible": true, "blendMode": "LUMINOSITY", "imageRef": "img_lum", "scaleMode": "FIT", "scalingFactor": 0.5}
					],
					"children": []
				}]
			}]
		}
	})");
	TestTrue(TEXT("Write local figma json"), FFileHelper::SaveStringToFile(Json, *TempJsonPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage);
	TestTrue(TEXT("Load local figma luminosity fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded || Document.Children.Num() == 0)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Document.Children[0];
	TestTrue(TEXT("Local document keeps both fills"), Node->Fills.Num() >= 2);
	if (Node->Fills.Num() < 2)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	const FDesignFill& ImageFill = Node->Fills[1];
	TestTrue(TEXT("Luminosity fill is rewritten to local png path"), FPaths::FileExists(ImageFill.Image.Url));
	TestTrue(TEXT("Luminosity fill now points at derived png"), !FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(ImageFill.Image.Url), FPaths::ConvertRelativePathToFull(SourcePngPath)));
	TestTrue(TEXT("Luminosity fill blend mode baked to normal"), ImageFill.BlendMode == TEXT("NORMAL"));
	TestTrue(TEXT("Luminosity fill opacity baked into derived png"), FMath::IsNearlyEqual(ImageFill.Image.Opacity, 1.0f));
	TestTrue(TEXT("Luminosity fill scaling factor cleared"), !ImageFill.Image.bHasScalingFactor);

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalLuminosityBakeScalingFactorWorkflowTest,
	"Design2UMG.BatchImport.FigmaLocalLuminosityBakeScalingFactor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalLuminosityBakeScalingFactorWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaLuminosityBakeScaling"));
	const FString TempImagesDir = FPaths::Combine(TempRoot, TEXT("images"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempImagesDir, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	const FString SourcePngPath = FPaths::Combine(TempImagesDir, TEXT("img_lum.png"));
	const TArray<FColor> Pixels = {
		FColor(0, 0, 0, 0),
		FColor(255, 255, 255, 255),
		FColor(64, 128, 255, 255),
		FColor(255, 64, 128, 128),
	};
	TestTrue(TEXT("Write scaling test png"), WriteTestPng(SourcePngPath, Pixels, 2, 2));

	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [
					{
						"id": "lum_half",
						"name": "LumHalf",
						"type": "FRAME",
						"visible": true,
						"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 80},
						"absoluteRenderBounds": {"x": 0, "y": 0, "width": 120, "height": 80},
						"fills": [
							{"type": "SOLID", "visible": true, "blendMode": "NORMAL", "color": {"r": 0.9254902, "g": 0.9019608, "b": 0.94117647, "a": 1}},
							{"type": "IMAGE", "visible": true, "blendMode": "LUMINOSITY", "imageRef": "img_lum", "scaleMode": "FIT", "scalingFactor": 0.5}
						],
						"children": []
					},
					{
						"id": "lum_full",
						"name": "LumFull",
						"type": "FRAME",
						"visible": true,
						"absoluteBoundingBox": {"x": 140, "y": 0, "width": 120, "height": 80},
						"absoluteRenderBounds": {"x": 140, "y": 0, "width": 120, "height": 80},
						"fills": [
							{"type": "SOLID", "visible": true, "blendMode": "NORMAL", "color": {"r": 0.9254902, "g": 0.9019608, "b": 0.94117647, "a": 1}},
							{"type": "IMAGE", "visible": true, "blendMode": "LUMINOSITY", "imageRef": "img_lum", "scaleMode": "FIT", "scalingFactor": 1.0}
						],
						"children": []
					}
				]
			}]
		}
	})");
	TestTrue(TEXT("Write scaling local figma json"), FFileHelper::SaveStringToFile(Json, *TempJsonPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage);
	TestTrue(TEXT("Load local figma scaling fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded || Document.Children.Num() < 2)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	const FString HalfPath = Document.Children[0]->Fills.Num() > 1 ? Document.Children[0]->Fills[1].Image.Url : FString();
	const FString FullPath = Document.Children[1]->Fills.Num() > 1 ? Document.Children[1]->Fills[1].Image.Url : FString();
	TestTrue(TEXT("Half-scale derived png exists"), !HalfPath.IsEmpty() && FPaths::FileExists(HalfPath));
	TestTrue(TEXT("Full-scale derived png exists"), !FullPath.IsEmpty() && FPaths::FileExists(FullPath));
	TestTrue(TEXT("FIT luminosity bake should reuse the same derived path"), !HalfPath.IsEmpty() && !FullPath.IsEmpty() && FPaths::IsSamePath(HalfPath, FullPath));
	if (HalfPath.IsEmpty() || FullPath.IsEmpty() || !FPaths::IsSamePath(HalfPath, FullPath))
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	TArray<uint8> HalfBytes;
	TArray<uint8> FullBytes;
	TestTrue(TEXT("Load half-scale derived png"), FFileHelper::LoadFileToArray(HalfBytes, *HalfPath));
	TestTrue(TEXT("Load full-scale derived png"), FFileHelper::LoadFileToArray(FullBytes, *FullPath));
	TestTrue(TEXT("FIT luminosity bake should keep identical baked image content"), HalfBytes == FullBytes);

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalBlendModeBakeWorkflowTest,
	"Design2UMG.BatchImport.FigmaLocalBlendModeBake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalBlendModeBakeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaBlendModeBake"));
	const FString TempImagesDir = FPaths::Combine(TempRoot, TEXT("images"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempImagesDir, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	const FString SourcePngPath = FPaths::Combine(TempImagesDir, TEXT("img_blend.png"));
	const TArray<FColor> Pixels = {
		FColor(128, 128, 128, 255)
	};
	TestTrue(TEXT("Write blend test png"), WriteTestPng(SourcePngPath, Pixels, 1, 1));

	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [
					{
						"id": "mul_node",
						"name": "Multiply",
						"type": "FRAME",
						"visible": true,
						"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 80},
						"absoluteRenderBounds": {"x": 0, "y": 0, "width": 120, "height": 80},
						"fills": [
							{"type": "SOLID", "visible": true, "blendMode": "NORMAL", "color": {"r": 0.2509804, "g": 0.2509804, "b": 0.2509804, "a": 1}},
							{"type": "IMAGE", "visible": true, "blendMode": "MULTIPLY", "imageRef": "img_blend", "scaleMode": "FIT"}
						],
						"children": []
					},
					{
						"id": "screen_node",
						"name": "Screen",
						"type": "FRAME",
						"visible": true,
						"absoluteBoundingBox": {"x": 140, "y": 0, "width": 120, "height": 80},
						"absoluteRenderBounds": {"x": 140, "y": 0, "width": 120, "height": 80},
						"fills": [
							{"type": "SOLID", "visible": true, "blendMode": "NORMAL", "color": {"r": 0.2509804, "g": 0.2509804, "b": 0.2509804, "a": 1}},
							{"type": "IMAGE", "visible": true, "blendMode": "SCREEN", "imageRef": "img_blend", "scaleMode": "FIT"}
						],
						"children": []
					},
					{
						"id": "overlay_node",
						"name": "Overlay",
						"type": "FRAME",
						"visible": true,
						"absoluteBoundingBox": {"x": 280, "y": 0, "width": 120, "height": 80},
						"absoluteRenderBounds": {"x": 280, "y": 0, "width": 120, "height": 80},
						"fills": [
							{"type": "SOLID", "visible": true, "blendMode": "NORMAL", "color": {"r": 0.2509804, "g": 0.2509804, "b": 0.2509804, "a": 1}},
							{"type": "IMAGE", "visible": true, "blendMode": "OVERLAY", "imageRef": "img_blend", "scaleMode": "FIT"}
						],
						"children": []
					}
				]
			}]
		}
	})");
	TestTrue(TEXT("Write blend local figma json"), FFileHelper::SaveStringToFile(Json, *TempJsonPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage);
	TestTrue(TEXT("Load local figma blend fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded || Document.Children.Num() < 3)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	auto ValidateBlendNode = [this](const TSharedPtr<FDesignNode>& Node, uint8 ExpectedGray, const TCHAR* Label)
	{
		const FString KeepsFillsLabel = FString::Printf(TEXT("%s keeps both fills"), Label);
		TestTrue(*KeepsFillsLabel, Node->Fills.Num() >= 2);
		if (Node->Fills.Num() < 2)
		{
			return false;
		}

		const FDesignFill& ImageFill = Node->Fills[1];
		const FString DerivedExistsLabel = FString::Printf(TEXT("%s derived png exists"), Label);
		const FString BlendNormalLabel = FString::Printf(TEXT("%s blend mode baked to normal"), Label);
		TestTrue(*DerivedExistsLabel, FPaths::FileExists(ImageFill.Image.Url));
		TestTrue(*BlendNormalLabel, ImageFill.BlendMode == TEXT("NORMAL"));

		TArray<FColor> DecodedPixels;
		int32 Width = 0;
		int32 Height = 0;
		const FString DecodeLabel = FString::Printf(TEXT("%s derived png decodes"), Label);
		TestTrue(*DecodeLabel, ReadTestPng(ImageFill.Image.Url, DecodedPixels, Width, Height));
		if (DecodedPixels.Num() == 0)
		{
			return false;
		}

		const FColor Pixel = DecodedPixels[0];
		const FString PixelLabel = FString::Printf(TEXT("%s blended pixel matches expected gray"), Label);
		return TestTrue(*PixelLabel,
			FMath::Abs(Pixel.R - ExpectedGray) <= 2
			&& FMath::Abs(Pixel.G - ExpectedGray) <= 2
			&& FMath::Abs(Pixel.B - ExpectedGray) <= 2);
	};

	const bool bMultiplyOk = ValidateBlendNode(Document.Children[0], 32, TEXT("Multiply"));
	const bool bScreenOk = ValidateBlendNode(Document.Children[1], 160, TEXT("Screen"));
	const bool bOverlayOk = ValidateBlendNode(Document.Children[2], 64, TEXT("Overlay"));

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return bMultiplyOk && bScreenOk && bOverlayOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenLocalBlendModeBakeWorkflowTest,
	"Design2UMG.BatchImport.PenLocalBlendModeBake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenLocalBlendModeBakeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("PenBlendModeBake"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempRoot, true);

	const FString TempPenPath = FPaths::Combine(TempRoot, TEXT("design.pen"));
	const FString SourcePngPath = FPaths::Combine(TempRoot, TEXT("img_blend.png"));
	const TArray<FColor> Pixels = {
		FColor(128, 128, 128, 255)
	};
	TestTrue(TEXT("Write pen blend test png"), WriteTestPng(SourcePngPath, Pixels, 1, 1));

	FString EscapedImagePath = FPaths::ConvertRelativePathToFull(SourcePngPath);
	EscapedImagePath.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	const FString Json = FString::Printf(TEXT(R"({
		"children": [
			{
				"id": "mul_node",
				"type": "frame",
				"width": 120,
				"height": 80,
				"fill": [
					{"type": "color", "blendMode": "NORMAL", "color": "#404040"},
					{"type": "image", "blendMode": "MULTIPLY", "url": "%s", "mode": "fit"}
				]
			},
			{
				"id": "screen_node",
				"type": "frame",
				"x": 140,
				"width": 120,
				"height": 80,
				"fill": [
					{"type": "color", "blendMode": "NORMAL", "color": "#404040"},
					{"type": "image", "blendMode": "SCREEN", "url": "%s", "mode": "fit"}
				]
			},
			{
				"id": "overlay_node",
				"type": "frame",
				"x": 280,
				"width": 120,
				"height": 80,
				"fill": [
					{"type": "color", "blendMode": "NORMAL", "color": "#404040"},
					{"type": "image", "blendMode": "OVERLAY", "url": "%s", "mode": "fit"}
				]
			}
		]
	})"), *EscapedImagePath, *EscapedImagePath, *EscapedImagePath);
	TestTrue(TEXT("Write pen blend fixture"), FFileHelper::SaveStringToFile(Json, *TempPenPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = DesignImportPipeline::LoadPenDocument(TempPenPath, FString(), Document, &ErrorMessage);
	TestTrue(TEXT("Load pen blend fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	FUMGWidgetData Data;
	DesignImportPipeline::ConvertDocument(Document, Data);
	TestTrue(TEXT("Pen blend conversion keeps all top-level nodes"), Document.Children.Num() == 3 && Data.RootWidget.Children.Num() == 3);
	if (Document.Children.Num() < 3)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	const FString FullSourcePath = FPaths::ConvertRelativePathToFull(SourcePngPath);
	auto ValidateBlendNode = [this, &FullSourcePath](const TSharedPtr<FDesignNode>& Node, const uint8 ExpectedGray, const TCHAR* Label)
	{
		const FString KeepsFillsLabel = FString::Printf(TEXT("%s keeps both fills"), Label);
		TestTrue(*KeepsFillsLabel, Node.IsValid() && Node->Fills.Num() >= 2);
		if (!Node.IsValid() || Node->Fills.Num() < 2)
		{
			return false;
		}

		const FDesignFill& ImageFill = Node->Fills[1];
		const FString DerivedExistsLabel = FString::Printf(TEXT("%s derived png exists"), Label);
		const FString DerivedPathLabel = FString::Printf(TEXT("%s derived png differs from source"), Label);
		const FString BlendNormalLabel = FString::Printf(TEXT("%s blend mode baked to normal"), Label);
		const FString OpacityLabel = FString::Printf(TEXT("%s image opacity baked into texture"), Label);
		const FString ScalingFactorLabel = FString::Printf(TEXT("%s scaling factor cleared"), Label);
		TestTrue(*DerivedExistsLabel, FPaths::FileExists(ImageFill.Image.Url));
		TestTrue(*DerivedPathLabel, !FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(ImageFill.Image.Url), FullSourcePath));
		TestTrue(*BlendNormalLabel, ImageFill.BlendMode == TEXT("NORMAL"));
		TestTrue(*OpacityLabel, FMath::IsNearlyEqual(ImageFill.Image.Opacity, 1.0f));
		TestTrue(*ScalingFactorLabel, !ImageFill.Image.bHasScalingFactor);

		TArray<FColor> DecodedPixels;
		int32 Width = 0;
		int32 Height = 0;
		const FString DecodeLabel = FString::Printf(TEXT("%s derived png decodes"), Label);
		TestTrue(*DecodeLabel, ReadTestPng(ImageFill.Image.Url, DecodedPixels, Width, Height));
		if (DecodedPixels.Num() == 0)
		{
			return false;
		}

		const FColor Pixel = DecodedPixels[0];
		const FString PixelLabel = FString::Printf(TEXT("%s blended pixel matches expected gray"), Label);
		return TestTrue(*PixelLabel,
			FMath::Abs(Pixel.R - ExpectedGray) <= 2
			&& FMath::Abs(Pixel.G - ExpectedGray) <= 2
			&& FMath::Abs(Pixel.B - ExpectedGray) <= 2);
	};

	const bool bMultiplyOk = ValidateBlendNode(Document.Children[0], 32, TEXT("Pen Multiply"));
	const bool bScreenOk = ValidateBlendNode(Document.Children[1], 160, TEXT("Pen Screen"));
	const bool bOverlayOk = ValidateBlendNode(Document.Children[2], 64, TEXT("Pen Overlay"));

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return bMultiplyOk && bScreenOk && bOverlayOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalLayerBlurBakeWorkflowTest,
	"Design2UMG.BatchImport.FigmaLocalLayerBlurBake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalLayerBlurBakeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaLayerBlurBake"));
	const FString TempImagesDir = FPaths::Combine(TempRoot, TEXT("images"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempImagesDir, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	const FString SourcePngPath = FPaths::Combine(TempImagesDir, TEXT("avatar.png"));
	const TArray<FColor> Pixels = {
		FColor(32, 64, 160, 255),
		FColor(64, 128, 224, 255),
		FColor(160, 96, 32, 255),
		FColor(224, 192, 160, 255),
	};
	TestTrue(TEXT("Write blur test png"), WriteTestPng(SourcePngPath, Pixels, 2, 2));

	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "root_frame",
					"name": "RootFrame",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 160, "height": 120},
					"absoluteRenderBounds": {"x": 0, "y": 0, "width": 160, "height": 120},
					"children": [{
						"id": "blur_avatar",
						"name": "BlurAvatar",
						"type": "ELLIPSE",
						"visible": true,
						"absoluteBoundingBox": {"x": 24, "y": 20, "width": 26, "height": 26},
						"absoluteRenderBounds": {"x": 2.25, "y": -1.75, "width": 69.5, "height": 69.5},
						"fills": [
							{"type": "IMAGE", "visible": true, "blendMode": "NORMAL", "imageRef": "avatar", "scaleMode": "FIT"},
							{"type": "IMAGE", "visible": true, "blendMode": "NORMAL", "imageRef": "avatar", "scaleMode": "FILL"}
						],
						"effects": [{
							"type": "LAYER_BLUR",
							"visible": true,
							"blendMode": "NORMAL",
							"radius": 21.75
						}],
						"children": []
					}]
				}]
			}]
		}
	})");
	TestTrue(TEXT("Write blur local figma json"), FFileHelper::SaveStringToFile(Json, *TempJsonPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage);
	TestTrue(TEXT("Load local figma blur fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded || Document.Children.Num() == 0)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	TFunction<TSharedPtr<FDesignNode>(const TArray<TSharedPtr<FDesignNode>>&)> FindBlurAvatar =
		[&FindBlurAvatar](const TArray<TSharedPtr<FDesignNode>>& Nodes) -> TSharedPtr<FDesignNode>
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}
			if (Node->Id == TEXT("blur_avatar"))
			{
				return Node;
			}
			if (TSharedPtr<FDesignNode> Found = FindBlurAvatar(Node->Children))
			{
				return Found;
			}
		}
		return nullptr;
	};

	const TSharedPtr<FDesignNode> Avatar = FindBlurAvatar(Document.Children);
	TestTrue(TEXT("Find blur avatar node"), Avatar.IsValid());
	if (!Avatar.IsValid())
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	TestTrue(TEXT("Blur avatar keeps both fills"), Avatar->Fills.Num() == 2);
	TestTrue(TEXT("Blur avatar width stays in layout space"), FMath::IsNearlyEqual(Avatar->Width.Value, 26.0f, 0.01f));
	TestTrue(TEXT("Blur avatar height stays in layout space"), FMath::IsNearlyEqual(Avatar->Height.Value, 26.0f, 0.01f));
	TestTrue(TEXT("Blur avatar X stays in layout space"), FMath::IsNearlyEqual(Avatar->X, 24.0f, 0.01f));
	TestTrue(TEXT("Blur avatar Y stays in layout space"), FMath::IsNearlyEqual(Avatar->Y, 20.0f, 0.01f));
	TestTrue(TEXT("Blur effect should be baked out"), Avatar->Effects.Num() == 0);
	TestTrue(TEXT("First blurred fill rewrites to derived png"), !Avatar->Fills[0].Image.Url.IsEmpty() && FPaths::FileExists(Avatar->Fills[0].Image.Url));
	TestTrue(TEXT("Second blurred fill rewrites to derived png"), !Avatar->Fills[1].Image.Url.IsEmpty() && FPaths::FileExists(Avatar->Fills[1].Image.Url));
	TestTrue(TEXT("Blur bake should not reuse original source path"), !FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(Avatar->Fills[0].Image.Url), FPaths::ConvertRelativePathToFull(SourcePngPath)));

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
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
	const FString DummyJson = TEXT("{}");
	TArray<FString> Warnings;
	TSharedPtr<SDesign2UMGPreviewWindow> PreviewWindow = SNew(SDesign2UMGPreviewWindow)
		.OriginalJson(DummyJson)
		.OutputPath(TEXT("/Game/Test"))
		.Warnings(Warnings);
	TestTrue(TEXT("Preview window instantiated"), PreviewWindow.IsValid());
	PreviewWindow.Reset();
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
	Root->Layout = EDesignLayout::Vertical;
	Root->Width = FDesignSize::Fixed(320.0f);
	Root->Height = FDesignSize::Fit();
	Root->bHasWidth = true;
	const TSharedPtr<FDesignNode> Row = MakeShared<FDesignNode>();
	Row->Id = TEXT("fit-min-row");
	Row->Name = TEXT("FitMinRow");
	Row->Type = EDesignNodeType::Frame;
	Row->Layout = EDesignLayout::Vertical;
	Row->Width = FDesignSize::Fixed(320.0f);
	Row->Height = FDesignSize::Fit(44.0f);
	Row->bHasWidth = true;
	Row->bHasHeight = true;
	const TSharedPtr<FDesignNode> Content = MakeShared<FDesignNode>();
	Content->Id = TEXT("fit-min-content");
	Content->Name = TEXT("FitMinContent");
	Content->Type = EDesignNodeType::Frame;
	Content->Layout = EDesignLayout::Vertical;
	Content->Width = FDesignSize::Fixed(320.0f);
	Content->Height = FDesignSize::Fixed(56.0f);
	Content->bHasWidth = true;
	Content->bHasHeight = true;
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
	Root->Layout = EDesignLayout::Vertical;
	Root->Width = FDesignSize::Fixed(160.0f);
	Root->Height = FDesignSize::Fit();
	Root->bHasWidth = true;
	const TSharedPtr<FDesignNode> HeaderCell = MakeShared<FDesignNode>();
	HeaderCell->Id = TEXT("header-cell");
	HeaderCell->Name = TEXT("HeaderCell");
	HeaderCell->Type = EDesignNodeType::Frame;
	HeaderCell->Layout = EDesignLayout::Horizontal;
	HeaderCell->Width = FDesignSize::Fixed(160.0f);
	HeaderCell->Height = FDesignSize::Fit(44.0f);
	HeaderCell->bHasWidth = true;
	HeaderCell->bHasHeight = true;
	HeaderCell->Padding.Top = 12.0f;
	HeaderCell->Padding.Right = 12.0f;
	HeaderCell->Padding.Bottom = 12.0f;
	HeaderCell->Padding.Left = 12.0f;
	HeaderCell->bHasPadding = true;
	HeaderCell->AlignItems = EDesignAlignItems::Center;
	const TSharedPtr<FDesignNode> HeaderText = MakeShared<FDesignNode>();
	HeaderText->Id = TEXT("header-text");
	HeaderText->Name = TEXT("HeaderText");
	HeaderText->Type = EDesignNodeType::Text;
	HeaderText->Width = FDesignSize::Fill();
	HeaderText->Height = FDesignSize::Fit();
	HeaderText->Content = TEXT("Status");
	HeaderText->FontFamily = TEXT("Inter");
	HeaderText->FontSize = 14.0f;
	HeaderText->FontWeight = TEXT("normal");
	HeaderText->LineHeight = 1.4285714285714286f;
	HeaderText->TextGrowth = EDesignTextGrowth::FixedWidth;
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
#endif // WITH_AUTOMATION_WORKER
