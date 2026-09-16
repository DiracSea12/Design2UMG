// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe Reimport QA: matched-update refresh paths (design-owned refresh, sibling correspondence, kind change, geometry/visual refresh, diagnostics, disk-loaded assets).

#include "Misc/AutomationTest.h"

#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"
#include "Tests/Design2UMGMappingQAWidget.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "HAL/FileManager.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Design2UMGReimportQATestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportQA;

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
				*MakeQAText(TEXT("qa_title_b"), TEXT("Title"), 32, 60, SecondText))) + TEXT(",") + MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 260, 24, TEXT("Keep")));
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
	UTextBlock* OriginalA = TitleA;
	UTextBlock* OriginalB = TitleB;

	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("Second v2")), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TitleA = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Title")));
	TitleB = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Title_1")));
	TestTrue(TEXT("First same-name object retained"), TitleA == OriginalA);
	TestTrue(TEXT("Second same-name object retained"), TitleB == OriginalB);
	TestNull(TEXT("Repeated import creates no extra same-name node"), WBP->WidgetTree->FindWidget(TEXT("Title_2")));
	if (TestNotNull(TEXT("Title still present"), TitleA) && TestNotNull(TEXT("Title_1 still present"), TitleB))
	{
		TestEqual(TEXT("first same-name sibling keeps its own design node value"), TitleA->GetText().ToString(), FString(TEXT("First")));
		TestEqual(TEXT("second same-name sibling refreshed to its own design node value"), TitleB->GetText().ToString(), FString(TEXT("Second v2")));
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

#endif
