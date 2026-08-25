// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Components/Widget.h"
#include "UMGSchema.h"
#include "UMGWidgetNodeAccess.h"

namespace UMGPropertyApplierSupport
{
	// Button hover/pressed tint derivation from the authored background color.
	// Hover brightens by 10%, pressed darkens by 10% (linear-space RGBA multiply
	// via FLinearColor::operator*, no clamping: channels may legitimately exceed 1
	// for HDR values, and multiplying by <=1.1 preserves the design's intent).
	// Keep these multipliers stable unless the formal visual gate proves a change.
	constexpr float HoverTintColorMultiplier = 1.1f;
	constexpr float PressedTintColorMultiplier = 0.9f;

	inline FLinearColor MakeHoverTintColor(const FLinearColor& BackgroundColor)
	{
		return BackgroundColor * HoverTintColorMultiplier;
	}

	inline FLinearColor MakePressedTintColor(const FLinearColor& BackgroundColor)
	{
		return BackgroundColor * PressedTintColorMultiplier;
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
		return Design2UMGWidgetNodeAccess::TryParseDrawAs(Value, OutDrawAs);
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
