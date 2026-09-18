// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGJsonParser.h"
#include "Design2UMGLog.h"
#include "DesignJsonUtils.h"
#include "UMGJsonKeys.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetTypes.h"
#include "UMGValueParsing.h"
#include "Dom/JsonObject.h"
#include "Misc/Guid.h"

// ============================================================================
// Helper functions.
// ============================================================================

namespace
{
	bool TryParseVector4(const FString& Value, FVector4& OutValue)
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","), false);
		float Parsed[4];
		if (Parts.Num() != UE_ARRAY_COUNT(Parsed))
		{
			return false;
		}
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Parsed); ++Index)
		{
			if (!FUMGValueParser::TryParseFloat(Parts[Index], Parsed[Index]))
			{
				return false;
			}
		}
		OutValue = FVector4(Parsed[0], Parsed[1], Parsed[2], Parsed[3]);
		return true;
	}

	TOptional<FVector2D> ParseVector2DValue(const TSharedPtr<FJsonValue>& Value, const TCHAR* Field)
	{
		FVector2D Parsed;
		if (Value->Type == EJson::String && FUMGValueParser::TryParseVector2D(Value->AsString(), Parsed))
		{
			return Parsed;
		}
		UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid vector for %s; expected a string of two finite numbers."), Field);
		return {};
	}

	bool TryReadFiniteFloat(const FJsonValue& Value, double& OutValue)
	{
		return Value.TryGetNumber(OutValue) && FMath::IsFinite(OutValue) && FMath::Abs(OutValue) <= MAX_flt;
	}

	// Keep double precision for pre-narrowing rules such as the negative radius sentinel.
	bool TryGetFiniteFloatField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double& OutValue)
	{
		const TSharedPtr<FJsonValue> Value = DesignJsonUtils::TryGetField(Object, Field);
		if (!Value.IsValid())
		{
			return false;
		}
		double Parsed;
		if (TryReadFiniteFloat(*Value, Parsed))
		{
			OutValue = Parsed;
			return true;
		}
		UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid %s; expected a finite float-range number."), Field);
		return false;
	}

	TOptional<FUMGVisualBrushData::FShapeData> ParseShapeValue(const TSharedPtr<FJsonValue>& Value)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value->TryGetObject(Object))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid Brush.Shape; expected an object."));
			return {};
		}

		FUMGVisualBrushData::FShapeData Shape;
		const auto ReadFloat = [&](const TCHAR* Field, TOptional<float>& Out)
		{
			const auto FieldValue = DesignJsonUtils::TryGetField(*Object, Field);
			if (!FieldValue) return;
			double Number;
			if (FieldValue->Type == EJson::Number && TryReadFiniteFloat(*FieldValue, Number)) Out = static_cast<float>(Number);
			else UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid Brush.Shape.%s; expected a finite float-range number."), Field);
		};
		const auto ReadInt = [&](const TCHAR* Field, TOptional<int32>& Out)
		{
			const auto FieldValue = DesignJsonUtils::TryGetField(*Object, Field);
			if (!FieldValue) return;
			int32 Number;
			if (FieldValue->Type == EJson::Number && FieldValue->TryGetNumber(Number)) Out = Number;
			else UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid Brush.Shape.%s; expected an int32-range number."), Field);
		};
		const auto ReadColor = [&](const TCHAR* Field, TOptional<FLinearColor>& Out)
		{
			const auto FieldValue = DesignJsonUtils::TryGetField(*Object, Field);
			if (!FieldValue) return;
			if (FieldValue->Type == EJson::String) Out = FUMGValueParser::ParseColor(FieldValue->AsString());
			else UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid Brush.Shape.%s; expected a color string."), Field);
		};
		ReadFloat(TEXT("Thickness"), Shape.Thickness);
		ReadFloat(TEXT("CornerRadius"), Shape.CornerRadius);
		ReadFloat(TEXT("BlurPadding"), Shape.BlurPadding);
		ReadInt(TEXT("StripeWidth"), Shape.StripeWidth);
		ReadInt(TEXT("StripeGap"), Shape.StripeGap);
		ReadInt(TEXT("Inset"), Shape.Inset);
		ReadColor(TEXT("Color"), Shape.Color);
		ReadColor(TEXT("StripeColor"), Shape.StripeColor);
		if (const auto RadiiValue = DesignJsonUtils::TryGetField(*Object, TEXT("CornerRadii")))
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			bool bValid = RadiiValue->TryGetArray(Values) && Values->Num() == 4;
			FVector4 Radii;
			for (int32 Index = 0; bValid && Index < 4; ++Index)
			{
				double Number;
				bValid = (*Values)[Index]->Type == EJson::Number && TryReadFiniteFloat(*(*Values)[Index], Number);
				if (bValid) Radii[Index] = Number;
			}
			if (bValid) Shape.CornerRadii = Radii;
			else UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid Brush.Shape.CornerRadii; expected four finite float-range numbers."));
		}
		return Shape;
	}

	TOptional<FMargin> ParsePaddingValue(const TSharedPtr<FJsonValue>& Value, const TCHAR* Field)
	{
		if (Value->Type == EJson::String)
		{
			const FString Str = Value->AsString();
			float Uniform;
			if (FUMGValueParser::TryParseFloat(Str, Uniform))
			{
				return FMargin(Uniform);
			}
			FVector4 Sides;
			if (TryParseVector4(Str, Sides))
			{
				return FMargin(Sides.X, Sides.Y, Sides.Z, Sides.W);
			}
		}
		UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid padding for %s; expected one or four finite numbers."), Field);
		return {};
	}

}

// ============================================================================
// Main parsing logic.
// ============================================================================

