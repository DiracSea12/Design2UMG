// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

struct FUMGWidgetData;
struct FUMGWidgetNode;

struct FDesignImportResult
{
	bool bSuccess = false;
	int32 WidgetCount = 0;
	int32 WarningCount = 0;
	double ElapsedSeconds = 0.0;
	TArray<FString> Warnings;

	static int32 CountWidgets(const FUMGWidgetNode& Node);
	static FDesignImportResult FromData(const FUMGWidgetData& Data, double Elapsed);
	void ShowSummaryNotification() const;
	void ShowSummaryDialog() const;
};

struct FDesignFrameInfo
{
	FString Id;
	FString Name;
	float Width = 0.0f;
	float Height = 0.0f;
};

TArray<FDesignFrameInfo> ExtractTopFrames(const FDesignDocument& Document);


