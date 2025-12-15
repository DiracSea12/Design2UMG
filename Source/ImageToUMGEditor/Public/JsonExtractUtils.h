// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * JSON 提取工具类
 * 用于从 AI 返回的文本中提取有效 JSON
 */
class FJsonExtractUtils
{
public:
	/**
	 * 从文本中提取 JSON
	 * @param Input AI 返回的原始文本
	 * @param OutJson 提取出的 JSON 字符串
	 * @param OutError 错误信息
	 * @return 是否成功提取
	 */
	static bool ExtractJson(const FString& Input, FString& OutJson, FString& OutError);

	/**
	 * 清理 JSON 字符串（移除BOM、统一换行、移除控制字符）
	 */
	static FString SanitizeJsonString(const FString& Input);

	/**
	 * 解析十六进制颜色 (#RGB 或 #RRGGBB)
	 * @return 是否成功解析
	 */
	static bool ParseHexColor(const FString& HexColor, uint8& OutR, uint8& OutG, uint8& OutB);

	/** 解析十六进制颜色为 FLinearColor */
	static FLinearColor ParseHexColorLinear(const FString& HexColor, FLinearColor Default = FLinearColor::White);

private:
	/** 从 markdown 代码块中提取内容 */
	static FString ExtractFromCodeBlock(const FString& Input);

	/** 使用括号匹配提取 JSON */
	static bool ExtractByBraceMatching(const FString& Input, FString& OutJson);
};
