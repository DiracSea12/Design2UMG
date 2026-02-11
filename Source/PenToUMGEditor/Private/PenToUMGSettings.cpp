// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenToUMGSettings.h"

UPenToUMGSettings* UPenToUMGSettings::Get()
{
	return GetMutableDefault<UPenToUMGSettings>();
}

UClass* UPenToUMGSettings::FindClassOverride(const FString& NodeName) const
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
