// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// White-box structural tests for Safe Reimport (slice B: phase 3 property-level reimport + phase 4 structural changes).
// Test design independent of the in-delivery dev tests: ships its own Figma fixture builders and a QA stand-in
// mapped class (UDesign2UMGMappingQAWidget), verified with asset/structure/input/behavior/identity/manifest
// assertions instead of visual scores. Each case maps one-to-one to a RunTest function in this file.

#include "Misc/AutomationTest.h"

#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationManifest.h"
#include "Design2UMGPrototypeNavigationExtensions.h"
#include "Design2UMGWidgetBindingTools.h"
#include "DesignImportPipeline.h"
#include "FigmaParser.h"
#include "UMGGenerator.h"
#include "../UMGWidgetUtils.h"
#include "Tests/Design2UMGMappingQAWidget.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "PackageTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"

#include <initializer_list>

#if WITH_AUTOMATION_WORKER

namespace
{
	using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

	// Fixture identities private to this QA file: scopes / component keys / mapped stand-in classes are not shared with the in-delivery dev tests.
	const TCHAR* QAReimportScope = TEXT("qa-reimport-doc-9f31");
	const TCHAR* QAReimportOtherScope = TEXT("qa-reimport-other-2c66");
	const TCHAR* QAReimportComponentKey = TEXT("qa-reimport-comp-key");
	const TCHAR* QAMappedClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappingQAWidget");
	const TCHAR* QAOtherMappedClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");
	const TCHAR* QAMappingKey = TEXT("figma|qa-reimport-doc-9f31|qa-reimport-comp-key");

	// ---------------------------------------------------------------------------
	// Figma fixture builders (maintained independently in this file)
	// ---------------------------------------------------------------------------

	FString QATextStyle()
	{
		return TEXT("\"style\":{\"fontFamily\":\"Inter\",\"fontSize\":14,\"fontWeight\":400,"
			"\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\","
			"\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0}");
	}

	FString MakeQAText(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* Text)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":90,\"height\":20},\"characters\":\"%s\",%s}"),
			Id, Name, X, Y, Text, *QATextStyle());
	}

	// Fill-less pure-container FRAME with multiple children: keeps the multi-child panel form so project/structure operations can mount.
	FString MakeQAPanel(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":200,\"height\":120},\"children\":[%s]}"),
			Id, Name, X, Y, *ChildrenJson);
	}

	FString MakeQAButton(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* Label)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"INSTANCE\","
			 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
			 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":120,\"height\":44},"
			 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}],"
			 "\"children\":[{\"id\":\"%s_label\",\"name\":\"InnerLabel\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":80,\"height\":18},\"characters\":\"%s\",%s}]}"),
			Id, Name, QAReimportComponentKey, Label, X, Y, Id, X + 8, Y + 12, Label, *QATextStyle());
	}

	FString MakeQAInteractive(const TCHAR* Id, const TCHAR* Name, int32 X, const TCHAR* TriggerType, const TCHAR* Url)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":40,\"width\":110,\"height\":30},"
			 "\"interactions\":[{\"trigger\":{\"type\":\"%s\"},\"actions\":[{\"type\":\"URL\",\"url\":\"%s\"}]}],"
			 "\"children\":[{\"id\":\"%s_text\",\"name\":\"%s_label\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":46,\"width\":80,\"height\":18},\"characters\":\"Go\",%s}]}"),
			Id, Name, X, TriggerType, Url, Id, Name, X + 8, *QATextStyle());
	}

	FString MakeQADoc(
		const FString& ChildrenJson,
		const TCHAR* FileKey = QAReimportScope,
		int32 Width = 360,
		int32 Height = 240)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\","
			 "\"document\":{\"id\":\"0:0\",\"name\":\"QADoc\",\"type\":\"DOCUMENT\",\"children\":["
			 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
			 "{\"id\":\"qa_root\",\"name\":\"QAPage\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":%d,\"height\":%d},\"children\":[%s]}"
			 "]}]}}"),
			FileKey, Width, Height, *ChildrenJson);
	}

	FDesign2UMGImportSettingsSnapshot MakeQASnapshot(const TCHAR* MappedClassPath)
	{
		FDesign2UMGImportSettingsSnapshot Snapshot;
		Snapshot.ConversionSettings = FDesign2UMGConversionSettings::Defaults();
		if (MappedClassPath)
		{
			DesignComponentMapping::FComponentMappingEntry& Entry =
				Snapshot.GenerationSettings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
			Entry.Key = QAMappingKey;
			Entry.Provider = TEXT("figma");
			Entry.DocumentScope = QAReimportScope;
			Entry.ComponentKey = QAReimportComponentKey;
			Entry.WidgetClass = MappedClassPath;

			DesignComponentMapping::FComponentInputDeclaration& Input = Entry.Inputs.AddDefaulted_GetRef();
			Input.Source = TEXT("label");
			Input.Target = TEXT("TextValue");
			Input.Type = DesignComponentMapping::EComponentInputType::Text;
			Input.ValueKind = DesignComponentMapping::EComponentInputValueKind::ComponentProperty;
		}
		return Snapshot;
	}

	UWidgetBlueprint* QAImport(const FString& Json, const TCHAR* MappedClassPath, const FString& OutputDir, bool bCleanFirst = true)
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
		const FDesign2UMGImportSettingsSnapshot Snapshot = MakeQASnapshot(MappedClassPath);
		return FUMGGenerator::GenerateWidgetBlueprint(Data, OutputDir, &Snapshot.GenerationSettings);
	}

	FDesignReimportResult QAReimport(
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
		Request.SettingsSnapshot = MakeQASnapshot(MappedClassPath);
		Request.bGenerateResolutionCopy = bGenerateResolutionCopy;
		return DesignImportPipeline::Reimport(Document, Request);
	}

	int32 QACountKind(const FDesignReimportResult& Result, const TCHAR* Kind)
	{
		int32 Count = 0;
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
		{
			Count += Diagnostic.Kind == Kind ? 1 : 0;
		}
		return Count;
	}

	bool QAHasMessage(const FDesignReimportResult& Result, const TCHAR* Kind, const TCHAR* Substring)
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
	bool QAHasMessageWithAll(const FDesignReimportResult& Result, const TCHAR* Kind, std::initializer_list<const TCHAR*> Substrings)
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

	/** mapping_-prefixed diagnostics belong solely to the componentMapping section: classification assertions check ComponentMappingDiagnostics. */
	bool QAHasMappingMessageWithAll(const FDesignReimportResult& Result, const TCHAR* Kind, std::initializer_list<const TCHAR*> Substrings)
	{
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.ComponentMappingDiagnostics)
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

	bool QALoadSidecar(const FString& AssetPackageName, FDesign2UMGGenerationManifestSidecar& OutSidecar)
	{
		FString Error;
		return Design2UMGGenerationManifest::TryLoadSidecarForAsset(AssetPackageName, OutSidecar, Error);
	}

	bool QAReadFileString(const FString& FilePath, FString& OutString)
	{
		return FFileHelper::LoadFileToString(OutString, *FilePath);
	}

	bool QAWriteFileString(const FString& FilePath, const FString& Contents)
	{
		const FString Dir = FPaths::GetPath(FilePath);
		if (!Dir.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*Dir, true);
		}
		return FFileHelper::SaveStringToFile(Contents, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool QAParseJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	void QAWipeOutput(const FString& OutputDir)
	{
		DeleteGeneratedAssetsAtPath(OutputDir);
		const FString DirOnDisk = FPackageName::LongPackageNameToFilename(OutputDir);
		if (!DirOnDisk.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*DirOnDisk, false, true);
		}
	}

	FString QAAssetFilePath(const FString& PackageName)
	{
		return FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	}

	UDesign2UMGMappingQAWidget* QAFindMappedInstance(UWidgetBlueprint* WBP)
	{
		UDesign2UMGMappingQAWidget* Found = nullptr;
		WBP->WidgetTree->ForEachWidget([&Found](UWidget* Widget)
		{
			if (UDesign2UMGMappingQAWidget* Candidate = Cast<UDesign2UMGMappingQAWidget>(Widget))
			{
				Found = Candidate;
			}
		});
		return Found;
	}

	// Multi-page prototype document fixture (slice D): a page = a top-level FRAME with fills (converted into the Border
	// page form; the page widget name = the page id); inside the page sits a text with a cross-page ON_CLICK NODE
	// navigation (the target page must really exist for the binding to be valid), so the document forms a multi-page
	// prototype and the conversion chain synthesizes the switcher host root (id = the page-set content fingerprint, rewritten whenever the set changes).
	FString MakeProtoNavText(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* DestinationPageId)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":90,\"height\":20},\"characters\":\"nav\",%s,"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"%s\",\"navigation\":\"NAVIGATE\"}]}]}"),
			Id, Name, X, Y, *QATextStyle(), DestinationPageId);
	}

	FString MakeProtoPage(const TCHAR* Id, const TCHAR* Name, int32 X, const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],"
				 "\"children\":[%s]}"),
			Id, Name, X, *ChildrenJson);
	}

	FString MakeProtoDoc(const FString& PagesJson)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"ProtoDoc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":[%s]}]}}"),
			QAReimportScope, *PagesJson);
	}
}

