// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGFigmaMetadataExtensions.h"
#include "WidgetMetadataCollector.h"
#include "Design2UMGLocalFigmaImport.h"
#include "DesignFontImportService.h"
#include "Design2UMGTestUtils.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"
#include "FontResolver.h"
#include "FigmaParser.h"
#include "PenParser.h"
#include "UMGGenerator.h"
#include "UMGWidgetUtils.h"
#include "PreBuildResourceNormalizer.h"
#include "PenMCPBridge.h"
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
#include "Components/ExpandableArea.h"
#include "Components/GridSlot.h"
#include "Components/NamedSlotInterface.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Layout/Children.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "RHI.h"
#include "UMGLayoutProjection.h"
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

	const FMargin WelcomeBodyPadding = WelcomeBody->WidgetStyle.Padding.Get(FMargin());
	const FMargin ProTipsPadding = ProTips->WidgetStyle.Padding.Get(FMargin());
	AddInfo(FString::Printf(
		TEXT("Welcome body summary: font=%.1f source=%.1f bounds=(%.1f,%.1f,%.1f,%.1f) wrap=%d wrapAt=%.1f lineHeight=%.3f fontFamily=%s typeface=%s padding=(%.1f,%.1f,%.1f,%.1f)"),
		WelcomeBody->TextStyle.FontSize,
		WelcomeBody->TextStyle.SourceFontSize,
		WelcomeBody->Bounds.X,
		WelcomeBody->Bounds.Y,
		WelcomeBody->Bounds.W,
		WelcomeBody->Bounds.H,
		WelcomeBody->Text.bAutoWrapText.Get(false) ? 1 : 0,
		WelcomeBody->Text.WrapTextAt.Get(0.0f),
		WelcomeBody->Text.LineHeightPercentage.Get(0.0f),
		*WelcomeBody->Text.FontFamily.Get(FString()),
		*WelcomeBody->Text.TypefaceFontName.Get(FString()),
		WelcomeBodyPadding.Left,
		WelcomeBodyPadding.Top,
		WelcomeBodyPadding.Right,
		WelcomeBodyPadding.Bottom));
	AddInfo(FString::Printf(
		TEXT("Pro tips summary: font=%.1f source=%.1f bounds=(%.1f,%.1f,%.1f,%.1f) wrap=%d wrapAt=%.1f lineHeight=%.3f fontFamily=%s typeface=%s padding=(%.1f,%.1f,%.1f,%.1f)"),
		ProTips->TextStyle.FontSize,
		ProTips->TextStyle.SourceFontSize,
		ProTips->Bounds.X,
		ProTips->Bounds.Y,
		ProTips->Bounds.W,
		ProTips->Bounds.H,
		ProTips->Text.bAutoWrapText.Get(false) ? 1 : 0,
		ProTips->Text.WrapTextAt.Get(0.0f),
		ProTips->Text.LineHeightPercentage.Get(0.0f),
		*ProTips->Text.FontFamily.Get(FString()),
		*ProTips->Text.TypefaceFontName.Get(FString()),
		ProTipsPadding.Left,
		ProTipsPadding.Top,
		ProTipsPadding.Right,
		ProTipsPadding.Bottom));

	constexpr float ExpectedCompiledRubikFontSize = 20.0f;
	TestTrue(TEXT("Welcome body keeps the authoritative 20px compiled Rubik size"),
		FMath::IsNearlyEqual(WelcomeBody->TextStyle.FontSize, ExpectedCompiledRubikFontSize, 0.01f));
	TestTrue(TEXT("Pro tips keeps the authoritative 20px compiled Rubik size"),
		FMath::IsNearlyEqual(ProTips->TextStyle.FontSize, ExpectedCompiledRubikFontSize, 0.01f));
	TestTrue(TEXT("Welcome body and Pro tips stay on the same authoritative Rubik size"),
		FMath::IsNearlyEqual(WelcomeBody->TextStyle.FontSize, ProTips->TextStyle.FontSize, 0.01f));
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
	TestTrue(TEXT("Converted widget data preserves Figma component catalog"), Data.SourceDocument().HasAnyComponentCatalog());

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetadataCollectorExactSnapshotTest,
	"Design2UMG.WidgetBlueprint.MetadataCollector.ExactSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMetadataCollectorExactSnapshotTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Id = TEXT("inst1");
	Node.Name = TEXT("NavItem");
	Node.Type = TEXT("INSTANCE");
	Node.MutableSource().SourceNodeType = TEXT("INSTANCE");
	Node.MutableSource().ComponentId = TEXT("12:34");
	Node.MutableSource().ComponentSetId = TEXT("56:78");
	Node.MutableSource().VariantPropertiesJson = TEXT("{\"Active\":\"True\"}");

	UTextBlock* Widget = NewObject<UTextBlock>(GetTransientPackage(), NAME_None, RF_Transient);
	Widget->Rename(TEXT("NavItem_0"));

	TArray<FDesign2UMGFigmaWidgetMetadataRecord> Records;
	FWidgetMetadataCollector::TryAppend(Widget, Node, &Records);

	TestEqual(TEXT("Exactly one record appended"), Records.Num(), 1);
	if (Records.Num() != 1) return false;

	const FDesign2UMGFigmaWidgetMetadataRecord& R = Records[0];

	// Normalize VariantPropertiesJson key order before snapshot comparison
	auto NormalizeJson = [](const FString& Raw) -> FString
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			return Raw;
		}
		TArray<FString> Keys;
		for (const auto& Pair : Obj->Values) { Keys.Add(Pair.Key); }
		Keys.Sort();
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		Writer->WriteObjectStart();
		for (const FString& Key : Keys)
		{
			Writer->WriteValue(Key, Obj->GetStringField(Key));
		}
		Writer->WriteObjectEnd();
		Writer->Close();
		return Out;
	};

	const FString ActualSnapshot = FString::Printf(
		TEXT("WidgetId=%s|WidgetName=%s|SourceNodeType=%s|ComponentId=%s|ComponentSetId=%s|VariantProperties=%s"),
		*R.WidgetId,
		*R.WidgetName.ToString(),
		*R.SourceNode.SourceNodeType,
		*R.SourceNode.ComponentId,
		*R.SourceNode.ComponentSetId,
		*NormalizeJson(R.SourceNode.VariantPropertiesJson));

	const FString ExpectedSnapshot = FString::Printf(
		TEXT("WidgetId=%s|WidgetName=%s|SourceNodeType=%s|ComponentId=%s|ComponentSetId=%s|VariantProperties=%s"),
		TEXT("inst1"),
		*Widget->GetFName().ToString(),
		TEXT("INSTANCE"),
		TEXT("12:34"),
		TEXT("56:78"),
		*NormalizeJson(TEXT("{\"Active\":\"True\"}")));

	TestEqual(TEXT("Exact metadata snapshot matches"), ActualSnapshot, ExpectedSnapshot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetadataCollectorNoRecordWithoutSemanticsTest,
	"Design2UMG.WidgetBlueprint.MetadataCollector.NoRecordWithoutSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMetadataCollectorNoRecordWithoutSemanticsTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Id = TEXT("frame1");
	Node.Name = TEXT("Frame");
	Node.Type = TEXT("FRAME");
	// Source has no component semantics (empty SourceNodeType/ComponentId)

	UTextBlock* Widget = NewObject<UTextBlock>(GetTransientPackage(), NAME_None, RF_Transient);
	Widget->Rename(TEXT("Frame_0"));

	TArray<FDesign2UMGFigmaWidgetMetadataRecord> Records;
	FWidgetMetadataCollector::TryAppend(Widget, Node, &Records);

	TestEqual(TEXT("No record appended for non-instance node"), Records.Num(), 0);
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

	FString SerializeWidgetTreeWithNamedSlots(const UWidget* Widget, int32 Depth = 0)
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
			for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
			{
				Result += SerializeWidgetTreeWithNamedSlots(Panel->GetChildAt(ChildIndex), Depth + 1);
			}
		}

		if (const INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(const_cast<UWidget*>(Widget)))
		{
			TArray<FName> SlotNames;
			NamedSlotHost->GetSlotNames(SlotNames);
			for (const FName& SlotName : SlotNames)
			{
				if (const UWidget* SlotContent = NamedSlotHost->GetContentForSlot(SlotName))
				{
					Result += FString::Printf(TEXT("%s<NamedSlot:%s>\n"),
						*FString::ChrN((Depth + 1) * 2, TEXT(' ')),
						*SlotName.ToString());
					Result += SerializeWidgetTreeWithNamedSlots(SlotContent, Depth + 2);
				}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNamedSlotOverflowWrapStructureStableTest,
	"Design2UMG.TreeBuilder.NamedSlotOverflowWrapStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FNamedSlotOverflowWrapStructureStableTest::RunTest(const FString& Parameters)
{
	UWidgetBlueprint* Blueprint = NewObject<UWidgetBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	TestNotNull(TEXT("Transient blueprint exists"), Blueprint);
	if (!Blueprint)
	{
		return false;
	}

	Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, UWidgetTree::StaticClass(), NAME_None, RF_Transient);
	TestNotNull(TEXT("Transient widget tree exists"), Blueprint->WidgetTree.Get());
	if (!Blueprint->WidgetTree)
	{
		return false;
	}

	FUMGWidgetNode RootNode;
	RootNode.Type = TEXT("CanvasPanel");
	RootNode.Id = TEXT("slot_root");
	RootNode.Name = TEXT("slot_root");
	RootNode.Bounds = {0.0f, 0.0f, 400.0f, 240.0f};

	FUMGWidgetNode HostNode;
	HostNode.Type = TEXT("ExpandableArea");
	HostNode.Id = TEXT("slot_host");
	HostNode.Name = TEXT("slot_host");
	HostNode.Bounds = {0.0f, 0.0f, 320.0f, 180.0f};

	FUMGWidgetNode HeaderNode;
	HeaderNode.Type = TEXT("TextBlock");
	HeaderNode.Id = TEXT("slot_header");
	HeaderNode.Name = TEXT("slot_header");
	HeaderNode.Bounds = {0.0f, 0.0f, 120.0f, 24.0f};
	HeaderNode.Text.Text = TEXT("Header");
	HeaderNode.WidgetStyle.SlotName = TEXT("Header");

	FUMGWidgetNode BodyPrimaryNode;
	BodyPrimaryNode.Type = TEXT("TextBlock");
	BodyPrimaryNode.Id = TEXT("slot_body_primary");
	BodyPrimaryNode.Name = TEXT("slot_body_primary");
	BodyPrimaryNode.Bounds = {0.0f, 28.0f, 180.0f, 24.0f};
	BodyPrimaryNode.Text.Text = TEXT("BodyPrimary");
	BodyPrimaryNode.WidgetStyle.SlotName = TEXT("Body");

	FUMGWidgetNode BodySecondaryNode;
	BodySecondaryNode.Type = TEXT("TextBlock");
	BodySecondaryNode.Id = TEXT("slot_body_secondary");
	BodySecondaryNode.Name = TEXT("slot_body_secondary");
	BodySecondaryNode.Bounds = {0.0f, 56.0f, 180.0f, 24.0f};
	BodySecondaryNode.Text.Text = TEXT("BodySecondary");
	BodySecondaryNode.WidgetStyle.SlotName = TEXT("Body");
	BodySecondaryNode.Slot.Padding = FMargin(3.0f, 0.0f, 0.0f, 7.0f);

	HostNode.Children.Add(HeaderNode);
	HostNode.Children.Add(BodyPrimaryNode);
	HostNode.Children.Add(BodySecondaryNode);
	RootNode.Children.Add(HostNode);

	UWidget* RootWidget = FUMGWidgetUtils::CreateWidgetRecursive(Blueprint->WidgetTree, RootNode);
	TestNotNull(TEXT("Root widget created"), RootWidget);
	if (!RootWidget)
	{
		return false;
	}
	Blueprint->WidgetTree->RootWidget = RootWidget;

	const UPanelWidget* RootPanel = Cast<UPanelWidget>(RootWidget);
	TestNotNull(TEXT("Root panel exists"), RootPanel);
	if (!RootPanel || RootPanel->GetChildrenCount() == 0)
	{
		return false;
	}

	UExpandableArea* HostWidget = Cast<UExpandableArea>(RootPanel->GetChildAt(0));
	TestNotNull(TEXT("ExpandableArea host created"), HostWidget);
	if (!HostWidget)
	{
		return false;
	}

	UWidget* HeaderContent = HostWidget->GetContentForSlot(TEXT("Header"));
	UWidget* BodyContent = HostWidget->GetContentForSlot(TEXT("Body"));
	TestNotNull(TEXT("Header slot received content"), HeaderContent);
	TestNotNull(TEXT("Body slot received content"), BodyContent);

	const UVerticalBox* BodyWrap = Cast<UVerticalBox>(BodyContent);
	TestNotNull(TEXT("Overflow body slot wrapped in VerticalBox"), BodyWrap);
	if (!HeaderContent || !BodyWrap)
	{
		return false;
	}

	TestEqual(TEXT("Header slot keeps explicit target"), HeaderContent->GetName(), FString(TEXT("slot_header")));
	TestEqual(TEXT("Wrap box keeps stable generated name"), BodyWrap->GetName(), FString(TEXT("slot_host_SlotWrap_Body")));
	TestEqual(TEXT("Body wrap child count"), BodyWrap->GetChildrenCount(), 2);
	TestEqual(TEXT("First wrapped child order"), BodyWrap->GetChildAt(0)->GetName(), FString(TEXT("slot_body_primary")));
	TestEqual(TEXT("Second wrapped child order"), BodyWrap->GetChildAt(1)->GetName(), FString(TEXT("slot_body_secondary")));

	const UPanelSlot* WrappedSecondSlot = BodyWrap->GetChildAt(1)->Slot;
	TestNotNull(TEXT("Wrapped second child has panel slot"), WrappedSecondSlot);
	if (const UVerticalBoxSlot* WrappedVBoxSlot = Cast<UVerticalBoxSlot>(WrappedSecondSlot))
	{
		const FMargin Padding = WrappedVBoxSlot->GetPadding();
		TestTrue(TEXT("Wrapped second child padding is preserved"), FMath::IsNearlyEqual(Padding.Left, 3.0f, 0.01f) && FMath::IsNearlyEqual(Padding.Bottom, 7.0f, 0.01f));
	}
	else
	{
		AddError(TEXT("Wrapped second child should use VerticalBoxSlot"));
		return false;
	}

	const FString Actual = SerializeWidgetTreeWithNamedSlots(RootWidget).TrimEnd();
	const FString Expected = TEXT(
		"CanvasPanel:slot_root[none]\n"
		"  ExpandableArea:slot_host[CanvasPanelSlot]\n"
		"    <NamedSlot:Header>\n"
		"      TextBlock:slot_header[none]\n"
		"    <NamedSlot:Body>\n"
		"      VerticalBox:slot_host_SlotWrap_Body[none]\n"
		"        TextBlock:slot_body_primary[VerticalBoxSlot]\n"
		"        TextBlock:slot_body_secondary[VerticalBoxSlot]");
	TestEqual(TEXT("Named-slot overflow structure snapshot matches"), Actual, Expected);

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
	{
		FDesignPadding P; P.Top = P.Right = P.Bottom = P.Left = 12.0f;
		HeaderCell->LayoutData.SetPadding(P);
	}
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
		S.PathGeometry = Node.Path.Geometry.Get(FString());
		S.bHasGeometry = Node.Path.Geometry.IsSet();
		S.MaterialRef = Node.Brush.MaterialRef.Get(FString());
		S.bHasMaterialRef = Node.Brush.MaterialRef.IsSet();
		S.TexturePath = Node.Brush.Image.TexturePath.Get(FString());
		S.bHasTexturePath = Node.Brush.Image.TexturePath.IsSet();
		return S;
	}

	FUMGWidgetNode MakePathImageNode(const FString& Id, const FString& Geometry)
	{
		FUMGWidgetNode Node;
		Node.Type = TEXT("Image");
		Node.Id = Id;
		Node.Name = Id;
		if (!Geometry.IsEmpty())
		{
			Node.Path.Geometry = Geometry;
		}
		Node.Path.FillRule = TEXT("nonzero");
		Node.Path.FillColor = FLinearColor::White;
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
		if (!MaterialRef.IsEmpty())
		{
			Node.Brush.MaterialRef = MaterialRef;
		}
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
	FDesignPixelBounds NB;
	NB.X = Node.Bounds.X / DesignSize.X;
	NB.Y = Node.Bounds.Y / DesignSize.Y;
	NB.W = Node.Bounds.W / DesignSize.X;
	NB.H = Node.Bounds.H / DesignSize.Y;
	const FUMGSlotData Expected = LayoutCalculator::CalculateSlot(NB, Ctx);

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
