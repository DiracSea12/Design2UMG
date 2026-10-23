// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGJsonParser.h"
#include "DesignBrowserWidget.h"
#include "Design2UMGLog.h"
#include "DesignJsonUtils.h"
#include "DesignTreeBudget.h"
#include "UMGJsonKeys.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetTypes.h"
#include "UMGValueParsing.h"
#include "Dom/JsonObject.h"
#include "Misc/Guid.h"

// Limits for this parser's tree budget. See DesignTreeBudget.h for the shared
// semantics and the parsed-design-tree vs generated-UMG-tree scope note.
using namespace DesignTreeLimits;

// ============================================================================
// Helper functions.
// ============================================================================

namespace
{
	bool TryGetStrictStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& OutValue)
	{
		const TSharedPtr<FJsonValue> Value = DesignJsonUtils::TryGetField(Object, Field);
		if (!Value.IsValid() || Value->Type != EJson::String)
		{
			return false;
		}
		OutValue = Value->AsString();
		return true;
	}

	bool TryGetStrictBoolField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool& OutValue)
	{
		const TSharedPtr<FJsonValue> Value = DesignJsonUtils::TryGetField(Object, Field);
		if (!Value.IsValid() || Value->Type != EJson::Boolean)
		{
			return false;
		}
		OutValue = Value->AsBool();
		return true;
	}

	bool TryGetStrictInt32Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32& OutValue)
	{
		const TSharedPtr<FJsonValue> Value = DesignJsonUtils::TryGetField(Object, Field);
		return Value.IsValid() && Value->Type == EJson::Number && Value->TryGetNumber(OutValue);
	}

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
		return Value.Type == EJson::Number && Value.TryGetNumber(OutValue)
			&& FMath::IsFinite(OutValue) && FMath::Abs(OutValue) <= MAX_flt;
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

	// Finite-checked float read that narrows to float ONLY after the value is
	// confirmed finite and within float range. Used for coordinate/geometry
	// fields so a value such as 1e100 (finite as double but past MAX_flt) is
	// rejected instead of silently becoming Inf/garbage. Negative coordinates are
	// explicitly allowed -- only finiteness/range is enforced here.
	bool TryGetFiniteFloatFieldF(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, float& OutValue)
	{
		double D;
		if (TryGetFiniteFloatField(Object, Field, D))
		{
			OutValue = static_cast<float>(D);
			return true;
		}
		return false;
	}

	// Read a finite number as int32 with truncation toward zero.
	// Out-of-range optional fields remain unset and emit a warning.
	bool TryGetFiniteInt32Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32& OutValue)
	{
		const TSharedPtr<FJsonValue> Value = DesignJsonUtils::TryGetField(Object, Field);
		if (!Value.IsValid() || Value->Type != EJson::Number)
		{
			return false;
		}
		double D;
		if (Value->TryGetNumber(D) && FMath::IsFinite(D))
		{
			// Truncation is representable inside (MIN_int32 - 1, MAX_int32 + 1).
			// Check that interval before casting, including fractional values near either boundary.
			if (D > static_cast<double>(MIN_int32) - 1.0 && D < static_cast<double>(MAX_int32) + 1.0)
			{
				OutValue = static_cast<int32>(D);
				return true;
			}
		}
		UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid %s; expected a finite int32-range number."), Field);
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
			double Number = 0.0;
			if (FieldValue->Type == EJson::Number && TryReadFiniteFloat(*FieldValue, Number)) Out = static_cast<float>(Number);
			else UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: Invalid Brush.Shape.%s; expected a finite float-range number."), Field);
		};
		const auto ReadInt = [&](const TCHAR* Field, TOptional<int32>& Out)
		{
			const auto FieldValue = DesignJsonUtils::TryGetField(*Object, Field);
			if (!FieldValue) return;
			int32 Number = 0;
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
				double Number = 0.0;
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
	// Reuse-output cleanliness: always start from a clean slate so a previous
	// call's data or error never leaks into this result (success OR failure).
	OutWidgetData = FUMGWidgetData();
	if (OutError) *OutError = TEXT("");

	TSharedPtr<FJsonObject> Root;
	if (!DesignJsonUtils::TryParseObject(JsonString, Root))
	{
		if (OutError) *OutError = TEXT("Invalid JSON");
		return false;
	}

	// Parse into a LOCAL candidate and publish to the caller only if the ENTIRE
	// tree parses. Any structural failure discards the candidate, so a failed call
	// never leaks a partial tree into the output (all-or-nothing).
	FUMGWidgetData Candidate;
	FDesignParserBudget Budget;
	const TArray<TSharedPtr<FJsonValue>>* ExportBounds = nullptr;
	if (Root->TryGetArrayField(TEXT("ExportBounds"), ExportBounds))
	{
		if (ExportBounds->Num() != 4)
		{
			if (OutError) *OutError = TEXT("ExportBounds requires four coordinates");
			return false;
		}
		double Values[4];
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (!(*ExportBounds)[Index]->TryGetNumber(Values[Index]))
			{
				if (OutError) *OutError = TEXT("ExportBounds requires numeric coordinates");
				return false;
			}
		}
		if (Values[2] <= Values[0] || Values[3] <= Values[1])
		{
			if (OutError) *OutError = TEXT("ExportBounds requires a positive extent");
			return false;
		}
		Candidate.ExportBounds = FBox2D(FVector2D(Values[0], Values[1]), FVector2D(Values[2], Values[3]));
	}

	const TSharedPtr<FJsonObject>* RootWidget;
	if (!DesignJsonUtils::TryGetObject(Root, TEXT("RootWidget"), RootWidget))
	{
		if (OutError) *OutError = TEXT("Invalid UI structure: missing RootWidget object");
		return false;
	}

	if (!ParseWidgetNode(*RootWidget, Candidate.RootWidget, TEXT("RootWidget"), OutError, Budget, 0))
	{
		// Candidate is discarded; OutWidgetData stays cleared (empty output).
		return false;
	}

	// Treat an empty pure-layout container as an invalid result.
	// Interactive widgets such as CheckBox can also derive from panel types, so keep this allowlist narrow.
	if (Candidate.RootWidget.Children.Num() == 0 &&
		UMGWidgetTypes::IsPureLayoutContainer(Candidate.RootWidget.Type))
	{
		if (OutError) *OutError = TEXT("Empty UI result");
		return false;
	}

	TryGetStrictStringField(Root, TEXT("WidgetName"), Candidate.WidgetName);
	if (Candidate.WidgetName.IsEmpty()) Candidate.WidgetName = TEXT("WBP_Generated");

	// Full success: publish the fully-parsed candidate.
	OutWidgetData = MoveTemp(Candidate);
	return true;
}

