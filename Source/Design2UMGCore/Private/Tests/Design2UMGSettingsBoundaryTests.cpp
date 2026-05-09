// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGSettings.h"

#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TCHAR* LegacySettingsSection = TEXT("/Script/Design2UMGEditor.Design2UMGSettings");
	const TCHAR* CurrentSettingsSection = TEXT("/Script/Design2UMGCore.Design2UMGSettings");
	const TCHAR* AutoImportFontsKey = TEXT("bAutoImportFonts");

	UDesign2UMGSettings* GetExistingSettingsDefault()
	{
		return Cast<UDesign2UMGSettings>(UDesign2UMGSettings::StaticClass()->GetDefaultObject(false));
	}

	struct FScopedSettingsConfigKeyOverride
	{
		FScopedSettingsConfigKeyOverride()
		{
			if (UDesign2UMGSettings* Settings = GetExistingSettingsDefault())
			{
				bHadSettings = true;
				bSavedAutoImportFonts = Settings->bAutoImportFonts;
			}

			if (GConfig)
			{
				bHadCurrentValue = GConfig->GetString(CurrentSettingsSection, AutoImportFontsKey, SavedCurrentValue, GEditorPerProjectIni);
				bHadLegacyValue = GConfig->GetString(LegacySettingsSection, AutoImportFontsKey, SavedLegacyValue, GEditorPerProjectIni);
			}
		}

		~FScopedSettingsConfigKeyOverride()
		{
			if (!GConfig)
			{
				return;
			}

			if (bHadCurrentValue)
			{
				GConfig->SetString(CurrentSettingsSection, AutoImportFontsKey, *SavedCurrentValue, GEditorPerProjectIni);
			}
			else
			{
				GConfig->RemoveKey(CurrentSettingsSection, AutoImportFontsKey, GEditorPerProjectIni);
			}

			if (bHadLegacyValue)
			{
				GConfig->SetString(LegacySettingsSection, AutoImportFontsKey, *SavedLegacyValue, GEditorPerProjectIni);
			}
			else
			{
				GConfig->RemoveKey(LegacySettingsSection, AutoImportFontsKey, GEditorPerProjectIni);
			}

			if (UDesign2UMGSettings* Settings = GetExistingSettingsDefault())
			{
				Settings->ReloadConfig();
				if (bHadSettings)
				{
					Settings->bAutoImportFonts = bSavedAutoImportFonts;
				}
			}
		}

		bool bHadSettings = false;
		bool bHadCurrentValue = false;
		bool bHadLegacyValue = false;
		bool bSavedAutoImportFonts = false;
		FString SavedCurrentValue;
		FString SavedLegacyValue;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesign2UMGSettingsGetDoesNotMigrateLegacySectionTest,
	"Design2UMG.Settings.GetDoesNotMigrateLegacySection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesign2UMGSettingsGetDoesNotMigrateLegacySectionTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GConfig is available"), GConfig))
	{
		return false;
	}

	FScopedSettingsConfigKeyOverride ConfigRestore;
	GConfig->SetString(CurrentSettingsSection, AutoImportFontsKey, TEXT("False"), GEditorPerProjectIni);
	GConfig->SetString(LegacySettingsSection, AutoImportFontsKey, TEXT("True"), GEditorPerProjectIni);

	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestNotNull(TEXT("Settings singleton is available"), Settings);

	FString CurrentValue;
	const bool bHasCurrentValue = GConfig->GetString(CurrentSettingsSection, AutoImportFontsKey, CurrentValue, GEditorPerProjectIni);
	TestTrue(TEXT("Current settings key remains present"), bHasCurrentValue);
	TestEqual(TEXT("Get does not migrate legacy section into current config"), CurrentValue, FString(TEXT("False")));

	return true;
}

#endif
