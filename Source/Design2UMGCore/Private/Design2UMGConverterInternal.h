// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Misc/Crc.h"
#include "DesignTreeLimits.h"
#include "Design2UMGLog.h"
#include "Design2UMGPrototypeNavigation.h"
#include "Design2UMGSettings.h"
#include "Design2UMGTextWidgetBuilder.h"
#include "DesignSchema.h"
#include "UMGSchema.h"
#include "VisualMapper.h"

namespace Design2UMGConverterInternal
{
	/**
	 * Per-conversion reference expansion storage.  Keeping this on conversion
	 * state (rather than in RefExpander) makes expanded nodes reusable only for
	 * the current conversion and keeps theme-specific results isolated.
	 */
	struct FRefExpansionCacheKey
	{
		const FDesignNode* Node = nullptr;
		FString ThemeSignature;

		friend bool operator==(const FRefExpansionCacheKey& A, const FRefExpansionCacheKey& B)
		{
			return A.Node == B.Node && A.ThemeSignature == B.ThemeSignature;
		}
	};

	inline uint32 GetTypeHash(const FRefExpansionCacheKey& Key)
	{
		return HashCombine(::GetTypeHash(reinterpret_cast<UPTRINT>(Key.Node)), FCrc::StrCrc32(*Key.ThemeSignature));
	}

	struct FRefExpansionCacheEntry
	{
		TSharedPtr<FDesignNode> Expanded;
		bool bResolved = false;
		bool bFitContentPrecomputed = false;
	};

	struct FRefExpansionCache
	{
		TMap<FRefExpansionCacheKey, FRefExpansionCacheEntry> Entries;
		// Working copies retain aliases to their immutable snapshot nodes so nested
		// refs can reuse the pointer-keyed cache without merging distinct instances.
		TMap<const FDesignNode*, const FDesignNode*> SnapshotAliases;
	};

	struct FConversionSettingsSnapshot
	{
		UDesign2UMGSettings::FResolvedPlaceholderHatchStyle PlaceholderHatchStyle;
		FVisualMapperOptions VisualMapperOptions;
		Design2UMGTextWidgetBuilder::FIconFontSettingsSnapshot IconFont;
	};

	struct FConversionState
	{
		explicit FConversionState(
			const FDesignDocument& InDocument,
			TArray<FString>& InWarnings,
			const FConversionSettingsSnapshot& InSettingsSnapshot)
			: Document(InDocument)
			, Warnings(InWarnings)
			, SettingsSnapshot(InSettingsSnapshot)
		{
		}

		const FDesignDocument& Document;
		TArray<FString>& Warnings;
		const FConversionSettingsSnapshot& SettingsSnapshot;
		TSharedPtr<FRefExpansionCache> RefExpansionCache = MakeShared<FRefExpansionCache>();
		TSet<FString> WarnedFamilies;
		TSet<FString> PrunedInvalidMappings;
		TSet<FString> PrototypeTriggerNodeIds;
		TMap<FString, FString> PendingIconFontMappings;
		FUMGPrototypeNavigationData PrototypeNavigation;
		int32 RecursionDepth = 0;
	};

	struct FConversionContext
	{
		explicit FConversionContext(FConversionState& InState)
			: State(InState)
		{
		}

		FConversionContext WithNodeTheme(const TSharedPtr<FDesignNode>& Node) const
		{
			FConversionContext Next(State);
			Next.ThemeContext = ThemeContext;
			if (Node.IsValid())
			{
				for (const auto& Pair : Node->Theme)
				{
					Next.ThemeContext.Add(Pair.Key, Pair.Value);
				}
			}
			return Next;
		}

		FConversionState& State;
		TMap<FString, FString> ThemeContext;
	};

	// Shared guard implementation lives in DesignTreeLimits.h.
	using FRecursionGuard = FDesignRecursionGuard;

	// Encapsulates the complete render-fallback widget creation path.
	// This separates the fallback branch decision from the normal widget creation path,
	// reducing mixed ownership in CreateBaseWidgetForNode.
	FUMGWidgetNode CreateFallbackBranchWidget(const TSharedPtr<FDesignNode>& Node);
}
