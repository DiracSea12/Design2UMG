// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGLocalFigmaImport.h"
#include "Design2UMGTestUtils.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"
#include "FigmaParser.h"
#include "LayoutFixer.h"
#include "PenMCPBridge.h"
#include "SDesign2UMGPreviewWindow.h"
#include "AssetRegistry/AssetRegistryModule.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalRenderFallbackCropWorkflowTest,
	"Design2UMG.BatchImport.FigmaLocalRenderFallbackCrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalRenderFallbackCropWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaRenderFallbackCrop"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempRoot, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	const FString ReferencePngPath = FPaths::Combine(TempRoot, TEXT("Reference.png"));
	const TArray<FColor> Pixels = {
		FColor(10, 20, 30, 255), FColor(20, 30, 40, 255), FColor(30, 40, 50, 255), FColor(40, 50, 60, 255),
		FColor(50, 60, 70, 255), FColor(255, 0, 0, 255), FColor(0, 255, 0, 255), FColor(80, 90, 100, 255),
		FColor(90, 100, 110, 255), FColor(0, 0, 255, 255), FColor(255, 255, 0, 255), FColor(120, 130, 140, 255),
		FColor(130, 140, 150, 255), FColor(140, 150, 160, 255), FColor(150, 160, 170, 255), FColor(160, 170, 180, 255),
	};
	TestTrue(TEXT("Write top-level reference png"), WriteTestPng(ReferencePngPath, Pixels, 4, 4));

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
					"id": "root",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 4, "height": 4},
					"absoluteRenderBounds": {"x": 0, "y": 0, "width": 4, "height": 4},
					"children": [{
						"id": "render_crop",
						"name": "Image",
						"type": "FRAME",
						"visible": true,
						"absoluteBoundingBox": {"x": 1, "y": 1, "width": 2, "height": 2},
						"absoluteRenderBounds": {"x": 1, "y": 1, "width": 2, "height": 2},
						"fills": [
							{"type":"SOLID","visible":true,"blendMode":"NORMAL","color":{"r":0.2,"g":0.2,"b":0.2,"a":1}},
							{"type":"IMAGE","visible":true,"blendMode":"LUMINOSITY","imageRef":"img_orig","scaleMode":"FIT","filters":{"contrast":0.25}}
						],
						"children": []
					}]
				}]
			}]
		}
	})");
	TestTrue(TEXT("Write local figma json with risky leaf"), FFileHelper::SaveStringToFile(Json, *TempJsonPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage);
	TestTrue(TEXT("Load local figma crop fallback fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded || Document.Children.Num() == 0 || Document.Children[0]->Children.Num() == 0)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Document.Children[0]->Children[0];
	const FString FallbackPath = Node->Metadata.FindRef(TEXT("figma.renderFallbackImageRef"));
	TestTrue(TEXT("Local crop fallback metadata remapped to local png path"), !FallbackPath.IsEmpty() && FPaths::FileExists(FallbackPath));
	TestTrue(TEXT("Local crop fallback source marked"), Node->Metadata.FindRef(TEXT("figma.renderFallbackSource")) == TEXT("figma_reference_crop"));

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("render_crop"));
	TestTrue(TEXT("Crop fallback widget exists"), Widget != nullptr);
	if (!Widget)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	TestEqual(TEXT("Crop fallback widget type"), Widget->Type, TEXT("Image"));
	TestEqual(TEXT("Crop fallback widget texture path"), GetWidgetProperty(*Widget, TEXT("TexturePath")), FallbackPath);

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
	FString HeightOverride;
	const bool bHasHeightOverride = RowSizeBox->Properties.TryGetString(TEXT("HeightOverride"), HeightOverride) && !HeightOverride.IsEmpty();
	FString MinDesiredHeight;
	const bool bHasMinDesiredHeight = RowSizeBox->Properties.TryGetString(TEXT("MinDesiredHeight"), MinDesiredHeight) && !MinDesiredHeight.IsEmpty();
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
	FString HeightOverride;
	const bool bHasHeightOverride = HeaderCellSizeBox->Properties.TryGetString(TEXT("HeightOverride"), HeightOverride) && !HeightOverride.IsEmpty();
	FString MinDesiredHeight;
	const bool bHasMinDesiredHeight = HeaderCellSizeBox->Properties.TryGetString(TEXT("MinDesiredHeight"), MinDesiredHeight) && !MinDesiredHeight.IsEmpty();
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
