// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "UMGSchema.h"

/**
 * Converts a design node tree into a UMG widget tree.
 * Layout mapping is delegated to FLayoutMapper.
 * Visual mapping is delegated to FVisualMapper.
 * Ref expansion is delegated to FRefExpander.
 */
class FDesign2UMGConverter
{
public:
	/** Convert an entire design document. */
	static FUMGWidgetData Convert(const FDesignDocument& Document);

private:
	/** Convert a single node recursively. */
	static FUMGWidgetNode ConvertNode(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc, const TMap<FString, FString>& ThemeContext = TMap<FString, FString>(), TArray<FString>* OutWarnings = nullptr, EDesignLayout ParentLayout = EDesignLayout::None);

	/** Create a container widget. */
	static FUMGWidgetNode CreateContainerWidget(const TSharedPtr<FDesignNode>& Node);

	/** Create a text widget. */
	static FUMGWidgetNode CreateTextWidget(const TSharedPtr<FDesignNode>& Node);

	/** Create an icon-font widget. */
	static FUMGWidgetNode CreateIconFontWidget(const TSharedPtr<FDesignNode>& Node);

	/** Create a path widget, rasterizing SVG geometry into an image when needed. */
	static FUMGWidgetNode CreatePathWidget(const TSharedPtr<FDesignNode>& Node);

	/** Apply text-specific properties. */
	static void ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);

	/** Recursively pre-compute fit-content sizes from child content. */
	static FVector2D PreComputeFitContentSizes(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc);
	static TSharedPtr<FDesignNode> ResolveExpandedChildForConversion(const TSharedPtr<FDesignNode>& Child, const FDesignDocument& Doc, const TMap<FString, FString>& ThemeContext, bool bPreComputeFitContent);
	static void PreResolveFlowChildren(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc, const FUMGWidgetNode& Widget, const TArray<TSharedPtr<FDesignNode>>& VisibleChildren, const TMap<FString, FString>& ThemeContext);
	static bool ShouldWrapChildWithSizeBox(const TSharedPtr<FDesignNode>& ParentNode, const TSharedPtr<FDesignNode>& ChildNode, const FUMGWidgetNode& ChildWidget);
	static void AppendConvertedChildren(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc, const TArray<TSharedPtr<FDesignNode>>& VisibleChildren, const TMap<FString, FString>& ActiveTheme, TArray<FString>* OutWarnings);
};
