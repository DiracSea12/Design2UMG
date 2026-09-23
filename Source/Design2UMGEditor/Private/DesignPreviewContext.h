// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "UMGSchema.h"

// Merge only non-editable import context; the edited widget tree remains authoritative.
bool RestoreDesignPreviewContext(const FUMGWidgetData& Original, FUMGWidgetData& Edited, FString& OutError);
