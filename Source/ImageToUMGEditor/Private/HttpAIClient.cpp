// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpAIClient.h"
#include "ImageToUMGSettings.h"
#include "ImageToUMGLog.h"
#include "ImageToUMGConstants.h"
#include "HttpModule.h"
#include "Http.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// 私有辅助函数（匿名namespace）
// ============================================================================

namespace
{
	// --- 请求配置 ---

	struct FRequestConfig
	{
		FString URL;
		TMap<FString, FString> Headers;
		bool bValid = false;
	};

	FRequestConfig BuildRequestConfig(EAIProvider Provider, const UImageToUMGSettings* Settings)
	{
		FRequestConfig Config;
		FString Endpoint = Settings->CustomEndpoint.IsEmpty()
			? UImageToUMGSettings::GetProviderEndpoint(Provider)
			: Settings->CustomEndpoint;

		switch (Provider)
		{
		case EAIProvider::OpenAI:
		case EAIProvider::DeepSeek:
		case EAIProvider::Groq:
		case EAIProvider::Together:
		case EAIProvider::OpenRouter:
		case EAIProvider::Mistral:
		case EAIProvider::Volcengine:
			Config.URL = Endpoint + TEXT("/chat/completions");
			Config.Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Settings->APIKey));
			Config.bValid = true;
			break;

		case EAIProvider::Anthropic:
			Config.URL = Endpoint + TEXT("/messages");
			Config.Headers.Add(TEXT("x-api-key"), Settings->APIKey);
			Config.Headers.Add(TEXT("anthropic-version"), TEXT("2023-06-01"));
			Config.bValid = true;
			break;

		case EAIProvider::Google:
			if (!Settings->ModelName.IsEmpty())
			{
				Config.URL = FString::Printf(TEXT("%s/models/%s:generateContent?key=%s"),
					*Endpoint, *Settings->ModelName, *Settings->APIKey);
				Config.bValid = true;
			}
			break;

		case EAIProvider::Custom:
			Config.URL = Settings->CustomEndpoint;
			Config.Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Settings->APIKey));
			Config.bValid = true;
			break;

		default:
			break;
		}

		if (Config.bValid)
		{
			Config.Headers.Add(TEXT("Content-Type"), TEXT("application/json"));
		}

		return Config;
	}

	FString GetHttpStatusDescription(EHttpRequestStatus::Type Status)
	{
		switch (Status)
		{
		case EHttpRequestStatus::NotStarted: return TEXT("Request not started");
		case EHttpRequestStatus::Processing: return TEXT("Request timeout");
		case EHttpRequestStatus::Failed: return TEXT("Network error");
		case EHttpRequestStatus::Succeeded: return TEXT("Unexpected failure");
		default: return FString::Printf(TEXT("Unknown status: %d"), static_cast<int32>(Status));
		}
	}

	// --- 请求体构建 ---

	TSharedPtr<FJsonObject> BuildOpenAICompatibleBody(const FString& Base64Image, const FString& MimeType, const FString& Prompt, int32 MaxTokens, const FString& ModelName, bool bIsVolcengine = false)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("model"), ModelName);

		// 火山引擎使用 max_completion_tokens，其他使用 max_tokens
		if (bIsVolcengine)
		{
			Root->SetNumberField(TEXT("max_completion_tokens"), MaxTokens);
		}
		else
		{
			Root->SetNumberField(TEXT("max_tokens"), MaxTokens);
		}

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
		// 火山引擎要求 image_url 在 text 之前
		UserMsg->SetArrayField(TEXT("content"), { MakeShared<FJsonValueObject>(ImagePart), MakeShared<FJsonValueObject>(TextPart) });

		Root->SetArrayField(TEXT("messages"), { MakeShared<FJsonValueObject>(UserMsg) });

		return Root;
	}

	TSharedPtr<FJsonObject> BuildAnthropicBody(const FString& Base64Image, const FString& MimeType, const FString& Prompt, int32 MaxTokens, const FString& ModelName)
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

	TSharedPtr<FJsonObject> BuildGoogleBody(const FString& Base64Image, const FString& MimeType, const FString& Prompt, int32 MaxTokens)
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

	// --- JSON 响应处理 ---

	FString SanitizeJsonString(const FString& Input)
	{
		FString Result = Input;

		if (Result.Len() > 0 && Result[0] == 0xFEFF)
		{
			Result.RemoveAt(0);
		}

		Result.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Result.ReplaceInline(TEXT("\r"), TEXT("\n"));

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

	FString ExtractFromCodeBlock(const FString& Input)
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

	bool ExtractJsonByBraceMatching(const FString& Input, FString& OutJson)
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

			if (Ch == TEXT('\\') && i + 1 < Input.Len())
			{
				++i;
				continue;
			}

			if (Ch == TEXT('"'))
			{
				bInString = !bInString;
				continue;
			}

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
}

