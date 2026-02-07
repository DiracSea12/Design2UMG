// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageToUMGSettings.h"
#include "ImageToUMGLog.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UImageToUMGSettings* UImageToUMGSettings::Get()
{
	UImageToUMGSettings* Settings = GetMutableDefault<UImageToUMGSettings>();

	// 首次使用时初始化默认 Material 列表
	if (Settings && Settings->PresetMaterials.Num() == 0)
	{
		Settings->PresetMaterials = GetDefaultPresetMaterials();
	}

	return Settings;
}

EAIProvider UImageToUMGSettings::DetectProviderFromAPIKey(const FString& InAPIKey)
{
	if (InAPIKey.IsEmpty()) return EAIProvider::Auto;

	// 根据 API Key 前缀识别提供商
	if (InAPIKey.StartsWith(TEXT("sk-ant-"))) return EAIProvider::Anthropic;
	if (InAPIKey.StartsWith(TEXT("AIza"))) return EAIProvider::Google;
	if (InAPIKey.StartsWith(TEXT("gsk_"))) return EAIProvider::Groq;
	if (InAPIKey.StartsWith(TEXT("sk-or-"))) return EAIProvider::OpenRouter;
	if (InAPIKey.StartsWith(TEXT("sk-")))
	{
		// sk- 可能是 OpenAI, DeepSeek, 或其他兼容服务
		// 需要进一步验证，默认假设 OpenAI
		return EAIProvider::OpenAI;
	}

	// 无法识别，返回 Auto
	return EAIProvider::Auto;
}

FString UImageToUMGSettings::GetProviderName(EAIProvider InProvider)
{
	switch (InProvider)
	{
	case EAIProvider::Auto: return TEXT("Auto Detect");
	case EAIProvider::OpenAI: return TEXT("OpenAI");
	case EAIProvider::Anthropic: return TEXT("Anthropic");
	case EAIProvider::Google: return TEXT("Google");
	case EAIProvider::DeepSeek: return TEXT("DeepSeek");
	case EAIProvider::Mistral: return TEXT("Mistral");
	case EAIProvider::Groq: return TEXT("Groq");
	case EAIProvider::Together: return TEXT("Together");
	case EAIProvider::OpenRouter: return TEXT("OpenRouter");
	case EAIProvider::Volcengine: return TEXT("Volcengine");
	case EAIProvider::Custom: return TEXT("Custom");
	default: return TEXT("Unknown");
	}
}

FString UImageToUMGSettings::GetProviderEndpoint(EAIProvider InProvider)
{
	switch (InProvider)
	{
	case EAIProvider::OpenAI: return TEXT("https://api.openai.com/v1");
	case EAIProvider::Anthropic: return TEXT("https://api.anthropic.com/v1");
	case EAIProvider::Google: return TEXT("https://generativelanguage.googleapis.com/v1beta");
	case EAIProvider::DeepSeek: return TEXT("https://api.deepseek.com/v1");
	case EAIProvider::Mistral: return TEXT("https://api.mistral.ai/v1");
	case EAIProvider::Groq: return TEXT("https://api.groq.com/openai/v1");
	case EAIProvider::Together: return TEXT("https://api.together.xyz/v1");
	case EAIProvider::OpenRouter: return TEXT("https://openrouter.ai/api/v1");
	case EAIProvider::Volcengine: return TEXT("https://ark.cn-beijing.volces.com/api/v3");
	default: return TEXT("");
	}
}

FString UImageToUMGSettings::GetEndpointSuffixForProvider(EAIProvider InProvider)
{
	switch (InProvider)
	{
	case EAIProvider::OpenAI:
	case EAIProvider::DeepSeek:
	case EAIProvider::Mistral:
	case EAIProvider::Groq:
	case EAIProvider::Together:
	case EAIProvider::OpenRouter:
		return TEXT("/chat/completions");
	case EAIProvider::Anthropic:
		return TEXT("/messages");
	case EAIProvider::Volcengine:
		return TEXT("/responses");
	case EAIProvider::Google:
		return TEXT("");  // Google 使用特殊 URL 构建
	default:
		return TEXT("");
	}
}