bool FUMGJsonParser::ParseJson(const FString& JsonString, FUMGWidgetData& OutWidgetData, FString* OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!DesignJsonUtils::TryParseObject(JsonString, Root))
	{
		if (OutError) *OutError = TEXT("Invalid JSON");
		return false;
	}

	// Parse RootWidget.
	const TSharedPtr<FJsonObject>* RootWidget;
	if (!DesignJsonUtils::TryGetObject(Root, TEXT("RootWidget"), RootWidget) || !ParseWidgetNode(*RootWidget, OutWidgetData.RootWidget))
	{
		if (OutError) *OutError = TEXT("Invalid UI structure");
		return false;
	}

	// Treat an empty pure-layout container as an invalid result.
	// Interactive widgets such as CheckBox can also derive from panel types, so keep this allowlist narrow.
	if (OutWidgetData.RootWidget.Children.Num() == 0 &&
		UMGWidgetTypes::IsPureLayoutContainer(OutWidgetData.RootWidget.Type))
	{
		if (OutError) *OutError = TEXT("Empty UI result");
		return false;
	}

	DesignJsonUtils::TryGetString(Root, TEXT("WidgetName"), OutWidgetData.WidgetName);
	if (OutWidgetData.WidgetName.IsEmpty()) OutWidgetData.WidgetName = TEXT("WBP_Generated");

	return true;
}

bool FUMGJsonParser::ParseWidgetNode(const TSharedPtr<FJsonObject>& JsonObject, FUMGWidgetNode& OutNode)
{
	if (!JsonObject.IsValid())
	{
		return false;
	}

	// Parse required fields without crashing on missing keys.
	if (!DesignJsonUtils::TryGetString(JsonObject, UMGJsonKeys::Type, OutNode.Type) || OutNode.Type.IsEmpty())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("FUMGJsonParser: Widget node missing Type field"));
		return false;
	}

	// Name is optional; synthesize one when absent.
	if (!DesignJsonUtils::TryGetString(JsonObject, TEXT("Name"), OutNode.Name) || OutNode.Name.IsEmpty())
	{
		OutNode.Name = FString::Printf(TEXT("%s_%s"), *OutNode.Type, *FGuid::NewGuid().ToString(EGuidFormats::Short));
	}

	// Parse v2.2 path-based stable id.
	DesignJsonUtils::TryGetString(JsonObject, TEXT("Id"), OutNode.Id);

	// Parse the LayoutMapper justify-spacer marker (absent in older JSON means false).
	DesignJsonUtils::TryGetBool(JsonObject, TEXT("SyntheticJustifySpacer"), OutNode.bSyntheticJustifySpacer);

	// Parse Bounds, added in v2.2 for normalized coordinates.
	const TSharedPtr<FJsonObject>* BoundsObject;
	if (DesignJsonUtils::TryGetObject(JsonObject, TEXT("Bounds"), BoundsObject))
	{
		float Val;
		if (DesignJsonUtils::TryGetFloat(*BoundsObject, TEXT("X"), Val)) OutNode.Bounds.X = Val;
		if (DesignJsonUtils::TryGetFloat(*BoundsObject, TEXT("Y"), Val)) OutNode.Bounds.Y = Val;
		if (DesignJsonUtils::TryGetFloat(*BoundsObject, TEXT("W"), Val)) OutNode.Bounds.W = Val;
		if (DesignJsonUtils::TryGetFloat(*BoundsObject, TEXT("H"), Val)) OutNode.Bounds.H = Val;
	}

	// Parse Slot data.
	const TSharedPtr<FJsonObject>* SlotObject;
	if (DesignJsonUtils::TryGetObject(JsonObject, TEXT("Slot"), SlotObject))
	{
		ParseSlotData(*SlotObject, OutNode.Slot);
	}

	// Parse each typed payload section; the payload parsers read their own section key.
	ParseTextStylePayload(JsonObject, OutNode.TextStyle);
	ParseWidgetStylePayload(JsonObject, OutNode.WidgetStyle);
	ParseTextPayload(JsonObject, OutNode.Text);
	ParseBrushPayload(JsonObject, OutNode.Brush);
	ParseEffectPayload(JsonObject, OutNode.Effect);
	ParsePathPayload(JsonObject, OutNode.Path);
	ParseShadowPayload(JsonObject, OutNode.Shadow);
	ParseRenderPayload(JsonObject, OutNode.Render);
	ParseWidgetStatePayload(JsonObject, OutNode.WidgetState);
	ParseScrollBoxPayload(JsonObject, OutNode.ScrollBox);

	// Parse Children in the standard format.
	if (const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = DesignJsonUtils::GetArray(JsonObject, TEXT("Children")))
	{
		for (const TSharedPtr<FJsonValue>& ChildValue : *ChildrenArray)
		{
			const TSharedPtr<FJsonObject>* ChildObject;
			if (ChildValue->TryGetObject(ChildObject))
			{
				FUMGWidgetNode ChildNode;
				if (ParseWidgetNode(*ChildObject, ChildNode))
				{
					OutNode.Children.Add(ChildNode);
				}
			}
		}
	}

	return true;
}

void FUMGJsonParser::ParseTextStylePayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGTextStyleData& OutTextStyle)
{
	// Parse TextStyle (v2.4).
	const TSharedPtr<FJsonObject>* TSObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("TextStyle"), TSObject))
	{
		float Val;
		if (DesignJsonUtils::TryGetFloat(*TSObject, UMGPropertyKeys::FontSize, Val))
			OutTextStyle.FontSize = Val;
		if (DesignJsonUtils::TryGetFloat(*TSObject, UMGJsonKeys::SourceFontSize, Val))
			OutTextStyle.SourceFontSize = Val;
		if (DesignJsonUtils::TryGetFloat(*TSObject, UMGPropertyKeys::LetterSpacing, Val))
		{
			OutTextStyle.LetterSpacing = Val;
		}
	}
}

