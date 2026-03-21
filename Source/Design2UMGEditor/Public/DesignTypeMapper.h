// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

/**
 * .pen 属性到 UMG 属性的映射
 */
class FDesignTypeMapper
{
public:
	/** 节点类型到Widget 类型 */
	static FString MapNodeTypeToWidget(EDesignNodeType Type, EDesignLayout Layout);

	/** fontWeight TypefaceFontName */
	static FName MapFontWeight(const FString& Weight);

	/** alignItems HAlign (Vertical布局) */
	static EHorizontalAlignment MapAlignItemsH(EDesignAlignItems Align);

	/** alignItems VAlign (Horizontal布局) */
	static EVerticalAlignment MapAlignItemsV(EDesignAlignItems Align);
};



