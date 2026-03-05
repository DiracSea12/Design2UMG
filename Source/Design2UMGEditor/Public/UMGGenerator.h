// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UWidgetBlueprint;

/**
 * UMG 鐢熸垚鍣?- 灏嗚В鏋愬悗鐨勬暟鎹粨鏋勮浆鎹负 Widget Blueprint
 */
class FUMGGenerator
{
public:
	/**
	 * 鐢熸垚 Widget Blueprint锛堟瀯寤?+ 缂栬瘧 + 淇濆瓨锛?
	 * @param WidgetData 瑙ｆ瀽鍚庣殑 Widget 鏁版嵁
	 * @param PackagePath 淇濆瓨璺緞 (渚嬪 "/Game/UI")
	 * @return 鐢熸垚鐨?Widget Blueprint锛屽け璐ヨ繑鍥?nullptr
	 */
	static UWidgetBlueprint* GenerateWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath);

	/** 鏋勫缓 + 缂栬瘧 Widget Blueprint锛屼絾涓嶄繚瀛樺埌纾佺洏銆倀ransient 绾圭悊姝ゆ椂浠嶅湪鍐呭瓨涓€?*/
	static UWidgetBlueprint* BuildWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath);

	/** 灏嗗凡缂栬瘧鐨?Widget Blueprint 淇濆瓨鍒扮鐩樸€?*/
	static void SaveWidgetBlueprint(UWidgetBlueprint* WBP);
};

