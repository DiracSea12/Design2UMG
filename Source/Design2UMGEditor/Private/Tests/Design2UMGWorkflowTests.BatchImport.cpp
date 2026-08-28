// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "DesignSchema.h"
#include "Design2UMGEditorDesignSourceRegistry.h"
#include "Design2UMGLocalFigmaImport.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "FontResolver.h"
#include "ExternalServices/PenMCPBridge.h"
#include "Design2UMGTestUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_AUTOMATION_WORKER
using namespace Design2UMGTestUtils;
using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

namespace
{
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

	FFontResolver::ClearCache();
	FFontResolver::FindMissingFonts(Document);

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
	TestTrue(TEXT("Parse raw Figma json through source registry"),
		Design2UMGEditorDesignSourceRegistry::TryParseSourceDocument(TEXT("Figma"), RawJson, Document));
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

#endif // WITH_AUTOMATION_WORKER
