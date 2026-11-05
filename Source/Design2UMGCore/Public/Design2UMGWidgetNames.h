// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Runtime widget naming shared by conversion, which plans the names graph wiring looks up,
 * and the editor tree builder, which creates and resolves those names.
 */
namespace Design2UMGWidgetNames
{
	/** Valid widget object name for a node name; digit-leading names get a "W" prefix. */
	DESIGN2UMGCORE_API FName Sanitize(const FString& InName, const FString& FallbackPrefix);

	/**
	 * Rank of a tree-builder dedup name: base -> 0, base_<N> -> 2N-1, base_w<N> -> 2N;
	 * any other tail (overlay mirrors included) -> MAX_int32.
	 */
	DESIGN2UMGCORE_API int32 SuffixRank(const FString& WidgetName, const FString& BaseName);

	/** True when either node name's widget could answer a base-name lookup for the other. */
	DESIGN2UMGCORE_API bool Collide(const FString& NameA, const FString& NameB, const FString& FallbackPrefix);
}
