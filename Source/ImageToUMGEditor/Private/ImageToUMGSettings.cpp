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
	return GetMutableDefault<UImageToUMGSettings>();
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
	case EAIProvider::Google: return TEXT("Google AI");
	case EAIProvider::DeepSeek: return TEXT("DeepSeek");
	case EAIProvider::Mistral: return TEXT("Mistral AI");
	case EAIProvider::Groq: return TEXT("Groq");
	case EAIProvider::Together: return TEXT("Together AI");
	case EAIProvider::OpenRouter: return TEXT("OpenRouter");
	case EAIProvider::Volcengine: return TEXT("Volcengine (Doubao)");
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
			Cfg.URL = FString::Printf(TEXT("%s/models?key=%s"), *UImageToUMGSettings::GetProviderEndpoint(Provider), *APIKey);
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

	UE_LOG(LogImageToUMG, Log, TEXT("Fetching models: %s (%s)"), *GetProviderName(ActualProvider), *Cfg.URL);

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

		UE_LOG(LogImageToUMG, Log, TEXT("Fetched %d models"), Result.TextModels.Num());
	});

	Request->ProcessRequest();
}


#if WITH_EDITOR
void UImageToUMGSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	// API Key 变化时刷新模型列表
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UImageToUMGSettings, APIKey) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UImageToUMGSettings, Provider))
	{
		CachedModels.Empty();

		if (!APIKey.IsEmpty())
		{
			EAIProvider Detected = DetectProviderFromAPIKey(APIKey);
			DetectedProvider = GetProviderName(Detected);
			RefreshAvailableModels();
		}
		else
		{
			DetectedProvider.Empty();
		}
	}
}
#endif

FString UImageToUMGSettings::GetDefaultSystemPrompt()
{
	return TEXT(
		"分析图片 ({WIDTH}x{HEIGHT}px)，识别所有可独立编辑的 UI 元素，输出 UMG Widget JSON。\n\n"
		"【核心原则】\n"
		"将图片拆分为多个独立控件，每个视觉元素单独一个控件，便于后续编辑和复用。\n\n"
		"【强制要求】\n"
		"1. 每个 Image 必须有 ImageRef 属性\n"
		"2. 必须输出 ImageAssets 数组\n"
		"3. BoundingBox 是元素在源图中的像素坐标 (X,Y,Width,Height)\n\n"
		"【元素识别】\n"
		"文字 -> TextBlock (提取内容、字号、颜色)\n"
		"按钮 -> Button + TextBlock\n"
		"图标/图形/背景 -> Image + ImageRef + BoundingBox\n"
		"进度条 -> ProgressBar\n"
		"输入框 -> EditableTextBox\n"
		"复选框 -> CheckBox\n"
		"滑块 -> Slider\n\n"
		"【容器】Overlay(层叠), CanvasPanel(绝对定位), VerticalBox, HorizontalBox, Border, SizeBox\n\n"
		"【输出格式】\n"
		"{\"WidgetName\":\"WBP_Name\",\n"
		"\"RootWidget\":{\"Type\":\"Overlay\",\"Name\":\"Root\",\"Children\":[{\"Type\":\"Image\",\"Name\":\"Img1\",\"ImageRef\":\"img1\",\"WidthOverride\":100,\"HeightOverride\":100,\"Slot\":{\"HAlign\":\"Center\",\"VAlign\":\"Center\"}}]},\n"
		"\"ImageAssets\":[{\"Id\":\"img1\",\"BoundingBox\":{\"X\":0,\"Y\":0,\"Width\":100,\"Height\":100}}]}\n\n"
		"【Slot】\n"
		"Overlay/Box: {\"HAlign\":\"Center\",\"VAlign\":\"Center\"}\n"
		"CanvasPanel: {\"Anchors\":{\"Min\":{\"X\":0.5,\"Y\":0.5},\"Max\":{\"X\":0.5,\"Y\":0.5}},\"Alignment\":{\"X\":0.5,\"Y\":0.5},\"Offsets\":{\"Left\":-50,\"Top\":-50,\"Right\":50,\"Bottom\":50}}\n\n"
		"【规则】仅输出JSON，无markdown，无解释，控件名唯一\n"
		"【非UI图片】{\"error\":\"NOT_UI_IMAGE\",\"reason\":\"原因\"}"
	);
}
