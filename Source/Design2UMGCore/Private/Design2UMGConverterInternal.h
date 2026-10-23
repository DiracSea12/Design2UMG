// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignReferenceExpansion.h"
#include "FontResolver.h"
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
	 * Cache reference expansions for one conversion and theme context.
	 * Source-pointer keys remain valid only while the owning document and working copies
	 * stay alive and unchanged.
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
		FDesignReferenceExpansionResult Result;
	};

	struct FRefExpansionCache
	{
		explicit FRefExpansionCache(
			const FDesignDocument& Document,
			TArray<FString>* InWarnings = nullptr,
			FString* InFatalError = nullptr)
			: Warnings(InWarnings)
			, FatalError(InFatalError)
		{
			Document.BuildReferenceNodeIndex(ReferenceTargetsById, InWarnings);
		}

		TMap<FRefExpansionCacheKey, FRefExpansionCacheEntry> Entries;
		TMap<FString, TSharedPtr<FDesignNode>> ReferenceTargetsById;
		TMap<FString, int32> WarningIndices;
		TArray<FString>* Warnings = nullptr;
		FString* FatalError = nullptr;
		// Working copies retain aliases to their immutable snapshot nodes so nested
		// refs can reuse the pointer-keyed cache without merging distinct instances.
		TMap<const FDesignNode*, const FDesignNode*> SnapshotAliases;
	};

	struct FConversionSettingsSnapshot
	{
		FDesign2UMGFontMappings FontMappings;
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
			, RefExpansionCache(MakeShared<FRefExpansionCache>(InDocument, &InWarnings, &ConversionError))
		{
		}

		const FDesignDocument& Document;
		TArray<FString>& Warnings;
		const FConversionSettingsSnapshot& SettingsSnapshot;
		FString ConversionError;
		TSharedPtr<FRefExpansionCache> RefExpansionCache;
		TSet<FString> WarnedFamilies;
		TSet<FString> PrunedInvalidMappings;
		TSet<FString> PrototypeTriggerNodeIds;
		TMap<FString, FString> PendingIconFontMappings;
		FUMGPrototypeNavigationData PrototypeNavigation;
		int32 RecursionDepth = 0;
		int32 ScriptGeneratedNodeCount = 0;
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
			Next.InstancePath = InstancePath;
			if (Node.IsValid())
			{
				Next.InstancePath += FString::Printf(TEXT("%d:%s;"), Node->Id.Len(), *Node->Id);
				for (const auto& Pair : Node->Theme)
				{
					Next.ThemeContext.Add(Pair.Key, Pair.Value);
				}
			}
			return Next;
		}

		FConversionState& State;
		TMap<FString, FString> ThemeContext;
		FString InstancePath;
	};

	// Shared guard implementation lives in DesignTreeLimits.h.
	using FRecursionGuard = FDesignRecursionGuard;

	// Encapsulates the complete render-fallback widget creation path.
	// This separates the fallback branch decision from the normal widget creation path,
	// reducing mixed ownership in CreateBaseWidgetForNode.
	FUMGWidgetNode CreateFallbackBranchWidget(const TSharedPtr<FDesignNode>& Node);
}
