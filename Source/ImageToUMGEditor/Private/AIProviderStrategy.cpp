// Copyright Epic Games, Inc. All Rights Reserved.

#include "AIProviderStrategy.h"
#include "ImageToUMGSettings.h"

// ============================================================================
// 基类默认实现
// ============================================================================

FString IAIProviderStrategy::BuildURL(const FString& Endpoint, const FString& ModelName) const
{
	// 默认实现：Endpoint + EndpointSuffix
	FString Suffix = GetEndpointSuffix();

	// 如果 Endpoint 以 / 结尾，去掉 Suffix 开头的 /
	if (Endpoint.EndsWith(TEXT("/")))
	{
		return Endpoint + (Suffix.StartsWith(TEXT("/")) ? Suffix.Mid(1) : Suffix);
	}

	return Endpoint + Suffix;
}

// ============================================================================
// 辅助函数
// ============================================================================

namespace
{
	bool GetNestedString(const TSharedPtr<FJsonObject>& Obj, const TArray<FString>& Path, FString& OutValue)
	{
		if (!Obj.IsValid() || Path.Num() == 0) return false;

		TSharedPtr<FJsonObject> Current = Obj;

		for (int32 i = 0; i < Path.Num(); ++i)
		{
			if (!Current.IsValid()) return false;

			const FString& Key = Path[i];
			const bool bIsLast = (i == Path.Num() - 1);

			const int32 BracketIdx = Key.Find(TEXT("["));
			const FString FieldName = (BracketIdx != INDEX_NONE) ? Key.Left(BracketIdx) : Key;
			const int32 ArrayIndex = (BracketIdx != INDEX_NONE) ? FCString::Atoi(*Key.Mid(BracketIdx + 1)) : -1;

			if (ArrayIndex >= 0)
			{
				const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
				if (!Current->TryGetArrayField(FieldName, ArrayField) || !ArrayField || ArrayIndex >= ArrayField->Num())
					return false;

				const TSharedPtr<FJsonValue>& Element = (*ArrayField)[ArrayIndex];
				if (!Element.IsValid()) return false;

				if (bIsLast) return Element->TryGetString(OutValue);

				const TSharedPtr<FJsonObject>* NextObj = nullptr;
				if (!Element->TryGetObject(NextObj) || !NextObj || !(*NextObj).IsValid())
					return false;
				Current = *NextObj;
			}
			else
			{
				if (bIsLast) return Current->TryGetStringField(FieldName, OutValue);

				const TSharedPtr<FJsonObject>* NextObj = nullptr;
				if (!Current->TryGetObjectField(FieldName, NextObj) || !NextObj || !(*NextObj).IsValid())
					return false;
				Current = *NextObj;
			}
		}
		return false;
	}
}

// ============================================================================
// OpenAI 兼容策略
// ============================================================================

TMap<FString, FString> FOpenAICompatibleStrategy::GetHeaders(const FString& APIKey) const
{
	TMap<FString, FString> Headers;
	Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *APIKey));
	Headers.Add(TEXT("Content-Type"), TEXT("application/json"));
	return Headers;
}

TSharedPtr<FJsonObject> FOpenAICompatibleStrategy::BuildRequestBody(
	const FString& Base64Image,
	const FString& MimeType,
	const FString& Prompt,
	int32 MaxTokens,
	const FString& ModelName) const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), ModelName);
	Root->SetNumberField(TEXT("max_tokens"), MaxTokens);

	TSharedPtr<FJsonObject> ImageUrl = MakeShared<FJsonObject>();
	ImageUrl->SetStringField(TEXT("url"), FString::Printf(TEXT("data:%s;base64,%s"), *MimeType, *Base64Image));

	TSharedPtr<FJsonObject> ImagePart = MakeShared<FJsonObject>();
	ImagePart->SetStringField(TEXT("type"), TEXT("image_url"));
	ImagePart->SetObjectField(TEXT("image_url"), ImageUrl);

	TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
	TextPart->SetStringField(TEXT("type"), TEXT("text"));
	TextPart->SetStringField(TEXT("text"), Prompt);

	TSharedPtr<FJsonObject> UserMsg = MakeShared<FJsonObject>();
	UserMsg->SetStringField(TEXT("role"), TEXT("user"));
	UserMsg->SetArrayField(TEXT("content"), { MakeShared<FJsonValueObject>(ImagePart), MakeShared<FJsonValueObject>(TextPart) });

	Root->SetArrayField(TEXT("messages"), { MakeShared<FJsonValueObject>(UserMsg) });
	return Root;
}

bool FOpenAICompatibleStrategy::ExtractContent(const TSharedPtr<FJsonObject>& Response, FString& OutContent) const
{
	return GetNestedString(Response, { TEXT("choices[0]"), TEXT("message"), TEXT("content") }, OutContent);
}

// ============================================================================
// Anthropic 策略
// ============================================================================

TMap<FString, FString> FAnthropicStrategy::GetHeaders(const FString& APIKey) const
{
	TMap<FString, FString> Headers;
	Headers.Add(TEXT("x-api-key"), APIKey);
	Headers.Add(TEXT("anthropic-version"), TEXT("2023-06-01"));
	Headers.Add(TEXT("Content-Type"), TEXT("application/json"));
	return Headers;
}

