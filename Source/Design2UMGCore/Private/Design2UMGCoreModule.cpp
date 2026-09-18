// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGFontMetrics.h"
#include "FontResolver.h"
#include "TextMeasurementRuntimeProbe.h"

#include "Modules/ModuleManager.h"

class FDesign2UMGCoreModule : public IModuleInterface
{
public:
	virtual void ShutdownModule() override
	{
		// Release module-owned objects and caches before unload / hot-reload.
		TextMeasurementRuntimeProbe::ResetProbeForModuleShutdown();
		Design2UMGFontMetrics::ClearCache();
		FFontResolver::ClearCache();
	}
};

IMPLEMENT_MODULE(FDesign2UMGCoreModule, Design2UMGCore)