void FUMGJsonParser::ParseWidgetStylePayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGWidgetStyleData& OutWidgetStyle)
{
	// Parse WidgetStyle
	const TSharedPtr<FJsonObject>* WidgetStyleObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("WidgetStyle"), WidgetStyleObject))
	{
		float Val = 0.0f;
		FString Str;
		if (DesignJsonUtils::TryGetFloat(*WidgetStyleObject, UMGPropertyKeys::WidthOverride, Val))
		{
			OutWidgetStyle.WidthOverride = Val;
		}
		if (DesignJsonUtils::TryGetFloat(*WidgetStyleObject, UMGPropertyKeys::HeightOverride, Val))
		{
			OutWidgetStyle.HeightOverride = Val;
		}
		if (DesignJsonUtils::TryGetFloat(*WidgetStyleObject, UMGPropertyKeys::MinDesiredWidth, Val))
		{
			OutWidgetStyle.MinDesiredWidth = Val;
		}
		if (DesignJsonUtils::TryGetFloat(*WidgetStyleObject, UMGPropertyKeys::MinDesiredHeight, Val))
		{
			OutWidgetStyle.MinDesiredHeight = Val;
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*WidgetStyleObject, UMGPropertyKeys::Padding); JsonValue.IsValid())
		{
			OutWidgetStyle.Padding = ParsePaddingValue(JsonValue, UMGPropertyKeys::Padding);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*WidgetStyleObject, UMGPropertyKeys::InnerSlotPadding); JsonValue.IsValid())
		{
			OutWidgetStyle.InnerSlotPadding = ParseVector2DValue(JsonValue, UMGPropertyKeys::InnerSlotPadding);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*WidgetStyleObject, UMGPropertyKeys::SlotPadding); JsonValue.IsValid())
		{
			OutWidgetStyle.SlotPadding = ParsePaddingValue(JsonValue, UMGPropertyKeys::SlotPadding);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*WidgetStyleObject, UMGPropertyKeys::StrokePadding); JsonValue.IsValid())
		{
			OutWidgetStyle.StrokePadding = ParsePaddingValue(JsonValue, UMGPropertyKeys::StrokePadding);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*WidgetStyleObject, UMGPropertyKeys::StrokeMargin); JsonValue.IsValid())
		{
			OutWidgetStyle.StrokeMargin = ParsePaddingValue(JsonValue, UMGPropertyKeys::StrokeMargin);
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::Stretch, Str))
		{
			OutWidgetStyle.Stretch = Str;
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::StretchDirection, Str))
		{
			OutWidgetStyle.StretchDirection = Str;
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::BackgroundColor, Str))
		{
			OutWidgetStyle.BackgroundColor = FUMGValueParser::ParseColor(Str);
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::HoveredColor, Str))
		{
			OutWidgetStyle.HoveredColor = FUMGValueParser::ParseColor(Str);
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::PressedColor, Str))
		{
			OutWidgetStyle.PressedColor = FUMGValueParser::ParseColor(Str);
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::NamedSlot, Str))
		{
			OutWidgetStyle.NamedSlot = Str;
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::SlotName, Str))
		{
			OutWidgetStyle.SlotName = Str;
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::Visibility, Str))
		{
			OutWidgetStyle.Visibility = Str;
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::ToolTipText, Str))
		{
			OutWidgetStyle.ToolTipText = Str;
		}
		bool BoolValue = false;
		if (DesignJsonUtils::TryGetBool(*WidgetStyleObject, UMGPropertyKeys::IsEnabled, BoolValue))
		{
			OutWidgetStyle.IsEnabled = BoolValue;
		}
	}
}

