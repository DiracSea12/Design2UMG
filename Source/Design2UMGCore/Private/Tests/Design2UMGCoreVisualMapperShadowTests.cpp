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

#endif // WITH_DEV_AUTOMATION_TESTS

