// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Design2UMGCanvasFlattening.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTypes.h"

namespace
{
	FUMGWidgetNode MakeCanvas(const FString& Name, float X, float Y)
	{
		FUMGWidgetNode Node;
		Node.Type = UMGWidgetTypes::CanvasPanel;
		Node.Name = Name;
		Node.Id = Name;
		Node.Slot.Offsets.Left = X;
		Node.Slot.Offsets.Top = Y;
		return Node;
	}

	FUMGWidgetNode MakeLeaf(const FString& Name, float X, float Y)
	{
		FUMGWidgetNode Node;
		Node.Type = UMGWidgetTypes::Image;
		Node.Name = Name;
		Node.Id = Name;
		Node.Slot.Offsets.Left = X;
		Node.Slot.Offsets.Top = Y;
		Node.Bounds.X = X;
		Node.Bounds.Y = Y;
		Node.Bounds.W = 10.0f;
		Node.Bounds.H = 10.0f;
		return Node;
	}

	void SetStretchAnchors(FUMGWidgetNode& Node)
	{
		Node.Slot.Anchors.Max.X = 1.0f;
	}

	// Root, its direct children, and their direct children are preserved entities, so the
	// merge/delete scenarios mount two bare canvas levels below the root.
	FUMGWidgetNode MakeScene(TArray<FUMGWidgetNode>&& MidChildren)
	{
		FUMGWidgetNode Root = MakeCanvas(TEXT("root"), 0.0f, 0.0f);
		FUMGWidgetNode Page = MakeCanvas(TEXT("page"), 0.0f, 0.0f);
		FUMGWidgetNode Mid = MakeCanvas(TEXT("mid"), 0.0f, 0.0f);
		Mid.Children = MoveTemp(MidChildren);
		Page.Children.Add(MoveTemp(Mid));
		Root.Children.Add(MoveTemp(Page));
		return Root;
	}
}

// ---------------------------------------------------------------------------
// Nested bare canvas merge with linear translation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCanvasFlatteningMergeNestedCanvasTest,
	"Design2UMG.Unit.CanvasFlattening.MergeNestedCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCanvasFlatteningMergeNestedCanvasTest::RunTest(const FString&)
{
	TArray<FUMGWidgetNode> MidChildren;
	MidChildren.Add(MakeLeaf(TEXT("before"), 1.0f, 2.0f));
	FUMGWidgetNode Inner = MakeCanvas(TEXT("inner"), 20.0f, 30.0f);
	Inner.Children.Add(MakeLeaf(TEXT("a"), 5.0f, 6.0f));
	Inner.Children.Add(MakeLeaf(TEXT("b"), 10.0f, 12.0f));
	MidChildren.Add(MoveTemp(Inner));
	MidChildren.Add(MakeLeaf(TEXT("after"), 40.0f, 50.0f));
	FUMGWidgetNode Root = MakeScene(MoveTemp(MidChildren));

	Design2UMGCanvasFlattening::FFlattenStats Stats;
	Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Root, Stats);

	TestEqual(TEXT("merged one nested canvas"), Stats.MergedNestedCanvasCount, 1);
	TestEqual(TEXT("removed nothing"), Stats.RemovedEmptyCanvasCount, 0);
	TestEqual(TEXT("mid child count"), Root.Children[0].Children[0].Children.Num(), 4);
	TestEqual(TEXT("sibling order preserved"), Root.Children[0].Children[0].Children[0].Name, TEXT("before"));
	TestEqual(TEXT("grandchild a spliced at index 1"), Root.Children[0].Children[0].Children[1].Name, TEXT("a"));
	TestEqual(TEXT("grandchild b spliced at index 2"), Root.Children[0].Children[0].Children[2].Name, TEXT("b"));
	TestEqual(TEXT("sibling order preserved after"), Root.Children[0].Children[0].Children[3].Name, TEXT("after"));
	TestEqual(TEXT("a translated X"), Root.Children[0].Children[0].Children[1].Slot.Offsets.Left, 25.0f);
	TestEqual(TEXT("a translated Y"), Root.Children[0].Children[0].Children[1].Slot.Offsets.Top, 36.0f);
	TestEqual(TEXT("a bounds translated X"), Root.Children[0].Children[0].Children[1].Bounds.X, 25.0f);
	TestEqual(TEXT("b translated X"), Root.Children[0].Children[0].Children[2].Slot.Offsets.Left, 30.0f);
	TestEqual(TEXT("b translated Y"), Root.Children[0].Children[0].Children[2].Slot.Offsets.Top, 42.0f);
	return true;
}

