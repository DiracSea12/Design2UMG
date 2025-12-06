// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImageToUMGSettings.h"

DECLARE_DELEGATE_OneParam(FOnAIResponseReceived, const FString& /*JsonResponse*/);
DECLARE_DELEGATE_OneParam(FOnAIRequestFailed, const FString& /*ErrorMessage*/);

/**
 * HTTP AI 客户端 - 调用多模态 LLM API 分析图片
 * 注意：调用方需持有 TSharedPtr/TSharedRef 直到回调完成
 */
class FHttpAIClient : public TSharedFromThis<FHttpAIClient>
{
public:
	/**
	 * 发送图片到 LLM 并获取 JSON 响应
	 * @param ImageData 图片的原始数据
	 * @param ImageFormat 图片格式 (png, jpeg, etc.)
	 * @param ImageWidth 图片宽度
	 * @param ImageHeight 图片高度
	 * @param OnSuccess 成功回调
	 * @param OnError 失败回调
	 */
	void AnalyzeImage(
		const TArray<uint8>& ImageData,
		const FString& ImageFormat,
		int32 ImageWidth,
		int32 ImageHeight,
		FOnAIResponseReceived OnSuccess,
		FOnAIRequestFailed OnError
	);

private:
	/** 处理 HTTP 响应 */
	void HandleResponse(
		TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> HttpRequest,
		TSharedPtr<class IHttpResponse, ESPMode::ThreadSafe> HttpResponse,
		bool bSucceeded,
		EAIProvider Provider,
		const FString& DebugInfo,
		FOnAIResponseReceived OnSuccess,
		FOnAIRequestFailed OnError);

	/** 构建请求体 */
	FString BuildRequestBody(EAIProvider Provider, const FString& Base64Image, const FString& MimeType, const FString& Prompt, int32 MaxTokens, const FString& ModelName);

	/** 获取 System Prompt */
	FString GetSystemPrompt(int32 ImageWidth, int32 ImageHeight) const;

	/** 从嵌套 JSON 提取字符串值 */
	static bool GetNestedString(const TSharedPtr<class FJsonObject>& Obj, const TArray<FString>& Path, FString& OutValue);

	/** 从响应中提取 JSON */
	bool ExtractJsonFromResponse(const FString& Response, EAIProvider Provider, FString& OutJson, FString& OutError);

	/** JSON 序列化辅助 */
	static FString SerializeJson(const TSharedPtr<class FJsonObject>& JsonObject);
};
