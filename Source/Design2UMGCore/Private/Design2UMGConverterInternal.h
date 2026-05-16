// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGLog.h"
#include "Design2UMGPrototypeNavigation.h"
#include "Design2UMGSettings.h"
#include "Design2UMGTextWidgetBuilder.h"
#include "DesignSchema.h"
#include "UMGSchema.h"
#include "VisualMapper.h"

namespace Design2UMGConverterInternal
{
	constexpr int32 MaxTreeRecursionDepth = 512;

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

	struct FRecursionGuard
	{
		explicit FRecursionGuard(int32& InDepth, const TCHAR* InName = TEXT(""))
			: Depth(InDepth)
			, bValid(InDepth < MaxTreeRecursionDepth)
		{
			if (bValid)
			{
				++Depth;
			}
			else
			{
				UE_LOG(LogDesign2UMG, Verbose, TEXT("%s: recursion depth exceeded (%d)"), InName, MaxTreeRecursionDepth);
				UE_LOG(LogDesign2UMG, Error, TEXT("Design tree nesting is too deep (over %d levels). A cyclic reference may exist."), MaxTreeRecursionDepth);
			}
		}

		~FRecursionGuard()
		{
			if (bValid)
			{
				--Depth;
			}
		}

		int32& Depth;
		bool bValid = false;
	};

	// Encapsulates the complete render-fallback widget creation path.
	// This separates the fallback branch decision from the normal widget creation path,
	// reducing mixed ownership in CreateBaseWidgetForNode.
	FUMGWidgetNode CreateFallbackBranchWidget(const TSharedPtr<FDesignNode>& Node);
}
