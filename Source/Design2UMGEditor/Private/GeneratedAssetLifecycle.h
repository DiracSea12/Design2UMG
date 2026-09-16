// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once
#include "Containers/UnrealString.h"

class UObject;

namespace GeneratedAssetLifecycle
{
	/** Existing uassets allow a read-only check; only sidecars allow an append-open probe. */
	bool IsFileWritableForSave(const FString& Path, bool bAllowWriteProbe, FString& OutReason);
	void DiscardUnsavedAsset(UObject* Asset);
	void RegisterSavedAsset(UObject* Asset);
}
