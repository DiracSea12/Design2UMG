// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserLayout.h"

#include "DesignJsonUtils.h"
#include "DesignSchemaSourceExtensions.h"
#include "FigmaParserLayoutBounds.h"
#include "FigmaParserMetadata.h"
#include "Dom/JsonValue.h"

namespace FigmaParserInternal
{
using DesignJsonUtils::GetArray;
using DesignJsonUtils::GetBool;
using DesignJsonUtils::GetNumber;
using DesignJsonUtils::GetObject;
using DesignJsonUtils::GetString;
using DesignJsonUtils::ToCompactString;
using DesignJsonUtils::TryGetNumber;

namespace
{
	// Preserve the existing wrap heuristic; this only gives the thresholds names.
	constexpr int32 WrappedHorizontalLayoutMinChildren = 6;
	constexpr int32 WrappedHorizontalLayoutMinFillChildren = 4;
	constexpr int32 WrappedHorizontalLayoutMinRows = 2;
	constexpr float WrappedHorizontalLayoutRowSeparationTolerance = 1.0f;

	bool IsWrapLayout(const TSharedPtr<FJsonObject>& NodeObj)
	{
		const FString Wrap = GetString(NodeObj, TEXT("layoutWrap"));
		return Wrap.Equals(TEXT("WRAP"), ESearchCase::IgnoreCase);
	}

	bool IsLikelyWrappedHorizontalLayout(const TSharedPtr<FJsonObject>& NodeObj)
	{
		if (!NodeObj.IsValid() || GetString(NodeObj, TEXT("layoutMode")) != TEXT("HORIZONTAL"))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(NodeObj, TEXT("children"));
		if (!Children || Children->Num() < WrappedHorizontalLayoutMinChildren)
		{
			return false;
		}

		int32 FillChildren = 0;
		TArray<float> ChildRowsY;
		ChildRowsY.Reserve(Children->Num());

		for (const TSharedPtr<FJsonValue>& ChildVal : *Children)
		{
			const TSharedPtr<FJsonObject> ChildObj = ChildVal->AsObject();
			if (!ChildObj.IsValid())
			{
				continue;
			}

			if (GetString(ChildObj, TEXT("layoutSizingHorizontal")) == TEXT("FILL"))
			{
				++FillChildren;
			}

			if (const TSharedPtr<FJsonObject> BoundsObj = GetObject(ChildObj, TEXT("absoluteBoundingBox")))
			{
				ChildRowsY.Add(GetNumber(BoundsObj, TEXT("y")));
			}
		}

		if (FillChildren < WrappedHorizontalLayoutMinFillChildren
			|| ChildRowsY.Num() < WrappedHorizontalLayoutMinFillChildren)
		{
			return false;
		}

		ChildRowsY.Sort();
		int32 RowCount = 1;
		float LastY = ChildRowsY[0];
		for (int32 Index = 1; Index < ChildRowsY.Num(); ++Index)
		{
			if (FMath::Abs(ChildRowsY[Index] - LastY) > WrappedHorizontalLayoutRowSeparationTolerance)
			{
				++RowCount;
				LastY = ChildRowsY[Index];
			}
		}

		return RowCount >= WrappedHorizontalLayoutMinRows;
	}

	void AddBoolExtensionIfPresent(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions, const FString& Key)
	{
		bool Value = false;
		if (Obj.IsValid() && Obj->TryGetBoolField(Key, Value))
		{
			Extensions.Add(Key, Value ? TEXT("true") : TEXT("false"));
		}
	}

	void SetFloatFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, TOptional<float>& OutValue)
	{
		double Value = 0.0;
		if (TryGetNumber(Obj, Key, Value))
		{
			OutValue.Emplace(static_cast<float>(Value));
		}
	}

