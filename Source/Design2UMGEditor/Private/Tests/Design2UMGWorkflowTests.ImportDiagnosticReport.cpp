// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "DesignImportPipeline.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "UMGSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Serialization/JsonSerializer.h"

#if WITH_AUTOMATION_WORKER
namespace
{
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
	Document.MutableParseDiagnostics().SkippedDesignerNodes.Add(SkippedNode);

	TSharedPtr<FDesignNode> TextPathNode = MakeShared<FDesignNode>();
	TextPathNode->Id = TEXT("text_path_1");
	TextPathNode->Name = TEXT("Curved Label");
	TextPathNode->Type = EDesignNodeType::Text;
	TextPathNode->MutableSource().SourceNodeType = TEXT("TEXT_PATH");
	TextPathNode->Text.FontFamily = TEXT("DefinitelyMissingReportFont");

	TSharedPtr<FDesignNode> RenderFallbackNode = MakeShared<FDesignNode>();
	RenderFallbackNode->Id = TEXT("render_1");
	RenderFallbackNode->Name = TEXT("Blurred Hero");
	RenderFallbackNode->Type = EDesignNodeType::Rectangle;
	RenderFallbackNode->MutableSource().SourceNodeType = TEXT("RECTANGLE");
	RenderFallbackNode->MutableSource().RenderFallback.ImageRef = TEXT("render_hero");
	RenderFallbackNode->MutableSource().RenderFallback.Scope = TEXT("full");
	RenderFallbackNode->MutableSource().RenderFallback.Source = TEXT("figma_render_png");
	RenderFallbackNode->MutableSource().RenderFallback.Reason = TEXT("foreground_blur");

	Document.Children.Add(TextPathNode);
	Document.Children.Add(RenderFallbackNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImportReportFixture");
	Data.Warnings.Add(TEXT("Figma TEXT_PATH 'Curved Label' -> TextBlock (text-on-path downgraded to regular text)"));

	FUMGWidgetNode Wrapper;
	Wrapper.Type = TEXT("BackgroundBlur");
	Wrapper.Id = TEXT("hero_blur");
	Wrapper.Name = TEXT("HeroBlur");
	Wrapper.Effect.BlurStrength = 12.0f;

	FUMGWidgetNode Child;
	Child.Type = TEXT("TextBlock");
	Child.Id = TEXT("hero_text");
	Child.Name = TEXT("HeroText");
	Wrapper.Children.Add(Child);
	Data.RootWidget = Wrapper;

	const FString ImageReport =
		TEXT("Remote image import: imported=0 reused=0 repaired=0 failed=1 path=/Game/Design2UMG/ImportedImages\n")
		TEXT("Image 'Hero' download failed: https://example.com/hero.png (reason: Network request failed)");

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
	Data.RootWidget.Bounds = {0.0f, 0.0f, 120.0f, 60.0f};

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
	Data.RootWidget.Bounds = {0.0f, 0.0f, 120.0f, 60.0f};

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
#endif
