// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Design2UMG 编辑器模块
 * .pen 文件 → Widget Blueprint 生成
 */
class FDesign2UMGEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle ToolMenusStartupHandle;
};
