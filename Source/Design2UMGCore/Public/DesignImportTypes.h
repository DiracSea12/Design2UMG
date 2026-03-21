// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

struct FUMGWidgetData;

struct DESIGN2UMGCORE_API FDesignFrameInfo
{
	FString Id;
	FString Name;
	float Width = 0.0f;
	float Height = 0.0f;
};

DESIGN2UMGCORE_API TArray<FDesignFrameInfo> ExtractTopFrames(const FDesignDocument& Document);


