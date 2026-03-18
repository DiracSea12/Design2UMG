// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "DesignImportTypes.h"

class FDesignSourceFacade
{
public:
	static bool ConvertBySource(FName SourceId, const FString& RawJson, FUMGWidgetData& OutData);
	static FUMGWidgetData ConvertDocument(const FDesignDocument& Document);
};

