// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// VisualMapper multi-fill and side-stroke surface family: fill-owned surfaces, empty/padded content elision and promotion.

#if WITH_DEV_AUTOMATION_TESTS

#include "Design2UMGConverterVisualSupport.h"
#include "Design2UMGGeneratedNameSuffixes.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "DesignSchema.h"
#include "UMGSchema.h"
#include "UMGPropertyKeys.h"
#include "VisualMapper.h"
#include "VisualMapperFillTransfer.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperMultiFillIntegrationTest,
	"Design2UMG.VisualMapper.MultiFillSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperMultiFillIntegrationTest::RunTest(const FString& Parameters)
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
	"Design2UMG.VisualMapper.MultiFillElidesEmptyBorderContent",
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
	"Design2UMG.VisualMapper.MultiFillPromotesBorderContent",
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
	"Design2UMG.VisualMapper.MultiFillElidesEmptyPanelContent",
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
	"Design2UMG.VisualMapper.MultiFillKeepsPaddedBorderContent",
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
	Border.WidgetStyle.Padding = FMargin(8.0f);
	FUMGBoxShadowLayerData& BoxShadow = Border.Shadow.BoxShadows.AddDefaulted_GetRef();
	BoxShadow.Spread = 3.0f;

	FUMGWidgetNode Content;
	Content.Type = TEXT("HorizontalBox");
	Content.Id = TEXT("multi_fill_padded_inner");
	Content.Name = TEXT("MultiFillPadded_inner");
	Border.Children.Add(MoveTemp(Content));

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithMultiFill(MoveTemp(Border), Node, FVisualMapperOptions());
	TestEqual(TEXT("Multi-fill overlay owns the node box shadow"), Wrapped.Shadow.BoxShadows.Num(), 1);
	if (Wrapped.Shadow.BoxShadows.Num() == 1)
	{
		TestEqual(TEXT("Multi-fill overlay preserves box-shadow data"), Wrapped.Shadow.BoxShadows[0].Spread, 3.0f);
	}
	TestEqual(TEXT("Padded multi-fill keeps paint layers plus Border carrier"), Wrapped.Children.Num(), 3);
	if (Wrapped.Children.Num() != 3)
	{
		return false;
	}
	TestEqual(TEXT("Padding remains owned by Border"), Wrapped.Children[2].Type, FString(TEXT("Border")));
	TestTrue(TEXT("Padding remains present"), Wrapped.Children[2].WidgetStyle.Padding.IsSet());
	TestTrue(TEXT("Content carrier has an explicit transparent fill"),
		Wrapped.Children[2].Brush.BrushColor.IsSet()
		&& Wrapped.Children[2].Brush.BrushColor.GetValue().Equals(FLinearColor::Transparent));
	TestEqual(TEXT("Retained content does not duplicate the box shadow"), Wrapped.Children[2].Shadow.BoxShadows.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperShaderFillKeepsContentTransparentTest,
	"Design2UMG.VisualMapper.ShaderFillKeepsContentTransparent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperShaderFillKeepsContentTransparentTest::RunTest(const FString& Parameters)
{
	const FString ShaderFile = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("FillSurface_"), TEXT(".glsl"));
	if (!TestTrue(TEXT("Write fill shader fixture"), FFileHelper::SaveStringToFile(
		TEXT("void main(){gl_FragColor=vec4(0.1,0.2,0.3,1.0);}"), *ShaderFile,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
	{
		return false;
	}
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*ShaderFile); };

	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 96.0f, 48.0f);
	FDesignFill Fill;
	Fill.Type = EDesignFillType::Shader;
	Fill.Shader.Emplace();
	Fill.Shader->Url = ShaderFile;
	Node->Visual.Fills.Add(MoveTemp(Fill));
	Node->Visual.CornerRadius.bUniform = true;
	Node->Visual.CornerRadius.Uniform = 4.0f;
	Node->Visual.Stroke.Thickness.bUniform = true;
	Node->Visual.Stroke.Thickness.Uniform = 2.0f;
	Node->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#112233FF"));
	Node->LayoutData.Layout = EDesignLayout::None;
	Node->LayoutData.ExplicitClip = true;

	FUMGWidgetNode Content;
	Content.Type = TEXT("CanvasPanel");
	Content.Id = TEXT("shader_surface");
	Content.Name = TEXT("ShaderSurface");
	Content.Bounds.W = 96.0f;
	Content.Bounds.H = 48.0f;
	FUMGWidgetNode Text;
	Text.Type = TEXT("TextBlock");
	Text.Text.Text = TEXT("Content above fill");
	Content.Children.Add(MoveTemp(Text));
	VisualMapper::ApplyVisualProperties(Content, Node);
	FUMGWidgetNode Border = VisualMapper::WrapWithBorder(MoveTemp(Content), Node);
	TestFalse(TEXT("Production shader carrier starts without a brush color"), Border.Brush.BrushColor.IsSet());
	TestFalse(TEXT("Plain fill shader needs no backdrop layers"), VisualMapperFillTransfer::RequiresBackdropLayers(Border, Node));

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithMultiFill(MoveTemp(Border), Node, FVisualMapperOptions());
	if (!TestEqual(TEXT("Shader fill precedes its retained content carrier"), Wrapped.Children.Num(), 2))
	{
		return false;
	}
	const FUMGWidgetNode& Paint = Wrapped.Children[0];
	const FUMGWidgetNode& Carrier = Wrapped.Children[1];
	TestTrue(TEXT("Paint layer owns the shader"), Paint.Brush.Shader.IsSet());
	TestEqual(TEXT("Paint layer retains the authored corner"), Paint.Brush.OutlineCornerRadius.Get(0.0f), 4.0f);
	TestEqual(TEXT("Content remains a Border"), Carrier.Type, FString(TEXT("Border")));
	TestTrue(TEXT("Content fill is explicitly transparent instead of engine-default white"),
		Carrier.Brush.BrushColor.IsSet() && Carrier.Brush.BrushColor.GetValue().Equals(FLinearColor::Transparent));
	TestFalse(TEXT("Content does not duplicate the shader"), Carrier.Brush.Shader.IsSet());
	TestEqual(TEXT("Content carrier retains its independent outline width"), Carrier.Brush.OutlineThickness.Get(0.0f), 2.0f);
	TestTrue(TEXT("Content carrier retains its independent outline color"),
		Carrier.Brush.OutlineColor.IsSet()
		&& Carrier.Brush.OutlineColor.GetValue().Equals(FLinearColor(FColor(0x11, 0x22, 0x33))));
	TestEqual(TEXT("Content carrier retains rounded outline drawing"),
		Carrier.Brush.DrawAs.Get(ESlateBrushDrawType::NoDrawType), ESlateBrushDrawType::RoundedBox);
	TestEqual(TEXT("Content width hint is preserved"), Carrier.WidgetStyle.WidthOverride.Get(0.0f), 96.0f);
	TestEqual(TEXT("Content height hint is preserved"), Carrier.WidgetStyle.HeightOverride.Get(0.0f), 48.0f);
	TestEqual(TEXT("Clipping remains on the shared overlay"), Wrapped.Render.Clipping.Get(FString()), FString(TEXT("ClipToBounds")));
	TestTrue(TEXT("Content panel and text remain intact"), Carrier.Children.Num() == 1
		&& Carrier.Children[0].Type == TEXT("CanvasPanel") && Carrier.Children[0].Children.Num() == 1
		&& Carrier.Children[0].Children[0].Text.Text.Get(FString()) == TEXT("Content above fill"));
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


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperBackdropPaintLayersTest,
    "Design2UMG.VisualMapper.BackdropPaintLayers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperBackdropPaintLayersTest::RunTest(const FString&)
{
    const FString ShaderFile = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("PencilBackgroundUniform_"), TEXT(".glsl"));
    const FString ShaderSource = TEXT(R"(#version 100
precision mediump float;
/** @default pencil:background */
uniform sampler2D Back;
void main(){gl_FragColor=texture2D(Back,vec2(0.5));}
)");
    if (!TestTrue(TEXT("Write annotation fixture"), FFileHelper::SaveStringToFile(ShaderSource, *ShaderFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
        return false;
    ON_SCOPE_EXIT { IFileManager::Get().Delete(*ShaderFile); };

    auto Node = MakeShared<FDesignNode>();
    Node->Type = EDesignNodeType::Frame; Node->Id = TEXT("surface"); Node->Name = TEXT("Surface");
    Node->Transform.SetWidthValue(FDesignSize::Fixed(96)); Node->Transform.SetHeightValue(FDesignSize::Fixed(64));
    FDesignFill Fill; Fill.Type = EDesignFillType::Color; Fill.SolidColor = TEXT("#FFFF00FF");
    Node->Visual.Fills.Add(Fill);
    FUMGWidgetNode Container; Container.Type = TEXT("CanvasPanel"); Container.Id = Node->Id; Container.Name = Node->Name;
    Container.Render.RenderOpacity = .6f;
    Container.Brush.OutlineThickness = 2.0f;
    FUMGWidgetNode Child; Child.Type = TEXT("Image"); Child.Brush.Shader.Emplace();
    Child.Brush.Shader->Url = ShaderFile;
    FDesignShaderValue Background; Background.Kind = FDesignShaderValue::EKind::String; Background.String = TEXT("pencil:background");
    Child.Brush.Shader->Uniforms.Add(TEXT("Back"), Background); Container.Children.Add(Child);
    TestTrue(TEXT("Background descendant requires explicit ancestor paint"), VisualMapperFillTransfer::RequiresBackdropLayers(Container, Node));
    FUMGWidgetNode Layered = VisualMapper::WrapWithMultiFill(MoveTemp(Container), Node, FVisualMapperOptions());
    TestEqual(TEXT("Ancestor has paint and original content"), Layered.Children.Num(), 2);
    if (Layered.Children.Num() == 2)
    {
        TestEqual(TEXT("Real child retained once"), Layered.Children[1].Children.Num(), 1);
        TestEqual(TEXT("Layout type retained"), Layered.Children[1].Type, FString(TEXT("CanvasPanel")));
        TestTrue(TEXT("Transferred fill keeps the independent native outline"), Layered.Children[1].Brush.OutlineThickness.IsSet() && FMath::IsNearlyEqual(Layered.Children[1].Brush.OutlineThickness.GetValue(), 2.0f));
        TestTrue(TEXT("Source opacity stays on group"), Layered.Render.RenderOpacity.IsSet() && FMath::IsNearlyEqual(Layered.Render.RenderOpacity.GetValue(), .6f));
    }
    FUMGWidgetNode ShadowContainer;
    ShadowContainer.Type = TEXT("CanvasPanel");
    FUMGWidgetNode ShadowChild;
    ShadowChild.Shadow.BoxShadows.AddDefaulted_GetRef().BlendMode = TEXT("multiply");
    ShadowContainer.Children.Add(MoveTemp(ShadowChild));
    TestTrue(TEXT("Subtree blended shadow requires ancestor paint"), VisualMapperFillTransfer::RequiresBackdropLayers(ShadowContainer, Node));

    auto OutsetChild = MakeShared<FDesignNode>();
    OutsetChild->Id = TEXT("outset-child");
    OutsetChild->Transform.X = -8;
    OutsetChild->Transform.SetWidthValue(FDesignSize::Fixed(16));
    OutsetChild->Transform.SetHeightValue(FDesignSize::Fixed(16));
    Node->Children.Add(OutsetChild);
    VisualMapperFillTransfer::MarkBackdropProducers(Layered, Node);
    TestTrue(TEXT("Context opacity group retains source capture outsets"), Layered.Render.BackgroundCaptureInsets.IsSet() && FMath::IsNearlyEqual(Layered.Render.BackgroundCaptureInsets->Left, 8.0f));
    TestTrue(TEXT("Only preceding paint is a producer and keeps source outsets"),
        Layered.Children.Num() == 2 && Layered.Children[0].Render.BackgroundCaptureInsets.IsSet()
        && FMath::IsNearlyEqual(Layered.Children[0].Render.BackgroundCaptureInsets->Left, 8.0f)
        && !Layered.Children[1].Render.BackgroundCaptureInsets.IsSet());

    auto FractionalNode = MakeFixedNode(EDesignNodeType::Frame, 96.0f, 64.0f);
    FractionalNode->Id = Node->Id;
    FractionalNode->Visual.Stroke.Align = EDesignStrokeAlign::Center;
    FractionalNode->Visual.Stroke.Thickness.bUniform = true;
    FractionalNode->Visual.Stroke.Thickness.Uniform = 1.0f;
    VisualMapperFillTransfer::MarkBackdropProducers(Layered, FractionalNode);
    TestTrue(TEXT("Context capture rounds half-pixel stroke outsets outward"),
        Layered.Render.BackgroundCaptureInsets.Get(FMargin(0)) == FMargin(1));
    TestTrue(TEXT("Preceding paint receives whole-pixel capture outsets without capturing later content"),
        Layered.Children.Num() == 2
        && Layered.Children[0].Render.BackgroundCaptureInsets.Get(FMargin(0)) == FMargin(1)
        && !Layered.Children[1].Render.BackgroundCaptureInsets.IsSet());

    const auto HalfGroup = []()
    {
        FUMGWidgetNode Group;
        Group.Type = TEXT("Overlay");
        Group.Render.RenderOpacity = .5f;
        FUMGWidgetNode Paint;
        Paint.Type = TEXT("Image");
        Paint.Brush.BrushColor = FLinearColor::Red;
        Group.Children.Add(Paint);
        Paint.Brush.BrushColor = FLinearColor::Blue;
        Group.Children.Add(MoveTemp(Paint));
        return Group;
    };
    FUMGWidgetNode CaptureTree;
    CaptureTree.Type = TEXT("Overlay");
    FUMGWidgetNode Prefix;
    Prefix.Type = TEXT("Overlay");
    Prefix.Children.Add(HalfGroup());
    CaptureTree.Children.Add(MoveTemp(Prefix));
    FUMGWidgetNode ContextSource;
    ContextSource.Type = TEXT("RetainerBox");
    ContextSource.Effect.Shader = Child.Brush.Shader;
    ContextSource.Children.Add(HalfGroup());
    CaptureTree.Children.Add(MoveTemp(ContextSource));
    CaptureTree.Children.Add(HalfGroup());
    VisualMapperFillTransfer::MarkBackdropProducers(CaptureTree, Node);
    TestTrue(TEXT("Internal opacity groups are captured only in real prefix and source branches"),
        CaptureTree.Children[0].Children[0].Render.BackgroundCaptureInsets.IsSet()
        && CaptureTree.Children[1].Children[0].Render.BackgroundCaptureInsets.IsSet()
        && !CaptureTree.Children[2].Render.BackgroundCaptureInsets.IsSet());
    CaptureTree.Render.RenderOpacity = .6f;
    VisualMapperFillTransfer::MarkBackdropProducers(CaptureTree, Node);
    TestTrue(TEXT("Final opacity capture includes its later internal groups"), CaptureTree.Children[2].Render.BackgroundCaptureInsets.IsSet());

    FUMGWidgetNode DefaultContainer;
    DefaultContainer.Type = TEXT("CanvasPanel");
    DefaultContainer.Children.Add(Child);
    auto& DefaultShader = DefaultContainer.Children[0].Brush.Shader.GetValue();
    DefaultShader.Url = ShaderFile;
    DefaultShader.Uniforms.Reset();
    TestTrue(TEXT("Official block-comment background default requires ancestor paint"),
        VisualMapperFillTransfer::RequiresBackdropLayers(DefaultContainer, Node));
    FDesignShaderValue ImageOverride;
    ImageOverride.Kind = FDesignShaderValue::EKind::String;
    ImageOverride.String = TEXT("texture.png");
    DefaultShader.Uniforms.Add(TEXT("Back"), ImageOverride);
    TestFalse(TEXT("Explicit sampler value overrides its background default"),
        VisualMapperFillTransfer::RequiresBackdropLayers(DefaultContainer, Node));

    Node->Visual.Fills[0].BlendMode = TEXT("overlay");
    FUMGWidgetNode Paint; Paint.Type = TEXT("Border"); Paint.Id = Node->Id; Paint.Name = Node->Name;
    FUMGWidgetNode Blended = VisualMapper::WrapWithMultiFill(MoveTemp(Paint), Node, FVisualMapperOptions());
    TestTrue(TEXT("Ordinary blend reaches independent paint"), !Blended.Children.IsEmpty() && Blended.Children[0].Render.PaintBlendMode.Get(FString()) == TEXT("overlay"));
    FUMGWidgetNode Plain; Plain.Type = TEXT("CanvasPanel"); Node->Visual.Fills[0].BlendMode.Reset();
    TestFalse(TEXT("Plain single fill is unchanged"), VisualMapperFillTransfer::RequiresBackdropLayers(Plain, Node));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperGlyphShadowBlendTransferTest,
    "Design2UMG.VisualMapper.GlyphShadowBlendTransfer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperGlyphShadowBlendTransferTest::RunTest(const FString&)
{
    FDesignFill Fill = FDesignFill::FromColor(TEXT("#FFFF00FF"));
    auto TextNode = MakeShared<FDesignNode>();
    TextNode->Type = EDesignNodeType::Text;
    TextNode->Id = TEXT("shadow-text");
    TextNode->Name = TEXT("ShadowText");
    TextNode->Visual.Fills.Add(Fill);
    TextNode->Transform.SetWidthValue(FDesignSize::Fixed(96));
    TextNode->Transform.SetHeightValue(FDesignSize::Fixed(24));
    FDesignEffect TextShadow;
    TextShadow.Type = EDesignEffectType::Shadow;
    TextShadow.Shadow.Color = TEXT("#000000FF");
    TextShadow.Shadow.Spread = 3.0f;
    TextShadow.Shadow.Blur = 4.0f;
    TextShadow.Shadow.OffsetX = 3.0f;
    TextShadow.Shadow.OffsetY = 2.0f;
    TextNode->Visual.Effects.Add(TextShadow);
    TextShadow.BlendMode = TEXT("MULTIPLY");
    TextNode->Visual.Effects.Add(TextShadow);
    FUMGWidgetNode TextPaint;
    TextPaint.Type = TEXT("TextBlock");
    TextPaint.Id = TextNode->Id;
    TextPaint.Name = TextNode->Name;
    VisualMapper::ApplyVisualProperties(TextPaint, TextNode);
    TestFalse(TEXT("Glyph color stays on the native text paint"), VisualMapperFillTransfer::RequiresBackdropLayers(TextPaint, TextNode));
    TestTrue(TEXT("Text shadows retain the complete stack and canonicalize only UMG mode"),
        TextPaint.Shadow.BoxShadows.Num() == 2 && TextPaint.Shadow.BoxShadows[1].BlendMode == TEXT("multiply")
        && TextNode->Visual.Effects[1].BlendMode == TEXT("MULTIPLY"));
    const auto TextShadowLayers = TextPaint.Shadow.BoxShadows;
    TestTrue(TEXT("Glyph shadow keeps legal blur/offset and ignores unsupported spread"),
        !TextShadowLayers.ContainsByPredicate([](const FUMGBoxShadowLayerData& Layer) { return !FMath::IsNearlyZero(Layer.Spread); })
        && TextShadowLayers.Num() == 2 && TextShadowLayers[0].Blur == 4.0f
        && TextShadowLayers[0].Offset == FVector2D(3.0f, 2.0f));
    TextPaint.Shadow.BoxShadows.Reset();
    FUMGWidgetNode TextGroup = VisualMapper::WrapWithBoxShadow(MoveTemp(TextPaint), TextNode, TextShadowLayers);
    TestTrue(TEXT("Blended text shadows use a single native subtree capture"),
        !TextGroup.Children.IsEmpty() && TextGroup.Children[0].Shadow.BoxShadows.Num() == 2);

    TextNode->Visual.Effects[1].BlendMode = TEXT("NORMAL");
    FUMGWidgetNode PlainText;
    PlainText.Type = TEXT("TextBlock");
    VisualMapper::ApplyVisualProperties(PlainText, TextNode);
    TestTrue(TEXT("Normal-only text shadows keep the original native route"), PlainText.Shadow.BoxShadows.IsEmpty());

    TextNode->Visual.Fills[0].BlendMode = TEXT("MULTIPLY");
    FDesignEffect Inner;
    Inner.Type = EDesignEffectType::Shadow;
    Inner.BlendMode = TEXT("SCREEN");
    Inner.Shadow.BlendMode = Inner.BlendMode;
    Inner.Shadow.bInner = true;
    Inner.Shadow.Color = TEXT("#FF000080");
    Inner.Shadow.Blur = 3.0f;
    Inner.Shadow.OffsetX = 2.0f;
    Inner.Shadow.OffsetY = 1.0f;
    TextNode->Visual.Effects.Add(Inner);
    FUMGWidgetNode MixedText;
    MixedText.Type = TEXT("TextBlock");
    MixedText.Id = TextNode->Id;
    MixedText.Name = TextNode->Name;
    VisualMapper::ApplyVisualProperties(MixedText, TextNode);
    FUMGWidgetNode MixedGroup = Design2UMGConverterVisualSupport::ApplyVisualEffectWrappers(
        MoveTemp(MixedText), TextNode, FVisualMapperOptions(), false);
    const FUMGWidgetNode* SharedShadow = nullptr;
    int32 RawGlyphCount = 0;
    int32 IndependentInnerCount = 0;
    bool RawGlyphHasBlend = false;
    TFunction<void(const FUMGWidgetNode&)> Inspect = [&](const FUMGWidgetNode& Item)
    {
        if (Item.Type == TEXT("TextBlock"))
        {
            ++RawGlyphCount;
            RawGlyphHasBlend |= Item.Render.PaintBlendMode.IsSet();
        }
        if (Item.Effect.EffectMaterial.Get(FString()) == UMGPropertyKeys::RetainerSubtreeShadowEffectRef)
            SharedShadow = &Item;
        if (Item.Effect.EffectMaterial.Get(FString()) == UMGPropertyKeys::RetainerInnerShadowEffectRef)
            ++IndependentInnerCount;
        for (const auto& ItemChild : Item.Children) Inspect(ItemChild);
    };
    Inspect(MixedGroup);
    TestTrue(TEXT("Glyph fill and single inner are owned by one raw-alpha shadow capture"),
        SharedShadow && SharedShadow->Render.PaintBlendMode.Get(FString()) == TEXT("multiply")
        && SharedShadow->Effect.InnerShadowBlendMode.Get(FString()) == TEXT("screen")
        && SharedShadow->Effect.EffectColor.IsSet() && SharedShadow->Shadow.BoxShadows.Num() == 2
        && RawGlyphCount == 1 && !RawGlyphHasBlend && IndependentInnerCount == 0
        && !MixedGroup.Render.PaintBlendMode.IsSet());
    if (TestTrue(TEXT("Combined inner shadow retains its required offset"),
        SharedShadow && SharedShadow->Effect.EffectOffset.IsSet()))
        TestTrue(TEXT("Combined inner offset uses the actual expanded capture size"),
            FMath::IsNearlyEqual(SharedShadow->Effect.EffectOffset->X * SharedShadow->Bounds.W, 2.0, KINDA_SMALL_NUMBER)
            && FMath::IsNearlyEqual(SharedShadow->Effect.EffectOffset->Y * SharedShadow->Bounds.H, 1.0, KINDA_SMALL_NUMBER));
    TextNode->Visual.Effects.RemoveAll([](const FDesignEffect& Effect) { return !Effect.Shadow.bInner; });
    FUMGWidgetNode InnerText;
    InnerText.Type = TEXT("TextBlock");
    InnerText.Id = TextNode->Id;
    InnerText.Name = TextNode->Name;
    VisualMapper::ApplyVisualProperties(InnerText, TextNode);
    FUMGWidgetNode InnerGlyphGroup = Design2UMGConverterVisualSupport::ApplyVisualEffectWrappers(
        MoveTemp(InnerText), TextNode, FVisualMapperOptions(), false);
    SharedShadow = nullptr; RawGlyphCount = 0; IndependentInnerCount = 0; RawGlyphHasBlend = false;
    Inspect(InnerGlyphGroup);
    TestTrue(TEXT("Glyph inner-only blend reuses the same capture with no outer layers"),
        SharedShadow && SharedShadow->Shadow.BoxShadows.IsEmpty() && SharedShadow->Effect.EffectColor.IsSet()
        && RawGlyphCount == 1 && !RawGlyphHasBlend && IndependentInnerCount == 0);
    TextNode->Type = EDesignNodeType::IconFont;
    TextNode->Visual.Effects.Reset();
    FUMGWidgetNode IconPaint;
    IconPaint.Type = TEXT("TextBlock");
    VisualMapper::ApplyVisualProperties(IconPaint, TextNode);
    TestEqual(TEXT("Official icon color fill keeps its independent paint mode"), IconPaint.Render.PaintBlendMode.Get(FString()), FString(TEXT("multiply")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperStrokeInnerBlendTransferTest,
    "Design2UMG.VisualMapper.StrokeInnerBlendTransfer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperStrokeInnerBlendTransferTest::RunTest(const FString&)
{
    FDesignFill Fill = FDesignFill::FromColor(TEXT("#FFFF00FF"));
    FDesignEffect Inner;
    Inner.Type = EDesignEffectType::Shadow;
    Inner.BlendMode = TEXT("SCREEN");
    Inner.Shadow.BlendMode = Inner.BlendMode;
    Inner.Shadow.bInner = true;
    Inner.Shadow.Color = TEXT("#FF000080");
    Inner.Shadow.Blur = 3.0f;
    Inner.Shadow.OffsetX = 2.0f;
    Inner.Shadow.OffsetY = 1.0f;
    auto ShapeNode = MakeShared<FDesignNode>();
    ShapeNode->Type = EDesignNodeType::Rectangle;
    ShapeNode->Id = TEXT("shape-blend"); ShapeNode->Name = TEXT("ShapeBlend");
    ShapeNode->Transform.SetWidthValue(FDesignSize::Fixed(96));
    ShapeNode->Transform.SetHeightValue(FDesignSize::Fixed(64));
    ShapeNode->Visual.Fills.Add(Fill);
    ShapeNode->Visual.Effects.Add(Inner);
    FUMGWidgetNode InnerShape;
    InnerShape.Type = TEXT("Border");
    VisualMapper::ApplyVisualProperties(InnerShape, ShapeNode);
    FUMGWidgetNode InnerShapeGroup = Design2UMGConverterVisualSupport::ApplyVisualEffectWrappers(
        MoveTemp(InnerShape), ShapeNode, FVisualMapperOptions(), false);
    TestTrue(TEXT("Ordinary inner shadow passes only its own paint mode"),
        InnerShapeGroup.Effect.EffectMaterial.Get(FString()) == UMGPropertyKeys::RetainerInnerShadowEffectRef
        && InnerShapeGroup.Effect.InnerShadowBlendMode.Get(FString()) == TEXT("screen")
        && !InnerShapeGroup.Render.PaintBlendMode.IsSet());
    ShapeNode->Visual.Effects.Reset();
    ShapeNode->Visual.Stroke.Thickness.bUniform = true;
    ShapeNode->Visual.Stroke.Thickness.Uniform = 4.0f;
    ShapeNode->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#FF000080"));
    ShapeNode->Visual.Stroke.Fill->BlendMode = TEXT("OVERLAY");
    FUMGWidgetNode StrokedShape;
    StrokedShape.Type = TEXT("Border");
    VisualMapper::ApplyVisualProperties(StrokedShape, ShapeNode);
    FUMGWidgetNode StrokeGroup = Design2UMGConverterVisualSupport::ApplyVisualEffectWrappers(
        MoveTemp(StrokedShape), ShapeNode, FVisualMapperOptions(), true);
    TestTrue(TEXT("Already-native stroke becomes a single independent blended paint"),
        StrokeGroup.Children.Num() == 2 && !StrokeGroup.Children[0].Brush.OutlineThickness.IsSet()
        && StrokeGroup.Children[1].Render.PaintBlendMode.Get(FString()) == TEXT("overlay"));

    ShapeNode->Visual.Fills[0].BlendMode = TEXT("MULTIPLY");
    FUMGWidgetNode LayeredShape;
    LayeredShape.Type = TEXT("Border");
    FUMGWidgetNode ShapeContent;
    ShapeContent.Type = TEXT("Image");
    LayeredShape.Children.Add(MoveTemp(ShapeContent));
    VisualMapper::ApplyVisualProperties(LayeredShape, ShapeNode);
    TestTrue(TEXT("Layered fill fixture starts with a native outline"), LayeredShape.Brush.OutlineThickness.IsSet());
    FUMGWidgetNode LayeredStrokeGroup = Design2UMGConverterVisualSupport::ApplyVisualEffectWrappers(
        MoveTemp(LayeredShape), ShapeNode, FVisualMapperOptions(), true);
    TestTrue(TEXT("Layered fill clears its original outline and preserves child content"),
        LayeredStrokeGroup.Children.Num() == 2 && LayeredStrokeGroup.Children[0].Children.Num() == 2
        && LayeredStrokeGroup.Children[0].Children[0].Render.PaintBlendMode.Get(FString()) == TEXT("multiply")
        && !LayeredStrokeGroup.Children[0].Children[1].Brush.OutlineThickness.IsSet()
        && !LayeredStrokeGroup.Children[0].Children[1].Brush.OutlineColor.IsSet()
        && LayeredStrokeGroup.Children[0].Children[1].Children.Num() == 1
        && LayeredStrokeGroup.Children[1].Render.PaintBlendMode.Get(FString()) == TEXT("overlay")
        && FMath::IsNearlyEqual(LayeredStrokeGroup.Children[1].Path.StrokeThickness.Get(0), 4.0f));

    ShapeNode->Visual.Fills.Reset();
    FUMGWidgetNode StrokeOnlyShape;
    StrokeOnlyShape.Type = TEXT("Border");
    StrokeOnlyShape.Id = ShapeNode->Id;
    StrokeOnlyShape.Name = ShapeNode->Name;
    VisualMapper::ApplyVisualProperties(StrokeOnlyShape, ShapeNode);
    StrokeOnlyShape.Render.RenderOpacity = .6f;
    StrokeOnlyShape.Render.RenderAngle = 30.0f;
    StrokeOnlyShape.Render.RenderTransformScale = FVector2D(2.0f, 3.0f);
    StrokeOnlyShape.Render.RenderTransformPivot = FVector2D(.25f, .75f);
    StrokeOnlyShape.Render.RenderTranslation = FVector2D(7.0f, 9.0f);
    FUMGWidgetNode CollapsedStroke = Design2UMGConverterVisualSupport::ApplyVisualEffectWrappers(
        MoveTemp(StrokeOnlyShape), ShapeNode, FVisualMapperOptions(), true);
    TestTrue(TEXT("Stroke-only paint collapses to one image with its independent blend"),
        CollapsedStroke.Type == TEXT("Image") && CollapsedStroke.Children.IsEmpty()
        && CollapsedStroke.Id == ShapeNode->Id && CollapsedStroke.Name == ShapeNode->Name
        && CollapsedStroke.Render.PaintBlendMode.Get(FString()) == TEXT("overlay"));
    TestTrue(TEXT("Collapsed stroke keeps object opacity and transforms"),
        CollapsedStroke.Render.RenderOpacity.Get(1.0f) == .6f
        && CollapsedStroke.Render.RenderAngle.Get(0.0f) == 30.0f
        && CollapsedStroke.Render.RenderTransformScale.Get(FVector2D::ZeroVector) == FVector2D(2.0f, 3.0f)
        && CollapsedStroke.Render.RenderTransformPivot.Get(FVector2D::ZeroVector) == FVector2D(.25f, .75f)
        && CollapsedStroke.Render.RenderTranslation.Get(FVector2D::ZeroVector) == FVector2D(7.0f, 9.0f));

    ShapeNode->Type = EDesignNodeType::Line;
    ShapeNode->Visual.Stroke.Cap = EDesignStrokeCap::Circular;
    FUMGWidgetNode StrokeOnlyLine;
    StrokeOnlyLine.Type = TEXT("Image");
    StrokeOnlyLine.Render.RenderOpacity = .6f;
    StrokeOnlyLine.Render.RenderTranslation = FVector2D(7.0f, 9.0f);
    FUMGWidgetNode CollapsedLine = Design2UMGConverterVisualSupport::ApplyVisualEffectWrappers(
        MoveTemp(StrokeOnlyLine), ShapeNode, FVisualMapperOptions(), false);
    TestTrue(TEXT("Stroke-only line retains blend, opacity and centered ink offset"),
        CollapsedLine.Type == TEXT("Image") && CollapsedLine.Children.IsEmpty()
        && CollapsedLine.Render.PaintBlendMode.Get(FString()) == TEXT("overlay")
        && CollapsedLine.Render.RenderOpacity.Get(1.0f) == .6f
        && CollapsedLine.Render.RenderTranslation.Get(FVector2D::ZeroVector) == FVector2D(7.0f, 7.0f));
    TestTrue(TEXT("Collapsed line keeps the cap-expanded canvas and original layout width"),
        CollapsedLine.Path.CanvasSize.Get(FVector2D::ZeroVector) == FVector2D(100.0f, 4.0f)
        && CollapsedLine.WidgetStyle.WidthOverride.Get(0.0f) == 96.0f
        && CollapsedLine.WidgetStyle.HeightOverride.Get(0.0f) == 4.0f
        && CollapsedLine.Render.RenderTransformScale.Get(FVector2D::ZeroVector).Equals(FVector2D(100.0f / 96.0f, 1.0f)));

    TestTrue(TEXT("Current Figma LIGHTEN adapts while unknown source modes remain errors"),
        VisualMapperFillTransfer::CanonicalPaintBlendMode(TEXT("LIGHTEN")) == TEXT("light")
        && VisualMapperFillTransfer::CanonicalPaintBlendMode(TEXT("unknown-mode")) == TEXT("unknown-mode"));

    return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