// ---------------------------------------------------------------------------
// Multi-level nesting collapses recursively; top-level children stay intact
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCanvasFlatteningRecursiveCollapseTest,
	"Design2UMG.Unit.CanvasFlattening.RecursiveCollapse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCanvasFlatteningRecursiveCollapseTest::RunTest(const FString&)
{
	TArray<FUMGWidgetNode> MidChildren;
	FUMGWidgetNode Outer = MakeCanvas(TEXT("outer"), 100.0f, 0.0f);
	FUMGWidgetNode Inner = MakeCanvas(TEXT("inner"), 0.0f, 50.0f);
	Inner.Children.Add(MakeLeaf(TEXT("leaf"), 7.0f, 8.0f));
	Outer.Children.Add(MoveTemp(Inner));
	MidChildren.Add(MoveTemp(Outer));
	FUMGWidgetNode Root = MakeScene(MoveTemp(MidChildren));

	Design2UMGCanvasFlattening::FFlattenStats Stats;
	Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Root, Stats);

	// Both outer and inner sit below the preserved depth, so they fold recursively:
	// the leaf lands directly under mid with the accumulated translation.
	TestEqual(TEXT("both levels merged"), Stats.MergedNestedCanvasCount, 2);
	const FUMGWidgetNode& Mid = Root.Children[0].Children[0];
	TestEqual(TEXT("mid keeps one child"), Mid.Children.Num(), 1);
	TestEqual(TEXT("leaf promoted under mid"), Mid.Children[0].Name, TEXT("leaf"));
	TestEqual(TEXT("leaf cumulative translation X"), Mid.Children[0].Slot.Offsets.Left, 107.0f);
	TestEqual(TEXT("leaf cumulative translation Y"), Mid.Children[0].Slot.Offsets.Top, 58.0f);
	return true;
}

