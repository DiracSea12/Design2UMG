// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpAIClient.h"
#include "AIProviderStrategy.h"
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
// 私有辅助函数
// ============================================================================

namespace
{
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

void FHttpAIClient::HandleResponse(
	FHttpRequestPtr HttpRequest,
	FHttpResponsePtr HttpResponse,
	bool bSucceeded,
	TSharedPtr<IAIProviderStrategy> Strategy,
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
	if (ExtractJsonFromResponse(ResponseBody, Strategy, ExtractedJson, ExtractError))
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

	// 获取策略
	TSharedPtr<IAIProviderStrategy> Strategy = FAIProviderStrategyFactory::GetStrategy(Provider);
	if (!Strategy.IsValid())
	{
		OnError.ExecuteIfBound(FString::Printf(
			TEXT("Unsupported AI provider: %s"),
			*UImageToUMGSettings::GetProviderName(Provider)));
		return;
	}

	// 校验 ModelName（除 Custom 外都需要）
	if (Settings->ModelName.IsEmpty() && Provider != EAIProvider::Custom)
	{
		OnError.ExecuteIfBound(FString::Printf(
			TEXT("No model specified for %s.\nPlease select a model in Project Settings > Plugins > ImageToUMG."),
			*Strategy->GetDisplayName()));
		return;
	}

	// 构建 URL
	FString Endpoint = Settings->CustomEndpoint.IsEmpty()
		? UImageToUMGSettings::GetProviderEndpoint(Provider)
		: Settings->CustomEndpoint;

	FString URL;
	if (Provider == EAIProvider::Google)
	{
		// Google 需要特殊的 URL 构建
		URL = StaticCastSharedPtr<FGoogleStrategy>(Strategy)->BuildFullURL(Endpoint, Settings->ModelName, Settings->APIKey);
	}
	else
	{
		URL = Endpoint + Strategy->GetEndpointSuffix();
	}

	// 获取请求头
	TMap<FString, FString> Headers = Strategy->GetHeaders(Settings->APIKey);

	// 准备请求数据
	const FString Base64Image = FBase64::Encode(ImageData);
	const FString MimeType = ImageFormat.ToLower() == TEXT("png") ? TEXT("image/png") : TEXT("image/jpeg");
	const FString Prompt = GetSystemPrompt(ImageWidth, ImageHeight);

	// 构建请求体
	TSharedPtr<FJsonObject> RequestBodyJson = Strategy->BuildRequestBody(
		Base64Image, MimeType, Prompt, Settings->MaxTokens, Settings->ModelName);
	FString RequestBody = SerializeJson(RequestBodyJson);

	// 调试选项：禁用SSL验证（必须在创建请求之前设置）
	const bool bDisableSSL = Settings->bDisableSSLVerification;
	if (bDisableSSL)
	{
		UE_LOG(LogImageToUMG, Warning, TEXT("SSL verification disabled for this request"));
		FPlatformHttp::VerifyPeerSslCertificate(false);
	}

	// 创建 HTTP 请求
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("POST"));
	Request->SetTimeout(FMath::Max(ImageToUMGConstants::DefaultTimeoutSeconds, Settings->TimeoutSeconds));

	// 添加必要的 HTTP 头
	Request->SetHeader(TEXT("Connection"), TEXT("keep-alive"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("UnrealEngine/5.0"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept-Encoding"), TEXT("identity")); // 禁用压缩，避免兼容性问题

	for (const auto& Header : Headers)
	{
		Request->SetHeader(Header.Key, Header.Value);
	}

	FTCHARToUTF8 Converter(*RequestBody);
	TArray<uint8> RequestBodyBytes;
	RequestBodyBytes.Append((const uint8*)Converter.Get(), Converter.Length());

	// 检查请求体大小
	const int32 BodySizeMB = RequestBodyBytes.Num() / (1024 * 1024);
	if (BodySizeMB > ImageToUMGConstants::MaxRequestBodySizeMB)
	{
		UE_LOG(LogImageToUMG, Warning, TEXT("Request body is very large: %d MB. This may cause network issues."), BodySizeMB);
	}

	Request->SetContent(RequestBodyBytes);

	// 日志
	UE_LOG(LogImageToUMG, Log, TEXT("Sending AI request: %s, Model: %s, Image: %dx%d, Body: %.2f MB"),
		*Strategy->GetDisplayName(), *Settings->ModelName, ImageWidth, ImageHeight,
		RequestBodyBytes.Num() / (1024.0f * 1024.0f));
	UE_LOG(LogImageToUMG, Log, TEXT("URL: %s"), *URL);
	UE_LOG(LogImageToUMG, Log, TEXT("SSL Verification: %s, Timeout: %.0f seconds"),
		bDisableSSL ? TEXT("DISABLED") : TEXT("enabled"), Settings->TimeoutSeconds);

	// 调试信息
	FString DebugInfo = FString::Printf(TEXT("Provider: %s\nModel: %s\nURL: %s"),
		*Strategy->GetDisplayName(), *Settings->ModelName, *URL);

	TWeakPtr<FHttpAIClient> WeakSelf = AsShared();

	Request->OnProcessRequestComplete().BindLambda(
		[WeakSelf, Strategy, OnSuccess, OnError, DebugInfo, bDisableSSL](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
		{
			// 恢复 SSL 设置
			if (bDisableSSL)
			{
				FPlatformHttp::VerifyPeerSslCertificate(true);
			}

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

			StrongThis->HandleResponse(HttpRequest, HttpResponse, bSucceeded, Strategy, DebugInfo, OnSuccess, OnError);
		});

	Request->ProcessRequest();
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

bool FHttpAIClient::ExtractJsonFromResponse(
	const FString& Response,
	TSharedPtr<IAIProviderStrategy> Strategy,
	FString& OutJson,
	FString& OutError)
{
	TSharedPtr<FJsonObject> ResponseObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

	if (!FJsonSerializer::Deserialize(Reader, ResponseObject) || !ResponseObject.IsValid())
	{
		OutError = TEXT("Failed to parse API response");
		return false;
	}

	FString ContentText;

	// 使用策略提取内容
	if (!Strategy->ExtractContent(ResponseObject, ContentText) || ContentText.IsEmpty())
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
