// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

class UObject;

namespace GeneratedAssetLifecycle
{
	void DiscardUnsavedAsset(UObject* Asset);
	void RegisterSavedAsset(UObject* Asset);
}
