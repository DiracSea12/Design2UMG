// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchemaFoundation.h"

/**
 * Mapping helpers from design-side semantics to UMG-side types and enums.
 */
class DESIGN2UMGCORE_API FDesignTypeMapper
{
public:
	/** Map a design node type to the target UMG widget type. */
	static FString MapNodeTypeToWidget(EDesignNodeType Type, EDesignLayout Layout);

	/** fontWeight TypefaceFontName */
	static FName MapFontWeight(const FString& Weight);

	/** Map alignItems to horizontal alignment for vertical layouts. */
	static EHorizontalAlignment MapAlignItemsH(EDesignAlignItems Align);

	/** Map alignItems to vertical alignment for horizontal layouts. */
	static EVerticalAlignment MapAlignItemsV(EDesignAlignItems Align);
};



