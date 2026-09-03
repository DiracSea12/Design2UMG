// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Assorted private helper contracts: explicit-empty-brush elision/fallback, layout mapper helpers and size-constraint fusion, variable resolver, gradient typed payload, typed direct-read and procedural-ref equivalence.

#if WITH_DEV_AUTOMATION_TESTS

#include "Design2UMGConverterVisualSupport.h"
#include "Misc/AutomationTest.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "LayoutMapper.h"
#include "UMGSchema.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "VisualMapper.h"
#include "VisualMapperFillTransfer.h"
#include "UMGValueParser.h"
#include "Design2UMGCoreTestHelpers.h"

using namespace Design2UMGCoreTests;

namespace
{
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
	TestTrue(TEXT("$color/ink with slash is a variable ref"), Resolver.IsVariableRef(TEXT("$color/ink")));
	TestTrue(TEXT("$spacing-0.5 with dot is a variable ref"), Resolver.IsVariableRef(TEXT("$spacing-0.5")));
	TestFalse(TEXT("$N:--background keeps namespace-prefixed form unresolved"), Resolver.IsVariableRef(TEXT("$N:--background")));
	return true;
}

namespace
{
	FDesignVariable MakeCascadeVariable()
	{
		FDesignVariable Var;
		Var.Name = TEXT("--background");
		Var.Value = TEXT("#F2F3F0");

		FDesignThemeValue UnconditionalFirst;
		UnconditionalFirst.Value = TEXT("#F2F3F0");
		Var.ThemedValues.Add(UnconditionalFirst);

		FDesignThemeValue Dark;
		Dark.Value = TEXT("#111111");
		Dark.ThemeMatch.Add(TEXT("Mode"), TEXT("Dark"));
		Var.ThemedValues.Add(Dark);
		return Var;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVariableResolverCascadeSelectionTest,
	"Design2UMG.Parser.ThemedValueCascadeSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVariableResolverCascadeSelectionTest::RunTest(const FString& Parameters)
{
	TMap<FString, FDesignVariable> Variables;
	Variables.Add(TEXT("--background"), MakeCascadeVariable());

	// Empty context (no theme axes at all): unconditional entries still cascade.
	{
		FDesignVariableResolver Resolver(Variables);
		TestEqual(TEXT("Empty context keeps first unconditional entry"),
			Resolver.ResolveColor(TEXT("$--background")), FString(TEXT("#F2F3F0")));
	}

	// Document-level active theme Dark: themed entry applies over the unconditional one.
	{
		FDesignVariableResolver Resolver(Variables);
		TMap<FString, FString> Theme;
		Theme.Add(TEXT("Mode"), TEXT("Dark"));
		Resolver.SetThemeContext(Theme);
		TestEqual(TEXT("Dark context applies themed entry"),
			Resolver.ResolveColor(TEXT("$--background")), FString(TEXT("#111111")));
	}

	// Later unconditional entries override earlier ones regardless of context.
	{
		FDesignVariable TelavathilStyle;
		TelavathilStyle.Name = TEXT("bg-primary");
		TelavathilStyle.Value = TEXT("#1C1C1E");
		FDesignThemeValue First;
		First.Value = TEXT("#1C1C1E");
		TelavathilStyle.ThemedValues.Add(First);
		FDesignThemeValue Second;
		Second.Value = TEXT("#0E0E0E");
		TelavathilStyle.ThemedValues.Add(Second);
		TMap<FString, FDesignVariable> Vars;
		Vars.Add(TEXT("bg-primary"), MoveTemp(TelavathilStyle));

		FDesignVariableResolver Resolver(Vars);
		TestEqual(TEXT("Trailing unconditional entry wins"),
			Resolver.ResolveColor(TEXT("$bg-primary")), FString(TEXT("#0E0E0E")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVariableResolverDocumentThemeTest,
	"Design2UMG.Parser.DocumentThemeAndOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVariableResolverDocumentThemeTest::RunTest(const FString& Parameters)
{
	TMap<FString, TArray<FString>> Themes;
	Themes.Add(TEXT("Mode"), TArray<FString>{TEXT("Light"), TEXT("Dark")});
	Themes.Add(TEXT("Base"), TArray<FString>{TEXT("Neutral"), TEXT("Gray")});

	const TMap<FString, FString> DocTheme = FDesignVariableResolver::BuildDocumentTheme(Themes);
	TestEqual(TEXT("Mode axis takes first value"), DocTheme.FindRef(TEXT("Mode")), FString(TEXT("Light")));
	TestEqual(TEXT("Base axis takes first value"), DocTheme.FindRef(TEXT("Base")), FString(TEXT("Neutral")));

	TMap<FString, FDesignVariable> Variables;
	Variables.Add(TEXT("--background"), MakeCascadeVariable());

	// Document-level Light keeps the unconditional entry; per-axis override to Dark
	// flips it while other axes (none here) would stay inherited.
	FDesignVariableResolver Resolver(Variables);
	Resolver.SetThemeContext(DocTheme);
	TestEqual(TEXT("Document-level Light keeps default entry"),
		Resolver.ResolveColor(TEXT("$--background")), FString(TEXT("#F2F3F0")));

	TMap<FString, FString> NodeOverride;
	NodeOverride.Add(TEXT("Mode"), TEXT("Dark"));
	Resolver.PushThemeOverride(NodeOverride);
	TestEqual(TEXT("Node override flips to Dark entry"),
		Resolver.ResolveColor(TEXT("$--background")), FString(TEXT("#111111")));

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

	FUMGWidgetNode ExplicitParamsNode;
	ExplicitParamsNode.Type = TEXT("Image");
	ExplicitParamsNode.Brush.MaterialRef = TEXT("roundedrect");
	ExplicitParamsNode.Brush.MaterialParams = TEXT("{\"ColorCenter\":\"#01020304\",\"ColorEdge\":\"#05060708\"}");
	FUMGProceduralResourceRequest ExplicitParamsRequest;
	TestTrue(TEXT("Typed procedural request keeps explicit params without a gradient"),
		Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(ExplicitParamsNode, ExplicitParamsRequest));
	TestEqual(TEXT("Explicit MaterialParams fallback is preserved without a gradient"),
		ExplicitParamsRequest.MaterialParams.Get(FString()),
		ExplicitParamsNode.Brush.MaterialParams.GetValue());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperTypedDirectReadEquivalenceTest,
	"Design2UMG.VisualMapper.TypedDirectReadEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperTypedDirectReadEquivalenceTest::RunTest(const FString& Parameters)
{
	// Locks the typed-direct-read migration of the border/wrapper transfer helpers:
	// values must arrive at the destination bit-identically (including sub-1e-6
	// precision that a serialize->parse roundtrip would quantize) and presence
	// rules must match the descriptor contract (absent stays absent).
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Type = EDesignNodeType::Frame;
	Node->Transform.Opacity = 1.0f;

	const float SubQuantumOpacity = 0.123456789f; // More decimals than SanitizeFloat's %f keeps.
	const FLinearColor PreciseColor(0.1111111f, 0.2222222f, 0.3333333f, 0.4444444f);

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Border");
	Widget.Id = TEXT("direct_read_panel");
	Widget.Name = TEXT("DirectReadPanel");
	Widget.Bounds.W = 200.0f;
	Widget.Bounds.H = 80.0f;
	Widget.Brush.BrushColor = PreciseColor;
	Widget.Brush.OutlineColor = PreciseColor;
	Widget.Render.RenderOpacity = SubQuantumOpacity;
	Widget.Render.RenderTransformScale = FVector2D(SubQuantumOpacity, 2.0f);

	FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);

	// Direct typed reads keep full precision across the border transfer.
	TestTrue(TEXT("Border BrushColor keeps full precision"),
		Wrapped.Brush.BrushColor.IsSet()
		&& Wrapped.Brush.BrushColor.GetValue().Equals(PreciseColor));
	TestTrue(TEXT("Border OutlineColor keeps full precision"),
		Wrapped.Brush.OutlineColor.IsSet()
		&& Wrapped.Brush.OutlineColor.GetValue().Equals(PreciseColor));
	TestTrue(TEXT("Border RenderOpacity keeps sub-quantum precision"),
		Wrapped.Render.RenderOpacity.IsSet()
		&& FMath::Abs(Wrapped.Render.RenderOpacity.GetValue() - SubQuantumOpacity) < 1e-9f);
	TestTrue(TEXT("Border RenderTransformScale keeps full precision"),
		Wrapped.Render.RenderTransformScale.IsSet()
		&& Wrapped.Render.RenderTransformScale.GetValue().Equals(FVector2D(SubQuantumOpacity, 2.0f)));

	// The inner widget loses the transferred fields entirely (presence rules).
	const FUMGWidgetNode& Inner = Wrapped.Children[0];
	TestFalse(TEXT("Inner widget does not keep BrushColor"), Inner.Brush.BrushColor.IsSet());
	TestFalse(TEXT("Inner widget does not keep RenderOpacity"), Inner.Render.RenderOpacity.IsSet());

	// Descriptor serialization of the transferred value still matches the
	// historical string form so downstream JSON/diagnostic seams stay stable.
	TestEqual(
		TEXT("Descriptor color output unchanged after direct read migration"),
		GetWidgetProperty(Wrapped, TEXT("BrushColor")),
		TEXT("#5E829C71"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProceduralResourceAccessExplicitRefEquivalenceTest,
	"Design2UMG.ProceduralResourceAccess.ExplicitRefEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FProceduralResourceAccessExplicitRefEquivalenceTest::RunTest(const FString& Parameters)
{
	// Explicit MaterialRef path: the migrated direct reads must reproduce the
	// descriptor precedence (gradient > explicit ref) and reject nodes with no
	// material source exactly like the previous descriptor lookup did.
	FUMGWidgetNode ExplicitRefNode;
	ExplicitRefNode.Type = TEXT("Image");
	ExplicitRefNode.WidgetStyle.WidthOverride = 320.5f;
	ExplicitRefNode.WidgetStyle.HeightOverride = 64.25f;
	ExplicitRefNode.Shadow.ShadowBlur = 7.5f;
	ExplicitRefNode.Brush.MaterialRef = TEXT("roundedrect");
	ExplicitRefNode.Brush.MaterialParams = TEXT("{\"Color\":\"#01020304\"}");
	ExplicitRefNode.Brush.BrushColor = FLinearColor(0.05f, 0.1f, 0.15f, 0.2f);

	FUMGProceduralResourceRequest ExplicitRequest;
	TestTrue(TEXT("Explicit material ref builds a request"),
		Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(ExplicitRefNode, ExplicitRequest));
	TestEqual(TEXT("Explicit ref material ref"), ExplicitRequest.MaterialRef, FString(TEXT("roundedrect")));
	TestEqual(TEXT("Explicit ref material params"), ExplicitRequest.MaterialParams.Get(FString()), TEXT("{\"Color\":\"#01020304\"}"));
	TestTrue(TEXT("Width override read directly"),
		ExplicitRequest.WidthOverride.IsSet() && FMath::Abs(ExplicitRequest.WidthOverride.GetValue() - 320.5f) < 1e-9f);
	TestTrue(TEXT("Height override read directly"),
		ExplicitRequest.HeightOverride.IsSet() && FMath::Abs(ExplicitRequest.HeightOverride.GetValue() - 64.25f) < 1e-9f);
	TestTrue(TEXT("Shadow blur read directly"),
		ExplicitRequest.ShadowBlur.IsSet() && FMath::Abs(ExplicitRequest.ShadowBlur.GetValue() - 7.5f) < 1e-9f);
	TestTrue(TEXT("Brush color read directly"),
		ExplicitRequest.BrushColor.IsSet()
		&& ExplicitRequest.BrushColor.GetValue().Equals(FLinearColor(0.05f, 0.1f, 0.15f, 0.2f)));

	// No material source at all -> request rejected (same as the descriptor gate).
	FUMGWidgetNode NoMaterialNode;
	NoMaterialNode.Type = TEXT("Image");
	NoMaterialNode.Brush.BrushColor = FLinearColor::Red;
	FUMGProceduralResourceRequest RejectedRequest;
	TestFalse(TEXT("Node without any material source is rejected"),
		Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(NoMaterialNode, RejectedRequest));

	// Gradient wins over an also-present explicit ref (descriptor precedence).
	FUMGWidgetNode GradientPrecedenceNode;
	GradientPrecedenceNode.Type = TEXT("Image");
	GradientPrecedenceNode.Brush.Gradient.Type = TEXT("Radial");
	GradientPrecedenceNode.Brush.Gradient.Center = FVector2D::ZeroVector;
	GradientPrecedenceNode.Brush.Gradient.Size = FVector2D(1.0f, 1.0f);
	GradientPrecedenceNode.Brush.Gradient.Stops = { { FLinearColor::White, 0.0f }, { FLinearColor::Black, 1.0f } };
	GradientPrecedenceNode.Brush.MaterialRef = TEXT("roundedrect");
	FUMGProceduralResourceRequest GradientRequest;
	TestTrue(TEXT("Gradient node builds a request"),
		Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(GradientPrecedenceNode, GradientRequest));
	TestEqual(TEXT("Gradient takes precedence over explicit ref"),
		GradientRequest.MaterialRef, FString(TEXT("radialgradient")));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FillTransferPathRasterTintGuardTest,
	"Design2UMG.FillTransfer.PathRasterTintGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FillTransferPathRasterTintGuardTest::RunTest(const FString& Parameters)
{
	const FString FillHex(TEXT("#D4916EFF"));
	const FLinearColor Expected = FUMGValueParser::ParseColor(FillHex);

	// Rasterized path widget: the fill is baked into its texture, so the brush
	// tint must stay unset or the render would square the color.
	TSharedPtr<FDesignNode> PathNode = MakeFixedNode(EDesignNodeType::Path, 24.0f, 24.0f);
	PathNode->Visual.Fills.Add(FDesignFill::FromColor(FillHex));
	FUMGWidgetNode RasterWidget;
	RasterWidget.Type = TEXT("Image");
	RasterWidget.Path.Geometry = TEXT("M0 0 L24 24");
	VisualMapperFillTransfer::ApplyFillProperties(RasterWidget, PathNode);
	TestFalse(TEXT("Raster path widget gets no brush tint"),
		RasterWidget.Brush.BrushColor.IsSet());

	// Pen ellipses rasterize through a side-effect-free clone: the widget is a
	// raster path even though the caller-owned node never carries geometry.
	TSharedPtr<FDesignNode> BareEllipseNode = MakeFixedNode(EDesignNodeType::Ellipse, 14.0f, 14.0f);
	BareEllipseNode->Visual.Fills.Add(FDesignFill::FromColor(FillHex));
	FUMGWidgetNode CloneRasterWidget;
	CloneRasterWidget.Type = TEXT("Image");
	CloneRasterWidget.Path.Geometry = TEXT("M7 0 A7 7 0 1 0 7 14");
	VisualMapperFillTransfer::ApplyFillProperties(CloneRasterWidget, BareEllipseNode);
	TestFalse(TEXT("Raster path widget without node geometry still gets no tint"),
		CloneRasterWidget.Brush.BrushColor.IsSet());

	// Stroked Figma ellipses carry fillGeometry but fall back to a Border with
	// no texture; the tint is their only fill source and must survive.
	TSharedPtr<FDesignNode> StrokedEllipseNode = MakeFixedNode(EDesignNodeType::Ellipse, 14.0f, 14.0f);
	StrokedEllipseNode->Shape.Geometry = TEXT("M7 0 A7 7 0 1 0 7 14");
	StrokedEllipseNode->Visual.Fills.Add(FDesignFill::FromColor(FillHex));
	FUMGWidgetNode BorderWidget;
	BorderWidget.Type = TEXT("Border");
	VisualMapperFillTransfer::ApplyFillProperties(BorderWidget, StrokedEllipseNode);
	TestTrue(TEXT("Non-raster widget with node geometry keeps the tint"),
		BorderWidget.Brush.BrushColor.IsSet());
	TestTrue(TEXT("Non-raster tint equals the fill color"),
		BorderWidget.Brush.BrushColor.IsSet()
			&& BorderWidget.Brush.BrushColor.GetValue().Equals(Expected));

	// Plain color fill on a plain widget: tint carries the color.
	TSharedPtr<FDesignNode> PlainNode = MakeFixedNode(EDesignNodeType::Rectangle, 24.0f, 24.0f);
	PlainNode->Visual.Fills.Add(FDesignFill::FromColor(FillHex));
	FUMGWidgetNode PlainWidget;
	PlainWidget.Type = TEXT("Border");
	VisualMapperFillTransfer::ApplyFillProperties(PlainWidget, PlainNode);
	TestTrue(TEXT("Plain widget keeps the tint"),
		PlainWidget.Brush.BrushColor.IsSet()
			&& PlainWidget.Brush.BrushColor.GetValue().Equals(Expected));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

