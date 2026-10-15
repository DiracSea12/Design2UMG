// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGAutomationTestAdapter.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"

#if WITH_AUTOMATION_WORKER

namespace
{
	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		return Object;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUMGJsonExplicitPresenceContractTest,
	"Design2UMG.Json.ExplicitPresenceContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUMGJsonExplicitPresenceContractTest::RunTest(const FString&)
{
	FUMGWidgetData Source;
	Source.WidgetName = TEXT("WBP_PresenceContract");
	Source.RootWidget.Type = TEXT("TextBlock");
	Source.RootWidget.Name = TEXT("PresenceRoot");
	Source.RootWidget.TextStyle.LetterSpacing = 0.0f;
	Source.RootWidget.WidgetStyle.WidthOverride = 0.0f;
	Source.RootWidget.WidgetStyle.ToolTipText = FString();
	Source.RootWidget.WidgetStyle.IsEnabled = false;
	Source.RootWidget.Text.Text = FString();
	Source.RootWidget.Text.bAutoWrapText = false;
	Source.RootWidget.Text.WrapTextAt = 0.0f;
	Source.RootWidget.Brush.MaterialRef = FString();
	Source.RootWidget.Effect.BlurStrength = 0.0f;
	Source.RootWidget.Effect.bRetainRendering = false;
	Source.RootWidget.Path.Geometry = FString();
	Source.RootWidget.Path.StrokeThickness = 0.0f;
	Source.RootWidget.Shadow.ShadowBlur = 0.0f;
	Source.RootWidget.Render.RenderOpacity = 0.0f;
	Source.RootWidget.Render.Clipping = FString();
	Source.RootWidget.WidgetState.ActiveWidgetIndex = 0;
	Source.RootWidget.ScrollBox.Orientation = FString();

	const FString Json = FUMGJsonSerializer::Serialize(Source);
	const TSharedPtr<FJsonObject> Serialized = ParseObject(Json);
	if (!TestTrue(TEXT("Serializer emits valid JSON object"), Serialized.IsValid()))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Root = Serialized->GetObjectField(TEXT("RootWidget"));
	const TSharedPtr<FJsonObject> TextStyle = Root->GetObjectField(TEXT("TextStyle"));
	const TSharedPtr<FJsonObject> WidgetStyle = Root->GetObjectField(TEXT("WidgetStyle"));
	const TSharedPtr<FJsonObject> Text = Root->GetObjectField(TEXT("TextData"));
	TestEqual(TEXT("Explicit-zero letter spacing is emitted under its independent key"),
		TextStyle->GetNumberField(UMGPropertyKeys::LetterSpacing), 0.0);
	TestEqual(TEXT("Explicit-zero width is emitted under its independent key"),
		WidgetStyle->GetNumberField(UMGPropertyKeys::WidthOverride), 0.0);
	TestFalse(TEXT("Explicit-false enabled state is emitted"), WidgetStyle->GetBoolField(UMGPropertyKeys::IsEnabled));
	TestEqual(TEXT("Explicit-empty tooltip is emitted"), WidgetStyle->GetStringField(UMGPropertyKeys::ToolTipText), FString());
	TestEqual(TEXT("Explicit-empty text is emitted"), Text->GetStringField(UMGPropertyKeys::Text), FString());
	TestFalse(TEXT("Explicit-false wrap state is emitted"), Text->GetBoolField(UMGPropertyKeys::AutoWrapText));
	TestEqual(TEXT("Explicit-zero wrap width is emitted"), Text->GetNumberField(UMGPropertyKeys::WrapTextAt), 0.0);
	TestEqual(TEXT("Explicit-empty material reference is emitted"),
		Root->GetObjectField(TEXT("Brush"))->GetStringField(UMGPropertyKeys::MaterialRef), FString());

	FUMGWidgetData Parsed;
	FString ParseError;
	if (!TestTrue(TEXT("Presence fixture parses"), FUMGJsonParser::ParseJson(Json, Parsed, &ParseError)))
	{
		AddError(ParseError);
		return false;
	}
	const FUMGWidgetNode& Node = Parsed.RootWidget;
	TestTrue(TEXT("Zero number remains present"),
		Node.TextStyle.LetterSpacing.IsSet() && Node.TextStyle.LetterSpacing.GetValue() == 0.0f
		&& Node.WidgetStyle.WidthOverride.IsSet() && Node.WidgetStyle.WidthOverride.GetValue() == 0.0f
		&& Node.Effect.BlurStrength.IsSet() && Node.Effect.BlurStrength.GetValue() == 0.0f
		&& Node.Path.StrokeThickness.IsSet() && Node.Path.StrokeThickness.GetValue() == 0.0f
		&& Node.Shadow.ShadowBlur.IsSet() && Node.Shadow.ShadowBlur.GetValue() == 0.0f
		&& Node.Render.RenderOpacity.IsSet() && Node.Render.RenderOpacity.GetValue() == 0.0f
		&& Node.WidgetState.ActiveWidgetIndex.IsSet() && Node.WidgetState.ActiveWidgetIndex.GetValue() == 0);
	TestTrue(TEXT("False bool remains present"),
		Node.WidgetStyle.IsEnabled.IsSet() && !Node.WidgetStyle.IsEnabled.GetValue()
		&& Node.Text.bAutoWrapText.IsSet() && !Node.Text.bAutoWrapText.GetValue()
		&& Node.Effect.bRetainRendering.IsSet() && !Node.Effect.bRetainRendering.GetValue());
	TestTrue(TEXT("Empty string remains present"),
		Node.Text.Text.IsSet() && Node.Text.Text.GetValue().IsEmpty()
		&& Node.WidgetStyle.ToolTipText.IsSet() && Node.WidgetStyle.ToolTipText.GetValue().IsEmpty()
		&& Node.Brush.MaterialRef.IsSet() && Node.Brush.MaterialRef.GetValue().IsEmpty()
		&& Node.Path.Geometry.IsSet() && Node.Path.Geometry.GetValue().IsEmpty()
		&& Node.Render.Clipping.IsSet() && Node.Render.Clipping.GetValue().IsEmpty()
		&& Node.ScrollBox.Orientation.IsSet() && Node.ScrollBox.Orientation.GetValue().IsEmpty());

	FUMGWidgetNode Unset;
	Unset.Type = TEXT("TextBlock");
	Unset.Name = TEXT("UnsetRoot");
	const TSharedRef<FJsonObject> UnsetJson = FUMGJsonSerializer::SerializeWidgetNode(Unset);
	TestFalse(TEXT("Unset text style section is omitted"), UnsetJson->HasField(TEXT("TextStyle")));
	TestFalse(TEXT("Unset widget style section is omitted"), UnsetJson->HasField(TEXT("WidgetStyle")));
	TestFalse(TEXT("Unset text section is omitted"), UnsetJson->HasField(TEXT("TextData")));
	TestFalse(TEXT("Unset brush section is omitted"), UnsetJson->HasField(TEXT("Brush")));
	TestFalse(TEXT("Unset effect section is omitted"), UnsetJson->HasField(TEXT("Effect")));
	TestFalse(TEXT("Unset path section is omitted"), UnsetJson->HasField(TEXT("PathData")));
	TestFalse(TEXT("Unset shadow section is omitted"), UnsetJson->HasField(TEXT("ShadowData")));
	TestFalse(TEXT("Unset render section is omitted"), UnsetJson->HasField(TEXT("Render")));
	TestFalse(TEXT("Unset widget-state section is omitted"), UnsetJson->HasField(TEXT("WidgetState")));
	TestFalse(TEXT("Unset scroll-box section is omitted"), UnsetJson->HasField(TEXT("ScrollBox")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUMGJsonInvalidOptionalTypesContractTest,
	"Design2UMG.Json.InvalidOptionalTypesContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUMGJsonInvalidOptionalTypesContractTest::RunTest(const FString&)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid"), ELogVerbosity::Warning,
		Design2UMGAutomationTestAdapter::EMessageMatch::Contains, 8);
	const FString Json = TEXT(R"JSON({
		"RootWidget": {
			"Type": "TextBlock",
			"Name": "InvalidOptionalTypes",
			"SyntheticJustifySpacer": "true",
			"Slot": { "Row": "1", "Column": 2, "bAutoSize": 1 },
			"TextStyle": { "LetterSpacing": "0" },
			"WidgetStyle": { "WidthOverride": false, "IsEnabled": 0, "ToolTipText": [] },
			"TextData": { "Text": 0, "HintText": "kept", "AutoWrapText": "false", "WrapTextAt": [] },
			"Brush": { "OutlineThickness": "0", "MaterialRef": false, "Image": { "TexturePath": 7 } },
			"Effect": { "BlurStrength": [], "RetainRendering": "false", "EffectMaterial": true },
			"PathData": { "PathGeometry": {}, "PathStrokeThickness": "0" },
			"ShadowData": { "ShadowBlur": false },
			"Render": { "RenderOpacity": "0", "Clipping": [] },
			"WidgetState": { "ActiveWidgetIndex": "0" },
			"ScrollBox": { "Orientation": false }
		}
	})JSON");

