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
	bool bAccepted = true;
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

	/** 显示 Diff 预览对话框，用户可逐项勾选。返回 true = 用户确认，false = 取消 */
	static bool ShowDiffPreviewDialog(FDiffResult& InOutDiff);

	/** 根据用户选择过滤新节点树，移除被拒绝的节点 */
	static FUMGWidgetNode FilterByAccepted(const FUMGWidgetNode& Root, const FDiffResult& Diff);
};
