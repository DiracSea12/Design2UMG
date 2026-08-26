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
	// Keep the authored alpha independent from the RGB state tint. Hover moves
	// 10% toward white so black still has a visible state color; pressed moves
	// 10% toward black. State RGB values are clamped to the usable 0..1 range.
	// Keep these multipliers stable unless the formal visual gate proves a change.
	constexpr float HoverTintColorMultiplier = 1.1f;
	constexpr float PressedTintColorMultiplier = 0.9f;

	inline FLinearColor MakeHoverTintColor(const FLinearColor& BackgroundColor)
	{
		const float HoverAmount = HoverTintColorMultiplier - 1.0f;
		return FLinearColor(
			FMath::Clamp(BackgroundColor.R + ((1.0f - BackgroundColor.R) * HoverAmount), 0.0f, 1.0f),
			FMath::Clamp(BackgroundColor.G + ((1.0f - BackgroundColor.G) * HoverAmount), 0.0f, 1.0f),
			FMath::Clamp(BackgroundColor.B + ((1.0f - BackgroundColor.B) * HoverAmount), 0.0f, 1.0f),
			BackgroundColor.A);
	}

	inline FLinearColor MakePressedTintColor(const FLinearColor& BackgroundColor)
	{
		return FLinearColor(
			FMath::Clamp(BackgroundColor.R * PressedTintColorMultiplier, 0.0f, 1.0f),
			FMath::Clamp(BackgroundColor.G * PressedTintColorMultiplier, 0.0f, 1.0f),
			FMath::Clamp(BackgroundColor.B * PressedTintColorMultiplier, 0.0f, 1.0f),
			BackgroundColor.A);
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
