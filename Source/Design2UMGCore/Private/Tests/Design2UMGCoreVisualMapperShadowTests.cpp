// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// VisualMapper box shadow family: texture extent clamp, render ownership, multi-layer single overlay.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DesignSchema.h"
#include "UMGSchema.h"
#include "VisualMapper.h"
#include "Design2UMGCoreTestHelpers.h"

using namespace Design2UMGCoreTests;


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
	// Shape grows by spread on both sides (10 + 2*3), then a 3-sigma falloff margin
	// (1.5 * 2 * 2) is added per side: 16 + 6 = 22 wide, 26 + 6 = 32 tall.
	TestEqual(TEXT("Normal finite width keeps the rounded result"), NormalWrapped.Children[0].WidgetStyle.WidthOverride.Get(0), 22.0f);
	TestEqual(TEXT("Normal finite height keeps the rounded result"), NormalWrapped.Children[0].WidgetStyle.HeightOverride.Get(0), 32.0f);

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
	if (!TestTrue(TEXT("Typed shape payload is present"), Shadow.Brush.Shape.IsSet())) return false;
	const auto& Shape = Shadow.Brush.Shape.GetValue();
	TestFalse(TEXT("Generated shadow carries no JSON parameters"), Shadow.Brush.MaterialParams.IsSet());
	TestEqual(TEXT("Box shadow is the first overlay child"), Shadow.Type, FString(TEXT("Image")));
	TestTrue(TEXT("Precoloured shadow texture uses a neutral image tint"),
		Shadow.Brush.BrushColor.IsSet()
		&& Shadow.Brush.BrushColor.GetValue().Equals(FLinearColor::White));
	TestTrue(TEXT("Shadow colour is retained in texture generation parameters"),
		Shape.Color.IsSet() && Shape.Color->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#000000FF")))));
	TestTrue(TEXT("Shadow fills with negative padding so it can extend past the wrapper, and keeps the raw source offset"),
		Shadow.Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill
			&& Shadow.Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill
			&& Shadow.Slot.Padding == FMargin(-48.0f)
			&& Shadow.Render.RenderTranslation.IsSet()
			&& Shadow.Render.RenderTranslation.GetValue().Equals(FVector2D(4.0f, -5.0f), 0.001f));
	TestEqual(TEXT("Source clipping moves to the layout wrapper"),
		Wrapped.Render.Clipping.Get(FString()), FString(TEXT("ClipToBounds")));
	TestTrue(TEXT("Shadow texture carries the 3-sigma blur padding"),
		Shape.BlurPadding.IsSet() && Shape.BlurPadding.GetValue() == 48.0f);
	TestTrue(TEXT("Non-uniform corners normalize against the spread-grown shape half-min"),
		Shape.CornerRadii.IsSet() && Shape.CornerRadii.GetValue() == FVector4(0.216f, 0.324f, 0.432f, 0.541f));

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
	if (!TestTrue(TEXT("Typed shape payload is present"), UniformWrapped.Children[0].Brush.Shape.IsSet())) return false;
	const auto& UniformShape = UniformWrapped.Children[0].Brush.Shape.GetValue();
	TestTrue(TEXT("Uniform corner radius normalizes against the spread-grown shape half-min"),
		UniformShape.CornerRadius.IsSet() && UniformShape.CornerRadius.GetValue() == 0.27f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperBoxShadowDesiredSizeContractTest,
	"Design2UMG.VisualMapper.BoxShadowDesiredSizeContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperBoxShadowDesiredSizeContractTest::RunTest(const FString& Parameters)
{
	// Geometry contract of the shadow layer: the falloff must be able to extend
	// beyond the wrapper, so the texture is bigger than the source shape and the
	// slot stays Fill with negative margins — SOverlay clamps aligned children
	// to the panel rect, so only the fill branch's unclamped negative-padding
	// rect lets the image extend past the wrapper.
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Rectangle, 139.0f, 39.0f);
	Node->Visual.CornerRadius.bUniform = true;
	Node->Visual.CornerRadius.Uniform = 12.0f;

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Image");
	Widget.Id = TEXT("neumorphic_source");
	Widget.Name = TEXT("NeumorphicSource");

	FUMGBoxShadowLayerData ShadowLayer;
	ShadowLayer.Color = FLinearColor::Gray;
	ShadowLayer.Offset = FVector2D(5.0f, 5.0f);
	ShadowLayer.Blur = 10.0f;
	ShadowLayer.Spread = 5.0f;
	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, TArray<FUMGBoxShadowLayerData>{ ShadowLayer });

	TestEqual(TEXT("Neumorphic-style shadow has one overlay wrapper"), Wrapped.Children.Num(), 2);
	if (Wrapped.Children.Num() != 2)
	{
		return false;
	}

	const FUMGWidgetNode& Shadow = Wrapped.Children[0];
	if (!TestTrue(TEXT("Typed shape payload is present"), Shadow.Brush.Shape.IsSet())) return false;
	const auto& Shape = Shadow.Brush.Shape.GetValue();
	// Shape grows by spread (139 + 10, 39 + 10), plus a 3-sigma falloff margin
	// (1.5 * 10 * 2) per side: 149 + 30 = 179 wide, 49 + 30 = 79 tall.
	TestEqual(TEXT("Shadow texture covers shape plus falloff on both axes"),
		Shadow.WidgetStyle.WidthOverride.Get(0), 179.0f);
	TestEqual(TEXT("Shadow texture height covers shape plus falloff"),
		Shadow.WidgetStyle.HeightOverride.Get(0), 79.0f);
	// The slot stays Fill but carries a negative padding: SOverlay clamps
	// aligned children to the panel rect (AlignChild bClampToParent), so a
	// centered image could never be larger than the wrapper. The fill branch
	// has no clamp — the -15 margins grow the allotted rect to 179x79 centered
	// on the 139x39 wrapper, letting the falloff render outside it.
	TestTrue(TEXT("Shadow slot fills with negative blur-padding margins"),
		Shadow.Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill
			&& Shadow.Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill
			&& Shadow.Slot.Padding == FMargin(-15.0f));
	// Translation is the raw source offset: the padded fill rect already
	// centers the texture on the wrapper, so only the offset remains.
	TestTrue(TEXT("Shadow translation carries the raw offset"),
		Shadow.Render.RenderTranslation.IsSet()
		&& Shadow.Render.RenderTranslation.GetValue().Equals(FVector2D(5.0f, 5.0f), 0.001f));
	// The falloff margin travels inside the brush params so the generator can
	// inset the shape by exactly that amount.
	TestTrue(TEXT("Blur padding rides along in the typed shape"),
		Shape.BlurPadding.IsSet() && Shape.BlurPadding.GetValue() == 15.0f);
	// Corner radius grows with spread, normalized against the shape half-min
	// ((12 + 5) / (49 * 0.5)).
	TestTrue(TEXT("Corner radius grows with spread"),
		Shape.CornerRadius.IsSet() && Shape.CornerRadius.GetValue() == 0.694f);

	// A pure offset shadow (no blur, no spread) keeps the exact source size and
	// a zero blur padding, so the generator draws a hard shape.
	FUMGBoxShadowLayerData HardShadow;
	HardShadow.Color = FLinearColor::Black;
	HardShadow.Offset = FVector2D(-5.0f, -5.0f);
	FUMGWidgetNode HardWidget;
	HardWidget.Type = TEXT("Image");
	HardWidget.Id = TEXT("hard_shadow_source");
	HardWidget.Name = TEXT("Hard Shadow Source");
	const FUMGWidgetNode HardWrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(HardWidget), Node, TArray<FUMGBoxShadowLayerData>{ HardShadow });
	if (!TestTrue(TEXT("Typed shape payload is present"), HardWrapped.Children[0].Brush.Shape.IsSet())) return false;
	const auto& HardShape = HardWrapped.Children[0].Brush.Shape.GetValue();
	TestEqual(TEXT("Hard shadow keeps the source width"),
		HardWrapped.Children[0].WidgetStyle.WidthOverride.Get(0), 139.0f);
	TestEqual(TEXT("Hard shadow keeps the source height"),
		HardWrapped.Children[0].WidgetStyle.HeightOverride.Get(0), 39.0f);
	TestTrue(TEXT("Hard shadow pads with zero"),
		HardShape.BlurPadding.IsSet() && HardShape.BlurPadding.GetValue() == 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperBoxShadowAutoSizeStretchContractTest,
	"Design2UMG.VisualMapper.BoxShadowAutoSizeStretchContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperBoxShadowAutoSizeStretchContractTest::RunTest(const FString& Parameters)
{
	// Fill/Fit sizing declares no extent: the wrapper is stretched at runtime,
	// so an exact shadow texture cannot be pre-baked (the declared value is 0
	// and the shape would collapse to a pixel, stretching into a smear). Those
	// nodes keep the stretch-to-wrapper layer: no BlurPadding inset (the shape
	// fills the texture), no negative margins, and the legacy translation.
	TSharedPtr<FDesignNode> Node = Design2UMGCoreTests::MakeNode(
		FString(), EDesignNodeType::Frame, FDesignSize::Fill(), FDesignSize::Fit());
	Node->Visual.CornerRadius.bUniform = true;
	Node->Visual.CornerRadius.Uniform = 20.0f;

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Image");
	Widget.Id = TEXT("fill_card_source");
	Widget.Name = TEXT("FillCardSource");

	FUMGBoxShadowLayerData ShadowLayer;
	ShadowLayer.Color = FLinearColor::Black;
	ShadowLayer.Offset = FVector2D(0.0f, 4.0f);
	ShadowLayer.Blur = 12.0f;
	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, TArray<FUMGBoxShadowLayerData>{ ShadowLayer });

	TestEqual(TEXT("Auto-size shadow keeps the overlay wrapper"), Wrapped.Children.Num(), 2);
	if (Wrapped.Children.Num() != 2)
	{
		return false;
	}

	const FUMGWidgetNode& Shadow = Wrapped.Children[0];
	if (!TestTrue(TEXT("Typed shape payload is present"), Shadow.Brush.Shape.IsSet())) return false;
	const auto& Shape = Shadow.Brush.Shape.GetValue();
	// Collapsed declared shape (1x1) plus the legacy blur extent (12 * 2):
	// the texture is a placeholder that stretches to the wrapper rect.
	TestEqual(TEXT("Auto-size shadow texture uses the legacy extent width"),
		Shadow.WidgetStyle.WidthOverride.Get(0), 25.0f);
	TestEqual(TEXT("Auto-size shadow texture uses the legacy extent height"),
		Shadow.WidgetStyle.HeightOverride.Get(0), 25.0f);
	TestTrue(TEXT("Auto-size shadow fills without negative margins"),
		Shadow.Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill
			&& Shadow.Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill
			&& Shadow.Slot.Padding == FMargin(0.0f));
	TestTrue(TEXT("Auto-size shadow keeps the legacy extent translation"),
		Shadow.Render.RenderTranslation.IsSet()
			&& Shadow.Render.RenderTranslation.GetValue().Equals(FVector2D(-12.0f, -8.0f), 0.001f));
	// No BlurPadding value: the generator stretches the shape across the whole
	// texture so the shadow tracks the wrapper.
	TestFalse(TEXT("Auto-size shadow omits the blur padding inset"),
		Shape.BlurPadding.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperBoxShadowFitEstimatePreciseContractTest,
	"Design2UMG.VisualMapper.BoxShadowFitEstimatePreciseContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperBoxShadowFitEstimatePreciseContractTest::RunTest(const FString& Parameters)
{
	// Fit sizes whose Value was promoted by SizeEstimator carry the resolved
	// content estimate (neumorphic's root lands here as Fit(139 x 39)), so they
	// must take the precise geometry — gating on IsFixed() alone regressed that
	// case back to baseline. The layer contract must match the Fixed path
	// exactly: same texture, BlurPadding inset, negative margins and raw offset.
	TSharedPtr<FDesignNode> Node = Design2UMGCoreTests::MakeNode(
		FString(), EDesignNodeType::Rectangle, FDesignSize::Fit(139.0f), FDesignSize::Fit(39.0f));
	Node->Visual.CornerRadius.bUniform = true;
	Node->Visual.CornerRadius.Uniform = 12.0f;

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Image");
	Widget.Id = TEXT("fit_estimate_source");
	Widget.Name = TEXT("FitEstimateSource");

	FUMGBoxShadowLayerData ShadowLayer;
	ShadowLayer.Color = FLinearColor::Gray;
	ShadowLayer.Offset = FVector2D(5.0f, 5.0f);
	ShadowLayer.Blur = 10.0f;
	ShadowLayer.Spread = 5.0f;
	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, TArray<FUMGBoxShadowLayerData>{ ShadowLayer });

	TestEqual(TEXT("Fit-estimate shadow keeps the overlay wrapper"), Wrapped.Children.Num(), 2);
	if (Wrapped.Children.Num() != 2)
	{
		return false;
	}

	const FUMGWidgetNode& Shadow = Wrapped.Children[0];
	if (!TestTrue(TEXT("Typed shape payload is present"), Shadow.Brush.Shape.IsSet())) return false;
	const auto& Shape = Shadow.Brush.Shape.GetValue();
	// Identical to the Fixed-path numbers: shape 139 + 2*5 spread + 2*15 falloff
	// = 179 wide, 39 + 10 + 30 = 79 tall.
	TestEqual(TEXT("Fit-estimate shadow texture matches the precise path width"),
		Shadow.WidgetStyle.WidthOverride.Get(0), 179.0f);
	TestEqual(TEXT("Fit-estimate shadow texture matches the precise path height"),
		Shadow.WidgetStyle.HeightOverride.Get(0), 79.0f);
	TestTrue(TEXT("Fit-estimate shadow pads with negative falloff margins"),
		Shadow.Slot.Padding == FMargin(-15.0f));
	TestTrue(TEXT("Fit-estimate shadow translation carries the raw offset"),
		Shadow.Render.RenderTranslation.IsSet()
			&& Shadow.Render.RenderTranslation.GetValue().Equals(FVector2D(5.0f, 5.0f), 0.001f));
	TestTrue(TEXT("Fit-estimate shadow carries the blur padding inset"),
		Shape.BlurPadding.IsSet() && Shape.BlurPadding.GetValue() == 15.0f);
	TestTrue(TEXT("Fit-estimate corner radius matches the precise path"),
		Shape.CornerRadius.IsSet() && Shape.CornerRadius.GetValue() == 0.694f);
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
	if (!TestTrue(TEXT("Typed shape payload is present"), Wrapped.Children[0].Brush.Shape.IsSet())) return false;
	const auto& LayerShape = Wrapped.Children[0].Brush.Shape.GetValue();
	TestEqual(TEXT("Second shadow layer renders below the first"), Wrapped.Children[0].Id, FString(TEXT("multi_shadow_source_shadow1")));
	TestEqual(TEXT("First shadow layer renders above the second"), Wrapped.Children[1].Id, FString(TEXT("multi_shadow_source_shadow0")));
	TestTrue(TEXT("Second shadow layer keeps its own baked colour without a second tint"),
		Wrapped.Children[0].Brush.BrushColor.IsSet()
		&& Wrapped.Children[0].Brush.BrushColor.GetValue().Equals(FLinearColor::White)
		&& LayerShape.Color.IsSet() && LayerShape.Color->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#FF0000FF")))));
	TestEqual(TEXT("Content stays on top with a single content suffix"), Wrapped.Children[2].Id, FString(TEXT("multi_shadow_source_content")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperSubtreePaintBoundsTest,
	"Design2UMG.VisualMapper.SubtreePaintBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperSubtreePaintBoundsTest::RunTest(const FString&)
{
	const auto Root = MakeFixedNode(EDesignNodeType::Frame, 100, 80);
	const auto Middle = MakeFixedNode(EDesignNodeType::Frame, 20, 20);
	const auto Leaf = MakeFixedNode(EDesignNodeType::Rectangle, 20, 20);
	Root->LayoutData.Layout = Middle->LayoutData.Layout = EDesignLayout::None;
	Middle->Transform.SetPosition(80, 60);
	FDesignEffect Effect;
	Effect.Type = EDesignEffectType::Shadow;
	Effect.Shadow.OffsetY = 20;
	Leaf->Visual.Effects.Add(Effect);
	Middle->Children.Add(Leaf);
	Root->Children.Add(Middle);
	FUMGBoxShadowLayerData Shadow;
	Shadow.Offset.X = 100;
	const auto CapturePadding = [&]()
	{
		FUMGWidgetNode Widget;
		Widget.Type = TEXT("CanvasPanel");
		Widget.Bounds.W = 100;
		Widget.Bounds.H = 80;
		const FUMGWidgetNode Wrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Root, {Shadow});
		TestEqual(TEXT("Source layout width unchanged"), Wrapped.Bounds.W, 100.0f);
		TestEqual(TEXT("Source layout height unchanged"), Wrapped.Bounds.H, 80.0f);
		return Wrapped.Children[0].Slot.Padding;
	};
	TestEqual(TEXT("Nested descendant paints beyond layout bottom"), CapturePadding().Bottom, -21.0f);
	Middle->LayoutData.SetClipValue(true);
	TestEqual(TEXT("Clipped descendant does not enlarge capture"), CapturePadding().Bottom, -1.0f);
	Middle->LayoutData.SetClipValue(false);
	Middle->Transform.SetPosition(0, 0);
	Middle->Transform.Rotation = 90;
	// The parent shadow is displaced entirely rightwards, so it adds no left gutter.
	TestEqual(TEXT("Rotation moves descendant overflow to the left"), CapturePadding().Left, -20.0f);
	Middle->Transform.Rotation = 0;
	TestEqual(TEXT("Contained absolute children need no extra bottom padding"), CapturePadding().Bottom, -1.0f);
	Root->LayoutData.Layout = EDesignLayout::Horizontal;
	TestEqual(TEXT("Flow children reserve outsets at their eventual layout edge"), CapturePadding().Bottom, -21.0f);
	Root->LayoutData.Layout = EDesignLayout::None;
	Root->Visual.Stroke.Align = EDesignStrokeAlign::Outside;
	Root->Visual.Stroke.Thickness.Uniform = 6;
	TestEqual(TEXT("Outside stroke remains inside the capture"), CapturePadding().Left, -6.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

