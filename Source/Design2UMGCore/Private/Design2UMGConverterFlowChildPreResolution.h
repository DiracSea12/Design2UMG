// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

namespace Design2UMGConverterInternal
{
	class FFlowChildPreResolutionResolver
	{
	public:
		FFlowChildPreResolutionResolver(const FDesignDocument& InDocument, const TMap<FString, FString>& InThemeContext);

		TSharedPtr<FDesignNode> ExpandRef(const TSharedPtr<FDesignNode>& Node) const;
		TSharedPtr<FDesignNode> ResolveExpandedChild(const TSharedPtr<FDesignNode>& Child, bool bPreComputeFitContent) const;

	private:
		const FDesignDocument& Document;
		const TMap<FString, FString>& ThemeContext;
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