void FUMGJsonParser::ParseTextPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGTextData& OutText)
{
	// Parse TextData
	const TSharedPtr<FJsonObject>* TextDataObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("TextData"), TextDataObject))
	{
		float Val = 0.0f;
		FString Str;
		bool BoolValue = false;

		if (DesignJsonUtils::TryGetString(*TextDataObject, UMGPropertyKeys::Text, Str))
		{
			OutText.Text = Str;
		}
		if (DesignJsonUtils::TryGetString(*TextDataObject, UMGPropertyKeys::RichTextStyleRows, Str))
		{
			TSharedPtr<FJsonObject> RowsObject;
			if (DesignJsonUtils::TryParseObject(Str, RowsObject))
			{
				TMap<FString, FUMGRichTextStyleRowData> Rows;
				for (const auto& Pair : RowsObject->Values)
				{
					const TSharedPtr<FJsonObject>* RowObject = nullptr;
					if (!Pair.Value->TryGetObject(RowObject) || !RowObject || !RowObject->IsValid()) continue;
					FUMGRichTextStyleRowData Row;
					float Number;
					FString Color;
					DesignJsonUtils::TryGetString(*RowObject, UMGPropertyKeys::FontFamily, Row.FontFamily);
					DesignJsonUtils::TryGetString(*RowObject, UMGPropertyKeys::TypefaceFontName, Row.TypefaceFontName);
					if (DesignJsonUtils::TryGetFloat(*RowObject, UMGPropertyKeys::FontSize, Number)) Row.FontSize = Number;
					if (DesignJsonUtils::TryGetFloat(*RowObject, UMGPropertyKeys::LetterSpacing, Number)) Row.LetterSpacing = Number;
					if (DesignJsonUtils::TryGetString(*RowObject, UMGPropertyKeys::Color, Color) && !Color.IsEmpty()) Row.Color = FUMGValueParser::ParseColor(Color);
					DesignJsonUtils::TryGetBool(*RowObject, UMGPropertyKeys::Underline, Row.bUnderline);
					DesignJsonUtils::TryGetBool(*RowObject, UMGPropertyKeys::Strikethrough, Row.bStrikethrough);
					DesignJsonUtils::TryGetString(*RowObject, UMGPropertyKeys::Href, Row.Href);
					Rows.Add(Pair.Key, MoveTemp(Row));
				}
				OutText.RichTextStyleRows = MoveTemp(Rows);
			}
		}
		if (DesignJsonUtils::TryGetString(*TextDataObject, UMGPropertyKeys::HintText, Str))
		{
			OutText.HintText = Str;
		}
		if (DesignJsonUtils::TryGetString(*TextDataObject, UMGPropertyKeys::FontFamily, Str))
		{
			OutText.FontFamily = Str;
		}
		if (DesignJsonUtils::TryGetString(*TextDataObject, UMGPropertyKeys::TypefaceFontName, Str))
		{
			OutText.TypefaceFontName = Str;
		}
		if (DesignJsonUtils::TryGetString(*TextDataObject, UMGPropertyKeys::FontStyle, Str))
		{
			OutText.FontStyle = Str;
		}
		if (DesignJsonUtils::TryGetString(*TextDataObject, UMGPropertyKeys::Justification, Str))
		{
			OutText.Justification = Str;
		}
		if (DesignJsonUtils::TryGetBool(*TextDataObject, UMGPropertyKeys::AutoWrapText, BoolValue))
		{
			OutText.bAutoWrapText = BoolValue;
		}
		if (DesignJsonUtils::TryGetFloat(*TextDataObject, UMGPropertyKeys::WrapTextAt, Val))
		{
			OutText.WrapTextAt = Val;
		}
		if (DesignJsonUtils::TryGetFloat(*TextDataObject, UMGPropertyKeys::LineHeightPercentage, Val))
		{
			OutText.LineHeightPercentage = Val;
		}
		if (DesignJsonUtils::TryGetFloat(*TextDataObject, UMGPropertyKeys::LineHeightBaselineShift, Val))
		{
			OutText.LineHeightBaselineShift = Val;
		}
		if (DesignJsonUtils::TryGetBool(*TextDataObject, UMGPropertyKeys::Underline, BoolValue))
		{
			OutText.bUnderline = BoolValue;
		}
		if (DesignJsonUtils::TryGetBool(*TextDataObject, UMGPropertyKeys::Strikethrough, BoolValue))
		{
			OutText.bStrikethrough = BoolValue;
		}
		if (DesignJsonUtils::TryGetString(*TextDataObject, UMGPropertyKeys::Href, Str))
		{
			OutText.Href = Str;
		}
		if (DesignJsonUtils::TryGetString(*TextDataObject, UMGPropertyKeys::TextAlignVertical, Str))
		{
			OutText.TextAlignVertical = Str;
		}
		if (DesignJsonUtils::TryGetString(*TextDataObject, UMGPropertyKeys::IconName, Str))
		{
			OutText.IconName = Str;
		}
		if (DesignJsonUtils::TryGetString(*TextDataObject, UMGPropertyKeys::Color, Str))
		{
			OutText.Color = FUMGValueParser::ParseColor(Str);
		}
	}
}