FString UImageToUMGSettings::BuildAPIURL(EAIProvider InProvider, const FString& Suffix)
{
	UImageToUMGSettings* Settings = Get();
	FString UserHost = Settings ? Settings->APIHost : TEXT("");
	FString DefaultEndpoint = GetProviderEndpoint(InProvider);

	// 留空用默认
	if (UserHost.IsEmpty())
	{
		return DefaultEndpoint + Suffix;
	}

	// 以 # 结尾：不拼接
	if (UserHost.EndsWith(TEXT("#")))
	{
		return UserHost.LeftChop(1);
	}

	// 以 / 结尾：去掉后缀开头的 /
	if (UserHost.EndsWith(TEXT("/")))
	{
		return UserHost + (Suffix.StartsWith(TEXT("/")) ? Suffix.Mid(1) : Suffix);
	}

	// 普通情况：直接拼接
	return UserHost + Suffix;
}

void UImageToUMGSettings::UpdateURLPreview()
{
	EAIProvider ActualProvider = (Provider == EAIProvider::Auto)
		? DetectProviderFromAPIKey(APIKey)
		: Provider;

	if (ActualProvider == EAIProvider::Custom || ActualProvider == EAIProvider::Auto)
	{
		// 如果在这个状态下勾选了 CustomizeURL，我们保留当前的 URLPreview
		if (!bCustomizeURL)
		{
			URLPreview = TEXT("(Select provider to preview)");
		}
		return;
	}

	// 如果开启了自定义 URL，不再自动更新预览
	if (bCustomizeURL)
	{
		return;
	}

	if (ActualProvider == EAIProvider::Google)
	{
		// Google 使用 x-goog-api-key header 传递密钥
		FString Endpoint = APIHost.IsEmpty() ? GetProviderEndpoint(ActualProvider) : APIHost;
		URLPreview = FString::Printf(TEXT("%s/models/{MODEL}:generateContent (key via header)"), *Endpoint);
		return;
	}

	FString Suffix = GetEndpointSuffixForProvider(ActualProvider);
	URLPreview = BuildAPIURL(ActualProvider, Suffix);
}

TArray<FString> UImageToUMGSettings::GetAvailableModels() const
{
	// 完全动态：如果有缓存就返回，否则提示用户刷新
	if (CachedModels.Num() > 0)
	{
		return CachedModels;
	}

	// 返回提示信息，不写死任何模型
	return { TEXT("(Click 'Refresh Models' or enter API Key to load models)") };
}


// ============================================================================
// 模型列表解析（按提供商拆分）
// ============================================================================

namespace
{
	struct FModelParseResult
	{
		TArray<FString> TextModels;
		TArray<FString> ImageModels;
	};

