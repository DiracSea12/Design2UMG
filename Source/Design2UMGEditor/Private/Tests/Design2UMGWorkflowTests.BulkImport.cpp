// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "DesignImportPlanning.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "FigmaParser.h"
#include "FigmaComponentDependencies.h"
#include "Design2UMGLocalFigmaImport.h"
#include "DesignImportPipeline.h"
#include "DesignImportGeneration.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "FontResolver.h"
#include "ExternalServices/PenMCPBridge.h"
#include "PenBundledLibraries.h"
#include "PenParser.h"
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
	"Design2UMG.BulkImport.PencilWelcomeLigatureIconWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenWelcomeLigatureIconWeightWorkflowTest::RunTest(const FString& Parameters)
{
	const FString PenFile = GetFixturePath(TEXT("pencil-welcome.pen"));
	if (PenFile.IsEmpty() || !FPaths::FileExists(PenFile))
	{
		AddError(FString::Printf(TEXT("Required welcome fixture not found: %s"), *PenFile));
		return false;
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

	FUMGWidgetData Data = DesignImportPlanning::AnalyzeImport(Document, FImportAnalysisInput{false}).Data;

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
	"Design2UMG.BulkImport.PenMCP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenMcpImportWorkflowTest::RunTest(const FString& Parameters)
{
	const FString PenFile = GetFixturePath(TEXT("pencil-welcome.pen"));
	if (PenFile.IsEmpty() || !FPaths::FileExists(PenFile))
	{
		AddError(FString::Printf(TEXT("Required Pen MCP fixture not found: %s"), *PenFile));
		return false;
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
	FArtifactGenerationRequest Request;
	Request.OutputPath = OutputPath;
	const bool bPng = DesignImportGeneration::GenerateBlueprintAndPng(Data, Request).bSucceeded;
	TestTrue(TEXT("Render Pen MCP import"), bPng);
	DeleteGeneratedAssetsAtPath(OutputPath);
	return bPng;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalFileImportWorkflowTest,
	"Design2UMG.BulkImport.LocalFigma",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalFileImportWorkflowTest::RunTest(const FString& Parameters)
{
	const FString JsonPath = GetFixturePath(TEXT("FigmaComplex.json"));
	if (JsonPath.IsEmpty() || !FPaths::FileExists(JsonPath))
	{
		AddError(FString::Printf(TEXT("Required local Figma fixture not found: %s"), *JsonPath));
		return false;
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
	FArtifactGenerationRequest Request;
	Request.OutputPath = OutputPath;
	const bool bPng = DesignImportGeneration::GenerateBlueprintAndPng(Data, Request).bSucceeded;
	TestTrue(TEXT("Render local Figma import"), bPng);
	DeleteGeneratedAssetsAtPath(OutputPath);
	return bPng;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSourceImportWorkflowTest,
	"Design2UMG.BulkImport.FigmaSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaSourceImportWorkflowTest::RunTest(const FString& Parameters)
{
	const FString JsonPath = GetFixturePath(TEXT("FigmaComplex.json"));
	if (JsonPath.IsEmpty() || !FPaths::FileExists(JsonPath))
	{
		AddError(FString::Printf(TEXT("Required Figma source fixture not found: %s"), *JsonPath));
		return false;
	}

	FString RawJson;
	TestTrue(TEXT("Load raw Figma json"), FFileHelper::LoadFileToString(RawJson, *JsonPath));
	if (RawJson.IsEmpty())
	{
		return false;
	}

	FDesignDocument Document;
	FString PreparedJson, DependencyError;
	if (!TestTrue(TEXT("Prepare REST component definitions"), FigmaComponentDependencies::PrepareRestFile(
		RawJson, TEXT("source-workflow-fixture"), PreparedJson, DependencyError))) return false;
	TestTrue(TEXT("Parse prepared Figma source"), FFigmaParser::ParseDocument(PreparedJson, Document));
	TestTrue(TEXT("Parsed Figma source should contain children"), Document.Children.Num() > 0);

	FUMGWidgetData Data = DesignImportPlanning::AnalyzeImport(Document, FImportAnalysisInput{false}).Data;
	TestFalse(TEXT("Figma source import should yield a widget name"), Data.WidgetName.IsEmpty());
	TestTrue(TEXT("Figma source import should yield root widget children"), Data.RootWidget.Children.Num() > 0);

	if (GUsingNullRHI)
	{
		return true;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaSource");
	FArtifactGenerationRequest Request;
	Request.OutputPath = OutputPath;
	const bool bPng = DesignImportGeneration::GenerateBlueprintAndPng(Data, Request).bSucceeded;
	TestTrue(TEXT("Render figma source import"), bPng);
	DeleteGeneratedAssetsAtPath(OutputPath);
	return bPng;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenWelcomeBulkImportTest,
	"Design2UMG.BulkImport.PencilWelcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenWelcomeBulkImportTest::RunTest(const FString& Parameters)
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
		AddError(FString::Printf(TEXT("Required welcome fixture not found: %s"), *PenFile));
		return false;
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

		FUMGWidgetData Data = DesignImportPlanning::AnalyzeImport(Document, FImportAnalysisInput{false}).Data;
		if (GUsingNullRHI)
		{
			continue;
		}
		const FString FrameOutputPath = OutputPath / ObjectTools::SanitizeObjectName(Data.WidgetName);
		FArtifactGenerationRequest Request;
	Request.OutputPath = FrameOutputPath;
	const bool bPng = DesignImportGeneration::GenerateBlueprintAndPng(Data, Request).bSucceeded;
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

// Selecting a reusable component as the import target must render that node
// instead of dropping it as a pure component definition.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenReusableTargetImportWorkflowTest,
	"Design2UMG.BulkImport.PenReusableTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenReusableTargetImportWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json =
		TEXT("{")
		TEXT("\"version\":\"2.17\",")
		TEXT("\"children\":[")
		TEXT("{\"id\":\"compA\",\"name\":\"CompA\",\"type\":\"frame\",\"reusable\":true,\"width\":80,\"height\":40,\"children\":[{\"id\":\"compAChild\",\"type\":\"text\",\"content\":\"A\"}]},")
		TEXT("{\"id\":\"compB\",\"name\":\"CompB\",\"type\":\"frame\",\"reusable\":true,\"width\":50,\"height\":50},")
		TEXT("{\"id\":\"screen1\",\"name\":\"Screen1\",\"type\":\"frame\",\"width\":200,\"height\":100}")
		TEXT("]}");

	FDesignDocument Document;
	TestTrue(TEXT("Parse reusable fixture"), FPenParser::ParseDocument(Json, Document));
	TestEqual(TEXT("Three top-level nodes"), Document.Children.Num(), 3);

	FPenMCPBridge::PromoteSelectedNodeToRenderRoot(Document, TEXT("compA"));
	if (!TestEqual(TEXT("Only the selected target remains an output root"), Document.Children.Num(), 1)) return false;
	TestFalse(TEXT("Selected reusable target stops being reusable"), Document.Children[0]->bReusable);
	if (!TestEqual(TEXT("Only reusable definitions remain imported"), Document.SourceDocument().ImportedNodes.Num(), 1)) return false;
	TestTrue(TEXT("Other reusable definitions stay reusable"), Document.SourceDocument().ImportedNodes[0]->bReusable);
	TMap<FString, TSharedPtr<FDesignNode>> Targets;
	Document.BuildReferenceNodeIndex(Targets);
	TestFalse(TEXT("Unrelated plain frames are not imported"), Targets.Contains(TEXT("screen1")));

	FPenMCPBridge::PromoteSelectedNodeToRenderRoot(Document, TEXT("missing-id"));
	TestFalse(TEXT("Missing target changes nothing"), Document.Children[0]->bReusable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenFilteredDocImportsPassthroughTest,
	"Design2UMG.BulkImport.FilteredDocKeepsImports",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenFilteredDocImportsPassthroughTest::RunTest(const FString&)
{
	// Node-filtered pen imports rebuild the document object from the filtered
	// subtree; the doc-level imports map must survive that rebuild so
	// alias-qualified refs ("$Y:--primary") resolve against the registered
	// library variables. The complete library is registered from a fixture lib.pen
	// the same way EnsureBundledLibrariesRegistered registers real ones.
	FString LibPenJson;
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		if (!Plugin.IsValid())
		{
			AddError(TEXT("Design2UMG plugin not found"));
			return false;
		}
		const FString FixturePath = FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures/sample.lib.pen"));
		if (!FFileHelper::LoadFileToString(LibPenJson, *FixturePath))
		{
			AddError(FString::Printf(TEXT("Failed to load fixture: %s"), *FixturePath));
			return false;
		}
	}
	PenBundledLibraries::RegisterLibraryDocument(TEXT("sample"), LibPenJson);

	const FString PenJson = TEXT(R"PENJSON({"imports":{"Y":"pencil:sample.lib.pen"},"children":[{"id":"page-1","name":"Page","type":"frame","theme":{"Y:Accent":"Orange"},"children":[{"id":"dot-1","name":"Dot","type":"ellipse","width":8,"height":8,"fill":"$Y:--primary"}]}]})PENJSON");

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PenJson);
	if (!TestTrue(TEXT("fixture json parses"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid()))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Filtered;
	if (!TestTrue(TEXT("filter succeeds"), FPenMCPBridge::BuildFilteredDocumentObject(RootObject, Filtered, TEXT("page-1"))))
	{
		return false;
	}
	if (!TestTrue(TEXT("filtered doc keeps imports"), Filtered.IsValid() && Filtered->HasField(TEXT("imports"))))
	{
		return false;
	}

	FDesignDocument Doc;
	if (!TestTrue(TEXT("filtered doc parses"), FPenParser::ParseDocumentObject(Filtered, Doc)))
	{
		return false;
	}
	if (!TestEqual(TEXT("filtered doc has one root"), Doc.Children.Num(), 1)
		|| !TestTrue(TEXT("filtered root is valid"), Doc.Children[0].IsValid()))
	{
		return false;
	}
	const FDesignNode& Page = *Doc.Children[0];
	if (!TestEqual(TEXT("filtered root is the requested page"), Page.Id, FString(TEXT("page-1")))
		|| !TestEqual(TEXT("filtered page has one child"), Page.Children.Num(), 1)
		|| !TestTrue(TEXT("filtered child is valid"), Page.Children[0].IsValid()))
	{
		return false;
	}
	const FDesignNode& Dot = *Page.Children[0];
	if (!TestEqual(TEXT("filtered child is the target dot"), Dot.Id, FString(TEXT("dot-1"))))
	{
		return false;
	}
	return TestEqual(TEXT("orange primary fill"), Dot.Visual.TopmostFill().SolidColor, FString(TEXT("#EA580C")));
}

#endif // WITH_AUTOMATION_WORKER