void FUMGJsonParser::ParseBrushPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGVisualBrushData& OutBrush)
{
	// Parse Brush (v2.4).
	const TSharedPtr<FJsonObject>* BrushObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("Brush"), BrushObject))
	{
		// OutlineCornerRadius keeps the double read: the legacy >= 0 "unset" check
		// must run before any float narrowing.
		double Val;
		float FloatVal;
		FString Str;
		// Negative radius is the legacy "unset" encoding; keep it out of the
		// optional so IsSet() preserves the old >= 0 consumer contract.
		if (TryGetFiniteFloatField(*BrushObject, UMGPropertyKeys::OutlineCornerRadius, Val) && Val >= 0.0)
			OutBrush.OutlineCornerRadius = (float)Val;
		if (DesignJsonUtils::TryGetFloat(*BrushObject, UMGPropertyKeys::OutlineThickness, FloatVal))
		{
			OutBrush.OutlineThickness = FloatVal;
		}
		if (const TSharedPtr<FJsonValue> Margin = DesignJsonUtils::TryGetField(*BrushObject, UMGPropertyKeys::BrushMargin); Margin.IsValid())
		{
			OutBrush.Margin = ParsePaddingValue(Margin, UMGPropertyKeys::BrushMargin);
		}
		if (DesignJsonUtils::TryGetString(*BrushObject, UMGPropertyKeys::DrawAs, Str))
		{
			ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType;
			if (Design2UMGWidgetNodeAccess::TryParseDrawAs(Str, DrawAs))
			{
				OutBrush.DrawAs = DrawAs;
			}
		}
		if (DesignJsonUtils::TryGetString(*BrushObject, UMGPropertyKeys::BrushColor, Str))
		{
			OutBrush.BrushColor = FUMGValueParser::ParseColor(Str);
		}
		if (DesignJsonUtils::TryGetString(*BrushObject, UMGPropertyKeys::OutlineColor, Str))
		{
			OutBrush.OutlineColor = FUMGValueParser::ParseColor(Str);
		}
		if (DesignJsonUtils::TryGetString(*BrushObject, UMGPropertyKeys::MaterialRef, Str))
		{
			OutBrush.MaterialRef = Str;
		}
		if (DesignJsonUtils::TryGetString(*BrushObject, UMGPropertyKeys::MaterialParams, Str))
		{
			OutBrush.MaterialParams = Str;
		}
		FString LegacyTexturePath;
		DesignJsonUtils::TryGetString(*BrushObject, UMGPropertyKeys::TexturePath, LegacyTexturePath);
		if (const TSharedPtr<FJsonValue> RadiiValue = DesignJsonUtils::TryGetField(*BrushObject, UMGPropertyKeys::CornerRadii); RadiiValue.IsValid())
		{
			FVector4 Radii;
			if (RadiiValue->Type != EJson::String || !TryParseVector4(RadiiValue->AsString(), Radii))
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid Brush.CornerRadii; ignoring value."));
			}
			else if (Radii.X >= 0.0f)
			{
				OutBrush.CornerRadii = Radii;
			}
		}
		if (const auto ShapeValue = DesignJsonUtils::TryGetField(*BrushObject, TEXT("Shape")))
		{
			OutBrush.Shape = ParseShapeValue(ShapeValue);
		}
		const TSharedPtr<FJsonObject>* ImageObject = nullptr;
		if (DesignJsonUtils::TryGetObject(*BrushObject, TEXT("Image"), ImageObject))
		{
			if (DesignJsonUtils::TryGetString(*ImageObject, UMGPropertyKeys::TexturePath, Str))
			{
				OutBrush.Image.TexturePath = Str;
			}
			if (DesignJsonUtils::TryGetString(*ImageObject, UMGJsonKeys::Mode, Str))
			{
				OutBrush.Image.Mode = Str;
			}
			if (const TSharedPtr<FJsonValue> TransformValue = DesignJsonUtils::TryGetField(*ImageObject, UMGJsonKeys::Transform); TransformValue.IsValid())
			{
				FUMGVisualBrushData::FImageTransformData Transform;
				if (TransformValue->Type == EJson::String && Design2UMGWidgetNodeAccess::TryParseImageTransform(TransformValue->AsString(), Transform))
				{
					OutBrush.Image.Transform = Transform;
				}
				else
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid Brush.Image.Transform; ignoring value."));
				}
			}
		}
		// Existing root alias is accepted only here. The IR has one texture owner.
		if (!OutBrush.Image.TexturePath.IsSet())
		{
			if (!LegacyTexturePath.IsEmpty()) OutBrush.Image.TexturePath = LegacyTexturePath;
		}
		else if (!LegacyTexturePath.IsEmpty() && LegacyTexturePath != OutBrush.Image.TexturePath.GetValue())
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("FUMGJsonParser: Brush.TexturePath conflicts with Brush.Image.TexturePath; using Brush.Image.TexturePath ('%s') instead of legacy/root '%s'."),
				*OutBrush.Image.TexturePath.GetValue(), *LegacyTexturePath);
		}
		const TSharedPtr<FJsonObject>* GradientObject = nullptr;
		if (DesignJsonUtils::TryGetObject(*BrushObject, TEXT("Gradient"), GradientObject))
		{
			if (DesignJsonUtils::TryGetString(*GradientObject, UMGJsonKeys::Type, Str))
			{
				OutBrush.Gradient.Type = Str;
			}
			if (DesignJsonUtils::TryGetFloat(*GradientObject, UMGJsonKeys::Rotation, FloatVal))
			{
				OutBrush.Gradient.Rotation = FloatVal;
			}
			if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*GradientObject, UMGJsonKeys::StartPoint); JsonValue.IsValid())
			{
				OutBrush.Gradient.StartPoint = ParseVector2DValue(JsonValue, UMGJsonKeys::StartPoint);
			}
			if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*GradientObject, UMGJsonKeys::EndPoint); JsonValue.IsValid())
			{
				OutBrush.Gradient.EndPoint = ParseVector2DValue(JsonValue, UMGJsonKeys::EndPoint);
			}
			if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*GradientObject, UMGJsonKeys::Center); JsonValue.IsValid())
			{
				OutBrush.Gradient.Center = ParseVector2DValue(JsonValue, UMGJsonKeys::Center);
			}
			if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*GradientObject, UMGJsonKeys::Size); JsonValue.IsValid())
			{
				OutBrush.Gradient.Size = ParseVector2DValue(JsonValue, UMGJsonKeys::Size);
			}
			if (const TArray<TSharedPtr<FJsonValue>>* StopArray = DesignJsonUtils::GetArray(*GradientObject, UMGJsonKeys::Stops))
			{
				for (const TSharedPtr<FJsonValue>& StopValue : *StopArray)
				{
					const TSharedPtr<FJsonObject>* StopObject = nullptr;
					if (!StopValue->TryGetObject(StopObject) || !StopObject || !(*StopObject).IsValid())
					{
						continue;
					}

					FUMGVisualBrushData::FGradientStopData& Stop = OutBrush.Gradient.Stops.AddDefaulted_GetRef();
					if (DesignJsonUtils::TryGetString(*StopObject, UMGPropertyKeys::Color, Str))
					{
						Stop.Color = FUMGValueParser::ParseColor(Str);
					}
					if (DesignJsonUtils::TryGetFloat(*StopObject, UMGJsonKeys::Position, FloatVal))
					{
						Stop.Position = FloatVal;
					}
				}
			}
		}
	}
	// Preserve the existing input precedence, without carrying two owners downstream.
	const TSharedPtr<FJsonObject>* WidgetStyleObject = nullptr;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("WidgetStyle"), WidgetStyleObject))
	{
		FString Str;
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::ImageMode, Str))
		{
			OutBrush.Image.Mode = Str;
		}
		if (const TSharedPtr<FJsonValue> TransformValue = DesignJsonUtils::TryGetField(*WidgetStyleObject, UMGPropertyKeys::ImageTransform); TransformValue.IsValid())
		{
			FUMGVisualBrushData::FImageTransformData Transform;
			if (TransformValue->Type == EJson::String && Design2UMGWidgetNodeAccess::TryParseImageTransform(TransformValue->AsString(), Transform))
			{
				if (!OutBrush.Image.Transform.IsSet()) OutBrush.Image.Transform = Transform;
			}
			else
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid WidgetStyle.ImageTransform; ignoring value."));
			}
		}
	}

}