	/**
	 * 验证URL格式和安全性
	 * @return 空字符串表示有效，否则返回错误信息
	 */
	FString ValidateEndpointURL(const FString& URL)
	{
		if (URL.IsEmpty())
		{
			return TEXT(""); // 空URL是允许的（使用默认）
		}

		// 移除末尾的 # 和 /（用于智能拼接）
		FString CleanURL = URL;
		if (CleanURL.EndsWith(TEXT("#")))
		{
			CleanURL = CleanURL.LeftChop(1);
		}
		if (CleanURL.EndsWith(TEXT("/")))
		{
			CleanURL = CleanURL.LeftChop(1);
		}

		// 检查协议
		if (!CleanURL.StartsWith(TEXT("https://")) && !CleanURL.StartsWith(TEXT("http://")))
		{
			return TEXT("URL must start with https:// or http://");
		}

		// 警告HTTP（不安全）
		if (CleanURL.StartsWith(TEXT("http://")) && !CleanURL.Contains(TEXT("localhost")) && !CleanURL.Contains(TEXT("127.0.0.1")))
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("Using insecure HTTP protocol for API endpoint. Consider using HTTPS."));
		}

		// 基本格式检查
		int32 SchemeEnd = CleanURL.Find(TEXT("://"));
		if (SchemeEnd == INDEX_NONE || SchemeEnd + 3 >= CleanURL.Len())
		{
			return TEXT("Invalid URL format");
		}

		// 检查是否有域名部分
		FString AfterScheme = CleanURL.Mid(SchemeEnd + 3);
		if (AfterScheme.IsEmpty() || AfterScheme.StartsWith(TEXT("/")))
		{
			return TEXT("URL must include a domain name");
		}

		return TEXT(""); // 有效
	}

	// 统一的数组字符串查找（bExact=true 精确匹配，false 包含匹配）
	bool ArrayFind(const TArray<TSharedPtr<FJsonValue>>* Arr, const FString& Search, bool bExact = true)
	{
		if (!Arr) return false;
		for (const auto& V : *Arr)
		{
			const FString& S = V->AsString();
			if (bExact ? (S == Search) : S.Contains(Search)) return true;
		}
		return false;
	}

	// 从 data 数组提取模型 ID（通用格式）
	void ExtractModelIds(const TSharedPtr<FJsonObject>& Json, const FString& ArrayName, const FString& IdField, TArray<FString>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (!Json->TryGetArrayField(ArrayName, Arr)) return;
		for (const auto& M : *Arr)
		{
			FString Id;
			if (M->AsObject()->TryGetStringField(IdField, Id)) Out.Add(Id);
		}
	}

	// 解析单个 Google 模型条目
	void ParseGoogleModel(const TSharedPtr<FJsonObject>& Obj, FModelParseResult& Out)
	{
		FString Name;
		if (!Obj->TryGetStringField(TEXT("name"), Name)) return;
		Name.RemoveFromStart(TEXT("models/"));

		const TArray<TSharedPtr<FJsonValue>>* Methods;
		Obj->TryGetArrayField(TEXT("supportedGenerationMethods"), Methods);

		bool bText = ArrayFind(Methods, TEXT("generateContent"), false);
		bool bImage = ArrayFind(Methods, TEXT("generateImage"), false) || ArrayFind(Methods, TEXT("imagen"), false);

		if (bText) Out.TextModels.Add(Name);
		if (bImage) Out.ImageModels.Add(Name);
		if (!bText && !bImage) Out.TextModels.Add(Name + TEXT(" (?)"));
	}

	// 解析单个 OpenRouter 模型条目
	void ParseOpenRouterModel(const TSharedPtr<FJsonObject>& Obj, FModelParseResult& Out)
	{
		FString Id;
		if (!Obj->TryGetStringField(TEXT("id"), Id)) return;

		const TSharedPtr<FJsonObject>* Arch;
		if (!Obj->TryGetObjectField(TEXT("architecture"), Arch)) return;

		const TArray<TSharedPtr<FJsonValue>>* InMod;
		const TArray<TSharedPtr<FJsonValue>>* OutMod;
		(*Arch)->TryGetArrayField(TEXT("input_modalities"), InMod);
		(*Arch)->TryGetArrayField(TEXT("output_modalities"), OutMod);

		if (ArrayFind(InMod, TEXT("image")) && ArrayFind(OutMod, TEXT("text"))) Out.TextModels.Add(Id);
		if (ArrayFind(OutMod, TEXT("image"))) Out.ImageModels.Add(Id);
	}

	void ParseModelsResponse(EAIProvider Provider, const TSharedPtr<FJsonObject>& Json, FModelParseResult& Out)
	{
		if (Provider == EAIProvider::Google)
		{
			const TArray<TSharedPtr<FJsonValue>>* ModelsArray;
			if (!Json->TryGetArrayField(TEXT("models"), ModelsArray)) return;
			for (const auto& M : *ModelsArray) ParseGoogleModel(M->AsObject(), Out);
		}
		else if (Provider == EAIProvider::OpenRouter)
		{
			const TArray<TSharedPtr<FJsonValue>>* DataArray;
			if (!Json->TryGetArrayField(TEXT("data"), DataArray)) return;
			for (const auto& M : *DataArray) ParseOpenRouterModel(M->AsObject(), Out);
		}
		else
		{
			ExtractModelIds(Json, TEXT("data"), TEXT("id"), Out.TextModels);
			if (Provider != EAIProvider::Anthropic) Out.ImageModels = Out.TextModels;
		}
	}

	// 构建请求配置
	struct FModelsRequestConfig
	{
		FString URL;
		TMap<FString, FString> Headers;
		bool bValid = false;
	};

	FModelsRequestConfig BuildModelsRequestConfig(EAIProvider Provider, const FString& APIKey)
	{
		FModelsRequestConfig Cfg;
		Cfg.bValid = true;

		switch (Provider)
		{
		case EAIProvider::OpenAI:
		case EAIProvider::DeepSeek:
		case EAIProvider::Groq:
		case EAIProvider::Together:
		case EAIProvider::Mistral:
		case EAIProvider::Volcengine:
			Cfg.URL = UImageToUMGSettings::GetProviderEndpoint(Provider) + TEXT("/models");
			Cfg.Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *APIKey));
			break;
		case EAIProvider::OpenRouter:
			Cfg.URL = TEXT("https://openrouter.ai/api/v1/models");
			Cfg.Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *APIKey));
			break;
		case EAIProvider::Anthropic:
			Cfg.URL = UImageToUMGSettings::GetProviderEndpoint(Provider) + TEXT("/models");
			Cfg.Headers.Add(TEXT("x-api-key"), APIKey);
			Cfg.Headers.Add(TEXT("anthropic-version"), TEXT("2023-06-01"));
			break;
		case EAIProvider::Google:
			// Google 使用 x-goog-api-key header 传递密钥，避免URL泄露
			Cfg.URL = FString::Printf(TEXT("%s/models"), *UImageToUMGSettings::GetProviderEndpoint(Provider));
			Cfg.Headers.Add(TEXT("x-goog-api-key"), APIKey);
			break;
		default:
			Cfg.bValid = false;
			break;
		}
		return Cfg;
	}
}

