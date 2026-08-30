// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace Design2UMGEditorUnattendedExit
{
	bool ExitUnattendedWithStatus(const bool bSuccess, const TCHAR* Message);
	bool ShouldUsePreviewInCurrentMode(const bool bRequestedPreview);
}
