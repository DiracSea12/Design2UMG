// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

struct FAssetData;
struct FSoftObjectPath;
class IAssetRegistry;

namespace Design2UMGAssetRegistryAccess
{
	FAssetData GetAssetByObjectPath(
		const IAssetRegistry& AssetRegistry,
		const FSoftObjectPath& ObjectPath,
		bool bIncludeOnlyOnDiskAssets = false);
}
