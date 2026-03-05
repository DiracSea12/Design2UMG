// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

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
	/** 璁＄畻鏂拌妭鐐规爲涓庡凡鏈?Widget 涔嬮棿鐨?diff */
	static FDiffResult ComputeDiff(
		const FUMGWidgetNode& NewRoot,
		UWidgetBlueprint* ExistingBP);

	/** 鏄剧ず Diff 棰勮瀵硅瘽妗嗭紝鐢ㄦ埛鍙€愰」鍕鹃€夈€傝繑鍥?true = 鐢ㄦ埛纭锛宖alse = 鍙栨秷 */
	static bool ShowDiffPreviewDialog(FDiffResult& InOutDiff);

	/** 鏍规嵁鐢ㄦ埛閫夋嫨杩囨护鏂拌妭鐐规爲锛岀Щ闄よ鎷掔粷鐨勮妭鐐?*/
	static FUMGWidgetNode FilterByAccepted(const FUMGWidgetNode& Root, const FDiffResult& Diff);
};