TSharedPtr<FJsonObject> FAnthropicStrategy::BuildRequestBody(
	const FString& Base64Image,
	const FString& MimeType,
	const FString& Prompt,
	int32 MaxTokens,
	const FString& ModelName) const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), ModelName);
	Root->SetNumberField(TEXT("max_tokens"), MaxTokens);

	TSharedPtr<FJsonObject> Source = MakeShared<FJsonObject>();
	Source->SetStringField(TEXT("type"), TEXT("base64"));
	Source->SetStringField(TEXT("media_type"), MimeType);
	Source->SetStringField(TEXT("data"), Base64Image);

	TSharedPtr<FJsonObject> ImagePart = MakeShared<FJsonObject>();
	ImagePart->SetStringField(TEXT("type"), TEXT("image"));
	ImagePart->SetObjectField(TEXT("source"), Source);

	TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
	TextPart->SetStringField(TEXT("type"), TEXT("text"));
	TextPart->SetStringField(TEXT("text"), Prompt);

	TSharedPtr<FJsonObject> UserMsg = MakeShared<FJsonObject>();
	UserMsg->SetStringField(TEXT("role"), TEXT("user"));
	UserMsg->SetArrayField(TEXT("content"), { MakeShared<FJsonValueObject>(ImagePart), MakeShared<FJsonValueObject>(TextPart) });

	Root->SetArrayField(TEXT("messages"), { MakeShared<FJsonValueObject>(UserMsg) });
	return Root;
}

bool FAnthropicStrategy::ExtractContent(const TSharedPtr<FJsonObject>& Response, FString& OutContent) const
{
	return GetNestedString(Response, { TEXT("content[0]"), TEXT("text") }, OutContent);
}

// ============================================================================
// Google 策略
// ============================================================================

TMap<FString, FString> FGoogleStrategy::GetHeaders(const FString& APIKey) const
{
	TMap<FString, FString> Headers;
	Headers.Add(TEXT("Content-Type"), TEXT("application/json"));
	// 使用 x-goog-api-key header 传递密钥，避免URL泄露
	Headers.Add(TEXT("x-goog-api-key"), APIKey);
	return Headers;
}

FString FGoogleStrategy::BuildURL(const FString& Endpoint, const FString& ModelName) const
{
	// 密钥通过 x-goog-api-key header 传递，不在URL中暴露
	return FString::Printf(TEXT("%s/models/%s:generateContent"), *Endpoint, *ModelName);
}

TSharedPtr<FJsonObject> FGoogleStrategy::BuildRequestBody(
	const FString& Base64Image,
	const FString& MimeType,
	const FString& Prompt,
	int32 MaxTokens,
	const FString& ModelName) const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
	TextPart->SetStringField(TEXT("text"), Prompt);

	TSharedPtr<FJsonObject> InlineData = MakeShared<FJsonObject>();
	InlineData->SetStringField(TEXT("mimeType"), MimeType);
	InlineData->SetStringField(TEXT("data"), Base64Image);

	TSharedPtr<FJsonObject> ImagePart = MakeShared<FJsonObject>();
	ImagePart->SetObjectField(TEXT("inlineData"), InlineData);

	TSharedPtr<FJsonObject> ContentObj = MakeShared<FJsonObject>();
	ContentObj->SetArrayField(TEXT("parts"), { MakeShared<FJsonValueObject>(TextPart), MakeShared<FJsonValueObject>(ImagePart) });
	Root->SetArrayField(TEXT("contents"), { MakeShared<FJsonValueObject>(ContentObj) });

	TSharedPtr<FJsonObject> GenConfig = MakeShared<FJsonObject>();
	GenConfig->SetNumberField(TEXT("maxOutputTokens"), MaxTokens);
	Root->SetObjectField(TEXT("generationConfig"), GenConfig);

	return Root;
}

bool FGoogleStrategy::ExtractContent(const TSharedPtr<FJsonObject>& Response, FString& OutContent) const
{
	return GetNestedString(Response, { TEXT("candidates[0]"), TEXT("content"), TEXT("parts[0]"), TEXT("text") }, OutContent);
}

// ============================================================================
// 火山引擎策略
// ============================================================================

TMap<FString, FString> FVolcengineStrategy::GetHeaders(const FString& APIKey) const
{
	TMap<FString, FString> Headers;
	Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *APIKey));
	Headers.Add(TEXT("Content-Type"), TEXT("application/json"));
	return Headers;
}

