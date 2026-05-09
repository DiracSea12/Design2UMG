// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "LayoutFixer.h"
#include "LayoutMapper.h"
#include "UMGSchema.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"
#include "VisualMapper.h"

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
		return Design2UMGWidgetNodeAccess::HasWidgetString(Node, Key);
	}

	FDesignFill MakeGradientFill()
	{
		FDesignFill Fill;
		Fill.Type = EDesignFillType::Gradient;
		Fill.Gradient.GradientType = EDesignGradientType::Radial;
		Fill.Gradient.Rotation = 25.0f;
		Fill.Gradient.bHasCenter = true;
		Fill.Gradient.CenterX = 0.25f;
		Fill.Gradient.CenterY = 0.75f;
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
		Fill.Gradient.bHasCenter = true;
		Fill.Gradient.CenterX = 0.2f;
		Fill.Gradient.CenterY = 0.7f;
		Fill.Gradient.bHasSize = true;
		Fill.Gradient.SizeWidth = 0.8f;
		Fill.Gradient.SizeHeight = 1.35f;

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
	Widget.Brush.bHasBrushColor = true;
	Widget.Render.RenderOpacity = 0.6f;
	Widget.Render.bHasRenderOpacity = true;
	Widget.Render.RenderAngle = 15.0f;
	Widget.Render.bHasRenderAngle = true;
	Widget.Render.Clipping = TEXT("ClipToBounds");
	Widget.Render.bHasClipping = true;

	FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("Wrapped node remains Border"), Wrapped.Type, TEXT("Border"));
	TestEqual(TEXT("BrushColor moved to outer border"), GetWidgetProperty(Wrapped, TEXT("BrushColor")), TEXT("#112233FF"));
	TestEqual(TEXT("RenderOpacity moved to outer border"), GetWidgetProperty(Wrapped, TEXT("RenderOpacity")), TEXT("0.6"));
	TestEqual(TEXT("RenderAngle moved to outer border"), GetWidgetProperty(Wrapped, TEXT("RenderAngle")), TEXT("15.0"));
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
	TestFalse(TEXT("Inner widget should not keep RenderOpacity"), HasWidgetProperty(Inner, TEXT("RenderOpacity")));
	TestFalse(TEXT("Inner widget should not keep RenderAngle"), HasWidgetProperty(Inner, TEXT("RenderAngle")));
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
	Widget.Brush.bHasBrushColor = true;
	Widget.Render.RenderOpacity = 0.55f;
	Widget.Render.bHasRenderOpacity = true;
	Widget.Render.RenderAngle = 12.0f;
	Widget.Render.bHasRenderAngle = true;
	Widget.Render.Clipping = TEXT("ClipToBounds");
	Widget.Render.bHasClipping = true;

	FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("Typed wrapped node remains Border"), Wrapped.Type, TEXT("Border"));
	TestEqual(TEXT("Typed brush color moved to outer border"), GetWidgetProperty(Wrapped, TEXT("BrushColor")), TEXT("#89BCE1FF"));
	TestTrue(TEXT("Typed render opacity moved to outer border"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(Wrapped, TEXT("RenderOpacity"))), 0.55f, 0.0001f));
	TestTrue(TEXT("Typed render angle moved to outer border"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(Wrapped, TEXT("RenderAngle"))), 12.0f, 0.0001f));
	TestEqual(TEXT("Typed clipping moved to outer border"), GetWidgetProperty(Wrapped, TEXT("Clipping")), TEXT("ClipToBounds"));
	TestEqual(TEXT("Typed wrapped child count"), Wrapped.Children.Num(), 1);
	if (Wrapped.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& Inner = Wrapped.Children[0];
	TestFalse(TEXT("Typed inner widget should not keep BrushColor"), HasWidgetProperty(Inner, TEXT("BrushColor")));
	TestFalse(TEXT("Typed inner widget should not keep RenderOpacity"), HasWidgetProperty(Inner, TEXT("RenderOpacity")));
	TestFalse(TEXT("Typed inner widget should not keep RenderAngle"), HasWidgetProperty(Inner, TEXT("RenderAngle")));
	TestFalse(TEXT("Typed inner widget should not keep Clipping"), HasWidgetProperty(Inner, TEXT("Clipping")));
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
	TestTrue(TEXT("Gradient layer carries typed material ref"), GradientLayer.Brush.bHasMaterialRef);
	TestEqual(TEXT("Radial gradient resolves to radial material"),
		GradientLayer.Brush.MaterialRef,
		FString(TEXT("radialgradient")));
	TestTrue(TEXT("Gradient layer carries typed gradient stops"), GradientLayer.Brush.Gradient.Stops.Num() == 2);
	TestTrue(TEXT("Original widget becomes content child"), Wrapped.Children[2].Id.EndsWith(TEXT("_content")));
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
	Node->Visual.Fill = MakeRadialMultiStopGradientFill();

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Image");
	Widget.Id = TEXT("gradient_payload_widget");
	Widget.Name = TEXT("GradientPayloadWidget");
	VisualMapper::ApplyVisualProperties(Widget, Node);

	TestEqual(TEXT("Radial gradient uses procedural material ref"), GetWidgetProperty(Widget, UMGPropertyKeys::MaterialRef), FString(TEXT("radialgradient")));
	TestEqual(TEXT("Typed gradient type"), Widget.Brush.Gradient.Type, FString(TEXT("Radial")));
	TestTrue(TEXT("Typed gradient keeps center"), Widget.Brush.Gradient.bHasCenter);
	TestTrue(TEXT("Typed gradient keeps size"), Widget.Brush.Gradient.bHasSize);
	TestTrue(TEXT("Typed gradient keeps rotation"), Widget.Brush.Gradient.bHasRotation);
	TestEqual(TEXT("Typed gradient stop count"), Widget.Brush.Gradient.Stops.Num(), 3);
	TestTrue(TEXT("Typed gradient center X"),
		FMath::IsNearlyEqual(Widget.Brush.Gradient.Center.X, 0.2f, 0.0001f));
	TestTrue(TEXT("Typed gradient center Y"),
		FMath::IsNearlyEqual(Widget.Brush.Gradient.Center.Y, 0.7f, 0.0001f));
	TestTrue(TEXT("Typed gradient size width"),
		FMath::IsNearlyEqual(Widget.Brush.Gradient.Size.X, 0.8f, 0.0001f));
	TestTrue(TEXT("Typed gradient size height"),
		FMath::IsNearlyEqual(Widget.Brush.Gradient.Size.Y, 1.35f, 0.0001f));

	const FString MaterialParams = GetWidgetProperty(Widget, UMGPropertyKeys::MaterialParams);
	TestTrue(TEXT("Material params include multi-stop array"), MaterialParams.Contains(TEXT("\"Stops\"")));
	TestTrue(TEXT("Material params include middle stop position"), MaterialParams.Contains(TEXT("\"Pos\":0.4500")));
	TestTrue(TEXT("Material params include radial center"), MaterialParams.Contains(TEXT("\"CenterX\":0.2000")));
	TestTrue(TEXT("Material params include radial size"), MaterialParams.Contains(TEXT("\"SizeHeight\":1.3500")));
	TestTrue(TEXT("Material params include rotation"), MaterialParams.Contains(TEXT("\"Rotation\":37.5000")));

	const FUMGPropertyBag PropertyBag = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Widget);
	FString BagMaterialRef;
	FString BagMaterialParams;
	TestTrue(TEXT("Typed property bag exposes gradient material ref"),
		PropertyBag.TryGetString(UMGPropertyKeys::MaterialRef, BagMaterialRef));
	TestTrue(TEXT("Typed property bag exposes gradient material params"),
		PropertyBag.TryGetString(UMGPropertyKeys::MaterialParams, BagMaterialParams));
	TestEqual(TEXT("Bag material ref"), BagMaterialRef, FString(TEXT("radialgradient")));
	TestEqual(TEXT("Bag material params"), BagMaterialParams, MaterialParams);
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

#endif // WITH_DEV_AUTOMATION_TESTS
