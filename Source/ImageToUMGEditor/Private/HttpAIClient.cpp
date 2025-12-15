// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpAIClient.h"
#include "AIProviderStrategy.h"
#include "ImageToUMGSettings.h"
#include "ImageToUMGLog.h"
#include "ImageToUMGConstants.h"
#include "JsonExtractUtils.h"
#include "HttpModule.h"
#include "Http.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Editor.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Async/Async.h"

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

	/**
	 * 掩码URL中的敏感参数（如key=xxx, token=xxx等）
	 * 避免在日志中泄露密钥
	 */
	FString MaskSensitiveURL(const FString& URL)
	{
		FString Result = URL;

		// 常见的敏感参数名
		const TArray<FString> SensitiveParams = { TEXT("key"), TEXT("token"), TEXT("api_key"), TEXT("apikey"), TEXT("secret") };

		for (const FString& Param : SensitiveParams)
		{
			// 匹配 param=value 格式
			FString Pattern = Param + TEXT("=");
			int32 StartIdx = Result.Find(Pattern, ESearchCase::IgnoreCase);
			if (StartIdx != INDEX_NONE)
			{
				int32 ValueStart = StartIdx + Pattern.Len();
				int32 ValueEnd = Result.Find(TEXT("&"), ESearchCase::IgnoreCase, ESearchDir::FromStart, ValueStart);
				if (ValueEnd == INDEX_NONE)
				{
					ValueEnd = Result.Len();
				}
				// 替换值为 ***
				Result = Result.Left(ValueStart) + TEXT("***") + Result.Mid(ValueEnd);
			}
		}

		return Result;
	}

	/**
	 * 判断HTTP错误是否可重试
	 * 可重试的情况：网络错误、超时、服务器错误(5xx)、限流(429)
	 */
	bool IsRetryableError(bool bSucceeded, int32 ResponseCode)
	{
		// 网络层失败
		if (!bSucceeded)
		{
			return true;
		}

		// 服务器错误 (5xx)
		if (ResponseCode >= 500 && ResponseCode < 600)
		{
			return true;
		}

		// 限流 (429 Too Many Requests)
		if (ResponseCode == 429)
		{
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
	const int32 ResponseCode = HttpResponse.IsValid() ? HttpResponse->GetResponseCode() : 0;
	const bool bIsRetryable = IsRetryableError(bSucceeded, ResponseCode);
	const FString RetryHint = bIsRetryable ? TEXT("\n\n[This error is typically temporary. Please try again.]") : TEXT("");

	if (!bSucceeded)
	{
		FString ErrorDetail = TEXT("Unknown error");
		if (HttpRequest.IsValid())
		{
			ErrorDetail = GetHttpStatusDescription(HttpRequest->GetStatus());
			if (HttpResponse.IsValid())
			{
				ErrorDetail += FString::Printf(TEXT(" (HTTP %d)"), ResponseCode);
			}
		}
		UE_LOG(LogImageToUMG, Error, TEXT("HTTP Request Failed: %s - %s"), *DebugInfo, *ErrorDetail);
		OnError.ExecuteIfBound(FString::Printf(TEXT("HTTP request failed: %s\n\n%s%s"), *ErrorDetail, *DebugInfo, *RetryHint));
		return;
	}

	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogImageToUMG, Error, TEXT("Invalid HTTP response"));
		OnError.ExecuteIfBound(TEXT("Invalid HTTP response") + RetryHint);
		return;
	}

	const FString ResponseBody = HttpResponse->GetContentAsString();

	UE_LOG(LogImageToUMG, Verbose, TEXT("Response: HTTP %d, %d bytes"), ResponseCode, ResponseBody.Len());

	if (ResponseCode != 200)
	{
		// 特殊处理429限流错误
		FString ErrorMsg;
		if (ResponseCode == 429)
		{
			ErrorMsg = FString::Printf(TEXT("Rate limited by API (HTTP 429)\n\nPlease wait a moment and try again.\n\n%s"),
				*DebugInfo);
		}
		else
		{
			ErrorMsg = FString::Printf(TEXT("API Error (HTTP %d)\n\n%s\n\nResponse:\n%s%s"),
				ResponseCode, *DebugInfo, *ResponseBody.Left(ImageToUMGConstants::ErrorResponseDetailLen), *RetryHint);
		}
		UE_LOG(LogImageToUMG, Error, TEXT("API Error (HTTP %d): %s"), ResponseCode, *ResponseBody.Left(ImageToUMGConstants::ErrorResponsePreviewLen));
		OnError.ExecuteIfBound(ErrorMsg);
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
	// 调用内部实现，初始重试次数为0
	SendRequestInternal(ImageData, ImageFormat, ImageWidth, ImageHeight, OnSuccess, OnError, 0);
}