// ============================================================================
// FHttpAIClient 实现
// ============================================================================

FString FHttpAIClient::SerializeJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	return Output;
}

bool FHttpAIClient::GetNestedString(const TSharedPtr<FJsonObject>& Obj, const TArray<FString>& Path, FString& OutValue)
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

void FHttpAIClient::HandleResponse(
	FHttpRequestPtr HttpRequest,
	FHttpResponsePtr HttpResponse,
	bool bSucceeded,
	EAIProvider Provider,
	const FString& DebugInfo,
	FOnAIResponseReceived OnSuccess,
	FOnAIRequestFailed OnError)
{
	if (!bSucceeded)
	{
		FString ErrorDetail = TEXT("Unknown error");
		if (HttpRequest.IsValid())
		{
			ErrorDetail = GetHttpStatusDescription(HttpRequest->GetStatus());
			if (HttpResponse.IsValid())
			{
				ErrorDetail += FString::Printf(TEXT(" (HTTP %d)"), HttpResponse->GetResponseCode());
			}
		}
		UE_LOG(LogImageToUMG, Error, TEXT("HTTP Request Failed: %s - %s"), *DebugInfo, *ErrorDetail);
		OnError.ExecuteIfBound(FString::Printf(TEXT("HTTP request failed: %s\n\n%s"), *ErrorDetail, *DebugInfo));
		return;
	}

	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogImageToUMG, Error, TEXT("Invalid HTTP response"));
		OnError.ExecuteIfBound(TEXT("Invalid HTTP response"));
		return;
	}

	const int32 ResponseCode = HttpResponse->GetResponseCode();
	const FString ResponseBody = HttpResponse->GetContentAsString();

	UE_LOG(LogImageToUMG, Verbose, TEXT("Response: HTTP %d, %d bytes"), ResponseCode, ResponseBody.Len());

	if (ResponseCode != 200)
	{
		UE_LOG(LogImageToUMG, Error, TEXT("API Error (HTTP %d): %s"), ResponseCode, *ResponseBody.Left(ImageToUMGConstants::ErrorResponsePreviewLen));
		OnError.ExecuteIfBound(FString::Printf(TEXT("API Error (HTTP %d)\n\n%s\n\nResponse:\n%s"),
			ResponseCode, *DebugInfo, *ResponseBody.Left(ImageToUMGConstants::ErrorResponseDetailLen)));
		return;
	}

	FString ExtractedJson, ExtractError;
	if (ExtractJsonFromResponse(ResponseBody, Provider, ExtractedJson, ExtractError))
	{
		UE_LOG(LogImageToUMG, Log, TEXT("AI request successful"));
		OnSuccess.ExecuteIfBound(ExtractedJson);
	}
	else
	{
		UE_LOG(LogImageToUMG, Error, TEXT("Failed to extract JSON: %s"), *ExtractError);
		OnError.ExecuteIfBound(FString::Printf(TEXT("Failed to parse response: %s\n\nRaw:\n%s"),
			*ExtractError, *ResponseBody.Left(ImageToUMGConstants::ErrorResponsePreviewLen)));
	}
}

// ============================================================================
// 主要实现
// ============================================================================

