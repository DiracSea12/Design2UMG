// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchemaFoundation.h"
#include "Types/SlateEnums.h"

/**
 * Mapping helpers from design-side semantics to UMG-side types and enums.
 */
namespace DesignTypeMapper
{
	/** Map a design node type to the target UMG widget type. */
	FString MapNodeTypeToWidget(EDesignNodeType Type, EDesignLayout Layout);

	/** fontWeight TypefaceFontName */
	FName MapFontWeight(const FString& Weight);

	/** Map alignItems to horizontal alignment for vertical layouts. */
	EHorizontalAlignment MapAlignItemsH(EDesignAlignItems Align);

	/** Map alignItems to vertical alignment for horizontal layouts. */
	EVerticalAlignment MapAlignItemsV(EDesignAlignItems Align);
}