// ============================================================================
// 拆分后的辅助函数实现
// ============================================================================

TSharedPtr<IAIProviderStrategy> FHttpAIClient::ValidateAndGetStrategy(FOnAIRequestFailed& OnError) const
{
	const UImageToUMGSettings* Settings = UImageToUMGSettings::Get();

	if (Settings->APIKey.IsEmpty())
	{
		OnError.ExecuteIfBound(TEXT("API Key is not configured"));
		return nullptr;
	}

	// 确定实际提供商
	EAIProvider Provider = Settings->Provider;
	if (Provider == EAIProvider::Auto)
	{
		Provider = UImageToUMGSettings::DetectProviderFromAPIKey(Settings->APIKey);
		if (Provider == EAIProvider::Auto)
		{
			OnError.ExecuteIfBound(TEXT("Could not detect AI provider from API key.\nPlease select a provider manually in Project Settings > Plugins > ImageToUMG."));
			return nullptr;
		}
	}

	// 获取策略
	TSharedPtr<IAIProviderStrategy> Strategy = FAIProviderStrategyFactory::GetStrategy(Provider);
	if (!Strategy.IsValid())
	{
		OnError.ExecuteIfBound(FString::Printf(TEXT("Unsupported AI provider: %s"),
			*UImageToUMGSettings::GetProviderName(Provider)));
		return nullptr;
	}

	// 校验 ModelName（除 Custom 外都需要）
	if (Settings->ModelName.IsEmpty() && Provider != EAIProvider::Custom)
	{
		OnError.ExecuteIfBound(FString::Printf(TEXT("No model specified for %s.\nPlease select a model in Project Settings > Plugins > ImageToUMG."),
			*Strategy->GetDisplayName()));
		return nullptr;
	}

	return Strategy;
}

FString FHttpAIClient::BuildRequestURL(TSharedPtr<IAIProviderStrategy> Strategy) const
{
	const UImageToUMGSettings* Settings = UImageToUMGSettings::Get();

	FString Endpoint = Settings->APIHost.IsEmpty()
		? UImageToUMGSettings::GetProviderEndpoint(Settings->Provider == EAIProvider::Auto
			? UImageToUMGSettings::DetectProviderFromAPIKey(Settings->APIKey) : Settings->Provider)
		: Settings->APIHost;

	// 处理 APIHost 的智能拼接标记
	if (Endpoint.EndsWith(TEXT("#")))
	{
		Endpoint = Endpoint.LeftChop(1);
	}

	return Strategy->BuildURL(Endpoint, Settings->ModelName);
}

FString FHttpAIClient::BuildRequestBody(
	TSharedPtr<IAIProviderStrategy> Strategy,
	const FString& Base64Image,
	const FString& MimeType,
	int32 ImageWidth,
	int32 ImageHeight) const
{
	const UImageToUMGSettings* Settings = UImageToUMGSettings::Get();
	const FString Prompt = Settings->GetFormattedSystemPrompt(ImageWidth, ImageHeight);

	TSharedPtr<FJsonObject> RequestBodyJson = Strategy->BuildRequestBody(
		Base64Image, MimeType, Prompt, Settings->MaxTokens, Settings->ModelName);

	return SerializeJson(RequestBodyJson);
}

