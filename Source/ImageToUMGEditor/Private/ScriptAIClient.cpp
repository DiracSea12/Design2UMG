// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScriptAIClient.h"
#include "ImageToUMGSettings.h"
#include "ImageToUMGLog.h"
#include "JsonExtractUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "HAL/PlatformProcess.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"

void FScriptAIClient::AnalyzeImage(
	const TArray<uint8>& ImageData,
	int32 ImageWidth,
	int32 ImageHeight,
	FOnScriptAIComplete OnComplete,
	FOnScriptAIError OnError)
{
	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("已有请求正在进行"));
		return;
	}

	UImageToUMGSettings* Settings = UImageToUMGSettings::Get();
	if (!Settings || Settings->APIKey.IsEmpty())
	{
		OnError.ExecuteIfBound(TEXT("未配置 API Key"));
		return;
	}

	// 检测提供商
	EAIProvider Provider = Settings->Provider;
	if (Provider == EAIProvider::Auto)
	{
		Provider = UImageToUMGSettings::DetectProviderFromAPIKey(Settings->APIKey);
	}
	if (Provider == EAIProvider::Auto || Provider == EAIProvider::Custom)
	{
		OnError.ExecuteIfBound(TEXT("无法识别 AI 提供商"));
		return;
	}

	// 构建配置 JSON
	TSharedRef<FJsonObject> ConfigJson = MakeShared<FJsonObject>();
	ConfigJson->SetStringField(TEXT("provider"), UImageToUMGSettings::GetProviderName(Provider).ToLower());
	ConfigJson->SetStringField(TEXT("api_key"), Settings->APIKey);
	ConfigJson->SetStringField(TEXT("model"), Settings->ModelName);
	ConfigJson->SetStringField(TEXT("base_url"), Settings->APIHost);
	ConfigJson->SetNumberField(TEXT("timeout"), Settings->TimeoutSeconds);
	ConfigJson->SetNumberField(TEXT("max_tokens"), Settings->MaxTokens);
	ConfigJson->SetStringField(TEXT("system_prompt"), Settings->GetFormattedSystemPrompt(ImageWidth, ImageHeight));
	ConfigJson->SetStringField(TEXT("image_base64"), FBase64::Encode(ImageData));

	if (!Settings->HttpProxy.IsEmpty())
	{
		FString Proxy = Settings->HttpProxy;
		if (!Proxy.StartsWith(TEXT("http")))
		{
			Proxy = TEXT("http://") + Proxy;
		}
		ConfigJson->SetStringField(TEXT("proxy"), Proxy);
	}

	// 调试模式：保存请求体到文件
	if (Settings->bDebugSaveRequestBody)
	{
		ConfigJson->SetBoolField(TEXT("debug"), true);
	}

	// 写入临时配置文件
	FString ConfigJson_Str;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ConfigJson_Str);
	FJsonSerializer::Serialize(ConfigJson, Writer);

	FString TempDir = FPaths::ProjectSavedDir() / TEXT("ImageToUMG");
	IFileManager::Get().MakeDirectory(*TempDir, true);

	FString ConfigFile = TempDir / TEXT("ai_request_config.json");
	FString OutputFile = TempDir / TEXT("ai_request_output.json");

	if (!FFileHelper::SaveStringToFile(ConfigJson_Str, *ConfigFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OnError.ExecuteIfBound(TEXT("无法写入配置文件"));
		return;
	}

	// 删除旧的输出文件
	IFileManager::Get().Delete(*OutputFile);

	bRequestInProgress = true;

	// 在后台线程执行 Python 脚本
	FString PythonPath = GetPythonPath();
	FString ScriptPath = GetScriptPath();

	UE_LOG(LogImageToUMG, Log, TEXT("Executing Python script: %s %s"), *PythonPath, *ScriptPath);

	Async(EAsyncExecution::Thread, [this, PythonPath, ScriptPath, ConfigFile, OutputFile, OnComplete, OnError]()
	{
		FString Args = FString::Printf(TEXT("\"%s\" \"%s\""), *ScriptPath, *ConfigFile);
		FString StdOut;
		FString StdErr;
		int32 ReturnCode = -1;

		// 执行脚本并捕获输出
		bool bSuccess = FPlatformProcess::ExecProcess(
			*PythonPath,
			*Args,
			&ReturnCode,
			&StdOut,
			&StdErr
		);

		// 回到游戏线程处理结果
		AsyncTask(ENamedThreads::GameThread, [this, bSuccess, ReturnCode, StdOut, StdErr, OnComplete, OnError]()
		{
			bRequestInProgress = false;

			// 始终输出 stderr 到日志（包含调试信息）
			if (!StdErr.IsEmpty())
			{
				UE_LOG(LogImageToUMG, Log, TEXT("[Python] %s"), *StdErr);
			}

			if (!bSuccess)
			{
				OnError.ExecuteIfBound(FString::Printf(TEXT("执行 Python 脚本失败: %s"), *StdErr));
				return;
			}

			// 解析输出
			TSharedPtr<FJsonObject> ResultJson;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StdOut);

			if (!FJsonSerializer::Deserialize(Reader, ResultJson) || !ResultJson.IsValid())
			{
				OnError.ExecuteIfBound(FString::Printf(TEXT("解析脚本输出失败: %s"), *StdOut.Left(500)));
				return;
			}

			// 检查错误
			FString ErrorMsg;
			if (ResultJson->TryGetStringField(TEXT("error"), ErrorMsg))
			{
				FString Details;
				ResultJson->TryGetStringField(TEXT("details"), Details);
				OnError.ExecuteIfBound(Details.IsEmpty() ? ErrorMsg : FString::Printf(TEXT("%s: %s"), *ErrorMsg, *Details));
				return;
			}

			// 获取内容并提取 JSON
			FString Content;
			if (ResultJson->TryGetStringField(TEXT("content"), Content))
			{
				FString ExtractedJson, ExtractError;
				if (FJsonExtractUtils::ExtractJson(Content, ExtractedJson, ExtractError))
				{
					OnComplete.ExecuteIfBound(ExtractedJson);
				}
				else
				{
					OnError.ExecuteIfBound(FString::Printf(TEXT("JSON 提取失败: %s\n\n原始内容:\n%s"), *ExtractError, *Content.Left(500)));
				}
			}
			else
			{
				OnError.ExecuteIfBound(TEXT("响应中没有 content 字段"));
			}
		});
	});
}

