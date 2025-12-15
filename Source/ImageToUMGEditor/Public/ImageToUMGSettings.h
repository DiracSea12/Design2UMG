// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "UMGSchema.h"
#include "ImageToUMGSettings.generated.h"

/**
 * AI 提供商枚举 - 支持多种服务商
 */
UENUM()
enum class EAIProvider : uint8
{
	Auto			UMETA(DisplayName = "Auto Detect"),
	OpenAI			UMETA(DisplayName = "OpenAI"),
	Anthropic		UMETA(DisplayName = "Anthropic"),
	Google			UMETA(DisplayName = "Google AI"),
	DeepSeek		UMETA(DisplayName = "DeepSeek"),
	Mistral			UMETA(DisplayName = "Mistral AI"),
	Groq			UMETA(DisplayName = "Groq"),
	Together		UMETA(DisplayName = "Together AI"),
	OpenRouter		UMETA(DisplayName = "OpenRouter"),
	Volcengine		UMETA(DisplayName = "Volcengine (Doubao)"),
	Custom			UMETA(DisplayName = "Custom Endpoint")
};

/**
 * Image To UMG 插件设置
 * 保存在 Editor Preferences 中（用户级别，不会提交到版本控制）
 * 路径：Edit -> Editor Preferences -> Plugins -> Image To UMG
 */
