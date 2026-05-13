// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGGenerationSettingsSnapshot.h"

#include "Components/Widget.h"

namespace
{
	thread_local const FDesign2UMGGenerationSettingsSnapshot* GCurrentSnapshot = nullptr;
}

FDesign2UMGGenerationSettingsSnapshot FDesign2UMGGenerationSettingsSnapshot::FromSettings(const UDesign2UMGSettings& Settings)
{
	FDesign2UMGGenerationSettingsSnapshot Snapshot;
	Snapshot.PrototypeNavigationGenerationMode = Settings.PrototypeNavigationGenerationMode;
	Snapshot.ClassOverrides = Settings.ClassOverrides;
	return Snapshot;
}

UClass* FDesign2UMGGenerationSettingsSnapshot::FindClassOverride(const FString& NodeName) const
{
	for (const FWidgetClassOverride& Rule : ClassOverrides)
	{
		if (Rule.NamePattern.IsEmpty() || Rule.WidgetClass.IsNull())
		{
			continue;
		}

		if (NodeName.Contains(Rule.NamePattern, ESearchCase::IgnoreCase))
		{
			UClass* Loaded = Rule.WidgetClass.LoadSynchronous();
			if (Loaded && Loaded->IsChildOf(UWidget::StaticClass()))
			{
				return Loaded;
			}
		}
	}

	return nullptr;
}

namespace Design2UMGGenerationSettingsContext
{
	const FDesign2UMGGenerationSettingsSnapshot* GetCurrent()
	{
		return GCurrentSnapshot;
	}

	FScopedSettingsSnapshot::FScopedSettingsSnapshot(const FDesign2UMGGenerationSettingsSnapshot& InSnapshot)
		: PreviousSnapshot(GCurrentSnapshot)
		, Snapshot(InSnapshot)
	{
		GCurrentSnapshot = &Snapshot;
	}

	FScopedSettingsSnapshot::~FScopedSettingsSnapshot()
	{
		GCurrentSnapshot = PreviousSnapshot;
	}
}