void FScriptAIClient::CancelRequest()
{
	// Note: ExecProcess is blocking, so we can only prevent result processing
	// The Python process will continue until completion
	bRequestInProgress = false;
}

FString FScriptAIClient::GetPythonPath() const
{
	// 优先使用系统 Python
#if PLATFORM_WINDOWS
	// 尝试常见路径
	TArray<FString> PythonPaths = {
		TEXT("python"),
		TEXT("python3"),
		TEXT("C:\\Python312\\python.exe"),
		TEXT("C:\\Python311\\python.exe"),
		TEXT("C:\\Python310\\python.exe"),
		TEXT("C:\\Python39\\python.exe"),
		FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")), TEXT("Programs/Python/Python312/python.exe")),
		FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")), TEXT("Programs/Python/Python311/python.exe")),
	};

	for (const FString& Path : PythonPaths)
	{
		if (Path == TEXT("python") || Path == TEXT("python3"))
		{
			// 检查是否在 PATH 中
			FString FullPath;
			int32 ReturnCode = -1;
			if (FPlatformProcess::ExecProcess(TEXT("where"), *Path, &ReturnCode, &FullPath, nullptr))
			{
				// where 命令返回 0 表示找到，非 0 表示未找到
				if (ReturnCode == 0 && !FullPath.IsEmpty())
				{
					return Path;
				}
			}
		}
		else if (FPaths::FileExists(Path))
		{
			return Path;
		}
	}
#else
	return TEXT("python3");
#endif

	return TEXT("python");
}

FString FScriptAIClient::GetScriptPath() const
{
	// 脚本在插件目录下
	FString PluginDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Marketplace/ImageToUMG"))
	);
	return FPaths::Combine(PluginDir, TEXT("Scripts/ai_request.py"));
}