bool FUMGJsonParser::ParseWidgetNode(const TSharedPtr<FJsonObject>& JsonObject, FUMGWidgetNode& OutNode, const FString& NodePath, FString* OutError, FDesignParserBudget& Budget, int32 Depth)
{
	if (!JsonObject.IsValid())
	{
		if (OutError) *OutError = FString::Printf(TEXT("Widget node at '%s' is not a valid object."), *NodePath);
		return false;
	}

	// Whole-tree budget: one shared counting context across the entire document
	// tree (both depth and node count). Bounding here -- not per-parent/sibling --
	// so a wide-but-shallow tree is limited just like a deep one. A budget breach
	// fails the WHOLE parse; it must never yield a truncated "success" tree.
	if (!Budget.TryEnterNode(Depth))
	{
		if (OutError) *OutError = FString::Printf(
			TEXT("Design tree exceeds the supported budget near node '%s' (limit: %d nodes / %d depth)."),
			*NodePath, MaxParsedNodeCount, MaxNodeDepth);
		UE_LOG(LogDesign2UMG, Error, TEXT("FUMGJsonParser: Design tree budget exceeded near node '%s'."), *NodePath);
		return false;
	}

	// Parse required fields without crashing on missing keys.
	if (!TryGetStrictStringField(JsonObject, UMGJsonKeys::Type, OutNode.Type) || OutNode.Type.IsEmpty())
	{
		if (OutError) *OutError = FString::Printf(TEXT("Widget node at '%s' is missing required Type field."), *NodePath);
		UE_LOG(LogDesign2UMG, Error, TEXT("FUMGJsonParser: Widget node at '%s' missing Type field"), *NodePath);
		return false;
	}

	// Name is optional; synthesize one when absent.
	if (!TryGetStrictStringField(JsonObject, TEXT("Name"), OutNode.Name) || OutNode.Name.IsEmpty())
	{
		OutNode.Name = FString::Printf(TEXT("%s_%s"), *OutNode.Type, *FGuid::NewGuid().ToString(EGuidFormats::Short));
	}

	// Parse v2.2 path-based stable id.
	TryGetStrictStringField(JsonObject, TEXT("Id"), OutNode.Id);

	// Parse the LayoutMapper justify-spacer marker (absent in older JSON means false).
	TryGetStrictBoolField(JsonObject, TEXT("SyntheticJustifySpacer"), OutNode.bSyntheticJustifySpacer);

	// Parse Bounds, added in v2.2 for normalized coordinates. Coordinates are
	// allowed to be negative; only finiteness/float-range is enforced here so a
	// large-exponent value (e.g. 1e100) is rejected rather than narrowed to Inf.
	const TSharedPtr<FJsonObject>* BoundsObject;
	if (DesignJsonUtils::TryGetObject(JsonObject, TEXT("Bounds"), BoundsObject))
	{
		float Val;
		if (TryGetFiniteFloatFieldF(*BoundsObject, TEXT("X"), Val)) OutNode.Bounds.X = Val;
		if (TryGetFiniteFloatFieldF(*BoundsObject, TEXT("Y"), Val)) OutNode.Bounds.Y = Val;
		if (TryGetFiniteFloatFieldF(*BoundsObject, TEXT("W"), Val)) OutNode.Bounds.W = Val;
		if (TryGetFiniteFloatFieldF(*BoundsObject, TEXT("H"), Val)) OutNode.Bounds.H = Val;
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
	DesignBrowserWidget::Parse(JsonObject, OutNode.Browser);

	// Parse Children. A present-but-non-array Children field, a non-object child,
	// or any failing child must fail the WHOLE parse (all-or-nothing): the failed
	// input must never enter resource creation / save.
	TSharedPtr<FJsonValue> ChildrenField = DesignJsonUtils::TryGetField(JsonObject, TEXT("Children"));
	if (ChildrenField.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = nullptr;
		if (!ChildrenField->TryGetArray(ChildrenArray) || ChildrenArray == nullptr)
		{
			if (OutError) *OutError = FString::Printf(TEXT("Field 'Children' at '%s' must be an array of objects."), *NodePath);
			UE_LOG(LogDesign2UMG, Error, TEXT("FUMGJsonParser: Field 'Children' at '%s' must be an array of objects."), *NodePath);
			return false;
		}
		for (int32 Index = 0; Index < ChildrenArray->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& ChildValue = (*ChildrenArray)[Index];
			const TSharedPtr<FJsonObject>* ChildObject = nullptr;
			if (!ChildValue->TryGetObject(ChildObject) || !ChildObject || !ChildObject->IsValid())
			{
				if (OutError) *OutError = FString::Printf(TEXT("Child at '%s.Children[%d]' is not a valid object."), *NodePath, Index);
				UE_LOG(LogDesign2UMG, Error, TEXT("FUMGJsonParser: Child at '%s.Children[%d]' is not a valid object."), *NodePath, Index);
				return false;
			}
			FUMGWidgetNode ChildNode;
			const FString ChildPath = FString::Printf(TEXT("%s.Children[%d]"), *NodePath, Index);
			if (!ParseWidgetNode(*ChildObject, ChildNode, ChildPath, OutError, Budget, Depth + 1))
			{
				// The failing child already recorded its own specific error; abort
				// the whole parse so no partial branch leaks into the output.
				return false;
			}
			OutNode.Children.Add(MoveTemp(ChildNode));
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
		if (TryGetFiniteFloatFieldF(*TSObject, UMGPropertyKeys::FontSize, Val))
			OutTextStyle.FontSize = Val;
		if (TryGetFiniteFloatFieldF(*TSObject, UMGJsonKeys::SourceFontSize, Val))
			OutTextStyle.SourceFontSize = Val;
		if (TryGetFiniteFloatFieldF(*TSObject, UMGPropertyKeys::LetterSpacing, Val))
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
		if (TryGetFiniteFloatFieldF(*WidgetStyleObject, UMGPropertyKeys::WidthOverride, Val))
		{
			OutWidgetStyle.WidthOverride = Val;
		}
		if (TryGetFiniteFloatFieldF(*WidgetStyleObject, UMGPropertyKeys::HeightOverride, Val))
		{
			OutWidgetStyle.HeightOverride = Val;
		}
		if (TryGetFiniteFloatFieldF(*WidgetStyleObject, UMGPropertyKeys::MinDesiredWidth, Val))
		{
			OutWidgetStyle.MinDesiredWidth = Val;
		}
		if (TryGetFiniteFloatFieldF(*WidgetStyleObject, UMGPropertyKeys::MinDesiredHeight, Val))
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
		if (TryGetStrictStringField(*WidgetStyleObject, UMGPropertyKeys::Stretch, Str))
		{
			OutWidgetStyle.Stretch = Str;
		}
		if (TryGetStrictStringField(*WidgetStyleObject, UMGPropertyKeys::StretchDirection, Str))
		{
			OutWidgetStyle.StretchDirection = Str;
		}
		if (TryGetStrictStringField(*WidgetStyleObject, UMGPropertyKeys::BackgroundColor, Str))
		{
			OutWidgetStyle.BackgroundColor = FUMGValueParser::ParseColor(Str);
		}
		if (TryGetStrictStringField(*WidgetStyleObject, UMGPropertyKeys::HoveredColor, Str))
		{
			OutWidgetStyle.HoveredColor = FUMGValueParser::ParseColor(Str);
		}
		if (TryGetStrictStringField(*WidgetStyleObject, UMGPropertyKeys::PressedColor, Str))
		{
			OutWidgetStyle.PressedColor = FUMGValueParser::ParseColor(Str);
		}
		if (TryGetStrictStringField(*WidgetStyleObject, UMGPropertyKeys::NamedSlot, Str))
		{
			OutWidgetStyle.NamedSlot = Str;
		}
		if (TryGetStrictStringField(*WidgetStyleObject, UMGPropertyKeys::SlotName, Str))
		{
			OutWidgetStyle.SlotName = Str;
		}
		if (TryGetStrictStringField(*WidgetStyleObject, UMGPropertyKeys::Visibility, Str))
		{
			OutWidgetStyle.Visibility = Str;
		}
		if (TryGetStrictStringField(*WidgetStyleObject, UMGPropertyKeys::ToolTipText, Str))
		{
			OutWidgetStyle.ToolTipText = Str;
		}
		bool BoolValue = false;
		if (TryGetStrictBoolField(*WidgetStyleObject, UMGPropertyKeys::IsEnabled, BoolValue))
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

		if (TryGetStrictStringField(*TextDataObject, UMGPropertyKeys::Text, Str))
		{
			OutText.Text = Str;
		}
		if (TryGetStrictStringField(*TextDataObject, UMGPropertyKeys::RichTextStyleRows, Str))
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
					TryGetStrictStringField(*RowObject, UMGPropertyKeys::FontFamily, Row.FontFamily);
					TryGetStrictStringField(*RowObject, UMGPropertyKeys::TypefaceFontName, Row.TypefaceFontName);
					if (TryGetFiniteFloatFieldF(*RowObject, UMGPropertyKeys::FontSize, Number)) Row.FontSize = Number;
					if (TryGetFiniteFloatFieldF(*RowObject, UMGPropertyKeys::LetterSpacing, Number)) Row.LetterSpacing = Number;
					if (TryGetStrictStringField(*RowObject, UMGPropertyKeys::Color, Color) && !Color.IsEmpty()) Row.Color = FUMGValueParser::ParseColor(Color);
					TryGetStrictBoolField(*RowObject, UMGPropertyKeys::Underline, Row.bUnderline);
					TryGetStrictBoolField(*RowObject, UMGPropertyKeys::Strikethrough, Row.bStrikethrough);
					TryGetStrictStringField(*RowObject, UMGPropertyKeys::Href, Row.Href);
					Rows.Add(FString(Pair.Key), MoveTemp(Row));
				}
				OutText.RichTextStyleRows = MoveTemp(Rows);
			}
		}
		if (TryGetStrictStringField(*TextDataObject, UMGPropertyKeys::HintText, Str))
		{
			OutText.HintText = Str;
		}
		if (TryGetStrictStringField(*TextDataObject, UMGPropertyKeys::FontFamily, Str))
		{
			OutText.FontFamily = Str;
		}
		if (TryGetStrictStringField(*TextDataObject, UMGPropertyKeys::TypefaceFontName, Str))
		{
			OutText.TypefaceFontName = Str;
		}
		if (TryGetStrictStringField(*TextDataObject, UMGPropertyKeys::FontStyle, Str))
		{
			OutText.FontStyle = Str;
		}
		if (TryGetStrictStringField(*TextDataObject, UMGPropertyKeys::Justification, Str))
		{
			OutText.Justification = Str;
		}
		if (TryGetStrictBoolField(*TextDataObject, UMGPropertyKeys::AutoWrapText, BoolValue))
		{
			OutText.bAutoWrapText = BoolValue;
		}
		if (TryGetFiniteFloatFieldF(*TextDataObject, UMGPropertyKeys::WrapTextAt, Val))
		{
			OutText.WrapTextAt = Val;
		}
		if (TryGetFiniteFloatFieldF(*TextDataObject, UMGPropertyKeys::LineHeightPercentage, Val))
		{
			OutText.LineHeightPercentage = Val;
		}
		if (TryGetFiniteFloatFieldF(*TextDataObject, UMGPropertyKeys::LineHeightBaselineShift, Val))
		{
			OutText.LineHeightBaselineShift = Val;
		}
		if (TryGetStrictBoolField(*TextDataObject, UMGPropertyKeys::Underline, BoolValue))
		{
			OutText.bUnderline = BoolValue;
		}
		if (TryGetStrictBoolField(*TextDataObject, UMGPropertyKeys::Strikethrough, BoolValue))
		{
			OutText.bStrikethrough = BoolValue;
		}
		if (TryGetStrictStringField(*TextDataObject, UMGPropertyKeys::Href, Str))
		{
			OutText.Href = Str;
		}
		if (TryGetStrictStringField(*TextDataObject, UMGPropertyKeys::TextAlignVertical, Str))
		{
			OutText.TextAlignVertical = Str;
		}
		if (TryGetStrictStringField(*TextDataObject, UMGPropertyKeys::IconName, Str))
		{
			OutText.IconName = Str;
		}
		if (TryGetStrictStringField(*TextDataObject, UMGPropertyKeys::Color, Str))
		{
			OutText.Color = FUMGValueParser::ParseColor(Str);
		}
	}
}

