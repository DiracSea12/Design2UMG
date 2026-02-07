// Copyright Epic Games, Inc. All Rights Reserved.

#include "VisualComparator.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "HttpAIClient.h"
#include "ScriptAIClient.h"
#include "ImageToUMGSettings.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "AIProviderStrategy.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogVisualComparator, Log, All);

// ============================================================
// PNG 解码
// ============================================================

bool FVisualComparator::DecodePng(const TArray<uint8>& PngData, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(PngData.GetData(), PngData.Num()))
		return false;

	OutWidth = Wrapper->GetWidth();
	OutHeight = Wrapper->GetHeight();

	TArray<uint8> RawData;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
		return false;

	int32 PixelCount = OutWidth * OutHeight;
	OutPixels.SetNumUninitialized(PixelCount);
	FMemory::Memcpy(OutPixels.GetData(), RawData.GetData(), PixelCount * sizeof(FColor));
	return true;
}

// ============================================================
// 灰度转换
// ============================================================

void FVisualComparator::ToGrayscale(const TArray<FColor>& Pixels, TArray<uint8>& OutGray)
{
	OutGray.SetNumUninitialized(Pixels.Num());
	for (int32 i = 0; i < Pixels.Num(); ++i)
	{
		const FColor& C = Pixels[i];
		OutGray[i] = (uint8)(0.299f * C.R + 0.587f * C.G + 0.114f * C.B);
	}
}

// ============================================================
// 差异像素占比
// ============================================================

float FVisualComparator::ComputeDiffRatio(const TArray<uint8>& GrayA, const TArray<uint8>& GrayB, uint8 Threshold)
{
	if (GrayA.Num() != GrayB.Num() || GrayA.Num() == 0)
		return 1.f;

	int32 DiffCount = 0;
	for (int32 i = 0; i < GrayA.Num(); ++i)
	{
		if (FMath::Abs((int32)GrayA[i] - (int32)GrayB[i]) > Threshold)
			++DiffCount;
	}
	return (float)DiffCount / (float)GrayA.Num();
}

// ============================================================
// SSIM（8x8 窗口简化版）
// ============================================================

float FVisualComparator::ComputeSSIM(const TArray<uint8>& GrayA, const TArray<uint8>& GrayB, int32 Width, int32 Height)
{
	if (GrayA.Num() != GrayB.Num() || GrayA.Num() == 0)
		return 0.f;

	constexpr int32 BlockSize = 8;
	constexpr float C1 = 6.5025f;   // (0.01*255)^2
	constexpr float C2 = 58.5225f;  // (0.03*255)^2

	int32 BlocksX = Width / BlockSize;
	int32 BlocksY = Height / BlockSize;
	if (BlocksX == 0 || BlocksY == 0)
		return 0.f;

	double SSIMSum = 0.0;
	int32 BlockCount = 0;

	for (int32 by = 0; by < BlocksY; ++by)
	{
		for (int32 bx = 0; bx < BlocksX; ++bx)
		{
			float SumA = 0.f, SumB = 0.f;
			float SumA2 = 0.f, SumB2 = 0.f, SumAB = 0.f;

			for (int32 dy = 0; dy < BlockSize; ++dy)
			{
				int32 Row = by * BlockSize + dy;
				for (int32 dx = 0; dx < BlockSize; ++dx)
				{
					int32 Idx = Row * Width + bx * BlockSize + dx;
					float A = (float)GrayA[Idx];
					float B = (float)GrayB[Idx];
					SumA += A;
					SumB += B;
					SumA2 += A * A;
					SumB2 += B * B;
					SumAB += A * B;
				}
			}

			constexpr float N = (float)(BlockSize * BlockSize);
			float MuA = SumA / N;
			float MuB = SumB / N;
			float SigA2 = SumA2 / N - MuA * MuA;
			float SigB2 = SumB2 / N - MuB * MuB;
			float SigAB = SumAB / N - MuA * MuB;

			float Numerator = (2.f * MuA * MuB + C1) * (2.f * SigAB + C2);
			float Denominator = (MuA * MuA + MuB * MuB + C1) * (SigA2 + SigB2 + C2);

			SSIMSum += (double)(Numerator / Denominator);
			++BlockCount;
		}
	}

	return (float)(SSIMSum / (double)BlockCount);
}

// ============================================================
// CompareImages（公开接口：SSIM + 差异像素占比）
// ============================================================

