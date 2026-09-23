// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Fixtures for property and structure reconciliation during safe reimport.

#pragma once

#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"
#include "FigmaParser.h"
#include "UMGGenerator.h"
#include "Tests/Design2UMGComponentInputTestWidget.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WidgetBlueprint.h"

#include <initializer_list>

namespace Design2UMGReimportFixtures
{
	using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

	// Source identities and mapped classes used by reimport fixtures.
	inline const TCHAR* FixtureReimportScope = TEXT("fixture-reimport-doc-9f31");
	inline const TCHAR* FixtureReimportOtherScope = TEXT("fixture-reimport-other-2c66");
	inline const TCHAR* FixtureReimportComponentKey = TEXT("fixture-reimport-comp-key");
	inline const TCHAR* FixtureMappedClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGComponentInputTestWidget");
	inline const TCHAR* FixtureOtherMappedClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");
	inline const TCHAR* FixtureMappingKey = TEXT("figma|fixture-reimport-doc-9f31|fixture-reimport-comp-key");

	// ---------------------------------------------------------------------------
	// Figma fixture builders (maintained independently in this file)
	// ---------------------------------------------------------------------------

	inline FString FixtureTextStyle()
	{
		return TEXT("\"style\":{\"fontFamily\":\"Inter\",\"fontSize\":14,\"fontWeight\":400,"
			"\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\","
			"\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0}");
	}

