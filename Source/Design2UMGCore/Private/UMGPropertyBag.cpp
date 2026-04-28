// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGSchemaFoundation.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"

namespace
{
	enum class EKnownPropertyValueType : uint8
	{
		String,
		Bool,
		Int,
		Float,
		Color,
		Vector2D,
		Vector4,
		Margin,
	};

	bool IsKnownColorKey(const FString& Key)
	{
		using namespace UMGPropertyKeys;

		if (Key == BackgroundColor || Key == BrushColor || Key == Color || Key == ColorAndOpacity
			|| Key == ContentColor || Key == EffectColor || Key == FillColor || Key == HoveredColor
			|| Key == OutlineColor || Key == PathFillColor || Key == PathStrokeColor
			|| Key == PressedColor || Key == ShadowColorAndOpacity)
		{
			return true;
		}

		if (Key.StartsWith(TEXT("BoxShadow_")))
		{
			TArray<FString> Parts;
			Key.ParseIntoArray(Parts, TEXT("_"));
			return Parts.Num() == 3 && Parts[2] == TEXT("Color");
		}

		return false;
	}

	TOptional<EKnownPropertyValueType> GetKnownPropertyValueType(const FString& Key)
	{
		using namespace UMGPropertyKeys;

		if (Key == AutoWrapText || Key == CanCache || Key == IsChecked || Key == IsEnabled || Key == IsExpanded
			|| Key == IsPassword || Key == IsReadOnly || Key == RetainRendering || Key == Animate
			|| Key == AnimateHorizontally || Key == AnimateOpacity || Key == AnimateVertically
			|| Key == PadLeft || Key == PadRight || Key == PadTop || Key == PadBottom
			|| Key == Underline || Key == Strikethrough)
		{
			return EKnownPropertyValueType::Bool;
		}

		if (Key == ActiveWidgetIndex || Key == NumberOfPieces || Key == Phase || Key == PhaseCount)
		{
			return EKnownPropertyValueType::Int;
		}

		if (Key == BlurStrength || Key == DesignFontSize || Key == EntryHeight || Key == EntryWidth
			|| Key == FontSize || Key == Height || Key == HeightOverride
			|| Key == LetterSpacing || Key == LineHeightPercentage || Key == MaxDesiredHeight
			|| Key == MaxDesiredWidth || Key == MaxValue || Key == MinDesiredHeight
			|| Key == MinDesiredSlotHeight || Key == MinDesiredSlotWidth || Key == MinDesiredWidth
			|| Key == MinValue || Key == MinimumDesiredWidth || Key == OutlineCornerRadius
			|| Key == OutlineThickness || Key == PathStrokeThickness || Key == Percent || Key == Period || Key == Radius
			|| Key == RenderAngle || Key == RenderOpacity || Key == ShadowBlur || Key == UserSpecifiedScale
			|| Key == Value || Key == Width || Key == WidthOverride || Key == WrapTextAt)
		{
			return EKnownPropertyValueType::Float;
		}

		if (IsKnownColorKey(Key))
		{
			return EKnownPropertyValueType::Color;
		}

		if (Key == EffectBlurUVStep || Key == InnerSlotPadding || Key == RenderTransformPivot
			|| Key == RenderTransformScale || Key == RenderTranslation || Key == ShadowOffset)
		{
			return EKnownPropertyValueType::Vector2D;
		}

		if (Key == CornerRadii)
		{
			return EKnownPropertyValueType::Vector4;
		}

		if (Key == Padding || Key == SlotPadding || Key == StrokeMargin || Key == StrokePadding)
		{
			return EKnownPropertyValueType::Margin;
		}

		if (Key == BoxShadowCount)
		{
			return EKnownPropertyValueType::Int;
		}

		return EKnownPropertyValueType::String;
	}

