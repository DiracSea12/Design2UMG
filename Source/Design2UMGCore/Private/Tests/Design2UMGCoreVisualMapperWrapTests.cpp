// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// VisualMapper WrapWithBorder family: direct/typed wrapping, panel elision, single-child flow promotion and semantics matrices, multi-child flow, scroll box, explicit side stroke.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DesignSchema.h"
#include "LayoutMapper.h"
#include "UMGSchema.h"
#include "UMGValueParser.h"
#include "VisualMapper.h"
#include "Design2UMGCoreTestHelpers.h"

using namespace Design2UMGCoreTests;


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
			Spacer.bSyntheticJustifySpacer = true;
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

#endif // WITH_DEV_AUTOMATION_TESTS

