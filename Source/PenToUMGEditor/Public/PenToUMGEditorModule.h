// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * PenToUMG 编辑器模块
 * .pen 文件 → Widget Blueprint 生成
 */
class FPenToUMGEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle ToolMenusStartupHandle;
};
