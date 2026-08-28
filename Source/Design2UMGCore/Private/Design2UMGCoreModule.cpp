// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FontResolver.h"
#include "Modules/ModuleManager.h"

class FDesign2UMGCoreModule : public IModuleInterface
{
public:
	virtual void ShutdownModule() override
	{
		// Drain FFontResolver's file-private caches so no resolver state
		// outlives the module on unload.
		FFontResolver::ClearCache();
	}
};

IMPLEMENT_MODULE(FDesign2UMGCoreModule, Design2UMGCore)