void FUMGJsonParser::ParseEffectPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGEffectData& OutEffect)
{
	// Parse Effect (v2.5)
	const TSharedPtr<FJsonObject>* EffectObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("Effect"), EffectObject))
	{
		// Phase/PhaseCount keep the double read: their historical (int32) casts
		// truncate, which differs from the engine int32 field overload's
		// range-check + round-half-from-zero conversion.
		double Val;
		float FloatVal;
		FString Str;
		bool BoolValue = false;
		if (DesignJsonUtils::TryGetFloat(*EffectObject, UMGPropertyKeys::BlurStrength, FloatVal))
		{
			OutEffect.BlurStrength = FloatVal;
		}
		if (DesignJsonUtils::TryGetBool(*EffectObject, UMGPropertyKeys::RetainRendering, BoolValue))
		{
			OutEffect.bRetainRendering = BoolValue;
		}
		if (DesignJsonUtils::TryGetString(*EffectObject, UMGPropertyKeys::EffectMaterial, Str))
		{
			OutEffect.EffectMaterial = Str;
		}
		if (DesignJsonUtils::TryGetString(*EffectObject, UMGPropertyKeys::TextureParameter, Str))
		{
			OutEffect.TextureParameter = Str;
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*EffectObject, UMGPropertyKeys::EffectBlurUVStep); JsonValue.IsValid())
		{
			OutEffect.EffectBlurUVStep = ParseVector2DValue(JsonValue, UMGPropertyKeys::EffectBlurUVStep);
		}
		if (DesignJsonUtils::TryGetString(*EffectObject, UMGPropertyKeys::EffectColor, Str))
		{
			OutEffect.EffectColor = FUMGValueParser::ParseColor(Str);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*EffectObject, UMGPropertyKeys::EffectOffset); JsonValue.IsValid())
		{
			OutEffect.EffectOffset = ParseVector2DValue(JsonValue, UMGPropertyKeys::EffectOffset);
		}
		if (DesignJsonUtils::TryGetString(*EffectObject, UMGPropertyKeys::GeometryMaskShape, Str))
		{
			if (Str.Equals(TEXT("Ellipse"), ESearchCase::IgnoreCase))
			{
				OutEffect.GeometryMaskShape = EUMGGeometryMaskShape::Ellipse;
			}
			else if (Str.Equals(TEXT("RoundedRectangle"), ESearchCase::IgnoreCase))
			{
				OutEffect.GeometryMaskShape = EUMGGeometryMaskShape::RoundedRectangle;
			}
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*EffectObject, UMGPropertyKeys::GeometryMaskSize); JsonValue.IsValid())
		{
			OutEffect.GeometryMaskSize = ParseVector2DValue(JsonValue, UMGPropertyKeys::GeometryMaskSize);
		}
		if (const TSharedPtr<FJsonValue> RadiiValue = DesignJsonUtils::TryGetField(*EffectObject, UMGPropertyKeys::GeometryMaskCornerRadii); RadiiValue.IsValid())
		{
			FVector4 Radii;
			if (RadiiValue->Type == EJson::String && TryParseVector4(RadiiValue->AsString(), Radii))
			{
				OutEffect.GeometryMaskCornerRadii = Radii;
			}
			else
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid Effect.GeometryMaskCornerRadii; ignoring value."));
			}
		}
		FUMGEffectData::FRenderingPhaseData RenderingPhase = OutEffect.RenderingPhase.Get(FUMGEffectData::FRenderingPhaseData());
		bool bRenderingPhaseSet = false;
		if (DesignJsonUtils::TryGetNumber(*EffectObject, UMGPropertyKeys::Phase, Val))
		{
			RenderingPhase.Phase = (int32)Val;
			bRenderingPhaseSet = true;
		}
		if (DesignJsonUtils::TryGetNumber(*EffectObject, UMGPropertyKeys::PhaseCount, Val))
		{
			RenderingPhase.PhaseCount = FMath::Max(1, (int32)Val);
			bRenderingPhaseSet = true;
		}
		if (bRenderingPhaseSet)
		{
			OutEffect.RenderingPhase = RenderingPhase;
		}
	}
}

void FUMGJsonParser::ParsePathPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGPathData& OutPath)
{
	// Parse PathData
	const TSharedPtr<FJsonObject>* PathDataObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("PathData"), PathDataObject))
	{
		float Val = 0.0f;
		FString Str;

		if (DesignJsonUtils::TryGetString(*PathDataObject, UMGPropertyKeys::PathGeometry, Str))
		{
			OutPath.Geometry = Str;
		}
		if (DesignJsonUtils::TryGetString(*PathDataObject, UMGPropertyKeys::PathFillRule, Str))
		{
			OutPath.FillRule = Str;
		}
		if (DesignJsonUtils::TryGetString(*PathDataObject, UMGPropertyKeys::PathFillColor, Str))
		{
			OutPath.FillColor = FUMGValueParser::ParseColor(Str);
		}
		if (DesignJsonUtils::TryGetString(*PathDataObject, UMGPropertyKeys::PathStrokeColor, Str))
		{
			OutPath.StrokeColor = FUMGValueParser::ParseColor(Str);
		}
		if (DesignJsonUtils::TryGetFloat(*PathDataObject, UMGPropertyKeys::PathStrokeThickness, Val))
		{
			OutPath.StrokeThickness = Val;
		}
		if (DesignJsonUtils::TryGetString(*PathDataObject, UMGPropertyKeys::PathStrokeCap, Str))
		{
			OutPath.StrokeCap = Str;
		}
		if (DesignJsonUtils::TryGetString(*PathDataObject, UMGPropertyKeys::PathStrokeJoin, Str))
		{
			OutPath.StrokeJoin = Str;
		}
	}
}

