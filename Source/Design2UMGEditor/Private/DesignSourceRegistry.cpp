// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "DesignSourceRegistry.h"

void FDesignSourceRegistry::Register(FName SourceId, FDesignSourceConvertFunc Func)
{
	GetRegistry().Add(SourceId, MoveTemp(Func));
}

void FDesignSourceRegistry::Unregister(FName SourceId)
{
	GetRegistry().Remove(SourceId);
}

bool FDesignSourceRegistry::Convert(FName SourceId, const FString& RawJson, FUMGWidgetData& OutData)
{
	if (auto* Func = GetRegistry().Find(SourceId))
	{
		return (*Func)(RawJson, OutData);
	}
	return false;
}

bool FDesignSourceRegistry::IsRegistered(FName SourceId)
{
	return GetRegistry().Contains(SourceId);
}

TArray<FName> FDesignSourceRegistry::GetRegisteredSources()
{
	TArray<FName> Keys;
	GetRegistry().GetKeys(Keys);
	return Keys;
}

TMap<FName, FDesignSourceConvertFunc>& FDesignSourceRegistry::GetRegistry()
{
	static TMap<FName, FDesignSourceConvertFunc> Registry;
	return Registry;
}