void UImageToUMGSettings::RefreshAvailableModels()
{
	if (APIKey.IsEmpty()) return;

	EAIProvider ActualProvider = (Provider == EAIProvider::Auto) ? DetectProviderFromAPIKey(APIKey) : Provider;
	DetectedProvider = GetProviderName(ActualProvider);

	FModelsRequestConfig Cfg = BuildModelsRequestConfig(ActualProvider, APIKey);
	if (!Cfg.bValid)
	{
		CachedModels = { TEXT("(Unsupported provider - enter model name manually)") };
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Cfg.URL);
	Request->SetVerb(TEXT("GET"));
	for (const auto& H : Cfg.Headers) Request->SetHeader(H.Key, H.Value);

	UE_LOG(LogImageToUMG, Verbose, TEXT("Fetching models: %s"), *GetProviderName(ActualProvider));

	Request->OnProcessRequestComplete().BindLambda([ActualProvider](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
	{
		UImageToUMGSettings* S = UImageToUMGSettings::Get();

		if (!bSuccess || !Resp.IsValid() || Resp->GetResponseCode() != 200)
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("Failed to fetch models list"));
			S->CachedModels = { TEXT("(Enter model name manually)") };
			return;
		}

		TSharedPtr<FJsonObject> Json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Resp->GetContentAsString()), Json))
		{
			S->CachedModels = { TEXT("(Failed to parse response)") };
			return;
		}

		FModelParseResult Result;
		ParseModelsResponse(ActualProvider, Json, Result);

		S->CachedModels = Result.TextModels.Num() > 0 ? Result.TextModels : TArray<FString>{ TEXT("(No models found)") };

		UE_LOG(LogImageToUMG, Verbose, TEXT("Fetched %d models"), Result.TextModels.Num());
	});

	Request->ProcessRequest();
}

void UImageToUMGSettings::CancelPendingModelRefresh()
{
	if (ModelRefreshDebounceHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ModelRefreshDebounceHandle);
		ModelRefreshDebounceHandle.Reset();
	}
}

void UImageToUMGSettings::ScheduleModelRefresh()
{
	// 取消之前的待处理刷新
	CancelPendingModelRefresh();

	// 延迟0.5秒后刷新（防抖）
	ModelRefreshDebounceHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float DeltaTime)
		{
			RefreshAvailableModels();
			return false; // 只执行一次
		}),
		0.5f // 延迟秒数
	);
}


