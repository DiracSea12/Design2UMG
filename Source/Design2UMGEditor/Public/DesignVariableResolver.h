// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

/**
 * 鍙橀噺瑙ｆ瀽鍣?- 瑙ｆ瀽 $variableName 鏍煎紡鐨勫彉閲忓紩鐢? */
class FDesignVariableResolver
{
public:
	FDesignVariableResolver() = default;
	explicit FDesignVariableResolver(const TMap<FString, FDesignVariable>& InVariables)
		: Variables(&InVariables) {}

	void SetVariables(const TMap<FString, FDesignVariable>& InVariables)
	{
		Variables = &InVariables;
	}

	/** 璁剧疆褰撳墠涓婚涓婁笅鏂囷紝鐢ㄤ簬閫夋嫨姝ｇ‘鐨勪富棰樺€?*/
	void SetThemeContext(const TMap<FString, FString>& InTheme)
	{
		ThemeContext = InTheme;
	}

	/** 瑙ｆ瀽瀛楃涓插€硷紝鏇挎崲鍙橀噺寮曠敤 */
	FString ResolveString(const FString& Value) const;

	/** 瑙ｆ瀽鏁板€硷紝鏀寔鍙橀噺寮曠敤 */
	float ResolveNumber(const FString& Value) const;

	/** 瑙ｆ瀽甯冨皵鍊硷紝鏀寔鍙橀噺寮曠敤 */
	bool ResolveBoolean(const FString& Value) const;

	/** 瑙ｆ瀽棰滆壊锛屾敮鎸佸彉閲忓紩鐢?*/
	FString ResolveColor(const FString& Value) const;

	/** 瑙ｆ瀽鍦嗚锛屾敮鎸佸崟鍊煎拰鍥涘€兼暟缁?*/
	FDesignCornerRadius ResolveCornerRadius(const TSharedPtr<FJsonValue>& Value) const;

	/** 妫€鏌ユ槸鍚︿负鍙橀噺寮曠敤 */
	static bool IsVariableRef(const FString& Value);

private:
	const TMap<FString, FDesignVariable>* Variables = nullptr;
	TMap<FString, FString> ThemeContext;

	/** 鑾峰彇鍙橀噺鍊?*/
	const FDesignVariable* GetVariable(const FString& VarName) const;

	/** 鏍规嵁褰撳墠涓婚涓婁笅鏂囦粠鍙橀噺鐨勪富棰樺€兼暟缁勪腑閫夋嫨姝ｇ‘鐨勫€?*/
	FString ResolveThemedValue(const FDesignVariable& Var) const;
};



