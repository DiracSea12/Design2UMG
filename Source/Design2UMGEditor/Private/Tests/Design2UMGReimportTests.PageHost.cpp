// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe reimport: multi-page prototype page host-host continuation (add/remove/degrade page cases).

#include "Misc/AutomationTest.h"

#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Components/CanvasPanel.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Design2UMGReimportTestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportFixtures;

namespace
{
	// Multi-page prototype document fixture : a page = a top-level FRAME with fills (converted into the Border
	// page form; the page widget name = the page id); inside the page sits a text with a cross-page ON_CLICK NODE
	// navigation (the target page must really exist for the binding to be valid), so the document forms a multi-page
	// prototype and the conversion chain synthesizes the page host root (id = the page-set content fingerprint, rewritten whenever the set changes).
	FString MakeProtoNavText(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* DestinationPageId)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":90,\"height\":20},\"characters\":\"nav\",%s,"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"%s\",\"navigation\":\"NAVIGATE\"}]}]}"),
			Id, Name, X, Y, *FixtureTextStyle(), DestinationPageId);
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
			FixtureReimportScope, *PagesJson);
	}
}

// ---------------------------------------------------------------------------
// a multi-page prototype document going from two to three pages. The page host
// host root id is the page-set content fingerprint, so adding a page necessarily rewrites it: after host
// continuation, reimport must not reject wholesale - the new page hangs into the same page host as a purely
// generated add (the host's and the existing pages' widget identities preserved), and the manifest source-root identity refreshes to the new fingerprint (the next reimport benchmarks against it).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportPageHostHostAddPageTest,
	"Design2UMG.SafeReimport.PageHostHostAddPageContinuesHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportPageHostHostAddPageTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportPageHostAddPage");

	// v1: two pages pointing at each other (forming a multi-page prototype).
	const FString V1 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1")))));

	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic page host"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UCanvasPanel>()))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	FDesign2UMGGenerationManifestSidecar SidecarV1;
	if (!TestTrue(TEXT("v1 manifest readable"), FixtureLoadSidecar(TargetPackage, SidecarV1))
		|| !TestTrue(TEXT("v1 records the fingerprinted page host root"),
			SidecarV1.SourceRootNodeId.StartsWith(TEXT("__d2u_prototype_page_host_"))))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UWidget> HostBefore = WBP->WidgetTree->RootWidget;
	const TWeakObjectPtr<UWidget> Page1Before = WBP->WidgetTree->FindWidget(TEXT("Page1"));

	// v2: a third page is added (its in-page navigation points at existing pages; the bindings stay valid).
	const FString V2 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1"))),
		*MakeProtoPage(TEXT("sd_pg3"), TEXT("Page3"), 800, MakeProtoNavText(TEXT("sd_t3"), TEXT("P3Nav"), 20, 20, TEXT("sd_pg1")))));

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("add-page reimport succeeds"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("no full-conflict diagnostic"), FixtureCountKind(Result, TEXT("reimport_full_conflict")), 0);
	TestEqual(TEXT("host continuation diagnosed once"),
		FixtureCountKind(Result, TEXT("reimport_prototype_host_continued")), 1);

	// Host continuation: the same host object; the new page hangs into the page host.
	TestTrue(TEXT("page host object identity preserved"),
		WBP->WidgetTree->RootWidget == HostBefore.Get());
	UWidget* Page3 = WBP->WidgetTree->FindWidget(TEXT("Page3"));
	if (TestNotNull(TEXT("new page generated"), Page3))
	{
		TestTrue(TEXT("new page attached under the page host"),
			Page3->GetParent() == WBP->WidgetTree->RootWidget);
	}
	TestTrue(TEXT("existing page widget identity preserved"),
		WBP->WidgetTree->FindWidget(TEXT("Page1")) == Page1Before.Get());
	TestEqual(TEXT("add diagnosed once"), FixtureCountKind(Result, TEXT("reimport_added")), 1);
	TestTrue(TEXT("add names the new page"),
		FixtureHasMessage(Result, TEXT("reimport_added"), TEXT("sd_pg3")));

	FDesign2UMGGenerationManifestSidecar SidecarV2;
	if (TestTrue(TEXT("manifest reloads after add-page reimport"), FixtureLoadSidecar(TargetPackage, SidecarV2)))
	{
		TestTrue(TEXT("manifest source root renewed to the new fingerprint"),
			SidecarV2.SourceRootNodeId != SidecarV1.SourceRootNodeId
			&& SidecarV2.SourceRootNodeId.StartsWith(TEXT("__d2u_prototype_page_host_")));
		TestNotNull(TEXT("new page recorded as instance"),
			SidecarV2.FindInstanceByDesignNodeId(TEXT("sd_pg3")));
		TestNotNull(TEXT("existing pages stay recorded"),
			SidecarV2.FindInstanceByDesignNodeId(TEXT("sd_pg1")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// a multi-page prototype deletes a purely D2U page. The deleted page is
// removed by the existing safe-delete flow (neither the tree nor the sidecar keeps it) + a removed diagnostic naming it; host continuation does not reject wholesale.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportPageHostHostRemovePageTest,
	"Design2UMG.SafeReimport.PageHostHostRemovePageSafeDelete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportPageHostHostRemovePageTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportPageHostRemovePage");

	// v1: three pages; pg1/pg2 point at each other, pg3 points at pg1 (after deleting pg3 the remaining navigations all stay valid).
	const auto MakeV1Pages = []()
	{
		return FString::Printf(
			TEXT("%s,%s,%s"),
			*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
			*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1"))),
			*MakeProtoPage(TEXT("sd_pg3"), TEXT("Page3"), 800, MakeProtoNavText(TEXT("sd_t3"), TEXT("P3Nav"), 20, 20, TEXT("sd_pg1"))));
	};

	UWidgetBlueprint* WBP = FixtureImport(MakeProtoDoc(MakeV1Pages()), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic page host"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UCanvasPanel>())
		|| !TestNotNull(TEXT("v1 has the doomed page"), WBP->WidgetTree->FindWidget(TEXT("Page3"))))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const TWeakObjectPtr<UWidget> HostBefore = WBP->WidgetTree->RootWidget;

	// v2: pg3 disappears.
	const FString V2 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1")))));

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("remove-page reimport succeeds"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("host continuation diagnosed once"),
		FixtureCountKind(Result, TEXT("reimport_prototype_host_continued")), 1);
	TestTrue(TEXT("page host object identity preserved"),
		WBP->WidgetTree->RootWidget == HostBefore.Get());

	TestNull(TEXT("removed page deleted from the tree"), WBP->WidgetTree->FindWidget(TEXT("Page3")));
	TestEqual(TEXT("safe remove diagnosed once"), FixtureCountKind(Result, TEXT("reimport_removed")), 1);
	TestTrue(TEXT("removed diagnostic names the page"),
		FixtureHasMessage(Result, TEXT("reimport_removed"), TEXT("sd_pg3")));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after remove-page reimport"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		TestNull(TEXT("removed page dropped from the manifest"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("sd_pg3")));
		TestTrue(TEXT("removed page dropped from ownership"), !Sidecar.D2UOwned.Contains(TEXT("sd_pg3")));
		TestNotNull(TEXT("surviving page stays recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("sd_pg1")));
		TestTrue(TEXT("manifest source root renewed"),
			Sidecar.SourceRootNodeId.StartsWith(TEXT("__d2u_prototype_page_host_")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// a multi-page prototype deletes a page containing project content (the page's
// inner layer holds developer children). The page is kept + a remove-conflict diagnostic names it, the project
// children survive; host continuation does not reject wholesale, and the remaining pages update as usual.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportPageHostHostRemovePageProjectContentTest,
	"Design2UMG.SafeReimport.PageHostHostRemovePageProjectContentPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportPageHostHostRemovePageProjectContentTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportPageHostRemoveProjPage");

	// v1: three pages; pg2 is the page the design will delete but which contains project content (pg1/pg3 point at each other, unaffected).
	const FString V1 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg3"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1"))),
		*MakeProtoPage(TEXT("sd_pg3"), TEXT("Page3"), 800, MakeProtoNavText(TEXT("sd_t3"), TEXT("P3Nav"), 20, 20, TEXT("sd_pg1")))));

	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic page host"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UCanvasPanel>()))
	{
		FixtureWipeOutput(OutputDir);
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

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds (conflict is per-page)"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("host continuation diagnosed once"),
		FixtureCountKind(Result, TEXT("reimport_prototype_host_continued")), 1);

	TestNotNull(TEXT("removed-with-project-content page preserved"),
		WBP->WidgetTree->FindWidget(TEXT("Page2")));
	TestNotNull(TEXT("project child preserved with it"),
		WBP->WidgetTree->FindWidget(TEXT("ProjectNote")));
	TestTrue(TEXT("remove conflict diagnosed for the page"),
		FixtureHasMessage(Result, TEXT("reimport_remove_conflict"), TEXT("sd_pg2")));
	TestEqual(TEXT("no silent delete of the page"), FixtureCountKind(Result, TEXT("reimport_removed")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after the conflict reimport"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("conflicted page stays recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("sd_pg2")));
		TestTrue(TEXT("project widget stays outside d2uOwned"), !Sidecar.D2UOwned.Contains(TEXT("ProjectNote")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// a multi-page prototype downgrades to a single page (the host
// disappears: the new source no longer forms a multi-page prototype, no page host synthesized root). The
// conservative full-conflict semantics hold; the asset keeps its pre-reimport state.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportPageHostHostDegradeToSinglePageTest,
	"Design2UMG.SafeReimport.PageHostHostDegradeToSinglePageStillFullConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportPageHostHostDegradeToSinglePageTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportPageHostDegrade");

	// v1: two pages pointing at each other's navigation.
	const FString V1 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1")))));

	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic page host"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UCanvasPanel>()))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2: only one page remains, without interactions (no longer a multi-page prototype; the new IR root is the page itself, not a synthesized page host root).
	const FString V2 = MakeProtoDoc(MakeProtoPage(
		TEXT("sd_pg1"), TEXT("Page1"), 0, MakeFixtureText(TEXT("sd_t1"), TEXT("P1Text"), 20, 20, TEXT("solo"))));

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
	TestFalse(TEXT("degraded reimport does not update in place"), Result.bSucceeded);
	TestTrue(TEXT("degraded reimport is a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("full conflict diagnosed once"), FixtureCountKind(Result, TEXT("reimport_full_conflict")), 1);
	TestEqual(TEXT("no host continuation for the degraded source"),
		FixtureCountKind(Result, TEXT("reimport_prototype_host_continued")), 0);

	// The asset keeps its original state: the host and the second page survive unchanged.
	TestTrue(TEXT("page host untouched"), WBP->WidgetTree->RootWidget->IsA<UCanvasPanel>());
	TestNotNull(TEXT("second page untouched"), WBP->WidgetTree->FindWidget(TEXT("Page2")));

	FixtureWipeOutput(OutputDir);
	return true;
}

#endif
