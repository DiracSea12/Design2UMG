// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PenSchema.h"

/**
 * .pen 属性到 UMG 属性的映射
 */
class FPenTypeMapper
{
public:
	/** 解析颜色 #RGB/#RRGGBB/#RRGGBBAA -> FLinearColor */
	static FLinearColor ParseColor(const FString& HexColor);

	/** 节点类型到 Widget 类型 */
	static FString MapNodeTypeToWidget(EPenNodeType Type, EPenLayout Layout);

	/** fontWeight 到 TypefaceFontName */
	static FName MapFontWeight(const FString& Weight);

	/** textAlign 到 ETextJustify */
	static ETextJustify::Type MapTextAlign(EPenTextAlign Align);

	/** alignItems 到 HAlign (Vertical布局) */
	static EHorizontalAlignment MapAlignItemsH(EPenAlignItems Align);

	/** alignItems 到 VAlign (Horizontal布局) */
	static EVerticalAlignment MapAlignItemsV(EPenAlignItems Align);
};
