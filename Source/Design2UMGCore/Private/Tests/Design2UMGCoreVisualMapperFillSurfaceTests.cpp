// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// VisualMapper multi-fill and side-stroke surface family: fill-owned surfaces, empty/padded content elision and promotion.

#if WITH_DEV_AUTOMATION_TESTS

#include "Design2UMGConverterVisualSupport.h"
#include "Design2UMGGeneratedNameSuffixes.h"
#include "Misc/AutomationTest.h"
#include "DesignSchema.h"
#include "UMGSchema.h"
#include "VisualMapper.h"
#include "Design2UMGCoreTestHelpers.h"

using namespace Design2UMGCoreTests;

namespace
{
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
	TestEqual(TEXT("Multi-fill wrapper keeps two paint layers when content is empty"), Wrapped.Children.Num(), 2);
	if (Wrapped.Children.Num() != 2)
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
	TestFalse(TEXT("Empty original widget is elided from the wrapper"), Wrapped.Children.ContainsByPredicate(
		[](const FUMGWidgetNode& Child)
		{
			return Child.Id.EndsWith(Design2UMGGeneratedNameSuffixes::Content());
		}));
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
	TestTrue(TEXT("Promoted content keeps the stable content suffix"), Wrapped.Children[2].Id.EndsWith(Design2UMGGeneratedNameSuffixes::Content()));
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

#endif // WITH_DEV_AUTOMATION_TESTS

