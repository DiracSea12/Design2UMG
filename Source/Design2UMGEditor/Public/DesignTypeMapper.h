// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

/**
 * .pen 灞炴€у埌 UMG 灞炴€х殑鏄犲皠
 */
class FDesignTypeMapper
{
public:
	/** 鑺傜偣绫诲瀷鍒?Widget 绫诲瀷 */
	static FString MapNodeTypeToWidget(EDesignNodeType Type, EDesignLayout Layout);

	/** fontWeight 鍒?TypefaceFontName */
	static FName MapFontWeight(const FString& Weight);

	/** alignItems 鍒?HAlign (Vertical甯冨眬) */
	static EHorizontalAlignment MapAlignItemsH(EDesignAlignItems Align);

	/** alignItems 鍒?VAlign (Horizontal甯冨眬) */
	static EVerticalAlignment MapAlignItemsV(EDesignAlignItems Align);
};



