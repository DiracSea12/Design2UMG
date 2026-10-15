// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Design2UMGAssetRegistryAccess.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/SoftObjectPath.h"

namespace Design2UMGAssetRegistryAccess
{
	FAssetData GetAssetByObjectPath(
		const IAssetRegistry& AssetRegistry,
		const FSoftObjectPath& ObjectPath,
		const bool bIncludeOnlyOnDiskAssets)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		return AssetRegistry.GetAssetByObjectPath(FName(*ObjectPath.ToString()), bIncludeOnlyOnDiskAssets);
#else
		return AssetRegistry.GetAssetByObjectPath(ObjectPath, bIncludeOnlyOnDiskAssets);
#endif
	}
}
