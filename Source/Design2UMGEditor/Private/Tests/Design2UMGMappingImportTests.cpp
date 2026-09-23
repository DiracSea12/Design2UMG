// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Component-mapping workflow tests for saved generation manifests.

#include "Misc/Build.h"

#if WITH_AUTOMATION_WORKER

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationManifest.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
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
	using namespace Design2UMGCoreTests;

	FString MakeFixtureNormalImportFigmaJson()
	{
		return MakeFigmaDocJson(
			MakeContainerNode(TEXT("fixture_root"), TEXT("FixtureNormalPage"), TEXT("FRAME"), 0, 0, 360, 200,
				MakeContainerNode(TEXT("fixture_card"), TEXT("Card"), TEXT("FRAME"), 24, 24, 140, 100,
					MakeTextNode(TEXT("fixture_card_text"), TEXT("Caption"), 32, 32, 100, 20, TEXT("No mapping here"),
						TEXT("\"fontFamily\":\"Inter\",\"fontSize\":14,\"fontWeight\":400,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")),
					FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.9, 0.9, 0.9)))),
			TEXT(",\"fileKey\":\"fixture-normal-import-key\""));
	}

	void DeleteMappingTestOutputDir(const FString& OutputPath)
	{
		const FString OutputDir = FPackageName::LongPackageNameToFilename(OutputPath);
		if (!OutputDir.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*OutputDir, false, true);
		}
	}
}

// A plain import writes a generated-only manifest with source identity and owned widgets.
// An empty mapping configuration produces no mapping diagnostics.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingNormalImportWritesManifestTest,
	"Design2UMG.WidgetBlueprint.NormalImportWritesManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingNormalImportWritesManifestTest::RunTest(const FString&)
{
	FDesignDocument Document;
	TestTrue(TEXT("parse normal-import fixture"),
		FFigmaParser::ParseDocument(MakeFixtureNormalImportFigmaJson(), Document));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FixtureNormalImport");
	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteMappingTestOutputDir(OutputPath);

	// Default snapshot: ClassOverrides empty, ComponentMapping an empty table (equivalent to no project ini).
	const FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput GenerationOutput;
	UWidgetBlueprint* Blueprint = FUMGGenerator::GenerateWidgetBlueprint(
		Data, OutputPath, &Settings, &GenerationOutput);
	TestTrue(TEXT("normal import still generates"), Blueprint != nullptr);
	if (!Blueprint)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		DeleteMappingTestOutputDir(OutputPath);
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
			Manifest->GetStringField(TEXT("documentScope")), TEXT("fixture-normal-import-key"));
		const TSharedPtr<FJsonObject>* SourceRoot = nullptr;
		TestTrue(TEXT("source root present"),
			Manifest->TryGetObjectField(TEXT("sourceRoot"), SourceRoot) && SourceRoot);
		if (SourceRoot && (*SourceRoot).IsValid())
		{
			TestEqual(TEXT("source root node id"),
				(*SourceRoot)->GetStringField(TEXT("nodeId")), TEXT("fixture_root"));
			TestEqual(TEXT("source root name"),
				(*SourceRoot)->GetStringField(TEXT("name")), TEXT("FixtureNormalPage"));
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
	DeleteMappingTestOutputDir(OutputPath);
	return true;
}

#endif
