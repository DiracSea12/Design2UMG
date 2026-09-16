// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "PenParser.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGValueParsing.h"
#include "UMGWidgetUtils.h"
#include "Design2UMGTestUtils.h"
#include "Components/EditableTextBox.h"
#include "UObject/UObjectGlobals.h"

#if WITH_AUTOMATION_WORKER
using namespace Design2UMGTestUtils;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedNodeEmptyStringStillAppliesTest,
	"Design2UMG.PropertyApplier.TypedEmptyStringApplies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedNodeEmptyStringStillAppliesTest::RunTest(const FString& Parameters)
{
	UEditableTextBox* Widget = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("EditableTextBox created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	Widget->SetText(FText::FromString(TEXT("Existing Text")));
	Widget->SetHintText(FText::FromString(TEXT("Existing Hint")));

	FUMGWidgetNode Node;
	Node.Text.Text = FString();
	Node.Text.HintText = FString();
	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	TestTrue(TEXT("Explicit empty Text still clears widget text"), Widget->GetText().ToString().IsEmpty());
	TestTrue(TEXT("Explicit empty HintText still clears widget hint"), Widget->GetHintText().ToString().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRenderTransformScaleJsonContractTest,
	"Design2UMG.Json.RenderTransformScaleContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRenderTransformScaleJsonContractTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"RootWidget": {
			"Type": "TextBlock",
			"Name": "ScaledText",
			"Bounds": { "X": 0, "Y": 0, "Width": 100, "Height": 20 },
			"Render": {
				"RenderTransformScale": "1.250000,0.750000"
			}
		}
	})");

	FUMGWidgetData Parsed;
	FString ParseError;
	TestTrue(TEXT("UMGJsonParser parses RenderTransformScale-only payload"), FUMGJsonParser::ParseJson(Json, Parsed, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddError(ParseError);
	}
	TestFalse(TEXT("Root widget type parsed"), Parsed.RootWidget.Type.IsEmpty());
	if (Parsed.RootWidget.Type.IsEmpty())
	{
		return false;
	}

	const FUMGWidgetNode& Root = Parsed.RootWidget;
	TestTrue(TEXT("RenderTransformScale is marked present"), Root.Render.RenderTransformScale.IsSet());
	TestTrue(TEXT("RenderTransformScale X preserved"), FMath::IsNearlyEqual(Root.Render.RenderTransformScale.GetValue().X, 1.25, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("RenderTransformScale Y preserved"), FMath::IsNearlyEqual(Root.Render.RenderTransformScale.GetValue().Y, 0.75, KINDA_SMALL_NUMBER));

	const FString RoundTripJson = FUMGJsonSerializer::Serialize(Parsed);
	TestTrue(TEXT("Serializer keeps RenderTransformScale"), RoundTripJson.Contains(TEXT("\"RenderTransformScale\"")));
	TestFalse(TEXT("Serializer no longer emits RenderScale alias"), RoundTripJson.Contains(TEXT("\"RenderScale\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlotJsonTypedCompatibilityTest,
	"Design2UMG.Json.SlotTypedCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSlotJsonTypedCompatibilityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"RootWidget": {
			"Type": "TextBlock",
			"Name": "SlotText",
			"Slot": {
				"HorizontalAlignment": "left",
				"HAlign": "center",
				"VerticalAlignment": "top",
				"VAlign": "bottom",
				"SizeRule": "fill"
			}
		}
	})");

	FUMGWidgetData Parsed;
	FString ParseError;
	TestTrue(TEXT("UMGJsonParser parses slot payload"), FUMGJsonParser::ParseJson(Json, Parsed, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddError(ParseError);
	}

	const FUMGSlotData& Slot = Parsed.RootWidget.Slot;
	TestTrue(TEXT("HAlign short key is ignored; HorizontalAlignment wins"), Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Left);
	TestTrue(TEXT("VAlign short key is ignored; VerticalAlignment wins"), Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Top);
	TestTrue(TEXT("Lowercase SizeRule parses as Fill"), Slot.SizeRule == EUMGSlotSizeRule::Fill);

	const FString RoundTripJson = FUMGJsonSerializer::Serialize(Parsed);
	TestTrue(TEXT("Serializer keeps canonical horizontal alignment"), RoundTripJson.Contains(TEXT("\"HorizontalAlignment\": \"Left\"")));
	TestTrue(TEXT("Serializer keeps canonical vertical alignment"), RoundTripJson.Contains(TEXT("\"VerticalAlignment\": \"Top\"")));
	TestTrue(TEXT("Serializer keeps canonical size rule"), RoundTripJson.Contains(TEXT("\"SizeRule\": \"Fill\"")));

	FUMGSlotData UnknownAlignmentSlot;
	UnknownAlignmentSlot.HorizontalAlignment = FUMGValueParser::ParseSlotHorizontalAlignment(TEXT("Diagonal"));
	UnknownAlignmentSlot.VerticalAlignment = FUMGValueParser::ParseSlotVerticalAlignment(TEXT("Outside"));
	TestTrue(TEXT("Unknown horizontal alignment falls back to Fill"), UnknownAlignmentSlot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill);
	TestTrue(TEXT("Unknown vertical alignment falls back to Fill"), UnknownAlignmentSlot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill);
	TestTrue(TEXT("Unknown SizeRule falls back to Auto"), FUMGValueParser::ParseSlotSizeRule(TEXT("Unexpected")) == EUMGSlotSizeRule::Auto);
	TestTrue(TEXT("Lowercase Auto size rule parses as Auto"), FUMGValueParser::ParseSlotSizeRule(TEXT("auto")) == EUMGSlotSizeRule::Auto);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParserNumberStringFlexibleTest,
	"Design2UMG.Unit.NumberStringFlexible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FParserNumberStringFlexibleTest::RunTest(const FString& Parameters)
{
	FString Json;
	TestTrue(TEXT("Load fixture ParserNumberStringFlexible"), LoadFixtureJson(TEXT("ParserNumberStringFlexible.json"), Json));
	if (Json.IsEmpty())
		return false;
	FDesignDocument Doc;
	TestTrue(TEXT("String number parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level child count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() == 0)
		return false;
	const TSharedPtr<FDesignNode>& Root = Doc.Children[0];
	TestTrue(TEXT("Root has parsed X"), Root->Transform.HasExplicitX());
	TestTrue(TEXT("Root has parsed Y"), Root->Transform.HasExplicitY());
	TestEqual(TEXT("Root gap parsed from string"), Root->LayoutData.ExplicitGap.Get(0.0f), 8.0f);
	TestEqual(TEXT("Root opacity parsed from string"), Root->Transform.Opacity, 0.75f);
	TestEqual(TEXT("Root padding top parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Top, 1.0f);
	TestEqual(TEXT("Root padding right parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Right, 2.0f);
	TestEqual(TEXT("Root padding bottom parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Bottom, 3.0f);
	TestEqual(TEXT("Root padding left parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Left, 4.0f);
	TestTrue(TEXT("Root cornerRadius parsed as per-corner"), !Root->Visual.CornerRadius.bUniform);
	TestEqual(TEXT("Root cornerRadius TL parsed from string"), Root->Visual.CornerRadius.TopLeft, 5.0f);
	TestEqual(TEXT("Root cornerRadius TR parsed from string"), Root->Visual.CornerRadius.TopRight, 6.0f);
	TestEqual(TEXT("Root cornerRadius BR parsed from string"), Root->Visual.CornerRadius.BottomRight, 7.0f);
	TestEqual(TEXT("Root cornerRadius BL parsed from string"), Root->Visual.CornerRadius.BottomLeft, 8.0f);
	TestTrue(TEXT("Root stroke thickness parsed as uniform"), Root->Visual.Stroke.Thickness.bUniform);
	TestEqual(TEXT("Root stroke thickness parsed from string"), Root->Visual.Stroke.Thickness.Uniform, 2.5f);
	TestEqual(TEXT("Root image transform A parsed from string"), Root->Visual.TopmostFill().Image.Transform.A, 1.0f);
	TestEqual(TEXT("Root image transform D parsed from string"), Root->Visual.TopmostFill().Image.Transform.D, 1.0f);
	TestEqual(TEXT("Root child count"), Root->Children.Num(), 2);
	if (Root->Children.Num() < 2)
		return false;
	TestEqual(TEXT("Text fontSize parsed from string"), Root->Children[0]->Text.FontSize, 16.0f);
	TestEqual(TEXT("Text lineHeight parsed from string"), Root->Children[0]->Text.LineHeight, 1.5f);
	TestEqual(TEXT("Text letterSpacing parsed from string"), Root->Children[0]->Text.LetterSpacing.GetValue(), 0.2f);
	TestEqual(TEXT("Icon weight parsed from string"), Root->Children[1]->Text.IconWeight, 500);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenPaddingTwoValueAxisOrderTest,
	"Design2UMG.Parser.PenPaddingTwoValueAxisOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenPaddingTwoValueAxisOrderTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [{
			"id": "panel",
			"type": "frame",
			"padding": [6, 12]
		}]
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("ParseDocument succeeds"), FPenParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Child count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() == 0)
	{
		return false;
	}
	const TSharedPtr<FDesignNode>& Panel = Doc.Children[0];
	TestEqual(TEXT("Two-value padding top"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Top, 6.0f);
	TestEqual(TEXT("Two-value padding bottom"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Bottom, 6.0f);
	TestEqual(TEXT("Two-value padding left"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Left, 12.0f);
	TestEqual(TEXT("Two-value padding right"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Right, 12.0f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignSchemaSourceExtensionsRemainOptInTest,
	"Design2UMG.Schema.SourceExtensionsRemainOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignSchemaSourceExtensionsRemainOptInTest::RunTest(const FString& Parameters)
{
	FDesignNode Node;
	TestFalse(TEXT("Fresh node has no source extension"), Node.HasSourceData());
	const FDesignSourceNodeData& EmptyNodeSource = Node.Source();
	TestFalse(TEXT("Read-only node source access stays opt-in"), Node.HasSourceData());
	TestTrue(TEXT("Read-only node source returns empty source type"), EmptyNodeSource.SourceNodeType.IsEmpty());

	Node.MutableSource().SourceNodeType = TEXT("FRAME");
	TestTrue(TEXT("Explicit node source mutation materializes extension"), Node.HasSourceData());
	TestEqual(TEXT("Explicit node source mutation persists"), Node.Source().SourceNodeType, FString(TEXT("FRAME")));

	FDesignDocument Document;
	TestFalse(TEXT("Fresh document has no source document extension"), Document.HasSourceDocumentData());
	TestFalse(TEXT("Fresh document has no parse diagnostics extension"), Document.HasParseDiagnostics());

	const FDesignSourceDocumentData& EmptyDocumentSource = Document.SourceDocument();
	const FDesignParseDiagnostics& EmptyDiagnostics = Document.ParseDiagnostics();
	TestFalse(TEXT("Read-only document source access stays opt-in"), Document.HasSourceDocumentData());
	TestFalse(TEXT("Read-only diagnostics access stays opt-in"), Document.HasParseDiagnostics());
	TestTrue(TEXT("Read-only document source returns empty prototype start id"), EmptyDocumentSource.PrototypeStartNodeId.IsEmpty());
	TestEqual(TEXT("Read-only diagnostics returns empty skipped-node list"), EmptyDiagnostics.SkippedDesignerNodes.Num(), 0);

	Document.MutableSourceDocument().PrototypeStartNodeId = TEXT("root");
	FDesignSkippedNodeReport& AddedReport = Document.MutableParseDiagnostics().SkippedDesignerNodes.AddDefaulted_GetRef();
	AddedReport.NodeId = TEXT("skip-1");

	TestTrue(TEXT("Explicit document source mutation materializes source document"), Document.HasSourceDocumentData());
	TestTrue(TEXT("Explicit diagnostics mutation materializes diagnostics"), Document.HasParseDiagnostics());
	TestEqual(TEXT("Explicit document source mutation persists"), Document.SourceDocument().PrototypeStartNodeId, FString(TEXT("root")));
	TestEqual(TEXT("Explicit diagnostics mutation persists"), Document.ParseDiagnostics().SkippedDesignerNodes.Num(), 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUMGSchemaSourceExtensionsRemainOptInTest,
	"Design2UMG.Schema.UMGSourceExtensionsRemainOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FUMGSchemaSourceExtensionsRemainOptInTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	TestFalse(TEXT("Fresh UMG node has no source extension"), Node.HasSourceData());
	const FUMGSourceNodeData& EmptyNodeSource = Node.Source();
	TestFalse(TEXT("Read-only UMG node source access stays opt-in"), Node.HasSourceData());
	TestTrue(TEXT("Read-only UMG node source starts empty"), EmptyNodeSource.SourceNodeType.IsEmpty());

	Node.MutableSource().SourceNodeType = TEXT("INSTANCE");
	Node.MutableSource().ComponentId = TEXT("12:34");
	TestTrue(TEXT("Explicit UMG node source mutation materializes extension"), Node.HasSourceData());
	TestEqual(TEXT("Explicit UMG node source mutation persists"), Node.Source().SourceNodeType, FString(TEXT("INSTANCE")));
	TestEqual(TEXT("Explicit UMG node component id persists"), Node.Source().ComponentId, FString(TEXT("12:34")));

	FUMGWidgetData Data;
	TestFalse(TEXT("Fresh UMG widget data has no source document extension"), Data.HasSourceDocumentData());
	TestFalse(TEXT("Fresh UMG widget data has no prototype navigation extension"), Data.HasPrototypeNavigationData());

	const FUMGSourceDocumentData& EmptySourceDocument = Data.SourceDocument();
	const FUMGPrototypeNavigationData& EmptyPrototypeNavigation = Data.PrototypeNavigation();
	TestFalse(TEXT("Read-only UMG source document access stays opt-in"), Data.HasSourceDocumentData());
	TestFalse(TEXT("Read-only UMG prototype access stays opt-in"), Data.HasPrototypeNavigationData());
	TestTrue(TEXT("Read-only UMG source document starts empty"), EmptySourceDocument.ComponentsJson.IsEmpty());
	TestEqual(TEXT("Read-only UMG prototype starts empty"), EmptyPrototypeNavigation.ClickBindings.Num(), 0);

	Data.MutableSourceDocument().ComponentsJson = TEXT("{\"12:34\":{\"key\":\"button\"}}");
	FUMGPrototypeNavigationBinding& Binding = Data.MutablePrototypeNavigation().ClickBindings.AddDefaulted_GetRef();
	Binding.TriggerWidgetId = TEXT("cta_button");
	Binding.TriggerWidgetName = TEXT("cta_button");
	Binding.Action = EUMGPrototypeNavigationAction::Back;

	TestTrue(TEXT("Explicit UMG source document mutation materializes extension"), Data.HasSourceDocumentData());
	TestTrue(TEXT("Explicit UMG prototype navigation mutation materializes extension"), Data.HasPrototypeNavigationData());
	TestTrue(TEXT("Explicit UMG source document mutation persists"), Data.SourceDocument().ComponentsJson.Contains(TEXT("button")));
	TestEqual(TEXT("Explicit UMG prototype navigation mutation persists"), Data.PrototypeNavigation().ClickBindings.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxShadowLayerMissingFieldsDefaultTest,
	"Design2UMG.Json.BoxShadowLayerMissingFieldsDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBoxShadowLayerMissingFieldsDefaultTest::RunTest(const FString& Parameters)
{
	// Node-level ShadowBlur is non-zero and the first layer omits Blur/Spread, so a layer
	// missing those fields must keep the struct defaults (0) instead of leaking the stale
	// scratch value from the node-level number read.
	const FString Json = TEXT(R"({
		"RootWidget": {
			"Type": "Border",
			"Name": "Shadowed",
			"ShadowData": {
				"ShadowBlur": 20,
				"BoxShadows": [
					{ "Offset": "4.000000,6.000000", "Color": "#00000080" },
					{ "Offset": "1.000000,2.000000", "Blur": 12, "Spread": -3, "Color": "#FF0000FF" }
				]
			}
		}
	})");

	FUMGWidgetData Parsed;
	FString ParseError;
	TestTrue(TEXT("UMGJsonParser parses BoxShadows payload"), FUMGJsonParser::ParseJson(Json, Parsed, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddError(ParseError);
	}
	if (!TestEqual(TEXT("Two shadow layers parsed"), Parsed.RootWidget.Shadow.BoxShadows.Num(), 2))
	{
		return false;
	}

	const FUMGBoxShadowLayerData& MissingFieldsLayer = Parsed.RootWidget.Shadow.BoxShadows[0];
	TestTrue(TEXT("Layer without Blur keeps Blur at 0 (no stale ShadowBlur leak)"),
		FMath::IsNearlyEqual(MissingFieldsLayer.Blur, 0.0f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Layer without Spread keeps Spread at 0"),
		FMath::IsNearlyEqual(MissingFieldsLayer.Spread, 0.0f, KINDA_SMALL_NUMBER));

	const FUMGBoxShadowLayerData& CompleteLayer = Parsed.RootWidget.Shadow.BoxShadows[1];
	TestTrue(TEXT("Complete layer keeps Blur"), FMath::IsNearlyEqual(CompleteLayer.Blur, 12.0f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Complete layer keeps negative Spread"), FMath::IsNearlyEqual(CompleteLayer.Spread, -3.0f, KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetDataRoundTripDeepEqualityTest,
	"Design2UMG.Json.WidgetDataRoundTripDeepEquality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetDataRoundTripDeepEqualityTest::RunTest(const FString& Parameters)
{
	// Round-trip contract across the major field families in one representative tree:
	// root + children, slot layout (anchors/offsets/padding), typed text style, widget
	// style, image brush (texture path, transform, gradient), path, effect, shadow,
	// and render payloads. Warnings, IconFontMappingChanges, and the node/document
	// source extensions are intentionally one-way (never serialized), so they are
	// excluded from this contract. Field values are chosen to survive the string
	// formats used by the write side (margins/vectors print fixed decimals; colors
	// round-trip through 8-bit hex), so every populated field must compare equal.
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_RoundTripDeepEquality");

	FUMGWidgetNode& Root = SourceData.RootWidget;
	Root.Type = TEXT("CanvasPanel");
	Root.Id = TEXT("round_trip/root");
	Root.Name = TEXT("RoundTripRoot");
	Root.Bounds = FDesignPixelBounds(10.0f, 20.0f, 800.0f, 600.0f);
	Root.Effect.BlurStrength = 4.5f;
	Root.Effect.GeometryMaskShape = EUMGGeometryMaskShape::RoundedRectangle;
	Root.Effect.GeometryMaskSize = FVector2D(100.0f, 80.0f);
	Root.Effect.GeometryMaskCornerRadii = FVector4(4.0f, 8.0f, 12.0f, 16.0f);
	Root.Shadow.ShadowOffset = FVector2D(2.0f, -3.0f);
	Root.Shadow.ShadowBlur = 12.5f;
	Root.Shadow.BoxShadows = {
		{ FVector2D(4.25f, -6.5f), 12.0f, -3.0f, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f) },
		{ FVector2D(1.5f, 2.25f), 0.0f, 5.0f, FLinearColor(1.0f, 0.0f, 0.0f, 1.0f) },
	};
	Root.Render.RenderOpacity = 0.875f;
	Root.Render.RenderTransformScale = FVector2D(1.25f, 0.75f);
	Root.Render.RenderTransformPivot = FVector2D(0.5f, 0.25f);
	Root.Render.RenderTranslation = FVector2D(-8.5f, 4.5f);
	Root.Render.Clipping = TEXT("ClipToBounds");

	FUMGWidgetNode& TextChild = Root.Children.AddDefaulted_GetRef();
	TextChild.Type = TEXT("TextBlock");
	TextChild.Id = TEXT("round_trip/root/label");
	TextChild.Name = TEXT("RoundTripLabel");
	TextChild.Bounds = FDesignPixelBounds(40.0f, 60.0f, 320.0f, 96.0f);
	TextChild.Slot.Anchors.Min = FUMGAnchorData{ 0.25f, 0.75f };
	TextChild.Slot.Anchors.Max = FUMGAnchorData{ 0.75f, 0.25f };
	TextChild.Slot.Alignment = FUMGAnchorData{ 0.5f, 0.5f };
	TextChild.Slot.Offsets.Left = 10.0f;
	TextChild.Slot.Offsets.Top = 20.0f;
	TextChild.Slot.Offsets.Right = 30.0f;
	TextChild.Slot.Offsets.Bottom = 40.0f;
	TextChild.Slot.Padding = FMargin(4.5f, 8.25f, 12.75f, 3.5f);
	TextChild.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Center;
	TextChild.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Bottom;
	TextChild.Slot.SizeRule = EUMGSlotSizeRule::Fill;
	TextChild.Slot.FillWeight = 2.5f;
	TextChild.Slot.bAutoSize = true;
	TextChild.TextStyle.FontSize = 18.0f;
	TextChild.TextStyle.SourceFontSize = 24.0f;
	TextChild.TextStyle.LetterSpacing = 0.5f;
	TextChild.Text.Text = TEXT("Hello Round Trip");
	TextChild.Text.FontFamily = TEXT("Inter");
	TextChild.Text.TypefaceFontName = TEXT("Bold");
	TextChild.Text.Justification = TEXT("Center");
	TextChild.Text.bAutoWrapText = true;
	TextChild.Text.WrapTextAt = 300.0f;
	TextChild.Text.Color = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);
	TextChild.WidgetStyle.WidthOverride = 320.0f;
	TextChild.WidgetStyle.HeightOverride = 96.0f;
	TextChild.WidgetStyle.Padding = FMargin(6.0f, 4.5f, 3.0f, 2.5f);
	TextChild.WidgetStyle.InnerSlotPadding = FVector2D(8.0f, 4.0f);
	TextChild.WidgetStyle.Visibility = TEXT("Hidden");
	TextChild.WidgetStyle.ToolTipText = TEXT("Round-trip tooltip");
	TextChild.WidgetStyle.IsEnabled = false;

	FUMGWidgetNode& ImageChild = Root.Children.AddDefaulted_GetRef();
	ImageChild.Type = TEXT("Image");
	ImageChild.Id = TEXT("round_trip/root/hero");
	ImageChild.Name = TEXT("RoundTripHero");
	ImageChild.Bounds = FDesignPixelBounds(80.0f, 120.0f, 400.0f, 240.0f);
	ImageChild.Brush.Image.TexturePath = TEXT("https://example.com/hero.png");
	ImageChild.Brush.Image.Mode = TEXT("crop");
	ImageChild.Brush.Image.Transform = FUMGVisualBrushData::FImageTransformData{ 1.25f, 0.5f, -3.75f, 0.25f, 1.5f, 2.5f };
	ImageChild.Brush.OutlineCornerRadius = 6.0f;
	ImageChild.Brush.CornerRadii = FVector4(4.0f, 8.0f, 12.0f, 16.0f);
	ImageChild.Brush.OutlineThickness = 2.0f;
	ImageChild.Brush.Margin = FMargin(1.0f / 3.0f);
	ImageChild.Brush.DrawAs = ESlateBrushDrawType::Image;
	ImageChild.Brush.BrushColor = FLinearColor(0.0f, 0.0f, 1.0f, 1.0f);
	ImageChild.Brush.OutlineColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	ImageChild.Brush.Gradient.Type = TEXT("linear");
	ImageChild.Brush.Gradient.Rotation = 45.5f;
	ImageChild.Brush.Gradient.StartPoint = FVector2D(0.25f, 0.5f);
	ImageChild.Brush.Gradient.EndPoint = FVector2D(0.75f, 0.5f);
	ImageChild.Brush.Gradient.Stops = {
		{ FLinearColor(0.0f, 0.0f, 0.0f, 1.0f), 0.0f },
		{ FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), 1.0f },
	};
	ImageChild.Path.Geometry = TEXT("M 0 0 L 10 10 Z");
	ImageChild.Path.FillRule = TEXT("evenodd");
	ImageChild.Path.StrokeColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);
	ImageChild.Path.StrokeThickness = 2.5f;

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);

	FUMGWidgetData Parsed;
	FString ParseError;
	TestTrue(TEXT("Round-trip JSON parses back"), FUMGJsonParser::ParseJson(Json, Parsed, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddError(ParseError);
		return false;
	}

	TestEqual(TEXT("WidgetName round-trips"), Parsed.WidgetName, SourceData.WidgetName);

	const FUMGWidgetNode& ParsedRoot = Parsed.RootWidget;
	TestEqual(TEXT("Root type round-trips"), ParsedRoot.Type, Root.Type);
	TestEqual(TEXT("Root id round-trips"), ParsedRoot.Id, Root.Id);
	TestEqual(TEXT("Root name round-trips"), ParsedRoot.Name, Root.Name);
	TestTrue(TEXT("Root bounds round-trip"),
		FMath::IsNearlyEqual(ParsedRoot.Bounds.X, Root.Bounds.X)
		&& FMath::IsNearlyEqual(ParsedRoot.Bounds.Y, Root.Bounds.Y)
		&& FMath::IsNearlyEqual(ParsedRoot.Bounds.W, Root.Bounds.W)
		&& FMath::IsNearlyEqual(ParsedRoot.Bounds.H, Root.Bounds.H));
	TestTrue(TEXT("Root effect blur round-trips"), ParsedRoot.Effect.BlurStrength.IsSet() && FMath::IsNearlyEqual(ParsedRoot.Effect.BlurStrength.GetValue(), Root.Effect.BlurStrength.GetValue()));
	TestTrue(TEXT("Root effect mask shape round-trips"), ParsedRoot.Effect.GeometryMaskShape.IsSet() && ParsedRoot.Effect.GeometryMaskShape.GetValue() == EUMGGeometryMaskShape::RoundedRectangle);
	TestTrue(TEXT("Root effect mask size round-trips"), ParsedRoot.Effect.GeometryMaskSize.IsSet() && ParsedRoot.Effect.GeometryMaskSize.GetValue().Equals(Root.Effect.GeometryMaskSize.GetValue(), 0.0001f));
	TestTrue(TEXT("Root effect mask radii round-trip"), ParsedRoot.Effect.GeometryMaskCornerRadii.IsSet() && ParsedRoot.Effect.GeometryMaskCornerRadii.GetValue().Equals(Root.Effect.GeometryMaskCornerRadii.GetValue(), 0.0001f));
	TestTrue(TEXT("Root shadow offset round-trips"), ParsedRoot.Shadow.ShadowOffset.IsSet() && ParsedRoot.Shadow.ShadowOffset.GetValue().Equals(Root.Shadow.ShadowOffset.GetValue(), 0.0001f));
	TestTrue(TEXT("Root shadow blur round-trips"), ParsedRoot.Shadow.ShadowBlur.IsSet() && FMath::IsNearlyEqual(ParsedRoot.Shadow.ShadowBlur.GetValue(), Root.Shadow.ShadowBlur.GetValue()));
	TestEqual(TEXT("Root box shadow layer count round-trips"), ParsedRoot.Shadow.BoxShadows.Num(), Root.Shadow.BoxShadows.Num());
	if (ParsedRoot.Shadow.BoxShadows.Num() == Root.Shadow.BoxShadows.Num())
	{
		for (int32 LayerIndex = 0; LayerIndex < Root.Shadow.BoxShadows.Num(); ++LayerIndex)
		{
			const FUMGBoxShadowLayerData& Expected = Root.Shadow.BoxShadows[LayerIndex];
			const FUMGBoxShadowLayerData& Actual = ParsedRoot.Shadow.BoxShadows[LayerIndex];
			TestTrue(*FString::Printf(TEXT("Box shadow %d offset round-trips"), LayerIndex), Actual.Offset.Equals(Expected.Offset, 0.0001f));
			TestTrue(*FString::Printf(TEXT("Box shadow %d blur round-trips"), LayerIndex), FMath::IsNearlyEqual(Actual.Blur, Expected.Blur));
			TestTrue(*FString::Printf(TEXT("Box shadow %d spread round-trips"), LayerIndex), FMath::IsNearlyEqual(Actual.Spread, Expected.Spread));
			TestTrue(*FString::Printf(TEXT("Box shadow %d color round-trips"), LayerIndex), Actual.Color.Equals(Expected.Color, 0.0001f));
		}
	}
	TestTrue(TEXT("Root render opacity round-trips"), ParsedRoot.Render.RenderOpacity.IsSet() && FMath::IsNearlyEqual(ParsedRoot.Render.RenderOpacity.GetValue(), Root.Render.RenderOpacity.GetValue()));
	TestTrue(TEXT("Root render scale round-trips"), ParsedRoot.Render.RenderTransformScale.IsSet() && ParsedRoot.Render.RenderTransformScale.GetValue().Equals(Root.Render.RenderTransformScale.GetValue(), 0.0001f));
	TestTrue(TEXT("Root render pivot round-trips"), ParsedRoot.Render.RenderTransformPivot.IsSet() && ParsedRoot.Render.RenderTransformPivot.GetValue().Equals(Root.Render.RenderTransformPivot.GetValue(), 0.0001f));
	TestTrue(TEXT("Root render translation round-trips"), ParsedRoot.Render.RenderTranslation.IsSet() && ParsedRoot.Render.RenderTranslation.GetValue().Equals(Root.Render.RenderTranslation.GetValue(), 0.0001f));
	TestEqual(TEXT("Root clipping round-trips"), ParsedRoot.Render.Clipping.GetValue(), Root.Render.Clipping.GetValue());

	TestEqual(TEXT("Child count round-trips"), ParsedRoot.Children.Num(), Root.Children.Num());
	if (ParsedRoot.Children.Num() != Root.Children.Num())
	{
		return false;
	}

	const FUMGWidgetNode& ParsedText = ParsedRoot.Children[0];
	TestEqual(TEXT("Text child type round-trips"), ParsedText.Type, TextChild.Type);
	TestEqual(TEXT("Text child id round-trips"), ParsedText.Id, TextChild.Id);
	TestEqual(TEXT("Text child name round-trips"), ParsedText.Name, TextChild.Name);
	TestTrue(TEXT("Text child bounds round-trip"),
		FMath::IsNearlyEqual(ParsedText.Bounds.X, TextChild.Bounds.X)
		&& FMath::IsNearlyEqual(ParsedText.Bounds.Y, TextChild.Bounds.Y)
		&& FMath::IsNearlyEqual(ParsedText.Bounds.W, TextChild.Bounds.W)
		&& FMath::IsNearlyEqual(ParsedText.Bounds.H, TextChild.Bounds.H));
	TestTrue(TEXT("Slot anchors min round-trip"),
		FMath::IsNearlyEqual(ParsedText.Slot.Anchors.Min.X, TextChild.Slot.Anchors.Min.X)
		&& FMath::IsNearlyEqual(ParsedText.Slot.Anchors.Min.Y, TextChild.Slot.Anchors.Min.Y));
	TestTrue(TEXT("Slot anchors max round-trip"),
		FMath::IsNearlyEqual(ParsedText.Slot.Anchors.Max.X, TextChild.Slot.Anchors.Max.X)
		&& FMath::IsNearlyEqual(ParsedText.Slot.Anchors.Max.Y, TextChild.Slot.Anchors.Max.Y));
	TestTrue(TEXT("Slot alignment round-trips"),
		FMath::IsNearlyEqual(ParsedText.Slot.Alignment.X, TextChild.Slot.Alignment.X)
		&& FMath::IsNearlyEqual(ParsedText.Slot.Alignment.Y, TextChild.Slot.Alignment.Y));
	TestTrue(TEXT("Slot offsets round-trip"),
		FMath::IsNearlyEqual(ParsedText.Slot.Offsets.Left, TextChild.Slot.Offsets.Left)
		&& FMath::IsNearlyEqual(ParsedText.Slot.Offsets.Top, TextChild.Slot.Offsets.Top)
		&& FMath::IsNearlyEqual(ParsedText.Slot.Offsets.Right, TextChild.Slot.Offsets.Right)
		&& FMath::IsNearlyEqual(ParsedText.Slot.Offsets.Bottom, TextChild.Slot.Offsets.Bottom));
	TestTrue(TEXT("Slot padding round-trips"), FUMGValueParser::IsNearlyEqualMargin(ParsedText.Slot.Padding, TextChild.Slot.Padding));
	TestTrue(TEXT("Slot horizontal alignment round-trips"), ParsedText.Slot.HorizontalAlignment == TextChild.Slot.HorizontalAlignment);
	TestTrue(TEXT("Slot vertical alignment round-trips"), ParsedText.Slot.VerticalAlignment == TextChild.Slot.VerticalAlignment);
	TestTrue(TEXT("Slot size rule round-trips"), ParsedText.Slot.SizeRule == TextChild.Slot.SizeRule);
	TestTrue(TEXT("Slot fill weight round-trips"), FMath::IsNearlyEqual(ParsedText.Slot.FillWeight, TextChild.Slot.FillWeight));
	TestEqual(TEXT("Slot auto-size round-trips"), ParsedText.Slot.bAutoSize, TextChild.Slot.bAutoSize);
	TestTrue(TEXT("Text style font size round-trips"), FMath::IsNearlyEqual(ParsedText.TextStyle.FontSize, TextChild.TextStyle.FontSize));
	TestTrue(TEXT("Text style source font size round-trips"), FMath::IsNearlyEqual(ParsedText.TextStyle.SourceFontSize, TextChild.TextStyle.SourceFontSize));
	TestTrue(TEXT("Text style letter spacing round-trips"), ParsedText.TextStyle.LetterSpacing.IsSet() && FMath::IsNearlyEqual(ParsedText.TextStyle.LetterSpacing.GetValue(), TextChild.TextStyle.LetterSpacing.GetValue()));
	TestEqual(TEXT("Text content round-trips"), ParsedText.Text.Text.GetValue(), TextChild.Text.Text.GetValue());
	TestEqual(TEXT("Text font family round-trips"), ParsedText.Text.FontFamily.GetValue(), TextChild.Text.FontFamily.GetValue());
	TestEqual(TEXT("Text typeface round-trips"), ParsedText.Text.TypefaceFontName.GetValue(), TextChild.Text.TypefaceFontName.GetValue());
	TestEqual(TEXT("Text justification round-trips"), ParsedText.Text.Justification.GetValue(), TextChild.Text.Justification.GetValue());
	TestEqual(TEXT("Text auto-wrap round-trips"), ParsedText.Text.bAutoWrapText.GetValue(), TextChild.Text.bAutoWrapText.GetValue());
	TestTrue(TEXT("Text wrap width round-trips"), ParsedText.Text.WrapTextAt.IsSet() && FMath::IsNearlyEqual(ParsedText.Text.WrapTextAt.GetValue(), TextChild.Text.WrapTextAt.GetValue()));
	TestTrue(TEXT("Text color round-trips"), ParsedText.Text.Color.IsSet() && ParsedText.Text.Color.GetValue().Equals(TextChild.Text.Color.GetValue(), 0.0001f));
	TestTrue(TEXT("Widget style width override round-trips"), ParsedText.WidgetStyle.WidthOverride.IsSet() && FMath::IsNearlyEqual(ParsedText.WidgetStyle.WidthOverride.GetValue(), TextChild.WidgetStyle.WidthOverride.GetValue()));
	TestTrue(TEXT("Widget style height override round-trips"), ParsedText.WidgetStyle.HeightOverride.IsSet() && FMath::IsNearlyEqual(ParsedText.WidgetStyle.HeightOverride.GetValue(), TextChild.WidgetStyle.HeightOverride.GetValue()));
	TestTrue(TEXT("Widget style padding round-trips"), ParsedText.WidgetStyle.Padding.IsSet() && FUMGValueParser::IsNearlyEqualMargin(ParsedText.WidgetStyle.Padding.GetValue(), TextChild.WidgetStyle.Padding.GetValue()));
	TestTrue(TEXT("Widget style inner slot padding round-trips"), ParsedText.WidgetStyle.InnerSlotPadding.IsSet() && ParsedText.WidgetStyle.InnerSlotPadding.GetValue().Equals(TextChild.WidgetStyle.InnerSlotPadding.GetValue(), 0.0001f));
	TestEqual(TEXT("Widget style visibility round-trips"), ParsedText.WidgetStyle.Visibility.GetValue(), TextChild.WidgetStyle.Visibility.GetValue());
	TestEqual(TEXT("Widget style tooltip round-trips"), ParsedText.WidgetStyle.ToolTipText.GetValue(), TextChild.WidgetStyle.ToolTipText.GetValue());
	TestEqual(TEXT("Widget style enabled flag round-trips"), ParsedText.WidgetStyle.IsEnabled.GetValue(), TextChild.WidgetStyle.IsEnabled.GetValue());

	const FUMGWidgetNode& ParsedImage = ParsedRoot.Children[1];
	TestEqual(TEXT("Image child type round-trips"), ParsedImage.Type, ImageChild.Type);
	TestEqual(TEXT("Image child id round-trips"), ParsedImage.Id, ImageChild.Id);
	TestTrue(TEXT("Brush image texture path round-trips"), ParsedImage.Brush.Image.TexturePath.IsSet() && ParsedImage.Brush.Image.TexturePath.GetValue() == ImageChild.Brush.Image.TexturePath.GetValue());
	TestEqual(TEXT("Brush image mode round-trips"), ParsedImage.Brush.Image.Mode.GetValue(), ImageChild.Brush.Image.Mode.GetValue());
	TestTrue(TEXT("Brush image transform round-trips"),
		ParsedImage.Brush.Image.Transform.IsSet()
		&& FMath::IsNearlyEqual(ParsedImage.Brush.Image.Transform.GetValue().A, ImageChild.Brush.Image.Transform.GetValue().A)
		&& FMath::IsNearlyEqual(ParsedImage.Brush.Image.Transform.GetValue().B, ImageChild.Brush.Image.Transform.GetValue().B)
		&& FMath::IsNearlyEqual(ParsedImage.Brush.Image.Transform.GetValue().TX, ImageChild.Brush.Image.Transform.GetValue().TX)
		&& FMath::IsNearlyEqual(ParsedImage.Brush.Image.Transform.GetValue().C, ImageChild.Brush.Image.Transform.GetValue().C)
		&& FMath::IsNearlyEqual(ParsedImage.Brush.Image.Transform.GetValue().D, ImageChild.Brush.Image.Transform.GetValue().D)
		&& FMath::IsNearlyEqual(ParsedImage.Brush.Image.Transform.GetValue().TY, ImageChild.Brush.Image.Transform.GetValue().TY));
	TestTrue(TEXT("Brush outline corner radius round-trips"), ParsedImage.Brush.OutlineCornerRadius.IsSet() && FMath::IsNearlyEqual(ParsedImage.Brush.OutlineCornerRadius.GetValue(), ImageChild.Brush.OutlineCornerRadius.GetValue()));
	TestTrue(TEXT("Brush corner radii round-trip"), ParsedImage.Brush.CornerRadii.IsSet() && ParsedImage.Brush.CornerRadii.GetValue().Equals(ImageChild.Brush.CornerRadii.GetValue(), 0.0001f));
	TestTrue(TEXT("Brush outline thickness round-trips"), ParsedImage.Brush.OutlineThickness.IsSet() && FMath::IsNearlyEqual(ParsedImage.Brush.OutlineThickness.GetValue(), ImageChild.Brush.OutlineThickness.GetValue()));
	TestTrue(TEXT("Brush slicing margin round-trips without precision loss"), ParsedImage.Brush.Margin.IsSet() && ParsedImage.Brush.Margin.GetValue() == ImageChild.Brush.Margin.GetValue());
	TestTrue(TEXT("Brush draw-as round-trips"), ParsedImage.Brush.DrawAs.IsSet() && ParsedImage.Brush.DrawAs.GetValue() == ESlateBrushDrawType::Image);
	TestTrue(TEXT("Brush color round-trips"), ParsedImage.Brush.BrushColor.IsSet() && ParsedImage.Brush.BrushColor.GetValue().Equals(ImageChild.Brush.BrushColor.GetValue(), 0.0001f));
	TestTrue(TEXT("Brush outline color round-trips"), ParsedImage.Brush.OutlineColor.IsSet() && ParsedImage.Brush.OutlineColor.GetValue().Equals(ImageChild.Brush.OutlineColor.GetValue(), 0.0001f));
	TestEqual(TEXT("Gradient type round-trips"), ParsedImage.Brush.Gradient.Type.GetValue(), ImageChild.Brush.Gradient.Type.GetValue());
	TestTrue(TEXT("Gradient rotation round-trips"), ParsedImage.Brush.Gradient.Rotation.IsSet() && FMath::IsNearlyEqual(ParsedImage.Brush.Gradient.Rotation.GetValue(), ImageChild.Brush.Gradient.Rotation.GetValue()));
	TestTrue(TEXT("Gradient start point round-trips"), ParsedImage.Brush.Gradient.StartPoint.IsSet() && ParsedImage.Brush.Gradient.StartPoint.GetValue().Equals(ImageChild.Brush.Gradient.StartPoint.GetValue(), 0.0001f));
	TestTrue(TEXT("Gradient end point round-trips"), ParsedImage.Brush.Gradient.EndPoint.IsSet() && ParsedImage.Brush.Gradient.EndPoint.GetValue().Equals(ImageChild.Brush.Gradient.EndPoint.GetValue(), 0.0001f));
	TestEqual(TEXT("Gradient stop count round-trips"), ParsedImage.Brush.Gradient.Stops.Num(), ImageChild.Brush.Gradient.Stops.Num());
	if (ParsedImage.Brush.Gradient.Stops.Num() == ImageChild.Brush.Gradient.Stops.Num())
	{
		for (int32 StopIndex = 0; StopIndex < ImageChild.Brush.Gradient.Stops.Num(); ++StopIndex)
		{
			TestTrue(*FString::Printf(TEXT("Gradient stop %d color round-trips"), StopIndex), ParsedImage.Brush.Gradient.Stops[StopIndex].Color.Equals(ImageChild.Brush.Gradient.Stops[StopIndex].Color, 0.0001f));
			TestTrue(*FString::Printf(TEXT("Gradient stop %d position round-trips"), StopIndex), FMath::IsNearlyEqual(ParsedImage.Brush.Gradient.Stops[StopIndex].Position, ImageChild.Brush.Gradient.Stops[StopIndex].Position));
		}
	}
	TestEqual(TEXT("Path geometry round-trips"), ParsedImage.Path.Geometry.GetValue(), ImageChild.Path.Geometry.GetValue());
	TestEqual(TEXT("Path fill rule round-trips"), ParsedImage.Path.FillRule.GetValue(), ImageChild.Path.FillRule.GetValue());
	TestTrue(TEXT("Path stroke color round-trips"), ParsedImage.Path.StrokeColor.IsSet() && ParsedImage.Path.StrokeColor.GetValue().Equals(ImageChild.Path.StrokeColor.GetValue(), 0.0001f));
	TestTrue(TEXT("Path stroke thickness round-trips"), ParsedImage.Path.StrokeThickness.IsSet() && FMath::IsNearlyEqual(ParsedImage.Path.StrokeThickness.GetValue(), ImageChild.Path.StrokeThickness.GetValue()));
	return true;
}

#endif // WITH_AUTOMATION_WORKER
