// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

// Shared sidecar-read and output-dir cleanup helpers for Editor workflow tests
// (single copy so unity builds never merge two anonymous-namespace definitions).

#include "Design2UMGGenerationManifest.h"
#include "Design2UMGSettings.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace Design2UMGTestUtils
{
	// Prototype-navigation QA cases pin the opt-in RuntimeExtension mode because the
	// product default is Disabled (editor-preview-only; packaging strips those classes).
	struct FScopedPrototypeNavigationGenerationMode
	{
		explicit FScopedPrototypeNavigationGenerationMode(
			EDesign2UMGPrototypeNavigationGenerationMode InMode)
		{
			Settings = UDesign2UMGSettings::Get();
			if (Settings)
			{
				SavedMode = Settings->PrototypeNavigationGenerationMode;
				Settings->PrototypeNavigationGenerationMode = InMode;
			}
		}

		~FScopedPrototypeNavigationGenerationMode()
		{
			if (Settings)
			{
				Settings->PrototypeNavigationGenerationMode = SavedMode;
				// Same ini-leak guard as the reimport QA fixtures scope.
				Settings->SaveConfig();
			}
		}

		TObjectPtr<UDesign2UMGSettings> Settings;
		EDesign2UMGPrototypeNavigationGenerationMode SavedMode =
			EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension;
	};

	inline bool ReadSidecarJson(const FString& AssetPackageName, TSharedPtr<FJsonObject>& OutObject)
	{
		const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(AssetPackageName);
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *SidecarPath))
		{
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	// Clear the output directory (including .d2u.json sidecars and leftover assets) so tests stay independent.
	inline void DeleteOutputDirectory(const FString& OutputPath)
	{
		const FString OutputDir = FPackageName::LongPackageNameToFilename(OutputPath);
		if (!OutputDir.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*OutputDir, false, true);
		}
	}
}