	void AssignTypedValue(FUMGTypedProperty& Property, EKnownPropertyValueType ValueType, const FString& Value)
	{
		Property.SerializedValue = Value;
		Property.bHasValue = true;

		switch (ValueType)
		{
		case EKnownPropertyValueType::Bool:
			Property.BoolValue = Value.ToBool();
			break;
		case EKnownPropertyValueType::Int:
			Property.IntValue = FCString::Atoi(*Value);
			break;
		case EKnownPropertyValueType::Float:
			Property.FloatValue = FCString::Atof(*Value);
			break;
		case EKnownPropertyValueType::Color:
			Property.ColorValue = FUMGValueParser::ParseColor(Value);
			break;
		case EKnownPropertyValueType::Vector2D:
			Property.Vector2DValue = FUMGValueParser::ParseVector2D(Value);
			break;
		case EKnownPropertyValueType::Vector4:
		{
			TArray<FString> Parts;
			Value.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() == 4)
			{
				Property.Vector4Value = FVector4(
					FCString::Atof(*Parts[0].TrimStartAndEnd()),
					FCString::Atof(*Parts[1].TrimStartAndEnd()),
					FCString::Atof(*Parts[2].TrimStartAndEnd()),
					FCString::Atof(*Parts[3].TrimStartAndEnd()));
			}
			break;
		}
		case EKnownPropertyValueType::Margin:
			Property.MarginValue = FUMGValueParser::ParseMargin(Value);
			break;
		case EKnownPropertyValueType::String:
		default:
			break;
		}
	}

}

void FUMGPropertyBag::Add(const FString& Key, const FString& Value)
{
	FUMGTypedProperty& Property = Values.FindOrAdd(Key);
	AssignTypedValue(Property, GetKnownPropertyValueType(Key).Get(EKnownPropertyValueType::String), Value);
}

void FUMGPropertyBag::Add(const TCHAR* Key, const FString& Value)
{
	Add(FString(Key), Value);
}

void FUMGPropertyBag::Add(const TCHAR* Key, const TCHAR* Value)
{
	Add(FString(Key), FString(Value));
}

int32 FUMGPropertyBag::Remove(const FString& Key)
{
	return Values.Remove(Key);
}

int32 FUMGPropertyBag::Remove(const TCHAR* Key)
{
	return Remove(FString(Key));
}

int32 FUMGPropertyBag::Num() const
{
	return Values.Num();
}

void FUMGPropertyBag::Empty()
{
	Values.Empty();
}

bool FUMGPropertyBag::TryGetBool(const FString& Key, bool& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = Property->SerializedValue.ToBool();
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetInt(const FString& Key, int32& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = FCString::Atoi(*Property->SerializedValue);
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetFloat(const FString& Key, float& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = FCString::Atof(*Property->SerializedValue);
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetString(const FString& Key, FString& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = Property->SerializedValue;
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetColor(const FString& Key, FLinearColor& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		if (IsKnownColorKey(Key))
		{
			OutValue = Property->ColorValue;
			return Property->bHasValue;
		}

		OutValue = FUMGValueParser::ParseColor(Property->SerializedValue);
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetVector2D(const FString& Key, FVector2D& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = FUMGValueParser::ParseVector2D(Property->SerializedValue);
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetVector4(const FString& Key, FVector4& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		TArray<FString> Parts;
		Property->SerializedValue.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() != 4)
		{
			return false;
		}

		OutValue = FVector4(
			FCString::Atof(*Parts[0].TrimStartAndEnd()),
			FCString::Atof(*Parts[1].TrimStartAndEnd()),
			FCString::Atof(*Parts[2].TrimStartAndEnd()),
			FCString::Atof(*Parts[3].TrimStartAndEnd()));
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetMargin(const FString& Key, FMargin& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = FUMGValueParser::ParseMargin(Property->SerializedValue);
		return Property->bHasValue;
	}
	return false;
}

void FUMGPropertyBag::ForEachSerialized(TFunctionRef<void(const FString&, const FString&)> Visitor) const
{
	for (const TPair<FString, FUMGTypedProperty>& Pair : Values)
	{
		Visitor(Pair.Key, Pair.Value.SerializedValue);
	}
}