bool FHttpAIClient::ConfigureProxySettings(FString& OutOriginalProxy) const
{
	const UImageToUMGSettings* Settings = UImageToUMGSettings::Get();
	const bool bUseCustomProxy = !Settings->HttpProxy.IsEmpty();
	const bool bBypassProxy = Settings->bBypassSystemProxy;

	// 警告：UE HTTP 模块只支持模块级别的代理设置。
	// 并发请求可能会受影响。如需可靠的代理控制，请在插件设置中启用"使用 Python 脚本"。

	if (bBypassProxy)
	{
		OutOriginalProxy = FHttpModule::Get().GetProxyAddress();
		FHttpModule::Get().SetProxyAddress(TEXT(""));
		// 注意：不再修改 NO_PROXY 环境变量，因为会影响整个进程
		UE_LOG(LogImageToUMG, Verbose, TEXT("绕过系统代理 (原: %s)。注意：这会影响所有并发的HTTP请求。"),
			OutOriginalProxy.IsEmpty() ? TEXT("无") : *OutOriginalProxy);
		return true;
	}

	if (bUseCustomProxy)
	{
		OutOriginalProxy = FHttpModule::Get().GetProxyAddress();
		FHttpModule::Get().SetProxyAddress(Settings->HttpProxy);
		UE_LOG(LogImageToUMG, Verbose, TEXT("使用自定义HTTP代理: %s。注意：这会影响所有并发的HTTP请求。"), *Settings->HttpProxy);
		return true;
	}

	return false;
}

void FHttpAIClient::RestoreProxySettings(const FString& OriginalProxy)
{
	FHttpModule::Get().SetProxyAddress(OriginalProxy);
}

void FHttpAIClient::LogRequestDiagnostics(
	TSharedPtr<IAIProviderStrategy> Strategy,
	const FString& URL,
	int32 BodySizeBytes) const
{
	const UImageToUMGSettings* Settings = UImageToUMGSettings::Get();
	const FString& ProxyAddress = FHttpModule::Get().GetProxyAddress();

	UE_LOG(LogImageToUMG, Log, TEXT("=== AI Request ==="));
	UE_LOG(LogImageToUMG, Log, TEXT("  Provider: %s, Model: %s"), *Strategy->GetDisplayName(), *Settings->ModelName);
	UE_LOG(LogImageToUMG, Log, TEXT("  URL: %s"), *MaskSensitiveURL(URL));
	UE_LOG(LogImageToUMG, Log, TEXT("  Body: %.2f MB, Timeout: %.0fs, Proxy: %s"),
		BodySizeBytes / (1024.0f * 1024.0f), Settings->TimeoutSeconds,
		ProxyAddress.IsEmpty() ? TEXT("none") : *ProxyAddress);
}