void FHttpAIClient::AnalyzeImage(
	const TArray<uint8>& ImageData,
	const FString& ImageFormat,
	int32 ImageWidth,
	int32 ImageHeight,
	FOnAIResponseReceived OnSuccess,
	FOnAIRequestFailed OnError)
{
	const UImageToUMGSettings* Settings = UImageToUMGSettings::Get();

	if (Settings->APIKey.IsEmpty())
	{
		OnError.ExecuteIfBound(TEXT("API Key is not configured"));
		return;
	}

	// 确定实际提供商
	EAIProvider Provider = Settings->Provider;
	if (Provider == EAIProvider::Auto)
	{
		Provider = UImageToUMGSettings::DetectProviderFromAPIKey(Settings->APIKey);
		if (Provider == EAIProvider::Auto)
		{
			OnError.ExecuteIfBound(TEXT("Could not detect AI provider from API key.\nPlease select a provider manually in Project Settings > Plugins > ImageToUMG."));
			return;
		}
	}

	// 校验 ModelName（除 Custom 外都需要）
	if (Settings->ModelName.IsEmpty() && Provider != EAIProvider::Custom)
	{
		OnError.ExecuteIfBound(FString::Printf(
			TEXT("No model specified for %s.\nPlease select a model in Project Settings > Plugins > ImageToUMG."),
			*UImageToUMGSettings::GetProviderName(Provider)));
		return;
	}

	// 构建请求配置
	FRequestConfig Config = BuildRequestConfig(Provider, Settings);
	if (!Config.bValid)
	{
		OnError.ExecuteIfBound(FString::Printf(
			TEXT("Failed to configure request for %s.\nThis provider may not support vision API or requires additional configuration."),
			*UImageToUMGSettings::GetProviderName(Provider)));
		return;
	}

	// 准备请求数据
	const FString Base64Image = FBase64::Encode(ImageData);
	const FString MimeType = ImageFormat.ToLower() == TEXT("png") ? TEXT("image/png") : TEXT("image/jpeg");
	const FString Prompt = GetSystemPrompt(ImageWidth, ImageHeight);
	FString RequestBody = BuildRequestBody(Provider, Base64Image, MimeType, Prompt, Settings->MaxTokens, Settings->ModelName);

	// 创建 HTTP 请求
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Config.URL);
	Request->SetVerb(TEXT("POST"));
	Request->SetTimeout(FMath::Max(ImageToUMGConstants::DefaultTimeoutSeconds, Settings->TimeoutSeconds));
	Request->SetHeader(TEXT("Connection"), TEXT("keep-alive"));

	// 调试选项：禁用SSL验证（用于排查网络问题）
	if (Settings->bDisableSSLVerification)
	{
		UE_LOG(LogImageToUMG, Warning, TEXT("SSL verification disabled - this is insecure, use only for debugging!"));
		FPlatformHttp::VerifyPeerSslCertificate(false);
	}

	// 禁用代理（UE5可能从系统读取了错误的代理配置）
	FHttpModule::Get().SetProxyAddress(TEXT(""));

	for (const auto& Header : Config.Headers)
	{
		Request->SetHeader(Header.Key, Header.Value);
	}

	FTCHARToUTF8 Converter(*RequestBody);
	TArray<uint8> RequestBodyBytes;
	RequestBodyBytes.Append((const uint8*)Converter.Get(), Converter.Length());

	// 检查请求体大小（某些服务器限制请求大小）
	const int32 BodySizeMB = RequestBodyBytes.Num() / (1024 * 1024);
	if (BodySizeMB > 20)
	{
		UE_LOG(LogImageToUMG, Warning, TEXT("Request body is very large: %d MB. This may cause network issues."), BodySizeMB);
	}

	Request->SetContent(RequestBodyBytes);

	// 日志
	UE_LOG(LogImageToUMG, Log, TEXT("Sending AI request: %s, Model: %s, Image: %dx%d, Body: %.2f MB"),
		*UImageToUMGSettings::GetProviderName(Provider), *Settings->ModelName, ImageWidth, ImageHeight,
		RequestBodyBytes.Num() / (1024.0f * 1024.0f));
	UE_LOG(LogImageToUMG, Verbose, TEXT("URL: %s"), *Config.URL);

	// 调试信息
	FString DebugInfo = FString::Printf(TEXT("Provider: %s\nModel: %s\nURL: %s"),
		*UImageToUMGSettings::GetProviderName(Provider), *Settings->ModelName, *Config.URL);

	TWeakPtr<FHttpAIClient> WeakSelf = AsShared();

	Request->OnProcessRequestComplete().BindLambda(
		[WeakSelf, Provider, OnSuccess, OnError, DebugInfo](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
		{
			TSharedPtr<FHttpAIClient> StrongThis = WeakSelf.Pin();
			if (!StrongThis.IsValid())
			{
				UE_LOG(LogImageToUMG, Warning, TEXT("FHttpAIClient destroyed before callback"));
				return;
			}

			// 详细的失败原因日志
			if (!bSucceeded && HttpRequest.IsValid())
			{
				EHttpRequestStatus::Type Status = HttpRequest->GetStatus();
				EHttpFailureReason FailureReason = HttpRequest->GetFailureReason();
				UE_LOG(LogImageToUMG, Error, TEXT("HTTP Request Status: %d, FailureReason: %d"),
					static_cast<int32>(Status), static_cast<int32>(FailureReason));
			}

			StrongThis->HandleResponse(HttpRequest, HttpResponse, bSucceeded, Provider, DebugInfo, OnSuccess, OnError);
		});

	Request->ProcessRequest();
}

