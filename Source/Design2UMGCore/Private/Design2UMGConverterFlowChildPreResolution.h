// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

namespace Design2UMGConverterInternal
{
	struct FRefExpansionCache;

	class FFlowChildPreResolutionResolver
	{
	public:
		FFlowChildPreResolutionResolver(
			const FDesignDocument& InDocument,
			const TMap<FString, FString>& InThemeContext,
			TSharedPtr<FRefExpansionCache> InExpansionCache = nullptr);

		TSharedPtr<FDesignNode> ExpandRef(const TSharedPtr<FDesignNode>& Node) const;
		TSharedPtr<FDesignNode> ResolveExpandedChild(const TSharedPtr<FDesignNode>& Child, bool bPreComputeFitContent) const;
		void MarkFitContentPrecomputed(const TSharedPtr<FDesignNode>& RefNode) const;
		/** Return a mutable stage copy while keeping the cached expansion snapshot untouched. */
		TSharedPtr<FDesignNode> MakeWorkingCopy(const TSharedPtr<FDesignNode>& RefNode) const;

	private:
		const FDesignDocument& Document;
		const TMap<FString, FString>& ThemeContext;
		TSharedPtr<FRefExpansionCache> ExpansionCache;
	};

	class FFlowChildPreResolution
	{
	public:
		explicit FFlowChildPreResolution(const FFlowChildPreResolutionResolver& InPreResolutionResolver);

		void PreResolveFlowChildren(
			const TSharedPtr<FDesignNode>& Node,
			const FUMGWidgetNode& Widget,
			const TArray<TSharedPtr<FDesignNode>>& VisibleChildren) const;

	private:
		const FFlowChildPreResolutionResolver& PreResolutionResolver;
	};
}
