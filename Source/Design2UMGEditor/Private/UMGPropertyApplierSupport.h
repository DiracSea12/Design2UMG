// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Components/Widget.h"
#include "UMGSchema.h"

namespace UMGPropertyApplierSupport
{
	inline bool TryGetProp(const FUMGPropertyBag& Properties, const TCHAR* Key, FString& OutValue)
	{
		return Properties.TryGetString(Key, OutValue) && !OutValue.IsEmpty();
	}

	inline bool TryGetPropAllowEmpty(const FUMGPropertyBag& Properties, const TCHAR* Key, FString& OutValue)
	{
		return Properties.TryGetString(Key, OutValue);
	}

	inline bool HasProp(const FUMGPropertyBag& Properties, const TCHAR* Key)
	{
		FString Value;
		return TryGetProp(Properties, Key, Value);
	}

	inline bool TryParseVector4(const FString& Value, FVector4& OutVector)
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() != 4)
		{
			return false;
		}

		OutVector = FVector4(
			FCString::Atof(*Parts[0].TrimStartAndEnd()),
			FCString::Atof(*Parts[1].TrimStartAndEnd()),
			FCString::Atof(*Parts[2].TrimStartAndEnd()),
			FCString::Atof(*Parts[3].TrimStartAndEnd()));
		return true;
	}

	inline bool TryParseDrawAs(const FString& Value, ESlateBrushDrawType::Type& OutDrawAs)
	{
		static const TMap<FString, ESlateBrushDrawType::Type> DrawAsMap = {
			{TEXT("NoDrawType"), ESlateBrushDrawType::NoDrawType},
			{TEXT("Box"), ESlateBrushDrawType::Box},
			{TEXT("Border"), ESlateBrushDrawType::Border},
			{TEXT("Image"), ESlateBrushDrawType::Image},
			{TEXT("RoundedBox"), ESlateBrushDrawType::RoundedBox},
		};

		if (const ESlateBrushDrawType::Type* Found = DrawAsMap.Find(Value))
		{
			OutDrawAs = *Found;
			return true;
		}
		return false;
	}

	inline bool TryParseWidgetClipping(const FString& Value, EWidgetClipping& OutClipping)
	{
		static const TMap<FString, EWidgetClipping> ClippingMap = {
			{TEXT("Inherit"), EWidgetClipping::Inherit},
			{TEXT("ClipToBounds"), EWidgetClipping::ClipToBounds},
			{TEXT("ClipToBoundsWithoutIntersecting"), EWidgetClipping::ClipToBoundsWithoutIntersecting},
			{TEXT("ClipToBoundsAlways"), EWidgetClipping::ClipToBoundsAlways},
			{TEXT("OnDemand"), EWidgetClipping::OnDemand},
		};

		if (const EWidgetClipping* Found = ClippingMap.Find(Value))
		{
			OutClipping = *Found;
			return true;
		}
		return false;
	}
}