FVisualCompareReport FVisualComparator::CompareImages(
	const TArray<uint8>& DesignPng,
	const TArray<uint8>& RenderedPng,
	float PassThreshold)
{
	FVisualCompareReport Report;

	TArray<FColor> PixelsA, PixelsB;
	int32 WA, HA, WB, HB;

	if (!DecodePng(DesignPng, PixelsA, WA, HA) || !DecodePng(RenderedPng, PixelsB, WB, HB))
	{
		UE_LOG(LogVisualComparator, Error, TEXT("PNG decode failed"));
		Report.SSIM = 0.f;
		Report.DiffPixelRatio = 1.f;
		return Report;
	}

	if (WA != WB || HA != HB)
	{
		UE_LOG(LogVisualComparator, Warning, TEXT("Size mismatch: %dx%d vs %dx%d"), WA, HA, WB, HB);
		Report.SSIM = 0.f;
		Report.DiffPixelRatio = 1.f;
		return Report;
	}

	TArray<uint8> GrayA, GrayB;
	ToGrayscale(PixelsA, GrayA);
	ToGrayscale(PixelsB, GrayB);

	Report.SSIM = ComputeSSIM(GrayA, GrayB, WA, HA);
	Report.DiffPixelRatio = ComputeDiffRatio(GrayA, GrayB);
	Report.bPassThreshold = Report.SSIM >= PassThreshold;

	UE_LOG(LogVisualComparator, Log, TEXT("SSIM=%.4f DiffRatio=%.4f Pass=%d"),
		Report.SSIM, Report.DiffPixelRatio, Report.bPassThreshold);

	return Report;
}

// ============================================================
// 左右拼接为对比图 PNG
// ============================================================

bool FVisualComparator::StitchSideBySide(
	const TArray<uint8>& PngA, const TArray<uint8>& PngB,
	TArray<uint8>& OutPng, int32& OutWidth, int32& OutHeight)
{
	TArray<FColor> PixA, PixB;
	int32 WA, HA, WB, HB;
	if (!DecodePng(PngA, PixA, WA, HA) || !DecodePng(PngB, PixB, WB, HB))
		return false;

	OutWidth = WA + WB;
	OutHeight = FMath::Max(HA, HB);

	TArray<FColor> Combined;
	Combined.SetNumZeroed(OutWidth * OutHeight);

	// 左半：A
	for (int32 y = 0; y < HA; ++y)
		FMemory::Memcpy(&Combined[y * OutWidth], &PixA[y * WA], WA * sizeof(FColor));

	// 右半：B
	for (int32 y = 0; y < HB; ++y)
		FMemory::Memcpy(&Combined[y * OutWidth + WA], &PixB[y * WB], WB * sizeof(FColor));

	// 编码为 PNG
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid())
		return false;

	if (!Wrapper->SetRaw(Combined.GetData(), Combined.Num() * sizeof(FColor),
		OutWidth, OutHeight, ERGBFormat::BGRA, 8))
		return false;

	const TArray64<uint8>& Compressed = Wrapper->GetCompressed();
	OutPng.SetNumUninitialized(Compressed.Num());
	FMemory::Memcpy(OutPng.GetData(), Compressed.GetData(), Compressed.Num());
	return true;
}

// ============================================================
// 解析 AI 返回的差异 JSON
// ============================================================

void FVisualComparator::ParseAIDiffs(const FString& AIResponse, TArray<FVisualDiffItem>& OutDiffs)
{
	// 尝试从 AI 响应中提取 JSON 数组
	// AI 可能返回 [...] 或包裹在 markdown ```json ... ``` 中
	FString JsonStr = AIResponse;

	// 去掉 markdown 代码块
	int32 JsonStart = JsonStr.Find(TEXT("["));
	int32 JsonEnd = JsonStr.Find(TEXT("]"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (JsonStart != INDEX_NONE && JsonEnd != INDEX_NONE && JsonEnd > JsonStart)
		JsonStr = JsonStr.Mid(JsonStart, JsonEnd - JsonStart + 1);

	TArray<TSharedPtr<FJsonValue>> Arr;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Arr))
	{
		UE_LOG(LogVisualComparator, Warning, TEXT("Failed to parse AI diff JSON"));
		return;
	}

	for (const auto& Val : Arr)
	{
		const TSharedPtr<FJsonObject>& Obj = Val->AsObject();
		if (!Obj.IsValid())
			continue;

		FVisualDiffItem Item;
		Item.Region = Obj->GetStringField(TEXT("region"));
		Item.Type = Obj->GetStringField(TEXT("type"));
		Item.Description = Obj->GetStringField(TEXT("description"));
		Item.Severity = (float)Obj->GetNumberField(TEXT("severity"));
		OutDiffs.Add(MoveTemp(Item));
	}

	UE_LOG(LogVisualComparator, Log, TEXT("Parsed %d AI diff items"), OutDiffs.Num());
}

// ============================================================
// AI 增强对比（内部先调 CompareImages，再追加 AI 分析）
// ============================================================

