// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignSourceFacade.h"
#include "DesignSourceRegistry.h"
#include "Design2UMGConverter.h"

bool FDesignSourceFacade::ConvertBySource(FName SourceId, const FString& RawJson, FUMGWidgetData& OutData)
{
	return FDesignSourceRegistry::Convert(SourceId, RawJson, OutData);
}

FUMGWidgetData FDesignSourceFacade::ConvertDocument(const FDesignDocument& Document)
{
	return FDesign2UMGConverter::Convert(Document);
}

