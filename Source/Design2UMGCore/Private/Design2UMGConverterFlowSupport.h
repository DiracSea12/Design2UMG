// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

namespace Design2UMGConverterInternal
{
	class FExpandedNodeResolver
	{
	public:
		FExpandedNodeResolver(const FDesignDocument& InDocument, const TMap<FString, FString>& InThemeContext);

		TSharedPtr<FDesignNode> ExpandRef(const TSharedPtr<FDesignNode>& Node) const;
		TSharedPtr<FDesignNode> ResolveExpandedChild(const TSharedPtr<FDesignNode>& Child, bool bPreComputeFitContent) const;

	private:
		const FDesignDocument& Document;
		const TMap<FString, FString>& ThemeContext;
	};

	class FFlowChildPreparation
	{
	public:
		explicit FFlowChildPreparation(const FExpandedNodeResolver& InExpandedNodeResolver);

		void PreResolveFlowChildren(
			const TSharedPtr<FDesignNode>& Node,
			const FUMGWidgetNode& Widget,
			const TArray<TSharedPtr<FDesignNode>>& VisibleChildren) const;

	private:
		const FExpandedNodeResolver& ExpandedNodeResolver;
	};
}