void FVisualComparator::CompareWithAI(
	const TArray<uint8>& DesignPng,
	const TArray<uint8>& RenderedPng,
	float PassThreshold,
	TFunction<void(const FVisualCompareReport&)> OnComplete)
{
	// 第一层：脚本级对比
	FVisualCompareReport Report = CompareImages(DesignPng, RenderedPng, PassThreshold);

	// 拼接左右对比图
	TArray<uint8> StitchedPng;
	int32 StitchedW, StitchedH;
	if (!StitchSideBySide(DesignPng, RenderedPng, StitchedPng, StitchedW, StitchedH))
	{
		UE_LOG(LogVisualComparator, Error, TEXT("Failed to stitch side-by-side image"));
		if (OnComplete) OnComplete(Report);
		return;
	}

	// 获取 AI 设置
	const UImageToUMGSettings* Settings = UImageToUMGSettings::Get();
	EAIProvider Provider = Settings->Provider;
	if (Provider == EAIProvider::Auto)
		Provider = UImageToUMGSettings::DetectProviderFromAPIKey(Settings->APIKey);

	TSharedPtr<IAIProviderStrategy> Strategy = FAIProviderStrategyFactory::GetStrategy(Provider);
	if (!Strategy.IsValid() || Settings->APIKey.IsEmpty())
	{
		UE_LOG(LogVisualComparator, Warning, TEXT("AI provider not configured, returning script-level report only"));
		if (OnComplete) OnComplete(Report);
		return;
	}

	// 构建 UI 对比专用 prompt
	const FString Prompt = FString::Printf(
		TEXT("You are a UI comparison expert. The image shows two UI screenshots side by side.\n"
			"Left = design mockup, Right = rendered result.\n"
			"SSIM=%.4f, DiffPixelRatio=%.4f.\n\n"
			"Identify visual differences. Ignore font rendering and sub-pixel aliasing.\n"
			"Return a JSON array of objects with these fields:\n"
			"- region: semantic location (e.g. \"top-left button\")\n"
			"- type: one of offset/missing/color/size\n"
			"- description: what is different\n"
			"- severity: 0.0-1.0\n\n"
			"Return ONLY the JSON array, no other text."),
		Report.SSIM, Report.DiffPixelRatio);

	// Base64 编码拼接图
	const FString Base64Image = FBase64::Encode(StitchedPng);
	const FString MimeType = TEXT("image/png");

	// 构建请求体
	TSharedPtr<FJsonObject> RequestBodyJson = Strategy->BuildRequestBody(
		Base64Image, MimeType, Prompt,
		Settings->MaxTokens, Settings->Temperature, Settings->TopP, Settings->ModelName);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RequestBodyJson.ToSharedRef(), Writer);

	// 构建 URL（复用 HttpAIClient 的逻辑）
	FString Endpoint = Settings->APIHost.IsEmpty()
		? UImageToUMGSettings::GetProviderEndpoint(Provider)
		: Settings->APIHost;
	if (Endpoint.EndsWith(TEXT("#")))
		Endpoint = Endpoint.LeftChop(1);
	const FString URL = Strategy->BuildURL(Endpoint, Settings->ModelName);

	// 创建 HTTP 请求
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("POST"));
	Request->SetTimeout(FMath::Max(120, Settings->TimeoutSeconds));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

	for (const auto& Header : Strategy->GetHeaders(Settings->APIKey))
		Request->SetHeader(Header.Key, Header.Value);

	Request->SetContentAsString(RequestBody);

	// 捕获 Report 和 Strategy 到回调
	Request->OnProcessRequestComplete().BindLambda(
		[Report, Strategy, OnComplete](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess) mutable
		{
			if (!bSuccess || !Resp.IsValid())
			{
				UE_LOG(LogVisualComparator, Error, TEXT("AI request failed"));
				if (OnComplete) OnComplete(Report);
				return;
			}

			if (Resp->GetResponseCode() != 200)
			{
				UE_LOG(LogVisualComparator, Error, TEXT("AI returned HTTP %d: %s"),
					Resp->GetResponseCode(), *Resp->GetContentAsString().Left(500));
				if (OnComplete) OnComplete(Report);
				return;
			}

			// 解析 AI 响应 JSON
			TSharedPtr<FJsonObject> RespJson;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
			if (!FJsonSerializer::Deserialize(Reader, RespJson) || !RespJson.IsValid())
			{
				UE_LOG(LogVisualComparator, Error, TEXT("Failed to parse AI response JSON"));
				if (OnComplete) OnComplete(Report);
				return;
			}

			// 用策略提取内容文本
			FString Content;
			if (!Strategy->ExtractContent(RespJson, Content))
			{
				UE_LOG(LogVisualComparator, Error, TEXT("Failed to extract content from AI response"));
				if (OnComplete) OnComplete(Report);
				return;
			}

			// 保存 AI 原文
			Report.AIAnalysis = Content;

			// 解析差异列表
			ParseAIDiffs(Content, Report.Diffs);

			UE_LOG(LogVisualComparator, Log, TEXT("AI analysis complete: %d diffs found"), Report.Diffs.Num());
			if (OnComplete) OnComplete(Report);
		});

	UE_LOG(LogVisualComparator, Log, TEXT("Sending AI comparison request to %s"), *URL);
	Request->ProcessRequest();
}
