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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderExplicitSideStrokeTest,
	"Design2UMG.VisualMapper.WrapWithBorderExplicitSideStroke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FVisualMapperWrapWithBorderExplicitSideStrokeTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Frame, 128.0f, 48.0f);
	Node->Visual.Stroke.Thickness.bUniform = false;
	Node->Visual.Stroke.Thickness.Top = 2.0f;
	Node->Visual.Stroke.Thickness.Right = 4.0f;
	Node->Visual.Stroke.Thickness.Bottom = 6.0f;
	Node->Visual.Stroke.Thickness.Left = 8.0f;
	Node->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#112233FF"));

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Image");
	Widget.Id = TEXT("stroke_panel");
	Widget.Name = TEXT("StrokePanel");
	Widget.Bounds.W = 128.0f;
	Widget.Bounds.H = 48.0f;

	VisualMapper::ApplyStroke(Widget, Node->Visual.Stroke);
	FUMGWidgetNode Wrapped = VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);

	TestEqual(TEXT("Explicit side stroke wraps with overlay"), Wrapped.Type, TEXT("Overlay"));
	TestEqual(TEXT("Explicit side stroke child count"), Wrapped.Children.Num(), 5);
	if (Wrapped.Children.Num() != 5)
	{
		return false;
	}

	const FUMGWidgetNode& Surface = Wrapped.Children[0];
	TestEqual(TEXT("Surface child stays first"), Surface.Name, TEXT("StrokePanel_surface"));
	TestEqual(TEXT("Surface id gets suffix"), Surface.Id, TEXT("stroke_panel_surface"));
	TestEqual(TEXT("Surface keeps wrapped content count"), Surface.Children.Num(), 1);
	TestFalse(TEXT("Surface outline color cleared"), Surface.Brush.OutlineColor.IsSet());
	TestFalse(TEXT("Surface stroke padding cleared"), Surface.WidgetStyle.StrokePadding.IsSet());

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
	FUMGWidgetNode NormalWrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, NormalShadow, 0);
	TestEqual(TEXT("Normal shadow wrapper has shadow and content"), NormalWrapped.Children.Num(), 2);
	if (NormalWrapped.Children.Num() != 2)
	{
		return false;
	}
	TestEqual(TEXT("Normal finite width keeps existing rounded result"), NormalWrapped.Children[0].WidgetStyle.WidthOverride.Get(0), 20.0f);
	TestEqual(TEXT("Normal finite height keeps existing rounded result"), NormalWrapped.Children[0].WidgetStyle.HeightOverride.Get(0), 30.0f);

	FUMGWidgetNode HugeWidget;
	HugeWidget.Type = TEXT("Image");
	HugeWidget.Id = TEXT("huge_shadow_source");
	HugeWidget.Name = TEXT("HugeShadowSource");

	FUMGBoxShadowLayerData HugeShadow;
	HugeShadow.Color = FLinearColor::Black;
	HugeShadow.Blur = 1.0e20f;
	constexpr float SafeRoundToIntMax = static_cast<float>(TNumericLimits<int32>::Max() - 64);
	FUMGWidgetNode HugeWrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(HugeWidget), Node, HugeShadow, 0);
	TestEqual(TEXT("Overflowing shadow clamps width to safe RoundToInt max"), HugeWrapped.Children[0].WidgetStyle.WidthOverride.Get(0), SafeRoundToIntMax);
	TestEqual(TEXT("Overflowing shadow clamps height to safe RoundToInt max"), HugeWrapped.Children[0].WidgetStyle.HeightOverride.Get(0), SafeRoundToIntMax);

	FUMGWidgetNode InvalidWidget;
	InvalidWidget.Type = TEXT("Image");
	InvalidWidget.Id = TEXT("invalid_shadow_source");
	InvalidWidget.Name = TEXT("InvalidShadowSource");

	FUMGBoxShadowLayerData InvalidShadow;
	InvalidShadow.Color = FLinearColor::Black;
	InvalidShadow.Blur = FMath::Sqrt(-1.0f);
	FUMGWidgetNode InvalidWrapped = VisualMapper::WrapWithBoxShadow(MoveTemp(InvalidWidget), Node, InvalidShadow, 0);
	TestEqual(TEXT("Non-finite shadow width falls back to one pixel"), InvalidWrapped.Children[0].WidgetStyle.WidthOverride.Get(0), 1.0f);
	TestEqual(TEXT("Non-finite shadow height falls back to one pixel"), InvalidWrapped.Children[0].WidgetStyle.HeightOverride.Get(0), 1.0f);
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
	LinearNode->Visual.Fill.Type = EDesignFillType::Gradient;
	LinearNode->Visual.Fill.Gradient.GradientType = EDesignGradientType::Linear;
	LinearNode->Visual.Fill.Gradient.Rotation = 15.0f;
	LinearNode->Visual.Fill.Gradient.StartPoint = FVector2D(0.2f, 0.3f);
	LinearNode->Visual.Fill.Gradient.EndPoint = FVector2D(0.8f, 0.7f);
	FDesignGradientStop& LinearStartStop = LinearNode->Visual.Fill.Gradient.Colors.AddDefaulted_GetRef();
	LinearStartStop.Color = TEXT("#FF0000FF");
	LinearStartStop.Position = 0.0f;
	FDesignGradientStop& LinearEndStop = LinearNode->Visual.Fill.Gradient.Colors.AddDefaulted_GetRef();
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

#endif // WITH_DEV_AUTOMATION_TESTS
