// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGComponentMappingGeneration.h"

class UWidgetBlueprint;

/**
 * Canonical values for sidecar instance kinds and the "sidecar write failed" diagnostic kind.
 * The write side (GenerationManifest serialization / UMGGenerator diagnostics) and the read side (reimport
 * reconciliation and save-outcome decisions) share this one set of constants: these strings are a cross-file
 * protocol - changing a literal on either end would silently break reconciliation (e.g. reimport reporting a failed sidecar write as success).
 */
namespace Design2UMGManifestKinds
{
	constexpr const TCHAR* Mapped = TEXT("mapped");
	constexpr const TCHAR* Generated = TEXT("generated");
	constexpr const TCHAR* ManifestWriteFailedDiagnostic = TEXT("manifest_write_failed");
}


/**
 * Minimal generation record (sidecar manifest) payload:
 * schemaVersion/provider/documentScope/sourceRoot/instances(kind=mapped|generated)/d2uOwned。
 * assembled by BuildWidgetBlueprint and atomically written to <AssetName>.d2u.json after a successful import/reimport asset save.
 * Property values never enter the manifest: reimport updates properties by "re-convert by designNodeId, then diff";
 * the inputs record keeps declarations only (source/target/valueKind), not design values.
 */
struct FDesign2UMGGenerationManifestPayload
{
	bool bWholeAssetOwned = false;
	bool bComponentBatchRoot = false;
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
	bool bComponentBatchRoot = false;
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

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * Test injection: once armed, the next WriteSidecarForAsset fails exactly once and touches no file.
	 * Used to verify deterministically the reimport propagation path "uasset saved but manifest not updated"
	 * (a genuinely read-only sidecar is caught by reimport's pre-save check, so that branch is unreachable otherwise).
	 */
	void ArmAutomationSidecarWriteFailureOnce();
#endif

	/**
	 * Called after a successful import save: atomically writes the manifest next to the asset (temp file, then replace);
	 * on failure no half-written file remains. Returns whether the write succeeded.
	 */
	bool WriteSidecarForAsset(
		const FString& AssetPackageName,
		const FDesign2UMGGenerationManifestPayload& Payload);

	/**
	 * Reads and parses the sidecar next to the asset before reimport. Missing file / invalid JSON / unrecognized
	 * schema returns false with a readable reason (goes into conflict diagnostics). Historical value fields inside inputs are always ignored.
	 */
	bool TryLoadSidecarForAsset(
		const FString& AssetPackageName,
		FDesign2UMGGenerationManifestSidecar& OutSidecar,
		FString& OutError);
}
