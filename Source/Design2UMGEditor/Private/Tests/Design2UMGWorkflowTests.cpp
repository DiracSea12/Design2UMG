// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGLocalFigmaImport.h"
#include "Design2UMGPrototypeNavigationExtensions.h"
#include "Design2UMGTestUtils.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"
#include "FigmaParser.h"
#include "LayoutFixer.h"
#include "UMGGenerator.h"
#include "PenMCPBridge.h"
#include "SDesign2UMGPreviewWindow.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Editor.h"
#include "Components/Button.h"
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

namespace
{
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

	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(WidgetInstance->WidgetTree->FindWidget(TEXT("__d2u_prototype_switcher")));
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

	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(WidgetInstance->WidgetTree->FindWidget(TEXT("__d2u_prototype_switcher")));
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
