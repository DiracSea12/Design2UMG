// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "Design2UMGSettings.h"
#include "Design2UMGLog.h"

namespace
{
	constexpr int32 DefaultDesignWidth = 1920;
	constexpr int32 DefaultDesignHeight = 1080;

	bool IsInvalidDesignSize(const int32 Width, const int32 Height)
	{
		const bool bInvalidWidth = Width < 320 || Width > 7680;
		const bool bInvalidHeight = Height < 240 || Height > 4320;
		return bInvalidWidth || bInvalidHeight;
	}

	void EnsureValidDesignSize(UDesign2UMGSettings* Settings)
	{
		if (!Settings)
		{
			return;
		}

		if (!IsInvalidDesignSize(Settings->DesignWidth, Settings->DesignHeight))
		{
			return;
		}

		UE_LOG(LogDesign2UMG, Warning,
			TEXT("Invalid DesignSize in config (%d x %d), auto-repair to %d x %d"),
			Settings->DesignWidth,
			Settings->DesignHeight,
			DefaultDesignWidth,
			DefaultDesignHeight);

		Settings->DesignWidth = DefaultDesignWidth;
		Settings->DesignHeight = DefaultDesignHeight;
		Settings->SaveConfig();
	}

	FString NormalizeFontFamilyKey(const FString& InFamily)
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
	UDesign2UMGSettings* Settings = GetMutableDefault<UDesign2UMGSettings>();
	EnsureValidDesignSize(Settings);
	return Settings;
}

FIntPoint UDesign2UMGSettings::GetDesignSize() const
{
	if (IsInvalidDesignSize(DesignWidth, DesignHeight))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Invalid DesignSize in config (%d x %d), fallback to %d x %d"),
			DesignWidth, DesignHeight,
			DefaultDesignWidth, DefaultDesignHeight);
		return FIntPoint(DefaultDesignWidth, DefaultDesignHeight);
	}

	return FIntPoint(DesignWidth, DesignHeight);
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

	const FString NormalizedQuery = NormalizeFontFamilyKey(FontFamily);
	for (const TPair<FString, TSoftObjectPtr<UFont>>& Pair : IconFontAssetMap)
	{
		if (Pair.Value.IsNull())
			continue;
		if (NormalizeFontFamilyKey(Pair.Key) == NormalizedQuery)
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