UCLASS(config=EditorPerProjectUserSettings, meta=(DisplayName="Image To UMG"))
class UImageToUMGSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 获取单例实例 */
	static UImageToUMGSettings* Get();

	/** 获取设置类别名称 - 显示在 Editor Preferences 中 */
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Image To UMG"); }

	// ========== API 配置 ==========

	/**
	 * API Key - 输入后自动识别提供商并获取可用模型
	 * 支持: OpenAI, Anthropic, Google, DeepSeek, Mistral, Groq, Together, Replicate, Stability AI, OpenRouter
	 */
	UPROPERTY(Config, EditAnywhere, Category = "API Settings", meta = (DisplayName = "API Key", PasswordField = true))
	FString APIKey;

	/** 提供商（自动检测或手动选择） */
	UPROPERTY(Config, EditAnywhere, Category = "API Settings", meta = (DisplayName = "Provider"))
	EAIProvider Provider = EAIProvider::Auto;

	/** 检测到的实际提供商（只读，由自动检测填充） */
	UPROPERTY(VisibleAnywhere, Category = "API Settings", meta = (DisplayName = "Detected Provider"))
	FString DetectedProvider;

	/** 模型名称（可从下方列表选择或手动输入 Endpoint ID） */
	UPROPERTY(Config, EditAnywhere, Category = "API Settings", meta = (DisplayName = "Model"))
	FString ModelName;

	/**
	 * API 主机地址（代理服务器）
	 * - 留空：使用官方默认地址
	 * - 填根地址：自动拼接路径（如 https://proxy.com → https://proxy.com/v1/chat/completions）
	 * - 以 / 结尾：只拼接后半段（如 https://proxy.com/v1/ → https://proxy.com/v1/chat/completions）
	 * - 以 # 结尾：不拼接，使用完整地址
	 */
	UPROPERTY(Config, EditAnywhere, Category = "API Settings", meta = (DisplayName = "API Host",
		EditCondition = "Provider != EAIProvider::Custom", EditConditionHides))
	FString APIHost;

	/** 完整 API URL 预览（只读，实时显示最终请求地址） */
	UPROPERTY(VisibleAnywhere, Category = "API Settings", meta = (DisplayName = "URL Preview",
		EditCondition = "Provider != EAIProvider::Custom", EditConditionHides))
	FString URLPreview;

	/** 自定义 API 端点（仅 Custom 模式） */
	UPROPERTY(Config, EditAnywhere, Category = "API Settings", meta = (DisplayName = "Custom Endpoint",
		EditCondition = "Provider == EAIProvider::Custom", EditConditionHides))
	FString CustomEndpoint;

	/** 最大输出 Token 数 */
	UPROPERTY(Config, EditAnywhere, Category = "API Settings", meta = (DisplayName = "Max Tokens", ClampMin = "1024", ClampMax = "32768"))
	int32 MaxTokens = 4096;

	/** HTTP 请求超时时间（秒） */
	UPROPERTY(Config, EditAnywhere, Category = "API Settings", meta = (DisplayName = "Timeout (seconds)", ClampMin = "30", ClampMax = "300"))
	float TimeoutSeconds = 120.0f;

	/** 缓存的可用模型列表 */
	UPROPERTY()
	TArray<FString> CachedModels;

	// ========== 提示词配置 ==========

	/** 系统提示词（可使用 {WIDTH} 和 {HEIGHT} 占位符） */
	UPROPERTY(Config, EditAnywhere, Category = "Prompt Settings", meta = (DisplayName = "System Prompt", MultiLine = true))
	FString SystemPrompt = GetDefaultSystemPrompt();

	/** 生成的 Widget Blueprint 默认保存路径 */
	UPROPERTY(Config, EditAnywhere, Category = "Generation Settings", meta = (DisplayName = "Default Output Path"))
	FString DefaultOutputPath = TEXT("/Game/UI/Generated");

	/** 是否在生成后自动打开 Widget Blueprint */
	UPROPERTY(Config, EditAnywhere, Category = "Generation Settings", meta = (DisplayName = "Auto Open After Generation"))
	bool bAutoOpenAfterGeneration = true;

	/** 图像资产保存路径 */
	UPROPERTY(Config, EditAnywhere, Category = "Generation Settings", meta = (DisplayName = "Image Assets Output Path"))
	FString ImageAssetsOutputPath = TEXT("/Game/UI/Generated/Textures");

	// ========== Material 预置设置 ==========

	/**
	 * 预置 Material 列表 - AI 可以引用这些 Material 来绘制形状
	 * 插件内置一些常用形状（圆形、圆环、圆角矩形等）
	 * 用户可以在此添加自定义 Material
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Material Presets", meta = (DisplayName = "Preset Materials", TitleProperty = "Name"))
	TArray<FUMGPresetMaterial> PresetMaterials;

	/** 获取默认的预置 Material 列表 */
	static TArray<FUMGPresetMaterial> GetDefaultPresetMaterials();

	/** 生成 Material 列表提示词片段（供 AI 参考） */
	FString GenerateMaterialPromptSection() const;

	/** 根据名称查找预置 Material */
	const FUMGPresetMaterial* FindPresetMaterial(const FString& Name) const;

	// ========== 网络设置 ==========

	/**
	 * HTTP 代理地址（留空使用系统代理）
	 * 格式：IP:端口，例如 127.0.0.1:7890
	 * 如果您的网络需要代理才能访问 AI API，请在此配置
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network Settings", meta = (DisplayName = "HTTP Proxy"))
	FString HttpProxy;

	/**
	 * 绕过系统代理，直接连接
	 * 如果系统有代理软件（如 Clash）导致连接失败，请勾选此选项
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network Settings", meta = (DisplayName = "Bypass System Proxy"))
	bool bBypassSystemProxy = false;

	/** 保存请求体到文件用于调试（路径：{Project}/Saved/ImageToUMG/Debug/last_request_body.json） */
	UPROPERTY(Config, EditAnywhere, Category = "Network Settings", meta = (DisplayName = "Debug: Save Request Body"))
	bool bDebugSaveRequestBody = false;

	/**
	 * 使用 Python 脚本处理 AI 请求（推荐）
	 * 完全绕过 UE 的 HTTP 模块，使用 Python requests 库
	 * 对代理和 SSL 支持更好，解决大部分网络连接问题
	 * 需要系统已安装 Python 和 requests 库（pip install requests）
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Network Settings", meta = (DisplayName = "Use Python Script (Recommended)"))
	bool bUseScriptClient = true;

	/** 获取默认系统提示词 */
	static FString GetDefaultSystemPrompt();

	/**
	 * 获取格式化后的系统提示词（替换占位符并附加 Material 列表）
	 * @param ImageWidth 图像宽度（用于替换 {WIDTH}）
	 * @param ImageHeight 图像高度（用于替换 {HEIGHT}）
	 * @return 完整的系统提示词
	 */
	FString GetFormattedSystemPrompt(int32 ImageWidth, int32 ImageHeight) const;

	/** 获取可用模型下拉选项（供 GetOptions 使用） */
	UFUNCTION()
	TArray<FString> GetAvailableModels() const;


	/** 检测 API Key 对应的提供商 */
	static EAIProvider DetectProviderFromAPIKey(const FString& InAPIKey);

	/** 获取提供商名称 */
	static FString GetProviderName(EAIProvider InProvider);

	/** 获取提供商的默认端点 */
	static FString GetProviderEndpoint(EAIProvider InProvider);

	/** 构建完整 API URL（智能拼接） */
	static FString BuildAPIURL(EAIProvider InProvider, const FString& Suffix);

	/** 获取提供商对应的端点后缀 */
	static FString GetEndpointSuffixForProvider(EAIProvider InProvider);

	/** 更新 URL 预览 */
	void UpdateURLPreview();

	/** 触发模型列表刷新（异步） */
	void RefreshAvailableModels();

private:
	/** 模型列表刷新防抖句柄 */
	FTSTicker::FDelegateHandle ModelRefreshDebounceHandle;

	/** 取消待处理的防抖刷新 */
	void CancelPendingModelRefresh();

	/** 调度模型列表刷新（带防抖） */
	void ScheduleModelRefresh();

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
