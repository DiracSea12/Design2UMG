// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationManifest.h"
#include "Design2UMGPrototypeNavigationExtensions.h"
#include "Design2UMGSidecarTestUtils.h"
#include "DesignImportPipeline.h"
#include "FigmaParser.h"
#include "UMGGenerator.h"
#include "Tests/Design2UMGMappedComponentTestWidget.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/Design2UMGMappingQAWidget.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

#if WITH_AUTOMATION_WORKER

namespace
{
	using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;
	using Design2UMGTestUtils::ReadSidecarJson;
	using Design2UMGTestUtils::DeleteOutputDirectory;

	const TCHAR* ReimportTestScope = TEXT("safe-reimport-file-key");
	const TCHAR* MappedTestClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");
	const TCHAR* OtherMappedTestClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappingQAWidget");

	// ---------------------------------------------------------------------------
	// Figma fixture builders (v1/v2/v3 share the same node assembly)
	// ---------------------------------------------------------------------------

	FString TextStyleJson()
	{
		return TEXT("\"style\":{\"fontFamily\":\"Inter\",\"fontSize\":14,\"fontWeight\":400,"
			"\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\","
			"\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0}");
	}

	FString MakeTextNode(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* Text)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":90,\"height\":20},\"characters\":\"%s\",%s}"),
			Id, Name, X, Y, Text, *TextStyleJson());
	}

	// No background fill + two children: a filled single-child FRAME converts into a single-content widget (Border
	// kind) and project children would not fit; a pure container with two children keeps the multi-child panel form, so the test can add project children/move nodes in.
	FString MakeCardNode(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* CaptionText)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":140,\"height\":100},"
			 "\"children\":[{\"id\":\"%s_text\",\"name\":\"Caption\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":100,\"height\":20},\"characters\":\"%s\",%s},"
			 "{\"id\":\"%s_hint\",\"name\":\"SubHint\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":100,\"height\":20},\"characters\":\"hint\",%s}]}"),
			Id, Name, X, Y, Id, X + 8, Y + 8, CaptionText, *TextStyleJson(),
			Id, X + 8, Y + 40, *TextStyleJson());
	}

	FString MakeMappedButtonNode(int32 X, int32 Y, const TCHAR* Label)
	{
		return FString::Printf(
			TEXT("{\"id\":\"re_btn_1\",\"name\":\"PrimaryButton\",\"type\":\"INSTANCE\","
			 "\"componentId\":\"11:22\",\"componentSetId\":\"33:44\",\"mainComponentKey\":\"reimport-component-key\","
			 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":120,\"height\":44},"
			 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.1,\"g\":0.4,\"b\":0.9,\"a\":1}}],"
			 "\"children\":[{\"id\":\"re_btn_1_label\",\"name\":\"Label\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":80,\"height\":20},\"characters\":\"%s\",%s}]}"),
			Label, X, Y, X + 8, Y + 12, Label, *TextStyleJson());
	}

	// Fill-less pure container: a filled FRAME converts into a single-content widget (Border kind),
	// which would make the move target panel refuse mounts; a pure container keeps the multi-child panel form.
	FString MakeWrapperNode(const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"id\":\"re_wrapper\",\"name\":\"Wrapper\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":24,\"y\":140,\"width\":200,\"height\":90},"
			 "\"children\":[%s]}"),
			*ChildrenJson);
	}

	// Fill-less pure-container FRAME with configurable identity/position (multi-child panel form); the v1/v2 container of the container-delete + content-migration cases.
	FString MakeNamedPanelNode(
		const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":200,\"height\":120},"
			 "\"children\":[%s]}"),
			Id, Name, X, Y, *ChildrenJson);
	}

	FString MakeLinkNode(const TCHAR* Id, const TCHAR* Name, int32 X, const TCHAR* Url)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":180,\"width\":120,\"height\":32},"
			 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"%s\"}]}],"
			 "\"children\":[{\"id\":\"%s_text\",\"name\":\"%s_label\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":186,\"width\":80,\"height\":20},\"characters\":\"Open\",%s}]}"),
			Id, Name, X, Url, Id, Name, X + 8, *TextStyleJson());
	}

	FString MakeReimportDoc(const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\","
			 "\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
			 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
			 "{\"id\":\"re_root\",\"name\":\"ReimportPage\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},\"children\":[%s]}"
			 "]}]}}"),
			ReimportTestScope, *ChildrenJson);
	}

	FString MakeOtherScopeDoc(const FString& ChildrenJson)
	{
		FString Doc = MakeReimportDoc(ChildrenJson);
		const FString ScopeKeyField = FString::Printf(TEXT("\"fileKey\":\"%s\""), ReimportTestScope);
		Doc.ReplaceInline(*ScopeKeyField, TEXT("\"fileKey\":\"another-file-key\""), ESearchCase::CaseSensitive);
		return Doc;
	}

	FDesign2UMGImportSettingsSnapshot MakeReimportSnapshot(const TCHAR* MappedClassPath)
	{
		FDesign2UMGImportSettingsSnapshot Snapshot;
		Snapshot.ConversionSettings = FDesign2UMGConversionSettings::Defaults();
		if (MappedClassPath)
		{
			DesignComponentMapping::FComponentMappingEntry& Entry =
				Snapshot.GenerationSettings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
			Entry.Key = FString::Printf(TEXT("figma|%s|reimport-component-key"), ReimportTestScope);
			Entry.Provider = TEXT("figma");
			Entry.DocumentScope = ReimportTestScope;
			Entry.ComponentKey = TEXT("reimport-component-key");
			Entry.WidgetClass = MappedClassPath;

			DesignComponentMapping::FComponentInputDeclaration& Input = Entry.Inputs.AddDefaulted_GetRef();
			Input.Source = TEXT("label");
			Input.Target = TEXT("Label");
			Input.Type = DesignComponentMapping::EComponentInputType::Text;
			Input.ValueKind = DesignComponentMapping::EComponentInputValueKind::ComponentText;
		}
		return Snapshot;
	}

	UWidgetBlueprint* ImportFromJson(const FString& Json, const TCHAR* MappedClassPath, const FString& OutputPath)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);

		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(Json, Document))
		{
			return nullptr;
		}
		FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		const FDesign2UMGImportSettingsSnapshot Snapshot = MakeReimportSnapshot(MappedClassPath);
		return FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath, &Snapshot.GenerationSettings);
	}

	FDesignReimportResult ReimportFromJson(
		const FString& Json,
		const TCHAR* MappedClassPath,
		const FString& TargetPackage,
		const bool bGenerateResolutionCopy = false)
	{
		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(Json, Document))
		{
			FDesignReimportResult Failed;
			return Failed;
		}

		FDesignReimportRequest Request;
		Request.TargetAssetPath = TargetPackage;
		Request.SettingsSnapshot = MakeReimportSnapshot(MappedClassPath);
		Request.bGenerateResolutionCopy = bGenerateResolutionCopy;
		return DesignImportPipeline::Reimport(Document, Request);
	}

	int32 CountReimportDiagnostics(const FDesignReimportResult& Result, const TCHAR* Kind)
	{
		int32 Count = 0;
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
		{
			Count += Diagnostic.Kind == Kind ? 1 : 0;
		}
		return Count;
	}

	bool LoadManifestSidecar(const FString& AssetPackageName, FDesign2UMGGenerationManifestSidecar& OutSidecar)
	{
		FString Error;
		return Design2UMGGenerationManifest::TryLoadSidecarForAsset(AssetPackageName, OutSidecar, Error);
	}
}

