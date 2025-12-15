// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

DECLARE_DELEGATE_OneParam(FOnScriptAIComplete, const FString& /* JsonResult */);
DECLARE_DELEGATE_OneParam(FOnScriptAIError, const FString& /* ErrorMessage */);

/**
 * 使用 Python 脚本处理 AI 请求
 * 绕过 UE HTTP 模块的各种网络问题
 */
class FScriptAIClient
{
public:
	/**
	 * 发送图像分析请求
	 * @param ImageData PNG 图像数据
	 * @param ImageWidth 图像宽度
	 * @param ImageHeight 图像高度
	 * @param OnComplete 成功回调
	 * @param OnError 错误回调
	 */
	void AnalyzeImage(
		const TArray<uint8>& ImageData,
		int32 ImageWidth,
		int32 ImageHeight,
		FOnScriptAIComplete OnComplete,
		FOnScriptAIError OnError);

	/** 取消当前请求 */
	void CancelRequest();

	/** 是否有请求正在进行 */
	bool IsRequestInProgress() const { return bRequestInProgress; }

private:
	/** 获取 Python 可执行文件路径 */
	FString GetPythonPath() const;

	/** 获取脚本路径 */
	FString GetScriptPath() const;

	/** 请求进行中标志 */
	bool bRequestInProgress = false;
};
