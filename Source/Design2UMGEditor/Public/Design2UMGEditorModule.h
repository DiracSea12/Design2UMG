// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Design2UMG 缂栬緫鍣ㄦā鍧? * .pen 鏂囦欢 鈫?Widget Blueprint 鐢熸垚
 */
class FDesign2UMGEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle ToolMenusStartupHandle;
};