// ---------------------------------------------------------------------------
// Property-level lifecycle (tasks 3.2/3.4):
// v1 import -> developer customization (add a project child / edit a D2U property) -> v2 reimport ->
// D2U-owned updated (text/layout/mapped inputs overwritten) while customizations survive and mapped instances are not replaced.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportLifecycleTest,
	"Design2UMG.WidgetBlueprint.SafeReimportLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportLifecycleTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportLifecycle");

	const FString V1 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeMappedButtonNode(24, 24, TEXT("Checkout")),
		*MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 180, 24, TEXT("Not mapped"))));

	UWidgetBlueprint* WBP = ImportFromJson(V1, MappedTestClassPath, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappedComponentTestWidget* MappedBefore = nullptr;
	WBP->WidgetTree->ForEachWidget([&MappedBefore](UWidget* Widget)
	{
		if (UDesign2UMGMappedComponentTestWidget* Candidate = Cast<UDesign2UMGMappedComponentTestWidget>(Widget))
		{
			MappedBefore = Candidate;
		}
	});
	if (!TestNotNull(TEXT("Mapped instance generated"), MappedBefore))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	TWeakObjectPtr<UWidget> MappedWeak = MappedBefore;

	UCanvasPanelSlot* MappedSlotBefore = Cast<UCanvasPanelSlot>(MappedBefore->Slot);
	const float BeforeY = MappedSlotBefore ? MappedSlotBefore->GetPosition().Y : -1.0f;

	// Simulate developer customization: the project adds one child widget + edits a D2U text property (should be refreshed by the design value).
	UTextBlock* ProjectBadge = WBP->WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("ProjectBadge"));
	ProjectBadge->SetText(FText::FromString(TEXT("Project Content")));
	if (UPanelWidget* RootPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		RootPanel->AddChild(ProjectBadge);
	}
	if (UTextBlock* Caption = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Caption"))))
	{
		Caption->SetText(FText::FromString(TEXT("DeveloperEdit")));
	}

	// v2: label gets a new value, the button moves down, the card text changes.
	const FString V2 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeMappedButtonNode(40, 88, TEXT("Pay Now")),
		*MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 180, 24, TEXT("Caption v2"))));

	const FDesignReimportResult Result = ReimportFromJson(V2, MappedTestClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	// D2U-owned updated: text, mapped inputs overwritten directly.
	UTextBlock* CaptionAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Caption")));
	TestNotNull(TEXT("Caption widget still present"), CaptionAfter);
	if (CaptionAfter)
	{
		TestEqual(TEXT("D2U text refreshed from design"), CaptionAfter->GetText().ToString(), TEXT("Caption v2"));
	}

	UWidget* MappedAfter = MappedWeak.Get();
	TestNotNull(TEXT("Mapped instance still alive"), MappedAfter);
	if (UDesign2UMGMappedComponentTestWidget* Mapped = Cast<UDesign2UMGMappedComponentTestWidget>(MappedAfter))
	{
		TestTrue(TEXT("Mapped instance not replaced"), Mapped == MappedBefore);
		TestEqual(TEXT("Declared input overwritten with latest design value"),
			Mapped->Label.ToString(), TEXT("Pay Now"));
		// Outer layout updated: the button moves down from y=24 to y=88.
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Mapped->Slot))
		{
			TestTrue(TEXT("Mapped instance outer layout updated"),
				FMath::Abs(Slot->GetPosition().Y - 88.0f) < 4.0f && FMath::Abs(Slot->GetPosition().X - 40.0f) < 4.0f);
		}
		else
		{
			AddError(TEXT("Mapped instance slot is not a canvas slot"));
		}
	}
	TestTrue(TEXT("Pre-reimport slot Y was the v1 position"), FMath::Abs(BeforeY - 24.0f) < 4.0f);

	// Customization survives: the project-added child is still there (and not in d2uOwned).
	TestNotNull(TEXT("Project-added widget preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectBadge")));

	// Diagnostics: matched update + inputs overwrite.
	TestTrue(TEXT("Update diagnostics present"), CountReimportDiagnostics(Result, TEXT("reimport_updated")) >= 2);
	TestTrue(TEXT("Input overwrite diagnostics present"),
		CountReimportDiagnostics(Result, TEXT("mapping_input_applied")) >= 1);

	// Manifest updated: mapped instances + top-level generated records present; project widgets not in d2uOwned.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("Manifest reloaded after reimport"), LoadManifestSidecar(TargetPackage, Sidecar)))
	{
		TestTrue(TEXT("Mapped instance still recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_btn_1")) != nullptr);
		TestTrue(TEXT("Generated instance still recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_card")) != nullptr);
		TestTrue(TEXT("Nested D2U widget owned"), Sidecar.D2UOwned.Contains(TEXT("Caption")));
		TestFalse(TEXT("Project-added widget not owned"), Sidecar.D2UOwned.Contains(TEXT("ProjectBadge")));
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// Structure changes (tasks 4.1/4.2/4.3): add (new top-level node) / move (into a new container) / remove (pure-D2U delete).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportStructureTest,
	"Design2UMG.WidgetBlueprint.SafeReimportStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportStructureTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportStructure");

	const FString V1 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v1")),
		*MakeTextNode(TEXT("re_stat"), TEXT("StatText"), 200, 24, TEXT("Old"))));

	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	TestNotNull(TEXT("v1 has StatText"), WBP->WidgetTree->FindWidget(TEXT("StatText")));

	// v2: StatText disappears (remove); PlainCard moves into the new container Wrapper (move; the container also
	// holds a second child so it converts into a multi-child panel); NewBadge is added (add).
	const FString V2 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeWrapperNode(FString::Printf(
			TEXT("%s,%s"),
			*MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 32, 152, TEXT("Card v2")),
			*MakeTextNode(TEXT("re_wrapper_hint"), TEXT("WrapperHint"), 32, 190, TEXT("Hint")))),
		*MakeTextNode(TEXT("re_badge"), TEXT("NewBadge"), 200, 140, TEXT("Added")),
		*MakeTextNode(TEXT("re_keep"), TEXT("KeepText"), 200, 24, TEXT("Keep"))));

	const FDesignReimportResult Result = ReimportFromJson(V2, nullptr, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	// remove: the pure-D2U node is deleted safely.
	TestNull(TEXT("Removed design node deleted from tree"), WBP->WidgetTree->FindWidget(TEXT("StatText")));
	TestTrue(TEXT("Safe remove diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_removed")) >= 1);

	// add: the new top-level node joins and is recorded in the manifest.
	UWidget* NewBadge = WBP->WidgetTree->FindWidget(TEXT("NewBadge"));
	TestNotNull(TEXT("New design node added"), NewBadge);
	TestTrue(TEXT("Add diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_added")) >= 1);

	// move: PlainCard hangs under Wrapper; the widget identity survives.
	UWidget* Wrapper = WBP->WidgetTree->FindWidget(TEXT("Wrapper"));
	UWidget* Card = WBP->WidgetTree->FindWidget(TEXT("PlainCard"));
	TestNotNull(TEXT("New wrapper container added"), Wrapper);
	TestNotNull(TEXT("Moved card still exists"), Card);
	if (Wrapper && Card)
	{
		TestEqual(TEXT("Card reparented under the wrapper"), Card->GetParent()->GetFName(), Wrapper->GetFName());
	}
	TestTrue(TEXT("Move diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_moved")) >= 1);

	// The card's inner text is refreshed as well.
	if (UTextBlock* Caption = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Caption"))))
	{
		TestEqual(TEXT("Moved subtree text refreshed"), Caption->GetText().ToString(), TEXT("Card v2"));
	}

	// Manifest: Wrapper/NewBadge recorded as top-level instances; PlainCard leaves the top level, so it is no longer an instance but stays in d2uOwned.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("Manifest updated after structural reimport"), LoadManifestSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("Wrapper recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("re_wrapper")));
		TestNotNull(TEXT("NewBadge recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("re_badge")));
		TestNull(TEXT("Moved card no longer a top-level instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("re_card")));
		TestTrue(TEXT("Moved card still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("PlainCard")));
		TestTrue(TEXT("Removed node dropped from ownership"), !Sidecar.D2UOwned.Contains(TEXT("StatText")));
		TestTrue(TEXT("New node ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("NewBadge")));
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// Container delete + content migration (nested mapped instance; the safe-reimport spec's identity-preserving
// move): v1 FrameA>Button (mapping hit); v2 deletes FrameA and adds FrameC>Button.
// The old-container whole-delete decision must recognize manifest instances still alive inside the subtree -> keep
// FrameA conservatively; Button takes its own move plan and rehangs under FrameC: instance alive, identity
// preserved, manifest free of ghost records (every instance record locates a widget in the tree), no duplicates, readable diagnostics.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportNestedInstanceMigrationTest,
	"Design2UMG.WidgetBlueprint.SafeReimportNestedInstanceMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportNestedInstanceMigrationTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportNestedMigration");

	// v1: a mapped instance button + a placeholder text hang inside FrameA (pure container, two children keep the panel form).
	const FString V1 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeNamedPanelNode(TEXT("re_frame_a"), TEXT("FrameA"), 24, 24, FString::Printf(
			TEXT("%s,%s"),
			*MakeMappedButtonNode(32, 32, TEXT("Checkout")),
			*MakeTextNode(TEXT("re_a_pad"), TEXT("FrameAPad"), 32, 84, TEXT("Pad")))),
		*MakeTextNode(TEXT("re_keep"), TEXT("KeepText"), 250, 24, TEXT("Keep"))));

	UWidgetBlueprint* WBP = ImportFromJson(V1, MappedTestClassPath, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappedComponentTestWidget* MappedBefore = nullptr;
	WBP->WidgetTree->ForEachWidget([&MappedBefore](UWidget* Widget)
	{
		if (UDesign2UMGMappedComponentTestWidget* Candidate = Cast<UDesign2UMGMappedComponentTestWidget>(Widget))
		{
			MappedBefore = Candidate;
		}
	});
	if (!TestNotNull(TEXT("v1 has the nested mapped instance"), MappedBefore)
		|| !TestTrue(TEXT("v1 mapped instance sits under FrameA"),
			MappedBefore->GetParent() && MappedBefore->GetParent()->GetFName() == FName(TEXT("FrameA"))))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappedComponentTestWidget> MappedWeak = MappedBefore;

	// v2: FrameA deleted; FrameC added and adopts the same button (designNodeId unchanged, still mapped).
	const FString V2 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeNamedPanelNode(TEXT("re_frame_c"), TEXT("FrameC"), 24, 108, FString::Printf(
			TEXT("%s,%s"),
			*MakeMappedButtonNode(32, 116, TEXT("Pay Now")),
			*MakeTextNode(TEXT("re_c_pad"), TEXT("FrameCPad"), 32, 170, TEXT("Pad")))),
		*MakeTextNode(TEXT("re_keep"), TEXT("KeepText"), 250, 24, TEXT("Keep"))));

	const FDesignReimportResult Result = ReimportFromJson(V2, MappedTestClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	// Instance alive + identity preserved: the same object, rehung under the new container FrameC.
	UDesign2UMGMappedComponentTestWidget* MappedAfter = MappedWeak.Get();
	TestNotNull(TEXT("nested mapped instance survived the container swap"), MappedAfter);
	UWidget* NewContainer = WBP->WidgetTree->FindWidget(TEXT("FrameC"));
	TestNotNull(TEXT("new container added"), NewContainer);
	if (MappedAfter && NewContainer)
	{
		TestTrue(TEXT("widget object identity preserved"), MappedAfter == MappedBefore);
		TestEqual(TEXT("instance reparented under the new container"),
			MappedAfter->GetParent()->GetFName(), NewContainer->GetFName());
		TestEqual(TEXT("declared input overwritten with the v2 design value"),
			MappedAfter->Label.ToString(), TEXT("Pay Now"));
	}

	// The old container is kept conservatively (the subtree holds live instances; no whole delete).
	TestNotNull(TEXT("old container preserved conservatively"), WBP->WidgetTree->FindWidget(TEXT("FrameA")));

	// Readable diagnostics: the conservative keep names the live instance; the move names the identity; the node_missing ghost path must not appear.
	TestEqual(TEXT("one remove conflict for the old container"),
		CountReimportDiagnostics(Result, TEXT("reimport_remove_conflict")), 1);
	TestTrue(TEXT("remove-conflict message names the live instance"),
		[&Result]()
		{
			for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
			{
				if (Diagnostic.Kind == TEXT("reimport_remove_conflict")
					&& Diagnostic.Message.Contains(TEXT("FrameA"))
					&& Diagnostic.Message.Contains(TEXT("PrimaryButton")))
				{
					return true;
				}
			}
			return false;
		}());
	TestEqual(TEXT("one identity-preserving move diagnosed"),
		CountReimportDiagnostics(Result, TEXT("reimport_moved")), 1);
	TestEqual(TEXT("no ghost node-missing diagnostics"),
		CountReimportDiagnostics(Result, TEXT("reimport_node_missing")), 0);

	// Manifest self-consistent: instances present, old and new containers present, no ghost records, no duplicates.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("Manifest updated after migration"), LoadManifestSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("migrated instance still recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_btn_1")));
		TestNotNull(TEXT("preserved old container stays recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_frame_a")));
		TestNotNull(TEXT("new container recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_frame_c")));

		int32 ButtonRecords = 0;
		int32 GhostRecords = 0;
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Sidecar.Instances)
		{
			ButtonRecords += Record.DesignNodeId == TEXT("re_btn_1") ? 1 : 0;
			GhostRecords += WBP->WidgetTree->FindWidget(FName(*Record.WidgetName)) ? 0 : 1;
		}
		TestEqual(TEXT("migrated instance recorded exactly once"), ButtonRecords, 1);
		TestEqual(TEXT("no ghost records (every record resolves to a live widget)"), GhostRecords, 0);
		TestTrue(TEXT("migrated instance still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("PrimaryButton")));
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// Identity conversion (task 4.6): generated -> mapped (v2 adds the mapping) -> generated (v3 drops it).
// The old subtree is purely D2U-owned both times; each round replaces it with the new form plus diagnostics.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportKindChangedTest,
	"Design2UMG.WidgetBlueprint.SafeReimportKindChanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportKindChangedTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportKindChanged");

	// v1: no mapping config -> the component instance takes the plain expansion (generated).
	const FString V1 = MakeReimportDoc(MakeMappedButtonNode(24, 24, TEXT("Checkout")));
	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	TestNotNull(TEXT("v1 expands component internals"),
		WBP->WidgetTree->FindWidget(TEXT("Label")));

	// v2: mapping added -> the same design node becomes mapped; the old subtree is purely D2U-owned -> replaced.
	const FString V2 = MakeReimportDoc(MakeMappedButtonNode(24, 24, TEXT("Checkout")));
	FDesignReimportResult Result = ReimportFromJson(V2, MappedTestClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);
	TestTrue(TEXT("Kind change diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_kind_changed")) == 1);

	bool bHasMappedInstance = false;
	bool bHasExpandedLabel = false;
	WBP->WidgetTree->ForEachWidget([&bHasMappedInstance, &bHasExpandedLabel](UWidget* Widget)
	{
		bHasMappedInstance |= Widget->IsA<UDesign2UMGMappedComponentTestWidget>();
		bHasExpandedLabel |= (Widget->GetFName() == FName(TEXT("Label")));
	});
	TestTrue(TEXT("Old generated subtree replaced with mapped instance"), bHasMappedInstance && !bHasExpandedLabel);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("Manifest readable after kind change"), LoadManifestSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_btn_1"));
		TestNotNull(TEXT("Transitioned node still recorded"), Record);
		if (Record)
		{
			TestEqual(TEXT("Manifest kind updated to mapped"), Record->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("Manifest mapped class updated"), Record->MappedClass, FString(MappedTestClassPath));
		}
	}

	// v3: mapping removed -> back to generated expansion; likewise replaced.
	const FString V3 = MakeReimportDoc(MakeMappedButtonNode(24, 24, TEXT("Checkout")));
	Result = ReimportFromJson(V3, nullptr, TargetPackage);
	TestTrue(TEXT("v3 reimport succeeds"), Result.bSucceeded);
	TestTrue(TEXT("Reverse kind change diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_kind_changed")) == 1);

	bool bHasMappedAfterV3 = false;
	WBP->WidgetTree->ForEachWidget([&bHasMappedAfterV3](UWidget* Widget)
	{
		bHasMappedAfterV3 |= Widget->IsA<UDesign2UMGMappedComponentTestWidget>();
	});
	TestFalse(TEXT("Mapped instance replaced back with generated expansion"), bHasMappedAfterV3);
	TestNotNull(TEXT("Generated expansion restored"), WBP->WidgetTree->FindWidget(TEXT("Label")));

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// Conflicts (tasks 4.4/4.5): a Mapped Class change never replaces; when the same interaction already has a
// project binding, the D2U Prototype binding yields. (The remove-with-project-content conflict is in SafeReimportRemoveConflict.)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportConflictTest,
	"Design2UMG.WidgetBlueprint.SafeReimportConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportConflictTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportConflict");

	const FString V1 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeMappedButtonNode(24, 24, TEXT("Checkout")),
		*MakeLinkNode(TEXT("re_link_btn"), TEXT("LinkArea"), 24, TEXT("https://example.com/old")),
		*MakeLinkNode(TEXT("re_link_btn2"), TEXT("LinkArea2"), 160, TEXT("https://example.com/keep"))));

	UWidgetBlueprint* WBP = ImportFromJson(V1, MappedTestClassPath, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Simulate developer customization:
	// 1) attach a project OnClicked binding to re_link_btn (double-trigger conflict).
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = TEXT("re_link_btn");
	ProjectBinding.PropertyName = TEXT("OnClicked");
	ProjectBinding.FunctionName = TEXT("HandleLinkClick");
	WBP->Bindings.Add(ProjectBinding);
	// 2) edit the mapped instance's input value (must not be refreshed under a Mapped Class conflict).
	TWeakObjectPtr<UDesign2UMGMappedComponentTestWidget> MappedBefore;
	WBP->WidgetTree->ForEachWidget([&MappedBefore](UWidget* Widget)
	{
		if (UDesign2UMGMappedComponentTestWidget* Candidate = Cast<UDesign2UMGMappedComponentTestWidget>(Widget))
		{
			MappedBefore = Candidate;
		}
	});
	if (UDesign2UMGMappedComponentTestWidget* Mapped = MappedBefore.Get())
	{
		Mapped->Label = FText::FromString(TEXT("Hacked"));
	}

	// v2: both interactions remain (URL changed); the mapping points at another class.
	const FString V2 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeMappedButtonNode(24, 24, TEXT("Pay Now")),
		*MakeLinkNode(TEXT("re_link_btn"), TEXT("LinkArea"), 24, TEXT("https://example.com/new")),
		*MakeLinkNode(TEXT("re_link_btn2"), TEXT("LinkArea2"), 160, TEXT("https://example.com/keep"))));

	const FDesignReimportResult Result = ReimportFromJson(V2, OtherMappedTestClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport itself succeeds (conflicts are per-node)"), Result.bSucceeded);

	// Conflict 1: Mapped Class changed -> instance kept, not replaced, inputs not overwritten.
	UDesign2UMGMappedComponentTestWidget* MappedAfter = Cast<UDesign2UMGMappedComponentTestWidget>(MappedBefore.Get());
	TestNotNull(TEXT("Mapped instance kept"), MappedAfter);
	if (MappedAfter)
	{
		TestEqual(TEXT("Mapped instance class unchanged"), MappedAfter->GetClass()->GetPathName(), FString(MappedTestClassPath));
		TestEqual(TEXT("Inputs not overwritten on mapped-class conflict"),
			MappedAfter->Label.ToString(), TEXT("Hacked"));
	}
	TestTrue(TEXT("Mapped class conflict diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_mapped_class_conflict")) == 1);

	// Conflict 2: double trigger -> the project binding stays; the D2U binding yields (only the non-conflicting second one remains).
	TestTrue(TEXT("Dual-trigger conflict diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_prototype_conflict")) == 1);
	TestTrue(TEXT("Project binding kept in WBP"), WBP->Bindings.Contains(ProjectBinding));
	if (UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass))
	{
		const UDesign2UMGPrototypeNavigationClassExtension* RuntimeExtension =
			GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>();
		if (TestNotNull(TEXT("Prototype class extension present after reimport"), RuntimeExtension))
		{
			const FUMGPrototypeNavigationData& NavData = RuntimeExtension->GetNavigationDataForTest();
			TestEqual(TEXT("D2U binding yields to project binding"), NavData.ClickBindings.Num(), 1);
			if (NavData.ClickBindings.Num() == 1)
			{
				TestEqual(TEXT("Remaining binding is the unconflicted one"),
					NavData.ClickBindings[0].TriggerWidgetName, FString(TEXT("re_link_btn2")));
			}
		}
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// remove with project content (task 4.3 conflict surface): the design-deleted node's subtree contains
// project-added children -> no delete; kept + conflict diagnostic (contrast: the content-free delete is in SafeReimportStructure).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportRemoveConflictTest,
	"Design2UMG.WidgetBlueprint.SafeReimportRemoveConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportRemoveConflictTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportRemoveConflict");

	const FString V1 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Doomed")),
		*MakeTextNode(TEXT("re_keep"), TEXT("KeepText"), 200, 24, TEXT("Keep"))));

	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Simulate developer customization: add a project child inside the PlainCard subtree (must survive the design delete).
	if (UPanelWidget* CardPanel = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(TEXT("PlainCard"))))
	{
		UTextBlock* ProjectChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("ProjectChild"));
		ProjectChild->SetText(FText::FromString(TEXT("Project")));
		CardPanel->AddChild(ProjectChild);
		TestNotNull(TEXT("Project child added"), WBP->WidgetTree->FindWidget(TEXT("ProjectChild")));
	}

	// v2: PlainCard disappears from the design.
	const FString V2 = MakeReimportDoc(MakeTextNode(TEXT("re_keep"), TEXT("KeepText"), 200, 24, TEXT("Keep")));
	const FDesignReimportResult Result = ReimportFromJson(V2, nullptr, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds (conflict is per-node)"), Result.bSucceeded);

	TestNotNull(TEXT("Removed-with-project-content node preserved"), WBP->WidgetTree->FindWidget(TEXT("PlainCard")));
	TestNotNull(TEXT("Project child preserved with it"), WBP->WidgetTree->FindWidget(TEXT("ProjectChild")));
	TestTrue(TEXT("Remove conflict diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_remove_conflict")) == 1);
	TestTrue(TEXT("No silent delete"), CountReimportDiagnostics(Result, TEXT("reimport_removed")) == 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("Manifest readable after conflict"), LoadManifestSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("Conflicted node stays recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("re_card")));
		TestTrue(TEXT("Project widget stays outside d2uOwned"), !Sidecar.D2UOwned.Contains(TEXT("ProjectChild")));
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// Sibling-copy fallback (task 4.7): scope mismatch / missing manifest = full conflict;
// the original asset is kept by default; a sibling WBP + new manifest is generated only on explicit user choice.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportResolutionCopyTest,
	"Design2UMG.WidgetBlueprint.SafeReimportResolutionCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportResolutionCopyTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportResolutionCopy");

	const FString V1 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v1")));
	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString OriginalAssetFile =
		FPackageName::LongPackageNameToFilename(TargetPackage, FPackageName::GetAssetPackageExtension());

	const FString V2 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v2")));

	// Scenario 1: DocumentScope mismatch -> full conflict; no copy by default.
	const FString OtherScopeDoc = MakeOtherScopeDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v2")));
	FDesignReimportResult Result = ReimportFromJson(OtherScopeDoc, nullptr, TargetPackage);
	TestFalse(TEXT("Scope mismatch reimport does not update in place"), Result.bSucceeded);
	TestTrue(TEXT("Scope mismatch reported as full conflict"), Result.bFullConflict);
	TestTrue(TEXT("Full conflict diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_full_conflict")) == 1);
	TestTrue(TEXT("Original asset file preserved"), FPaths::FileExists(OriginalAssetFile));
	TestTrue(TEXT("No copy without explicit choice"), Result.ResolutionCopyPath.IsEmpty());

	// Scenario 2: manifest missing -> full conflict; after the explicit choice a sibling copy is generated (the original asset and state untouched).
	IFileManager::Get().Delete(*Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), false, true);
	const int64 OriginalSize = IFileManager::Get().FileSize(*OriginalAssetFile);

	Result = ReimportFromJson(V2, nullptr, TargetPackage);
	TestTrue(TEXT("Missing manifest reported as full conflict"), Result.bFullConflict);
	TestFalse(TEXT("Default action preserves without copy"), Result.bSucceeded);
	TestTrue(TEXT("Still no implicit copy"), Result.ResolutionCopyPath.IsEmpty());

	Result = ReimportFromJson(V2, nullptr, TargetPackage, /*bGenerateResolutionCopy=*/true);
	TestTrue(TEXT("Explicit choice still reports the full conflict"), Result.bFullConflict);
	TestTrue(TEXT("Resolution copy generated"), !Result.ResolutionCopyPath.IsEmpty());

	const FString CopyPackage = FPaths::GetPath(TargetPackage) / TEXT("WBP_ReimportPage_Resolution");
	TestEqual(TEXT("Resolution copy path reported"), Result.ResolutionCopyPath, CopyPackage + TEXT(".WBP_ReimportPage_Resolution"));
	FDesign2UMGGenerationManifestSidecar CopySidecar;
	TestTrue(TEXT("Resolution copy has its own manifest"),
		LoadManifestSidecar(CopyPackage, CopySidecar));
	TestTrue(TEXT("Original asset untouched"), FPaths::FileExists(OriginalAssetFile));
	TestEqual(TEXT("Original asset bytes unchanged"),
		IFileManager::Get().FileSize(*OriginalAssetFile), OriginalSize);
	TestFalse(TEXT("Original still has no manifest"),
		FPaths::FileExists(Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage)));

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// Save-failure semantics (task 3.3): save fails -> transaction rollback, the asset keeps its pre-reimport state, the manifest is not updated.
// The save failure is injected via FUMGGenerator::ArmAutomationSaveFailureOnce() (the engine's SavePackage
// failure path goes through appError and cannot serve as a test injection vehicle).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportSaveFailureTest,
	"Design2UMG.WidgetBlueprint.SafeReimportSaveFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportSaveFailureTest::RunTest(const FString&)
{
	// The failure-injection path logs an error by design; registered as expected.
	AddExpectedError(TEXT("failed to save"), EAutomationExpectedErrorFlags::Contains, 0);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportSaveFailure");

	const FString V1 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v1")));
	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	FString ManifestBefore;
	TestTrue(TEXT("v1 manifest exists"), FFileHelper::LoadFileToString(ManifestBefore, *SidecarPath));

	const FString V2 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v2")));

	// Use the test injection to make the next SaveWidgetBlueprint fail deterministically once (production unaffected).
	// The engine's SavePackage failure goes through appError and cannot be used as a test injection vehicle.
	FUMGGenerator::ArmAutomationSaveFailureOnce();
	const FDesignReimportResult Result = ReimportFromJson(V2, nullptr, TargetPackage);

	TestFalse(TEXT("Reimport reports failure"), Result.bSucceeded);
	TestTrue(TEXT("Save failure diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_save_failed")) == 1);

	// Transaction rollback: the in-memory text property keeps its pre-reimport state (the developer's value), not the v2 design value.
	if (UTextBlock* Caption = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Caption"))))
	{
		TestEqual(TEXT("In-memory state rolled back to pre-reimport"),
			Caption->GetText().ToString(), TEXT("Card v1"));
	}
	else
	{
		AddError(TEXT("Caption widget missing after rollback"));
	}

	// Manifest not updated: byte-for-byte identical.
	FString ManifestAfter;
	TestTrue(TEXT("Manifest still exists"), FFileHelper::LoadFileToString(ManifestAfter, *SidecarPath));
	TestTrue(TEXT("Manifest untouched on save failure"), ManifestAfter == ManifestBefore);

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// Console-command smoke test: the Design2UMG.Reimport command-line entry (the automated path outside the menu UI).
// Verifies the entry itself only - the command is reachable, parameters map correctly into
// FDesignReimportRequest, and the success path runs the same DesignImportPipeline::Reimport; reimport semantics are covered above.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportConsoleCommandTest,
	"Design2UMG.WidgetBlueprint.SafeReimportConsoleCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportConsoleCommandTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportConsoleCommand");

	const FString V1 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v1")));
	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// The command entry takes only on-disk design.json paths, so the v2 source is written to disk first (no-mapping scenario; default settings suffice to match).
	const FString V2JsonPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / TEXT("Temp")
		/ TEXT("SafeReimportConsoleCommand_v2.json");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(V2JsonPath), true);
	const FString V2 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v2")));
	if (!TestTrue(TEXT("v2 design.json written"),
		FFileHelper::SaveStringToFile(V2, *V2JsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
	{
		IFileManager::Get().Delete(*V2JsonPath, false, true);
		DeleteGeneratedAssetsAtPath(OutputPath);
		DeleteOutputDirectory(OutputPath);
		return false;
	}

	IConsoleObject* ConsoleObject = IConsoleManager::Get().FindConsoleObject(TEXT("Design2UMG.Reimport"));
	if (TestNotNull(TEXT("Design2UMG.Reimport console command registered"), ConsoleObject))
	{
		const TArray<FString> Args = { TargetPackage, V2JsonPath };
		TestTrue(TEXT("console command executes"), ConsoleObject->AsCommand()->Execute(Args, nullptr, *GLog));

		// Success-path marker: the command entry runs the same pipeline; the v2 design text is refreshed into the target asset.
		if (UTextBlock* Caption = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Caption"))))
		{
			TestEqual(TEXT("v2 text refreshed through the console command"),
				Caption->GetText().ToString(), TEXT("Card v2"));
		}
		else
		{
			AddError(TEXT("Caption widget missing after console reimport"));
		}
	}

	IFileManager::Get().Delete(*V2JsonPath, false, true);
	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

#endif