#if WITH_EDITOR
void UImageToUMGSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	// API Key 或 Provider 变化时刷新模型列表
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UImageToUMGSettings, APIKey) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UImageToUMGSettings, Provider))
	{
		CachedModels.Empty();

		if (!APIKey.IsEmpty())
		{
			EAIProvider Detected = DetectProviderFromAPIKey(APIKey);
			DetectedProvider = GetProviderName(Detected);
			// 使用防抖刷新，避免频繁输入时重复请求
			ScheduleModelRefresh();
		}
		else
		{
			DetectedProvider.Empty();
			CancelPendingModelRefresh();
		}

		// 更新 URL 预览
		UpdateURLPreview();
	}

	// API Host 变化时验证并更新 URL 预览
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UImageToUMGSettings, APIHost))
	{
		// 验证URL格式
		FString ValidationError = ValidateEndpointURL(APIHost);
		if (!ValidationError.IsEmpty())
		{
			UE_LOG(LogImageToUMG, Error, TEXT("Invalid API Host URL: %s"), *ValidationError);
			// 不阻止保存，但记录错误
		}

		UpdateURLPreview();
	}

	// CustomEndpoint 变化时验证
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UImageToUMGSettings, CustomEndpoint))
	{
		FString ValidationError = ValidateEndpointURL(CustomEndpoint);
		if (!ValidationError.IsEmpty())
		{
			UE_LOG(LogImageToUMG, Error, TEXT("Invalid Custom Endpoint URL: %s"), *ValidationError);
		}
	}

	// CustomizeURL 切换
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UImageToUMGSettings, bCustomizeURL))
	{
		if (bCustomizeURL)
		{
			// 开启自定义：如果 URLPreview 为空或无效，先填入当前的自动值
			if (URLPreview.IsEmpty() || URLPreview.Contains(TEXT("Select provider")))
			{
				UpdateURLPreview();
			}
			
			// 立即将 APIHost 设置为自定义 URL（带 # 后缀）
			APIHost = URLPreview + TEXT("#");
		}
		else
		{
			// 关闭自定义：恢复自动更新，并清空可能被修改的 APIHost
			// 这里我们选择不清空 APIHost，而是让它重新参与自动构建
			// 如果 APIHost 之前被设置为 "url#"，我们去掉 "#" 或者是直接清空？
			// 最安全的做法是：如果是手动模式设置的，清空它让其回退到默认；或者保留它作为 base_url
			// 简单起见，我们只触发 UpdateURLPreview，用户可能通过 APIHost 字段自己通过修改
			UpdateURLPreview();
			
			// 如果 APIHost 是我们自动设置的那个带 # 的值，尝试还原?
			// 为了避免混淆，我们可以提示用户检查 API Host
		}
	}

	// URLPreview 更改（仅在自定义模式下）
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UImageToUMGSettings, URLPreview) && bCustomizeURL)
	{
		// 自动同步到 API Host，确保底层逻辑使用完整的 URL
		if (!URLPreview.IsEmpty())
		{
			// 如果用户输入了 #，我们也保留；主要目的是确保 ScriptAIClient 拿到的是全路径
			if (URLPreview.EndsWith(TEXT("#")))
			{
				APIHost = URLPreview;
			}
			else
			{
				APIHost = URLPreview + TEXT("#");
			}
		}
	}
}
#endif

FString UImageToUMGSettings::GetDefaultSystemPrompt()
{
	// 尝试从外部文件加载
	const FString PluginDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Marketplace/ImageToUMG")));
	const FString PromptFile = FPaths::Combine(PluginDir, TEXT("Content/Config/DefaultPrompt_v2.txt"));

	FString FileContent;
	if (FFileHelper::LoadFileToString(FileContent, *PromptFile))
	{
		UE_LOG(LogImageToUMG, Verbose, TEXT("从文件加载提示词: %s"), *PromptFile);
		return FileContent;
	}

	// 回退：使用内嵌的v2最小提示词（Bounds格式）
	UE_LOG(LogImageToUMG, Verbose, TEXT("提示词文件未找到: %s, 使用内嵌默认值"), *PromptFile);
	return TEXT(
		"Analyze image ({WIDTH}x{HEIGHT}px), output UMG Widget JSON with Bounds format.\n\n"
		"Output format:\n"
		"{\"WidgetName\":\"WBP_Name\",\"RootWidget\":{\"Type\":\"CanvasPanel\",\"Id\":\"Canvas_Root\",\"Name\":\"Root\",\"Bounds\":{\"X\":0,\"Y\":0,\"W\":1,\"H\":1},\"Children\":[...]},\"ImageAssets\":[]}\n\n"
		"Bounds: normalized 0-1 coordinates relative to image size. X=left/width, Y=top/height, W=width/imageWidth, H=height/imageHeight. Use 4 decimal places.\n\n"
		"Widget types: CanvasPanel, Overlay, VerticalBox, HorizontalBox, Image, TextBlock, Border, Button, ProgressBar\n"
		"Each widget needs: Type, Id (path format: ParentId/Type_Index), Name, Bounds\n"
		"Optional: Properties, Children, _meta:{confidence:0-1, source:\"ai\"}\n\n"
		"Output only JSON, no markdown, no explanation."
	);
}