void FUMGJsonParser::ParseShadowPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGShadowData& OutShadow)
{
	// Parse ShadowData
	const TSharedPtr<FJsonObject>* ShadowDataObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("ShadowData"), ShadowDataObject))
	{
		float Val = 0.0f;
		FString Str;

		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*ShadowDataObject, UMGPropertyKeys::ShadowOffset); JsonValue.IsValid())
		{
			OutShadow.ShadowOffset = ParseVector2DValue(JsonValue, UMGPropertyKeys::ShadowOffset);
		}
		if (DesignJsonUtils::TryGetString(*ShadowDataObject, UMGPropertyKeys::ShadowColorAndOpacity, Str))
		{
			OutShadow.ShadowColorAndOpacity = FUMGValueParser::ParseColor(Str);
		}
		if (DesignJsonUtils::TryGetFloat(*ShadowDataObject, UMGPropertyKeys::ShadowBlur, Val))
		{
			OutShadow.ShadowBlur = Val;
		}
		if (const TArray<TSharedPtr<FJsonValue>>* BoxShadowArray = DesignJsonUtils::GetArray(*ShadowDataObject, UMGJsonKeys::BoxShadows))
		{
			for (const TSharedPtr<FJsonValue>& LayerValue : *BoxShadowArray)
			{
				const TSharedPtr<FJsonObject>* LayerObject = nullptr;
				if (!LayerValue->TryGetObject(LayerObject) || !LayerObject || !(*LayerObject).IsValid())
				{
					continue;
				}

				FUMGBoxShadowLayerData Layer;
				if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*LayerObject, UMGJsonKeys::Offset); JsonValue.IsValid())
				{
					Layer.Offset = ParseVector2DValue(JsonValue, UMGJsonKeys::Offset).Get(Layer.Offset);
				}
				if (DesignJsonUtils::TryGetFloat(*LayerObject, UMGJsonKeys::Blur, Val))
				{
					Layer.Blur = Val;
				}
				if (DesignJsonUtils::TryGetFloat(*LayerObject, UMGJsonKeys::Spread, Val))
				{
					Layer.Spread = Val;
				}
				if (DesignJsonUtils::TryGetString(*LayerObject, UMGPropertyKeys::Color, Str))
				{
					Layer.Color = FUMGValueParser::ParseColor(Str);
				}
				OutShadow.BoxShadows.Add(Layer);
			}
		}
	}
}

void FUMGJsonParser::ParseRenderPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGRenderStyleData& OutRender)
{
	// Parse Render (v2.7)
	const TSharedPtr<FJsonObject>* RenderObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("Render"), RenderObject))
	{
		float Val;
		FString Str;
		if (DesignJsonUtils::TryGetString(*RenderObject, UMGPropertyKeys::Clipping, Str))
		{
			OutRender.Clipping = Str;
		}
		if (DesignJsonUtils::TryGetFloat(*RenderObject, UMGPropertyKeys::RenderOpacity, Val))
		{
			OutRender.RenderOpacity = Val;
		}
		double Angle;
		if (TryGetFiniteFloatField(*RenderObject, UMGPropertyKeys::RenderAngle, Angle))
		{
			OutRender.RenderAngle = static_cast<float>(Angle);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*RenderObject, UMGPropertyKeys::RenderTransformPivot); JsonValue.IsValid())
		{
			OutRender.RenderTransformPivot = ParseVector2DValue(JsonValue, UMGPropertyKeys::RenderTransformPivot);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*RenderObject, UMGPropertyKeys::RenderTransformScale); JsonValue.IsValid())
		{
			OutRender.RenderTransformScale = ParseVector2DValue(JsonValue, UMGPropertyKeys::RenderTransformScale);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*RenderObject, UMGPropertyKeys::RenderTranslation); JsonValue.IsValid())
		{
			OutRender.RenderTranslation = ParseVector2DValue(JsonValue, UMGPropertyKeys::RenderTranslation);
		}
	}
}

void FUMGJsonParser::ParseWidgetStatePayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGWidgetStateData& OutWidgetState)
{
	// Parse WidgetState (v2.6)
	const TSharedPtr<FJsonObject>* WidgetStateObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("WidgetState"), WidgetStateObject))
	{
		// ActiveWidgetIndex keeps the double read: the historical static_cast<int32>
		// truncates, which differs from the engine int32 field overload's
		// range-check + round-half-from-zero conversion.
		double Val;
		if (DesignJsonUtils::TryGetNumber(*WidgetStateObject, UMGPropertyKeys::ActiveWidgetIndex, Val))
		{
			OutWidgetState.ActiveWidgetIndex = static_cast<int32>(Val);
		}
	}
}

void FUMGJsonParser::ParseScrollBoxPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGScrollBoxData& OutScrollBox)
{
	// Parse ScrollBox (v2.6)
	const TSharedPtr<FJsonObject>* ScrollBoxObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("ScrollBox"), ScrollBoxObject))
	{
		FString Str;
		if (DesignJsonUtils::TryGetString(*ScrollBoxObject, UMGPropertyKeys::Orientation, Str))
		{
			OutScrollBox.Orientation = Str;
		}
		if (DesignJsonUtils::TryGetString(*ScrollBoxObject, UMGPropertyKeys::ScrollBarVisibility, Str))
		{
			OutScrollBox.ScrollBarVisibility = Str;
		}
	}
}

