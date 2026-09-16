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
	// Parse Padding values from a single-value or an "L,T,R,B" string.
	FMargin ParsePaddingValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (Value->Type == EJson::String)
		{
			TArray<FString> Parts;
			Value->AsString().ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() == 4)
			{
				return FMargin(
					FCString::Atof(*Parts[0].TrimStartAndEnd()),
					FCString::Atof(*Parts[1].TrimStartAndEnd()),
					FCString::Atof(*Parts[2].TrimStartAndEnd()),
					FCString::Atof(*Parts[3].TrimStartAndEnd())
				);
			}
			if (Parts.Num() == 1)
			{
				return FMargin(FCString::Atof(*Parts[0].TrimStartAndEnd()));
			}
		}

		return FMargin(0);
	}

	bool TryParseVector4(const FString& Value, FVector4& OutValue)
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 4)
		{
			return false;
		}

		float Parsed[4] = {};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Parsed); ++Index)
		{
			if (!LexTryParseString(Parsed[Index], *Parts[Index].TrimStartAndEnd()))
			{
				return false;
			}
		}
		OutValue = FVector4(Parsed[0], Parsed[1], Parsed[2], Parsed[3]);
		return true;
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
			OutWidgetStyle.Padding = ParsePaddingValue(JsonValue);
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::InnerSlotPadding, Str))
		{
			OutWidgetStyle.InnerSlotPadding = FUMGValueParser::ParseVector2D(Str);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*WidgetStyleObject, UMGPropertyKeys::SlotPadding); JsonValue.IsValid())
		{
			OutWidgetStyle.SlotPadding = ParsePaddingValue(JsonValue);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*WidgetStyleObject, UMGPropertyKeys::StrokePadding); JsonValue.IsValid())
		{
			OutWidgetStyle.StrokePadding = ParsePaddingValue(JsonValue);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = DesignJsonUtils::TryGetField(*WidgetStyleObject, UMGPropertyKeys::StrokeMargin); JsonValue.IsValid())
		{
			OutWidgetStyle.StrokeMargin = ParsePaddingValue(JsonValue);
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::ImageMode, Str))
		{
			OutWidgetStyle.ImageMode = Str;
		}
		if (DesignJsonUtils::TryGetString(*WidgetStyleObject, UMGPropertyKeys::ImageTransform, Str))
		{
			OutWidgetStyle.ImageTransform = Str;
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
			OutText.RichTextStyleRowsJson = Str;
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
		if (DesignJsonUtils::TryGetNumber(*BrushObject, UMGPropertyKeys::OutlineCornerRadius, Val) && Val >= 0.0)
			OutBrush.OutlineCornerRadius = (float)Val;
		if (DesignJsonUtils::TryGetFloat(*BrushObject, UMGPropertyKeys::OutlineThickness, FloatVal))
		{
			OutBrush.OutlineThickness = FloatVal;
		}
		if (const TSharedPtr<FJsonValue> Margin = DesignJsonUtils::TryGetField(*BrushObject, UMGPropertyKeys::BrushMargin); Margin.IsValid())
		{
			OutBrush.Margin = ParsePaddingValue(Margin);
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
		if (DesignJsonUtils::TryGetString(*BrushObject, UMGPropertyKeys::TexturePath, Str))
		{
			OutBrush.TexturePath = Str;
		}
		if (DesignJsonUtils::TryGetString(*BrushObject, UMGPropertyKeys::CornerRadii, Str))
		{
			TArray<FString> Parts;
			Str.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() == 4)
			{
				const FVector4 Radii(
					FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]),
					FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
				// Legacy "unset" contract only tested the first component for
				// negativity; keep that exact behavior for IsSet().
				if (Radii.X >= 0.0f)
				{
					OutBrush.CornerRadii = Radii;
				}
			}
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
			if (DesignJsonUtils::TryGetString(*ImageObject, UMGJsonKeys::Transform, Str)
				&& [&]()
				{
					FUMGVisualBrushData::FImageTransformData Transform;
					if (!Design2UMGWidgetNodeAccess::TryParseImageTransform(Str, Transform))
					{
						return false;
					}
					OutBrush.Image.Transform = Transform;
					return true;
				}())
			{
			}
		}
		FString BrushTextureDiagnostic;
		if (Design2UMGWidgetNodeAccess::NormalizeBrushTexturePathOwner(OutBrush, &BrushTextureDiagnostic)
			&& !BrushTextureDiagnostic.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: %s"), *BrushTextureDiagnostic);
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
			if (DesignJsonUtils::TryGetString(*GradientObject, UMGJsonKeys::StartPoint, Str))
			{
				OutBrush.Gradient.StartPoint = FUMGValueParser::ParseVector2D(Str, FVector2D::ZeroVector);
			}
			if (DesignJsonUtils::TryGetString(*GradientObject, UMGJsonKeys::EndPoint, Str))
			{
				OutBrush.Gradient.EndPoint = FUMGValueParser::ParseVector2D(Str, FVector2D(1.0f, 0.0f));
			}
			if (DesignJsonUtils::TryGetString(*GradientObject, UMGJsonKeys::Center, Str))
			{
				OutBrush.Gradient.Center = FUMGValueParser::ParseVector2D(Str, FVector2D(0.5f, 0.5f));
			}
			if (DesignJsonUtils::TryGetString(*GradientObject, UMGJsonKeys::Size, Str))
			{
				OutBrush.Gradient.Size = FUMGValueParser::ParseVector2D(Str, FVector2D(1.0f, 1.0f));
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
		if (DesignJsonUtils::TryGetString(*EffectObject, UMGPropertyKeys::EffectBlurUVStep, Str))
		{
			OutEffect.EffectBlurUVStep = FUMGValueParser::ParseVector2D(Str);
		}
		if (DesignJsonUtils::TryGetString(*EffectObject, UMGPropertyKeys::EffectColor, Str))
		{
			OutEffect.EffectColor = FUMGValueParser::ParseColor(Str);
		}
		if (DesignJsonUtils::TryGetString(*EffectObject, UMGPropertyKeys::EffectOffset, Str))
		{
			OutEffect.EffectOffset = FUMGValueParser::ParseVector2D(Str);
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
		if (DesignJsonUtils::TryGetString(*EffectObject, UMGPropertyKeys::GeometryMaskSize, Str))
		{
			OutEffect.GeometryMaskSize = FUMGValueParser::ParseVector2D(Str);
		}
		if (DesignJsonUtils::TryGetString(*EffectObject, UMGPropertyKeys::GeometryMaskCornerRadii, Str))
		{
			FVector4 Radii;
			if (TryParseVector4(Str, Radii))
			{
				OutEffect.GeometryMaskCornerRadii = Radii;
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

		if (DesignJsonUtils::TryGetString(*ShadowDataObject, UMGPropertyKeys::ShadowOffset, Str))
		{
			OutShadow.ShadowOffset = FUMGValueParser::ParseVector2D(Str);
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
				if (DesignJsonUtils::TryGetString(*LayerObject, UMGJsonKeys::Offset, Str))
				{
					Layer.Offset = FUMGValueParser::ParseVector2D(Str);
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
		if (DesignJsonUtils::TryGetFloat(*RenderObject, UMGPropertyKeys::RenderAngle, Val))
		{
			OutRender.RenderAngle = Val;
		}
		if (DesignJsonUtils::TryGetString(*RenderObject, UMGPropertyKeys::RenderTransformPivot, Str))
		{
			OutRender.RenderTransformPivot = FUMGValueParser::ParseVector2D(Str, FVector2D(0.5f, 0.5f));
		}
		if (DesignJsonUtils::TryGetString(*RenderObject, UMGPropertyKeys::RenderTransformScale, Str))
		{
			OutRender.RenderTransformScale = FUMGValueParser::ParseVector2D(Str);
		}
		if (DesignJsonUtils::TryGetString(*RenderObject, UMGPropertyKeys::RenderTranslation, Str))
		{
			OutRender.RenderTranslation = FUMGValueParser::ParseVector2D(Str);
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
		OutSlot.Padding = ParsePaddingValue(PaddingValue);
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
