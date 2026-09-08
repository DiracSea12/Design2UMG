// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGCanvasFlattening.h"
#include "Design2UMGLog.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTypes.h"

namespace
{
	// Depth 0 is the root's direct children, depth 1 their children. Nodes at or above
	// this depth are preserved; deeper bare canvases merge into their canvas parent.
	constexpr int32 MaxPreservedDepth = 2;

	bool IsBareCanvas(const FUMGWidgetNode& Node)
	{
		// Wrappers rebase canvases (Border/RetainerBox/Button take over Type), so a plain
		// CanvasPanel node is a pure coordinate container by construction. Component
		// instances and mapping candidates keep their identity: reimport manifests and
		// editor-side component replacement address those subtrees wholesale.
		return Node.Type == UMGWidgetTypes::CanvasPanel
			&& !(Node.HasSourceData()
				&& (Node.Source().bMappedComponentCandidate || Node.Source().IsFigmaInstance()));
	}

	bool IsPointAnchoredSlot(const FUMGSlotData& Slot)
	{
		// Anchored slots scale their geometry against the parent's size; reparenting them
		// changes that reference size and breaks the layout.
		return Slot.Anchors.Min.X == 0.0f && Slot.Anchors.Min.Y == 0.0f
			&& Slot.Anchors.Max.X == 0.0f && Slot.Anchors.Max.Y == 0.0f;
	}

	bool CanTranslateIntoParent(const FUMGWidgetNode& NestedCanvas)
	{
		// A non-zero canvas-slot alignment pivots the child inside its anchor rect, so
		// Offsets.Left/Top no longer equal the canvas origin and translation would drift.
		if (!IsPointAnchoredSlot(NestedCanvas.Slot)
			|| NestedCanvas.Slot.Alignment.X != 0.0f
			|| NestedCanvas.Slot.Alignment.Y != 0.0f)
		{
			return false;
		}

		for (const FUMGWidgetNode& GrandChild : NestedCanvas.Children)
		{
			if (!IsPointAnchoredSlot(GrandChild.Slot))
			{
				return false;
			}
		}
		return true;
	}

	void FlattenNode(FUMGWidgetNode& Parent, const int32 Depth, Design2UMGCanvasFlattening::FFlattenStats& OutStats)
	{
		for (int32 ChildIndex = 0; ChildIndex < Parent.Children.Num(); )
		{
			FUMGWidgetNode& Child = Parent.Children[ChildIndex];
			FlattenNode(Child, Depth + 1, OutStats);

			// The root, the document's top-level frames, and their direct children are all
			// page/block-level entities (reimport manifests, editor workflows and developer
			// mounts address them); only deeper nesting is coordinate relay.
			if (Depth >= MaxPreservedDepth
				&& Parent.Type == UMGWidgetTypes::CanvasPanel
				&& IsBareCanvas(Child))
			{
				if (Child.Children.Num() == 0)
				{
					UE_LOG(LogDesign2UMG, Verbose, TEXT("CanvasFlattening: removed empty canvas '%s'"), *Child.Name);
					Parent.Children.RemoveAt(ChildIndex);
					++OutStats.RemovedEmptyCanvasCount;
					continue;
				}

				if (CanTranslateIntoParent(Child))
				{
					const float OffsetX = Child.Slot.Offsets.Left;
					const float OffsetY = Child.Slot.Offsets.Top;
					// Detach grandchildren before splicing: inserting into Parent.Children reallocates
					// it and would dangle the Child reference into the parent array.
					TArray<FUMGWidgetNode> GrandChildren = MoveTemp(Child.Children);
					for (FUMGWidgetNode& GrandChild : GrandChildren)
					{
						GrandChild.Slot.Offsets.Left += OffsetX;
						GrandChild.Slot.Offsets.Top += OffsetY;
						GrandChild.Bounds.X += OffsetX;
						GrandChild.Bounds.Y += OffsetY;
					}
					UE_LOG(LogDesign2UMG, Verbose, TEXT("CanvasFlattening: merged nested canvas '%s' (%d children, offset %.1f,%.1f)"),
						*Child.Name, GrandChildren.Num(), OffsetX, OffsetY);

					const int32 GrandChildCount = GrandChildren.Num();
					for (int32 GrandIndex = 0; GrandIndex < GrandChildCount; ++GrandIndex)
					{
						Parent.Children.Insert(MoveTemp(GrandChildren[GrandIndex]), ChildIndex + GrandIndex);
					}
					Parent.Children.RemoveAt(ChildIndex + GrandChildCount);
					++OutStats.MergedNestedCanvasCount;
					// The first spliced grandchild now sits at ChildIndex and may itself be
					// a mergeable bare canvas, so re-evaluate without advancing.
					continue;
				}
			}

			++ChildIndex;
		}
	}
} // namespace

void Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(FUMGWidgetNode& Root, FFlattenStats& OutStats)
{
	FlattenNode(Root, 0, OutStats);
}
