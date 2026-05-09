// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGSettings.h"
#include "Design2UMGLog.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* LegacySettingsSection = TEXT("/Script/Design2UMGEditor.Design2UMGSettings");
	const TCHAR* CurrentSettingsSection = TEXT("/Script/Design2UMGCore.Design2UMGSettings");

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

	FString ExportConfigValue(UObject* Object, FProperty* Property)
	{
		FString ExportedValue;
		if (!Object || !Property)
		{
			return ExportedValue;
		}

		Property->ExportText_InContainer(0, ExportedValue, Object, Object, Object, PPF_None);
		return ExportedValue;
	}

	bool MigrateLegacySettingsSection(UDesign2UMGSettings* Settings)
	{
		if (!Settings || !GConfig)
		{
			return false;
		}

		const FString ConfigPath = GEditorPerProjectIni;
		if (!GConfig->DoesSectionExist(LegacySettingsSection, ConfigPath))
		{
			return false;
		}

		UDesign2UMGSettings* RawDefaults = NewObject<UDesign2UMGSettings>(GetTransientPackage(), NAME_None, RF_Transient);
		bool bUpdated = false;

		for (TFieldIterator<FProperty> It(UDesign2UMGSettings::StaticClass()); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Config))
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			FString LegacyValue;
			if (!GConfig->GetString(LegacySettingsSection, *PropertyName, LegacyValue, ConfigPath))
			{
				continue;
			}

			FString CurrentValue;
			const bool bHasCurrentValue = GConfig->GetString(CurrentSettingsSection, *PropertyName, CurrentValue, ConfigPath);
			const FString DefaultValue = ExportConfigValue(RawDefaults, Property);
			if (bHasCurrentValue && CurrentValue != DefaultValue)
			{
				continue;
			}

			if (!bHasCurrentValue || CurrentValue != LegacyValue)
			{
				GConfig->SetString(CurrentSettingsSection, *PropertyName, *LegacyValue, ConfigPath);
				bUpdated = true;
			}
		}

		if (bUpdated)
		{
			GConfig->Flush(false, ConfigPath);
			Settings->ReloadConfig();
			UE_LOG(LogDesign2UMG, Log, TEXT("Migrated legacy Design2UMG settings section to Design2UMGCore"));
		}

		return bUpdated;
	}
}

UDesign2UMGSettings* UDesign2UMGSettings::Get()
{
	return GetMutableDefault<UDesign2UMGSettings>();
}

bool UDesign2UMGSettings::MigrateLegacySettingsSectionToCurrent()
{
	return MigrateLegacySettingsSection(GetMutableDefault<UDesign2UMGSettings>());
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