FString UImageToUMGSettings::GetFormattedSystemPrompt(int32 ImageWidth, int32 ImageHeight) const
{
	FString Prompt = SystemPrompt;

	if (Prompt.IsEmpty())
	{
		Prompt = GetDefaultSystemPrompt();
	}

	// 替换占位符
	Prompt.ReplaceInline(TEXT("{WIDTH}"), *FString::FromInt(ImageWidth));
	Prompt.ReplaceInline(TEXT("{HEIGHT}"), *FString::FromInt(ImageHeight));

	// 追加 Material 预置片段
	FString MaterialSection = GenerateMaterialPromptSection();
	if (!MaterialSection.IsEmpty())
	{
		Prompt += MaterialSection;
	}

	return Prompt;
}

// ============================================================================
// Material 预置系统
// ============================================================================

TArray<FUMGPresetMaterial> UImageToUMGSettings::GetDefaultPresetMaterials()
{
	TArray<FUMGPresetMaterial> Defaults;

	// 圆形
	{
		FUMGPresetMaterial M;
		M.Name = TEXT("Circle");
		M.Description = TEXT("纯色圆形（程序化生成）");
		M.Parameters = { TEXT("Color") };
		Defaults.Add(M);
	}

	// 圆环
	{
		FUMGPresetMaterial M;
		M.Name = TEXT("Ring");
		M.Description = TEXT("空心圆环（程序化生成）");
		M.Parameters = { TEXT("Color"), TEXT("Thickness") };
		Defaults.Add(M);
	}

	// 圆角矩形
	{
		FUMGPresetMaterial M;
		M.Name = TEXT("RoundedRect");
		M.Description = TEXT("圆角矩形（程序化生成）");
		M.Parameters = { TEXT("Color"), TEXT("CornerRadius") };
		Defaults.Add(M);
	}

	// 渐变（水平）
	{
		FUMGPresetMaterial M;
		M.Name = TEXT("GradientH");
		M.Description = TEXT("水平渐变（程序化生成）");
		M.Parameters = { TEXT("ColorStart"), TEXT("ColorEnd") };
		Defaults.Add(M);
	}

	// 渐变（垂直）
	{
		FUMGPresetMaterial M;
		M.Name = TEXT("GradientV");
		M.Description = TEXT("垂直渐变（程序化生成）");
		M.Parameters = { TEXT("ColorStart"), TEXT("ColorEnd") };
		Defaults.Add(M);
	}

	// 径向渐变
	{
		FUMGPresetMaterial M;
		M.Name = TEXT("RadialGradient");
		M.Description = TEXT("径向渐变（程序化生成）");
		M.Parameters = { TEXT("ColorCenter"), TEXT("ColorEdge") };
		Defaults.Add(M);
	}

	return Defaults;
}

FString UImageToUMGSettings::GenerateMaterialPromptSection() const
{
	if (PresetMaterials.Num() == 0)
	{
		return TEXT("");
	}

	FString Section = TEXT("\n\n=== MaterialRef 预置形状 ===\n");
	Section += TEXT("Image控件可用MaterialRef绘制程序化形状(比裁图更清晰):\n\n");

	for (const FUMGPresetMaterial& M : PresetMaterials)
	{
		Section += FString::Printf(TEXT("%s: "), *M.Name);
		if (M.Parameters.Num() > 0)
		{
			Section += TEXT("MaterialParams={");
			for (int32 i = 0; i < M.Parameters.Num(); ++i)
			{
				if (i > 0) Section += TEXT(",");
				Section += M.Parameters[i];
			}
			Section += TEXT("}");
		}
		Section += TEXT("\n");
	}

	Section += TEXT("\n示例: {\"Type\":\"Image\",\"MaterialRef\":\"Circle\",\"MaterialParams\":{\"Color\":\"#FFFFFF\"},\"WidthOverride\":64,\"HeightOverride\":64,\"Slot\":{...}}\n");

	return Section;
}

const FUMGPresetMaterial* UImageToUMGSettings::FindPresetMaterial(const FString& Name) const
{
	for (const FUMGPresetMaterial& M : PresetMaterials)
	{
		if (M.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return &M;
		}
	}
	return nullptr;
}
