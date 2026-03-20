// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGConverter.h"
#include "DesignSchema.h"
#include "DesignImportTypes.h"
#include "DesignSourceRegistry.h"

class FDesignSourceFacade
{
public:
	static bool ConvertBySource(FName SourceId, const FString& RawJson, FUMGWidgetData& OutData)
	{
		return FDesignSourceRegistry::Convert(SourceId, RawJson, OutData);
	}

	static FUMGWidgetData ConvertDocument(const FDesignDocument& Document)
	{
		return FDesign2UMGConverter::Convert(Document);
	}
};