void FUMGJsonParser::ParseGradientPayload(const TSharedPtr<FJsonObject>& GradientObject, FUMGVisualBrushData::FGradientData& Gradient)
{
	float FloatVal = 0.0f;
	FString Str;
	if (TryGetStrictStringField(GradientObject, UMGJsonKeys::Type, Str))
	{
		Gradient.Type = Str;
	}
	if (TryGetFiniteFloatFieldF(GradientObject, UMGJsonKeys::Rotation, FloatVal))
	{
		Gradient.Rotation = FloatVal;
	}
	if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(GradientObject, UMGJsonKeys::StartPoint); JsonValue.IsValid())
	{
		Gradient.StartPoint = ParseVector2DValue(JsonValue, UMGJsonKeys::StartPoint);
	}
	if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(GradientObject, UMGJsonKeys::EndPoint); JsonValue.IsValid())
	{
		Gradient.EndPoint = ParseVector2DValue(JsonValue, UMGJsonKeys::EndPoint);
	}
	if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(GradientObject, UMGJsonKeys::Center); JsonValue.IsValid())
	{
		Gradient.Center = ParseVector2DValue(JsonValue, UMGJsonKeys::Center);
	}
	if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(GradientObject, UMGJsonKeys::Size); JsonValue.IsValid())
	{
		Gradient.Size = ParseVector2DValue(JsonValue, UMGJsonKeys::Size);
	}
	const TSharedPtr<FJsonObject>* MeshObject = nullptr;
	if (GradientObject->TryGetObjectField(TEXT("Mesh"), MeshObject))
	{
		FDesignMeshGradient Mesh;
		if (DesignMeshGradientJson::Parse(*MeshObject, Mesh)) Gradient.Mesh = MoveTemp(Mesh);
	}
	if (const TArray<TSharedPtr<FJsonValue>>* StopArray = DesignJsonUtils::GetArray(GradientObject, UMGJsonKeys::Stops))
	{
		for (const TSharedPtr<FJsonValue>& StopValue : *StopArray)
		{
			const TSharedPtr<FJsonObject>* StopObject = nullptr;
			if (!StopValue->TryGetObject(StopObject) || !StopObject || !(*StopObject).IsValid())
			{
				continue;
			}

			FUMGVisualBrushData::FGradientStopData& Stop = Gradient.Stops.AddDefaulted_GetRef();
			if (TryGetStrictStringField(*StopObject, UMGPropertyKeys::Color, Str))
			{
				Stop.Color = FUMGValueParser::ParseColor(Str);
			}
			if (TryGetFiniteFloatFieldF(*StopObject, UMGJsonKeys::Position, FloatVal))
			{
				Stop.Position = FloatVal;
			}
		}
	}
}

