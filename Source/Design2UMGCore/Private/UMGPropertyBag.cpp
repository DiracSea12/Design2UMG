// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGSchema.h"
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
			|| Key == FillColor || Key == FontSize || Key == Height || Key == HeightOverride
			|| Key == LetterSpacing || Key == LineHeightPercentage || Key == MaxDesiredHeight
			|| Key == MaxDesiredWidth || Key == MaxValue || Key == MinDesiredHeight
			|| Key == MinDesiredSlotHeight || Key == MinDesiredSlotWidth || Key == MinDesiredWidth
			|| Key == MinValue || Key == MinimumDesiredWidth || Key == OutlineCornerRadius
			|| Key == OutlineThickness || Key == Percent || Key == Period || Key == Radius
			|| Key == RenderAngle || Key == RenderOpacity || Key == ShadowBlur || Key == UserSpecifiedScale
			|| Key == Value || Key == Width || Key == WidthOverride || Key == WrapTextAt)
		{
			return EKnownPropertyValueType::Float;
		}

		if (Key == BackgroundColor || Key == BrushColor || Key == Color || Key == ColorAndOpacity
			|| Key == ContentColor || Key == FillColor || Key == HoveredColor || Key == OutlineColor
			|| Key == PathFillColor || Key == PressedColor || Key == ShadowColorAndOpacity)
		{
			return EKnownPropertyValueType::Color;
		}

		if (Key == InnerSlotPadding || Key == RenderScale || Key == RenderTransformScale
			|| Key == RenderTranslation || Key == ShadowOffset)
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
	bCacheDirty = true;
}

void FUMGPropertyBag::Add(const TCHAR* Key, const FString& Value)
{
	Add(FString(Key), Value);
}

void FUMGPropertyBag::Add(const TCHAR* Key, const TCHAR* Value)
{
	Add(FString(Key), FString(Value));
}

bool FUMGPropertyBag::Contains(const FString& Key) const
{
	return Values.Contains(Key);
}

bool FUMGPropertyBag::Contains(const TCHAR* Key) const
{
	return Contains(FString(Key));
}

const FString* FUMGPropertyBag::Find(const FString& Key) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		return &Property->SerializedValue;
	}
	return nullptr;
}

const FString* FUMGPropertyBag::Find(const TCHAR* Key) const
{
	return Find(FString(Key));
}

FString FUMGPropertyBag::FindRef(const FString& Key) const
{
	if (const FString* Value = Find(Key))
	{
		return *Value;
	}
	return FString();
}

FString FUMGPropertyBag::FindRef(const TCHAR* Key) const
{
	return FindRef(FString(Key));
}

FString FUMGPropertyBag::GetValueOr(const FString& Key, const FString& DefaultValue) const
{
	if (const FString* Value = Find(Key))
	{
		return *Value;
	}
	return DefaultValue;
}

FString FUMGPropertyBag::GetValueOr(const TCHAR* Key, const FString& DefaultValue) const
{
	return GetValueOr(FString(Key), DefaultValue);
}

int32 FUMGPropertyBag::Remove(const FString& Key)
{
	const int32 Removed = Values.Remove(Key);
	if (Removed > 0)
	{
		bCacheDirty = true;
	}
	return Removed;
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
	CachedMap.Empty();
	bCacheDirty = false;
}

const FString& FUMGPropertyBag::operator[](const FString& Key) const
{
	static const FString EmptyString;
	if (const FString* Value = Find(Key))
	{
		return *Value;
	}
	return EmptyString;
}

const FString& FUMGPropertyBag::operator[](const TCHAR* Key) const
{
	return (*this)[FString(Key)];
}

const TMap<FString, FString>& FUMGPropertyBag::AsMap() const
{
	if (!bCacheDirty)
	{
		return CachedMap;
	}

	CachedMap.Empty(Values.Num());
	for (const TPair<FString, FUMGTypedProperty>& Pair : Values)
	{
		CachedMap.Add(Pair.Key, Pair.Value.SerializedValue);
	}
	bCacheDirty = false;
	return CachedMap;
}

TMap<FString, FString> FUMGPropertyBag::ToMap() const
{
	return AsMap();
}

bool FUMGPropertyBag::TryGetBool(const FString& Key, bool& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = Property->BoolValue;
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetInt(const FString& Key, int32& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = Property->IntValue;
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetFloat(const FString& Key, float& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = Property->FloatValue;
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
		OutValue = Property->ColorValue;
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetVector2D(const FString& Key, FVector2D& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = Property->Vector2DValue;
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetVector4(const FString& Key, FVector4& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = Property->Vector4Value;
		return Property->bHasValue;
	}
	return false;
}

bool FUMGPropertyBag::TryGetMargin(const FString& Key, FMargin& OutValue) const
{
	if (const FUMGTypedProperty* Property = Values.Find(Key))
	{
		OutValue = Property->MarginValue;
		return Property->bHasValue;
	}
	return false;
}
