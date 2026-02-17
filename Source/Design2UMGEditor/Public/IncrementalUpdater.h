// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UWidget;
class UWidgetBlueprint;

enum class EDiffAction : uint8
{
	Added,
	Modified,
	Removed
};

struct FDiffEntry
{
	EDiffAction Action;
	FString NodeId;
	FString NodeName;
	FString NodeType;
};

struct FDiffResult
{
	TArray<FDiffEntry> Entries;

	int32 CountAdded() const;
	int32 CountModified() const;
	int32 CountRemoved() const;
	bool HasChanges() const { return Entries.Num() > 0; }
};

class FIncrementalUpdater
{
public:
	/** 计算新节点树与已有 Widget 之间的 diff */
	static FDiffResult ComputeDiff(
		const FUMGWidgetNode& NewRoot,
		UWidgetBlueprint* ExistingBP);
};
