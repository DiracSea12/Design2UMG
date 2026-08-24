// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Design2UMGConverterVisualSupport.h"
#include "Design2UMGTextWidgetBuilder.h"
#include "Misc/AutomationTest.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "Dom/JsonObject.h"
#include "Fonts/SlateFontInfo.h"
#include "LayoutFixer.h"
#include "LayoutMapper.h"
#include "SizeEstimator.h"
#include "TextMeasurementInternal.h"
#include "TextMeasurementRuntimeProbe.h"
#include "UMGSchema.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "VisualMapper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString GetWidgetProperty(const FUMGWidgetNode& Node, const TCHAR* Key)
	{
		FString Value;
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, Key, Value);
		return Value;
	}

	bool HasWidgetProperty(const FUMGWidgetNode& Node, const TCHAR* Key)
	{
		return Design2UMGWidgetNodeAccess::HasWidgetPropertyPresence(Node, Key);
	}

	TSharedPtr<FDesignNode> MakeFixedNode(const EDesignNodeType Type, const float Width, const float Height)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Type = Type;
		Node->Transform.SetWidthValue(FDesignSize::Fixed(Width));
		Node->Transform.SetHeightValue(FDesignSize::Fixed(Height));
		return Node;
	}

	TSharedPtr<FDesignNode> MakeStrokeOnlyBorderNode()
	{
		TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 128.0f, 48.0f);
		Node->Visual.Stroke.Thickness.bUniform = true;
		Node->Visual.Stroke.Thickness.Uniform = 2.0f;
		Node->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#112233FF"));
		return Node;
	}

	// Mirror the current private converter branch so explicit-empty presence stays executable in tests
	// without opening a production-only seam just for automation coverage.
	bool ShouldWrapChildWithSizeBoxCurrentTruth(
		const TSharedPtr<FDesignNode>& ParentNode,
		const TSharedPtr<FDesignNode>& ChildNode,
		const FUMGWidgetNode& ChildWidget)
	{
		const bool bHasFixedSize = ChildNode->Transform.Width.IsFixed() || ChildNode->Transform.Height.IsFixed();
		const bool bHasMinSize = (ChildNode->Transform.Width.IsFit() && ChildNode->Transform.Width.Value > 0)
			|| (ChildNode->Transform.Height.IsFit() && ChildNode->Transform.Height.Value > 0);
		const bool bCanvasMixedAxis = ParentNode->LayoutData.Layout == EDesignLayout::None
			&& ((ChildNode->Transform.Width.IsFit() && ChildNode->Transform.Height.IsFixed())
				|| (ChildNode->Transform.Height.IsFit() && ChildNode->Transform.Width.IsFixed()));
		const bool bWrapOrGridMeasuredFill =
			(ParentNode->LayoutData.Layout == EDesignLayout::Wrap || ParentNode->LayoutData.Layout == EDesignLayout::Grid)
			&& ((ChildNode->Transform.Width.IsFill() && ChildNode->Transform.Width.Value > 0.0f)
				|| (ChildNode->Transform.Height.IsFill() && ChildNode->Transform.Height.Value > 0.0f));
		const bool bWidgetOwnsDesiredSize =
			ParentNode->LayoutData.Layout != EDesignLayout::None
			&& ChildWidget.Type == TEXT("Image")
			&& ChildNode->Children.Num() == 0
			&& (HasWidgetProperty(ChildWidget, UMGPropertyKeys::WidthOverride)
				|| HasWidgetProperty(ChildWidget, UMGPropertyKeys::HeightOverride))
			&& (HasWidgetProperty(ChildWidget, UMGPropertyKeys::MaterialRef)
				|| HasWidgetProperty(ChildWidget, UMGPropertyKeys::TexturePath)
				|| HasWidgetProperty(ChildWidget, UMGPropertyKeys::PathGeometry));
		return ParentNode->LayoutData.Layout == EDesignLayout::None
			? bCanvasMixedAxis
			: (bHasFixedSize || bHasMinSize || bWrapOrGridMeasuredFill) && !bWidgetOwnsDesiredSize;
	}

	FDesignFill MakeGradientFill()
	{
		FDesignFill Fill;
		Fill.Type = EDesignFillType::Gradient;
		Fill.Gradient.GradientType = EDesignGradientType::Radial;
		Fill.Gradient.Rotation = 25.0f;
		Fill.Gradient.Center = FVector2D(0.25f, 0.75f);
		FDesignGradientStop& StartStop = Fill.Gradient.Colors.AddDefaulted_GetRef();
		StartStop.Color = TEXT("#FF0000FF");
		StartStop.Position = 0.0f;
		FDesignGradientStop& EndStop = Fill.Gradient.Colors.AddDefaulted_GetRef();
		EndStop.Color = TEXT("#00FF00FF");
		EndStop.Position = 1.0f;
		return Fill;
	}

	FDesignFill MakeRadialMultiStopGradientFill()
	{
		FDesignFill Fill;
		Fill.Type = EDesignFillType::Gradient;
		Fill.Gradient.GradientType = EDesignGradientType::Radial;
		Fill.Gradient.Rotation = 37.5f;
		Fill.Gradient.Center = FVector2D(0.2f, 0.7f);
		Fill.Gradient.Size = FVector2D(0.8f, 1.35f);

		FDesignGradientStop& Stop0 = Fill.Gradient.Colors.AddDefaulted_GetRef();
		Stop0.Color = TEXT("#FF0000FF");
		Stop0.Position = 0.0f;
		FDesignGradientStop& Stop1 = Fill.Gradient.Colors.AddDefaulted_GetRef();
		Stop1.Color = TEXT("#00FF00FF");
		Stop1.Position = 0.45f;
		FDesignGradientStop& Stop2 = Fill.Gradient.Colors.AddDefaulted_GetRef();
		Stop2.Color = TEXT("#0000FFFF");
		Stop2.Position = 1.0f;
		return Fill;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderDirectTest,
	"Design2UMG.VisualMapper.WrapWithBorderDirect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperWrapWithBorderDirectTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Type = EDesignNodeType::Frame;
	Node->Transform.Opacity = 1.0f;

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Border");
	Widget.Id = TEXT("panel");
	Widget.Name = TEXT("Panel");
	Widget.Bounds.W = 240.0f;
	Widget.Bounds.H = 60.0f;
	Widget.Brush.BrushColor = FUMGValueParser::ParseColor(TEXT("#112233FF"));
	Widget.Render.RenderOpacity = 0.6f;
	Widget.Render.RenderAngle = 15.0f;
	Widget.Render.Clipping = TEXT("ClipToBounds");
	Widget.Render.RenderTransformPivot = FVector2D(0.25f, 0.75f);

	FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("Wrapped node remains Border"), Wrapped.Type, TEXT("Border"));
	TestEqual(TEXT("BrushColor moved to outer border"), GetWidgetProperty(Wrapped, TEXT("BrushColor")), TEXT("#112233FF"));
	TestEqual(TEXT("RenderOpacity moved to outer border"), GetWidgetProperty(Wrapped, TEXT("RenderOpacity")), TEXT("0.6"));
	TestEqual(TEXT("RenderAngle moved to outer border"), GetWidgetProperty(Wrapped, TEXT("RenderAngle")), TEXT("15.0"));
	const FVector2D OuterPivot = FUMGValueParser::ParseVector2D(GetWidgetProperty(Wrapped, TEXT("RenderTransformPivot")));
	TestTrue(TEXT("RenderTransformPivot moved to outer border X"),
		FMath::IsNearlyEqual(OuterPivot.X, 0.25f, 0.0001f));
	TestTrue(TEXT("RenderTransformPivot moved to outer border Y"),
		FMath::IsNearlyEqual(OuterPivot.Y, 0.75f, 0.0001f));
	TestEqual(TEXT("Clipping moved to outer border"), GetWidgetProperty(Wrapped, TEXT("Clipping")), TEXT("ClipToBounds"));
	TestEqual(TEXT("Wrapped child count"), Wrapped.Children.Num(), 1);
	if (Wrapped.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& Inner = Wrapped.Children[0];
	TestEqual(TEXT("Inner widget id suffix"), Inner.Id, TEXT("panel_inner"));
	TestEqual(TEXT("Inner widget name suffix"), Inner.Name, TEXT("Panel_inner"));
	TestFalse(TEXT("Inner widget should not keep BrushColor"), HasWidgetProperty(Inner, TEXT("BrushColor")));
	TestFalse(TEXT("Inner widget should not keep OutlineColor"), HasWidgetProperty(Inner, TEXT("OutlineColor")));
	TestFalse(TEXT("Inner widget should not keep RenderOpacity"), HasWidgetProperty(Inner, TEXT("RenderOpacity")));
	TestFalse(TEXT("Inner widget should not keep RenderAngle"), HasWidgetProperty(Inner, TEXT("RenderAngle")));
	TestFalse(TEXT("Inner widget should not keep RenderTransformPivot"), HasWidgetProperty(Inner, TEXT("RenderTransformPivot")));
	TestFalse(TEXT("Inner widget should not keep Clipping"), HasWidgetProperty(Inner, TEXT("Clipping")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderTypedTest,
	"Design2UMG.VisualMapper.WrapWithBorderTyped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperWrapWithBorderTypedTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Type = EDesignNodeType::Frame;
	Node->Transform.Opacity = 1.0f;

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Border");
	Widget.Id = TEXT("typed_panel");
	Widget.Name = TEXT("TypedPanel");
	Widget.Bounds.W = 320.0f;
	Widget.Bounds.H = 72.0f;
	Widget.Brush.BrushColor = FLinearColor(0.25f, 0.5f, 0.75f, 1.0f);
	Widget.Render.RenderOpacity = 0.55f;
	Widget.Render.RenderAngle = 12.0f;
	Widget.Render.Clipping = TEXT("ClipToBounds");
	Widget.Render.RenderTransformPivot = FVector2D(0.4f, 0.6f);

	FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("Typed wrapped node remains Border"), Wrapped.Type, TEXT("Border"));
	TestEqual(TEXT("Typed brush color moved to outer border"), GetWidgetProperty(Wrapped, TEXT("BrushColor")), TEXT("#89BCE1FF"));
	TestTrue(TEXT("Typed render opacity moved to outer border"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(Wrapped, TEXT("RenderOpacity"))), 0.55f, 0.0001f));
	TestTrue(TEXT("Typed render angle moved to outer border"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(Wrapped, TEXT("RenderAngle"))), 12.0f, 0.0001f));
	const FVector2D TypedOuterPivot = FUMGValueParser::ParseVector2D(GetWidgetProperty(Wrapped, TEXT("RenderTransformPivot")));
	TestTrue(TEXT("Typed render pivot moved to outer border X"),
		FMath::IsNearlyEqual(TypedOuterPivot.X, 0.4f, 0.0001f));
	TestTrue(TEXT("Typed render pivot moved to outer border Y"),
		FMath::IsNearlyEqual(TypedOuterPivot.Y, 0.6f, 0.0001f));
	TestEqual(TEXT("Typed clipping moved to outer border"), GetWidgetProperty(Wrapped, TEXT("Clipping")), TEXT("ClipToBounds"));
	TestEqual(TEXT("Typed wrapped child count"), Wrapped.Children.Num(), 1);
	if (Wrapped.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& Inner = Wrapped.Children[0];
	TestFalse(TEXT("Typed inner widget should not keep BrushColor"), HasWidgetProperty(Inner, TEXT("BrushColor")));
	TestFalse(TEXT("Typed inner widget should not keep OutlineColor"), HasWidgetProperty(Inner, TEXT("OutlineColor")));
	TestFalse(TEXT("Typed inner widget should not keep RenderOpacity"), HasWidgetProperty(Inner, TEXT("RenderOpacity")));
	TestFalse(TEXT("Typed inner widget should not keep RenderAngle"), HasWidgetProperty(Inner, TEXT("RenderAngle")));
	TestFalse(TEXT("Typed inner widget should not keep RenderTransformPivot"), HasWidgetProperty(Inner, TEXT("RenderTransformPivot")));
	TestFalse(TEXT("Typed inner widget should not keep Clipping"), HasWidgetProperty(Inner, TEXT("Clipping")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderElidesEmptyPanelTest,
	"Design2UMG.VisualMapper.WrapWithBorderElidesEmptyPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperWrapWithBorderElidesEmptyPanelTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 120.0f, 48.0f);
	Node->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#224466FF")));

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("CanvasPanel");
	Widget.Id = TEXT("visual_leaf");
	Widget.Name = TEXT("VisualLeaf");
	Widget.Bounds.W = 120.0f;
	Widget.Bounds.H = 48.0f;
	Widget.Brush.BrushColor = FUMGValueParser::ParseColor(TEXT("#224466FF"));

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("Visual leaf uses Border as the final primitive"), Wrapped.Type, FString(TEXT("Border")));
	TestEqual(TEXT("Visual leaf does not keep an empty panel child"), Wrapped.Children.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderPromotesSingleChildFlowTest,
	"Design2UMG.VisualMapper.WrapWithBorderPromotesSingleChildFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperWrapWithBorderPromotesSingleChildFlowTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 160.0f, 48.0f);
	Node->LayoutData.Layout = EDesignLayout::Horizontal;
	Node->LayoutData.ExplicitPadding = FDesignPadding{8.0f, 12.0f, 8.0f, 12.0f};
	Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 80.0f, 20.0f));

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("HorizontalBox");
	Widget.Id = TEXT("single_flow");
	Widget.Name = TEXT("SingleFlow");
	Widget.Brush.BrushColor = FLinearColor::White;

	FUMGWidgetNode Child;
	Child.Type = TEXT("TextBlock");
	Child.Id = TEXT("single_flow_text");
	Child.Name = TEXT("SingleFlowText");
	Child.Slot.SizeRule = EUMGSlotSizeRule::Fill;
	Widget.Children.Add(MoveTemp(Child));

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("Border remains the source visual surface"), Wrapped.Type, FString(TEXT("Border")));
	TestTrue(TEXT("Source padding stays on the Border"), Wrapped.WidgetStyle.Padding.IsSet());
	TestEqual(TEXT("Single flow child is promoted directly under Border"), Wrapped.Children.Num(), 1);
	if (Wrapped.Children.Num() != 1)
	{
		return false;
	}
	TestEqual(TEXT("Promoted child keeps its widget type"), Wrapped.Children[0].Type, FString(TEXT("TextBlock")));
	TestEqual(TEXT("Promoted child keeps its source identity"), Wrapped.Children[0].Name, FString(TEXT("SingleFlowText")));
	TestEqual(TEXT("Border content fill is expressed as alignment"),
		Wrapped.Children[0].Slot.HorizontalAlignment,
		EUMGSlotHorizontalAlignment::Fill);
	TestEqual(TEXT("Promoted child no longer carries a flow size rule"),
		Wrapped.Children[0].Slot.SizeRule,
		EUMGSlotSizeRule::Auto);

	TSharedPtr<FDesignNode> FilteredNode = MakeFixedNode(EDesignNodeType::Frame, 160.0f, 48.0f);
	FilteredNode->LayoutData.Layout = EDesignLayout::Horizontal;
	TSharedPtr<FDesignNode> DisabledChild = MakeFixedNode(EDesignNodeType::Text, 80.0f, 20.0f);
	DisabledChild->Transform.bEnabled = false;
	FilteredNode->Children.Add(DisabledChild);
	FilteredNode->Children.Add(MakeFixedNode(EDesignNodeType::Text, 80.0f, 20.0f));
	FUMGWidgetNode FilteredWidget;
	FilteredWidget.Type = TEXT("HorizontalBox");
	FilteredWidget.Name = TEXT("FilteredSingleFlow");
	FUMGWidgetNode& FilteredChild = FilteredWidget.Children.AddDefaulted_GetRef();
	FilteredChild.Type = TEXT("TextBlock");
	FilteredChild.Name = TEXT("FilteredSingleFlowText");
	const FUMGWidgetNode FilteredWrapped = VisualMapper::WrapWithBorder(MoveTemp(FilteredWidget), FilteredNode);
	TestEqual(TEXT("Skipped source child does not keep a redundant flow panel"), FilteredWrapped.Children.Num(), 1);
	if (FilteredWrapped.Children.Num() == 1)
	{
		TestEqual(TEXT("Filtered flow keeps the remaining child"), FilteredWrapped.Children[0].Name, FString(TEXT("FilteredSingleFlowText")));
	}

	TSharedPtr<FDesignNode> InnerShadowNode = MakeFixedNode(EDesignNodeType::Frame, 160.0f, 48.0f);
	InnerShadowNode->LayoutData.Layout = EDesignLayout::Horizontal;
	FDesignEffect& InnerShadowEffect = InnerShadowNode->Visual.Effects.AddDefaulted_GetRef();
	InnerShadowEffect.Type = EDesignEffectType::Shadow;
	InnerShadowEffect.Shadow.bInner = true;
	FUMGWidgetNode InnerShadowWidget;
	InnerShadowWidget.Type = TEXT("HorizontalBox");
	InnerShadowWidget.Name = TEXT("InnerShadowFlow");
	FUMGWidgetNode& InnerShadowChild = InnerShadowWidget.Children.AddDefaulted_GetRef();
	InnerShadowChild.Type = TEXT("TextBlock");
	InnerShadowChild.Name = TEXT("InnerShadowFlowText");
	const FUMGWidgetNode InnerShadowWrapped = VisualMapper::WrapWithBorder(MoveTemp(InnerShadowWidget), InnerShadowNode);
	TestEqual(TEXT("Inner-shadow flow keeps its layout panel"), InnerShadowWrapped.Children.Num(), 1);
	if (InnerShadowWrapped.Children.Num() == 1)
	{
		TestEqual(TEXT("Inner-shadow flow keeps the panel type"), InnerShadowWrapped.Children[0].Type, FString(TEXT("HorizontalBox")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderSingleFlowSemanticsMatrixTest,
	"Design2UMG.VisualMapper.WrapWithBorderSingleFlowSemanticsMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperWrapWithBorderSingleFlowSemanticsMatrixTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		EDesignLayout Layout;
		EUMGSlotSizeRule SizeRule;
		const TCHAR* WidgetType;
		EDesignNodeType ChildNodeType;
	};

	const TArray<FCase> Cases = {
		{EDesignLayout::Horizontal, EUMGSlotSizeRule::Auto, TEXT("TextBlock"), EDesignNodeType::Text},
		{EDesignLayout::Horizontal, EUMGSlotSizeRule::Fill, TEXT("Image"), EDesignNodeType::Rectangle},
		{EDesignLayout::Vertical, EUMGSlotSizeRule::Auto, TEXT("TextBlock"), EDesignNodeType::Text},
		{EDesignLayout::Vertical, EUMGSlotSizeRule::Fill, TEXT("Image"), EDesignNodeType::Rectangle},
	};

	for (const FCase& Case : Cases)
	{
		TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 160.0f, 48.0f);
		Node->LayoutData.Layout = Case.Layout;
		Node->LayoutData.ExplicitPadding = FDesignPadding{6.0f, 10.0f, 6.0f, 10.0f};
		Node->Children.Add(MakeFixedNode(Case.ChildNodeType, 80.0f, 20.0f));

		FUMGWidgetNode Widget;
		Widget.Type = Case.Layout == EDesignLayout::Horizontal ? TEXT("HorizontalBox") : TEXT("VerticalBox");
		Widget.Id = TEXT("matrix_flow");
		Widget.Name = TEXT("MatrixFlow");
		Widget.Brush.BrushColor = FLinearColor::White;

		FUMGWidgetNode Child;
		Child.Type = Case.WidgetType;
		Child.Name = TEXT("MatrixChild");
		Child.Slot.SizeRule = Case.SizeRule;
		Widget.Children.Add(MoveTemp(Child));

		const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
		TestEqual(TEXT("Matrix case keeps Border as the source surface"), Wrapped.Type, FString(TEXT("Border")));
		TestEqual(TEXT("Matrix case promotes exactly one child"), Wrapped.Children.Num(), 1);
		if (Wrapped.Children.Num() != 1)
		{
			continue;
		}

		TestEqual(TEXT("Matrix case keeps child widget type"), Wrapped.Children[0].Type, FString(Case.WidgetType));
		TestEqual(TEXT("Matrix case keeps child identity"), Wrapped.Children[0].Name, FString(TEXT("MatrixChild")));
		TestTrue(TEXT("Matrix case keeps Border padding"), Wrapped.WidgetStyle.Padding.IsSet());
		TestEqual(TEXT("Matrix case consumes flow size rule"), Wrapped.Children[0].Slot.SizeRule, EUMGSlotSizeRule::Auto);
		if (Case.Layout == EDesignLayout::Horizontal)
		{
			TestEqual(
				TEXT("Horizontal flow maps the main-axis rule to HAlign"),
				Wrapped.Children[0].Slot.HorizontalAlignment,
				Case.SizeRule == EUMGSlotSizeRule::Fill
					? EUMGSlotHorizontalAlignment::Fill
					: EUMGSlotHorizontalAlignment::Left);
		}
		else
		{
			TestEqual(
				TEXT("Vertical flow maps the main-axis rule to VAlign"),
				Wrapped.Children[0].Slot.VerticalAlignment,
				Case.SizeRule == EUMGSlotSizeRule::Fill
					? EUMGSlotVerticalAlignment::Fill
					: EUMGSlotVerticalAlignment::Top);
		}
	}

	// FitContent at the absolute-parent boundary suppresses spacer creation in
	// LayoutMapper; the Border slot still carries the authored justify result.
	{
		TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 0.0f, 48.0f);
		Node->Transform.Width = FDesignSize::Fit();
		Node->LayoutData.Layout = EDesignLayout::Horizontal;
		Node->LayoutData.JustifyContent = EDesignJustifyContent::Center;
		Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 80.0f, 20.0f));

		FUMGWidgetNode Widget;
		Widget.Type = TEXT("HorizontalBox");
		Widget.Name = TEXT("FitJustifyFlow");
		Widget.Brush.BrushColor = FLinearColor::White;
		FUMGWidgetNode& Content = Widget.Children.AddDefaulted_GetRef();
		Content.Type = TEXT("TextBlock");
		Content.Name = TEXT("FitJustifyChild");
		Content.Slot.SizeRule = EUMGSlotSizeRule::Auto;
		Content.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Top;

		const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
		TestEqual(TEXT("FitContent justify promotes without synthetic spacers"), Wrapped.Children.Num(), 1);
		if (Wrapped.Children.Num() == 1)
		{
			TestEqual(TEXT("FitContent justify keeps content identity"), Wrapped.Children[0].Name, FString(TEXT("FitJustifyChild")));
			TestEqual(TEXT("FitContent justify maps to Border alignment"), Wrapped.Children[0].Slot.HorizontalAlignment, EUMGSlotHorizontalAlignment::Center);
		}
	}
	{
		TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 64.0f, 0.0f);
		Node->Transform.Height = FDesignSize::Fit();
		Node->LayoutData.Layout = EDesignLayout::Vertical;
		Node->LayoutData.JustifyContent = EDesignJustifyContent::Center;
		Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 40.0f, 20.0f));

		FUMGWidgetNode Widget;
		Widget.Type = TEXT("VerticalBox");
		Widget.Name = TEXT("FitVerticalJustifyFlow");
		Widget.Brush.BrushColor = FLinearColor::White;
		FUMGWidgetNode& Content = Widget.Children.AddDefaulted_GetRef();
		Content.Type = TEXT("TextBlock");
		Content.Name = TEXT("FitVerticalJustifyChild");
		Content.Slot.SizeRule = EUMGSlotSizeRule::Auto;
		Content.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Left;

		const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
		TestEqual(TEXT("Vertical FitContent justify promotes without synthetic spacers"), Wrapped.Children.Num(), 1);
		if (Wrapped.Children.Num() == 1)
		{
			TestEqual(TEXT("Vertical FitContent justify keeps content identity"), Wrapped.Children[0].Name, FString(TEXT("FitVerticalJustifyChild")));
			TestEqual(TEXT("Vertical FitContent justify maps to Border alignment"), Wrapped.Children[0].Slot.VerticalAlignment, EUMGSlotVerticalAlignment::Center);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderSingleFlowBoundaryTest,
	"Design2UMG.VisualMapper.WrapWithBorderSingleFlowBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperWrapWithBorderSingleFlowBoundaryTest::RunTest(const FString& Parameters)
{
	auto BuildWrapped = [](const TSharedPtr<FDesignNode>& Node, const bool bPanelPadding) -> FUMGWidgetNode
	{
		FUMGWidgetNode Widget;
		Widget.Type = TEXT("HorizontalBox");
		Widget.Id = TEXT("boundary_flow");
		Widget.Name = TEXT("BoundaryFlow");
		Widget.Brush.BrushColor = FLinearColor::White;
		if (bPanelPadding)
		{
			Widget.WidgetStyle.InnerSlotPadding = FVector2D(4.0f, 4.0f);
		}
		FUMGWidgetNode Child;
		Child.Type = TEXT("TextBlock");
		Child.Name = TEXT("BoundaryChild");
		Widget.Children.Add(MoveTemp(Child));
		return VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	};

	auto AssertKeptFlowPanel = [this](const FUMGWidgetNode& Wrapped, const TCHAR* Label)
	{
		TestEqual(Label, Wrapped.Children.Num(), 1);
		if (Wrapped.Children.Num() == 1)
		{
			TestEqual(TEXT("Boundary case keeps the flow panel type"), Wrapped.Children[0].Type, FString(TEXT("HorizontalBox")));
			TestEqual(TEXT("Boundary case marks the retained panel as inner"), Wrapped.Children[0].Name, FString(TEXT("BoundaryFlow_inner")));
		}
	};

	{
		TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 160.0f, 48.0f);
		Node->LayoutData.Layout = EDesignLayout::None;
		Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 80.0f, 20.0f));
		AssertKeptFlowPanel(BuildWrapped(Node, false), TEXT("None-layout case does not promote"));
	}
	{
		TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 160.0f, 48.0f);
		Node->LayoutData.Layout = EDesignLayout::Horizontal;
		Node->LayoutData.JustifyContent = EDesignJustifyContent::Center;
		Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 80.0f, 20.0f));
		AssertKeptFlowPanel(BuildWrapped(Node, false), TEXT("Justify case does not promote"));
	}
	{
		TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 160.0f, 48.0f);
		Node->LayoutData.Layout = EDesignLayout::Horizontal;
		TSharedPtr<FDesignNode> Child = MakeFixedNode(EDesignNodeType::Text, 80.0f, 20.0f);
		Child->Transform.SetPosition(12.0f, 4.0f);
		Node->Children.Add(Child);
		AssertKeptFlowPanel(BuildWrapped(Node, false), TEXT("Positioned child does not promote"));
	}
	{
		TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 160.0f, 48.0f);
		Node->LayoutData.Layout = EDesignLayout::Horizontal;
		Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 80.0f, 20.0f));
		AssertKeptFlowPanel(BuildWrapped(Node, true), TEXT("Panel-property case does not promote"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderSingleChildJustifyMatrixTest,
	"Design2UMG.VisualMapper.WrapWithBorderSingleChildJustifyMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperWrapWithBorderSingleChildJustifyMatrixTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		EDesignLayout Layout;
		EDesignJustifyContent Justify;
		int32 SpacerCount;
		EUMGSlotHorizontalAlignment ExpectedH;
		EUMGSlotVerticalAlignment ExpectedV;
	};

	const TArray<FCase> Cases = {
		{EDesignLayout::Horizontal, EDesignJustifyContent::Center, 2, EUMGSlotHorizontalAlignment::Center, EUMGSlotVerticalAlignment::Top},
		{EDesignLayout::Horizontal, EDesignJustifyContent::End, 1, EUMGSlotHorizontalAlignment::Right, EUMGSlotVerticalAlignment::Top},
		{EDesignLayout::Horizontal, EDesignJustifyContent::SpaceAround, 2, EUMGSlotHorizontalAlignment::Center, EUMGSlotVerticalAlignment::Top},
		{EDesignLayout::Horizontal, EDesignJustifyContent::SpaceBetween, 0, EUMGSlotHorizontalAlignment::Left, EUMGSlotVerticalAlignment::Top},
		{EDesignLayout::Vertical, EDesignJustifyContent::Center, 2, EUMGSlotHorizontalAlignment::Fill, EUMGSlotVerticalAlignment::Center},
		{EDesignLayout::Vertical, EDesignJustifyContent::End, 1, EUMGSlotHorizontalAlignment::Fill, EUMGSlotVerticalAlignment::Bottom},
		{EDesignLayout::Vertical, EDesignJustifyContent::SpaceAround, 2, EUMGSlotHorizontalAlignment::Fill, EUMGSlotVerticalAlignment::Center},
		{EDesignLayout::Vertical, EDesignJustifyContent::SpaceBetween, 0, EUMGSlotHorizontalAlignment::Fill, EUMGSlotVerticalAlignment::Top},
	};

	for (const FCase& Case : Cases)
	{
		TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 160.0f, 48.0f);
		Node->LayoutData.Layout = Case.Layout;
		Node->LayoutData.JustifyContent = Case.Justify;
		Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 80.0f, 20.0f));

		FUMGWidgetNode Widget;
		Widget.Type = Case.Layout == EDesignLayout::Horizontal ? TEXT("HorizontalBox") : TEXT("VerticalBox");
		Widget.Name = TEXT("JustifyFlow");
		Widget.Brush.BrushColor = FLinearColor::White;
		for (int32 Index = 0; Index < Case.SpacerCount; ++Index)
		{
			FUMGWidgetNode Spacer;
			Spacer.Type = TEXT("Spacer");
			Spacer.Name = FString::Printf(TEXT("Spacer_Widget_%d"), Index);
			Widget.Children.Add(MoveTemp(Spacer));
		}
		FUMGWidgetNode Content;
		Content.Type = TEXT("TextBlock");
		Content.Name = TEXT("JustifyChild");
		if (Case.Layout == EDesignLayout::Horizontal)
		{
			Content.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Top;
		}
		else
		{
			Content.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		}
		Widget.Children.Insert(MoveTemp(Content), Case.SpacerCount > 0 ? 1 : 0);

		const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
		TestEqual(TEXT("Justify case promotes the content child"), Wrapped.Children.Num(), 1);
		if (Wrapped.Children.Num() != 1)
		{
			continue;
		}
		TestEqual(TEXT("Justify case keeps content identity"), Wrapped.Children[0].Name, FString(TEXT("JustifyChild")));
		TestEqual(TEXT("Justify case clears flow size rule"), Wrapped.Children[0].Slot.SizeRule, EUMGSlotSizeRule::Auto);
		TestEqual(TEXT("Justify case maps horizontal alignment"), Wrapped.Children[0].Slot.HorizontalAlignment, Case.ExpectedH);
		TestEqual(TEXT("Justify case maps vertical alignment"), Wrapped.Children[0].Slot.VerticalAlignment, Case.ExpectedV);
	}

	// LayoutMapper omits synthetic justify spacers when the only child already fills
	// the main axis. The promotion rule must preserve that same behavior.
	{
		TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 160.0f, 48.0f);
		Node->LayoutData.Layout = EDesignLayout::Horizontal;
		Node->LayoutData.JustifyContent = EDesignJustifyContent::Center;
		Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 80.0f, 20.0f));

		FUMGWidgetNode Widget;
		Widget.Type = TEXT("HorizontalBox");
		Widget.Name = TEXT("FillJustifyFlow");
		Widget.Brush.BrushColor = FLinearColor::White;
		FUMGWidgetNode& Content = Widget.Children.AddDefaulted_GetRef();
		Content.Type = TEXT("TextBlock");
		Content.Name = TEXT("FillJustifyChild");
		Content.Slot.SizeRule = EUMGSlotSizeRule::Fill;
		Content.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Top;

		const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
		TestEqual(TEXT("Fill child with justify still promotes"), Wrapped.Children.Num(), 1);
		if (Wrapped.Children.Num() == 1)
		{
			TestEqual(TEXT("Fill justify keeps content identity"), Wrapped.Children[0].Name, FString(TEXT("FillJustifyChild")));
			TestEqual(TEXT("Fill justify keeps fill alignment"), Wrapped.Children[0].Slot.HorizontalAlignment, EUMGSlotHorizontalAlignment::Fill);
			TestEqual(TEXT("Fill justify clears transferred size rule"), Wrapped.Children[0].Slot.SizeRule, EUMGSlotSizeRule::Auto);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderKeepsMultiChildFlowTest,
	"Design2UMG.VisualMapper.WrapWithBorderKeepsMultiChildFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperWrapWithBorderKeepsMultiChildFlowTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 160.0f, 48.0f);
	Node->LayoutData.Layout = EDesignLayout::Horizontal;
	Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 60.0f, 20.0f));
	Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 60.0f, 20.0f));

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("HorizontalBox");
	Widget.Id = TEXT("multi_flow");
	Widget.Name = TEXT("MultiFlow");
	Widget.Brush.BrushColor = FLinearColor::White;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FUMGWidgetNode& Child = Widget.Children.AddDefaulted_GetRef();
		Child.Type = TEXT("TextBlock");
		Child.Name = FString::Printf(TEXT("MultiFlowText%d"), Index);
	}

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("Multi-child flow panel remains the content widget"), Wrapped.Children.Num(), 1);
	if (Wrapped.Children.Num() != 1)
	{
		return false;
	}
	TestEqual(TEXT("Multi-child flow panel keeps its layout type"), Wrapped.Children[0].Type, FString(TEXT("HorizontalBox")));
	TestEqual(TEXT("Multi-child content receives the inner identity suffix"), Wrapped.Children[0].Name, FString(TEXT("MultiFlow_inner")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderKeepsEmptyScrollBoxTest,
	"Design2UMG.VisualMapper.WrapWithBorderKeepsEmptyScrollBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperWrapWithBorderKeepsEmptyScrollBoxTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 120.0f, 48.0f);
	Node->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#224466FF")));

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("ScrollBox");
	Widget.Id = TEXT("scroll_leaf");
	Widget.Name = TEXT("ScrollLeaf");
	Widget.Bounds.W = 120.0f;
	Widget.Bounds.H = 48.0f;
	Widget.Brush.BrushColor = FUMGValueParser::ParseColor(TEXT("#224466FF"));
	Widget.ScrollBox.Orientation = TEXT("Vertical");
	Widget.ScrollBox.ScrollBarVisibility = TEXT("Hidden");

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("ScrollBox carrier remains under the visual Border"), Wrapped.Children.Num(), 1);
	if (Wrapped.Children.Num() != 1)
	{
		return false;
	}
	TestEqual(TEXT("Scrollable leaf keeps its ScrollBox type"), Wrapped.Children[0].Type, FString(TEXT("ScrollBox")));
	TestTrue(TEXT("Scrollable leaf keeps its orientation"), Wrapped.Children[0].ScrollBox.Orientation.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderExplicitSideStrokeTest,
	"Design2UMG.VisualMapper.WrapWithBorderExplicitSideStroke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FVisualMapperWrapWithBorderExplicitSideStrokeTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 128.0f, 48.0f);
	Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 64.0f, 20.0f));
	Node->Visual.Stroke.Thickness.bUniform = false;
	Node->Visual.Stroke.Thickness.Top = 2.0f;
	Node->Visual.Stroke.Thickness.Right = 4.0f;
	Node->Visual.Stroke.Thickness.Bottom = 6.0f;
	Node->Visual.Stroke.Thickness.Left = 8.0f;
	Node->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#112233FF"));

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("HorizontalBox");
	Widget.Id = TEXT("stroke_panel");
	Widget.Name = TEXT("StrokePanel");
	Widget.Bounds.W = 128.0f;
	Widget.Bounds.H = 48.0f;
	Widget.WidgetStyle.WidthOverride = 128.0f;

	VisualMapper::ApplyStroke(Widget, Node->Visual.Stroke);
	FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);

	TestEqual(TEXT("Explicit side stroke wraps with overlay"), Wrapped.Type, TEXT("Overlay"));
	TestEqual(TEXT("Promoted content replaces the surface beside four stroke lines"), Wrapped.Children.Num(), 5);
	if (Wrapped.Children.Num() != 5)
	{
		return false;
	}

	const FUMGWidgetNode& Content = Wrapped.Children[0];
	TestEqual(TEXT("Layout content is promoted directly"), Content.Type, TEXT("HorizontalBox"));
	TestEqual(TEXT("Promoted content keeps its inner identity"), Content.Name, TEXT("StrokePanel_inner"));
	TestEqual(TEXT("Promoted content keeps its inner id"), Content.Id, TEXT("stroke_panel_inner"));
	TestTrue(TEXT("Promoted content regains its transferred width"),
		Content.WidgetStyle.WidthOverride.IsSet()
		&& FMath::IsNearlyEqual(Content.WidgetStyle.WidthOverride.GetValue(), 128.0f));

	const FUMGWidgetNode& Top = Wrapped.Children[1];
	const FUMGWidgetNode& Right = Wrapped.Children[2];
	const FUMGWidgetNode& Bottom = Wrapped.Children[3];
	const FUMGWidgetNode& Left = Wrapped.Children[4];

	TestEqual(TEXT("Top stroke name"), Top.Name, TEXT("StrokePanel_stroke_top"));
	TestEqual(TEXT("Right stroke name"), Right.Name, TEXT("StrokePanel_stroke_right"));
	TestEqual(TEXT("Bottom stroke name"), Bottom.Name, TEXT("StrokePanel_stroke_bottom"));
	TestEqual(TEXT("Left stroke name"), Left.Name, TEXT("StrokePanel_stroke_left"));

	TestTrue(TEXT("Top stroke height tracks thickness"),
		Top.WidgetStyle.HeightOverride.IsSet() && FMath::IsNearlyEqual(Top.WidgetStyle.HeightOverride.GetValue(), 2.0f));
	TestTrue(TEXT("Right stroke width tracks thickness"),
		Right.WidgetStyle.WidthOverride.IsSet() && FMath::IsNearlyEqual(Right.WidgetStyle.WidthOverride.GetValue(), 4.0f));
	TestTrue(TEXT("Bottom stroke height tracks thickness"),
		Bottom.WidgetStyle.HeightOverride.IsSet() && FMath::IsNearlyEqual(Bottom.WidgetStyle.HeightOverride.GetValue(), 6.0f));
	TestTrue(TEXT("Left stroke width tracks thickness"),
		Left.WidgetStyle.WidthOverride.IsSet() && FMath::IsNearlyEqual(Left.WidgetStyle.WidthOverride.GetValue(), 8.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetNodeAccessExplicitEmptyStringPresenceTest,
	"Design2UMG.WidgetNodeAccess.ExplicitEmptyStringPresence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetNodeAccessExplicitEmptyStringPresenceTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("EmptyStringPresence");
	Node.Brush.MaterialRef = TEXT("");
	Node.Brush.Image.TexturePath = TEXT("");

	FString MaterialRef;
	TestTrue(TEXT("Explicit empty material ref is queryable"),
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialRef, MaterialRef));
	TestTrue(TEXT("Explicit empty material ref stays empty"), MaterialRef.IsEmpty());
	TestTrue(TEXT("Presence helper treats explicit empty material ref as present"), HasWidgetProperty(Node, UMGPropertyKeys::MaterialRef));

	FString TexturePath;
	TestTrue(TEXT("Explicit empty image texture path is queryable"),
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::TexturePath, TexturePath));
	TestTrue(TEXT("Explicit empty image texture path stays empty"), TexturePath.IsEmpty());
	TestTrue(TEXT("Presence helper treats explicit empty image texture path as present"), HasWidgetProperty(Node, UMGPropertyKeys::TexturePath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetNodeAccessExplicitNoDrawTypePresenceTest,
	"Design2UMG.WidgetNodeAccess.ExplicitNoDrawTypePresence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetNodeAccessExplicitNoDrawTypePresenceTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("NoDrawTypePresence");
	Node.Brush.DrawAs = ESlateBrushDrawType::NoDrawType;

	TestTrue(TEXT("Presence helper treats explicit NoDrawType as present"), HasWidgetProperty(Node, UMGPropertyKeys::DrawAs));
	TestEqual(TEXT("Explicit NoDrawType stays queryable"), GetWidgetProperty(Node, UMGPropertyKeys::DrawAs), FString(TEXT("NoDrawType")));

	FString DrawAs;
	TestTrue(TEXT("Typed draw-as helper keeps explicit NoDrawType"),
		Design2UMGWidgetNodeAccess::TryGetTypedWidgetString(Node, UMGPropertyKeys::DrawAs, DrawAs));
	TestEqual(TEXT("Typed draw-as helper keeps serialized value"), DrawAs, FString(TEXT("NoDrawType")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderRichTextMarkupContractTest,
	"Design2UMG.TextWidgetBuilder.RichTextMarkupContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderRichTextMarkupContractTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Text, 200.0f, 40.0f);
	Node->Text.Content = TEXT("unused");
	Node->Text.FontSize = 16.0f;
	Node->Text.FontFamily = TEXT("Roboto");

	FDesignTextRun& LinkRun = Node->Text.TextRuns.AddDefaulted_GetRef();
	LinkRun.Content = TEXT("A&B <C>");
	LinkRun.Href = TEXT("https://example.test/?q=\"x\"&v='y'");
	LinkRun.bUnderline = true;

	FDesignTextRun& PlainRun = Node->Text.TextRuns.AddDefaulted_GetRef();
	PlainRun.Content = TEXT(" tail");
	PlainRun.FontSize = 18.0f;

	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestEqual(TEXT("Rich text node type"), Widget.Type, FString(TEXT("RichTextBlock")));
	TestEqual(TEXT("Rich text markup keeps escaping contract"),
		Widget.Text.Text.Get(FString()),
		FString(TEXT("<d2u_run_0 href=\"https://example.test/?q=&quot;x&quot;&amp;v=&apos;y&apos;\">A&amp;B &lt;C&gt;</><d2u_run_1> tail</>")));
	TestEqual(TEXT("Single href remains tooltip text"), Widget.WidgetStyle.ToolTipText.Get(FString()), LinkRun.Href);
	TestTrue(TEXT("Style rows still serialize"), Widget.Text.RichTextStyleRowsJson.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderSourceFontSizeConversionContractTest,
	"Design2UMG.TextWidgetBuilder.SourceFontSizeConversionContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderSourceFontSizeConversionContractTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("source_conversion_contract");
	Node->Name = TEXT("Source Conversion Contract");
	Node->Type = EDesignNodeType::Text;
	Node->Text.Content = TEXT("Compact label");
	Node->Text.FontFamily = TEXT("Inter");
	Node->Text.FontSize = 14.0f;
	Node->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	Node->Transform.Width = FDesignSize::Fit(0.0f);
	Node->Transform.Height = FDesignSize::Fit(16.0f);
	Node->Transform.bHasWidth = false;
	Node->Transform.bSourceHasWidth = true;
	Node->Transform.bHasHeight = true;
	Node->Transform.bSourceHasHeight = true;

	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	const float ExpectedFontSize = Node->Text.FontSize * 72.0f / static_cast<float>(FontConstants::RenderDPI);
	TestTrue(TEXT("Builder uses the source px-to-point conversion"),
		FMath::IsNearlyEqual(Widget.TextStyle.FontSize, ExpectedFontSize, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderSourceFontSizeFractionalContractTest,
	"Design2UMG.TextWidgetBuilder.SourceFontSizeFractionalContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderSourceFontSizeFractionalContractTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("source_fractional_contract");
	Node->Name = TEXT("Source Fractional Contract");
	Node->Type = EDesignNodeType::Text;
	Node->Text.Content = TEXT("Fractional label");
	Node->Text.FontFamily = TEXT("Inter");
	Node->Text.FontSize = 0.5f;
	Node->Text.TextGrowth = EDesignTextGrowth::Auto;

	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	const float ExpectedFontSize = Node->Text.FontSize * 72.0f / static_cast<float>(FontConstants::RenderDPI);
	TestTrue(TEXT("Builder preserves the exact direct source px-to-point conversion"),
		FMath::IsNearlyEqual(Widget.TextStyle.FontSize, ExpectedFontSize, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementRuntimeProbeSubpointFontInfoContractTest,
	"Design2UMG.TextMeasurement.RuntimeProbeSubpointFontInfoContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementRuntimeProbeSubpointFontInfoContractTest::RunTest(const FString& Parameters)
{
	TextMeasurementRuntimeProbe::FTextMeasureRequest Request;
	Request.FontSize = 0.375f;
	const FSlateFontInfo FontInfo = TextMeasurementRuntimeProbe::BuildFontInfo(Request);
	TestTrue(TEXT("Runtime probe font info preserves 0.375 point sizes"),
		FMath::IsNearlyEqual(FontInfo.Size, 0.375f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderRichTextRunFontSizeConversionContractTest,
	"Design2UMG.TextWidgetBuilder.RichTextRunFontSizeConversionContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderRichTextRunFontSizeConversionContractTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("rich_text_run_source_conversion");
	Node->Name = TEXT("Rich Text Run Source Conversion");
	Node->Type = EDesignNodeType::Text;
	Node->Text.Content = TEXT("unused");
	Node->Text.FontFamily = TEXT("Inter");
	Node->Text.FontSize = 0.0f;

	FDesignTextRun& ValidRun = Node->Text.TextRuns.AddDefaulted_GetRef();
	ValidRun.Content = TEXT("valid");
	ValidRun.FontSize = 16.0f;
	FDesignTextRun& InvalidRun = Node->Text.TextRuns.AddDefaulted_GetRef();
	InvalidRun.Content = TEXT("invalid");
	InvalidRun.FontSize = -1.0f;

	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestEqual(TEXT("Explicit run size produces rich text"), Widget.Type, FString(TEXT("RichTextBlock")));
	if (!TestTrue(TEXT("Rich text style rows serialize"), Widget.Text.RichTextStyleRowsJson.IsSet()))
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Widget.Text.RichTextStyleRowsJson.GetValue());
	if (!TestTrue(TEXT("Rich text style rows parse"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* ValidRow = nullptr;
	const TSharedPtr<FJsonObject>* InvalidRow = nullptr;
	if (!TestTrue(TEXT("Valid run row exists"), RootObject->TryGetObjectField(TEXT("d2u_run_0"), ValidRow) && ValidRow)
		|| !TestTrue(TEXT("Invalid run row exists"), RootObject->TryGetObjectField(TEXT("d2u_run_1"), InvalidRow) && InvalidRow))
	{
		return false;
	}

	double ValidFontSize = 0.0;
	TestTrue(TEXT("Valid run has an applied point size"), (*ValidRow)->TryGetNumberField(UMGPropertyKeys::FontSize, ValidFontSize));
	TestTrue(TEXT("16 source pixels convert to 12 points independently of the invalid parent size"),
		FMath::IsNearlyEqual(static_cast<float>(ValidFontSize), 12.0f, 0.001f));
	TestFalse(TEXT("Invalid explicit run size does not inherit or bypass conversion"),
		(*InvalidRow)->HasField(UMGPropertyKeys::FontSize));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperBoxShadowTextureExtentClampTest,
	"Design2UMG.VisualMapper.BoxShadowTextureExtentClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperBoxShadowTextureExtentClampTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Rectangle, 10.0f, 20.0f);

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Image");
	Widget.Id = TEXT("shadow_source");
	Widget.Name = TEXT("ShadowSource");

	FUMGBoxShadowLayerData NormalShadow;
	NormalShadow.Color = FLinearColor::Black;
	NormalShadow.Blur = 2.0f;
	NormalShadow.Spread = 3.0f;
	FUMGWidgetNode NormalWrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, TArray<FUMGBoxShadowLayerData>{ NormalShadow });
	TestEqual(TEXT("Normal shadow wrapper has shadow and content"), NormalWrapped.Children.Num(), 2);
	if (NormalWrapped.Children.Num() != 2)
	{
		return false;
	}
	TestEqual(TEXT("Unclipped shadow stays in the overlay layout contract"), NormalWrapped.Children[0].Type, FString(TEXT("Image")));
	TestEqual(TEXT("Normal finite width keeps the rounded result"), NormalWrapped.Children[0].WidgetStyle.WidthOverride.Get(0), 20.0f);
	TestEqual(TEXT("Normal finite height keeps the rounded result"), NormalWrapped.Children[0].WidgetStyle.HeightOverride.Get(0), 30.0f);

	FUMGWidgetNode HugeWidget;
	HugeWidget.Type = TEXT("Image");
	HugeWidget.Id = TEXT("huge_shadow_source");
	HugeWidget.Name = TEXT("HugeShadowSource");

	FUMGBoxShadowLayerData HugeShadow;
	HugeShadow.Color = FLinearColor::Black;
	HugeShadow.Blur = 1.0e20f;
	constexpr float SafeRoundToIntMax = static_cast<float>(TNumericLimits<int32>::Max() - 64);
	FUMGWidgetNode HugeWrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(HugeWidget), Node, TArray<FUMGBoxShadowLayerData>{ HugeShadow });
	TestEqual(TEXT("Overflowing shadow clamps width to safe RoundToInt max"), HugeWrapped.Children[0].WidgetStyle.WidthOverride.Get(0), SafeRoundToIntMax);
	TestEqual(TEXT("Overflowing shadow clamps height to safe RoundToInt max"), HugeWrapped.Children[0].WidgetStyle.HeightOverride.Get(0), SafeRoundToIntMax);

	FUMGWidgetNode InvalidWidget;
	InvalidWidget.Type = TEXT("Image");
	InvalidWidget.Id = TEXT("invalid_shadow_source");
	InvalidWidget.Name = TEXT("InvalidShadowSource");

	FUMGBoxShadowLayerData InvalidShadow;
	InvalidShadow.Color = FLinearColor::Black;
	InvalidShadow.Blur = FMath::Sqrt(-1.0f);
	FUMGWidgetNode InvalidWrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(InvalidWidget), Node, TArray<FUMGBoxShadowLayerData>{ InvalidShadow });
	TestEqual(TEXT("Non-finite shadow width falls back to one pixel"), InvalidWrapped.Children[0].WidgetStyle.WidthOverride.Get(0), 1.0f);
	TestEqual(TEXT("Non-finite shadow height falls back to one pixel"), InvalidWrapped.Children[0].WidgetStyle.HeightOverride.Get(0), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperBoxShadowRenderOwnershipTest,
	"Design2UMG.VisualMapper.BoxShadowRenderOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperBoxShadowRenderOwnershipTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Rectangle, 100.0f, 80.0f);
	Node->LayoutData.SetClipValue(true);
	Node->Visual.CornerRadius.bUniform = false;
	Node->Visual.CornerRadius.TopLeft = 8.0f;
	Node->Visual.CornerRadius.TopRight = 12.0f;
	Node->Visual.CornerRadius.BottomRight = 16.0f;
	Node->Visual.CornerRadius.BottomLeft = 20.0f;

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Image");
	Widget.Id = TEXT("shadow_geometry_source");
	Widget.Name = TEXT("Shadow Geometry Source");
	Widget.Render.Clipping = TEXT("ClipToBounds");

	FUMGBoxShadowLayerData ShadowLayer;
	ShadowLayer.Color = FLinearColor::Black;
	ShadowLayer.Offset = FVector2D(4.0f, -5.0f);
	ShadowLayer.Blur = 32.0f;
	ShadowLayer.Spread = -3.0f;
	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, TArray<FUMGBoxShadowLayerData>{ ShadowLayer });

	TestEqual(TEXT("Box shadow keeps the content overlay wrapper"), Wrapped.Type, FString(TEXT("Overlay")));
	TestEqual(TEXT("Box shadow overlay has shadow and content"), Wrapped.Children.Num(), 2);
	if (Wrapped.Children.Num() != 2)
	{
		return false;
	}

	const FUMGWidgetNode& Shadow = Wrapped.Children[0];
	TestEqual(TEXT("Box shadow is the first overlay child"), Shadow.Type, FString(TEXT("Image")));
	TestTrue(TEXT("Shadow image retains the source brush colour"),
		Shadow.Brush.BrushColor.IsSet()
		&& Shadow.Brush.BrushColor.GetValue().Equals(FLinearColor::Black));
	TestTrue(TEXT("Shadow remains a fill child and keeps its render offset"),
		Shadow.Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill
		&& Shadow.Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill
		&& Shadow.Render.RenderTranslation.IsSet()
		&& Shadow.Render.RenderTranslation.GetValue().Equals(FVector2D(-25.0f, -34.0f), 0.001f));
	TestEqual(TEXT("Source clipping moves to the layout wrapper"),
		Wrapped.Render.Clipping.Get(FString()), FString(TEXT("ClipToBounds")));
	TestTrue(TEXT("Non-uniform corners normalize against the shadow texture half-min"),
		Shadow.Brush.MaterialParams.Get(FString()).Contains(TEXT("\"CornerRadii\":[0.580,0.638,0.696,0.754]")));

	const FUMGWidgetNode& Content = Wrapped.Children[1];
	TestTrue(TEXT("Content remains a fill child of the layout wrapper"),
		FMath::IsNearlyEqual(Content.Bounds.X, 0.0f, 0.001f)
		&& FMath::IsNearlyEqual(Content.Bounds.Y, 0.0f, 0.001f)
		&& FMath::IsNearlyEqual(Content.Bounds.W, 0.0f, 0.001f)
		&& FMath::IsNearlyEqual(Content.Bounds.H, 0.0f, 0.001f)
		&& Content.Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill
		&& Content.Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill);
	TestFalse(TEXT("Content does not retain wrapper clipping"), Content.Render.Clipping.IsSet());

	TSharedPtr<FDesignNode> UniformNode = MakeFixedNode(EDesignNodeType::Rectangle, 100.0f, 80.0f);
	UniformNode->Visual.CornerRadius.bUniform = true;
	UniformNode->Visual.CornerRadius.Uniform = 10.0f;
	FUMGWidgetNode UniformWidget;
	UniformWidget.Type = TEXT("Image");
	UniformWidget.Id = TEXT("uniform_shadow_geometry_source");
	UniformWidget.Name = TEXT("Uniform Shadow Geometry Source");
	const FUMGWidgetNode UniformWrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(UniformWidget), UniformNode, TArray<FUMGBoxShadowLayerData>{ ShadowLayer });
	TestTrue(TEXT("Uniform corner radius normalizes against the shadow texture half-min"),
		UniformWrapped.Children[0].Brush.MaterialParams.Get(FString()).Contains(TEXT("\"CornerRadius\":0.609")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperBoxShadowMultiLayerSingleOverlayTest,
	"Design2UMG.VisualMapper.BoxShadowMultiLayerSingleOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperBoxShadowMultiLayerSingleOverlayTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Rectangle, 100.0f, 80.0f);

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Image");
	Widget.Id = TEXT("multi_shadow_source");
	Widget.Name = TEXT("Multi Shadow Source");

	FUMGBoxShadowLayerData FirstShadow;
	FirstShadow.Color = FLinearColor::Black;
	FirstShadow.Offset = FVector2D(2.0f, 3.0f);
	FirstShadow.Blur = 4.0f;
	FUMGBoxShadowLayerData SecondShadow;
	SecondShadow.Color = FLinearColor::Red;
	SecondShadow.Offset = FVector2D(-2.0f, -3.0f);
	SecondShadow.Blur = 8.0f;

	FUMGWidgetNode Wrapped = VisualMapper::WrapWithBoxShadow(
		MoveTemp(Widget), Node, TArray<FUMGBoxShadowLayerData>{ FirstShadow, SecondShadow });

	TestEqual(TEXT("Multiple shadow layers stay on one overlay"), Wrapped.Type, FString(TEXT("Overlay")));
	TestEqual(TEXT("Single overlay carries both shadows and the content"), Wrapped.Children.Num(), 3);
	if (Wrapped.Children.Num() != 3)
	{
		return false;
	}

	// The later source layer renders below the earlier one, matching the stacking the retired per-layer nesting produced.
	TestEqual(TEXT("Second shadow layer renders below the first"), Wrapped.Children[0].Id, FString(TEXT("multi_shadow_source_shadow1")));
	TestEqual(TEXT("First shadow layer renders above the second"), Wrapped.Children[1].Id, FString(TEXT("multi_shadow_source_shadow0")));
	TestTrue(TEXT("Second shadow layer keeps its own colour"),
		Wrapped.Children[0].Brush.BrushColor.IsSet()
		&& Wrapped.Children[0].Brush.BrushColor.GetValue().Equals(FLinearColor::Red));
	TestEqual(TEXT("Content stays on top with a single content suffix"), Wrapped.Children[2].Id, FString(TEXT("multi_shadow_source_content")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConverterExplicitEmptyBrushPresenceSizeBoxElisionTest,
	"Design2UMG.Converter.ExplicitEmptyBrushPresenceSizeBoxElision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConverterExplicitEmptyBrushPresenceSizeBoxElisionTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> ParentNode = MakeFixedNode(EDesignNodeType::Frame, 256.0f, 128.0f);
	ParentNode->LayoutData.Layout = EDesignLayout::Horizontal;
	TSharedPtr<FDesignNode> ChildNode = MakeFixedNode(EDesignNodeType::Path, 24.0f, 24.0f);

	FUMGWidgetNode BaseImageWidget;
	BaseImageWidget.Type = TEXT("Image");
	BaseImageWidget.WidgetStyle.WidthOverride = 24.0f;
	BaseImageWidget.WidgetStyle.HeightOverride = 24.0f;

	TestTrue(TEXT("Size-constrained image without authored brush or path data still wraps"),
		ShouldWrapChildWithSizeBoxCurrentTruth(ParentNode, ChildNode, BaseImageWidget));

	FUMGWidgetNode NonEmptyMaterialWidget = BaseImageWidget;
	NonEmptyMaterialWidget.Brush.MaterialRef = TEXT("roundedrect");
	TestFalse(TEXT("Non-empty material ref elides the SizeBox wrapper"),
		ShouldWrapChildWithSizeBoxCurrentTruth(ParentNode, ChildNode, NonEmptyMaterialWidget));

	FUMGWidgetNode ExplicitEmptyMaterialWidget = BaseImageWidget;
	ExplicitEmptyMaterialWidget.Brush.MaterialRef = TEXT("");
	TestTrue(TEXT("Explicit empty material ref stays queryable"),
		HasWidgetProperty(ExplicitEmptyMaterialWidget, UMGPropertyKeys::MaterialRef));
	TestTrue(TEXT("Explicit empty material ref stays empty"),
		GetWidgetProperty(ExplicitEmptyMaterialWidget, UMGPropertyKeys::MaterialRef).IsEmpty());
	TestFalse(TEXT("Explicit empty material ref follows the same SizeBox elision path"),
		ShouldWrapChildWithSizeBoxCurrentTruth(ParentNode, ChildNode, ExplicitEmptyMaterialWidget));

	FUMGWidgetNode NonEmptyTextureWidget = BaseImageWidget;
	NonEmptyTextureWidget.Brush.Image.TexturePath = TEXT("/Game/UI/T_Test.T_Test");
	TestFalse(TEXT("Non-empty texture path elides the SizeBox wrapper"),
		ShouldWrapChildWithSizeBoxCurrentTruth(ParentNode, ChildNode, NonEmptyTextureWidget));

	FUMGWidgetNode ExplicitEmptyTextureWidget = BaseImageWidget;
	ExplicitEmptyTextureWidget.Brush.Image.TexturePath = TEXT("");
	TestTrue(TEXT("Explicit empty texture path stays queryable"),
		HasWidgetProperty(ExplicitEmptyTextureWidget, UMGPropertyKeys::TexturePath));
	TestTrue(TEXT("Explicit empty texture path stays empty"),
		GetWidgetProperty(ExplicitEmptyTextureWidget, UMGPropertyKeys::TexturePath).IsEmpty());
	TestFalse(TEXT("Explicit empty texture path follows the same SizeBox elision path"),
		ShouldWrapChildWithSizeBoxCurrentTruth(ParentNode, ChildNode, ExplicitEmptyTextureWidget));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualSupportExplicitEmptyBrushPresenceBorderFallbackTest,
	"Design2UMG.VisualSupport.ExplicitEmptyBrushPresenceBorderFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualSupportExplicitEmptyBrushPresenceBorderFallbackTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FDesignNode> StrokeOnlyBorderNode = MakeStrokeOnlyBorderNode();

	FUMGWidgetNode NoFillWidget;
	NoFillWidget.Type = TEXT("Border");
	Design2UMGConverterVisualSupport::ApplyBaseVisualProperties(NoFillWidget, StrokeOnlyBorderNode);
	TestEqual(TEXT("Missing fill promotes transparent border fallback"),
		GetWidgetProperty(NoFillWidget, UMGPropertyKeys::BrushColor),
		FString(TEXT("#00000000")));
	TestEqual(TEXT("Missing fill promotes rounded border fallback"),
		GetWidgetProperty(NoFillWidget, UMGPropertyKeys::DrawAs),
		FString(TEXT("RoundedBox")));

	FUMGWidgetNode ExplicitEmptyMaterialWidget;
	ExplicitEmptyMaterialWidget.Type = TEXT("Border");
	ExplicitEmptyMaterialWidget.Brush.MaterialRef = TEXT("");
	Design2UMGConverterVisualSupport::ApplyBaseVisualProperties(ExplicitEmptyMaterialWidget, StrokeOnlyBorderNode);
	TestTrue(TEXT("Explicit empty material ref stays present on border"),
		HasWidgetProperty(ExplicitEmptyMaterialWidget, UMGPropertyKeys::MaterialRef));
	TestTrue(TEXT("Explicit empty material ref stays empty on border"),
		GetWidgetProperty(ExplicitEmptyMaterialWidget, UMGPropertyKeys::MaterialRef).IsEmpty());
	TestFalse(TEXT("Explicit empty material ref blocks transparent brush fallback"),
		HasWidgetProperty(ExplicitEmptyMaterialWidget, UMGPropertyKeys::BrushColor));
	TestEqual(TEXT("Explicit empty material ref keeps stroke-owned draw type"),
		GetWidgetProperty(ExplicitEmptyMaterialWidget, UMGPropertyKeys::DrawAs),
		FString(TEXT("Box")));

	FUMGWidgetNode ExplicitEmptyTextureWidget;
	ExplicitEmptyTextureWidget.Type = TEXT("Border");
	ExplicitEmptyTextureWidget.Brush.Image.TexturePath = TEXT("");
	Design2UMGConverterVisualSupport::ApplyBaseVisualProperties(ExplicitEmptyTextureWidget, StrokeOnlyBorderNode);
	TestTrue(TEXT("Explicit empty texture path stays present on border"),
		HasWidgetProperty(ExplicitEmptyTextureWidget, UMGPropertyKeys::TexturePath));
	TestTrue(TEXT("Explicit empty texture path stays empty on border"),
		GetWidgetProperty(ExplicitEmptyTextureWidget, UMGPropertyKeys::TexturePath).IsEmpty());
	TestFalse(TEXT("Explicit empty texture path blocks transparent brush fallback"),
		HasWidgetProperty(ExplicitEmptyTextureWidget, UMGPropertyKeys::BrushColor));
	TestEqual(TEXT("Explicit empty texture path keeps stroke-owned draw type"),
		GetWidgetProperty(ExplicitEmptyTextureWidget, UMGPropertyKeys::DrawAs),
		FString(TEXT("Box")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutMapperDirectHelpersTest,
	"Design2UMG.LayoutMapper.DirectHelpers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutMapperDirectHelpersTest::RunTest(const FString& Parameters)
{
	TArray<FUMGWidgetNode> Children;
	Children.SetNum(3);
	LayoutMapper::ApplyGap(Children, 12.0f, EDesignLayout::Horizontal);
	TestEqual(TEXT("First child horizontal gap stays zero"), Children[0].Slot.Padding.Left, 0.0f);
	TestEqual(TEXT("Second child horizontal gap"), Children[1].Slot.Padding.Left, 12.0f);
	TestEqual(TEXT("Third child horizontal gap"), Children[2].Slot.Padding.Left, 12.0f);

	FUMGWidgetNode Widget;
	FDesignPadding Padding;
	Padding.Left = 1.0f;
	Padding.Top = 2.0f;
	Padding.Right = 3.0f;
	Padding.Bottom = 4.0f;
	LayoutMapper::ApplyPadding(Widget, Padding);
	TestEqual(TEXT("Padding serialized directly"), GetWidgetProperty(Widget, TEXT("Padding")), TEXT("1.0,2.0,3.0,4.0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVariableResolverLiteralDollarTextTest,
	"Design2UMG.Parser.LiteralDollarTextIsNotVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVariableResolverLiteralDollarTextTest::RunTest(const FString& Parameters)
{
	TMap<FString, FDesignVariable> Variables;
	FDesignVariableResolver Resolver;
	Resolver.SetVariables(Variables);
	TestFalse(TEXT("$45.00 should not be treated as variable ref"), Resolver.IsVariableRef(TEXT("$45.00")));
	TestEqual(TEXT("Literal dollar text should round-trip unchanged"),
		Resolver.ResolveString(TEXT("$45.00")),
		FString(TEXT("$45.00")));
	TestTrue(TEXT("$--font-primary remains a variable ref"), Resolver.IsVariableRef(TEXT("$--font-primary")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCVisualMapperMultiFillSmokeTest,
	"Design2UMG.BatchC.VisualMapperMultiFillSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCVisualMapperMultiFillSmokeTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("multi_fill_source");
	Node->Name = TEXT("MultiFillSource");
	Node->Type = EDesignNodeType::Rectangle;
	Node->Transform.SetWidthValue(FDesignSize::Fixed(96.0f));
	Node->Transform.SetHeightValue(FDesignSize::Fixed(48.0f));
	Node->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#112233FF")));
	Node->Visual.Fills.Add(MakeGradientFill());

	FUMGWidgetNode BaseWidget;
	BaseWidget.Type = TEXT("Image");
	BaseWidget.Id = TEXT("multi_fill_widget");
	BaseWidget.Name = TEXT("MultiFillWidget");
	BaseWidget.Bounds.W = 96.0f;
	BaseWidget.Bounds.H = 48.0f;

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithMultiFill(MoveTemp(BaseWidget), Node, FVisualMapperOptions());
	TestEqual(TEXT("Multi-fill wrapper creates an overlay shell"), Wrapped.Type, FString(TEXT("Overlay")));
	TestEqual(TEXT("Multi-fill wrapper keeps two paint layers plus content"), Wrapped.Children.Num(), 3);
	if (Wrapped.Children.Num() != 3)
	{
		return false;
	}

	const FUMGWidgetNode& GradientLayer = Wrapped.Children[1];
	TestEqual(TEXT("Gradient fill layer becomes image material"), GradientLayer.Type, FString(TEXT("Image")));
	TestTrue(TEXT("Gradient layer carries typed material ref"), GradientLayer.Brush.MaterialRef.IsSet());
	TestEqual(TEXT("Radial gradient resolves to radial material"),
		GradientLayer.Brush.MaterialRef.GetValue(),
		FString(TEXT("radialgradient")));
	TestTrue(TEXT("Gradient layer carries typed gradient stops"), GradientLayer.Brush.Gradient.Stops.Num() == 2);
	TestTrue(TEXT("Original widget becomes content child"), Wrapped.Children[2].Id.EndsWith(TEXT("_content")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualSupportMultiFillOwnsSurfaceTest,
	"Design2UMG.VisualSupport.MultiFillOwnsSurfaceWithoutBorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualSupportMultiFillOwnsSurfaceTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 96.0f, 48.0f);
	Node->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#112233FF")));
	Node->Visual.Fills.Add(MakeGradientFill());
	TestFalse(
		TEXT("A plain multi-fill frame lets the multi-fill overlay own the surface"),
		Design2UMGConverterVisualSupport::ShouldWrapNodeWithBorder(Node, false));

	Node->Visual.CornerRadius.bUniform = true;
	Node->Visual.CornerRadius.Uniform = 8.0f;
	TestTrue(
		TEXT("A rounded multi-fill frame keeps a Border for the authored corner semantics"),
		Design2UMGConverterVisualSupport::ShouldWrapNodeWithBorder(Node, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperExplicitSideStrokeKeepsNonLayoutSurfaceTest,
	"Design2UMG.VisualMapper.ExplicitSideStrokeKeepsNonLayoutSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperExplicitSideStrokeKeepsNonLayoutSurfaceTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 64.0f, 32.0f);
	Node->Children.Add(MakeFixedNode(EDesignNodeType::Rectangle, 16.0f, 16.0f));
	Node->Visual.Stroke.Thickness.bUniform = false;
	Node->Visual.Stroke.Thickness.Left = 2.0f;
	Node->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#112233FF"));

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Image");
	Widget.Id = TEXT("image_stroke_surface");
	Widget.Name = TEXT("ImageStrokeSurface");
	VisualMapper::ApplyStroke(Widget, Node->Visual.Stroke);

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("Non-layout content keeps the surface plus one stroke line"), Wrapped.Children.Num(), 2);
	if (Wrapped.Children.Num() != 2)
	{
		return false;
	}
	TestEqual(TEXT("Non-layout content remains owned by a Border"), Wrapped.Children[0].Type, TEXT("Border"));
	TestEqual(TEXT("Surface keeps the image content"), Wrapped.Children[0].Children.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperExplicitSideStrokeKeepsPaddedSurfaceTest,
	"Design2UMG.VisualMapper.ExplicitSideStrokeKeepsPaddedSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperExplicitSideStrokeKeepsPaddedSurfaceTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 128.0f, 48.0f);
	Node->Children.Add(MakeFixedNode(EDesignNodeType::Text, 64.0f, 20.0f));
	FDesignPadding Padding;
	Padding.Left = 8.0f;
	Padding.Top = 4.0f;
	Padding.Right = 8.0f;
	Padding.Bottom = 4.0f;
	Node->LayoutData.ExplicitPadding = Padding;
	Node->Visual.Stroke.Thickness.bUniform = false;
	Node->Visual.Stroke.Thickness.Bottom = 2.0f;
	Node->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#112233FF"));

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("VerticalBox");
	Widget.Id = TEXT("padded_stroke_panel");
	Widget.Name = TEXT("PaddedStrokePanel");
	Widget.Bounds.W = 128.0f;
	Widget.Bounds.H = 48.0f;

	VisualMapper::ApplyStroke(Widget, Node->Visual.Stroke);
	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("Explicit side stroke still uses an overlay"), Wrapped.Type, TEXT("Overlay"));
	TestEqual(TEXT("Padding keeps the surface plus one stroke line"), Wrapped.Children.Num(), 2);
	if (Wrapped.Children.Num() != 2)
	{
		return false;
	}
	TestEqual(TEXT("Padded surface remains a Border"), Wrapped.Children[0].Type, TEXT("Border"));
	TestTrue(TEXT("Padded surface retains authored padding"), Wrapped.Children[0].WidgetStyle.Padding.IsSet());
	TestEqual(TEXT("Padded surface still owns its content"), Wrapped.Children[0].Children.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperMultiFillElidesEmptyBorderContentTest,
	"Design2UMG.BatchC.VisualMapperMultiFillElidesEmptyBorderContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperMultiFillElidesEmptyBorderContentTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 96.0f, 48.0f);
	Node->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#112233FF")));
	Node->Visual.Fills.Add(MakeGradientFill());
	Node->Visual.CornerRadius.bUniform = true;
	Node->Visual.CornerRadius.Uniform = 8.0f;

	FUMGWidgetNode Border;
	Border.Type = TEXT("Border");
	Border.Id = TEXT("multi_fill_leaf");
	Border.Name = TEXT("MultiFillLeaf");
	Border.Bounds.W = 96.0f;
	Border.Bounds.H = 48.0f;
	Border.Brush.BrushColor = FLinearColor::Transparent;
	VisualMapper::ApplyRoundedCorner(Border, Node->Visual.CornerRadius, 96.0f, 48.0f);

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithMultiFill(MoveTemp(Border), Node, FVisualMapperOptions());
	TestEqual(TEXT("Multi-fill leaf keeps only its two paint layers"), Wrapped.Children.Num(), 2);
	for (const FUMGWidgetNode& Layer : Wrapped.Children)
	{
		TestTrue(TEXT("Each multi-fill paint layer keeps the authored corner"), Layer.Brush.DrawAs.IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperMultiFillPromotesBorderContentTest,
	"Design2UMG.BatchC.VisualMapperMultiFillPromotesBorderContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperMultiFillPromotesBorderContentTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 96.0f, 48.0f);
	Node->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#112233FF")));
	Node->Visual.Fills.Add(MakeGradientFill());

	FUMGWidgetNode Border;
	Border.Type = TEXT("Border");
	Border.Id = TEXT("multi_fill_container");
	Border.Name = TEXT("MultiFillContainer");
	Border.Bounds.W = 96.0f;
	Border.Bounds.H = 48.0f;
	Border.Brush.BrushColor = FLinearColor::Transparent;

	FUMGWidgetNode Content;
	Content.Type = TEXT("HorizontalBox");
	Content.Id = TEXT("multi_fill_container_inner");
	Content.Name = TEXT("MultiFillContainer_inner");
	Border.Children.Add(MoveTemp(Content));

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithMultiFill(MoveTemp(Border), Node, FVisualMapperOptions());
	TestEqual(TEXT("Multi-fill container keeps paint layers plus direct content"), Wrapped.Children.Num(), 3);
	if (Wrapped.Children.Num() != 3)
	{
		return false;
	}
	TestEqual(TEXT("Content panel is promoted out of the empty Border"), Wrapped.Children[2].Type, FString(TEXT("HorizontalBox")));
	TestTrue(TEXT("Promoted content keeps the stable content suffix"), Wrapped.Children[2].Id.EndsWith(TEXT("_content")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperMultiFillElidesEmptyPanelContentTest,
	"Design2UMG.BatchC.VisualMapperMultiFillElidesEmptyPanelContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperMultiFillElidesEmptyPanelContentTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 96.0f, 48.0f);
	Node->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#112233FF")));
	Node->Visual.Fills.Add(MakeGradientFill());

	FUMGWidgetNode EmptyPanel;
	EmptyPanel.Type = TEXT("CanvasPanel");
	EmptyPanel.Id = TEXT("multi_fill_empty_panel");
	EmptyPanel.Name = TEXT("MultiFillEmptyPanel");

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithMultiFill(MoveTemp(EmptyPanel), Node, FVisualMapperOptions());
	TestEqual(TEXT("An empty non-border panel is not retained as a content carrier"), Wrapped.Children.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperMultiFillKeepsPaddedBorderContentTest,
	"Design2UMG.BatchC.VisualMapperMultiFillKeepsPaddedBorderContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperMultiFillKeepsPaddedBorderContentTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 96.0f, 48.0f);
	Node->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#112233FF")));
	Node->Visual.Fills.Add(MakeGradientFill());

	FUMGWidgetNode Border;
	Border.Type = TEXT("Border");
	Border.Id = TEXT("multi_fill_padded");
	Border.Name = TEXT("MultiFillPadded");
	Border.Bounds.W = 96.0f;
	Border.Bounds.H = 48.0f;
	Border.Brush.BrushColor = FLinearColor::Transparent;
	Border.WidgetStyle.Padding = FMargin(8.0f);

	FUMGWidgetNode Content;
	Content.Type = TEXT("HorizontalBox");
	Content.Id = TEXT("multi_fill_padded_inner");
	Content.Name = TEXT("MultiFillPadded_inner");
	Border.Children.Add(MoveTemp(Content));

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithMultiFill(MoveTemp(Border), Node, FVisualMapperOptions());
	TestEqual(TEXT("Padded multi-fill keeps paint layers plus Border carrier"), Wrapped.Children.Num(), 3);
	if (Wrapped.Children.Num() != 3)
	{
		return false;
	}
	TestEqual(TEXT("Padding remains owned by Border"), Wrapped.Children[2].Type, FString(TEXT("Border")));
	TestTrue(TEXT("Padding remains present"), Wrapped.Children[2].WidgetStyle.Padding.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradientTypedPayloadMaterialParamsTest,
	"Design2UMG.Gradient.TypedPayloadMaterialParams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGradientTypedPayloadMaterialParamsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("gradient_payload_source");
	Node->Name = TEXT("GradientPayloadSource");
	Node->Type = EDesignNodeType::Rectangle;
	Node->Transform.SetWidthValue(FDesignSize::Fixed(128.0f));
	Node->Transform.SetHeightValue(FDesignSize::Fixed(64.0f));
	Node->Visual.Fills.Add(MakeRadialMultiStopGradientFill());

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Image");
	Widget.Id = TEXT("gradient_payload_widget");
	Widget.Name = TEXT("GradientPayloadWidget");
	VisualMapper::ApplyVisualProperties(Widget, Node);

	TestEqual(TEXT("Radial gradient uses procedural material ref"), GetWidgetProperty(Widget, UMGPropertyKeys::MaterialRef), FString(TEXT("radialgradient")));
	TestEqual(TEXT("Typed gradient type"), Widget.Brush.Gradient.Type.GetValue(), FString(TEXT("Radial")));
	TestTrue(TEXT("Typed gradient keeps center"), Widget.Brush.Gradient.Center.IsSet());
	TestTrue(TEXT("Typed gradient keeps size"), Widget.Brush.Gradient.Size.IsSet());
	TestTrue(TEXT("Typed gradient keeps rotation"), Widget.Brush.Gradient.Rotation.IsSet());
	TestEqual(TEXT("Typed gradient stop count"), Widget.Brush.Gradient.Stops.Num(), 3);
	TestTrue(TEXT("Typed gradient center X"),
		FMath::IsNearlyEqual(Widget.Brush.Gradient.Center.GetValue().X, 0.2f, 0.0001f));
	TestTrue(TEXT("Typed gradient center Y"),
		FMath::IsNearlyEqual(Widget.Brush.Gradient.Center.GetValue().Y, 0.7f, 0.0001f));
	TestTrue(TEXT("Typed gradient size width"),
		FMath::IsNearlyEqual(Widget.Brush.Gradient.Size.GetValue().X, 0.8f, 0.0001f));
	TestTrue(TEXT("Typed gradient size height"),
		FMath::IsNearlyEqual(Widget.Brush.Gradient.Size.GetValue().Y, 1.35f, 0.0001f));

	const FString MaterialParams = GetWidgetProperty(Widget, UMGPropertyKeys::MaterialParams);
	TestTrue(TEXT("Material params include multi-stop array"), MaterialParams.Contains(TEXT("\"Stops\"")));
	TestTrue(TEXT("Material params include middle stop position"), MaterialParams.Contains(TEXT("\"Pos\":0.4500")));
	TestTrue(TEXT("Material params include radial center"), MaterialParams.Contains(TEXT("\"CenterX\":0.2000")));
	TestTrue(TEXT("Material params include radial size"), MaterialParams.Contains(TEXT("\"SizeHeight\":1.3500")));
	TestTrue(TEXT("Material params include rotation"), MaterialParams.Contains(TEXT("\"Rotation\":37.5000")));

	FUMGProceduralResourceRequest Request;
	TestTrue(TEXT("Typed procedural request exposes gradient material data"),
		Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Widget, Request));
	TestEqual(TEXT("Request material ref"), Request.MaterialRef, FString(TEXT("radialgradient")));
	TestEqual(TEXT("Request material params"), Request.MaterialParams.Get(FString()), MaterialParams);

	TSharedPtr<FDesignNode> LinearNode = MakeShared<FDesignNode>();
	LinearNode->Id = TEXT("linear_gradient_payload_source");
	LinearNode->Name = TEXT("LinearGradientPayloadSource");
	LinearNode->Type = EDesignNodeType::Rectangle;
	LinearNode->Transform.SetWidthValue(FDesignSize::Fixed(128.0f));
	LinearNode->Transform.SetHeightValue(FDesignSize::Fixed(64.0f));
	FDesignFill& LinearFill = LinearNode->Visual.Fills.AddDefaulted_GetRef();
	LinearFill.Type = EDesignFillType::Gradient;
	LinearFill.Gradient.GradientType = EDesignGradientType::Linear;
	LinearFill.Gradient.Rotation = 15.0f;
	LinearFill.Gradient.StartPoint = FVector2D(0.2f, 0.3f);
	LinearFill.Gradient.EndPoint = FVector2D(0.8f, 0.7f);
	FDesignGradientStop& LinearStartStop = LinearFill.Gradient.Colors.AddDefaulted_GetRef();
	LinearStartStop.Color = TEXT("#FF0000FF");
	LinearStartStop.Position = 0.0f;
	FDesignGradientStop& LinearEndStop = LinearFill.Gradient.Colors.AddDefaulted_GetRef();
	LinearEndStop.Color = TEXT("#0000FFFF");
	LinearEndStop.Position = 1.0f;

	FUMGWidgetNode LinearWidget;
	LinearWidget.Type = TEXT("Image");
	LinearWidget.Id = TEXT("linear_gradient_payload_widget");
	LinearWidget.Name = TEXT("LinearGradientPayloadWidget");
	VisualMapper::ApplyVisualProperties(LinearWidget, LinearNode);
	TestTrue(TEXT("Typed linear gradient keeps start point"), LinearWidget.Brush.Gradient.StartPoint.IsSet());
	TestTrue(TEXT("Typed linear gradient keeps end point"), LinearWidget.Brush.Gradient.EndPoint.IsSet());
	const FString LinearMaterialParams = GetWidgetProperty(LinearWidget, UMGPropertyKeys::MaterialParams);
	TestTrue(TEXT("Linear material params include start point"), LinearMaterialParams.Contains(TEXT("\"StartX\":0.2000"))
		&& LinearMaterialParams.Contains(TEXT("\"StartY\":0.3000")));
	TestTrue(TEXT("Linear material params include end point"), LinearMaterialParams.Contains(TEXT("\"EndX\":0.8000"))
		&& LinearMaterialParams.Contains(TEXT("\"EndY\":0.7000")));

	Widget.Brush.MaterialParams = TEXT("{\"ColorCenter\":\"#01020304\",\"ColorEdge\":\"#05060708\"}");
	FUMGProceduralResourceRequest ExistingParamsRequest;
	TestTrue(TEXT("Typed procedural request still builds when legacy raw material params are present"),
		Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Widget, ExistingParamsRequest));
	TestEqual(TEXT("Typed gradient descriptor keeps precedence over legacy raw material params"),
		ExistingParamsRequest.MaterialParams.Get(FString()),
		MaterialParams);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutFixerConservatismTest,
	"Design2UMG.LayoutFixer.ConservatismCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutFixerConservatismTest::RunTest(const FString& Parameters)
{
	FFixerPreset Preset;
	Preset.DesignSize = FIntPoint(1920, 1080);
	Preset.SnapPx = 4;
	FFixerWidgetNode Root;
	Root.Bounds = {0, 0, 1, 1};
	FFixerWidgetNode Child1;
	Child1.Bounds = {0, 0, 0.1f, 0.1f};
	FFixerWidgetNode Child2;
	Child2.Bounds = {0.5f, 0, 0.1f, 0.1f};
	Root.Children.Add(Child1);
	Root.Children.Add(Child2);
	TArray<FFixerLogEntry> Log;
	FFixerStats Stats;
	LayoutFixer::FixWidgetTree(Root, Preset, Log, &Stats);
	TestEqual(TEXT("RolledBack should be non-negative"), Stats.RolledBack >= 0, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutMapperSizeConstraintFusionTest,
	"Design2UMG.LayoutMapper.SizeConstraintFusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutMapperSizeConstraintFusionTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Child;
	Child.Type = TEXT("Border");
	Child.Id = TEXT("card");
	Child.Name = TEXT("Card");

	FUMGWidgetNode SizeBox;
	SizeBox.Type = TEXT("SizeBox");
	SizeBox.WidgetStyle.WidthOverride = 320.0f;
	SizeBox.WidgetStyle.MinDesiredHeight = 48.0f;

	TestTrue(TEXT("Border supports size constraint fusion"), LayoutMapper::TryFuseSizeConstraints(Child, SizeBox));
	TestEqual(TEXT("Fused Border type"), Child.Type, FString(TEXT("Design2UMGConstrainedBorder")));
	TestEqual(TEXT("Fused Border keeps id"), Child.Id, FString(TEXT("card")));
	TestEqual(TEXT("Fused Border keeps name"), Child.Name, FString(TEXT("Card")));
	TestTrue(TEXT("Fused width override is preserved"), Child.WidgetStyle.WidthOverride.IsSet());
	TestTrue(TEXT("Fused minimum height is preserved"), Child.WidgetStyle.MinDesiredHeight.IsSet());
	TestEqual(TEXT("Fused width override value"), Child.WidgetStyle.WidthOverride.Get(0.0f), 320.0f);
	TestEqual(TEXT("Fused minimum height value"), Child.WidgetStyle.MinDesiredHeight.Get(0.0f), 48.0f);

	FUMGWidgetNode Unsupported;
	Unsupported.Type = TEXT("EditableTextBox");
	TestFalse(TEXT("Unsupported widget stays wrapped"), LayoutMapper::TryFuseSizeConstraints(Unsupported, SizeBox));
	TestEqual(TEXT("Unsupported type is unchanged"), Unsupported.Type, FString(TEXT("EditableTextBox")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
