// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
struct FUMGWidgetNode;
namespace RetainerCaptureBounds
{
// Runs before path payloads and symbolic retainer materials are consumed.
bool Prepare(FUMGWidgetNode& Root,FString& Error);
}