FString FHttpAIClient::BuildRequestBody(EAIProvider Provider, const FString& Base64Image, const FString& MimeType, const FString& Prompt, int32 MaxTokens, const FString& ModelName)
{
	TSharedPtr<FJsonObject> Body;

	switch (Provider)
	{
	case EAIProvider::Anthropic:
		Body = BuildAnthropicBody(Base64Image, MimeType, Prompt, MaxTokens, ModelName);
		break;
	case EAIProvider::Google:
		Body = BuildGoogleBody(Base64Image, MimeType, Prompt, MaxTokens);
		break;
	case EAIProvider::Volcengine:
		Body = BuildOpenAICompatibleBody(Base64Image, MimeType, Prompt, MaxTokens, ModelName, true);
		break;
	default:
		Body = BuildOpenAICompatibleBody(Base64Image, MimeType, Prompt, MaxTokens, ModelName, false);
		break;
	}

	return SerializeJson(Body);
}

FString FHttpAIClient::GetSystemPrompt(int32 ImageWidth, int32 ImageHeight) const
{
	const UImageToUMGSettings* Settings = UImageToUMGSettings::Get();
	FString Prompt = Settings->SystemPrompt;

	if (Prompt.IsEmpty())
	{
		Prompt = UImageToUMGSettings::GetDefaultSystemPrompt();
	}

	Prompt.ReplaceInline(TEXT("{WIDTH}"), *FString::FromInt(ImageWidth));
	Prompt.ReplaceInline(TEXT("{HEIGHT}"), *FString::FromInt(ImageHeight));
	return Prompt;
}

bool FHttpAIClient::ExtractJsonFromResponse(const FString& Response, EAIProvider Provider, FString& OutJson, FString& OutError)
{
	TSharedPtr<FJsonObject> ResponseObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

	if (!FJsonSerializer::Deserialize(Reader, ResponseObject) || !ResponseObject.IsValid())
	{
		OutError = TEXT("Failed to parse API response");
		return false;
	}

	FString ContentText;

	switch (Provider)
	{
	case EAIProvider::Anthropic:
		GetNestedString(ResponseObject, { TEXT("content[0]"), TEXT("text") }, ContentText);
		break;
	case EAIProvider::Google:
		GetNestedString(ResponseObject, { TEXT("candidates[0]"), TEXT("content"), TEXT("parts[0]"), TEXT("text") }, ContentText);
		break;
	default:
		GetNestedString(ResponseObject, { TEXT("choices[0]"), TEXT("message"), TEXT("content") }, ContentText);
		break;
	}

	if (ContentText.IsEmpty())
	{
		OutError = TEXT("No content in API response");
		return false;
	}

	// 步骤1：清理和预处理
	FString CleanedJson = SanitizeJsonString(ContentText);

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

	// 步骤4：Fallback - 括号匹配提取
	if (ExtractJsonByBraceMatching(CleanedJson, OutJson))
	{
		// 验证提取的 JSON 是否有效
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
