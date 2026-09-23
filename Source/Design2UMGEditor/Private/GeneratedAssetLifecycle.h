// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once
#include "Containers/UnrealString.h"
#include "Misc/Guid.h"

class UObject;

namespace GeneratedAssetLifecycle
{
	/** Undo only the latest completed transaction with this identity; never a user's later edit. */
	bool UndoOwnedTransaction(const FGuid& TransactionId);
	/** Existing uassets allow a read-only check; only sidecars allow an append-open probe. */
	bool IsFileWritableForSave(const FString& Path, bool bAllowWriteProbe, FString& OutReason);
	bool DiscardUnsavedAsset(UObject* Asset);
	void RegisterSavedAsset(UObject* Asset);
}
