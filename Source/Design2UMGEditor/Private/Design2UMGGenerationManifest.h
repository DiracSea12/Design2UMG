// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGCommitResult.h"
#include "Design2UMGComponentMappingGeneration.h"

class UWidgetBlueprint;

/** Canonical sidecar instance kinds and diagnostic names. Save outcomes use the typed result, not these strings. */
namespace Design2UMGManifestKinds
{
	constexpr const TCHAR* Mapped = TEXT("mapped");
	constexpr const TCHAR* Generated = TEXT("generated");
	constexpr const TCHAR* ManifestWriteFailedDiagnostic = TEXT("manifest_write_failed");
}

/**
 * Records source identity, generated instances, ownership and input declarations.
 * Written to <AssetName>.d2u.json after a successful asset save.
 * Design property values are read from the source during reimport, not stored here.
 */
struct FDesign2UMGGenerationManifestPayload
{
	bool bWholeAssetOwned = false;
	bool bComponentBulkRoot = false;
	FString Provider;
	FString DocumentScope;
	FString SourceRootNodeId;
	FString SourceRootName;
	TArray<FDesign2UMGMappedInstanceRecord> MappedInstances;
	TArray<FDesign2UMGGeneratedInstanceRecord> TopLevelGenerated;
	/** Widget names created entirely by D2U (including nested leaves); reimport uses this for delete/move ownership decisions. */
	TArray<FString> D2UOwnedNames;
};

/** Generation record deserialized from the sidecar (input of the reimport pipeline). */
struct FDesign2UMGGenerationManifestSidecar
{
	FString GeneratedAssetHash;
	bool bComponentBulkRoot = false;
	int32 SchemaVersion = 0;
	FString Provider;
	FString DocumentScope;
	FString SourceRootNodeId;
	FString SourceRootName;

	/** Single instance record: kind = "mapped" | "generated". */
	struct FInstanceRecord
	{
		FString DesignNodeId;
		FString WidgetName;
		FString Kind;
		FString MappedKey;
		FString MappedClass;
		bool bMatchedAtSetLevel = false;
		TArray<FDesign2UMGMappedInputRecord> Inputs;
	};

	TArray<FInstanceRecord> Instances;
	TArray<FString> D2UOwned;

	const FInstanceRecord* FindInstanceByDesignNodeId(const FString& DesignNodeId) const
	{
		for (const FInstanceRecord& Record : Instances)
		{
			if (Record.DesignNodeId == DesignNodeId)
			{
				return &Record;
			}
		}
		return nullptr;
	}
};

namespace Design2UMGGenerationManifest
{
	FString ComputeSavedAssetHash(const FString& AssetPackageName);
	/** Manifest sidecar file name suffix (<AssetName>.d2u.json). */
	FString BuildSidecarPathForAsset(const FString& AssetPackageName);

	/**
	 * Write via a temp file, then replace the sidecar. Failures before replacement leave it Unchanged;
	 * a failed replacement is Indeterminate because the file manager may already have removed the old file.
	 */
	EDesign2UMGFileState WriteSidecarForAsset(
		const FString& AssetPackageName,
		const FDesign2UMGGenerationManifestPayload& Payload);

	/**
	 * Load the asset sidecar. Missing files, invalid JSON and unknown schemas return a reason.
	 * Input declarations exclude stored design values.
	 */
	bool TryLoadSidecarForAsset(
		const FString& AssetPackageName,
		FDesign2UMGGenerationManifestSidecar& OutSidecar,
		FString& OutError);
}
