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
	TestTrue(TEXT("Gradient layer carries typed gradient data"), GradientLayer.Brush.Gradient.HasAny());
	TestFalse(TEXT("Gradient layer does not duplicate the derived material ref"), GradientLayer.Brush.MaterialRef.IsSet());
	TestFalse(TEXT("Gradient layer carries no generated JSON"), GradientLayer.Brush.MaterialParams.IsSet());
	TestEqual(TEXT("Radial gradient resolves to radial material"),
		Design2UMGWidgetNodeAccess::GetGradientMaterialRef(GradientLayer.Brush.Gradient),
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperTranslucentStrokeCompositesOverSolidFillTest,
	"Design2UMG.VisualMapper.TranslucentStrokeCompositesOverSolidFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperTranslucentStrokeCompositesOverSolidFillTest::RunTest(const FString& Parameters)
{
	// Pencil composites an inside stroke over the node fill at the edge band.
	// The UMG RoundedBox outline instead REPLACES that band, so a translucent
	// stroke over a dark fill must bake the composite into the outline color;
	// otherwise the edge renders as a bare translucent stripe on transparency.
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 64.0f, 48.0f);
	Node->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#171F33")));
	Node->Visual.Stroke.Thickness.bUniform = true;
	Node->Visual.Stroke.Thickness.Uniform = 1.0f;
	Node->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#FFFFFF1A"));

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Border");
	VisualMapper::ApplyVisualProperties(Widget, Node);

	if (!TestTrue(TEXT("Stroke outline color is set"), Widget.Brush.OutlineColor.IsSet()))
	{
		return false;
	}
	// sRGB byte composite: 255*(26/255) + fill*(1-26/255) => (47, 54, 72).
	const FLinearColor ExpectedComposited(FColor(47, 54, 72, 255));
	const FLinearColor Actual = Widget.Brush.OutlineColor.GetValue();
	TestTrue(TEXT("Translucent stroke bakes the over-fill composite into the outline color"),
		FMath::Abs(Actual.R - ExpectedComposited.R) < 0.005f
		&& FMath::Abs(Actual.G - ExpectedComposited.G) < 0.005f
		&& FMath::Abs(Actual.B - ExpectedComposited.B) < 0.005f
		&& FMath::Abs(Actual.A - 1.0f) < 0.005f);

	// Opaque strokes must stay untouched (composite equals the stroke itself).
	TSharedPtr<FDesignNode> OpaqueNode = MakeFixedNode(EDesignNodeType::Frame, 64.0f, 48.0f);
	OpaqueNode->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#171F33")));
	OpaqueNode->Visual.Stroke.Thickness.bUniform = true;
	OpaqueNode->Visual.Stroke.Thickness.Uniform = 1.0f;
	OpaqueNode->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#FFAA00"));

	FUMGWidgetNode OpaqueWidget;
	OpaqueWidget.Type = TEXT("Border");
	VisualMapper::ApplyVisualProperties(OpaqueWidget, OpaqueNode);
	if (!TestTrue(TEXT("Opaque stroke outline color is set"), OpaqueWidget.Brush.OutlineColor.IsSet()))
	{
		return false;
	}
	const FLinearColor OpaqueExpected(FColor(255, 170, 0, 255));
	const FLinearColor OpaqueActual = OpaqueWidget.Brush.OutlineColor.GetValue();
	TestTrue(TEXT("Opaque stroke keeps its own color"),
		FMath::Abs(OpaqueActual.R - OpaqueExpected.R) < 0.005f
		&& FMath::Abs(OpaqueActual.G - OpaqueExpected.G) < 0.005f
		&& FMath::Abs(OpaqueActual.B - OpaqueExpected.B) < 0.005f);

	// Translucent stroke without a solid opaque fill stays as authored.
	TSharedPtr<FDesignNode> BareNode = MakeFixedNode(EDesignNodeType::Frame, 64.0f, 48.0f);
	BareNode->Visual.Stroke.Thickness.bUniform = true;
	BareNode->Visual.Stroke.Thickness.Uniform = 1.0f;
	BareNode->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#FFFFFF1A"));

	FUMGWidgetNode BareWidget;
	BareWidget.Type = TEXT("Border");
	VisualMapper::ApplyVisualProperties(BareWidget, BareNode);
	if (TestTrue(TEXT("Bare translucent stroke outline color is set"), BareWidget.Brush.OutlineColor.IsSet()))
	{
		const FLinearColor BareActual = BareWidget.Brush.OutlineColor.GetValue();
		TestTrue(TEXT("Bare translucent stroke keeps authored alpha"),
			BareActual.A > 0.05f && BareActual.A < 0.2f);
	}

	// Center/Outside strokes do not overlay the node's own fill (ApplyStroke
	// pushes their band off the body), so the composite must NOT be baked and
	// the outline keeps the authored translucent stroke color untouched.
	for (const EDesignStrokeAlign Align : { EDesignStrokeAlign::Center, EDesignStrokeAlign::Outside })
	{
		TSharedPtr<FDesignNode> AlignedNode = MakeFixedNode(EDesignNodeType::Frame, 64.0f, 48.0f);
		AlignedNode->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#171F33")));
		AlignedNode->Visual.Stroke.Align = Align;
		AlignedNode->Visual.Stroke.Thickness.bUniform = true;
		AlignedNode->Visual.Stroke.Thickness.Uniform = 1.0f;
		AlignedNode->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#FFFFFF1A"));

		FUMGWidgetNode AlignedWidget;
		AlignedWidget.Type = TEXT("Border");
		VisualMapper::ApplyVisualProperties(AlignedWidget, AlignedNode);
		if (TestTrue(TEXT("Non-inside stroke outline color is set"), AlignedWidget.Brush.OutlineColor.IsSet()))
		{
			const FLinearColor AlignedActual = AlignedWidget.Brush.OutlineColor.GetValue();
			const FLinearColor Authored(FColor(255, 255, 255, 26));
			TestTrue(TEXT("Non-inside translucent stroke is not composited over the fill"),
				FMath::Abs(AlignedActual.R - Authored.R) < 0.005f
				&& FMath::Abs(AlignedActual.G - Authored.G) < 0.005f
				&& FMath::Abs(AlignedActual.B - Authored.B) < 0.005f
				&& FMath::Abs(AlignedActual.A - Authored.A) < 0.005f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperStrokeBrushSelectionTest,
	"Design2UMG.VisualMapper.StrokeBrushSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperStrokeBrushSelectionTest::RunTest(const FString&)
{
	FDesignStroke Stroke;
	Stroke.Thickness.bUniform = true;
	Stroke.Thickness.Uniform = 2.0f;
	Stroke.Fill = FDesignFill::FromColor(TEXT("#FF0000"));
	for (int32 Variant = 0; Variant < 6; ++Variant)
	{
		FUMGWidgetNode Widget;
		switch (Variant)
		{
		case 0: Widget.Brush.BrushColor = FLinearColor::White; break;
		case 1: Widget.Brush.Image.TexturePath = TEXT("synthetic-image.png"); break;
		case 2: Widget.Brush.Gradient.Type = TEXT("linear"); break;
		case 3: Widget.Brush.MaterialRef = TEXT("/Game/SyntheticMaterial"); break;
		case 4: Widget.Brush.DrawAs = ESlateBrushDrawType::Image; break;
		case 5: Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox; break;
		}
		VisualMapper::ApplyStroke(Widget, Stroke);
		const ESlateBrushDrawType::Type Expected = Variant == 4 ? ESlateBrushDrawType::Image
			: (Variant == 0 || Variant == 5 ? ESlateBrushDrawType::RoundedBox : ESlateBrushDrawType::Box);
		TestEqual(FString::Printf(TEXT("Stroke brush selection variant %d"), Variant),
			Widget.Brush.DrawAs.Get(ESlateBrushDrawType::NoDrawType), Expected);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