// ---------------------------------------------------------------------------
// Anchored slots, non-canvas parents, wrapped canvases and identity nodes block the merge
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCanvasFlatteningBlocksUnsafeMergeTest,
	"Design2UMG.Unit.CanvasFlattening.BlocksUnsafeMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCanvasFlatteningBlocksUnsafeMergeTest::RunTest(const FString&)
{
	// Stretch-anchored nested canvas: its geometry follows the parent size, not Offsets.
	{
		TArray<FUMGWidgetNode> MidChildren;
		FUMGWidgetNode Inner = MakeCanvas(TEXT("inner"), 0.0f, 0.0f);
		SetStretchAnchors(Inner);
		Inner.Children.Add(MakeLeaf(TEXT("a"), 0.0f, 0.0f));
		MidChildren.Add(MoveTemp(Inner));
		FUMGWidgetNode Root = MakeScene(MoveTemp(MidChildren));

		Design2UMGCanvasFlattening::FFlattenStats Stats;
		Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Root, Stats);
		TestEqual(TEXT("anchored nested canvas kept"), Stats.MergedNestedCanvasCount, 0);
		TestEqual(TEXT("child kept"), Root.Children[0].Children[0].Children.Num(), 1);
	}

	// Stretch-anchored grandchild: reparenting would re-anchor it against a different size.
	{
		TArray<FUMGWidgetNode> MidChildren;
		FUMGWidgetNode Inner = MakeCanvas(TEXT("inner"), 0.0f, 0.0f);
		FUMGWidgetNode StretchedLeaf = MakeLeaf(TEXT("s"), 0.0f, 0.0f);
		SetStretchAnchors(StretchedLeaf);
		Inner.Children.Add(MoveTemp(StretchedLeaf));
		MidChildren.Add(MoveTemp(Inner));
		FUMGWidgetNode Root = MakeScene(MoveTemp(MidChildren));

		Design2UMGCanvasFlattening::FFlattenStats Stats;
		Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Root, Stats);
		TestEqual(TEXT("anchored grandchild blocks merge"), Stats.MergedNestedCanvasCount, 0);
		TestEqual(TEXT("nested canvas kept"), Root.Children[0].Children[0].Children.Num(), 1);
	}

	// Non-canvas parent keeps its canvas child (only canvas-in-canvas is a coordinate relay).
	{
		FUMGWidgetNode Box;
		Box.Type = UMGWidgetTypes::VerticalBox;
		Box.Name = TEXT("vbox");
		// Mount below the preserved depth so the exemption cannot mask the parent-type rule.
		FUMGWidgetNode Deep;
		Deep.Type = UMGWidgetTypes::VerticalBox;
		Deep.Name = TEXT("deep_vbox");
		FUMGWidgetNode Inner = MakeCanvas(TEXT("inner"), 0.0f, 0.0f);
		Inner.Children.Add(MakeLeaf(TEXT("a"), 0.0f, 0.0f));
		Deep.Children.Add(MoveTemp(Inner));
		Box.Children.Add(MoveTemp(Deep));

		Design2UMGCanvasFlattening::FFlattenStats Stats;
		Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Box, Stats);
		TestEqual(TEXT("non-canvas parent keeps child"), Stats.MergedNestedCanvasCount, 0);
		TestEqual(TEXT("canvas child kept"), Box.Children[0].Children.Num(), 1);
	}

	// Wrapped canvases (visual/effect/button rebase Type) are not bare canvases.
	{
		TArray<FUMGWidgetNode> MidChildren;
		FUMGWidgetNode Wrapped = MakeCanvas(TEXT("wrapped"), 0.0f, 0.0f);
		Wrapped.Type = UMGWidgetTypes::Border;
		Wrapped.Children.Add(MakeLeaf(TEXT("a"), 0.0f, 0.0f));
		MidChildren.Add(MoveTemp(Wrapped));
		FUMGWidgetNode Root = MakeScene(MoveTemp(MidChildren));

		Design2UMGCanvasFlattening::FFlattenStats Stats;
		Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Root, Stats);
		TestEqual(TEXT("wrapped canvas kept"), Stats.MergedNestedCanvasCount, 0);
	}

	// Component-mapping candidates must stay addressable for editor-side replacement.
	{
		TArray<FUMGWidgetNode> MidChildren;
		FUMGWidgetNode Candidate = MakeCanvas(TEXT("candidate"), 0.0f, 0.0f);
		Candidate.MutableSource().bMappedComponentCandidate = true;
		Candidate.Children.Add(MakeLeaf(TEXT("a"), 0.0f, 0.0f));
		MidChildren.Add(MoveTemp(Candidate));
		FUMGWidgetNode Root = MakeScene(MoveTemp(MidChildren));

		Design2UMGCanvasFlattening::FFlattenStats Stats;
		Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Root, Stats);
		TestEqual(TEXT("mapped candidate kept"), Stats.MergedNestedCanvasCount, 0);
	}

	// Figma component instances carry identity (reimport manifests address them); they are
	// never layout relays even when they expand into a plain canvas.
	{
		TArray<FUMGWidgetNode> MidChildren;
		FUMGWidgetNode Instance = MakeCanvas(TEXT("instance"), 0.0f, 0.0f);
		Instance.MutableSource().ComponentId = TEXT("77:88");
		Instance.Children.Add(MakeLeaf(TEXT("a"), 0.0f, 0.0f));
		MidChildren.Add(MoveTemp(Instance));
		FUMGWidgetNode Root = MakeScene(MoveTemp(MidChildren));

		Design2UMGCanvasFlattening::FFlattenStats Stats;
		Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Root, Stats);
		TestEqual(TEXT("component instance kept"), Stats.MergedNestedCanvasCount, 0);
	}

	return true;
}

