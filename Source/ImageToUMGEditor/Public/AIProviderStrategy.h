// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * AI 提供商策略接口
 * 每个 AI 提供商实现此接口，封装其特定的 API 格式
 */
class IAIProviderStrategy
{
public:
	virtual ~IAIProviderStrategy() = default;

	/** 获取 API 端点后缀（如 /chat/completions, /messages, /responses） */
	virtual FString GetEndpointSuffix() const = 0;

	/** 获取请求头 */
	virtual TMap<FString, FString> GetHeaders(const FString& APIKey) const = 0;

	/** 构建请求体 */
	virtual TSharedPtr<FJsonObject> BuildRequestBody(
		const FString& Base64Image,
		const FString& MimeType,
		const FString& Prompt,
		int32 MaxTokens,
		const FString& ModelName) const = 0;

	/** 从响应中提取内容文本 */
	virtual bool ExtractContent(
		const TSharedPtr<FJsonObject>& Response,
		FString& OutContent) const = 0;

	/** 获取提供商显示名称 */
	virtual FString GetDisplayName() const = 0;
};

// ============================================================================
// 具体策略实现
// ============================================================================

/** OpenAI 兼容格式（OpenAI, DeepSeek, Groq, Together, OpenRouter, Mistral） */
class FOpenAICompatibleStrategy : public IAIProviderStrategy
{
public:
	virtual FString GetEndpointSuffix() const override { return TEXT("/chat/completions"); }
	virtual TMap<FString, FString> GetHeaders(const FString& APIKey) const override;
	virtual TSharedPtr<FJsonObject> BuildRequestBody(const FString& Base64Image, const FString& MimeType, const FString& Prompt, int32 MaxTokens, const FString& ModelName) const override;
	virtual bool ExtractContent(const TSharedPtr<FJsonObject>& Response, FString& OutContent) const override;
	virtual FString GetDisplayName() const override { return TEXT("OpenAI Compatible"); }
};

/** Anthropic Claude */
class FAnthropicStrategy : public IAIProviderStrategy
{
public:
	virtual FString GetEndpointSuffix() const override { return TEXT("/messages"); }
	virtual TMap<FString, FString> GetHeaders(const FString& APIKey) const override;
	virtual TSharedPtr<FJsonObject> BuildRequestBody(const FString& Base64Image, const FString& MimeType, const FString& Prompt, int32 MaxTokens, const FString& ModelName) const override;
	virtual bool ExtractContent(const TSharedPtr<FJsonObject>& Response, FString& OutContent) const override;
	virtual FString GetDisplayName() const override { return TEXT("Anthropic"); }
};

/** Google Gemini */
class FGoogleStrategy : public IAIProviderStrategy
{
public:
	virtual FString GetEndpointSuffix() const override { return TEXT(""); } // Google 使用完整 URL
	virtual TMap<FString, FString> GetHeaders(const FString& APIKey) const override;
	virtual TSharedPtr<FJsonObject> BuildRequestBody(const FString& Base64Image, const FString& MimeType, const FString& Prompt, int32 MaxTokens, const FString& ModelName) const override;
	virtual bool ExtractContent(const TSharedPtr<FJsonObject>& Response, FString& OutContent) const override;
	virtual FString GetDisplayName() const override { return TEXT("Google AI"); }

	/** Google 需要特殊的 URL 构建 */
	FString BuildFullURL(const FString& Endpoint, const FString& ModelName, const FString& APIKey) const;
};

/** 火山引擎 Doubao（使用 /responses 端点） */
class FVolcengineStrategy : public IAIProviderStrategy
{
public:
	virtual FString GetEndpointSuffix() const override { return TEXT("/responses"); }
	virtual TMap<FString, FString> GetHeaders(const FString& APIKey) const override;
	virtual TSharedPtr<FJsonObject> BuildRequestBody(const FString& Base64Image, const FString& MimeType, const FString& Prompt, int32 MaxTokens, const FString& ModelName) const override;
	virtual bool ExtractContent(const TSharedPtr<FJsonObject>& Response, FString& OutContent) const override;
	virtual FString GetDisplayName() const override { return TEXT("Volcengine (Doubao)"); }
};

// ============================================================================
// 策略工厂
// ============================================================================

enum class EAIProvider : uint8;

class FAIProviderStrategyFactory
{
public:
	/** 获取指定提供商的策略实例 */
	static TSharedPtr<IAIProviderStrategy> GetStrategy(EAIProvider Provider);

	/** 检查提供商是否支持 */
	static bool IsSupported(EAIProvider Provider);

private:
	static TMap<EAIProvider, TSharedPtr<IAIProviderStrategy>> Strategies;
	static bool bInitialized;
	static void Initialize();
};
