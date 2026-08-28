// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "UMGGenerator.h"
#include "UMGLayoutProjection.h"
#include "WidgetLayoutAdapter.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/VerticalBox.h"
#include "Components/Widget.h"
#include "UObject/UObjectGlobals.h"

#if WITH_AUTOMATION_WORKER

// ---- WidgetLayoutAdapter focused tests ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutAdapterSlotSnapshotParityTest,
	"Design2UMG.LayoutAdapter.SlotSnapshotParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutAdapterSlotSnapshotParityTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Bounds = {100.f, 200.f, 300.f, 150.f};

	const FIntPoint DesignSize(1920, 1080);

	FLayoutContext Ctx;
	Ctx.DesignSize = DesignSize;
	Ctx.Preset = FLayoutPreset::GetStandardHUDPreset();
	FDesignPixelBounds NB;
	NB.X = Node.Bounds.X / DesignSize.X;
	NB.Y = Node.Bounds.Y / DesignSize.Y;
	NB.W = Node.Bounds.W / DesignSize.X;
	NB.H = Node.Bounds.H / DesignSize.Y;
	const FUMGSlotData Expected = LayoutCalculator::CalculateSlot(NB, Ctx);

	UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	UWidgetTree* Tree = NewObject<UWidgetTree>(WBP, NAME_None, RF_Transient);
	UCanvasPanel* Canvas = NewObject<UCanvasPanel>(Tree, NAME_None, RF_Transient);
	Tree->RootWidget = Canvas;

	const FUMGSlotData Actual = FWidgetLayoutAdapter::Resolve(Node, Canvas, Tree, DesignSize, true);

	TestEqual(TEXT("AnchorMinX"), Actual.Anchors.Min.X, Expected.Anchors.Min.X);
	TestEqual(TEXT("AnchorMinY"), Actual.Anchors.Min.Y, Expected.Anchors.Min.Y);
	TestEqual(TEXT("AnchorMaxX"), Actual.Anchors.Max.X, Expected.Anchors.Max.X);
	TestEqual(TEXT("AnchorMaxY"), Actual.Anchors.Max.Y, Expected.Anchors.Max.Y);
	TestEqual(TEXT("OffsetLeft"),  Actual.Offsets.Left,   Expected.Offsets.Left);
	TestEqual(TEXT("OffsetTop"),   Actual.Offsets.Top,    Expected.Offsets.Top);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutAdapterPassThroughWhenNoCanvasTest,
	"Design2UMG.LayoutAdapter.PassThroughWhenNoCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutAdapterPassThroughWhenNoCanvasTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Bounds = {50.f, 50.f, 200.f, 100.f};
	Node.Slot.Anchors.Min.X = 0.25f;
	Node.Slot.Anchors.Min.Y = 0.25f;

	UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	UWidgetTree* Tree = NewObject<UWidgetTree>(WBP, NAME_None, RF_Transient);
	UVerticalBox* VBox = NewObject<UVerticalBox>(Tree, NAME_None, RF_Transient);
	Tree->RootWidget = VBox;

	const FUMGSlotData Result = FWidgetLayoutAdapter::Resolve(Node, VBox, Tree, FIntPoint(1920, 1080), true);

	TestEqual(TEXT("PassThrough AnchorMinX"), Result.Anchors.Min.X, Node.Slot.Anchors.Min.X);
	TestEqual(TEXT("PassThrough AnchorMinY"), Result.Anchors.Min.Y, Node.Slot.Anchors.Min.Y);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutAdapterAutoSizePreservedTest,
	"Design2UMG.LayoutAdapter.AutoSizePreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutAdapterAutoSizePreservedTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Bounds = {100.f, 100.f, 400.f, 200.f};
	Node.Slot.bAutoSize = true;

	UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	UWidgetTree* Tree = NewObject<UWidgetTree>(WBP, NAME_None, RF_Transient);
	UCanvasPanel* Canvas = NewObject<UCanvasPanel>(Tree, NAME_None, RF_Transient);
	Tree->RootWidget = Canvas;

	const FUMGSlotData Result = FWidgetLayoutAdapter::Resolve(Node, Canvas, Tree, FIntPoint(1920, 1080), true);

	TestTrue(TEXT("bAutoSize preserved"), Result.bAutoSize);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutAdapterTopologyInvariantTest,
	"Design2UMG.LayoutAdapter.TopologyInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutAdapterTopologyInvariantTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData WidgetData;
	WidgetData.WidgetName = TEXT("WBP_TopologyTest");

	FUMGWidgetNode Root;
	Root.Type = TEXT("CanvasPanel");
	Root.Id = TEXT("topo_root");
	Root.Name = TEXT("topo_root");
	Root.Bounds = {0, 0, 1920, 1080};

	FUMGWidgetNode Child;
	Child.Type = TEXT("TextBlock");
	Child.Id = TEXT("topo_text");
	Child.Name = TEXT("topo_text");
	Child.Bounds = {100, 100, 200, 50};
	Root.Children.Add(Child);

	WidgetData.RootWidget = Root;

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/TopologyInvariant");
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(WidgetData, OutputPath);
	if (!TestNotNull(TEXT("WBP created"), WBP)) return false;
	if (!TestNotNull(TEXT("WidgetTree exists"), WBP->WidgetTree.Get())) return false;

	TArray<FString> Snapshot;
	WBP->WidgetTree->ForEachWidget([&](UWidget* W) {
		Snapshot.Add(W->GetClass()->GetName());
	});

	TestTrue(TEXT("Has CanvasPanel"), Snapshot.Contains(TEXT("CanvasPanel")));
	TestTrue(TEXT("Has TextBlock"), Snapshot.Contains(TEXT("TextBlock")));
	TestEqual(TEXT("Exactly 2 widgets"), Snapshot.Num(), 2);

	return true;
}

#endif // WITH_AUTOMATION_WORKER
