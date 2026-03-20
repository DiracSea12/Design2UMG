// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace Design2UMGEditorActions
{
	void RegisterBuiltInDesignSources();
	void UnregisterBuiltInDesignSources();
	void RegisterConsoleCommands();
	void UnregisterConsoleCommands();
	FDelegateHandle RegisterMenus();
	void UnregisterMenus(FDelegateHandle StartupHandle, void* Owner);
	void MaybeShowWelcomeDialog();
	void ShutdownTransientState();
}