	void SetIntFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, TOptional<int32>& OutValue)
	{
		double Value = 0.0;
		if (TryGetNumber(Obj, Key, Value))
		{
			OutValue.Emplace(FMath::RoundToInt(Value));
		}
	}

	TOptional<EDesignAxisAlignment> ParseAxisAlignment(const FString& Value)
	{
		if (Value.Equals(TEXT("MIN"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("LEFT"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("TOP"), ESearchCase::IgnoreCase))
		{
			return EDesignAxisAlignment::Start;
		}
		if (Value.Equals(TEXT("CENTER"), ESearchCase::IgnoreCase))
		{
			return EDesignAxisAlignment::Center;
		}
		if (Value.Equals(TEXT("MAX"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("RIGHT"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("BOTTOM"), ESearchCase::IgnoreCase))
		{
			return EDesignAxisAlignment::End;
		}
		if (Value.Equals(TEXT("STRETCH"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("FILL"), ESearchCase::IgnoreCase))
		{
			return EDesignAxisAlignment::Fill;
		}
		return {};
	}

	void ParseFigmaGridLayout(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
	{
		SetIntFieldIfPresent(NodeObj, TEXT("gridRowCount"), OutNode.LayoutData.LayoutRowCount);
		SetIntFieldIfPresent(NodeObj, TEXT("gridColumnCount"), OutNode.LayoutData.LayoutColumnCount);
		SetFloatFieldIfPresent(NodeObj, TEXT("gridRowGap"), OutNode.LayoutData.LayoutRowGap);
		SetFloatFieldIfPresent(NodeObj, TEXT("gridColumnGap"), OutNode.LayoutData.LayoutColumnGap);
		SetIntFieldIfPresent(NodeObj, TEXT("gridRowSpan"), OutNode.LayoutData.LayoutRowSpan);
		SetIntFieldIfPresent(NodeObj, TEXT("gridColumnSpan"), OutNode.LayoutData.LayoutColumnSpan);
		SetIntFieldIfPresent(NodeObj, TEXT("gridRowAnchorIndex"), OutNode.LayoutData.LayoutRow);
		SetIntFieldIfPresent(NodeObj, TEXT("gridColumnAnchorIndex"), OutNode.LayoutData.LayoutColumn);

		if (OutNode.LayoutData.LayoutRowCount.IsSet())
		{
			OutNode.LayoutData.LayoutRowCount = FMath::Max(0, OutNode.LayoutData.LayoutRowCount.GetValue());
		}
		if (OutNode.LayoutData.LayoutColumnCount.IsSet())
		{
			OutNode.LayoutData.LayoutColumnCount = FMath::Max(0, OutNode.LayoutData.LayoutColumnCount.GetValue());
		}
		if (OutNode.LayoutData.LayoutRowGap.IsSet())
		{
			OutNode.LayoutData.LayoutRowGap = FMath::Max(0.0f, OutNode.LayoutData.LayoutRowGap.GetValue());
		}
		else if (OutNode.LayoutData.WrapCrossAxisGap.IsSet())
		{
			OutNode.LayoutData.LayoutRowGap = OutNode.LayoutData.WrapCrossAxisGap;
		}
		if (OutNode.LayoutData.LayoutColumnGap.IsSet())
		{
			OutNode.LayoutData.LayoutColumnGap = FMath::Max(0.0f, OutNode.LayoutData.LayoutColumnGap.GetValue());
		}
		OutNode.LayoutData.LayoutChildHorizontalAlign = ParseAxisAlignment(GetString(NodeObj, TEXT("gridChildHorizontalAlign")));
		OutNode.LayoutData.LayoutChildVerticalAlign = ParseAxisAlignment(GetString(NodeObj, TEXT("gridChildVerticalAlign")));
		if (OutNode.LayoutData.LayoutRowSpan.IsSet())
		{
			OutNode.LayoutData.LayoutRowSpan = FMath::Max(1, OutNode.LayoutData.LayoutRowSpan.GetValue());
		}
		if (OutNode.LayoutData.LayoutColumnSpan.IsSet())
		{
			OutNode.LayoutData.LayoutColumnSpan = FMath::Max(1, OutNode.LayoutData.LayoutColumnSpan.GetValue());
		}
		if (OutNode.LayoutData.LayoutRow.IsSet())
		{
			OutNode.LayoutData.LayoutRow = FMath::Max(0, OutNode.LayoutData.LayoutRow.GetValue());
		}
		if (OutNode.LayoutData.LayoutColumn.IsSet())
		{
			OutNode.LayoutData.LayoutColumn = FMath::Max(0, OutNode.LayoutData.LayoutColumn.GetValue());
		}
	}

	bool IsStretchAutoLayoutChild(const FDesignNode& ParentNode, const TSharedPtr<FDesignNode>& Child)
	{
		if (!Child.IsValid() || Child->LayoutData.bAbsoluteLayoutPositioning)
		{
			return false;
		}

		if (Child->LayoutData.SelfAlignment.Get(EDesignAxisAlignment::Start) == EDesignAxisAlignment::Fill)
		{
			return true;
		}

		if (ParentNode.LayoutData.Layout == EDesignLayout::Vertical)
		{
			return Child->Transform.Width.IsFill();
		}

		if (ParentNode.LayoutData.Layout == EDesignLayout::Horizontal)
		{
			return Child->Transform.Height.IsFill();
		}

		return false;
	}

	bool ShouldTopLevelFigmaInstancePaddingWrapAtRuntime(const FDesignNode& Node)
	{
		bool bHasStretchAutoChild = false;
		bool bHasNonStretchAutoChild = false;
		for (const TSharedPtr<FDesignNode>& Child : Node.Children)
		{
			if (!Child.IsValid() || Child->LayoutData.bAbsoluteLayoutPositioning)
			{
				continue;
			}

			if (IsStretchAutoLayoutChild(Node, Child))
			{
				bHasStretchAutoChild = true;
			}
			else
			{
				bHasNonStretchAutoChild = true;
			}

			if (bHasStretchAutoChild && bHasNonStretchAutoChild)
			{
				return true;
			}
		}

		return false;
	}

	bool ShouldFigmaAutoLayoutPaddingWrapAtRuntime(const FDesignNode& Node, const bool bHasInstanceAncestor)
	{
		if (Node.LayoutData.Layout != EDesignLayout::Horizontal && Node.LayoutData.Layout != EDesignLayout::Vertical)
		{
			return false;
		}

		if (bHasInstanceAncestor)
		{
			return false;
		}

		if (Node.Source().ComponentInstance.bIsInstance)
		{
			return ShouldTopLevelFigmaInstancePaddingWrapAtRuntime(Node);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node.Children)
		{
			if (!Child.IsValid() || Child->LayoutData.bAbsoluteLayoutPositioning)
			{
				continue;
			}

			return true;
		}

		return false;
	}
}

bool HasAutoLayout(const TSharedPtr<FJsonObject>& NodeObj)
{
	if (IsWrapLayout(NodeObj) || IsLikelyWrappedHorizontalLayout(NodeObj))
	{
		return true;
	}

	const FString Mode = GetString(NodeObj, TEXT("layoutMode"));
	return Mode == TEXT("HORIZONTAL") || Mode == TEXT("VERTICAL") || Mode == TEXT("GRID");
}

void ParseLayout(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
{
	const bool bWrapLayout = IsWrapLayout(NodeObj) || IsLikelyWrappedHorizontalLayout(NodeObj);
	const FString Mode = GetString(NodeObj, TEXT("layoutMode"));
	if (bWrapLayout)
	{
		OutNode.LayoutData.Layout = EDesignLayout::Wrap;
	}
	else if (Mode == TEXT("HORIZONTAL"))
	{
		OutNode.LayoutData.Layout = EDesignLayout::Horizontal;
	}
	else if (Mode == TEXT("VERTICAL"))
	{
		OutNode.LayoutData.Layout = EDesignLayout::Vertical;
	}
	else if (Mode == TEXT("GRID"))
	{
		OutNode.LayoutData.Layout = EDesignLayout::Grid;
	}
	else
	{
		OutNode.LayoutData.Layout = EDesignLayout::None;
	}

	OutNode.LayoutData.SetGap(GetNumber(NodeObj, TEXT("itemSpacing")));

	FDesignPadding Padding;
	Padding.Left = GetNumber(NodeObj, TEXT("paddingLeft"));
	Padding.Right = GetNumber(NodeObj, TEXT("paddingRight"));
	Padding.Top = GetNumber(NodeObj, TEXT("paddingTop"));
	Padding.Bottom = GetNumber(NodeObj, TEXT("paddingBottom"));
	OutNode.LayoutData.SetPadding(Padding);

	OutNode.LayoutData.bAbsoluteLayoutPositioning =
		GetString(NodeObj, TEXT("layoutPositioning")).Equals(TEXT("ABSOLUTE"), ESearchCase::IgnoreCase);
	OutNode.LayoutData.SelfAlignment = ParseAxisAlignment(GetString(NodeObj, TEXT("layoutAlign")));
	const FString OverflowDirection = GetString(NodeObj, TEXT("overflowDirection"));
	if (OverflowDirection.Equals(TEXT("HORIZONTAL_SCROLLING"), ESearchCase::IgnoreCase))
	{
		OutNode.LayoutData.Overflow = EDesignOverflow::HorizontalScrolling;
	}
	else if (OverflowDirection.Equals(TEXT("VERTICAL_SCROLLING"), ESearchCase::IgnoreCase))
	{
		OutNode.LayoutData.Overflow = EDesignOverflow::VerticalScrolling;
	}

	FigmaParserMetadata::CaptureLayoutMetadataExtensions(NodeObj, OutNode.Metadata);
	AddBoolExtensionIfPresent(NodeObj, OutNode.Metadata, TEXT("itemReverseZIndex"));
	AddBoolExtensionIfPresent(NodeObj, OutNode.Metadata, TEXT("isExposedInstance"));
	// Keep the source-shaped Figma key as an explicit metadata alias this phase.
	AddBoolExtensionIfPresent(NodeObj, OutNode.Metadata, TEXT("strokesIncludedInLayout"));

	const TSharedPtr<FJsonValue> BoundVarsField = NodeObj->TryGetField(TEXT("boundVariables"));
	if (BoundVarsField.IsValid() && BoundVarsField->Type == EJson::Object)
	{
		OutNode.Metadata.Add(TEXT("boundVariables"), ToCompactString(BoundVarsField));
		const TSharedPtr<FJsonObject> BoundVarsObj = BoundVarsField->AsObject();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BoundVarsObj->Values)
		{
			OutNode.Metadata.Add(FString::Printf(TEXT("boundVariables.%s"), *Pair.Key), ToCompactString(Pair.Value));
		}
	}

	const FString Primary = GetString(NodeObj, TEXT("primaryAxisAlignItems"));
	if (Primary == TEXT("CENTER"))
	{
		OutNode.LayoutData.JustifyContent = EDesignJustifyContent::Center;
	}
	else if (Primary == TEXT("MAX"))
	{
		OutNode.LayoutData.JustifyContent = EDesignJustifyContent::End;
	}
	else if (Primary == TEXT("SPACE_BETWEEN"))
	{
		OutNode.LayoutData.JustifyContent = EDesignJustifyContent::SpaceBetween;
	}
	else
	{
		OutNode.LayoutData.JustifyContent = EDesignJustifyContent::Start;
	}

	const FString Counter = GetString(NodeObj, TEXT("counterAxisAlignItems"));
	if (Counter == TEXT("CENTER"))
	{
		OutNode.LayoutData.AlignItems = EDesignAlignItems::Center;
	}
	else if (Counter == TEXT("MAX"))
	{
		OutNode.LayoutData.AlignItems = EDesignAlignItems::End;
	}
	else
	{
		OutNode.LayoutData.AlignItems = EDesignAlignItems::Start;
	}

	OutNode.LayoutData.SetClipValue(GetBool(NodeObj, TEXT("clipsContent"), false));

	SetFloatFieldIfPresent(NodeObj, TEXT("counterAxisSpacing"), OutNode.LayoutData.WrapCrossAxisGap);
	if (OutNode.LayoutData.WrapCrossAxisGap.IsSet())
	{
		OutNode.LayoutData.WrapCrossAxisGap = FMath::Max(0.0f, OutNode.LayoutData.WrapCrossAxisGap.GetValue());
	}
	ParseFigmaGridLayout(NodeObj, OutNode);

	if (OutNode.LayoutData.Layout == EDesignLayout::Horizontal || OutNode.LayoutData.Layout == EDesignLayout::Vertical)
	{
		OutNode.LayoutData.PaddingRequiresRuntimeWrap = false;
	}
	else
	{
		OutNode.LayoutData.PaddingRequiresRuntimeWrap =
			OutNode.LayoutData.Layout == EDesignLayout::None
			|| OutNode.LayoutData.Layout == EDesignLayout::Wrap
			|| OutNode.LayoutData.Layout == EDesignLayout::Grid;
	}
}

void ParseSize(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FDesignNode& OutNode, bool bParentHasAutoLayout)
{
	FNodeBounds VisualBounds;
	FNodeBounds LayoutBounds;
	const bool bHasVisualBounds = TryGetVisualBounds(NodeObj, ParentObj, VisualBounds);
	const bool bHasLayoutBounds = TryReadBounds(GetObject(NodeObj, TEXT("absoluteBoundingBox")), LayoutBounds);
	if (bHasLayoutBounds)
	{
		OutNode.Transform.LayoutBoundsSize = FVector2D(LayoutBounds.W, LayoutBounds.H);
	}

	const FNodeBounds& EffectiveBounds = bParentHasAutoLayout
		? (bHasLayoutBounds ? LayoutBounds : VisualBounds)
		: (bHasVisualBounds ? VisualBounds : LayoutBounds);

	const float Width = EffectiveBounds.W;
	const float Height = EffectiveBounds.H;
	const FString SizingH = GetString(NodeObj, TEXT("layoutSizingHorizontal"), TEXT("FIXED"));
	const FString SizingV = GetString(NodeObj, TEXT("layoutSizingVertical"), TEXT("FIXED"));

	if (bParentHasAutoLayout)
	{
		if (SizingH == TEXT("FILL"))
		{
			OutNode.Transform.SetWidthValue(FDesignSize::Fill(Width));
		}
		else if (SizingH == TEXT("HUG"))
		{
			OutNode.Transform.SetWidthValue(FDesignSize::Fit(Width));
		}
		else
		{
			OutNode.Transform.SetWidthValue(FDesignSize::Fixed(Width));
		}

		if (SizingV == TEXT("FILL"))
		{
			OutNode.Transform.SetHeightValue(FDesignSize::Fill(Height));
		}
		else if (SizingV == TEXT("HUG"))
		{
			OutNode.Transform.SetHeightValue(FDesignSize::Fit(Height));
		}
		else
		{
			OutNode.Transform.SetHeightValue(FDesignSize::Fixed(Height));
		}
	}
	else
	{
		const bool bNodeHasAutoLayout = HasAutoLayout(NodeObj);
		OutNode.Transform.SetWidthValue(bNodeHasAutoLayout && SizingH == TEXT("HUG")
			? FDesignSize::Fit()
			: FDesignSize::Fixed(Width));
		OutNode.Transform.SetHeightValue(bNodeHasAutoLayout && SizingV == TEXT("HUG")
			? FDesignSize::Fit()
			: FDesignSize::Fixed(Height));
	}

	double GrowValue = 0.0;
	if (NodeObj->TryGetNumberField(TEXT("layoutGrow"), GrowValue))
	{
		OutNode.LayoutData.LayoutGrow = static_cast<float>(GrowValue);
	}
}

void ParsePosition(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	const FNodeBounds* ParentCoordinateBounds,
	FDesignNode& OutNode,
	bool bParentHasAutoLayout)
{
	if (bParentHasAutoLayout)
	{
		OutNode.Transform.ClearPosition();
		return;
	}

	FNodeBounds ChildBounds;
	const bool bHasChildBounds = TryGetVisualBounds(NodeObj, ParentObj, ChildBounds);

	FNodeBounds ParentBounds;
	const bool bHasParentBounds = ParentCoordinateBounds
		? (ParentBounds = *ParentCoordinateBounds, true)
		: ParentObj && TryGetVisualBounds(ParentObj, nullptr, ParentBounds);

	if (bHasChildBounds && bHasParentBounds)
	{
		OutNode.Transform.SetPosition(ChildBounds.X - ParentBounds.X, ChildBounds.Y - ParentBounds.Y);
	}
	else if (bHasChildBounds)
	{
		OutNode.Transform.SetPosition(ChildBounds.X, ChildBounds.Y);
	}
}

void FinalizeNodePaddingRuntimeWrap(FDesignNode& Node, bool bInsideInstanceSubtree)
{
	if (Node.LayoutData.Layout == EDesignLayout::Horizontal || Node.LayoutData.Layout == EDesignLayout::Vertical)
	{
		Node.LayoutData.PaddingRequiresRuntimeWrap = ShouldFigmaAutoLayoutPaddingWrapAtRuntime(Node, bInsideInstanceSubtree);
	}
	else if (Node.LayoutData.Layout == EDesignLayout::Wrap || Node.LayoutData.Layout == EDesignLayout::Grid)
	{
		Node.LayoutData.PaddingRequiresRuntimeWrap = Node.Children.Num() > 0;
	}
}
}