void FUMGJsonParser::ParseBrushPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGVisualBrushData& OutBrush)
{
	// Parse Brush (v2.4).
	const TSharedPtr<FJsonObject>* BrushObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("Brush"), BrushObject))
	{
        const TSharedPtr<FJsonObject>* ShaderObject=nullptr;
        if((*BrushObject)->TryGetObjectField(TEXT("Shader"),ShaderObject))
        {FDesignShaderData Shader;if(DesignShaderJson::Parse(*ShaderObject,Shader)) OutBrush.Shader=MoveTemp(Shader);}
		// Check for a negative unset value before narrowing OutlineCornerRadius to float.
		double Val;
		float FloatVal;
		FString Str;
		// Negative radius means unset.
		if (TryGetFiniteFloatField(*BrushObject, UMGPropertyKeys::OutlineCornerRadius, Val) && Val >= 0.0)
			OutBrush.OutlineCornerRadius = (float)Val;
		if (TryGetFiniteFloatFieldF(*BrushObject, UMGPropertyKeys::OutlineThickness, FloatVal))
		{
			OutBrush.OutlineThickness = FloatVal;
		}
		if (const TSharedPtr<FJsonValue> Margin = DesignJsonUtils::TryGetField(*BrushObject, UMGPropertyKeys::BrushMargin); Margin.IsValid())
		{
			OutBrush.Margin = ParsePaddingValue(Margin, UMGPropertyKeys::BrushMargin);
		}
		if (TryGetStrictStringField(*BrushObject, UMGPropertyKeys::DrawAs, Str))
		{
			ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType;
			if (Design2UMGWidgetNodeAccess::TryParseDrawAs(Str, DrawAs))
			{
				OutBrush.DrawAs = DrawAs;
			}
		}
		if (TryGetStrictStringField(*BrushObject, UMGPropertyKeys::BrushColor, Str))
		{
			OutBrush.BrushColor = FUMGValueParser::ParseColor(Str);
		}
		if (TryGetStrictStringField(*BrushObject, UMGPropertyKeys::OutlineColor, Str))
		{
			OutBrush.OutlineColor = FUMGValueParser::ParseColor(Str);
		}
		if (TryGetStrictStringField(*BrushObject, UMGPropertyKeys::MaterialRef, Str))
		{
			OutBrush.MaterialRef = Str;
		}
		if (TryGetStrictStringField(*BrushObject, UMGPropertyKeys::MaterialParams, Str))
		{
			OutBrush.MaterialParams = Str;
		}
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
			if (TryGetStrictStringField(*ImageObject, UMGPropertyKeys::TexturePath, Str))
			{
				OutBrush.Image.TexturePath = Str;
			}
			if (TryGetStrictStringField(*ImageObject, UMGJsonKeys::Mode, Str))
			{
				OutBrush.Image.Mode = Str;
			}
			if (TryGetStrictStringField(*ImageObject, UMGJsonKeys::CropFitMode, Str))
			{
				OutBrush.Image.CropFitMode = Str;
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
		const TSharedPtr<FJsonObject>* GradientObject = nullptr;
		if (DesignJsonUtils::TryGetObject(*BrushObject, TEXT("Gradient"), GradientObject))
		{
			ParseGradientPayload(*GradientObject, OutBrush.Gradient);
		}
	}
}

