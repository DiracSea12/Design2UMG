// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
//
// QA white-box structural tests (run d2u-cmsr-slice-a, phase 2 manifest regression surface).
// Phase 2 added a side effect to every successful import: writing the sidecar generation record.
// This case verifies that "a plain import without Component Mapping config" also writes the manifest,
// that the record contains only generated-kind instances and produces no mapping diagnostics (the plain path stays undisturbed).

#if WITH_AUTOMATION_WORKER

#include "Misc/AutomationTest.h"

#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationManifest.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "FigmaParser.h"
#include "UMGGenerator.h"
#include "UMGSchema.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "WidgetBlueprint.h"

namespace
{
	using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

	FString MakeQANormalImportFigmaJson()
	{
		return TEXT(R"({
			"fileKey":"qa-normal-import-key",
			"document":{"id":"0:0","name":"Doc","type":"DOCUMENT","children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"qa_root","name":"QANormalPage","type":"FRAME",
					 "absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":200},"children":[
						{"id":"qa_card","name":"Card","type":"FRAME",
						 "absoluteBoundingBox":{"x":24,"y":24,"width":140,"height":100},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.9,"g":0.9,"b":0.9,"a":1}}],
						 "children":[{"id":"qa_card_text","name":"Caption","type":"TEXT",
							"absoluteBoundingBox":{"x":32,"y":32,"width":100,"height":20},"characters":"No mapping here",
							"style":{"fontFamily":"Inter","fontSize":14,"fontWeight":400,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}}]}
					]}
				]}
			]}
		})");
	}

	void DeleteQATestOutputDir(const FString& OutputPath)
	{
		const FString OutputDir = FPackageName::LongPackageNameToFilename(OutputPath);
		if (!OutputDir.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*OutputDir, false, true);
		}
	}
}

// ---------------------------------------------------------------------------
// Manifest regression surface of a plain import (no mapping config):
// generation succeeds, the sidecar exists, provider/documentScope/sourceRoot are correct,
// all instances are kind=generated (no mapped records), d2uOwned is as long as instances,
// and there are no Component Mapping diagnostics at all (with an empty config the match stage is a pure no-op).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQANormalImportWritesManifestTest,
	"Design2UMG.WidgetBlueprint.QANormalImportWritesManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQANormalImportWritesManifestTest::RunTest(const FString&)
{
	FDesignDocument Document;
	TestTrue(TEXT("parse normal-import fixture"),
		FFigmaParser::ParseDocument(MakeQANormalImportFigmaJson(), Document));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/QANormalImport");
	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteQATestOutputDir(OutputPath);

	// Default snapshot: ClassOverrides empty, ComponentMapping an empty table (equivalent to no project ini).
	const FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput GenerationOutput;
	UWidgetBlueprint* Blueprint = FUMGGenerator::GenerateWidgetBlueprint(
		Data, OutputPath, &Settings, &GenerationOutput);
	TestTrue(TEXT("normal import still generates"), Blueprint != nullptr);
	if (!Blueprint)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		DeleteQATestOutputDir(OutputPath);
		return false;
	}

	// The plain path is undisturbed by the mapping stage: no mapping diagnostics.
	TestEqual(TEXT("no mapping diagnostics without config"),
		GenerationOutput.MappingDiagnostics.Num(), 0);

	const FString AssetPackageName = Blueprint->GetOutermost()->GetName();
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(AssetPackageName);
	FString Json;
	TestTrue(TEXT("manifest sidecar written for plain import"), FFileHelper::LoadFileToString(Json, *SidecarPath));

	TSharedPtr<FJsonObject> Manifest;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (FJsonSerializer::Deserialize(Reader, Manifest) && Manifest.IsValid())
	{
		TestEqual(TEXT("schema version"), Manifest->GetIntegerField(TEXT("schemaVersion")), 1);
		TestEqual(TEXT("provider"), Manifest->GetStringField(TEXT("provider")), TEXT("figma"));
		TestEqual(TEXT("document scope uses file key"),
			Manifest->GetStringField(TEXT("documentScope")), TEXT("qa-normal-import-key"));
		const TSharedPtr<FJsonObject>* SourceRoot = nullptr;
		TestTrue(TEXT("source root present"),
			Manifest->TryGetObjectField(TEXT("sourceRoot"), SourceRoot) && SourceRoot);
		if (SourceRoot && (*SourceRoot).IsValid())
		{
			TestEqual(TEXT("source root node id"),
				(*SourceRoot)->GetStringField(TEXT("nodeId")), TEXT("qa_root"));
			TestEqual(TEXT("source root name"),
				(*SourceRoot)->GetStringField(TEXT("name")), TEXT("QANormalPage"));
		}

		const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
		TestTrue(TEXT("instances array present"),
			Manifest->TryGetArrayField(TEXT("instances"), Instances) && Instances);
		if (Instances)
		{
			int32 MappedCount = 0;
			int32 GeneratedCount = 0;
			for (const TSharedPtr<FJsonValue>& Value : *Instances)
			{
				const TSharedPtr<FJsonObject> Instance = Value->AsObject();
				if (!Instance.IsValid())
				{
					AddError(TEXT("instance entry is not an object"));
					continue;
				}
				if (Instance->GetStringField(TEXT("kind")) == TEXT("mapped"))
				{
					++MappedCount;
				}
				else if (Instance->GetStringField(TEXT("kind")) == TEXT("generated"))
				{
					++GeneratedCount;
				}
			}
			TestEqual(TEXT("no mapped instances in a plain import"), MappedCount, 0);
			TestTrue(TEXT("at least one generated top-level instance recorded"), GeneratedCount >= 1);

			const TArray<TSharedPtr<FJsonValue>>* D2UOwned = nullptr;
			TestTrue(TEXT("d2uOwned present"),
				Manifest->TryGetArrayField(TEXT("d2uOwned"), D2UOwned) && D2UOwned);
			if (D2UOwned)
			{
				// d2uOwned is every created widget name (including nested leaves), a superset of instances.
				TestTrue(TEXT("d2uOwned covers every recorded instance"),
					D2UOwned->Num() >= Instances->Num());
			}
		}
	}
	else
	{
		AddError(TEXT("manifest sidecar is not valid JSON"));
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteQATestOutputDir(OutputPath);
	return true;
}

#endif
