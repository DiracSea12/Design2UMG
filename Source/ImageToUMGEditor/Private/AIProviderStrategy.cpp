// Copyright Epic Games, Inc. All Rights Reserved.

#include "AIProviderStrategy.h"
#include "ImageToUMGSettings.h"

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
			const FString& Key = Path[i];
			const bool bIsLast = (i == Path.Num() - 1);

			int32 BracketIdx = Key.Find(TEXT("["));
			FString FieldName = (BracketIdx != INDEX_NONE) ? Key.Left(BracketIdx) : Key;
			int32 ArrayIndex = (BracketIdx != INDEX_NONE) ? FCString::Atoi(*Key.Mid(BracketIdx + 1)) : -1;

			if (ArrayIndex >= 0)
			{
				const TArray<TSharedPtr<FJsonValue>>* ArrayField;
				if (!Current->TryGetArrayField(FieldName, ArrayField) || ArrayIndex >= ArrayField->Num())
					return false;

				if (bIsLast) return (*ArrayField)[ArrayIndex]->TryGetString(OutValue);

				const TSharedPtr<FJsonObject>* NextObj;
				if (!(*ArrayField)[ArrayIndex]->TryGetObject(NextObj)) return false;
				Current = *NextObj;
			}
			else
			{
				if (bIsLast) return Current->TryGetStringField(FieldName, OutValue);

				const TSharedPtr<FJsonObject>* NextObj;
				if (!Current->TryGetObjectField(FieldName, NextObj)) return false;
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
	// Google 使用 URL 参数传递 API Key，不需要 Authorization header
	return Headers;
}

FString FGoogleStrategy::BuildFullURL(const FString& Endpoint, const FString& ModelName, const FString& APIKey) const
{
	return FString::Printf(TEXT("%s/models/%s:generateContent?key=%s"), *Endpoint, *ModelName, *APIKey);
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
	// 火山引擎 /responses 端点的响应格式:
	// output 数组中包含多个项，需要找到 type="message" 的项
	// output: [
	//   {"type": "reasoning", ...},
	//   {"type": "message", "content": [{"type": "output_text", "text": "..."}]}
	// ]

	const TArray<TSharedPtr<FJsonValue>>* OutputArray;
	if (!Response->TryGetArrayField(TEXT("output"), OutputArray))
	{
		// 备用路径：尝试 choices 格式（兼容旧版本）
		return GetNestedString(Response, { TEXT("choices[0]"), TEXT("message"), TEXT("content") }, OutContent);
	}

	// 遍历 output 数组，找到 type="message" 的项
	for (const TSharedPtr<FJsonValue>& OutputItem : *OutputArray)
	{
		const TSharedPtr<FJsonObject>* OutputObj;
		if (!OutputItem->TryGetObject(OutputObj)) continue;

		FString Type;
		if (!(*OutputObj)->TryGetStringField(TEXT("type"), Type)) continue;

		if (Type == TEXT("message"))
		{
			// 找到 message 类型，提取 content[0].text
			const TArray<TSharedPtr<FJsonValue>>* ContentArray;
			if (!(*OutputObj)->TryGetArrayField(TEXT("content"), ContentArray) || ContentArray->Num() == 0)
				continue;

			const TSharedPtr<FJsonObject>* ContentObj;
			if (!(*ContentArray)[0]->TryGetObject(ContentObj)) continue;

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
// 策略工厂
// ============================================================================

TMap<EAIProvider, TSharedPtr<IAIProviderStrategy>> FAIProviderStrategyFactory::Strategies;
bool FAIProviderStrategyFactory::bInitialized = false;

void FAIProviderStrategyFactory::Initialize()
{
	if (bInitialized) return;

	// OpenAI 兼容提供商
	TSharedPtr<IAIProviderStrategy> OpenAIStrategy = MakeShared<FOpenAICompatibleStrategy>();
	Strategies.Add(EAIProvider::OpenAI, OpenAIStrategy);
	Strategies.Add(EAIProvider::DeepSeek, OpenAIStrategy);
	Strategies.Add(EAIProvider::Groq, OpenAIStrategy);
	Strategies.Add(EAIProvider::Together, OpenAIStrategy);
	Strategies.Add(EAIProvider::OpenRouter, OpenAIStrategy);
	Strategies.Add(EAIProvider::Mistral, OpenAIStrategy);
	Strategies.Add(EAIProvider::Custom, OpenAIStrategy);

	// 特殊格式提供商
	Strategies.Add(EAIProvider::Anthropic, MakeShared<FAnthropicStrategy>());
	Strategies.Add(EAIProvider::Google, MakeShared<FGoogleStrategy>());
	Strategies.Add(EAIProvider::Volcengine, MakeShared<FVolcengineStrategy>());

	bInitialized = true;
}

TSharedPtr<IAIProviderStrategy> FAIProviderStrategyFactory::GetStrategy(EAIProvider Provider)
{
	Initialize();

	if (TSharedPtr<IAIProviderStrategy>* Found = Strategies.Find(Provider))
	{
		return *Found;
	}
	return nullptr;
}

bool FAIProviderStrategyFactory::IsSupported(EAIProvider Provider)
{
	Initialize();
	return Strategies.Contains(Provider);
}