void FUMGJsonParser::ParseEffectPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGEffectData& OutEffect)
{
	// Parse Effect (v2.5)
	const TSharedPtr<FJsonObject>* EffectObject;
	if (DesignJsonUtils::TryGetObject(NodeObject, TEXT("Effect"), EffectObject))
	{
        const TSharedPtr<FJsonObject>* ShaderObject=nullptr;
        if((*EffectObject)->TryGetObjectField(TEXT("Shader"),ShaderObject))
        {FDesignShaderData Shader;if(DesignShaderJson::Parse(*ShaderObject,Shader)) OutEffect.Shader=MoveTemp(Shader);}
		// Range-check rendering phase values before truncating to int32; PhaseCount is at least one.
		float FloatVal;
		FString Str;
		bool BoolValue = false;
		if (TryGetFiniteFloatFieldF(*EffectObject, UMGPropertyKeys::BlurStrength, FloatVal))
		{
			OutEffect.BlurStrength = FloatVal;
		}
		if (TryGetStrictBoolField(*EffectObject, UMGPropertyKeys::RetainRendering, BoolValue))
		{
			OutEffect.bRetainRendering = BoolValue;
		}
		if (TryGetStrictStringField(*EffectObject, UMGPropertyKeys::EffectMaterial, Str))
		{
			OutEffect.EffectMaterial = Str;
		}
		if (TryGetStrictStringField(*EffectObject, UMGPropertyKeys::TextureParameter, Str))
		{
			OutEffect.TextureParameter = Str;
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*EffectObject, UMGPropertyKeys::EffectBlurUVStep); JsonValue.IsValid())
		{
			OutEffect.EffectBlurUVStep = ParseVector2DValue(JsonValue, UMGPropertyKeys::EffectBlurUVStep);
		}
		if (TryGetStrictStringField(*EffectObject, UMGPropertyKeys::EffectColor, Str))
		{
			OutEffect.EffectColor = FUMGValueParser::ParseColor(Str);
		}
		if (TryGetStrictStringField(*EffectObject, UMGJsonKeys::InnerShadowBlendMode, Str))
		{
			OutEffect.InnerShadowBlendMode = Str;
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*EffectObject, UMGPropertyKeys::EffectOffset); JsonValue.IsValid())
		{
			OutEffect.EffectOffset = ParseVector2DValue(JsonValue, UMGPropertyKeys::EffectOffset);
		}
		if (TryGetStrictStringField(*EffectObject, UMGPropertyKeys::GeometryMaskShape, Str))
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
		// Reject unrepresentable int32 values before casting; truncate valid values toward zero.
		int32 PhaseVal = 0;
		if (TryGetFiniteInt32Field(*EffectObject, UMGPropertyKeys::Phase, PhaseVal))
		{
			RenderingPhase.Phase = PhaseVal;
			bRenderingPhaseSet = true;
		}
		int32 PhaseCountVal = 0;
		if (TryGetFiniteInt32Field(*EffectObject, UMGPropertyKeys::PhaseCount, PhaseCountVal))
		{
			RenderingPhase.PhaseCount = FMath::Max(1, PhaseCountVal);
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

		if (TryGetStrictStringField(*PathDataObject, UMGPropertyKeys::PathGeometry, Str))
		{
			OutPath.Geometry = Str;
		}
		if (TryGetStrictStringField(*PathDataObject, UMGPropertyKeys::PathFillRule, Str))
		{
			OutPath.FillRule = Str;
		}
		if (TryGetStrictStringField(*PathDataObject, UMGPropertyKeys::PathFillColor, Str))
		{
			OutPath.FillColor = FUMGValueParser::ParseColor(Str);
		}
		if (TryGetStrictStringField(*PathDataObject, UMGPropertyKeys::PathStrokeColor, Str))
		{
			OutPath.StrokeColor = FUMGValueParser::ParseColor(Str);
		}
		if (TryGetFiniteFloatFieldF(*PathDataObject, UMGPropertyKeys::PathStrokeThickness, Val))
		{
			OutPath.StrokeThickness = Val;
		}
		if (TryGetStrictStringField(*PathDataObject, UMGPropertyKeys::PathStrokeCap, Str))
		{
			OutPath.StrokeCap = Str;
		}
		if (TryGetStrictStringField(*PathDataObject, UMGPropertyKeys::PathStrokeJoin, Str))
		{
			OutPath.StrokeJoin = Str;
		}
		if (const TSharedPtr<FJsonValue> Value = DesignJsonUtils::TryGetField(*PathDataObject, TEXT("CanvasSize")); Value.IsValid())
			OutPath.CanvasSize = ParseVector2DValue(Value, TEXT("CanvasSize"));
		if (TryGetStrictStringField(*PathDataObject, TEXT("StrokeAlignment"), Str)) OutPath.StrokeAlignment = Str;
		bool bFitGeometryToBounds;
		if ((*PathDataObject)->TryGetBoolField(TEXT("FitGeometryToBounds"), bFitGeometryToBounds)) OutPath.FitGeometryToBounds = bFitGeometryToBounds;
		FVector4 ViewBox;
		if (TryGetStrictStringField(*PathDataObject, TEXT("ViewBox"), Str) && TryParseVector4(Str, ViewBox) && ViewBox.Z > 0 && ViewBox.W > 0) OutPath.ViewBox = ViewBox;
		if (TryGetStrictStringField(*PathDataObject, TEXT("StrokeGeometry"), Str)) OutPath.StrokeGeometry = Str;
		const TSharedPtr<FJsonObject>* SourceObject = nullptr;
        if (DesignJsonUtils::TryGetObject(*PathDataObject, TEXT("SourcePaintGeometry"), SourceObject))
        {
            FUMGSourcePaintGeometry Source;
            const auto ReadPaths = [&](const TCHAR* Key, TOptional<TArray<FDesignGeometryPath>>& Paths)
            {
                const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
                if (!(*SourceObject)->TryGetArrayField(Key, Values)) return;
                Paths.Emplace();
                for (const auto& Value : *Values)
                {
                    const TSharedPtr<FJsonObject> Object = Value->AsObject();
                    FDesignGeometryPath Geometry;
                    if (TryGetStrictStringField(Object, TEXT("Path"), Geometry.Path)
                        && TryGetStrictStringField(Object, TEXT("FillRule"), Geometry.FillRule))
                        Paths->Add(MoveTemp(Geometry));
                }
            };
            ReadPaths(TEXT("FillPaths"), Source.Geometry.FillPaths);
            ReadPaths(TEXT("StrokePaths"), Source.Geometry.StrokePaths);
            if (TryGetStrictStringField(*SourceObject, TEXT("StrokeAlignment"), Str)) Source.StrokeAlignment = Str;
            const auto ReadPaint = [](const TSharedPtr<FJsonObject>& Object)
            {
                TSharedPtr<FJsonObject> Holder = MakeShared<FJsonObject>();
                Holder->SetObjectField(TEXT("Brush"), Object);
                FUMGVisualBrushData Brush;
                ParseBrushPayload(Holder, Brush);
                FUMGSurfacePaint Paint;
                Paint.Color = Brush.BrushColor.Get(FLinearColor::White);
                Paint.Gradient = MoveTemp(Brush.Gradient);
                Paint.Image = MoveTemp(Brush.Image);
                return Paint;
            };
            const TArray<TSharedPtr<FJsonValue>>* Paints = nullptr;
            if ((*SourceObject)->TryGetArrayField(TEXT("FillPaints"), Paints))
            {
                Source.FillPaints = TArray<FUMGSurfacePaint>();
                for (const auto& Paint : *Paints) Source.FillPaints->Add(ReadPaint(Paint->AsObject()));
            }
            const TSharedPtr<FJsonObject>* StrokePaint = nullptr;
            if ((*SourceObject)->TryGetObjectField(TEXT("StrokePaint"), StrokePaint)) Source.StrokePaint = ReadPaint(*StrokePaint);
            OutPath.SourcePaintGeometry = MoveTemp(Source);
        }
		const TSharedPtr<FJsonObject>* StrokeGradient = nullptr;
		if (DesignJsonUtils::TryGetObject(*PathDataObject, TEXT("StrokeGradient"), StrokeGradient)) ParseGradientPayload(*StrokeGradient, OutPath.StrokeGradient);

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
		if (TryGetStrictStringField(*ShadowDataObject, UMGPropertyKeys::ShadowColorAndOpacity, Str))
		{
			OutShadow.ShadowColorAndOpacity = FUMGValueParser::ParseColor(Str);
		}
		if (TryGetFiniteFloatFieldF(*ShadowDataObject, UMGPropertyKeys::ShadowBlur, Val))
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
				if (TryGetFiniteFloatFieldF(*LayerObject, UMGJsonKeys::Blur, Val))
				{
					Layer.Blur = Val;
				}
				if (TryGetFiniteFloatFieldF(*LayerObject, UMGJsonKeys::Spread, Val))
				{
					Layer.Spread = Val;
				}
				TryGetStrictBoolField(*LayerObject, UMGJsonKeys::ShowShadowBehindNode, Layer.bShowShadowBehindNode);
				TryGetStrictStringField(*LayerObject, UMGJsonKeys::BlendMode, Layer.BlendMode);
				if (TryGetStrictStringField(*LayerObject, UMGPropertyKeys::Color, Str))
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
		if (TryGetStrictStringField(*RenderObject, UMGJsonKeys::PaintBlendMode, Str)) OutRender.PaintBlendMode = Str;
		if (const TSharedPtr<FJsonValue> Value = DesignJsonUtils::TryGetField(*RenderObject, UMGJsonKeys::BackgroundCaptureInsets); Value.IsValid())
			OutRender.BackgroundCaptureInsets = ParsePaddingValue(Value, UMGJsonKeys::BackgroundCaptureInsets);
		if (TryGetStrictStringField(*RenderObject, UMGPropertyKeys::Clipping, Str))
		{
			OutRender.Clipping = Str;
		}
		if (TryGetFiniteFloatFieldF(*RenderObject, UMGPropertyKeys::RenderOpacity, Val))
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
		// Range-check ActiveWidgetIndex before casting; invalid values leave the field unset.
		int32 ActiveWidgetIndexVal = 0;
		if (TryGetFiniteInt32Field(*WidgetStateObject, UMGPropertyKeys::ActiveWidgetIndex, ActiveWidgetIndexVal))
		{
			OutWidgetState.ActiveWidgetIndex = ActiveWidgetIndexVal;
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
		if (TryGetStrictStringField(*ScrollBoxObject, UMGPropertyKeys::Orientation, Str))
		{
			OutScrollBox.Orientation = Str;
		}
		if (TryGetStrictStringField(*ScrollBoxObject, UMGPropertyKeys::ScrollBarVisibility, Str))
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
	if (TryGetStrictStringField(JsonObject, TEXT("HorizontalAlignment"), HAlign))
	{
		OutSlot.HorizontalAlignment = FUMGValueParser::ParseSlotHorizontalAlignment(HAlign);
	}

	// Parse VerticalAlignment.
	FString VAlign;
	if (TryGetStrictStringField(JsonObject, TEXT("VerticalAlignment"), VAlign))
	{
		OutSlot.VerticalAlignment = FUMGValueParser::ParseSlotVerticalAlignment(VAlign);
	}

	// Parse SizeRule (Auto/Fill).
	FString SizeRule;
	if (TryGetStrictStringField(JsonObject, TEXT("SizeRule"), SizeRule))
	{
		OutSlot.SizeRule = FUMGValueParser::ParseSlotSizeRule(SizeRule);
	}

	// Parse FillWeight.
	float FillWeight;
	if (TryGetFiniteFloatFieldF(JsonObject, TEXT("FillWeight"), FillWeight))
	{
		OutSlot.FillWeight = FillWeight;
	}

	// Parse Grid-related fields.
	int32 IntValue;
	if (TryGetStrictInt32Field(JsonObject, TEXT("Row"), IntValue)) OutSlot.Row = IntValue;
	if (TryGetStrictInt32Field(JsonObject, TEXT("Column"), IntValue)) OutSlot.Column = IntValue;
	if (TryGetStrictInt32Field(JsonObject, TEXT("RowSpan"), IntValue)) OutSlot.RowSpan = IntValue;
	if (TryGetStrictInt32Field(JsonObject, TEXT("ColumnSpan"), IntValue)) OutSlot.ColumnSpan = IntValue;

	// Parse bAutoSize (Size To Content).
	bool bAutoSize = false;
	if (TryGetStrictBoolField(JsonObject, TEXT("bAutoSize"), bAutoSize))
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

	// Anchor coordinates may legitimately be negative; only finiteness/float-range
	// is enforced before narrowing so a large-exponent value is rejected rather
	// than stored as Inf.
	float Val;
	if (TryGetFiniteFloatFieldF(JsonObject, TEXT("X"), Val)) OutAnchor.X = Val;
	if (TryGetFiniteFloatFieldF(JsonObject, TEXT("Y"), Val)) OutAnchor.Y = Val;
}

void FUMGJsonParser::ParseOffsetsData(const TSharedPtr<FJsonObject>& JsonObject, FUMGOffsetsData& OutOffsets)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	// Canvas offsets are coordinates and may legitimately be negative (e.g. a
	// negative Left/Top). Only finiteness/float-range is enforced before narrowing.
	float Val;
	if (TryGetFiniteFloatFieldF(JsonObject, TEXT("Left"), Val)) OutOffsets.Left = Val;
	if (TryGetFiniteFloatFieldF(JsonObject, TEXT("Top"), Val)) OutOffsets.Top = Val;
	if (TryGetFiniteFloatFieldF(JsonObject, TEXT("Right"), Val)) OutOffsets.Right = Val;
	if (TryGetFiniteFloatFieldF(JsonObject, TEXT("Bottom"), Val)) OutOffsets.Bottom = Val;
}
