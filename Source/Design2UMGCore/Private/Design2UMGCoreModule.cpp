// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FontResolver.h"
#include "TextMeasurementRuntimeProbe.h"

#include "Editor.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/ImportSubsystem.h"

class FDesign2UMGCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// UImportSubsystem is an editor subsystem that does not exist yet while modules
		// start on a cold editor launch. Bind immediately when it is already alive
		// (module reload while the editor runs), otherwise defer until post engine init.
		if (!TrySubscribeFontReimportHook())
		{
			FCoreDelegates::OnPostEngineInit.AddRaw(this, &FDesign2UMGCoreModule::HandlePostEngineInit);
		}
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnPostEngineInit.RemoveAll(this);
		if (UImportSubsystem* ImportSubsystem = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr)
		{
			ImportSubsystem->OnAssetReimport.RemoveAll(this);
		}

		// Release the rooted probe UTextBlock and drain the hhea lineGap cache so no
		// probe UObject or font metric outlives the module on unload / hot-reload.
		TextMeasurementRuntimeProbe::ResetProbeForModuleShutdown();
		TextMeasurementRuntimeProbe::ClearLineGapEmCache();

		// Drain FFontResolver's file-private caches so no resolver state
		// outlives the module on unload.
		FFontResolver::ClearCache();
	}

private:
	bool TrySubscribeFontReimportHook()
	{
		UImportSubsystem* ImportSubsystem = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr;
		if (!ImportSubsystem)
		{
			return false;
		}
		ImportSubsystem->OnAssetReimport.AddRaw(this, &FDesign2UMGCoreModule::HandleAssetReimported);
		return true;
	}

	void HandlePostEngineInit()
	{
		TrySubscribeFontReimportHook();
	}

	void HandleAssetReimported(UObject* ReimportedObject)
	{
		// Both composite font and individual face imports can change measured metrics.
		if (ReimportedObject && (ReimportedObject->IsA<UFont>() || ReimportedObject->IsA<UFontFace>()))
		{
			TextMeasurementRuntimeProbe::ClearLineGapEmCache();
		}
	}
};

IMPLEMENT_MODULE(FDesign2UMGCoreModule, Design2UMGCore)