// ---------------------------------------------------------------------------
// Case 1 (unit): manifest schema consolidation - inputs persist declarations only (source/target/valueKind);
// design values never enter the manifest; the historical inputs[].value field in old sidecars is always ignored.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAManifestInputSchemaTest,
	"Design2UMG.SafeReimportQA.ManifestInputSchemaDeclarationsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAManifestInputSchemaTest::RunTest(const FString&)
{
	const FString Package = TEXT("/Game/Design2UMG/_TestOutput/QAReimportManifestSchema");
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);

	FDesign2UMGGenerationManifestPayload Payload;
	Payload.Provider = TEXT("figma");
	Payload.DocumentScope = TEXT("unit-scope-1");
	Payload.SourceRootNodeId = TEXT("qa-unit-root");
	Payload.SourceRootName = TEXT("QAPage");
	{
		FDesign2UMGMappedInstanceRecord Mapped;
		Mapped.DesignNodeId = TEXT("qa-m-1");
		Mapped.WidgetName = TEXT("W1");
		Mapped.MappedKey = TEXT("figma|unit-scope-1|k");
		Mapped.MappedClass = TEXT("/Script/Design2UMGEditor.Design2UMGMappingQAWidget");
		Mapped.bMatchedAtSetLevel = true;
		FDesign2UMGMappedInputRecord Input;
		Input.Source = TEXT("label");
		Input.Target = TEXT("TextValue");
		Input.ValueKind = TEXT("ComponentProperty");
		Mapped.Inputs.Add(MoveTemp(Input));
		Payload.MappedInstances.Add(MoveTemp(Mapped));
	}
	{
		FDesign2UMGGeneratedInstanceRecord Generated;
		Generated.DesignNodeId = TEXT("qa-g-1");
		Generated.WidgetName = TEXT("G1");
		Payload.TopLevelGenerated.Add(MoveTemp(Generated));
	}
	Payload.D2UOwnedNames = { TEXT("W1"), TEXT("G1") };

	if (!TestTrue(TEXT("WriteSidecarForAsset succeeds"), Design2UMGGenerationManifest::WriteSidecarForAsset(Package, Payload)))
	{
		IFileManager::Get().Delete(*SidecarPath, false, true);
		return false;
	}

	// Write side: the inputs object has exactly the three fields source/target/valueKind; value/bApplied are not persisted.
	FString RawJson;
	TestTrue(TEXT("sidecar file readable"), QAReadFileString(SidecarPath, RawJson));
	TSharedPtr<FJsonObject> Root;
	if (TestTrue(TEXT("sidecar JSON parses"), QAParseJsonObject(RawJson, Root)))
	{
		TestEqual(TEXT("schemaVersion is 1"), static_cast<int32>(Root->GetNumberField(TEXT("schemaVersion"))), 1);
		TestEqual(TEXT("provider written"), Root->GetStringField(TEXT("provider")), FString(TEXT("figma")));
		TestEqual(TEXT("documentScope written"), Root->GetStringField(TEXT("documentScope")), FString(TEXT("unit-scope-1")));
		const TSharedPtr<FJsonObject>* SourceRoot = nullptr;
		if (TestTrue(TEXT("sourceRoot present"), Root->TryGetObjectField(TEXT("sourceRoot"), SourceRoot) && SourceRoot != nullptr))
		{
			TestEqual(TEXT("sourceRoot.nodeId written"), (*SourceRoot)->GetStringField(TEXT("nodeId")), FString(TEXT("qa-unit-root")));
		}

		const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
		if (TestTrue(TEXT("instances present"), Root->TryGetArrayField(TEXT("instances"), Instances) && Instances != nullptr)
			&& TestEqual(TEXT("two instances written"), Instances->Num(), 2))
		{
			const TSharedPtr<FJsonObject> MappedObject = (*Instances)[0]->AsObject();
			const TSharedPtr<FJsonObject> GeneratedObject = (*Instances)[1]->AsObject();
			if (TestTrue(TEXT("mapped instance object"), MappedObject.IsValid())
				&& TestTrue(TEXT("generated instance object"), GeneratedObject.IsValid()))
			{
				TestEqual(TEXT("mapped kind"), MappedObject->GetStringField(TEXT("kind")), FString(TEXT("mapped")));
				TestEqual(TEXT("set-level flag written"), MappedObject->GetBoolField(TEXT("matchedAtSetLevel")), true);
				TestEqual(TEXT("mappedKey written"), MappedObject->GetStringField(TEXT("mappedKey")), FString(TEXT("figma|unit-scope-1|k")));

				const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
				if (TestTrue(TEXT("inputs array present"), MappedObject->TryGetArrayField(TEXT("inputs"), Inputs) && Inputs != nullptr)
					&& TestEqual(TEXT("one input written"), Inputs->Num(), 1))
				{
					const TSharedPtr<FJsonObject> InputObject = (*Inputs)[0]->AsObject();
					if (TestTrue(TEXT("input object"), InputObject.IsValid()))
					{
						TestEqual(TEXT("input source"), InputObject->GetStringField(TEXT("source")), FString(TEXT("label")));
						TestEqual(TEXT("input target"), InputObject->GetStringField(TEXT("target")), FString(TEXT("TextValue")));
						TestEqual(TEXT("input valueKind"), InputObject->GetStringField(TEXT("valueKind")), FString(TEXT("ComponentProperty")));
						TestFalse(TEXT("inputs[].value NOT serialized"), InputObject->HasField(TEXT("value")));
						TestFalse(TEXT("inputs[].bApplied NOT serialized"), InputObject->HasField(TEXT("bApplied")));
					}
				}
				TestEqual(TEXT("generated kind"), GeneratedObject->GetStringField(TEXT("kind")), FString(TEXT("generated")));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Owned = nullptr;
		if (TestTrue(TEXT("d2uOwned present"), Root->TryGetArrayField(TEXT("d2uOwned"), Owned) && Owned != nullptr)
			&& TestEqual(TEXT("d2uOwned count"), Owned->Num(), 2))
		{
			TestEqual(TEXT("d2uOwned[0]"), (*Owned)[0]->AsString(), FString(TEXT("W1")));
			TestEqual(TEXT("d2uOwned[1]"), (*Owned)[1]->AsString(), FString(TEXT("G1")));
		}
	}

	// Read side: the three declaration fields round-trip as-is (the record structure no longer has value/bApplied fields to restore).
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("TryLoadSidecarForAsset succeeds"), QALoadSidecar(Package, Sidecar)))
	{
		TestEqual(TEXT("loaded schemaVersion"), Sidecar.SchemaVersion, 1);
		TestEqual(TEXT("loaded provider"), Sidecar.Provider, FString(TEXT("figma")));
		TestEqual(TEXT("loaded documentScope"), Sidecar.DocumentScope, FString(TEXT("unit-scope-1")));
		TestEqual(TEXT("loaded sourceRoot id"), Sidecar.SourceRootNodeId, FString(TEXT("qa-unit-root")));
		TestEqual(TEXT("loaded sourceRoot name"), Sidecar.SourceRootName, FString(TEXT("QAPage")));
		TestEqual(TEXT("instance count"), Sidecar.Instances.Num(), 2);
		if (Sidecar.Instances.Num() == 2)
		{
			const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& M = Sidecar.Instances[0];
			TestEqual(TEXT("loaded mapped kind"), M.Kind, FString(TEXT("mapped")));
			TestTrue(TEXT("loaded set-level flag"), M.bMatchedAtSetLevel);
			TestEqual(TEXT("loaded mappedClass"), M.MappedClass, FString(QAMappedClassPath));
			if (TestEqual(TEXT("loaded input count"), M.Inputs.Num(), 1) && M.Inputs.Num() == 1)
			{
				TestEqual(TEXT("loaded input source"), M.Inputs[0].Source, FString(TEXT("label")));
				TestEqual(TEXT("loaded input target"), M.Inputs[0].Target, FString(TEXT("TextValue")));
				TestEqual(TEXT("loaded input valueKind"), M.Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
			}
			TestEqual(TEXT("loaded generated kind"), Sidecar.Instances[1].Kind, FString(TEXT("generated")));
			TestEqual(TEXT("loaded generated id"), Sidecar.Instances[1].DesignNodeId, FString(TEXT("qa-g-1")));
		}
		TestTrue(TEXT("d2uOwned loaded"), Sidecar.D2UOwned.Contains(TEXT("W1")) && Sidecar.D2UOwned.Contains(TEXT("G1")));
	}

	// A historical sidecar (with a value field) ignores value per the schema consolidation rule and loads without errors.
	// The injection needle is serialization-whitespace-independent (the value itself): append a historical value sibling field after the valueKind value.
	FString LegacyJson = RawJson;
	const TCHAR* DeclarationNeedle = TEXT("\"ComponentProperty\"");
	LegacyJson.ReplaceInline(
		DeclarationNeedle,
		TEXT("\"ComponentProperty\",\"value\":\"legacy-77f\""),
		ESearchCase::CaseSensitive);
	TestTrue(TEXT("legacy value injected"), LegacyJson != RawJson);
	TestTrue(TEXT("legacy sidecar written"), QAWriteFileString(SidecarPath, LegacyJson));

	FDesign2UMGGenerationManifestSidecar LegacySidecar;
	if (TestTrue(TEXT("legacy sidecar still loads"), QALoadSidecar(Package, LegacySidecar))
		&& TestEqual(TEXT("legacy input count"), LegacySidecar.Instances.Num() > 0 ? LegacySidecar.Instances[0].Inputs.Num() : -1, 1)
		&& LegacySidecar.Instances.Num() > 0 && LegacySidecar.Instances[0].Inputs.Num() == 1)
	{
		TestEqual(TEXT("legacy inputs[].value ignored; declarations intact"),
			LegacySidecar.Instances[0].Inputs[0].Source, FString(TEXT("label")));
		TestEqual(TEXT("legacy inputs[] declaration target intact"),
			LegacySidecar.Instances[0].Inputs[0].Target, FString(TEXT("TextValue")));
		TestEqual(TEXT("legacy inputs[] declaration valueKind intact"),
			LegacySidecar.Instances[0].Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
	}

	IFileManager::Get().Delete(*SidecarPath, false, true);
	return true;
}

// ---------------------------------------------------------------------------
// Case 2 (unit): error classification of sidecar read failures - missing file / invalid JSON / unrecognized schema
// version / missing provider|documentScope / empty instances each return false + a precise reason.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAManifestLoadRejectsBadSidecarsTest,
	"Design2UMG.SafeReimportQA.ManifestLoadRejectsBadSidecars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAManifestLoadRejectsBadSidecarsTest::RunTest(const FString&)
{
	const FString Package = TEXT("/Game/Design2UMG/_TestOutput/QAReimportManifestErrors");
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);

	// 1) Missing file.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	FString Error;
	TestFalse(TEXT("missing sidecar rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("missing-file reason"), Error.Contains(TEXT("generation manifest sidecar not found")));

	// 2) Invalid JSON.
	TestTrue(TEXT("garbage written"), QAWriteFileString(SidecarPath, TEXT("{ this is not json")));
	TestFalse(TEXT("invalid JSON rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("invalid-JSON reason"), Error.Contains(TEXT("generation manifest sidecar is not valid JSON")));

	// 3) Unrecognized schema version.
	TestTrue(TEXT("v2 schema written"), QAWriteFileString(
		SidecarPath,
		TEXT("{\"schemaVersion\":2,\"provider\":\"figma\",\"documentScope\":\"s\",\"sourceRoot\":{},\"instances\":[{}],\"d2uOwned\":[]}")));
	TestFalse(TEXT("unsupported schema rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("schema-version reason"), Error.Contains(TEXT("schema version 2 is not supported (expected 1)")));

	// 4) provider / documentScope missing.
	TestTrue(TEXT("empty provider written"), QAWriteFileString(
		SidecarPath,
		TEXT("{\"schemaVersion\":1,\"provider\":\"\",\"documentScope\":\"\",\"sourceRoot\":{},\"instances\":[{}],\"d2uOwned\":[]}")));
	TestFalse(TEXT("missing provider/documentScope rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("missing-provider reason"), Error.Contains(TEXT("missing provider/documentScope")));

	// 5) Empty instances.
	TestTrue(TEXT("empty instances written"), QAWriteFileString(
		SidecarPath,
		TEXT("{\"schemaVersion\":1,\"provider\":\"figma\",\"documentScope\":\"s\",\"sourceRoot\":{},\"instances\":[],\"d2uOwned\":[]}")));
	TestFalse(TEXT("empty instances rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("empty-instances reason"), Error.Contains(TEXT("records no instances")));

	IFileManager::Get().Delete(*SidecarPath, false, true);
	return true;
}

// ---------------------------------------------------------------------------
// Case 3 (integration): a corrupt sidecar (invalid JSON) = unclear relationship -> full conflict;
// the asset bytes stay untouched, and no sibling copy is generated without an explicit choice.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAUnreadableManifestConflictTest,
	"Design2UMG.SafeReimportQA.FullConflictOnUnreadableManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAUnreadableManifestConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportBadManifest");

	const FString V1 = MakeQADoc(MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = QAAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);

	// Corrupt sidecar: not even valid JSON.
	TestTrue(TEXT("corrupt sidecar written"), QAWriteFileString(
		Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), TEXT("<<<corrupt-not-json>>>")));

	const FDesignReimportResult Result = QAReimport(V1, nullptr, TargetPackage);
	TestFalse(TEXT("reimport does not succeed"), Result.bSucceeded);
	TestTrue(TEXT("full conflict flagged"), Result.bFullConflict);
	TestEqual(TEXT("exactly one full-conflict diagnostic"), QACountKind(Result, TEXT("reimport_full_conflict")), 1);
	TestTrue(TEXT("conflict reason cites invalid JSON"),
		QAHasMessage(Result, TEXT("reimport_full_conflict"), TEXT("is not valid JSON")));
	TestTrue(TEXT("target asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);
	TestTrue(TEXT("no implicit resolution copy"), Result.ResolutionCopyPath.IsEmpty());
	TestFalse(TEXT("no _Resolution asset file"),
		FPaths::FileExists(QAAssetFilePath(FPaths::GetPath(TargetPackage) / TEXT("WBP_QAPage_Resolution"))));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 4 (integration): DocumentScope mismatch (a different fileKey in the new source) = unclear relationship ->
// full conflict; the diagnostic names both sides' provider/scope, and the asset is not updated in place.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAScopeMismatchConflictTest,
	"Design2UMG.SafeReimportQA.FullConflictOnDocumentScopeMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAScopeMismatchConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportScopeMismatch");

	const FString V1 = MakeQADoc(MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = QAAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);

	const FString V2 = MakeQADoc(
		MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("Two")),
		QAReimportOtherScope);
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);

	TestFalse(TEXT("scope mismatch does not update in place"), Result.bSucceeded);
	TestTrue(TEXT("full conflict flagged"), Result.bFullConflict);
	TestEqual(TEXT("exactly one full-conflict diagnostic"), QACountKind(Result, TEXT("reimport_full_conflict")), 1);

	const FString ExpectedReason = FString::Printf(
		TEXT("document scope mismatch: manifest provider='figma' scope='%s', new source provider='figma' scope='%s'"),
		QAReimportScope, QAReimportOtherScope);
	bool bExactMessage = false;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Kind == TEXT("reimport_full_conflict") && Diagnostic.Message.Contains(ExpectedReason))
		{
			bExactMessage = true;
		}
	}
	TestTrue(TEXT("conflict reason reports both scopes verbatim"), bExactMessage);

	// The asset stays as it was: file bytes unchanged, the in-memory text still the v1 value (the pipeline returns at the matching stage).
	TestTrue(TEXT("asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);
	if (UTextBlock* Alpha = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("AlphaText"))))
	{
		TestEqual(TEXT("in-memory text untouched"), Alpha->GetText().ToString(), FString(TEXT("One")));
	}
	else
	{
		AddError(TEXT("AlphaText widget missing"));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 5 (integration): project-side restructure (none of the manifest-recorded instances can be located in the
// target tree) -> full conflict; the diagnostics finished first by the matching stage are visible; the project-side structure is left untouched.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQARestructureConflictTest,
	"Design2UMG.SafeReimportQA.FullConflictOnProjectRestructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQARestructureConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportRestructure");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
				*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v1")))),
		*MakeQAText(TEXT("qa_t2"), TEXT("AlphaText"), 250, 24, TEXT("One"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = QAAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);

	// Simulate a project restructure: remove every manifest-recorded instance from the tree and put the project's own widgets in.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (!TestTrue(TEXT("sidecar loads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Sidecar.Instances)
	{
		if (UWidget* Recorded = WBP->WidgetTree->FindWidget(FName(*Record.WidgetName)))
		{
			WBP->WidgetTree->RemoveWidget(Recorded);
		}
	}
	UTextBlock* ProjectReplacement = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProjectReplacement"));
	if (UPanelWidget* RootPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		RootPanel->AddChild(ProjectReplacement);
	}

	const FDesignReimportResult Result = QAReimport(V1, nullptr, TargetPackage);
	TestFalse(TEXT("restructure does not update in place"), Result.bSucceeded);
	TestTrue(TEXT("full conflict flagged"), Result.bFullConflict);
	TestTrue(TEXT("restructure reason reported"),
		QAHasMessage(Result, TEXT("reimport_full_conflict"), TEXT("none of the recorded instances could be located in the target widget tree")));
	// Spec: source matching / instance matching / ownership classification must complete before anything applies;
	// the scope-match diagnostic must appear first, followed by the full conflict.
	TestTrue(TEXT("scope matching completed first"), QACountKind(Result, TEXT("reimport_scope_matched")) == 1);

	// The project-side structure is left untouched.
	TestNotNull(TEXT("project replacement widget preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectReplacement")));
	TestTrue(TEXT("asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 6 (integration): the matched-update main flow - D2U-owned text/layout refreshed, the mapped instance gets
// only its outer layout + explicit inputs overwritten (instance not replaced, interior not expanded), project children
// kept, root size refreshed, manifest consolidated; the copy flag is ignored when there is no full conflict.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMatchedUpdateTest,
	"Design2UMG.SafeReimportQA.MatchedRefreshesDesignOwned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMatchedUpdateTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMatched");

	const FString InfoPanelV1 = MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 180, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 188, 32, TEXT("Head v1")),
			*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 188, 60, TEXT("Tail v1"))));
	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")),
		*InfoPanelV1),
		QAReimportScope, 360, 240);

	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;

	// Simulate developer customization: edit a mapped input value, edit D2U text, add a project child widget.
	MappedBefore->TextValue = FText::FromString(TEXT("Hacked"));
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		Head->SetText(FText::FromString(TEXT("DevEdit")));
	}
	UTextBlock* ProjectNote = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProjectNote"));
	if (UPanelWidget* RootPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		RootPanel->AddChild(ProjectNote);
	}

	const FString InfoPanelV2 = MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 180, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 188, 32, TEXT("Head v2")),
			*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 188, 60, TEXT("Tail v2"))));
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 60, 96, TEXT("Pay Now")),
		*InfoPanelV2),
		QAReimportScope, 480, 320);

	// The copy flag must be ignored without a full conflict (no copy generated).
	const FDesignReimportResult Result = QAReimport(V2, QAMappedClassPath, TargetPackage, /*bGenerateResolutionCopy=*/true);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestTrue(TEXT("updated blueprint returned"), Result.UpdatedBlueprint == WBP);

	// D2U-owned text is refreshed by the design value (the developer's edit is overwritten - text is D2U-owned).
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("D2U text refreshed from design"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}
	else
	{
		AddError(TEXT("Head widget missing after reimport"));
	}

	// mapped instance: the same object (not replaced), class unchanged, inputs overwritten directly, outer layout updated, interior subtree not expanded.
	UDesign2UMGMappingQAWidget* MappedAfter = MappedWeak.Get();
	TestNotNull(TEXT("mapped instance still alive"), MappedAfter);
	if (MappedAfter)
	{
		TestTrue(TEXT("mapped instance object identity preserved"), MappedAfter == MappedBefore);
		TestEqual(TEXT("mapped instance class unchanged"), MappedAfter->GetClass()->GetPathName(), FString(QAMappedClassPath));
		TestEqual(TEXT("declared input overwritten with latest design value"), MappedAfter->TextValue.ToString(), FString(TEXT("Pay Now")));
		// The component's interior subtree is not expanded: the inner label in the design never appears as a generated widget inside the host tree.
		TestNull(TEXT("mapped component internals not expanded into the host tree"),
			WBP->WidgetTree->FindWidget(TEXT("InnerLabel")));
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(MappedAfter->Slot))
		{
			TestTrue(TEXT("mapped outer layout updated"),
				FMath::Abs(Slot->GetPosition().X - 60.0f) < 4.0f && FMath::Abs(Slot->GetPosition().Y - 96.0f) < 4.0f);
		}
		else
		{
			AddError(TEXT("mapped instance slot is not a canvas slot"));
		}
	}

	// Project child widgets kept.
	TestNotNull(TEXT("project-added widget preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectNote")));

	// The root size change lands in the preview size.
	if (UUserWidget* CDO = Cast<UUserWidget>(WBP->GeneratedClass ? WBP->GeneratedClass->GetDefaultObject() : nullptr))
	{
		TestEqual(TEXT("root design size refreshed (X)"), static_cast<int32>(CDO->DesignTimeSize.X), 480);
		TestEqual(TEXT("root design size refreshed (Y)"), static_cast<int32>(CDO->DesignTimeSize.Y), 320);
		TestEqual(TEXT("design size mode custom"), static_cast<int32>(CDO->DesignSizeMode), static_cast<int32>(EDesignPreviewSizeMode::Custom));
	}
	else
	{
		AddError(TEXT("CDO missing after reimport"));
	}

	// Diagnostics: matched update + inputs overwrite (with the concrete design value).
	TestTrue(TEXT("update diagnostics present"), QACountKind(Result, TEXT("reimport_updated")) >= 2);
	TestTrue(TEXT("input overwrite diagnostic carries the design value"),
		QAHasMessage(Result, TEXT("mapping_input_applied"), TEXT("Pay Now")));

	// The copy flag is ignored: no copy.
	TestTrue(TEXT("no resolution copy without full conflict"), Result.ResolutionCopyPath.IsEmpty());
	TestFalse(TEXT("no _Resolution asset file"),
		FPaths::FileExists(QAAssetFilePath(FPaths::GetPath(TargetPackage) / TEXT("WBP_QAPage_Resolution"))));

	// Manifest: the instances remain; project widgets do not enter d2uOwned.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after reimport"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("mapped instance still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_1")));
		TestNotNull(TEXT("generated instance still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_panel")));
		TestTrue(TEXT("nested D2U widget owned"), Sidecar.D2UOwned.Contains(TEXT("Head")));
		TestFalse(TEXT("project widget not owned"), Sidecar.D2UOwned.Contains(TEXT("ProjectNote")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 7 (integration): deterministic pairing of same-named siblings - the k-th same-named design child pairs with
// the k-th suffixed existing widget; the refresh does not cross-assign.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASiblingNameCorrespondenceTest,
	"Design2UMG.SafeReimportQA.SiblingNameCorrespondence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASiblingNameCorrespondenceTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSiblingNames");

	auto BuildDoc = [](const TCHAR* SecondText)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_stats"), TEXT("Stats"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_title_a"), TEXT("Title"), 32, 32, TEXT("First")),
				*MakeQAText(TEXT("qa_title_b"), TEXT("Title"), 32, 60, SecondText))));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("Second")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UTextBlock* TitleA = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Title")));
	UTextBlock* TitleB = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Title_1")));
	if (!TestNotNull(TEXT("first sibling named Title"), TitleA) || !TestNotNull(TEXT("second sibling named Title_1"), TitleB))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TestEqual(TEXT("v1 first text"), TitleA->GetText().ToString(), FString(TEXT("First")));
	TestEqual(TEXT("v1 second text"), TitleB->GetText().ToString(), FString(TEXT("Second")));

	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("Second v2")), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TitleA = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Title")));
	TitleB = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Title_1")));
	if (TestNotNull(TEXT("Title still present"), TitleA) && TestNotNull(TEXT("Title_1 still present"), TitleB))
	{
		TestEqual(TEXT("first same-name sibling keeps its own design node value"), TitleA->GetText().ToString(), FString(TEXT("First")));
		TestEqual(TEXT("second same-name sibling refreshed to its own design node value"), TitleB->GetText().ToString(), FString(TEXT("Second v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 8 (integration): added - a new top-level purely generated node and a new mapped instance join the tree and
// are recorded in the new manifest (the mapped record carries key/class/inputs declarations; the inputs values are already written into the instance per the design).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAAddedNodesTest,
	"Design2UMG.SafeReimportQA.AddedNodesAndMappedInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAAddedNodesTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportAdded");

	const FString V1 = MakeQADoc(MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	TestNull(TEXT("v1 has no ExtraPanel yet"), WBP->WidgetTree->FindWidget(TEXT("ExtraPanel")));
	TestNull(TEXT("v1 has no Sidekick yet"), WBP->WidgetTree->FindWidget(TEXT("Sidekick")));

	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")),
		*MakeQAPanel(TEXT("qa_extra"), TEXT("ExtraPanel"), 24, 90,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_beta"), TEXT("Beta"), 32, 98, TEXT("Beta")),
				*MakeQAText(TEXT("qa_beta2"), TEXT("BetaTwo"), 32, 126, TEXT("Beta 2")))),
		*MakeQAButton(TEXT("qa_btn_2"), TEXT("Sidekick"), 250, 90, TEXT("Jump In"))));

	const FDesignReimportResult Result = QAReimport(V2, QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("two top-level adds diagnosed"), QACountKind(Result, TEXT("reimport_added")), 2);
	TestTrue(TEXT("added panel diagnosed"), QAHasMessage(Result, TEXT("reimport_added"), TEXT("ExtraPanel")));
	TestTrue(TEXT("added mapped instance diagnosed"), QAHasMessage(Result, TEXT("reimport_added"), TEXT("Sidekick")));

	// The new purely generated node + nested children join.
	UWidget* ExtraPanel = WBP->WidgetTree->FindWidget(TEXT("ExtraPanel"));
	TestNotNull(TEXT("new generated node added"), ExtraPanel);
	if (UTextBlock* Beta = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Beta"))))
	{
		TestEqual(TEXT("nested new child text applied"), Beta->GetText().ToString(), FString(TEXT("Beta")));
		if (ExtraPanel)
		{
			TestEqual(TEXT("nested new child parented under the new panel"), Beta->GetParent()->GetFName(), ExtraPanel->GetFName());
		}
	}
	else
	{
		AddError(TEXT("nested new child missing"));
	}

	// The new mapped instance joins and its inputs are already written per the design.
	UDesign2UMGMappingQAWidget* Sidekick = Cast<UDesign2UMGMappingQAWidget>(WBP->WidgetTree->FindWidget(TEXT("Sidekick")));
	TestNotNull(TEXT("new mapped instance added"), Sidekick);
	if (Sidekick)
	{
		TestEqual(TEXT("new mapped instance input written"), Sidekick->TextValue.ToString(), FString(TEXT("Jump In")));
	}

	// The manifest records the new relations.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Extra =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_extra"));
		if (TestNotNull(TEXT("new generated node recorded"), Extra))
		{
			TestEqual(TEXT("generated kind recorded"), Extra->Kind, FString(TEXT("generated")));
			TestEqual(TEXT("generated widget name recorded"), Extra->WidgetName, FString(TEXT("ExtraPanel")));
		}
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Mapped =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_2"));
		if (TestNotNull(TEXT("new mapped instance recorded"), Mapped))
		{
			TestEqual(TEXT("mapped kind recorded"), Mapped->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("mapped key recorded"), Mapped->MappedKey, FString(QAMappingKey));
			TestEqual(TEXT("mapped class recorded"), Mapped->MappedClass, FString(QAMappedClassPath));
			if (TestEqual(TEXT("mapped inputs declaration recorded"), Mapped->Inputs.Num(), 1) && Mapped->Inputs.Num() == 1)
			{
				TestEqual(TEXT("input source recorded"), Mapped->Inputs[0].Source, FString(TEXT("label")));
				TestEqual(TEXT("input target recorded"), Mapped->Inputs[0].Target, FString(TEXT("TextValue")));
				TestEqual(TEXT("input valueKind recorded"), Mapped->Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
			}
		}
		TestTrue(TEXT("new panel ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("ExtraPanel")));
		TestTrue(TEXT("new nested child ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("Beta")));
		TestTrue(TEXT("new mapped instance ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("Sidekick")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 9 (integration): safe remove, positive - a purely D2U-owned node is deleted safely;
// a node the design deleted but the project already removed from the asset is only dropped from the records (no error, no wrongful delete).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQARemovePureNodeTest,
	"Design2UMG.SafeReimportQA.RemoveDeletesPureD2UNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQARemovePureNodeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportRemovePure");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeQAText(TEXT("qa_clean"), TEXT("CleanStat"), 24, 24, TEXT("Old")),
		*MakeQAText(TEXT("qa_ghost"), TEXT("GhostStat"), 140, 24, TEXT("Gone")),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// The project side already deleted GhostStat's widget in the editor (the manifest still records it).
	// Uses the editor's own DeleteWidgets (destroys the objects + cleans variable GUIDs); a bare RemoveWidget merely
	// unhooks it from the tree while the object stays alive - any later compile would trip the compiler's ensure on the detached widget/GUID.
	if (UWidget* Ghost = WBP->WidgetTree->FindWidget(TEXT("GhostStat")))
	{
		FWidgetBlueprintEditorUtils::DeleteWidgets(
			WBP, { Ghost }, FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
	}

	const FString V2 = MakeQADoc(MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep")));
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("purely owned node deleted from tree"), WBP->WidgetTree->FindWidget(TEXT("CleanStat")));
	TestNull(TEXT("already-absent node stays absent"), WBP->WidgetTree->FindWidget(TEXT("GhostStat")));
	TestNotNull(TEXT("kept node still present"), WBP->WidgetTree->FindWidget(TEXT("Keeper")));

	TestEqual(TEXT("two removal diagnostics"), QACountKind(Result, TEXT("reimport_removed")), 2);
	TestTrue(TEXT("safe-delete flow diagnosed for present widget"),
		QAHasMessageWithAll(Result, TEXT("reimport_removed"), { TEXT("CleanStat"), TEXT("removed through the safe delete flow") }));
	TestTrue(TEXT("already-absent branch diagnosed"),
		QAHasMessageWithAll(Result, TEXT("reimport_removed"), { TEXT("GhostStat"), TEXT("widget already absent from the tree") }));
	TestEqual(TEXT("no remove conflicts"), QACountKind(Result, TEXT("reimport_remove_conflict")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNull(TEXT("deleted node dropped from instances"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_clean")));
		TestNull(TEXT("absent node dropped from instances"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_ghost")));
		TestNotNull(TEXT("kept node still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_keep")));
		TestFalse(TEXT("deleted node dropped from d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("CleanStat")));
		TestFalse(TEXT("absent node dropped from d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("GhostStat")));
		TestTrue(TEXT("kept node still owned"), Sidecar.D2UOwned.Contains(TEXT("Keeper")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 10 (integration): remove conflict - when the deleted node's subtree contains project content (project-added
// children / developer bindings), no delete: kept + a conflict diagnostic stating the reason, and the new manifest keeps the instance.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQARemoveProjectContentTest,
	"Design2UMG.SafeReimportQA.RemovePreservesProjectContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQARemoveProjectContentTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportRemoveConflict");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeQAPanel(TEXT("qa_doom"), TEXT("DoomedPanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_dhead"), TEXT("DoomHead"), 32, 32, TEXT("Doom")),
				*MakeQAText(TEXT("qa_dtail"), TEXT("DoomTail"), 32, 60, TEXT("Doom")))),
		*MakeQAPanel(TEXT("qa_bound"), TEXT("BoundPanel"), 24, 160,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_bhead"), TEXT("BoundHead"), 32, 168, TEXT("Bound")),
				*MakeQAText(TEXT("qa_btail"), TEXT("BoundTail"), 32, 196, TEXT("Bound")))),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Project-content signal A: a project-added child hangs inside the DoomedPanel subtree.
	if (UPanelWidget* Doomed = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(TEXT("DoomedPanel"))))
	{
		UTextBlock* ProjectChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProjectChild"));
		Doomed->AddChild(ProjectChild);
	}
	// Project-content signal B: WBP Bindings reference a widget inside the BoundPanel subtree.
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = TEXT("BoundPanel");
	ProjectBinding.PropertyName = TEXT("OnClicked");
	ProjectBinding.FunctionName = TEXT("HandleBound");
	WBP->Bindings.Add(ProjectBinding);

	const FString V2 = MakeQADoc(MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep")));
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflicts are per-node)"), Result.bSucceeded);

	TestNotNull(TEXT("panel with project child preserved"), WBP->WidgetTree->FindWidget(TEXT("DoomedPanel")));
	TestNotNull(TEXT("project child preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectChild")));
	TestNotNull(TEXT("panel with developer binding preserved"), WBP->WidgetTree->FindWidget(TEXT("BoundPanel")));
	TestNotNull(TEXT("kept node still present"), WBP->WidgetTree->FindWidget(TEXT("Keeper")));

	TestEqual(TEXT("no silent delete"), QACountKind(Result, TEXT("reimport_removed")), 0);
	TestEqual(TEXT("two remove conflicts"), QACountKind(Result, TEXT("reimport_remove_conflict")), 2);
	TestTrue(TEXT("unowned-widget reason names the project child"),
		QAHasMessageWithAll(Result, TEXT("reimport_remove_conflict"),
			{ TEXT("DoomedPanel"), TEXT("widgets outside the generation record"), TEXT("ProjectChild") }));
	TestTrue(TEXT("developer-binding reason names the node"),
		QAHasMessageWithAll(Result, TEXT("reimport_remove_conflict"),
			{ TEXT("BoundPanel"), TEXT("developer bindings in the blueprint") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("conflicted panel stays recorded (unowned child)"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_doom")));
		TestNotNull(TEXT("conflicted panel stays recorded (binding)"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_bound")));
		TestTrue(TEXT("subtree widgets stay owned"), Sidecar.D2UOwned.Contains(TEXT("DoomHead")) && Sidecar.D2UOwned.Contains(TEXT("BoundTail")));
		TestFalse(TEXT("project child stays outside d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("ProjectChild")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 11 (integration): kind-changed conflict - a mapped->generated conversion whose old subtree contains
// project content (developer bindings) -> kept as-is + conflict diagnostic, not replaced; the manifest record is unchanged.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAKindChangeProjectContentTest,
	"Design2UMG.SafeReimportQA.KindChangePreservesProjectContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAKindChangeProjectContentTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportKindConflict");

	const FString V1 = MakeQADoc(MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;

	// The developer binding hangs on the mapped instance (the subtree root): the old subtree is no longer purely D2U-owned.
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = TEXT("HeroButton");
	ProjectBinding.PropertyName = TEXT("OnClicked");
	ProjectBinding.FunctionName = TEXT("HandleHero");
	WBP->Bindings.Add(ProjectBinding);

	// v2: the same design node, but this round no longer provides the mapping -> kind flips mapped->generated.
	const FString V2 = MakeQADoc(MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UDesign2UMGMappingQAWidget* MappedAfter = MappedWeak.Get();
	TestNotNull(TEXT("mapped instance kept"), MappedAfter);
	if (MappedAfter)
	{
		TestEqual(TEXT("mapped instance class unchanged"), MappedAfter->GetClass()->GetPathName(), FString(QAMappedClassPath));
	}
	TestEqual(TEXT("kind change conflict diagnosed once"), QACountKind(Result, TEXT("reimport_kind_conflict")), 1);
	TestTrue(TEXT("conflict reports mapped -> generated and the reason on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_kind_conflict"),
			{ TEXT("HeroButton"), TEXT("mapped -> generated"), TEXT("developer bindings in the blueprint") }));
	TestEqual(TEXT("no replacement performed"), QACountKind(Result, TEXT("reimport_kind_changed")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_1"));
		if (TestNotNull(TEXT("transitioned node stays recorded"), Record))
		{
			TestEqual(TEXT("manifest kind stays mapped"), Record->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("manifest mapped class stays"), Record->MappedClass, FString(QAMappedClassPath));
			TestEqual(TEXT("manifest widget name stays"), Record->WidgetName, FString(TEXT("HeroButton")));
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 12 (integration): move, positive - the design moves a node into a newly added container; both ends clearly
// D2U-owned -> the widget changes parents but the object identity survives; manifest top-level instances consolidated, ownership kept.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMoveWithinOwnedBoundsTest,
	"Design2UMG.SafeReimportQA.MoveReparentWithinOwnedBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMoveWithinOwnedBoundsTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMoveOk");

	// MoverPanel is a fill-less two-child pure container in both versions (type consistent across versions); in v1 it hangs under the host root.
	const FString MoverPanelJson = MakeQAPanel(TEXT("qa_mover"), TEXT("MoverPanel"), 40, 32,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_ma"), TEXT("MoverA"), 48, 40, TEXT("A")),
			*MakeQAText(TEXT("qa_mb"), TEXT("MoverB"), 48, 68, TEXT("B"))));

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MoverPanelJson,
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UWidget* MoverBefore = WBP->WidgetTree->FindWidget(TEXT("MoverPanel"));
	if (!TestNotNull(TEXT("MoverPanel present in v1"), MoverBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UWidget> MoverWeak = MoverBefore;
	TestEqual(TEXT("v1 parent is the page host root"),
		MoverBefore->GetParent()->GetFName(), WBP->WidgetTree->RootWidget->GetFName());

	// v2: the new container TargetBox adopts MoverPanel (the whole subtree moves along).
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_target"), TEXT("TargetBox"), 24, 120,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_pad"), TEXT("PadText"), 32, 128, TEXT("Pad")),
				*MakeQAPanel(TEXT("qa_mover"), TEXT("MoverPanel"), 32, 160,
					FString::Printf(
						TEXT("%s,%s"),
						*MakeQAText(TEXT("qa_ma"), TEXT("MoverA"), 40, 168, TEXT("A")),
						*MakeQAText(TEXT("qa_mb"), TEXT("MoverB"), 40, 196, TEXT("B")))))),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	UWidget* MoverAfter = MoverWeak.Get();
	TestNotNull(TEXT("moved widget still alive"), MoverAfter);
	UWidget* TargetBox = WBP->WidgetTree->FindWidget(TEXT("TargetBox"));
	TestNotNull(TEXT("target container added"), TargetBox);
	if (MoverAfter && TargetBox)
	{
		TestTrue(TEXT("widget object identity preserved across reparent"), MoverAfter == MoverBefore);
		TestEqual(TEXT("moved under the new container"), MoverAfter->GetParent()->GetFName(), TargetBox->GetFName());
		if (UWidget* MoverChild = WBP->WidgetTree->FindWidget(TEXT("MoverA")))
		{
			TestEqual(TEXT("mover subtree travelled with it"), MoverChild->GetParent()->GetFName(), MoverAfter->GetFName());
		}
		else
		{
			AddError(TEXT("mover subtree child missing after move"));
		}
	}

	TestEqual(TEXT("one move diagnosed"), QACountKind(Result, TEXT("reimport_moved")), 1);
	TestTrue(TEXT("move diagnostic reports identity preserved on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("MoverPanel"), TEXT("widget identity preserved") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNull(TEXT("moved node no longer a top-level instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_mover")));
		TestNotNull(TEXT("new container recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_target")));
		TestTrue(TEXT("moved node still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("MoverPanel")));
		TestTrue(TEXT("moved subtree still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("MoverA")) && Sidecar.D2UOwned.Contains(TEXT("MoverB")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 13 (integration): move conflict - the new design's parent end is neither a manifest instance nor a D2U node
// created this round (a nested container, unclear ownership) -> structure kept + a move-conflict diagnostic; no move.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMoveBoundaryConflictTest,
	"Design2UMG.SafeReimportQA.MoveConflictOnUnclearBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMoveBoundaryConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMoveConflict");

	// v1: SidePanel > InnerZone (a nested container, not a manifest instance) + the top-level Mover.
	const FString InnerZoneChildren = FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_i1"), TEXT("InnerA"), 40, 40, TEXT("A")),
		*MakeQAText(TEXT("qa_i2"), TEXT("InnerB"), 40, 68, TEXT("B")));
	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_side"), TEXT("SidePanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAPanel(TEXT("qa_inner"), TEXT("InnerZone"), 32, 32, InnerZoneChildren),
				*MakeQAText(TEXT("qa_spad"), TEXT("SidePad"), 140, 32, TEXT("Pad")))),
		*MakeQAText(TEXT("qa_mover"), TEXT("Mover"), 250, 24, TEXT("Move Me"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2: the design moves Mover into InnerZone (a nested container, boundary unclear).
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s"),
		*MakeQAPanel(TEXT("qa_side"), TEXT("SidePanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAPanel(TEXT("qa_inner"), TEXT("InnerZone"), 32, 32,
					FString::Printf(
						TEXT("%s,%s,%s"),
						*MakeQAText(TEXT("qa_i1"), TEXT("InnerA"), 40, 40, TEXT("A")),
						*MakeQAText(TEXT("qa_i2"), TEXT("InnerB"), 40, 68, TEXT("B")),
						*MakeQAText(TEXT("qa_mover"), TEXT("Mover"), 40, 96, TEXT("Move Me")))),
				*MakeQAText(TEXT("qa_spad"), TEXT("SidePad"), 140, 32, TEXT("Pad"))))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UWidget* Mover = WBP->WidgetTree->FindWidget(TEXT("Mover"));
	TestNotNull(TEXT("mover still exists"), Mover);
	if (Mover)
	{
		TestEqual(TEXT("structure preserved: mover stays under the page host root"),
			Mover->GetParent()->GetFName(), WBP->WidgetTree->RootWidget->GetFName());
	}
	TestEqual(TEXT("move conflict diagnosed once"), QACountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("unclear-ownership reason reported on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_move_conflict"), { TEXT("Mover"), TEXT("not clearly D2U-owned") }));
	TestEqual(TEXT("no moves applied"), QACountKind(Result, TEXT("reimport_moved")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("mover stays recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_mover")));
		TestTrue(TEXT("mover stays owned"), Sidecar.D2UOwned.Contains(TEXT("Mover")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 14 (integration): Prototype bindings refresh by stable design identity (no project conflict) - the URL
// change lands in the runtime extension data and the refresh diagnostic states how many entries took effect.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAPrototypeRefreshTest,
	"Design2UMG.SafeReimportQA.PrototypeRefreshByDesignIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAPrototypeRefreshTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportPrototypeRefresh");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAInteractive(TEXT("qa_link_a"), TEXT("LinkAlpha"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/old-a")),
		*MakeQAInteractive(TEXT("qa_link_b"), TEXT("LinkBeta"), 160, TEXT("ON_CLICK"), TEXT("https://qa.example/old-b"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAInteractive(TEXT("qa_link_a"), TEXT("LinkAlpha"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/new-a")),
		*MakeQAInteractive(TEXT("qa_link_b"), TEXT("LinkBeta"), 160, TEXT("ON_CLICK"), TEXT("https://qa.example/new-b"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestEqual(TEXT("no prototype conflicts"), QACountKind(Result, TEXT("reimport_prototype_conflict")), 0);
	TestEqual(TEXT("refresh diagnosed once"), QACountKind(Result, TEXT("reimport_prototype_refreshed")), 1);
	TestTrue(TEXT("refresh diagnostic reports both bindings active"),
		QAHasMessage(Result, TEXT("reimport_prototype_refreshed"), TEXT("D2U prototype bindings refreshed by stable design identity (2 binding(s) active)")));

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	const UDesign2UMGPrototypeNavigationClassExtension* Extension =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>() : nullptr;
	if (TestNotNull(TEXT("prototype extension present after reimport"), Extension))
	{
		const FUMGPrototypeNavigationData& NavData = Extension->GetNavigationDataForTest();
		TestEqual(TEXT("both bindings kept"), NavData.ClickBindings.Num(), 2);
		const FUMGPrototypeNavigationBinding* BindingA = nullptr;
		const FUMGPrototypeNavigationBinding* BindingB = nullptr;
		for (const FUMGPrototypeNavigationBinding& Binding : NavData.ClickBindings)
		{
			if (Binding.TriggerWidgetId == TEXT("qa_link_a")) { BindingA = &Binding; }
			if (Binding.TriggerWidgetId == TEXT("qa_link_b")) { BindingB = &Binding; }
		}
		if (TestNotNull(TEXT("binding for qa_link_a present"), BindingA))
		{
			TestEqual(TEXT("binding A url refreshed"), BindingA->ExternalUrl, FString(TEXT("https://qa.example/new-a")));
		}
		if (TestNotNull(TEXT("binding for qa_link_b present"), BindingB))
		{
			TestEqual(TEXT("binding B url refreshed"), BindingB->ExternalUrl, FString(TEXT("https://qa.example/new-b")));
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 15 (integration): double-trigger conflict (hover trigger) - the same interaction already has a project
// binding -> the project behavior stays, the D2U binding yields + a conflict diagnostic; non-conflicting bindings refresh as usual.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAPrototypeHoverConflictTest,
	"Design2UMG.SafeReimportQA.PrototypeHoverConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAPrototypeHoverConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportPrototypeHover");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAInteractive(TEXT("qa_hover_a"), TEXT("HoverAlpha"), 24, TEXT("ON_HOVER"), TEXT("https://qa.example/old-a")),
		*MakeQAInteractive(TEXT("qa_hover_b"), TEXT("HoverBeta"), 160, TEXT("ON_HOVER"), TEXT("https://qa.example/old-b"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// A project binding occupies qa_hover_a's hover interaction.
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = TEXT("qa_hover_a");
	ProjectBinding.PropertyName = TEXT("OnHovered");
	ProjectBinding.FunctionName = TEXT("HandleHover");
	WBP->Bindings.Add(ProjectBinding);

	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAInteractive(TEXT("qa_hover_a"), TEXT("HoverAlpha"), 24, TEXT("ON_HOVER"), TEXT("https://qa.example/new-a")),
		*MakeQAInteractive(TEXT("qa_hover_b"), TEXT("HoverBeta"), 160, TEXT("ON_HOVER"), TEXT("https://qa.example/new-b"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestEqual(TEXT("hover dual-trigger conflict diagnosed once"), QACountKind(Result, TEXT("reimport_prototype_conflict")), 1);
	TestTrue(TEXT("conflict names trigger, type and outcome on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_prototype_conflict"),
			{ TEXT("trigger 'qa_hover_a'"), TEXT("type 'ON_HOVER'"), TEXT("project behavior kept") }));

	TestTrue(TEXT("project binding kept in the blueprint"), WBP->Bindings.Contains(ProjectBinding));

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	const UDesign2UMGPrototypeNavigationClassExtension* Extension =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>() : nullptr;
	if (TestNotNull(TEXT("prototype extension present after reimport"), Extension))
	{
		const FUMGPrototypeNavigationData& NavData = Extension->GetNavigationDataForTest();
		TestEqual(TEXT("only the unconflicted binding remains"), NavData.ClickBindings.Num(), 1);
		if (NavData.ClickBindings.Num() == 1)
		{
			TestEqual(TEXT("remaining binding is the unconflicted one"),
				NavData.ClickBindings[0].TriggerWidgetId, FString(TEXT("qa_hover_b")));
			TestEqual(TEXT("remaining binding refreshed to the new url"),
				NavData.ClickBindings[0].ExternalUrl, FString(TEXT("https://qa.example/new-b")));
		}
	}
	TestTrue(TEXT("refresh diagnostic reports one binding active"),
		QAHasMessage(Result, TEXT("reimport_prototype_refreshed"), TEXT("(1 binding(s) active)")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 15b (integration): double-trigger conflict (click trigger, real Figma node-id shape) - when the trigger
// node id contains ':', the button widget name is the slug-cleaned form (e.g. "12:7" -> "W127"), and project
// bindings register by that widget name; conflict detection must compare with the same cleaning rule, or every
// double-trigger conflict on a real Figma file is missed (project binding and refreshed D2U binding coexist).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAPrototypeConflictFigmaIdTest,
	"Design2UMG.SafeReimportQA.PrototypeConflictFigmaIdSanitizedMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAPrototypeConflictFigmaIdTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportProtoFigmaId");

	const FString V1 = MakeQADoc(
		MakeQAInteractive(TEXT("12:7"), TEXT("BuyButton"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/old")));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// The node id "12:7" becomes the button widget name "W127" after slug cleaning (drop ':') + digit-prefix handling;
	// the project binding is registered under that widget name in the UMG editor.
	const FName SanitizedButtonName = FUMGWidgetUtils::SanitizeWidgetName(TEXT("12:7"), TEXT("Button"));
	if (!TestEqual(TEXT("sanitized button name for Figma-style id"), SanitizedButtonName, FName(TEXT("W127"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = SanitizedButtonName.ToString();
	ProjectBinding.PropertyName = TEXT("OnClicked");
	ProjectBinding.FunctionName = TEXT("HandleBuy");
	WBP->Bindings.Add(ProjectBinding);

	const FString V2 = MakeQADoc(
		MakeQAInteractive(TEXT("12:7"), TEXT("BuyButton"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/new")));
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestEqual(TEXT("dual-trigger conflict on Figma-style id diagnosed once"),
		QACountKind(Result, TEXT("reimport_prototype_conflict")), 1);
	TestTrue(TEXT("conflict names the original design id"),
		QAHasMessageWithAll(Result, TEXT("reimport_prototype_conflict"),
			{ TEXT("trigger '12:7'"), TEXT("type 'ON_CLICK'"), TEXT("project behavior kept") }));
	TestTrue(TEXT("project binding kept in the blueprint"), WBP->Bindings.Contains(ProjectBinding));

	// With no surviving design binding after yielding (0 bindings active), not attaching the prototype extension is
	// expected; the core lock-points of this case: deterministic naming (the button name derivable from the node id) + no missed conflict detection.
	if (WBP->WidgetTree)
	{
		int32 ButtonCount = 0;
		WBP->WidgetTree->ForEachWidget([&ButtonCount](UWidget* W)
		{
			if (W->IsA<UButton>()) { ++ButtonCount; }
		});
		TestEqual(TEXT("single deterministic trigger button, no duplicate from failed match"), ButtonCount, 1);
		TestNotNull(TEXT("deterministically named button present"), WBP->WidgetTree->FindWidget(TEXT("W127")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 16 (integration): Mapped Class change conflict - the mapping points at another WBP class -> the existing
// instance is kept, not replaced, inputs not overwritten; the diagnostic reports old and new classes; the manifest record keeps the old class.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappedClassChangeTest,
	"Design2UMG.SafeReimportQA.MappedClassChangeIsConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappedClassChangeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappedClass");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;
	MappedBefore->TextValue = FText::FromString(TEXT("Hacked"));

	// v2: the same Key's mapping points at another WBP class.
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Pay Now")),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep"))));
	const FDesignReimportResult Result = QAReimport(V2, QAOtherMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UDesign2UMGMappingQAWidget* MappedAfter = MappedWeak.Get();
	if (TestNotNull(TEXT("mapped instance kept (not destroyed, not replaced)"), MappedAfter))
	{
		TestTrue(TEXT("instance object identity preserved"), MappedAfter == MappedBefore);
		TestEqual(TEXT("instance class not replaced"), MappedAfter->GetClass()->GetPathName(), FString(QAMappedClassPath));
		TestEqual(TEXT("inputs not overwritten on mapped-class conflict"), MappedAfter->TextValue.ToString(), FString(TEXT("Hacked")));
	}

	TestEqual(TEXT("mapped class conflict diagnosed once"), QACountKind(Result, TEXT("reimport_mapped_class_conflict")), 1);
	TestTrue(TEXT("conflict reports node, old class, new class and no-replacement on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_mapped_class_conflict"),
			{ TEXT("HeroButton"), QAMappedClassPath, QAOtherMappedClassPath, TEXT("not replaced in v1") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_1"));
		if (TestNotNull(TEXT("conflicted instance stays recorded"), Record))
		{
			TestEqual(TEXT("manifest keeps the old mapped class"), Record->MappedClass, FString(QAMappedClassPath));
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 17 (integration): save-failure semantics - transaction rollback (in-memory text and structure return to
// pre-reimport), manifest bytes unchanged, the diagnostic names the failure; retrying the same reimport afterwards succeeds (the asset was not poisoned).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASaveFailureRollbackTest,
	"Design2UMG.SafeReimportQA.SaveFailureRollsBackAndKeepsManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASaveFailureRollbackTest::RunTest(const FString&)
{
	// The save-failure injection path logs an error by design; registered as expected.
	AddExpectedError(TEXT("failed to save"), EAutomationExpectedErrorFlags::Contains, 0);

	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSaveFailure");

	const FString V1 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
			*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v1")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	FString ManifestBefore;
	if (!TestTrue(TEXT("v1 manifest exists"), QAReadFileString(SidecarPath, ManifestBefore)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v2")),
				*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v2")))),
		*MakeQAText(TEXT("qa_fresh"), TEXT("FreshText"), 250, 24, TEXT("Fresh"))));

	FUMGGenerator::ArmAutomationSaveFailureOnce();
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);

	TestFalse(TEXT("reimport reports failure"), Result.bSucceeded);
	TestFalse(TEXT("failure is not a full conflict"), Result.bFullConflict);
	TestNull(TEXT("no updated blueprint on failure"), Result.UpdatedBlueprint);
	TestEqual(TEXT("save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
	TestTrue(TEXT("failure diagnostic states rollback and untouched manifest on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_save_failed"),
			{ TEXT("transaction rolled back"), TEXT("asset keeps its pre-reimport state"), TEXT("manifest not updated") }));

	// Transaction rollback: the in-memory state stays pre-reimport (v1 design values, the v2 text never written).
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("in-memory text rolled back"), Head->GetText().ToString(), FString(TEXT("Head v1")));
	}
	else
	{
		AddError(TEXT("Head widget missing after rollback"));
	}
	TestNull(TEXT("reimport-added widget rolled back"), WBP->WidgetTree->FindWidget(TEXT("FreshText")));

	// Manifest not updated: byte-for-byte identical.
	FString ManifestAfter;
	TestTrue(TEXT("manifest still exists"), QAReadFileString(SidecarPath, ManifestAfter));
	TestTrue(TEXT("manifest untouched on save failure"), ManifestAfter == ManifestBefore);

	// The asset was not poisoned: retrying the same v2 immediately applies successfully.
	const FDesignReimportResult Retry = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("retry after rollback succeeds"), Retry.bSucceeded);
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("retry applies the design update"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 18 (integration): sibling-copy fallback - a missing manifest = full conflict; after the explicit choice a
// sibling WBP + its own manifest are generated and the original asset's bytes stay untouched; when the copy
// already exists, choosing explicitly again only reports a skip - no duplicate generation, no overwrite.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAResolutionCopySkipsExistingTest,
	"Design2UMG.SafeReimportQA.ResolutionCopySkipsExisting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAResolutionCopySkipsExistingTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportResolutionCopy");

	const FString V1 = MakeQADoc(MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = QAAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);
	const FString CopyPackage = FPaths::GetPath(TargetPackage) / TEXT("WBP_QAPage_Resolution");
	const FString CopyAssetFile = QAAssetFilePath(CopyPackage);

	// Create the full conflict: delete the sidecar.
	IFileManager::Get().Delete(*Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), false, true);

	// First explicit choice: the copy is generated.
	FDesignReimportResult Result = QAReimport(V1, nullptr, TargetPackage, /*bGenerateResolutionCopy=*/true);
	TestTrue(TEXT("full conflict still reported"), Result.bFullConflict);
	TestFalse(TEXT("in-place update did not happen"), Result.bSucceeded);
	TestEqual(TEXT("resolution copy path reported"),
		Result.ResolutionCopyPath, CopyPackage + TEXT(".WBP_QAPage_Resolution"));
	TestTrue(TEXT("copy asset file exists"), FPaths::FileExists(CopyAssetFile));
	FDesign2UMGGenerationManifestSidecar CopySidecar;
	TestTrue(TEXT("copy has its own manifest"), QALoadSidecar(CopyPackage, CopySidecar));
	TestTrue(TEXT("original asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);
	TestFalse(TEXT("original still has no manifest"),
		FPaths::FileExists(Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage)));
	const int64 CopyBytes = IFileManager::Get().FileSize(*CopyAssetFile);

	// Second explicit choice: the copy already exists -> skip, no overwrite.
	Result = QAReimport(V1, nullptr, TargetPackage, /*bGenerateResolutionCopy=*/true);
	TestTrue(TEXT("full conflict still reported on retry"), Result.bFullConflict);
	TestTrue(TEXT("no second copy path reported"), Result.ResolutionCopyPath.IsEmpty());
	TestTrue(TEXT("skip diagnosed on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_resolution_copy"),
			{ TEXT("Resolution copy skipped"), TEXT("already exists") }));
	TestEqual(TEXT("copy counted once"), QACountKind(Result, TEXT("reimport_resolution_copy")), 1);
	TestTrue(TEXT("existing copy asset untouched"), IFileManager::Get().FileSize(*CopyAssetFile) == CopyBytes);
	TestTrue(TEXT("original asset still untouched"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 19 (integration): plain-import regression surface - an existing asset name still goes through the
// auto-rename creation path (the old asset and old manifest untouched); in-place updates happen only on the Reimport path.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANormalImportAutoRenameTest,
	"Design2UMG.SafeReimportQA.NormalImportStillAutoRenames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANormalImportAutoRenameTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportAutoRename");

	const FString V1 = MakeQADoc(MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));

	// First plain import.
	UWidgetBlueprint* First = QAImport(V1, nullptr, OutputDir, /*bCleanFirst=*/true);
	if (!TestTrue(TEXT("first import generates"), First != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString FirstPackage = First->GetOutermost()->GetName();
	const FString FirstSidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(FirstPackage);
	FString FirstManifest;
	if (!TestTrue(TEXT("first manifest exists"), QAReadFileString(FirstSidecarPath, FirstManifest)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// Second plain import (asset name already taken): auto-rename creates anew, leaving the first one untouched.
	UWidgetBlueprint* Second = QAImport(V1, nullptr, OutputDir, /*bCleanFirst=*/false);
	TestNotNull(TEXT("second import generates a renamed asset"), Second);
	if (Second)
	{
		const FString SecondPackage = Second->GetOutermost()->GetName();
		TestTrue(TEXT("second asset is auto-renamed"), SecondPackage != FirstPackage);
		TestEqual(TEXT("auto-rename appends _1"), SecondPackage, FirstPackage + TEXT("_1"));
		TestTrue(TEXT("second import has its own manifest"),
			FPaths::FileExists(Design2UMGGenerationManifest::BuildSidecarPathForAsset(SecondPackage)));
	}

	// The original asset was not updated in place: loadable, manifest bytes unchanged.
	UWidgetBlueprint* FirstReloaded = LoadObject<UWidgetBlueprint>(
		nullptr, *(FirstPackage + TEXT(".") + FPaths::GetBaseFilename(FirstPackage)));
	TestNotNull(TEXT("original asset still loads"), FirstReloaded);
	FString FirstManifestAfter;
	TestTrue(TEXT("original manifest still readable"), QAReadFileString(FirstSidecarPath, FirstManifestAfter));
	TestTrue(TEXT("original manifest untouched by the second normal import"), FirstManifestAfter == FirstManifest);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 20 (integration): nested-structure boundary - design-added nested nodes inside an existing container go
// through the generation-chain rebuild; nested subtrees the design dropped keep their current state (the manifest records top-level instances only; nested deletion is outside the safe-delete scope).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedStructureBoundaryTest,
	"Design2UMG.SafeReimportQA.NestedAddAndRemovalBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedStructureBoundaryTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedBoundary");

	const FString V1 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
			*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v1")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2: the container's Head text changes + FreshLine is added; Tail disappears from the design.
	const FString V2 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v2")),
			*MakeQAText(TEXT("qa_fresh"), TEXT("FreshLine"), 32, 60, TEXT("Fresh v2")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("nested kept node refreshed"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}
	else
	{
		AddError(TEXT("Head widget missing"));
	}

	UTextBlock* Fresh = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("FreshLine")));
	TestNotNull(TEXT("nested new node added"), Fresh);
	if (Fresh)
	{
		TestEqual(TEXT("nested new node text applied"), Fresh->GetText().ToString(), FString(TEXT("Fresh v2")));
		TestEqual(TEXT("nested new node parented under the container"),
			Fresh->GetParent()->GetFName(), FName(TEXT("InfoPanel")));
	}
	TestTrue(TEXT("nested add diagnosed with parent context on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_added"), { TEXT("FreshLine"), TEXT("added under 'InfoPanel'") }));

	// The design-dropped nested subtree: kept as-is (not a manifest instance, never enters the safe-delete flow).
	TestNotNull(TEXT("design-removed nested node preserved"), WBP->WidgetTree->FindWidget(TEXT("Tail")));
	TestEqual(TEXT("no removal performed"), QACountKind(Result, TEXT("reimport_removed")), 0);
	TestEqual(TEXT("no removal conflict"), QACountKind(Result, TEXT("reimport_remove_conflict")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("container stays recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_panel")));
		TestTrue(TEXT("nested widgets stay owned"), Sidecar.D2UOwned.Contains(TEXT("Head")) && Sidecar.D2UOwned.Contains(TEXT("Tail")));
		TestTrue(TEXT("nested new node ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("FreshLine")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 21 (integration, R7a): kind-changed positive replacement in both directions - when the old subtree is
// purely D2U-owned, both mapped->generated and generated->mapped replace wholesale with the new form + a
// reimport_kind_changed diagnostic (one message naming the node and the direction) + a manifest kind update (contrast: the conflict branch is case 11).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAKindChangePositiveTest,
	"Design2UMG.SafeReimportQA.KindChangeReplacesPureSubtreeBothDirections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAKindChangePositiveTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportKindPositive");

	const auto BuildDoc = [](const TCHAR* Label)
	{
		return MakeQADoc(FString::Printf(
			TEXT("%s,%s"),
			*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, Label),
			*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep"))));
	};

	// v1: the mapping is active -> the component instance is in the mapped form.
	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("Checkout")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	if (!TestNotNull(TEXT("v1 mapped instance present"), QAFindMappedInstance(WBP)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// v2: the mapping is withdrawn -> the same design node flips mapped->generated; the old subtree is purely D2U-owned -> replaced wholesale.
	FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("Checkout")), nullptr, TargetPackage);
	TestTrue(TEXT("mapped->generated reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("kind change diagnosed once"), QACountKind(Result, TEXT("reimport_kind_changed")), 1);
	TestTrue(TEXT("transition message names node and direction"),
		QAHasMessageWithAll(Result, TEXT("reimport_kind_changed"), { TEXT("HeroButton"), TEXT("mapped -> generated") }));
	TestEqual(TEXT("no kind conflict on the pure subtree"), QACountKind(Result, TEXT("reimport_kind_conflict")), 0);

	bool bHasMappedAfterV2 = false;
	WBP->WidgetTree->ForEachWidget([&bHasMappedAfterV2](UWidget* Widget)
	{
		bHasMappedAfterV2 |= Widget->IsA<UDesign2UMGMappingQAWidget>();
	});
	TestFalse(TEXT("old mapped instance replaced away"), bHasMappedAfterV2);
	TestNotNull(TEXT("generated expansion present (component internals expanded)"),
		WBP->WidgetTree->FindWidget(TEXT("InnerLabel")));
	TestNotNull(TEXT("unrelated keeper untouched"), WBP->WidgetTree->FindWidget(TEXT("Keeper")));

	FDesign2UMGGenerationManifestSidecar SidecarAfterV2;
	if (TestTrue(TEXT("manifest reloads after mapped->generated"), QALoadSidecar(TargetPackage, SidecarAfterV2)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			SidecarAfterV2.FindInstanceByDesignNodeId(TEXT("qa_btn_1"));
		if (TestNotNull(TEXT("transitioned node stays recorded"), Record))
		{
			TestEqual(TEXT("manifest kind updated to generated"), Record->Kind, FString(TEXT("generated")));
			TestNotNull(TEXT("replaced record resolves to a live widget (no ghost)"),
				WBP->WidgetTree->FindWidget(FName(*Record->WidgetName)));
		}
	}

	// v3: the mapping is restored -> generated->mapped; the old expanded subtree is likewise purely D2U-owned -> replaced back into an instance.
	Result = QAReimport(BuildDoc(TEXT("Checkout")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("generated->mapped reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("reverse kind change diagnosed once"), QACountKind(Result, TEXT("reimport_kind_changed")), 1);
	TestTrue(TEXT("reverse transition message names node and direction"),
		QAHasMessageWithAll(Result, TEXT("reimport_kind_changed"), { TEXT("HeroButton"), TEXT("generated -> mapped") }));

	UDesign2UMGMappingQAWidget* Remapped = QAFindMappedInstance(WBP);
	TestNotNull(TEXT("mapped instance re-created"), Remapped);
	if (Remapped)
	{
		TestEqual(TEXT("declared input written on the new instance"), Remapped->TextValue.ToString(), FString(TEXT("Checkout")));
	}
	TestNull(TEXT("generated expansion replaced away"), WBP->WidgetTree->FindWidget(TEXT("InnerLabel")));

	FDesign2UMGGenerationManifestSidecar SidecarAfterV3;
	if (TestTrue(TEXT("manifest reloads after generated->mapped"), QALoadSidecar(TargetPackage, SidecarAfterV3)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			SidecarAfterV3.FindInstanceByDesignNodeId(TEXT("qa_btn_1"));
		if (TestNotNull(TEXT("transitioned node stays recorded"), Record))
		{
			TestEqual(TEXT("manifest kind updated back to mapped"), Record->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("manifest mapped class updated"), Record->MappedClass, FString(QAMappedClassPath));
			TestEqual(TEXT("manifest mapped key updated"), Record->MappedKey, FString(QAMappingKey));
			if (TestEqual(TEXT("input declaration recorded"), Record->Inputs.Num(), 1) && Record->Inputs.Num() == 1)
			{
				TestEqual(TEXT("input source recorded"), Record->Inputs[0].Source, FString(TEXT("label")));
				TestEqual(TEXT("input target recorded"), Record->Inputs[0].Target, FString(TEXT("TextValue")));
				TestEqual(TEXT("input valueKind recorded"), Record->Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
			}
		}
		TestNotNull(TEXT("keeper still recorded"), SidecarAfterV3.FindInstanceByDesignNodeId(TEXT("qa_keep")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 22 (integration, R7b): geometry and source-derived visual refresh of existing D2U-owned generated
// nodes - after v2 changes a purely generated node's position/size (the ApplySlotForNode path) and text fill
// color (the ApplyWidgetProperties path), reimport asserts the new values took effect.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAGeneratedGeometryVisualRefreshTest,
	"Design2UMG.SafeReimportQA.GeneratedGeometryAndVisualRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAGeneratedGeometryVisualRefreshTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportGeometryVisual");

	// Geometry observation target: a fill-less pure-container panel (Fixed size -> a direct canvas slot under the host root).
	const auto MakeGeoPanel = [](int32 X, int32 Y, int32 W, int32 H)
	{
		return FString::Printf(
			TEXT("{\"id\":\"qa_geo\",\"name\":\"GeoPanel\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},"
				 "\"children\":[{\"id\":\"qa_geo_pad\",\"name\":\"GeoPad\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":80,\"height\":18},"
				 "\"characters\":\"Pad\",%s}]}"),
			X, Y, W, H, X + 8, Y + 8, *QATextStyle());
	};

	// Visual observation target: a TEXT with a SOLID fill (fills -> ColorAndOpacity, a source-derived visual property).
	const auto MakeStyledText = [](double R, double G, double B, int32 X, int32 Y, int32 W, int32 H, const TCHAR* Text)
	{
		return FString::Printf(
			TEXT("{\"id\":\"qa_style\",\"name\":\"StatusLine\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},"
				 "\"characters\":\"%s\","
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":%.4f,\"g\":%.4f,\"b\":%.4f,\"a\":1}}],%s}"),
			X, Y, W, H, Text, R, G, B, *QATextStyle());
	};

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeGeoPanel(24, 24, 200, 120),
		*MakeStyledText(0.20, 0.20, 0.90, 24, 160, 90, 20, TEXT("v1 line"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v1 baseline: position (24,24), size 200x120, fill color rgb(51,51,230) (fills 0.20/0.20/0.90 -> bytes).
	UWidget* GeoPanel = WBP->WidgetTree->FindWidget(TEXT("GeoPanel"));
	UTextBlock* StatusLine = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("StatusLine")));
	if (!TestNotNull(TEXT("GeoPanel present"), GeoPanel) || !TestNotNull(TEXT("StatusLine present"), StatusLine))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UWidget> GeoPanelWeak = GeoPanel;
	if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(GeoPanel->Slot))
	{
		TestTrue(TEXT("v1 panel at design position"),
			FMath::Abs(Slot->GetPosition().X - 24.0f) < 1.0f && FMath::Abs(Slot->GetPosition().Y - 24.0f) < 1.0f);
		TestTrue(TEXT("v1 panel at design size"),
			FMath::Abs(Slot->GetSize().X - 200.0f) < 1.0f && FMath::Abs(Slot->GetSize().Y - 120.0f) < 1.0f);
	}
	else
	{
		AddError(TEXT("v1 GeoPanel slot is not a canvas slot"));
	}

	// v2: the panel moves to (60,96) and shrinks to 160x90; the text fill changes to 0.95/0.30/0.15 -> rgb(242,77,38).
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeGeoPanel(60, 96, 160, 90),
		*MakeStyledText(0.95, 0.30, 0.15, 40, 200, 150, 28, TEXT("v2 line"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);
	TestTrue(TEXT("generated nodes refreshed (diagnosed)"),
		QAHasMessage(Result, TEXT("reimport_updated"), TEXT("GeoPanel"))
		&& QAHasMessage(Result, TEXT("reimport_updated"), TEXT("StatusLine")));

	GeoPanel = WBP->WidgetTree->FindWidget(TEXT("GeoPanel"));
	StatusLine = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("StatusLine")));
	if (TestNotNull(TEXT("GeoPanel still present"), GeoPanel)
		&& TestNotNull(TEXT("StatusLine still present"), StatusLine))
	{
		// Geometry refresh: the ApplySlotForNode path (new position + new size).
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(GeoPanel->Slot))
		{
			TestTrue(TEXT("panel position refreshed from design"),
				FMath::Abs(Slot->GetPosition().X - 60.0f) < 1.0f && FMath::Abs(Slot->GetPosition().Y - 96.0f) < 1.0f);
			TestTrue(TEXT("panel size refreshed from design"),
				FMath::Abs(Slot->GetSize().X - 160.0f) < 1.0f && FMath::Abs(Slot->GetSize().Y - 90.0f) < 1.0f);
		}
		else
		{
			AddError(TEXT("GeoPanel slot is not a canvas slot after reimport"));
		}

		// Visual refresh: the ApplyWidgetProperties path (fills -> ColorAndOpacity) + the text content.
		const FColor ColorAfter = StatusLine->GetColorAndOpacity().GetSpecifiedColor().ToFColor(true);
		TestTrue(TEXT("text fill color refreshed from design"),
			FMath::Abs(ColorAfter.R - 242) <= 1 && FMath::Abs(ColorAfter.G - 77) <= 1 && FMath::Abs(ColorAfter.B - 38) <= 1);
		TestEqual(TEXT("text content refreshed from design"), StatusLine->GetText().ToString(), FString(TEXT("v2 line")));
	}

	// The node whose generated geometry changed keeps its identity (refreshed, not rebuilt).
	TestTrue(TEXT("GeoPanel widget identity preserved (refreshed in place)"), GeoPanelWeak.Get() == GeoPanel);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 23 (integration, R2): when a nested mapped instance's IR parent is a non-instance nested container
// (SidePanel>InnerZone>Button) and the structure is unchanged, reimport must not emit a misleading
// reimport_move_conflict (a diagnostic must mean a structural change was really skipped); the instance refreshes as usual.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedParentNoNoiseTest,
	"Design2UMG.SafeReimportQA.NestedParentNoMoveConflictNoise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedParentNoNoiseTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedNoNoise");

	// SidePanel > InnerZone (a nested pure container, not a manifest instance) > the mapped button.
	const auto BuildDoc = [](const TCHAR* Label)
	{
		return MakeQADoc(FString::Printf(
			TEXT("%s,%s"),
			*MakeQAPanel(TEXT("qa_side"), TEXT("SidePanel"), 24, 24, FString::Printf(
				TEXT("%s,%s"),
				*MakeQAPanel(TEXT("qa_inner"), TEXT("InnerZone"), 32, 32,
					MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 40, 40, Label)),
				*MakeQAText(TEXT("qa_spad"), TEXT("SidePad"), 240, 32, TEXT("Pad")))),
			*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 200, TEXT("Keep"))));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("Checkout")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappingQAWidget* Mapped = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("nested mapped instance generated"), Mapped)
		|| !TestTrue(TEXT("instance sits under InnerZone"),
			Mapped->GetParent() && Mapped->GetParent()->GetFName() == FName(TEXT("InnerZone"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = Mapped;

	// A structure-unchanged reimport (only the input value changes): no move-conflict noise.
	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("Pay Now")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("no move-conflict noise for the unchanged nested parent"),
		QACountKind(Result, TEXT("reimport_move_conflict")), 0);
	TestEqual(TEXT("no moves applied"), QACountKind(Result, TEXT("reimport_moved")), 0);

	UDesign2UMGMappingQAWidget* MappedAfter = MappedWeak.Get();
	if (TestNotNull(TEXT("instance still alive"), MappedAfter))
	{
		TestEqual(TEXT("instance stays under the nested container"),
			MappedAfter->GetParent()->GetFName(), FName(TEXT("InnerZone")));
		TestEqual(TEXT("instance still refreshed normally"), MappedAfter->TextValue.ToString(), FString(TEXT("Pay Now")));
	}
	TestTrue(TEXT("instance refresh diagnosed"), QAHasMessage(Result, TEXT("reimport_updated"), TEXT("HeroButton")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 24 (integration, R3 nested move): the design moves the nested node N from ContainerA to
// ContainerB - when the source end is purely D2U-owned, the widget moves (identity preserved, name
// unchanged, nothing left at the source, no copy built at the target) + a reimport_moved diagnostic; no false reimport_added.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedMoveIdentityTest,
	"Design2UMG.SafeReimportQA.NestedMovePreservesIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedMoveIdentityTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedMove");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint")))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20,
			MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UTextBlock* NBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (!TestNotNull(TEXT("nested N present in v1"), NBefore)
		|| !TestTrue(TEXT("N sits under ContainerA"),
			NBefore->GetParent() && NBefore->GetParent()->GetFName() == FName(TEXT("ContainerA"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UTextBlock> NWeak = NBefore;

	// v2: N moves from ContainerA to ContainerB.
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20,
			MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint"))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint"))))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	UTextBlock* NAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (TestNotNull(TEXT("N still present after move"), NAfter))
	{
		TestTrue(TEXT("widget object identity preserved"), NAfter == NWeak.Get());
		TestEqual(TEXT("N moved under ContainerB"), NAfter->GetParent()->GetFName(), FName(TEXT("ContainerB")));
		TestEqual(TEXT("moved N keeps its design text"), NAfter->GetText().ToString(), FString(TEXT("mv")));
	}
	TestNull(TEXT("no duplicate copy created under the target"), WBP->WidgetTree->FindWidget(TEXT("N_1")));
	TestEqual(TEXT("nested move diagnosed once"), QACountKind(Result, TEXT("reimport_moved")), 1);
	TestTrue(TEXT("nested move diagnostic names node and identity preservation"),
		QAHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("'N'"), TEXT("widget identity preserved") }));
	TestFalse(TEXT("N is not misreported as added"),
		QAHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 25 (integration, R3 nested move conflict): the developer already hung the nested node N under the
// project container DevPanel, and the design moves N to ContainerB - N stays in DevPanel, ContainerB must
// not get a new copy, and a reimport_move_conflict names N and the reason.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedMoveConflictNoCopyTest,
	"Design2UMG.SafeReimportQA.NestedMoveConflictCreatesNoCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedMoveConflictNoCopyTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedMoveConflict");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint")))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20,
			MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Developer customization: the project container DevPanel is hung into the host root and N is moved into it.
	UVerticalBox* DevPanel = WBP->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DevPanel"));
	if (UPanelWidget* Host = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		Host->AddChild(DevPanel);
	}
	UWidget* N = WBP->WidgetTree->FindWidget(TEXT("N"));
	if (!TestNotNull(TEXT("N present before customization"), N) || !TestTrue(TEXT("DevPanel added"), DevPanel != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	WBP->WidgetTree->RemoveWidget(N);
	DevPanel->AddChild(N);
	const TWeakObjectPtr<UWidget> NWeak = N;

	// v2: the design moves N to ContainerB.
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20,
			MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint"))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint"))))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UWidget* NAfter = NWeak.Get();
	if (TestNotNull(TEXT("N still alive"), NAfter))
	{
		TestEqual(TEXT("N kept under the project container DevPanel"),
			NAfter->GetParent()->GetFName(), FName(TEXT("DevPanel")));
	}
	TestNull(TEXT("no copy created under ContainerB"), WBP->WidgetTree->FindWidget(TEXT("N_1")));
	TestEqual(TEXT("nested move conflict diagnosed once"), QACountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("conflict names N and the project-container reason"),
		QAHasMessageWithAll(Result, TEXT("reimport_move_conflict"),
			{ TEXT("'N'"), TEXT("project-side container"), TEXT("no copy created") }));
	TestFalse(TEXT("N is not misreported as added"), QAHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 26 (integration, R3 nested delete conflict): the design-deleted nested node Tail contains a project
// child (DevChild) - Tail is kept + a reimport_remove_conflict naming the node and the reason;
// the silent keep of purely-D2U nested leftovers is covered by case 20 (NestedAddAndRemovalBoundary).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedRemoveConflictTest,
	"Design2UMG.SafeReimportQA.NestedRemoveConflictNamesNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedRemoveConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedRemoveConflict");

	const FString V1 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24, FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
		*MakeQAPanel(TEXT("qa_tail"), TEXT("Tail"), 32, 60,
			MakeQAText(TEXT("qa_tail_in"), TEXT("TailInner"), 40, 66, TEXT("inner"))))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Developer customization: the project child DevChild is hung into the nested node Tail.
	UWidget* Tail = WBP->WidgetTree->FindWidget(TEXT("Tail"));
	if (!TestNotNull(TEXT("nested Tail present in v1"), Tail))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UTextBlock* DevChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DevChild"));
	DevChild->SetText(FText::FromString(TEXT("dev")));
	Cast<UPanelWidget>(Tail)->AddChild(DevChild);

	// v2: Tail disappears from the design (Head stays and updates).
	const FString V2 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
		MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v2"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNotNull(TEXT("design-removed nested node preserved"), WBP->WidgetTree->FindWidget(TEXT("Tail")));
	TestNotNull(TEXT("project child stays inside the preserved node"),
		WBP->WidgetTree->FindWidget(TEXT("DevChild")));
	TestEqual(TEXT("nested remove conflict diagnosed once"), QACountKind(Result, TEXT("reimport_remove_conflict")), 1);
	TestTrue(TEXT("conflict names the node and the project-content reason"),
		QAHasMessageWithAll(Result, TEXT("reimport_remove_conflict"), { TEXT("'Tail'"), TEXT("DevChild") }));
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("kept sibling still refreshed"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 27 (integration, host-root form change): v1's interactive document generates the synthesized switcher
// host root (root_pc becomes a top-level instance); in v2, after the interactions vanish, root_pc directly
// becomes the IR root. Reimport must neither misreport root_pc's nested children as "host-root top-level
// added" (Keep_1) nor misreport root_pc itself as a move conflict; the deleted nested node N containing project content gets a remove-conflict diagnostic.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAHostRootShapeChangeTest,
	"Design2UMG.SafeReimportQA.HostRootShapeChangeNoMisdiagnosis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAHostRootShapeChangeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportHostRootShape");

	// v1: root_pc holds a button with an ON_CLICK interaction (a valid document navigation -> the switcher root is synthesized).
	const auto MakeInteractiveButton = []()
	{
		return FString::Printf(
			TEXT("{\"id\":\"pc_btn\",\"name\":\"Btn\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.2,\"b\":0.2,\"a\":1}}],"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"https://qa.example/rm\"}]}],"
				 "\"children\":[{\"id\":\"pc_btn_t\",\"name\":\"BtnText\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":48,\"y\":48,\"width\":80,\"height\":20},\"characters\":\"press\",%s}]}"),
			*QATextStyle());
	};
	const auto MakeRoot = [&MakeInteractiveButton](bool bWithN)
	{
		const FString NBlock = FString::Printf(
			TEXT("{\"id\":\"pc_n\",\"name\":\"N\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":180,\"height\":120},"
				 "\"children\":[%s]},"),
			*MakeInteractiveButton());
		const FString KeepBlock = FString::Printf(
			TEXT("{\"id\":\"pc_other\",\"name\":\"Keep\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":260,\"y\":40,\"width\":120,\"height\":30},"
				 "\"characters\":\"keep\",%s}"),
			*QATextStyle());
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"root_pc\",\"name\":\"RemoveProjChild\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":440,\"height\":240},"
				 "\"children\":[%s%s]}]}]}}"),
			QAReimportScope,
			bWithN ? *NBlock : TEXT(""),
			*KeepBlock);
	};

	UWidgetBlueprint* WBP = QAImport(MakeRoot(true), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Precondition check: the synthesized switcher root + root_pc are top-level instances.
	if (!TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UWidget* RootPc = WBP->WidgetTree->FindWidget(TEXT("root_pc"));
	UWidget* N = WBP->WidgetTree->FindWidget(TEXT("N"));
	if (!TestNotNull(TEXT("root_pc is the page under the switcher"), RootPc)
		|| !TestNotNull(TEXT("nested N present in v1"), N))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UTextBlock* DevChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DevChild"));
	DevChild->SetText(FText::FromString(TEXT("dev")));
	Cast<UPanelWidget>(N)->AddChild(DevChild);

	// v2: the interaction and N vanish together -> no valid navigation -> root_pc directly becomes the root.
	const FDesignReimportResult Result = QAReimport(MakeRoot(false), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("Keep is not duplicated onto the host root"), WBP->WidgetTree->FindWidget(TEXT("Keep_1")));
	TestNotNull(TEXT("Keep stays under root_pc"), WBP->WidgetTree->FindWidget(TEXT("Keep")));
	TestEqual(TEXT("no false top-level add"), QACountKind(Result, TEXT("reimport_added")), 0);
	TestEqual(TEXT("no false move conflict for the page root"), QACountKind(Result, TEXT("reimport_move_conflict")), 0);
	TestEqual(TEXT("N design-deletion reported as remove conflict"), QACountKind(Result, TEXT("reimport_remove_conflict")), 1);
	TestTrue(TEXT("remove conflict names N"),
		QAHasMessage(Result, TEXT("reimport_remove_conflict"), TEXT("'N'")));
	TestNotNull(TEXT("N preserved with its project child"), WBP->WidgetTree->FindWidget(TEXT("DevChild")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 28 (integration, S1 pre-save writability check): scenario B (sidecar read-only only)
// and scenario A (uasset + sidecar read-only) must both be intercepted before entering the save flow -
// the uasset does not land a new state, the sidecar is unchanged, completed is not reported, bSucceeded=false, and the diagnostic names the concrete file.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASaveWritabilityPrecheckTest,
	"Design2UMG.SafeReimportQA.SaveWritabilityPrecheckBlocksHalfUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASaveWritabilityPrecheckTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportWritability");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("fail-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = QAAssetFilePath(TargetPackage);
	const FString SidecarFile = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	const FMD5Hash AssetHashBefore = FMD5Hash::HashFile(*AssetFile);
	FString SidecarBefore;
	if (!TestTrue(TEXT("v1 manifest exists"), QAReadFileString(SidecarFile, SidecarBefore)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString V2 = BuildDoc(TEXT("fail-v2"));

	// Restore-writability safety net (an early return on a failed assertion must not leave a read-only file behind for cleanup either).
	const auto MakeWritable = [&]()
	{
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*AssetFile, false);
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, false);
	};

	// Scenario B: sidecar read-only only. The pre-check must stop the save: never the half-updated
	// combination "uasset updated + sidecar still old".
	TestTrue(TEXT("sidecar set read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, true));
	{
		const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
		TestFalse(TEXT("scenario B reports failure"), Result.bSucceeded);
		TestFalse(TEXT("scenario B is not a full conflict"), Result.bFullConflict);
		TestEqual(TEXT("scenario B save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
		TestTrue(TEXT("scenario B diagnostic names the blocked sidecar file"),
			QAHasMessageWithAll(Result, TEXT("reimport_save_failed"), { *SidecarFile, TEXT("read-only") }));
		TestEqual(TEXT("scenario B does not report completion"), QACountKind(Result, TEXT("reimport_completed")), 0);
		// Combined assertion: the disk is completely untouched (uasset hash unchanged + sidecar bytes unchanged).
		TestTrue(TEXT("scenario B leaves the uasset untouched on disk"),
			FMD5Hash::HashFile(*AssetFile) == AssetHashBefore);
		FString SidecarNow;
		TestTrue(TEXT("scenario B sidecar still readable"), QAReadFileString(SidecarFile, SidecarNow));
		TestTrue(TEXT("scenario B sidecar untouched"), SidecarNow == SidecarBefore);
		if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
		{
			TestEqual(TEXT("scenario B in-memory text rolled back"), Head->GetText().ToString(), FString(TEXT("fail-v1")));
		}
	}
	FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, false);

	// Scenario A: uasset + sidecar both read-only (the full Perforce-unchecked-out shape).
	TestTrue(TEXT("uasset set read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*AssetFile, true));
	TestTrue(TEXT("sidecar set read-only again"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, true));
	{
		const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
		TestFalse(TEXT("scenario A reports failure"), Result.bSucceeded);
		TestEqual(TEXT("scenario A save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
		TestTrue(TEXT("scenario A diagnostic names both blocked files"),
			QAHasMessage(Result, TEXT("reimport_save_failed"), *AssetFile)
			&& QAHasMessage(Result, TEXT("reimport_save_failed"), *SidecarFile));
		TestEqual(TEXT("scenario A does not report completion"), QACountKind(Result, TEXT("reimport_completed")), 0);
		TestTrue(TEXT("scenario A leaves the uasset untouched on disk"),
			FMD5Hash::HashFile(*AssetFile) == AssetHashBefore);
		FString SidecarNow;
		TestTrue(TEXT("scenario A sidecar still readable"), QAReadFileString(SidecarFile, SidecarNow));
		TestTrue(TEXT("scenario A sidecar untouched"), SidecarNow == SidecarBefore);
	}
	MakeWritable();

	// After writability is restored, retrying the same v2 succeeds (the asset was not poisoned by the two failed rounds).
	const FDesignReimportResult Retry = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("retry succeeds after restoring writability"), Retry.bSucceeded);
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("retry applies the design update"), Head->GetText().ToString(), FString(TEXT("fail-v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 29 (integration, S2 sidecar write-failure propagation): the uasset saved but the manifest write failed
// (an IO failure the pre-check cannot catch) - completed is not reported; a reimport_save_failed states
// that asset and record may be inconsistent, bSucceeded=false; memory keeps the new state (the disk already holds the new asset).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAManifestWriteFailurePropagatesTest,
	"Design2UMG.SafeReimportQA.ManifestWriteFailurePropagates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAManifestWriteFailurePropagatesTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("automation-injected sidecar write failure"), EAutomationExpectedErrorFlags::Contains, 0);

	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportManifestWriteFail");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("half-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString SidecarFile = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	FString SidecarV1;
	if (!TestTrue(TEXT("v1 manifest exists"), QAReadFileString(SidecarFile, SidecarV1)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// Inject the sidecar write failure (a genuinely read-only sidecar would be caught by the pre-check, so this branch is unreachable otherwise).
	Design2UMGGenerationManifest::ArmAutomationSidecarWriteFailureOnce();
	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("half-v2")), nullptr, TargetPackage);

	TestFalse(TEXT("manifest write failure is not reported as success"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
	TestTrue(TEXT("diagnostic states asset/manifest inconsistency"),
		QAHasMessageWithAll(Result, TEXT("reimport_save_failed"),
			{ TEXT("was saved"), TEXT("manifest could not be written"), TEXT("inconsistent") }));
	TestTrue(TEXT("underlying manifest_write_failed surfaces in the report"),
		QACountKind(Result, TEXT("manifest_write_failed")) >= 1);
	TestEqual(TEXT("completion not reported"), QACountKind(Result, TEXT("reimport_completed")), 0);

	// The uasset saved the new state: memory and disk agree (not rolled back to the old value).
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("in-memory text stays at the saved v2 value"), Head->GetText().ToString(), FString(TEXT("half-v2")));
	}
	FString SidecarAfter;
	TestTrue(TEXT("sidecar still readable"), QAReadFileString(SidecarFile, SidecarAfter));
	TestTrue(TEXT("sidecar keeps the stale v1 content (inconsistency is reported, not hidden)"),
		SidecarAfter == SidecarV1);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 30 (integration, S4 dedicated mapped-interaction diagnostic): a design interaction attached to a mapped
// component also emits a dedicated "inert" entry at reimport (mapping_prototype_inert, naming the trigger
// and type), not just the generic mapped-refresh entry.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappedInteractionInertTest,
	"Design2UMG.SafeReimportQA.MappedInteractionInertDiagnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappedInteractionInertTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappedInert");

	// A mapped button + an ON_CLICK design interaction (attached to the mapped instance body).
	const auto BuildDoc = [](const TCHAR* Label)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"QADoc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"qa_root\",\"name\":\"QAPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},\"children\":["
				 "{\"id\":\"qa_mib\",\"name\":\"HeroButton\",\"type\":\"INSTANCE\","
				 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
				 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
				 "\"absoluteBoundingBox\":{\"x\":24,\"y\":24,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}],"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"https://qa.example/inert\"}]}],"
				 "\"children\":[{\"id\":\"qa_mib_label\",\"name\":\"InnerLabel\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":32,\"y\":36,\"width\":80,\"height\":18},\"characters\":\"%s\",%s}]}"
				 "]}]}]}}"),
			QAReimportScope, QAReimportComponentKey, Label, Label, *QATextStyle());
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("Checkout")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestNotNull(TEXT("mapped instance generated"), QAFindMappedInstance(WBP)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("Pay Now")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestTrue(TEXT("dedicated inert diagnostic present and names trigger + type"),
		QAHasMappingMessageWithAll(Result, TEXT("mapping_prototype_inert"),
			{ TEXT("qa_mib"), TEXT("ON_CLICK"), TEXT("mapped WBP interactions are project-owned") }));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 31 (integration, S5 nested-update diagnostic granularity): property refreshes of nested D2U-owned
// nodes also emit reimport_updated naming the node (diagnostic granularity equal to the top level), not just top-level instances.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedUpdateDiagnosticTest,
	"Design2UMG.SafeReimportQA.NestedUpdateDiagnosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedUpdateDiagnosticTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedUpdateDiag");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			MakeQAText(TEXT("qa_t"), TEXT("Status"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("life-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("life-v2")), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestTrue(TEXT("nested node update diagnosed with node name and design id"),
		QAHasMessageWithAll(Result, TEXT("reimport_updated"), { TEXT("Status"), TEXT("qa_t") }));
	if (UTextBlock* Status = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Status"))))
	{
		TestEqual(TEXT("nested text actually refreshed"), Status->GetText().ToString(), FString(TEXT("life-v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 32 (integration, slice C C1): under a multi-page prototype document (synthesized switcher host root),
// the mapped instance gains an interaction in v2 (the same-named IR-side node becomes the trigger wrapper
// form named by node id). The reimport update plan must handle it via the Matched path: the instance is not
// created twice, no node-id-named copy appears, the sidecar holds no double record for the same designNodeId, and no false "content slot occupied" move conflict.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASwitcherMappedNoDuplicateTest,
	"Design2UMG.SafeReimportQA.SwitcherHostMappedNoDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASwitcherMappedNoDuplicateTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSwitcherMapped");

	// The mapped instance: no interaction in v1; in v2 it gains an ON_CLICK NODE navigation (becoming the trigger wrapper form).
	const auto MakeMappedButton = [](const TCHAR* Label, const bool bWithInteraction)
	{
		const FString Interactions = bWithInteraction
			? TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"c41_pagea\",\"navigation\":\"NAVIGATE\"}]}]")
			: FString();
		return FString::Printf(
			TEXT("{\"id\":\"qa_btn_1\",\"name\":\"PrimaryButton\",\"type\":\"INSTANCE\","
				 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
				 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
				 "\"absoluteBoundingBox\":{\"x\":24,\"y\":24,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}]%s}"),
			QAReimportComponentKey, Label, *Interactions);
	};
	// The text inside PageA carries a cross-page navigation (making the document a multi-page prototype; v1/v2 both synthesize the switcher host root).
	const auto MakePageAText = []()
	{
		return FString::Printf(
			TEXT("{\"id\":\"c41_pat\",\"name\":\"PageAText\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":90,\"height\":20},\"characters\":\"to-main\",%s,"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"c41_page\",\"navigation\":\"NAVIGATE\"}]}]}"),
			*QATextStyle());
	};
	const auto MakeSwitcherDoc = [&MakeMappedButton, &MakePageAText](const bool bButtonInteractive)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"c41_page\",\"name\":\"MainPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],"
				 "\"children\":[%s]},"
				 "{\"id\":\"c41_pagea\",\"name\":\"PageA\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":400,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.8,\"g\":0.8,\"b\":0.8,\"a\":1}}],"
				 "\"children\":[%s]}]}]}}"),
			QAReimportScope,
			*MakeMappedButton(bButtonInteractive ? TEXT("proto-v2") : TEXT("proto-v1"), bButtonInteractive),
			*MakePageAText());
	};

	UWidgetBlueprint* WBP = QAImport(MakeSwitcherDoc(false), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	if (!TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("v1 mapped instance generated"), MappedBefore)
		|| !TestEqual(TEXT("v1 mapped instance keeps its design name"), MappedBefore->GetFName(), FName(TEXT("PrimaryButton")))
		|| !TestTrue(TEXT("v1 mapped instance sits under the page inner layer"),
			MappedBefore->GetParent() && MappedBefore->GetParent()->GetFName() == FName(TEXT("MainPage_inner"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;

	const FDesignReimportResult Result = QAReimport(MakeSwitcherDoc(true), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	// The only instance + identity preserved + name unchanged (no node-id-named copy appears).
	int32 MappedInstanceCount = 0;
	UDesign2UMGMappingQAWidget* MappedAfter = nullptr;
	WBP->WidgetTree->ForEachWidget([&MappedInstanceCount, &MappedAfter](UWidget* Widget)
	{
		if (UDesign2UMGMappingQAWidget* Candidate = Cast<UDesign2UMGMappingQAWidget>(Widget))
		{
			++MappedInstanceCount;
			MappedAfter = Candidate;
		}
	});
	TestEqual(TEXT("exactly one mapped instance in the tree"), MappedInstanceCount, 1);
	TestNull(TEXT("no duplicate named after the design node id"),
		WBP->WidgetTree->FindWidget(TEXT("qa_btn_1")));
	if (TestNotNull(TEXT("mapped instance still alive"), MappedAfter))
	{
		TestTrue(TEXT("mapped instance object identity preserved"), MappedAfter == MappedWeak.Get());
		TestEqual(TEXT("mapped instance name unchanged"), MappedAfter->GetFName(), FName(TEXT("PrimaryButton")));
		// The Matched path's inputs are overwritten directly.
		TestEqual(TEXT("declared input overwritten with the v2 design value"),
			MappedAfter->TextValue.ToString(), FString(TEXT("proto-v2")));
	}

	// Diagnostics: the refresh is named; no reimport_added (no duplicate creation); no false move conflict.
	TestTrue(TEXT("mapped refresh diagnosed"),
		QAHasMessageWithAll(Result, TEXT("reimport_updated"), { TEXT("PrimaryButton"), TEXT("qa_btn_1") }));
	TestEqual(TEXT("no added duplicate"), QACountKind(Result, TEXT("reimport_added")), 0);
	TestEqual(TEXT("no false move conflict from the synthetic inner layer"),
		QACountKind(Result, TEXT("reimport_move_conflict")), 0);

	// Sidecar: only one mapped record per designNodeId.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		int32 RecordsForButton = 0;
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* ButtonRecord = nullptr;
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Sidecar.Instances)
		{
			if (Record.DesignNodeId == TEXT("qa_btn_1"))
			{
				++RecordsForButton;
				ButtonRecord = &Record;
			}
		}
		TestEqual(TEXT("single manifest record for the design node"), RecordsForButton, 1);
		if (TestNotNull(TEXT("mapped record present"), ButtonRecord))
		{
			TestEqual(TEXT("record kind mapped"), ButtonRecord->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("record widget name preserved"), ButtonRecord->WidgetName, FString(TEXT("PrimaryButton")));
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 33 (integration, slice C C2): after the canvas container gains a visual fill in v2, the converter
// inserts a synthetic inner layer for it (Border + inner canvas). Both the container's existing nested
// children and the newly moved-in node must pierce the synthetic layer to complete same-name matching:
// M refreshes in place, N moves from A into C (identity preserved), with no duplicate copies and no false additions.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQACompositeLayerNestedMoveTest,
	"Design2UMG.SafeReimportQA.CompositeLayerNestedMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQACompositeLayerNestedMoveTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportCompositeLayerMove");

	// C: a no-auto-layout container; bWithFill decides whether this version synthesizes the inner layer. A: a VERTICAL container.
	const auto MakeBox = [](const TCHAR* Id, const TCHAR* Name, const bool bWithFill, const bool bVertical, const FString& Children)
	{
		const FString Fills = bWithFill
			? TEXT("\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],")
			: FString();
		const FString Layout = bVertical ? TEXT("\"layoutMode\":\"VERTICAL\",") : FString();
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":20,\"y\":20,\"width\":200,\"height\":120},%s%s\"children\":[%s]}"),
			Id, Name, *Fills, *Layout, *Children);
	};

	// v1: C has no fill (no inner layer) and directly holds M; A (VERTICAL) holds N.
	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeBox(TEXT("qa_c"), TEXT("C"), false, false, MakeQAText(TEXT("qa_m"), TEXT("M"), 40, 40, TEXT("m-v1"))),
		*MakeBox(TEXT("qa_a"), TEXT("A"), true, true, MakeQAText(TEXT("qa_n"), TEXT("N"), 40, 40, TEXT("mv")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UTextBlock* MBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("M")));
	UTextBlock* NBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	UWidget* CBefore = WBP->WidgetTree->FindWidget(TEXT("C"));
	if (!TestNotNull(TEXT("M present in v1"), MBefore)
		|| !TestNotNull(TEXT("N present in v1"), NBefore)
		|| !TestNotNull(TEXT("C present in v1"), CBefore)
		|| !TestTrue(TEXT("v1 M sits directly under C (no synthetic layer)"),
			MBefore->GetParent() && MBefore->GetParent()->GetFName() == FName(TEXT("C"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UTextBlock> MWeak = MBefore;
	const TWeakObjectPtr<UTextBlock> NWeak = NBefore;

	// v2: C gains a fill (the converter inserts the synthetic inner layer) and N moves from A into C.
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeBox(TEXT("qa_c"), TEXT("C"), true, false, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_m"), TEXT("M"), 40, 40, TEXT("m-v2")),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 40, 80, TEXT("mv")))),
		*MakeBox(TEXT("qa_a"), TEXT("A"), true, true, FString())));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	UTextBlock* MAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("M")));
	UTextBlock* NAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (TestNotNull(TEXT("M still present"), MAfter))
	{
		TestTrue(TEXT("M identity preserved through the synthetic layer"), MAfter == MWeak.Get());
		TestEqual(TEXT("M keeps its parent C"), MAfter->GetParent()->GetFName(), FName(TEXT("C")));
		TestEqual(TEXT("M refreshed from the design"), MAfter->GetText().ToString(), FString(TEXT("m-v2")));
	}
	if (TestNotNull(TEXT("N still present"), NAfter))
	{
		TestTrue(TEXT("N identity preserved by the nested move"), NAfter == NWeak.Get());
		TestEqual(TEXT("N moved under C"), NAfter->GetParent()->GetFName(), FName(TEXT("C")));
		TestEqual(TEXT("moved N keeps its design text"), NAfter->GetText().ToString(), FString(TEXT("mv")));
	}
	TestNull(TEXT("no duplicate M created"), WBP->WidgetTree->FindWidget(TEXT("M_1")));
	TestNull(TEXT("no duplicate N created"), WBP->WidgetTree->FindWidget(TEXT("N_1")));
	TestTrue(TEXT("nested move diagnosed with identity preservation"),
		QAHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("'N'"), TEXT("widget identity preserved") }));
	TestFalse(TEXT("N is not misreported as added"),
		QAHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 34 (integration, slice C C3): a single-page canvas root (with a fill -> the host root is a Border +
// synthetic inner layer, and the manifest's top-level instance records the inner layer) deletes the page's only
// subtree N: the safe-delete diagnostic must also name the design nodes inside the deleted subtree (widget
// names derive deterministically from design node names), not just reference the host synthetic layer.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAEmptyRootRemovalNamesDesignNodesTest,
	"Design2UMG.SafeReimportQA.EmptyRootRemovalNamesDesignNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAEmptyRootRemovalNamesDesignNodesTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportEmptyRootRemoval");

	const auto MakeFilledRootDoc = [](const FString& Children)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"qa_root\",\"name\":\"QAPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],"
				 "\"children\":[%s]}]}]}}"),
			QAReimportScope, *Children);
	};
	// The page's only subtree: N (VERTICAL with a fill -> Border) containing a Caption text.
	const FString NSubtree = FString::Printf(
		TEXT("{\"id\":\"qa_n\",\"name\":\"N\",\"type\":\"FRAME\",\"layoutMode\":\"VERTICAL\","
			 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":180,\"height\":120},"
			 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.8,\"g\":0.8,\"b\":0.8,\"a\":1}}],"
			 "\"children\":[%s]}"),
		*MakeQAText(TEXT("qa_cap"), TEXT("Caption"), 48, 48, TEXT("cap")));

	UWidgetBlueprint* WBP = QAImport(MakeFilledRootDoc(NSubtree), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	if (!TestNotNull(TEXT("v1 N present"), WBP->WidgetTree->FindWidget(TEXT("N")))
		|| !TestNotNull(TEXT("v1 Caption present"), WBP->WidgetTree->FindWidget(TEXT("Caption")))
		|| !TestNotNull(TEXT("v1 inner layer present"), WBP->WidgetTree->FindWidget(TEXT("QAPage_inner"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// v2: delete the only subtree; the page becomes an empty root.
	const FDesignReimportResult Result = QAReimport(MakeFilledRootDoc(FString()), nullptr, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("N removed"), WBP->WidgetTree->FindWidget(TEXT("N")));
	TestNull(TEXT("Caption removed"), WBP->WidgetTree->FindWidget(TEXT("Caption")));
	TestNull(TEXT("inner layer removed"), WBP->WidgetTree->FindWidget(TEXT("QAPage_inner")));

	// Diagnostic granularity: beyond the host synthetic layer, the design nodes (N, Caption) must be named.
	TestEqual(TEXT("one removal diagnostic"), QACountKind(Result, TEXT("reimport_removed")), 1);
	TestTrue(TEXT("removal diagnostic names the synthetic host layer and the design nodes"),
		QAHasMessageWithAll(Result, TEXT("reimport_removed"),
			{ TEXT("QAPage_inner"), TEXT("N"), TEXT("Caption"), TEXT("removed through the safe delete flow") }));

	// An empty page is a legal end state: the sidecar is written with 0 instances. Slice B semantics make the
	// load side reject an empty-instances sidecar (the next reimport goes full conflict for an unclear
	// relationship), so here the raw JSON is read directly to assert the records really emptied, instead of going through TryLoadSidecarForAsset.
	FString RawSidecar;
	if (TestTrue(TEXT("sidecar file readable"), QAReadFileString(Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), RawSidecar)))
	{
		TSharedPtr<FJsonObject> SidecarRoot;
		if (TestTrue(TEXT("sidecar JSON parses"), QAParseJsonObject(RawSidecar, SidecarRoot)))
		{
			const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
			if (TestTrue(TEXT("instances array present"), SidecarRoot->TryGetArrayField(TEXT("instances"), Instances) && Instances != nullptr))
			{
				TestEqual(TEXT("inner layer instance dropped from the manifest"), Instances->Num(), 0);
			}
			const TArray<TSharedPtr<FJsonValue>>* Owned = nullptr;
			if (TestTrue(TEXT("d2uOwned array present"), SidecarRoot->TryGetArrayField(TEXT("d2uOwned"), Owned) && Owned != nullptr))
			{
				// The host-root Border never enters d2uOwned anyway (the generation chain does not record the root widget);
				// an empty owned list after deleting the only subtree is a legal end state.
				TArray<FString> OwnedNames;
				for (const TSharedPtr<FJsonValue>& OwnedValue : *Owned)
				{
					OwnedNames.Add(OwnedValue->AsString());
				}
				TestFalse(TEXT("N dropped from d2uOwned"), OwnedNames.Contains(TEXT("N")));
				TestFalse(TEXT("Caption dropped from d2uOwned"), OwnedNames.Contains(TEXT("Caption")));
				TestFalse(TEXT("inner layer dropped from d2uOwned"), OwnedNames.Contains(TEXT("QAPage_inner")));
			}
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 35 (integration, slice C 5.1 continuous lifecycle): v1 import -> the developer adds a real event binding
// to the trigger button via the C4 channel (FDelegateEditorBinding + a DevHandler function graph) and adds a
// project child -> v2 reimport (asserting the project logic survives, the design updates apply, the D2U binding
// yields on the double trigger) -> v3 reimport (asserting the widget relations and identity stay stable, the binding survives, the manifest stays consistent).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAContinuousLifecycleBindingsTest,
	"Design2UMG.SafeReimportQA.ContinuousLifecycleWithBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAContinuousLifecycleBindingsTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportLifecycleBindings");

	const auto MakeLifeDoc = [](const TCHAR* ButtonLabel, const TCHAR* HeadText)
	{
		return MakeQADoc(FString::Printf(
			TEXT("%s,%s,%s"),
			*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, ButtonLabel),
			*MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 180, 24,
				MakeQAText(TEXT("qa_head"), TEXT("Head"), 188, 32, HeadText)),
			*MakeQAInteractive(TEXT("qa_link"), TEXT("JumpLink"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/jump"))));
	};

	UWidgetBlueprint* WBP = QAImport(MakeLifeDoc(TEXT("life-v1"), TEXT("head-v1")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UWidget* LinkButton = WBP->WidgetTree->FindWidget(TEXT("qa_link"));
	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	UTextBlock* HeadBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head")));
	if (!TestNotNull(TEXT("trigger button widget present"), LinkButton)
		|| !TestNotNull(TEXT("mapped instance present"), MappedBefore)
		|| !TestNotNull(TEXT("Head text present"), HeadBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UWidget> LinkWeak = LinkButton;
	const TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;
	const TWeakObjectPtr<UTextBlock> HeadWeak = HeadBefore;

	// Developer customization: the C4 channel adds a real event binding (function graph + FDelegateEditorBinding).
	FString BindingError;
	if (!TestTrue(TEXT("C4 channel adds the event binding"),
			Design2UMGWidgetBindingTools::AddEventBinding(
				WBP, TEXT("qa_link"), TEXT("OnClicked"), TEXT("DevHandler"), BindingError)))
	{
		AddError(FString::Printf(TEXT("AddEventBinding failed: %s"), *BindingError));
		QAWipeOutput(OutputDir);
		return false;
	}
	// Idempotent: adding the same binding twice does not create a second one.
	TestTrue(TEXT("AddEventBinding is idempotent"),
		Design2UMGWidgetBindingTools::AddEventBinding(
			WBP, TEXT("qa_link"), TEXT("OnClicked"), TEXT("DevHandler"), BindingError));
	int32 BindingCount = 0;
	for (const FDelegateEditorBinding& Binding : WBP->Bindings)
	{
		BindingCount += (Binding.ObjectName == TEXT("qa_link") && Binding.PropertyName == TEXT("OnClicked")) ? 1 : 0;
	}
	TestEqual(TEXT("exactly one editor binding for the trigger"), BindingCount, 1);

	UTextBlock* ProjectNote = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProjectNote"));
	if (UPanelWidget* InfoPanel = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(TEXT("InfoPanel"))))
	{
		InfoPanel->AddChild(ProjectNote);
	}

	// v2 reimport: design values updated + project logic kept + double-trigger yields.
	const FDesignReimportResult ResultV2 = QAReimport(MakeLifeDoc(TEXT("life-v2"), TEXT("head-v2")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), ResultV2.bSucceeded);

	const bool bV2HasDevBinding = WBP->Bindings.ContainsByPredicate([](const FDelegateEditorBinding& Binding)
	{
		return Binding.ObjectName == TEXT("qa_link")
			&& Binding.PropertyName == TEXT("OnClicked")
			&& Binding.FunctionName == TEXT("DevHandler");
	});
	TestTrue(TEXT("project event binding preserved across v2 reimport"), bV2HasDevBinding);
	TestNotNull(TEXT("DevHandler function still resolvable"),
		WBP->SkeletonGeneratedClass ? WBP->SkeletonGeneratedClass->FindFunctionByName(TEXT("DevHandler")) : nullptr);
	TestNotNull(TEXT("project-added child preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectNote")));
	TestTrue(TEXT("D2U click binding yields to the project binding (dual trigger)"),
		QAHasMessageWithAll(ResultV2, TEXT("reimport_prototype_conflict"),
			{ TEXT("trigger 'qa_link'"), TEXT("project behavior kept") }));
	if (UDesign2UMGMappingQAWidget* MappedV2 = Cast<UDesign2UMGMappingQAWidget>(MappedWeak.Get()))
	{
		TestTrue(TEXT("mapped identity preserved across v2"), MappedV2 == MappedBefore);
		TestEqual(TEXT("mapped input refreshed in v2"), MappedV2->TextValue.ToString(), FString(TEXT("life-v2")));
	}
	if (UTextBlock* HeadV2 = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("D2U text refreshed in v2"), HeadV2->GetText().ToString(), FString(TEXT("head-v2")));
	}

	// v3 reimport: design properties change one more round; assert relations/identity/bindings all stay stable.
	const FDesignReimportResult ResultV3 = QAReimport(MakeLifeDoc(TEXT("life-v3"), TEXT("head-v3")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("v3 reimport succeeds"), ResultV3.bSucceeded);

	UWidget* LinkV3 = WBP->WidgetTree->FindWidget(TEXT("qa_link"));
	if (TestNotNull(TEXT("trigger button still present after v3"), LinkV3))
	{
		TestTrue(TEXT("trigger button identity stable across both reimports"), LinkV3 == LinkWeak.Get());
	}
	if (UDesign2UMGMappingQAWidget* MappedV3 = Cast<UDesign2UMGMappingQAWidget>(MappedWeak.Get()))
	{
		TestTrue(TEXT("mapped identity stable across both reimports"), MappedV3 == MappedBefore);
		TestEqual(TEXT("mapped input refreshed in v3"), MappedV3->TextValue.ToString(), FString(TEXT("life-v3")));
	}
	if (UTextBlock* HeadV3 = Cast<UTextBlock>(HeadWeak.Get()))
	{
		TestTrue(TEXT("Head identity stable across both reimports"), HeadV3 == WBP->WidgetTree->FindWidget(TEXT("Head")));
		TestEqual(TEXT("D2U text refreshed in v3"), HeadV3->GetText().ToString(), FString(TEXT("head-v3")));
		TestTrue(TEXT("Head parent relationship stable"),
			HeadV3->GetParent() && HeadV3->GetParent()->GetFName() == FName(TEXT("InfoPanel")));
	}
	const bool bV3HasDevBinding = WBP->Bindings.ContainsByPredicate([](const FDelegateEditorBinding& Binding)
	{
		return Binding.ObjectName == TEXT("qa_link")
			&& Binding.PropertyName == TEXT("OnClicked")
			&& Binding.FunctionName == TEXT("DevHandler");
	});
	TestTrue(TEXT("project event binding preserved across v3 reimport"), bV3HasDevBinding);
	TestNull(TEXT("no duplicate Head created"), WBP->WidgetTree->FindWidget(TEXT("Head_1")));
	TestNull(TEXT("no duplicate trigger created"), WBP->WidgetTree->FindWidget(TEXT("qa_link_1")));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after v3"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("mapped instance still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_1")));
		// The host root of an interactive document is the synthesized switcher: the page root qa_root is a top-level instance;
		// InfoPanel and friends are nested nodes inside the page and do not enter the top-level instance records.
		TestNotNull(TEXT("page instance still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_root")));
		TestFalse(TEXT("project child stays outside d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("ProjectNote")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 35b (integration): the reimport report's componentMapping section - mapping config errors (ini
// validation diagnostics + the unmappable-class-load fallback) must be as visible on the reimport path as on
// the import path: both the result's ComponentMappingDiagnostics and the persisted report JSON's componentMapping.entries must contain the entries.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappingDiagnosticsSurfacedTest,
	"Design2UMG.SafeReimportQA.MappingDiagnosticsSurfaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappingDiagnosticsSurfacedTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappingDiagnostics");

	const FString V1 = MakeQADoc(MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// The report file is located by the before/after directory diff of the reimport (the file name carries a timestamp + GUID and cannot be predicted).
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TSet<FString> ReportsBefore;
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(ReportDir / TEXT("*.import_report.json")), true, false);
		for (const FString& FileName : Found)
		{
			ReportsBefore.Add(FileName);
		}
	}

	// v2 reimport: the mapping table points at an unloadable class (the class-load-failure shape of an ini config
	// error), while a snapshot-carried ini validation diagnostic rides along (the same payload Load() produces on an illegal entry).
	const TCHAR* BrokenClassPath = TEXT("/Script/Design2UMGEditor.NoSuchMappedWidgetClass");
	FDesignDocument Document;
	if (!TestTrue(TEXT("v2 source parses"), FFigmaParser::ParseDocument(V1, Document)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	FDesignReimportRequest Request;
	Request.TargetAssetPath = TargetPackage;
	Request.SettingsSnapshot = MakeQASnapshot(BrokenClassPath);
	{
		FDesign2UMGMappingDiagnostic ValidationDiagnostic;
		ValidationDiagnostic.Kind = TEXT("mapping_config_invalid");
		ValidationDiagnostic.Message = TEXT("Mapping 'figma|qa-reimport-doc-9f31|qa-reimport-comp-key' dropped (QA seeded)");
		Request.SettingsSnapshot->GenerationSettings.ComponentMapping.ValidationDiagnostics.Add(
			MoveTemp(ValidationDiagnostic));
	}
	const FDesignReimportResult Result = DesignImportPipeline::Reimport(Document, Request);
	TestTrue(TEXT("reimport itself succeeds (class fallback is a per-node path)"), Result.bSucceeded);

	// Result exit: the ini validation diagnostic + the BuildState-collected match/fallback diagnostics are all in ComponentMappingDiagnostics.
	auto HasMappingDiagnostic = [&Result](const TCHAR* Kind, const TCHAR* Substring = nullptr)
	{
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.ComponentMappingDiagnostics)
		{
			if (Diagnostic.Kind == Kind && (!Substring || Diagnostic.Message.Contains(Substring)))
			{
				return true;
			}
		}
		return false;
	};
	TestTrue(TEXT("seeded ini validation diagnostic transported"),
		HasMappingDiagnostic(TEXT("mapping_config_invalid"), TEXT("QA seeded")));
	TestTrue(TEXT("mapping scope diagnostic present"),
		HasMappingDiagnostic(TEXT("mapping_scope"), QAReimportScope));
	TestTrue(TEXT("mapping hit diagnostic present"),
		HasMappingDiagnostic(TEXT("mapping_hit"), TEXT("HeroButton")));
	TestTrue(TEXT("unavailable mapped class diagnostic carries the configured class path"),
		HasMappingDiagnostic(TEXT("mapping_class_unavailable"), BrokenClassPath));
	// Classification aligned with the import path: mapping diagnostics do not leak into the reimport section (reimport_* prefix).
	TestEqual(TEXT("mapping diagnostics stay out of the reimport section"),
		static_cast<int32>(Result.Diagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind.StartsWith(TEXT("mapping_"));
		})), 0);

	// Persisted exit: the newly written report JSON's componentMapping.entries contains the class-load-failure entry.
	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFiles(ReportsAfter, *(ReportDir / TEXT("*.import_report.json")), true, false);
	FString NewReport;
	for (const FString& FileName : ReportsAfter)
	{
		if (!ReportsBefore.Contains(FileName))
		{
			NewReport = ReportDir / FileName;
		}
	}
	if (TestTrue(TEXT("a new import report was written by reimport"), !NewReport.IsEmpty()))
	{
		FString ReportJson;
		if (TestTrue(TEXT("report file readable"), QAReadFileString(NewReport, ReportJson)))
		{
			TSharedPtr<FJsonObject> ReportRoot;
			if (TestTrue(TEXT("report JSON parses"), QAParseJsonObject(ReportJson, ReportRoot)))
			{
				const TSharedPtr<FJsonObject>* ComponentMapping = nullptr;
				bool bFoundClassUnavailable = false;
				if (TestTrue(TEXT("componentMapping section present"),
						ReportRoot->TryGetObjectField(TEXT("componentMapping"), ComponentMapping) && ComponentMapping != nullptr))
				{
					const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
					if (TestTrue(TEXT("componentMapping.entries present"),
							(*ComponentMapping)->TryGetArrayField(TEXT("entries"), Entries) && Entries != nullptr))
					{
						for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
						{
							const TSharedPtr<FJsonObject> Entry = EntryValue->AsObject();
							if (Entry.IsValid()
								&& Entry->GetStringField(TEXT("kind")) == TEXT("mapping_class_unavailable")
								&& Entry->GetStringField(TEXT("message")).Contains(BrokenClassPath))
							{
								bFoundClassUnavailable = true;
							}
						}
					}
				}
				TestTrue(TEXT("report componentMapping entries carry the unavailable-class diagnostic"),
					bFoundClassUnavailable);
			}
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 35c (integration): function-name collision on the C4 binding channel - when the requested function name
// collides with an existing member (a member variable), the function graph is created under a unique name, and
// the binding and the guid lookup use that same final name; after compiling and saving, the function resolves and the binding survives (no longer pointing at a nonexistent function).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQABindingFunctionNameCollisionTest,
	"Design2UMG.SafeReimportQA.BindingSurvivesFunctionNameCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQABindingFunctionNameCollisionTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportBindingNameCollision");

	const FString V1 = MakeQADoc(MakeQAInteractive(TEXT("qa_link"), TEXT("JumpLink"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/jump")));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// Create the collision: a member variable shares the requested function name (that name is not a function, but it occupies the Kismet namespace).
	FEdGraphPinType BoolPin;
	BoolPin.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	TestTrue(TEXT("colliding member variable added"),
		FBlueprintEditorUtils::AddMemberVariable(WBP, TEXT("ClashHandler"), BoolPin));

	FString BindingError;
	if (!TestTrue(TEXT("binding added despite the name clash"),
			Design2UMGWidgetBindingTools::AddEventBinding(
				WBP, TEXT("qa_link"), TEXT("OnClicked"), TEXT("ClashHandler"), BindingError)))
	{
		AddError(FString::Printf(TEXT("AddEventBinding failed: %s"), *BindingError));
		QAWipeOutput(OutputDir);
		return false;
	}

	const FDelegateEditorBinding* Binding = nullptr;
	for (const FDelegateEditorBinding& Candidate : WBP->Bindings)
	{
		if (Candidate.ObjectName == TEXT("qa_link") && Candidate.PropertyName == TEXT("OnClicked"))
		{
			Binding = &Candidate;
		}
	}
	if (!TestNotNull(TEXT("editor binding recorded"), Binding))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	// The binding must point at the actually created uniquely-named function (under collision the requested name ClashHandler is taken by the member variable).
	TestTrue(TEXT("binding points at the unique function name, not the clashing requested name"),
		Binding->FunctionName != TEXT("ClashHandler"));
	bool bGraphExists = false;
	for (const UEdGraph* Graph : WBP->FunctionGraphs)
	{
		bGraphExists = bGraphExists || (Graph && Graph->GetFName() == Binding->FunctionName);
	}
	TestTrue(TEXT("function graph exists under the bound name"), bGraphExists);

	// After compiling and saving (the same path as the generation chain): the function resolves on the generated class and the binding was not stripped.
	if (!TestTrue(TEXT("compile + save succeeds with the collision binding"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const bool bBindingSurvivesCompile = WBP->Bindings.ContainsByPredicate(
		[Binding](const FDelegateEditorBinding& Candidate)
		{
			return Candidate.ObjectName == TEXT("qa_link")
				&& Candidate.PropertyName == TEXT("OnClicked")
				&& Candidate.FunctionName == Binding->FunctionName;
		});
	TestTrue(TEXT("binding survives compile with the unique function name"), bBindingSurvivesCompile);
	TestNotNull(TEXT("bound function resolvable on the generated class"),
		WBP->GeneratedClass ? WBP->GeneratedClass->FindFunctionByName(Binding->FunctionName) : nullptr);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 36 (integration, cross-session on-disk loaded state): after the v1 import saves, the package is unloaded
// (confirmed no longer a memory hit), then LoadPackage loads it back from disk - the engine again holds a
// read handle without write sharing on the uasset (the normal state of a fresh editor session reimporting an
// existing asset). Reimport must then succeed: the uasset pre-save check does no write probe (this process's
// read handle would falsely kill it and block a save that could succeed); real external IO failures are caught by the save-failure path (reimport_save_failed + rollback).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQADiskLoadedAssetTest,
	"Design2UMG.SafeReimportQA.DiskLoadedAssetReimportSucceeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQADiskLoadedAssetTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportDiskLoaded");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("disk-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	if (!TestTrue(TEXT("v1 asset exists on disk"),
			IFileManager::Get().FileSize(*QAAssetFilePath(TargetPackage)) > 0))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// Unload the package (the editor's own UPackageTools unload path: close editor refs + clear RF_Standalone +
	// ResetLoaders + GC), confirming the asset is no longer a memory hit.
	FText UnloadError;
	TestTrue(TEXT("package unloads"), UPackageTools::UnloadPackages({ WBP->GetOutermost() }, UnloadError));
	TestNull(TEXT("package no longer resident in memory"), FindObject<UPackage>(nullptr, *TargetPackage));

	// Reload from disk: the engine re-holds the uasset's read handle (the old write-probe false-positive trigger).
	UPackage* const LoadedPackage = LoadPackage(nullptr, *TargetPackage, LOAD_None);
	if (!TestNotNull(TEXT("package reloads from disk"), LoadedPackage))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("disk-v2")), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds on the disk-loaded asset"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("no save failure diagnosed"), QACountKind(Result, TEXT("reimport_save_failed")), 0);
	TestEqual(TEXT("completion diagnosed once"), QACountKind(Result, TEXT("reimport_completed")), 1);

	// The asset update takes effect: the WBP loaded back from disk has its text refreshed by the v2 design values, and the manifest consolidates normally.
	UWidgetBlueprint* const LoadedWBP = Result.UpdatedBlueprint;
	if (TestNotNull(TEXT("updated blueprint returned"), LoadedWBP))
	{
		if (UTextBlock* Head = Cast<UTextBlock>(LoadedWBP->WidgetTree->FindWidget(TEXT("Head"))))
		{
			TestEqual(TEXT("design update applied to the disk-loaded asset"),
				Head->GetText().ToString(), FString(TEXT("disk-v2")));
		}
		else
		{
			AddError(TEXT("Head widget missing on the disk-loaded asset"));
		}
	}
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after the disk-loaded reimport"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("container still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_panel")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 37 (integration, slice D): a multi-page prototype document going from two to three pages. The switcher
// host root id is the page-set content fingerprint, so adding a page necessarily rewrites it: after host
// continuation, reimport must not reject wholesale - the new page hangs into the same host switcher as a purely
// generated add (the host's and the existing pages' widget identities preserved), and the manifest source-root identity refreshes to the new fingerprint (the next reimport benchmarks against it).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASwitcherHostAddPageTest,
	"Design2UMG.SafeReimportQA.SwitcherHostAddPageContinuesHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASwitcherHostAddPageTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSwitcherAddPage");

	// v1: two pages pointing at each other (forming a multi-page prototype).
	const FString V1 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1")))));

	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	FDesign2UMGGenerationManifestSidecar SidecarV1;
	if (!TestTrue(TEXT("v1 manifest readable"), QALoadSidecar(TargetPackage, SidecarV1))
		|| !TestTrue(TEXT("v1 records the fingerprinted switcher root"),
			SidecarV1.SourceRootNodeId.StartsWith(TEXT("__d2u_prototype_switcher_"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UWidget> HostBefore = WBP->WidgetTree->RootWidget;
	const TWeakObjectPtr<UWidget> Page1Before = WBP->WidgetTree->FindWidget(TEXT("sd_pg1"));

	// v2: a third page is added (its in-page navigation points at existing pages; the bindings stay valid).
	const FString V2 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1"))),
		*MakeProtoPage(TEXT("sd_pg3"), TEXT("Page3"), 800, MakeProtoNavText(TEXT("sd_t3"), TEXT("P3Nav"), 20, 20, TEXT("sd_pg1")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("add-page reimport succeeds"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("no full-conflict diagnostic"), QACountKind(Result, TEXT("reimport_full_conflict")), 0);
	TestEqual(TEXT("host continuation diagnosed once"),
		QACountKind(Result, TEXT("reimport_prototype_host_continued")), 1);

	// Host continuation: the same host object; the new page hangs into the host switcher.
	TestTrue(TEXT("host switcher object identity preserved"),
		WBP->WidgetTree->RootWidget == HostBefore.Get());
	UWidget* Page3 = WBP->WidgetTree->FindWidget(TEXT("sd_pg3"));
	if (TestNotNull(TEXT("new page generated"), Page3))
	{
		TestTrue(TEXT("new page attached under the host switcher"),
			Page3->GetParent() == WBP->WidgetTree->RootWidget);
	}
	TestTrue(TEXT("existing page widget identity preserved"),
		WBP->WidgetTree->FindWidget(TEXT("sd_pg1")) == Page1Before.Get());
	TestEqual(TEXT("add diagnosed once"), QACountKind(Result, TEXT("reimport_added")), 1);
	TestTrue(TEXT("add names the new page"),
		QAHasMessage(Result, TEXT("reimport_added"), TEXT("sd_pg3")));

	FDesign2UMGGenerationManifestSidecar SidecarV2;
	if (TestTrue(TEXT("manifest reloads after add-page reimport"), QALoadSidecar(TargetPackage, SidecarV2)))
	{
		TestTrue(TEXT("manifest source root renewed to the new fingerprint"),
			SidecarV2.SourceRootNodeId != SidecarV1.SourceRootNodeId
			&& SidecarV2.SourceRootNodeId.StartsWith(TEXT("__d2u_prototype_switcher_")));
		TestNotNull(TEXT("new page recorded as instance"),
			SidecarV2.FindInstanceByDesignNodeId(TEXT("sd_pg3")));
		TestNotNull(TEXT("existing pages stay recorded"),
			SidecarV2.FindInstanceByDesignNodeId(TEXT("sd_pg1")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 38 (integration, slice D): a multi-page prototype deletes a purely D2U page. The deleted page is
// removed by the existing safe-delete flow (neither the tree nor the sidecar keeps it) + a removed diagnostic naming it; host continuation does not reject wholesale.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASwitcherHostRemovePageTest,
	"Design2UMG.SafeReimportQA.SwitcherHostRemovePageSafeDelete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASwitcherHostRemovePageTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSwitcherRemovePage");

	// v1: three pages; pg1/pg2 point at each other, pg3 points at pg1 (after deleting pg3 the remaining navigations all stay valid).
	const auto MakeV1Pages = []()
	{
		return FString::Printf(
			TEXT("%s,%s,%s"),
			*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
			*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1"))),
			*MakeProtoPage(TEXT("sd_pg3"), TEXT("Page3"), 800, MakeProtoNavText(TEXT("sd_t3"), TEXT("P3Nav"), 20, 20, TEXT("sd_pg1"))));
	};

	UWidgetBlueprint* WBP = QAImport(MakeProtoDoc(MakeV1Pages()), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>())
		|| !TestNotNull(TEXT("v1 has the doomed page"), WBP->WidgetTree->FindWidget(TEXT("sd_pg3"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const TWeakObjectPtr<UWidget> HostBefore = WBP->WidgetTree->RootWidget;

	// v2: pg3 disappears.
	const FString V2 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("remove-page reimport succeeds"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("host continuation diagnosed once"),
		QACountKind(Result, TEXT("reimport_prototype_host_continued")), 1);
	TestTrue(TEXT("host switcher object identity preserved"),
		WBP->WidgetTree->RootWidget == HostBefore.Get());

	TestNull(TEXT("removed page deleted from the tree"), WBP->WidgetTree->FindWidget(TEXT("sd_pg3")));
	TestEqual(TEXT("safe remove diagnosed once"), QACountKind(Result, TEXT("reimport_removed")), 1);
	TestTrue(TEXT("removed diagnostic names the page"),
		QAHasMessage(Result, TEXT("reimport_removed"), TEXT("sd_pg3")));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after remove-page reimport"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNull(TEXT("removed page dropped from the manifest"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("sd_pg3")));
		TestTrue(TEXT("removed page dropped from ownership"), !Sidecar.D2UOwned.Contains(TEXT("sd_pg3")));
		TestNotNull(TEXT("surviving page stays recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("sd_pg1")));
		TestTrue(TEXT("manifest source root renewed"),
			Sidecar.SourceRootNodeId.StartsWith(TEXT("__d2u_prototype_switcher_")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 39 (integration, slice D): a multi-page prototype deletes a page containing project content (the page's
// inner layer holds developer children). The page is kept + a remove-conflict diagnostic names it, the project
// children survive; host continuation does not reject wholesale, and the remaining pages update as usual.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASwitcherHostRemovePageProjectContentTest,
	"Design2UMG.SafeReimportQA.SwitcherHostRemovePageProjectContentPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASwitcherHostRemovePageProjectContentTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSwitcherRemoveProjPage");

	// v1: three pages; pg2 is the page the design will delete but which contains project content (pg1/pg3 point at each other, unaffected).
	const FString V1 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg3"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1"))),
		*MakeProtoPage(TEXT("sd_pg3"), TEXT("Page3"), 800, MakeProtoNavText(TEXT("sd_t3"), TEXT("P3Nav"), 20, 20, TEXT("sd_pg1")))));

	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Developer customization: a project child is hung into pg2's inner layer (the synthetic layer name = the page
	// display name + "_inner"; it must survive the design delete).
	if (UPanelWidget* Page2Inner = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(TEXT("Page2_inner"))))
	{
		UTextBlock* ProjectNote = WBP->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("ProjectNote"));
		ProjectNote->SetText(FText::FromString(TEXT("project")));
		Page2Inner->AddChild(ProjectNote);
		TestNotNull(TEXT("project child added"), WBP->WidgetTree->FindWidget(TEXT("ProjectNote")));
	}
	else
	{
		AddError(TEXT("page inner layer not found for the project child"));
	}

	// v2: pg2 disappears from the design.
	const FString V2 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg3"))),
		*MakeProtoPage(TEXT("sd_pg3"), TEXT("Page3"), 800, MakeProtoNavText(TEXT("sd_t3"), TEXT("P3Nav"), 20, 20, TEXT("sd_pg1")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds (conflict is per-page)"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("host continuation diagnosed once"),
		QACountKind(Result, TEXT("reimport_prototype_host_continued")), 1);

	TestNotNull(TEXT("removed-with-project-content page preserved"),
		WBP->WidgetTree->FindWidget(TEXT("sd_pg2")));
	TestNotNull(TEXT("project child preserved with it"),
		WBP->WidgetTree->FindWidget(TEXT("ProjectNote")));
	TestTrue(TEXT("remove conflict diagnosed for the page"),
		QAHasMessage(Result, TEXT("reimport_remove_conflict"), TEXT("sd_pg2")));
	TestEqual(TEXT("no silent delete of the page"), QACountKind(Result, TEXT("reimport_removed")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after the conflict reimport"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("conflicted page stays recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("sd_pg2")));
		TestTrue(TEXT("project widget stays outside d2uOwned"), !Sidecar.D2UOwned.Contains(TEXT("ProjectNote")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 40 (integration, slice D, regression lock): a multi-page prototype downgrades to a single page (the host
// disappears: the new source no longer forms a multi-page prototype, no switcher synthesized root). The
// conservative full-conflict semantics hold (settled by slice C CASE-011); the asset keeps its pre-reimport state.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASwitcherHostDegradeToSinglePageTest,
	"Design2UMG.SafeReimportQA.SwitcherHostDegradeToSinglePageStillFullConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASwitcherHostDegradeToSinglePageTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSwitcherDegrade");

	// v1: two pages pointing at each other's navigation.
	const FString V1 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1")))));

	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2: only one page remains, without interactions (no longer a multi-page prototype; the new IR root is the page itself, not a synthesized switcher root).
	const FString V2 = MakeProtoDoc(MakeProtoPage(
		TEXT("sd_pg1"), TEXT("Page1"), 0, MakeQAText(TEXT("sd_t1"), TEXT("P1Text"), 20, 20, TEXT("solo"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestFalse(TEXT("degraded reimport does not update in place"), Result.bSucceeded);
	TestTrue(TEXT("degraded reimport is a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("full conflict diagnosed once"), QACountKind(Result, TEXT("reimport_full_conflict")), 1);
	TestEqual(TEXT("no host continuation for the degraded source"),
		QACountKind(Result, TEXT("reimport_prototype_host_continued")), 0);

	// The asset keeps its original state: the host and the second page survive unchanged.
	TestTrue(TEXT("host switcher untouched"), WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>());
	TestNotNull(TEXT("second page untouched"), WBP->WidgetTree->FindWidget(TEXT("sd_pg2")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 41 (integration, S2 mapped self-property divergence report): the mapped instance's own visual
// properties (node-level opacity) are written with the generic properties at import; reimport does not refresh
// them per the "mapped interior belongs to the project" boundary, but the divergence between the declared design value and the instance's current state must be named (project value vs design value).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappedOwnAttrDivergenceTest,
	"Design2UMG.SafeReimportQA.MappedOwnAttrDivergenceReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappedOwnAttrDivergenceTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappedOwnAttr");

	// The mapped instance node carries a node-level opacity (Figma "opacity" -> Render.RenderOpacity, declared only when < 1).
	const auto BuildDoc = [](const TCHAR* NodeOpacity)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"QADoc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"qa_root\",\"name\":\"QAPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},\"children\":["
				 "{\"id\":\"qa_moa\",\"name\":\"HeroButton\",\"type\":\"INSTANCE\","
				 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
				 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"Checkout\"}},"
				 "\"opacity\":%s,"
				 "\"absoluteBoundingBox\":{\"x\":24,\"y\":24,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}],"
				 "\"children\":[{\"id\":\"qa_moa_label\",\"name\":\"InnerLabel\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":32,\"y\":36,\"width\":80,\"height\":18},\"characters\":\"Checkout\",%s}]}"
				 "]}]}]}}"),
			QAReimportScope, QAReimportComponentKey, NodeOpacity, *QATextStyle());
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("0.5")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UDesign2UMGMappingQAWidget* MappedInstance = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedInstance))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TestEqual(TEXT("v1 import writes design opacity onto the instance"),
		MappedInstance->GetRenderOpacity(), 0.5f);
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2: the design changes the node opacity to 0.25. Reimport does not refresh the self-property (the project side
	// keeps 0.5), but the divergence must be named in the report.
	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("0.25")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestEqual(TEXT("own opacity kept as project-side"),
		QAFindMappedInstance(WBP)->GetRenderOpacity(), 0.5f);
	TestTrue(TEXT("divergence diagnostic names kept + design values"),
		QAHasMessageWithAll(Result, TEXT("reimport_mapped_attr_kept"),
			{ TEXT("renderOpacity project=0.500 design=0.250") }));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 42 (integration, R3 top-level reparent source-side ownership): after the developer moves a top-level
// generated node into a project-side container, reimport must not drag it back to the host root - it stays in
// the project container + a reimport_move_conflict names it; the design-side content update (text) still arrives (placement belongs to the project, content to the design).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQATopLevelReparentSourceOwnedTest,
	"Design2UMG.SafeReimportQA.TopLevelReparentSourceProjectContainerPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQATopLevelReparentSourceOwnedTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportTopMoveSourceOwned");

	// v1: N is a top-level node directly under qa_root (a manifest instance); there is also a design container.
	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
		*MakeQAPanel(TEXT("qa_c"), TEXT("ContainerA"), 260, 20,
			MakeQAText(TEXT("qa_h"), TEXT("AHint"), 30, 80, TEXT("a-hint")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Developer customization: the project container DevPanel is hung into the host root and the top-level node N is moved into it.
	UVerticalBox* DevPanel = WBP->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DevPanel"));
	if (UPanelWidget* Host = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		Host->AddChild(DevPanel);
	}
	UWidget* N = WBP->WidgetTree->FindWidget(TEXT("N"));
	if (!TestNotNull(TEXT("N present before customization"), N) || !TestTrue(TEXT("DevPanel added"), DevPanel != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	WBP->WidgetTree->RemoveWidget(N);
	DevPanel->AddChild(N);
	const TWeakObjectPtr<UWidget> NWeak = N;

	// v2: in the design N is still a top-level node under the root (the design did not move it); its text updates.
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv-v2")),
		*MakeQAPanel(TEXT("qa_c"), TEXT("ContainerA"), 260, 20,
			MakeQAText(TEXT("qa_h"), TEXT("AHint"), 30, 80, TEXT("a-hint")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UWidget* NAfter = NWeak.Get();
	if (TestNotNull(TEXT("N still alive"), NAfter))
	{
		TestEqual(TEXT("N kept under the project container DevPanel"),
			NAfter->GetParent()->GetFName(), FName(TEXT("DevPanel")));
	}
	TestEqual(TEXT("top-level move conflict diagnosed once"), QACountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("conflict names N and the project-container reason"),
		QAHasMessageWithAll(Result, TEXT("reimport_move_conflict"),
			{ TEXT("'N'"), TEXT("project-side container") }));
	TestEqual(TEXT("no moves applied"), QACountKind(Result, TEXT("reimport_moved")), 0);

	QAWipeOutput(OutputDir);
	return true;
}

#endif