// ---------------------------------------------------------------------------
// The removed canvas's opacity multiplies into each spliced grandchild
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCanvasFlatteningPropagatesCanvasOpacityTest,
	"Design2UMG.Unit.CanvasFlattening.PropagatesCanvasOpacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCanvasFlatteningPropagatesCanvasOpacityTest::RunTest(const FString&)
{
	TArray<FUMGWidgetNode> MidChildren;
	FUMGWidgetNode Inner = MakeCanvas(TEXT("inner"), 20.0f, 30.0f);
	Inner.Render.RenderOpacity = 0.4f;
	Inner.Children.Add(MakeLeaf(TEXT("plain"), 0.0f, 0.0f));
	FUMGWidgetNode OpaqueLeaf = MakeLeaf(TEXT("opaque"), 0.0f, 0.0f);
	OpaqueLeaf.Render.RenderOpacity = 0.5f;
	Inner.Children.Add(MoveTemp(OpaqueLeaf));
	MidChildren.Add(MoveTemp(Inner));
	FUMGWidgetNode Root = MakeScene(MoveTemp(MidChildren));

	Design2UMGCanvasFlattening::FFlattenStats Stats;
	Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Root, Stats);

	TestEqual(TEXT("canvas merged"), Stats.MergedNestedCanvasCount, 1);
	const FUMGWidgetNode& Mid = Root.Children[0].Children[0];
	TestTrue(TEXT("plain grandchild gets canvas opacity"),
		Mid.Children[0].Render.RenderOpacity.IsSet() && FMath::IsNearlyEqual(Mid.Children[0].Render.RenderOpacity.GetValue(), 0.4f));
	TestTrue(TEXT("grandchild opacity composes with its own"),
		Mid.Children[1].Render.RenderOpacity.IsSet() && FMath::IsNearlyEqual(Mid.Children[1].Render.RenderOpacity.GetValue(), 0.2f));
	return true;
}

// ---------------------------------------------------------------------------
// Non-translatable render payload on the canvas keeps the wrapper alive
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCanvasFlatteningBlocksNonTranslatableRenderTest,
	"Design2UMG.Unit.CanvasFlattening.BlocksNonTranslatableRender",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCanvasFlatteningBlocksNonTranslatableRenderTest::RunTest(const FString&)
{
	TArray<FUMGWidgetNode> MidChildren;
	FUMGWidgetNode Rotated = MakeCanvas(TEXT("rotated"), 0.0f, 0.0f);
	Rotated.Render.RenderAngle = 15.0f;
	Rotated.Children.Add(MakeLeaf(TEXT("a"), 0.0f, 0.0f));
	MidChildren.Add(MoveTemp(Rotated));
	FUMGWidgetNode Clipped = MakeCanvas(TEXT("clipped"), 0.0f, 0.0f);
	Clipped.Render.Clipping = TEXT("ClipToBounds");
	Clipped.Children.Add(MakeLeaf(TEXT("b"), 0.0f, 0.0f));
	MidChildren.Add(MoveTemp(Clipped));
	FUMGWidgetNode Root = MakeScene(MoveTemp(MidChildren));

	Design2UMGCanvasFlattening::FFlattenStats Stats;
	Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Root, Stats);

	TestEqual(TEXT("rotated canvas kept"), Stats.MergedNestedCanvasCount, 0);
	TestEqual(TEXT("both wrappers kept"), Root.Children[0].Children[0].Children.Num(), 2);
	return true;
}

// ---------------------------------------------------------------------------
// Childless bare canvases below top level render nothing and are removed
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCanvasFlatteningRemoveEmptyCanvasTest,
	"Design2UMG.Unit.CanvasFlattening.RemoveEmptyCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCanvasFlatteningRemoveEmptyCanvasTest::RunTest(const FString&)
{
	TArray<FUMGWidgetNode> MidChildren;
	MidChildren.Add(MakeLeaf(TEXT("a"), 0.0f, 0.0f));
	MidChildren.Add(MakeCanvas(TEXT("empty"), 5.0f, 5.0f));
	FUMGWidgetNode Candidate = MakeCanvas(TEXT("empty_candidate"), 5.0f, 5.0f);
	Candidate.MutableSource().bMappedComponentCandidate = true;
	MidChildren.Add(MoveTemp(Candidate));
	FUMGWidgetNode Root = MakeScene(MoveTemp(MidChildren));

	Design2UMGCanvasFlattening::FFlattenStats Stats;
	Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Root, Stats);

	TestEqual(TEXT("only the plain empty canvas removed"), Stats.RemovedEmptyCanvasCount, 1);
	TestEqual(TEXT("remaining children"), Root.Children[0].Children[0].Children.Num(), 2);
	TestEqual(TEXT("kept leaf"), Root.Children[0].Children[0].Children[0].Name, TEXT("a"));
	TestEqual(TEXT("kept mapped candidate"), Root.Children[0].Children[0].Children[1].Name, TEXT("empty_candidate"));
	return true;
}

#endif