	inline FString MakeFixtureText(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* Text)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":90,\"height\":20},\"characters\":\"%s\",%s}"),
			Id, Name, X, Y, Text, *FixtureTextStyle());
	}

	// Fill-less pure-container FRAME with multiple children: keeps the multi-child panel form so project/structure operations can mount.
	inline FString MakeFixturePanel(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":200,\"height\":120},\"children\":[%s]}"),
			Id, Name, X, Y, *ChildrenJson);
	}

	inline FString MakeFixtureButton(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* Label)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"INSTANCE\","
			 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
			 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":120,\"height\":44},"
			 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}],"
			 "\"children\":[{\"id\":\"%s_label\",\"name\":\"InnerLabel\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":80,\"height\":18},\"characters\":\"%s\",%s}]}"),
			Id, Name, FixtureReimportComponentKey, Label, X, Y, Id, X + 8, Y + 12, Label, *FixtureTextStyle());
	}

	inline FString MakeFixtureInteractive(const TCHAR* Id, const TCHAR* Name, int32 X, const TCHAR* TriggerType, const TCHAR* Url)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":40,\"width\":110,\"height\":30},"
			 "\"interactions\":[{\"trigger\":{\"type\":\"%s\"},\"actions\":[{\"type\":\"URL\",\"url\":\"%s\"}]}],"
			 "\"children\":[{\"id\":\"%s_text\",\"name\":\"%s_label\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":46,\"width\":80,\"height\":18},\"characters\":\"Go\",%s}]}"),
			Id, Name, X, TriggerType, Url, Id, Name, X + 8, *FixtureTextStyle());
	}

	inline FString MakeFixtureDoc(
		const FString& ChildrenJson,
		const TCHAR* FileKey = FixtureReimportScope,
		int32 Width = 360,
		int32 Height = 240)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\","
			 "\"document\":{\"id\":\"0:0\",\"name\":\"FixtureDoc\",\"type\":\"DOCUMENT\",\"children\":["
			 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
			 "{\"id\":\"fixture_root\",\"name\":\"FixturePage\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":%d,\"height\":%d},\"children\":[%s]}"
			 "]}]}}"),
			FileKey, Width, Height, *ChildrenJson);
	}

	inline FDesign2UMGImportSettingsSnapshot MakeFixtureSnapshot(const TCHAR* MappedClassPath)
	{
		FDesign2UMGImportSettingsSnapshot Snapshot;
		Snapshot.ConversionSettings = FDesign2UMGConversionSettings::Defaults();
		if (MappedClassPath)
		{
			DesignComponentMapping::FComponentMappingEntry& Entry =
				Snapshot.GenerationSettings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
			Entry.Key = FixtureMappingKey;
			Entry.Provider = TEXT("figma");
			Entry.DocumentScope = FixtureReimportScope;
			Entry.ComponentKey = FixtureReimportComponentKey;
			Entry.WidgetClass = MappedClassPath;

			DesignComponentMapping::FComponentInputDeclaration& Input = Entry.Inputs.AddDefaulted_GetRef();
			Input.Source = TEXT("label");
			Input.Target = TEXT("TextValue");
			Input.Type = DesignComponentMapping::EComponentInputType::Text;
			Input.ValueKind = DesignComponentMapping::EComponentInputValueKind::ComponentProperty;
		}
		return Snapshot;
	}

	inline UWidgetBlueprint* FixtureImport(const FString& Json, const TCHAR* MappedClassPath, const FString& OutputDir, bool bCleanFirst = true)
	{
		if (bCleanFirst)
		{
			DeleteGeneratedAssetsAtPath(OutputDir);
		}

		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(Json, Document))
		{
			return nullptr;
		}
		FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		const FDesign2UMGImportSettingsSnapshot Snapshot = MakeFixtureSnapshot(MappedClassPath);
		return FUMGGenerator::GenerateWidgetBlueprint(Data, OutputDir, &Snapshot.GenerationSettings);
	}

	inline FDesignReimportResult FixtureReimport(
		const FString& Json,
		const TCHAR* MappedClassPath,
		const FString& TargetPackage,
		bool bGenerateResolutionCopy = false)
	{
		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(Json, Document))
		{
			return FDesignReimportResult();
		}

		FDesignReimportRequest Request;
		Request.TargetAssetPath = TargetPackage;
		Request.SettingsSnapshot = MakeFixtureSnapshot(MappedClassPath);
		Request.bGenerateResolutionCopy = bGenerateResolutionCopy;
		return DesignImportPipeline::Reimport(Document, Request);
	}

	inline int32 FixtureCountKind(const FDesignReimportResult& Result, const TCHAR* Kind)
	{
		int32 Count = 0;
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
		{
			Count += Diagnostic.Kind == Kind ? 1 : 0;
		}
		return Count;
	}

	inline bool FixtureHasMessage(const FDesignReimportResult& Result, const TCHAR* Kind, const TCHAR* Substring)
	{
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
		{
			if (Diagnostic.Kind == Kind && Diagnostic.Message.Contains(Substring))
			{
				return true;
			}
		}
		return false;
	}

	/** A single diagnostic message must contain all substrings (no cross-entry splicing that fakes a positive pairing). */
	inline bool FixtureHasMessageWithAll(const FDesignReimportResult& Result, const TCHAR* Kind, std::initializer_list<const TCHAR*> Substrings)
	{
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
		{
			if (Diagnostic.Kind != Kind)
			{
				continue;
			}
			bool bAll = true;
			for (const TCHAR* Substring : Substrings)
			{
				bAll = bAll && Diagnostic.Message.Contains(Substring);
			}
			if (bAll)
			{
				return true;
			}
		}
		return false;
	}

	inline bool FixtureLoadSidecar(const FString& AssetPackageName, FDesign2UMGGenerationManifestSidecar& OutSidecar)
	{
		FString Error;
		return Design2UMGGenerationManifest::TryLoadSidecarForAsset(AssetPackageName, OutSidecar, Error);
	}

	inline bool FixtureReadFileString(const FString& FilePath, FString& OutString)
	{
		return FFileHelper::LoadFileToString(OutString, *FilePath);
	}

	inline bool FixtureWriteFileString(const FString& FilePath, const FString& Contents)
	{
		const FString Dir = FPaths::GetPath(FilePath);
		if (!Dir.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*Dir, true);
		}
		return FFileHelper::SaveStringToFile(Contents, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	inline bool FixtureParseJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	inline void FixtureWipeOutput(const FString& OutputDir)
	{
		DeleteGeneratedAssetsAtPath(OutputDir);
		const FString DirOnDisk = FPackageName::LongPackageNameToFilename(OutputDir);
		if (!DirOnDisk.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*DirOnDisk, false, true);
		}
	}

	inline FString FixtureAssetFilePath(const FString& PackageName)
	{
		return FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	}

	inline UDesign2UMGComponentInputTestWidget* FixtureFindMappedInstance(UWidgetBlueprint* WBP)
	{
		UDesign2UMGComponentInputTestWidget* Found = nullptr;
		WBP->WidgetTree->ForEachWidget([&Found](UWidget* Widget)
		{
			if (UDesign2UMGComponentInputTestWidget* Candidate = Cast<UDesign2UMGComponentInputTestWidget>(Widget))
			{
				Found = Candidate;
			}
		});
		return Found;
	}

}

