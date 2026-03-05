// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct DESIGN2UMGEDITOR_API FDesign2UMGNotification
{
	static void Success(const FText& Message, const FString& AssetPath = TEXT(""));
	static void Warning(const FText& Message);
	static void Error(const FText& Message);
};

