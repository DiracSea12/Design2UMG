// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonExtractUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString FJsonExtractUtils::SanitizeJsonString(const FString& Input)
{
	FString Result = Input;

	// 移除 BOM
	if (Result.Len() > 0 && Result[0] == 0xFEFF)
	{
		Result.RemoveAt(0);
	}

	// 统一换行符
	Result.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	Result.ReplaceInline(TEXT("\r"), TEXT("\n"));

	// 移除控制字符
	FString Cleaned;
	Cleaned.Reserve(Result.Len());
	for (TCHAR Ch : Result)
	{
		if (Ch >= 32 || Ch == TEXT('\n') || Ch == TEXT('\t'))
		{
			Cleaned.AppendChar(Ch);
		}
	}

	return Cleaned.TrimStartAndEnd();
}

bool FJsonExtractUtils::ParseHexColor(const FString& HexColor, uint8& OutR, uint8& OutG, uint8& OutB)
{
	FString Hex = HexColor;
	Hex.RemoveFromStart(TEXT("#"));

	// #RGB -> #RRGGBB
	if (Hex.Len() == 3)
	{
		Hex = FString::Printf(TEXT("%c%c%c%c%c%c"),
			Hex[0], Hex[0], Hex[1], Hex[1], Hex[2], Hex[2]);
	}

	if (Hex.Len() != 6)
	{
		return false;
	}

	OutR = FParse::HexDigit(Hex[0]) * 16 + FParse::HexDigit(Hex[1]);
	OutG = FParse::HexDigit(Hex[2]) * 16 + FParse::HexDigit(Hex[3]);
	OutB = FParse::HexDigit(Hex[4]) * 16 + FParse::HexDigit(Hex[5]);
	return true;
}

FLinearColor FJsonExtractUtils::ParseHexColorLinear(const FString& HexColor, FLinearColor Default)
{
	uint8 R, G, B;
	if (!ParseHexColor(HexColor, R, G, B))
	{
		return Default;
	}
	return FLinearColor(R / 255.f, G / 255.f, B / 255.f, 1.0f);
}

FString FJsonExtractUtils::ExtractFromCodeBlock(const FString& Input)
{
	int32 CodeBlockStart = Input.Find(TEXT("```"));
	if (CodeBlockStart == INDEX_NONE)
	{
		return Input;
	}

	int32 ContentStart = Input.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromStart, CodeBlockStart);
	int32 ContentEnd = Input.Find(TEXT("```"), ESearchCase::IgnoreCase, ESearchDir::FromStart, ContentStart + 1);

	if (ContentStart != INDEX_NONE && ContentEnd != INDEX_NONE)
	{
		return Input.Mid(ContentStart + 1, ContentEnd - ContentStart - 1).TrimStartAndEnd();
	}

	return Input;
}

bool FJsonExtractUtils::ExtractByBraceMatching(const FString& Input, FString& OutJson)
{
	int32 JsonStart = Input.Find(TEXT("{"));
	if (JsonStart == INDEX_NONE)
	{
		return false;
	}

	int32 BraceCount = 0;
	int32 JsonEnd = INDEX_NONE;
	bool bInString = false;

	for (int32 i = JsonStart; i < Input.Len(); ++i)
	{
		TCHAR Ch = Input[i];

		// 跳过转义字符
		if (Ch == TEXT('\\') && i + 1 < Input.Len())
		{
			++i;
			continue;
		}

		// 字符串状态切换
		if (Ch == TEXT('"'))
		{
			bInString = !bInString;
			continue;
		}

		// 只在非字符串状态下计数括号
		if (!bInString)
		{
			if (Ch == TEXT('{'))
			{
				++BraceCount;
			}
			else if (Ch == TEXT('}'))
			{
				--BraceCount;
				if (BraceCount == 0)
				{
					JsonEnd = i;
					break;
				}
			}
		}
	}

	if (JsonEnd != INDEX_NONE)
	{
		OutJson = Input.Mid(JsonStart, JsonEnd - JsonStart + 1);
		return true;
	}

	return false;
}

bool FJsonExtractUtils::ExtractJson(const FString& Input, FString& OutJson, FString& OutError)
{
	// 步骤1：清理字符串
	FString CleanedJson = SanitizeJsonString(Input);

	// 步骤2：移除 markdown 代码块
	CleanedJson = ExtractFromCodeBlock(CleanedJson);

	// 步骤3：快速路径 - 直接解析
	{
		TSharedPtr<FJsonObject> TestParse;
		TSharedRef<TJsonReader<>> TestReader = TJsonReaderFactory<>::Create(CleanedJson);
		if (FJsonSerializer::Deserialize(TestReader, TestParse) && TestParse.IsValid())
		{
			OutJson = CleanedJson;
			return true;
		}
	}

	// 步骤4：回退 - 括号匹配提取
	if (ExtractByBraceMatching(CleanedJson, OutJson))
	{
		TSharedPtr<FJsonObject> ValidateParse;
		TSharedRef<TJsonReader<>> ValidateReader = TJsonReaderFactory<>::Create(OutJson);
		if (FJsonSerializer::Deserialize(ValidateReader, ValidateParse) && ValidateParse.IsValid())
		{
			return true;
		}
	}

	OutError = TEXT("Could not extract valid JSON from response");
	return false;
}