void FHttpAIClient::SendRequestInternal(
	const TArray<uint8>& ImageData,
	const FString& ImageFormat,
	int32 ImageWidth,
	int32 ImageHeight,
	FOnAIResponseReceived OnSuccess,
	FOnAIRequestFailed OnError,
	int32 RetryCount)
{
	// 1. 验证设置并获取策略
	TSharedPtr<IAIProviderStrategy> Strategy = ValidateAndGetStrategy(OnError);
	if (!Strategy.IsValid()) return;

	// 2. 构建 URL 和请求体
	const FString URL = BuildRequestURL(Strategy);
	const FString Base64Image = FBase64::Encode(ImageData);
	const FString MimeType = ImageFormat.ToLower() == TEXT("png") ? TEXT("image/png") : TEXT("image/jpeg");
	const FString RequestBody = BuildRequestBody(Strategy, Base64Image, MimeType, ImageWidth, ImageHeight);

	// 3. 调试：保存请求体到文件
	const UImageToUMGSettings* Settings = UImageToUMGSettings::Get();
	if (Settings->bDebugSaveRequestBody)
	{
		const FString DebugDir = FPaths::ProjectSavedDir() / TEXT("ImageToUMG/Debug");
		IFileManager::Get().MakeDirectory(*DebugDir, true);
		FFileHelper::SaveStringToFile(RequestBody, *(DebugDir / TEXT("last_request_body.json")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	// 4. 配置代理
	FString OriginalProxy;
	const bool bNeedRestoreProxy = ConfigureProxySettings(OriginalProxy);

	// 5. 创建 HTTP 请求
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("POST"));
	Request->SetTimeout(FMath::Max(ImageToUMGConstants::DefaultTimeoutSeconds, Settings->TimeoutSeconds));

	// 设置 Headers
	Request->SetHeader(TEXT("Connection"), TEXT("keep-alive"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("UnrealEngine/5.0"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept-Encoding"), TEXT("identity"));

	for (const auto& Header : Strategy->GetHeaders(Settings->APIKey))
	{
		Request->SetHeader(Header.Key, Header.Value);
	}

	// 设置请求体
	FTCHARToUTF8 Converter(*RequestBody);
	TArray<uint8> RequestBodyBytes;
	RequestBodyBytes.Append((const uint8*)Converter.Get(), Converter.Length());
	Request->SetContent(RequestBodyBytes);

	// 6. 记录诊断日志
	LogRequestDiagnostics(Strategy, URL, RequestBodyBytes.Num());

	// 7. 设置完成回调
	FString DebugInfo = FString::Printf(TEXT("Provider: %s\nModel: %s\nURL: %s"),
		*Strategy->GetDisplayName(), *Settings->ModelName, *MaskSensitiveURL(URL));

	TWeakPtr<FHttpAIClient> WeakSelf = AsShared();
	TArray<uint8> CapturedImageData = ImageData;
	FString CapturedImageFormat = ImageFormat;

	Request->OnProcessRequestComplete().BindLambda(
		[WeakSelf, Strategy, OnSuccess, OnError, DebugInfo, bNeedRestoreProxy, OriginalProxy,
		 CapturedImageData, CapturedImageFormat, ImageWidth, ImageHeight, RetryCount]
		(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
	{
		// 恢复代理设置
		if (bNeedRestoreProxy)
		{
			RestoreProxySettings(OriginalProxy);
		}

		TSharedPtr<FHttpAIClient> StrongThis = WeakSelf.Pin();
		if (!StrongThis.IsValid())
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("FHttpAIClient destroyed before callback"));
			return;
		}

		// 检查是否需要重试（仅网络层失败）
		const bool bIsNetworkError = !bSucceeded && HttpRequest.IsValid() &&
			HttpRequest->GetStatus() == EHttpRequestStatus::Failed;

		if (bIsNetworkError && RetryCount < ImageToUMGConstants::MaxRetryCount)
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("Network error, retry %d/%d in %.1fs..."),
				RetryCount + 1, ImageToUMGConstants::MaxRetryCount, ImageToUMGConstants::RetryDelaySeconds);

			if (GEditor)
			{
				FTimerHandle RetryTimer;
				GEditor->GetTimerManager()->SetTimer(RetryTimer,
					FTimerDelegate::CreateLambda([WeakSelf, CapturedImageData, CapturedImageFormat,
						ImageWidth, ImageHeight, OnSuccess, OnError, RetryCount]()
					{
						if (TSharedPtr<FHttpAIClient> RetryThis = WeakSelf.Pin())
						{
							RetryThis->SendRequestInternal(CapturedImageData, CapturedImageFormat,
								ImageWidth, ImageHeight, OnSuccess, OnError, RetryCount + 1);
						}
					}),
					ImageToUMGConstants::RetryDelaySeconds, false);
			}
			return;
		}

		// 记录失败详情
		if (!bSucceeded && HttpRequest.IsValid())
		{
			UE_LOG(LogImageToUMG, Error, TEXT("HTTP Failed: Status=%d, Reason=%s, Elapsed=%.2fs"),
				static_cast<int32>(HttpRequest->GetStatus()),
				LexToString(HttpRequest->GetFailureReason()),
				HttpRequest->GetElapsedTime());
		}

		StrongThis->HandleResponse(HttpRequest, HttpResponse, bSucceeded, Strategy, DebugInfo, OnSuccess, OnError);
	});

	// 8. 发送请求
	Request->ProcessRequest();
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
