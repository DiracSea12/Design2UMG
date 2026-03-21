// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorModule.h"

#include "Design2UMGEditorActions.h"
#include "Design2UMGLog.h"

void FDesign2UMGEditorModule::StartupModule()
{
	Design2UMGEditorActions::RegisterBuiltInDesignSources();
	Design2UMGEditorActions::RegisterConsoleCommands();
	ToolMenusStartupHandle = Design2UMGEditorActions::RegisterMenus();
	Design2UMGEditorActions::MaybeShowWelcomeDialog();
}

void FDesign2UMGEditorModule::ShutdownModule()
{
	Design2UMGEditorActions::ShutdownTransientState();
	Design2UMGEditorActions::UnregisterMenus(ToolMenusStartupHandle, this);
	Design2UMGEditorActions::UnregisterConsoleCommands();
	Design2UMGEditorActions::UnregisterBuiltInDesignSources();
}

IMPLEMENT_MODULE(FDesign2UMGEditorModule, Design2UMGEditor)