void FUMGJsonParser::ParseSlotData(const TSharedPtr<FJsonObject>& JsonObject, FUMGSlotData& OutSlot)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	// Parse Anchors (CanvasPanel).
	const TSharedPtr<FJsonObject>* AnchorsObject;
	if (DesignJsonUtils::TryGetObject(JsonObject, TEXT("Anchors"), AnchorsObject))
	{
		ParseAnchorsData(*AnchorsObject, OutSlot.Anchors);
	}

	// Parse Alignment in object form.
	if (const TSharedPtr<FJsonValue> AlignValue = DesignJsonUtils::TryGetField(JsonObject, TEXT("Alignment")); AlignValue.IsValid() && AlignValue->Type == EJson::Object)
	{
		ParseAnchorData(AlignValue->AsObject(), OutSlot.Alignment);
	}

	// Parse Offsets (CanvasPanel).
	const TSharedPtr<FJsonObject>* OffsetsObject;
	if (DesignJsonUtils::TryGetObject(JsonObject, TEXT("Offsets"), OffsetsObject))
	{
		ParseOffsetsData(*OffsetsObject, OutSlot.Offsets);
	}

	// Parse Padding.
	if (const TSharedPtr<FJsonValue> PaddingValue = DesignJsonUtils::TryGetField(JsonObject, UMGPropertyKeys::Padding); PaddingValue.IsValid())
	{
		const TOptional<FMargin> Padding = ParsePaddingValue(PaddingValue, TEXT("Slot.Padding"));
		if (Padding.IsSet())
		{
			OutSlot.Padding = Padding.GetValue();
		}
	}

	// Parse HorizontalAlignment (VerticalBox/HorizontalBox/Overlay).
	FString HAlign;
	if (DesignJsonUtils::TryGetString(JsonObject, TEXT("HorizontalAlignment"), HAlign))
	{
		OutSlot.HorizontalAlignment = FUMGValueParser::ParseSlotHorizontalAlignment(HAlign);
	}

	// Parse VerticalAlignment.
	FString VAlign;
	if (DesignJsonUtils::TryGetString(JsonObject, TEXT("VerticalAlignment"), VAlign))
	{
		OutSlot.VerticalAlignment = FUMGValueParser::ParseSlotVerticalAlignment(VAlign);
	}

	// Parse SizeRule (Auto/Fill).
	FString SizeRule;
	if (DesignJsonUtils::TryGetString(JsonObject, TEXT("SizeRule"), SizeRule))
	{
		OutSlot.SizeRule = FUMGValueParser::ParseSlotSizeRule(SizeRule);
	}

	// Parse FillWeight.
	float FillWeight;
	if (DesignJsonUtils::TryGetFloat(JsonObject, TEXT("FillWeight"), FillWeight))
	{
		OutSlot.FillWeight = FillWeight;
	}

	// Parse Grid-related fields.
	int32 IntValue;
	if (DesignJsonUtils::TryGetInt32(JsonObject, TEXT("Row"), IntValue)) OutSlot.Row = IntValue;
	if (DesignJsonUtils::TryGetInt32(JsonObject, TEXT("Column"), IntValue)) OutSlot.Column = IntValue;
	if (DesignJsonUtils::TryGetInt32(JsonObject, TEXT("RowSpan"), IntValue)) OutSlot.RowSpan = IntValue;
	if (DesignJsonUtils::TryGetInt32(JsonObject, TEXT("ColumnSpan"), IntValue)) OutSlot.ColumnSpan = IntValue;

	// Parse bAutoSize (Size To Content).
	bool bAutoSize = false;
	if (DesignJsonUtils::TryGetBool(JsonObject, TEXT("bAutoSize"), bAutoSize))
	{
		OutSlot.bAutoSize = bAutoSize;
	}
}

void FUMGJsonParser::ParseAnchorsData(const TSharedPtr<FJsonObject>& JsonObject, FUMGAnchorsData& OutAnchors)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	// Parse Min in object form.
	if (const TSharedPtr<FJsonValue> Value = DesignJsonUtils::TryGetField(JsonObject, TEXT("Min")); Value.IsValid() && Value->Type == EJson::Object)
	{
		ParseAnchorData(Value->AsObject(), OutAnchors.Min);
	}

	// Parse Max in object form.
	if (const TSharedPtr<FJsonValue> Value = DesignJsonUtils::TryGetField(JsonObject, TEXT("Max")); Value.IsValid() && Value->Type == EJson::Object)
	{
		ParseAnchorData(Value->AsObject(), OutAnchors.Max);
	}
}

void FUMGJsonParser::ParseAnchorData(const TSharedPtr<FJsonObject>& JsonObject, FUMGAnchorData& OutAnchor)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	DesignJsonUtils::TryGetFloat(JsonObject, TEXT("X"), OutAnchor.X);
	DesignJsonUtils::TryGetFloat(JsonObject, TEXT("Y"), OutAnchor.Y);
}

void FUMGJsonParser::ParseOffsetsData(const TSharedPtr<FJsonObject>& JsonObject, FUMGOffsetsData& OutOffsets)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	DesignJsonUtils::TryGetFloat(JsonObject, TEXT("Left"), OutOffsets.Left);
	DesignJsonUtils::TryGetFloat(JsonObject, TEXT("Top"), OutOffsets.Top);
	DesignJsonUtils::TryGetFloat(JsonObject, TEXT("Right"), OutOffsets.Right);
	DesignJsonUtils::TryGetFloat(JsonObject, TEXT("Bottom"), OutOffsets.Bottom);
}
