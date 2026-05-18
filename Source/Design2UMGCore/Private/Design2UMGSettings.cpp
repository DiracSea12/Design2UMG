// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGSettings.h"

namespace
{
	bool IsSettingsHexColorLiteral(const FString& InColor)
	{
		if (!InColor.StartsWith(TEXT("#")) || (InColor.Len() != 7 && InColor.Len() != 9))
		{
			return false;
		}

		for (int32 Index = 1; Index < InColor.Len(); ++Index)
		{
			if (!FChar::IsHexDigit(InColor[Index]))
			{
				return false;
			}
		}

		return true;
	}

	FString NormalizeSettingsFontFamilyKey(const FString& InFamily)
	{
		FString Key = InFamily.ToLower().TrimStartAndEnd();
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		Key.ReplaceInline(TEXT("-"), TEXT(""));
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		return Key;
	}
}

UDesign2UMGSettings* UDesign2UMGSettings::Get()
{
	return GetMutableDefault<UDesign2UMGSettings>();
}

UDesign2UMGSettings::FResolvedPlaceholderHatchStyle UDesign2UMGSettings::ResolvePlaceholderHatchStyle() const
{
	FResolvedPlaceholderHatchStyle Style;
	Style.BaseFillColor = CanonicalPlaceholderHatchBackgroundColor;
	Style.RefFillColor = CanonicalPlaceholderRefHatchBackgroundColor;
	Style.BaseStripeColor = CanonicalPlaceholderHatchStripeColor;
	Style.RefStripeColor = CanonicalPlaceholderRefHatchStripeColor;

	if (!bUseCustomPlaceholderHatchStyle)
	{
		return Style;
	}

	if (IsSettingsHexColorLiteral(PlaceholderHatchBackgroundColor))
	{
		Style.BaseFillColor = PlaceholderHatchBackgroundColor;
	}
	if (IsSettingsHexColorLiteral(PlaceholderRefHatchBackgroundColor))
	{
		Style.RefFillColor = PlaceholderRefHatchBackgroundColor;
	}
	if (IsSettingsHexColorLiteral(PlaceholderHatchStripeColor))
	{
		Style.BaseStripeColor = PlaceholderHatchStripeColor;
	}
	if (IsSettingsHexColorLiteral(PlaceholderRefHatchStripeColor))
	{
		Style.RefStripeColor = PlaceholderRefHatchStripeColor;
	}

	Style.StripeWidthPx = FMath::Clamp(PlaceholderHatchStripeWidth, 1, 32);
	Style.StripeGapPx = FMath::Clamp(PlaceholderHatchStripeGap, 1, 64);
	return Style;
}

float UDesign2UMGSettings::ResolveLuminosityFitOpacityCap() const
{
	return bUseCustomLuminosityFitOpacityCap
		? FMath::Clamp(LuminosityFitOpacityCap, 0.0f, 1.0f)
		: CanonicalLuminosityFitOpacityCap;
}

FString UDesign2UMGSettings::FindIconFontAssetPath(const FString& FontFamily) const
{
	if (FontFamily.TrimStartAndEnd().IsEmpty())
		return FString();

	if (const TSoftObjectPtr<UFont>* FontPtr = IconFontAssetMap.Find(FontFamily))
	{
		if (!FontPtr->IsNull())
			return FontPtr->ToString();
	}

	const FString NormalizedQuery = NormalizeSettingsFontFamilyKey(FontFamily);
	for (const TPair<FString, TSoftObjectPtr<UFont>>& Pair : IconFontAssetMap)
	{
		if (Pair.Value.IsNull())
			continue;
		if (NormalizeSettingsFontFamilyKey(Pair.Key) == NormalizedQuery)
			return Pair.Value.ToString();
	}

	return FString();
}

UClass* UDesign2UMGSettings::FindClassOverride(const FString& NodeName) const
{
	for (const FWidgetClassOverride& Rule : ClassOverrides)
	{
		if (Rule.NamePattern.IsEmpty() || Rule.WidgetClass.IsNull())
			continue;
		if (NodeName.Contains(Rule.NamePattern, ESearchCase::IgnoreCase))
		{
			UClass* Loaded = Rule.WidgetClass.LoadSynchronous();
			if (Loaded && Loaded->IsChildOf(UWidget::StaticClass()))
				return Loaded;
		}
	}
	return nullptr;
}