TSharedPtr<FJsonObject> FVolcengineStrategy::BuildRequestBody(
	const FString& Base64Image,
	const FString& MimeType,
	const FString& Prompt,
	int32 MaxTokens,
	const FString& ModelName) const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), ModelName);

	// 构建 input_image 部分
	TSharedPtr<FJsonObject> ImagePart = MakeShared<FJsonObject>();
	ImagePart->SetStringField(TEXT("type"), TEXT("input_image"));
	ImagePart->SetStringField(TEXT("image_url"), FString::Printf(TEXT("data:%s;base64,%s"), *MimeType, *Base64Image));

	// 构建 input_text 部分
	TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
	TextPart->SetStringField(TEXT("type"), TEXT("input_text"));
	TextPart->SetStringField(TEXT("text"), Prompt);

	// 构建 user message
	TSharedPtr<FJsonObject> UserMsg = MakeShared<FJsonObject>();
	UserMsg->SetStringField(TEXT("role"), TEXT("user"));
	UserMsg->SetArrayField(TEXT("content"), { MakeShared<FJsonValueObject>(ImagePart), MakeShared<FJsonValueObject>(TextPart) });

	// 使用 "input" 而不是 "messages"
	Root->SetArrayField(TEXT("input"), { MakeShared<FJsonValueObject>(UserMsg) });

	return Root;
}

bool FVolcengineStrategy::ExtractContent(const TSharedPtr<FJsonObject>& Response, FString& OutContent) const
{
	if (!Response.IsValid()) return false;

	// 火山引擎 /responses 端点的响应格式:
	// output 数组中包含多个项，需要找到 type="message" 的项
	// output: [
	//   {"type": "reasoning", ...},
	//   {"type": "message", "content": [{"type": "output_text", "text": "..."}]}
	// ]

	const TArray<TSharedPtr<FJsonValue>>* OutputArray = nullptr;
	if (!Response->TryGetArrayField(TEXT("output"), OutputArray) || !OutputArray)
	{
		// 备用路径：尝试 choices 格式（兼容旧版本）
		return GetNestedString(Response, { TEXT("choices[0]"), TEXT("message"), TEXT("content") }, OutContent);
	}

	// 遍历 output 数组，找到 type="message" 的项
	for (const TSharedPtr<FJsonValue>& OutputItem : *OutputArray)
	{
		if (!OutputItem.IsValid()) continue;

		const TSharedPtr<FJsonObject>* OutputObj = nullptr;
		if (!OutputItem->TryGetObject(OutputObj) || !OutputObj || !(*OutputObj).IsValid())
			continue;

		FString Type;
		if (!(*OutputObj)->TryGetStringField(TEXT("type"), Type)) continue;

		if (Type == TEXT("message"))
		{
			// 找到 message 类型，提取 content[0].text
			const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
			if (!(*OutputObj)->TryGetArrayField(TEXT("content"), ContentArray) || !ContentArray || ContentArray->Num() == 0)
				continue;

			const TSharedPtr<FJsonValue>& FirstContent = (*ContentArray)[0];
			if (!FirstContent.IsValid()) continue;

			const TSharedPtr<FJsonObject>* ContentObj = nullptr;
			if (!FirstContent->TryGetObject(ContentObj) || !ContentObj || !(*ContentObj).IsValid())
				continue;

			// 尝试 "text" 字段（output_text 类型）
			if ((*ContentObj)->TryGetStringField(TEXT("text"), OutContent))
			{
				return true;
			}
		}
	}

	return false;
}

// ============================================================================
// 策略工厂 (Meyers Singleton 模式，避免静态初始化顺序问题)
// ============================================================================

namespace
{
	TMap<EAIProvider, TSharedPtr<IAIProviderStrategy>>& GetStrategiesMap()
	{
		static TMap<EAIProvider, TSharedPtr<IAIProviderStrategy>> Strategies = []()
		{
			TMap<EAIProvider, TSharedPtr<IAIProviderStrategy>> Map;

			// OpenAI 兼容提供商
			TSharedPtr<IAIProviderStrategy> OpenAIStrategy = MakeShared<FOpenAICompatibleStrategy>();
			Map.Add(EAIProvider::OpenAI, OpenAIStrategy);
			Map.Add(EAIProvider::DeepSeek, OpenAIStrategy);
			Map.Add(EAIProvider::Groq, OpenAIStrategy);
			Map.Add(EAIProvider::Together, OpenAIStrategy);
			Map.Add(EAIProvider::OpenRouter, OpenAIStrategy);
			Map.Add(EAIProvider::Mistral, OpenAIStrategy);
			Map.Add(EAIProvider::Custom, OpenAIStrategy);

			// 特殊格式提供商
			Map.Add(EAIProvider::Anthropic, MakeShared<FAnthropicStrategy>());
			Map.Add(EAIProvider::Google, MakeShared<FGoogleStrategy>());
			Map.Add(EAIProvider::Volcengine, MakeShared<FVolcengineStrategy>());

			return Map;
		}();
		return Strategies;
	}
}

TSharedPtr<IAIProviderStrategy> FAIProviderStrategyFactory::GetStrategy(EAIProvider Provider)
{
	TMap<EAIProvider, TSharedPtr<IAIProviderStrategy>>& Strategies = GetStrategiesMap();
	if (TSharedPtr<IAIProviderStrategy>* Found = Strategies.Find(Provider))
	{
		return *Found;
	}
	return nullptr;
}

bool FAIProviderStrategyFactory::IsSupported(EAIProvider Provider)
{
	return GetStrategiesMap().Contains(Provider);
}
