// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "UMGSchema.h"

/**
 * .pen 鑺傜偣鏍戝埌 UMG Widget 鏍戠殑杞崲鍣紙閬嶅巻+鍒嗗彂锛? * 甯冨眬鏄犲皠 鈫?FLayoutMapper
 * 瑙嗚灞炴€?鈫?FVisualMapper
 * 缁勪欢灞曞紑 鈫?FRefExpander
 */
class FDesign2UMGConverter
{
public:
	/** 涓诲叆鍙ｏ細杞崲鏁翠釜鏂囨。 */
	static FUMGWidgetData Convert(const FDesignDocument& Document);

private:
	/** 閫掑綊杞崲鑺傜偣 */
	static FUMGWidgetNode ConvertNode(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc, const TMap<FString, FString>& ThemeContext = TMap<FString, FString>(), TArray<FString>* OutWarnings = nullptr, EDesignLayout ParentLayout = EDesignLayout::None);

	/** 鍒涘缓瀹瑰櫒 Widget */
	static FUMGWidgetNode CreateContainerWidget(const TSharedPtr<FDesignNode>& Node);

	/** 鍒涘缓鏂囨湰 Widget */
	static FUMGWidgetNode CreateTextWidget(const TSharedPtr<FDesignNode>& Node);

	/** 鍒涘缓鍥炬爣瀛椾綋 Widget */
	static FUMGWidgetNode CreateIconFontWidget(const TSharedPtr<FDesignNode>& Node);

	/** 鍒涘缓 Path Widget锛堟爡鏍煎寲 SVG geometry 涓?Image锛?*/
	static FUMGWidgetNode CreatePathWidget(const TSharedPtr<FDesignNode>& Node);

	/** 搴旂敤鏂囨湰灞炴€?*/
	static void ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);

	/** 閫掑綊棰勮绠?FitContent 鑺傜偣鐨勫疄闄呭昂瀵革紙浠庡瓙鑺傜偣鎺ㄧ畻锛?*/
	static FVector2D PreComputeFitContentSizes(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc);
};



