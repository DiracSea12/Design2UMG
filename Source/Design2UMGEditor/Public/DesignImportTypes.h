// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

struct FUMGWidgetData;

struct FDesignFrameInfo
{
	FString Id;
	FString Name;
	float Width = 0.0f;
	float Height = 0.0f;
};

TArray<FDesignFrameInfo> ExtractTopFrames(const FDesignDocument& Document);


