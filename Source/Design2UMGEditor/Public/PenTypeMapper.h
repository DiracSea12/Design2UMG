// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

/**
 * .pen 属性到 UMG 属性的映射
 */
class FPenTypeMapper
{
public:
	/** 节点类型到 Widget 类型 */
	static FString MapNodeTypeToWidget(EPenNodeType Type, EPenLayout Layout);

	/** fontWeight 到 TypefaceFontName */
	static FName MapFontWeight(const FString& Weight);

	/** alignItems 到 HAlign (Vertical布局) */
	static EHorizontalAlignment MapAlignItemsH(EPenAlignItems Align);

	/** alignItems 到 VAlign (Horizontal布局) */
	static EVerticalAlignment MapAlignItemsV(EPenAlignItems Align);
};