	FUMGWidgetData Parsed;
	FString Error;
	if (!TestTrue(TEXT("Wrong optional types do not reject an otherwise valid widget"),
		FUMGJsonParser::ParseJson(Json, Parsed, &Error)))
	{
		AddError(Error);
		return false;
	}
	const FUMGWidgetNode& Node = Parsed.RootWidget;
	TestFalse(TEXT("Wrong layout-marker type is ignored"), Node.bSyntheticJustifySpacer);
	TestEqual(TEXT("Wrong integer slot type leaves default"), Node.Slot.Row, 0);
	TestEqual(TEXT("Valid integer slot sibling survives"), Node.Slot.Column, 2);
	TestFalse(TEXT("Wrong bool slot type leaves default"), Node.Slot.bAutoSize);
	TestTrue(TEXT("Valid string sibling survives invalid text fields"),
		Node.Text.HintText.IsSet() && Node.Text.HintText.GetValue() == TEXT("kept"));
	TestFalse(TEXT("Wrong numeric types remain unset"),
		Node.TextStyle.LetterSpacing.IsSet() || Node.WidgetStyle.WidthOverride.IsSet()
		|| Node.Text.WrapTextAt.IsSet() || Node.Brush.OutlineThickness.IsSet()
		|| Node.Effect.BlurStrength.IsSet() || Node.Path.StrokeThickness.IsSet()
		|| Node.Shadow.ShadowBlur.IsSet() || Node.Render.RenderOpacity.IsSet()
		|| Node.WidgetState.ActiveWidgetIndex.IsSet());
	TestFalse(TEXT("Wrong bool types remain unset"),
		Node.WidgetStyle.IsEnabled.IsSet() || Node.Text.bAutoWrapText.IsSet()
		|| Node.Effect.bRetainRendering.IsSet());
	TestFalse(TEXT("Wrong string types remain unset"),
		Node.Text.Text.IsSet() || Node.WidgetStyle.ToolTipText.IsSet()
		|| Node.Brush.MaterialRef.IsSet() || Node.Brush.Image.TexturePath.IsSet()
		|| Node.Effect.EffectMaterial.IsSet() || Node.Path.Geometry.IsSet()
		|| Node.Render.Clipping.IsSet() || Node.ScrollBox.Orientation.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUMGJsonExtensionAndLayoutBoundaryContractTest,
	"Design2UMG.Json.ExtensionAndLayoutBoundaryContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUMGJsonExtensionAndLayoutBoundaryContractTest::RunTest(const FString&)
{
	FUMGWidgetData Source;
	Source.RootWidget.Type = TEXT("Border");
	Source.RootWidget.Name = TEXT("LayoutMarkerRoot");
	Source.RootWidget.bSyntheticJustifySpacer = true;
	Source.RootWidget.Slot.FillWeight = 0.0f;
	Source.RootWidget.Slot.Row = 0;
	Source.RootWidget.Slot.Column = 4;
	Source.RootWidget.Slot.RowSpan = 1;
	Source.RootWidget.Slot.ColumnSpan = 3;
	Source.RootWidget.Slot.bAutoSize = true;
	Source.RootWidget.MutableSource().SourceNodeId = TEXT("source-node");
	Source.RootWidget.MutableSource().ComponentId = TEXT("component-id");
	Source.MutableSourceDocument().Provider = TEXT("figma");
	Source.MutableSourceDocument().DocumentScope = TEXT("file-key");
	FUMGPrototypeNavigationBinding& Binding = Source.MutablePrototypeNavigation().ClickBindings.AddDefaulted_GetRef();
	Binding.TriggerWidgetId = TEXT("trigger");
	Binding.TriggerWidgetName = TEXT("Trigger");
	Binding.Action = EUMGPrototypeNavigationAction::Back;

	const FString Json = FUMGJsonSerializer::Serialize(Source);
	const TSharedPtr<FJsonObject> Serialized = ParseObject(Json);
	if (!TestTrue(TEXT("Boundary fixture serializes"), Serialized.IsValid()))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Root = Serialized->GetObjectField(TEXT("RootWidget"));
	const TSharedPtr<FJsonObject> Slot = Root->GetObjectField(TEXT("Slot"));
	TestTrue(TEXT("True synthetic-spacer marker is emitted"), Root->GetBoolField(TEXT("SyntheticJustifySpacer")));
	TestEqual(TEXT("Non-default grid column is emitted"), Slot->GetIntegerField(TEXT("Column")), 4);
	TestEqual(TEXT("Non-default column span is emitted"), Slot->GetIntegerField(TEXT("ColumnSpan")), 3);
	TestEqual(TEXT("Explicit zero fill weight is emitted"), Slot->GetNumberField(TEXT("FillWeight")), 0.0);
	TestTrue(TEXT("True auto-size marker is emitted"), Slot->GetBoolField(TEXT("bAutoSize")));
	TestFalse(TEXT("Default row is omitted"), Slot->HasField(TEXT("Row")));
	TestFalse(TEXT("Default row span is omitted"), Slot->HasField(TEXT("RowSpan")));
	TestFalse(TEXT("Node source extension is outside the visual JSON bridge"),
		Root->HasField(TEXT("Source")) || Root->HasField(TEXT("SourceData")));
	TestFalse(TEXT("Document source extension is outside the visual JSON bridge"),
		Serialized->HasField(TEXT("SourceDocument")) || Serialized->HasField(TEXT("SourceDocumentData")));
	TestFalse(TEXT("Prototype navigation is outside the visual JSON bridge"),
		Serialized->HasField(TEXT("PrototypeNavigation")) || Serialized->HasField(TEXT("PrototypeNavigationData")));

	FUMGWidgetData Parsed;
	FString Error;
	if (!TestTrue(TEXT("Layout marker JSON parses"), FUMGJsonParser::ParseJson(Json, Parsed, &Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("Synthetic-spacer marker roundtrips"), Parsed.RootWidget.bSyntheticJustifySpacer);
	TestEqual(TEXT("Grid column roundtrips"), Parsed.RootWidget.Slot.Column, 4);
	TestEqual(TEXT("Column span roundtrips"), Parsed.RootWidget.Slot.ColumnSpan, 3);
	TestEqual(TEXT("Zero fill weight roundtrips"), Parsed.RootWidget.Slot.FillWeight, 0.0f);
	TestTrue(TEXT("Auto-size marker roundtrips"), Parsed.RootWidget.Slot.bAutoSize);
	TestFalse(TEXT("Parsing visual JSON does not fabricate node source extension"), Parsed.RootWidget.HasSourceData());
	TestFalse(TEXT("Parsing visual JSON does not fabricate document source extension"), Parsed.HasSourceDocumentData());
	TestFalse(TEXT("Parsing visual JSON does not fabricate prototype navigation"), Parsed.HasPrototypeNavigationData());

	FUMGWidgetNode DefaultMarkers;
	DefaultMarkers.Type = TEXT("Border");
	DefaultMarkers.Name = TEXT("DefaultMarkers");
	const TSharedRef<FJsonObject> DefaultJson = FUMGJsonSerializer::SerializeWidgetNode(DefaultMarkers);
	TestFalse(TEXT("False synthetic-spacer marker is omitted"), DefaultJson->HasField(TEXT("SyntheticJustifySpacer")));
	TestFalse(TEXT("False auto-size marker is omitted"), DefaultJson->GetObjectField(TEXT("Slot"))->HasField(TEXT("bAutoSize")));
	return true;
}

#endif // WITH_AUTOMATION_WORKER
