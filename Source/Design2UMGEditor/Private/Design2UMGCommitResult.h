// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once
#include "CoreMinimal.h"

// Module-internal outcomes. Diagnostics explain the cause; they never own commit state.
enum class EDesign2UMGCommitStatus : uint8
{
	Succeeded,
	SucceededWithDegradation,
	NotCommitted,
	CommittedWithMetadataFailure,
	RollbackIncomplete,
	Conflict,
};

enum class EDesign2UMGFileState : uint8
{
	Unchanged,
	Saved,
	Restored,
	// A write or recovery failed: the previous contents are not guaranteed.
	Indeterminate,
};

enum class EDesign2UMGMemoryState : uint8
{
	Unchanged,
	Modified,
	Restored,
	RecoveryFailed,
};

/** State at return, not a history of attempted writes.
 * Bulk file states cover all assets/sidecars in that batch; Indeterminate includes
 * mixed recovery. Modified means changes remain without a successful rollback.
 * An unchanged manifest can also mean no manifest was requested (asset-only save).
 */
struct FDesign2UMGCommitResult
{
	EDesign2UMGCommitStatus Status = EDesign2UMGCommitStatus::NotCommitted;
	EDesign2UMGFileState Asset = EDesign2UMGFileState::Unchanged;
	EDesign2UMGFileState Manifest = EDesign2UMGFileState::Unchanged;
	EDesign2UMGMemoryState Memory = EDesign2UMGMemoryState::Unchanged;
	/** Target asset path; preserved even when generation returns no blueprint. */
	FString AssetPath;

	bool IsSuccess() const
	{
		return Status == EDesign2UMGCommitStatus::Succeeded
			|| Status == EDesign2UMGCommitStatus::SucceededWithDegradation;
	}
};
