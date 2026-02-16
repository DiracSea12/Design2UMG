// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "Design2UMGSettings.h"

UDesign2UMGSettings* UDesign2UMGSettings::Get()
{
	return GetMutableDefault<UDesign2UMGSettings>();
}

FString UDesign2UMGSettings::FindIconFontAssetPath(const FString& FontFamily) const
{
	if (const FString* Path = IconFontAssetMap.Find(FontFamily))
		return *Path;
	return FString();
}

UClass* UDesign2UMGSettings::FindClassOverride(const FString& NodeName) const
{
	for (const FWidgetClassOverride& Rule : ClassOverrides)
	{
		if (Rule.NamePattern.IsEmpty() || Rule.WidgetClass.IsNull())
			continue;
		if (NodeName.Contains(Rule.NamePattern))
		{
			UClass* Loaded = Rule.WidgetClass.LoadSynchronous();
			if (Loaded && Loaded->IsChildOf(UWidget::StaticClass()))
				return Loaded;
		}
	}
	return nullptr;
}
