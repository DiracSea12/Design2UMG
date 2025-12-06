// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Modules/ModuleManager.h"
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

	/** 缓存的图像生成模型列表 */
	UPROPERTY()
	TArray<FString> CachedImageModels;

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

	// ========== 调试选项 ==========

	/** 禁用SSL证书验证（仅用于调试网络问题，生产环境请保持关闭） */
	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (DisplayName = "Disable SSL Verification"))
	bool bDisableSSLVerification = false;

	/** 获取默认系统提示词 */
	static FString GetDefaultSystemPrompt();

	/** 获取可用模型下拉选项（供 GetOptions 使用） */
	UFUNCTION()
	TArray<FString> GetAvailableModels() const;


	/** 检测 API Key 对应的提供商 */
	static EAIProvider DetectProviderFromAPIKey(const FString& InAPIKey);

	/** 获取提供商名称 */
	static FString GetProviderName(EAIProvider InProvider);

	/** 获取提供商的默认端点 */
	static FString GetProviderEndpoint(EAIProvider InProvider);

	/** 触发模型列表刷新（异步） */
	void RefreshAvailableModels();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
