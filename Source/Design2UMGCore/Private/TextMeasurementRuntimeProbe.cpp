// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementRuntimeProbe.h"

#include "Components/TextBlock.h"
#include "DesignTypeMapper.h"
#include "FontResolver.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateTypes.h"
#include "TextMeasurement.h"
#include "TextMeasurementInternal.h"
#include "TextStyleUtils.h"

namespace
{
	constexpr float MetricProbeFontSize = 64.0f;

	bool IsFinitePositive(const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	}

	FString SerializeQuantizedMetricRatio(const float Value)
	{
		if (!FMath::IsFinite(Value))
		{
			return FString();
		}

		const float Scaled = Value * 10000.0f;
		if (!FMath::IsFinite(Scaled))
		{
			return FString();
		}

		float Quantized = FMath::RoundToFloat(Scaled) / 10000.0f;
		if (FMath::IsNearlyZero(Quantized, 0.00005f))
		{
			Quantized = 0.0f;
		}
		return FString::Printf(TEXT("%.4f"), Quantized);
	}

	FSlateFontInfo BuildProbeFontInfo(const TextMeasurementRuntimeProbe::FTextMeasureRequest& Request)
	{
		FSlateFontInfo FontInfo = FTextBlockStyle::GetDefault().Font;
		if (Request.FontSize > 0.0f)
		{
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(Request.FontSize));
		}

		if (!Request.FontAssetPath.IsEmpty())
		{
			if (UObject* FontObject = LoadObject<UObject>(nullptr, *Request.FontAssetPath))
			{
				FontInfo.FontObject = FontObject;
			}
		}

		if (!Request.TypefaceName.IsEmpty())
		{
			FontInfo.TypefaceFontName = FName(*Request.TypefaceName);
		}

		FontInfo.LetterSpacing = Request.LetterSpacing;
		return FontInfo;
	}

	ETextJustify::Type ToSlateJustification(const EDesignTextAlign TextAlign)
	{
		switch (TextAlign)
		{
		case EDesignTextAlign::Center: return ETextJustify::Center;
		case EDesignTextAlign::Right: return ETextJustify::Right;
		default: return ETextJustify::Left;
		}
	}

	FString ResolveTypefaceName(const FString& FontFamily, const FString& FontWeight)
	{
		FString Typeface = FFontResolver::FindBestTypeface(FontFamily, FontWeight);
		if (Typeface.IsEmpty() && !FontWeight.IsEmpty())
		{
			Typeface = DesignTypeMapper::MapFontWeight(FontWeight).ToString();
		}
		return Typeface;
	}

	FString ResolveTypefaceName(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		return ResolveTypefaceName(Node->Text.FontFamily, Node->Text.FontWeight);
	}

	TextMeasurementRuntimeProbe::FTextMeasureRequest BuildTextMeasureRequest(
		const FString& Text,
		const float FontSize,
		const FString& FontFamily,
		const FString& FontWeight,
		const int32 LetterSpacing)
	{
		TextMeasurementRuntimeProbe::FTextMeasureRequest Request;
		Request.Text = Text;
		Request.FontSize = FontSize;
		Request.FontAssetPath = FFontResolver::FindFontAssetPath(FontFamily);
		Request.TypefaceName = ResolveTypefaceName(FontFamily, FontWeight);
		Request.LetterSpacing = LetterSpacing;
		Request.bRequiresResolvedFontAsset = FFontResolver::HasInvalidProjectFontAsset(FontFamily);
		return Request;
	}

	TextMeasurementRuntimeProbe::FTextBlockMeasureRequest BuildTextBlockMeasureRequest(
		const TSharedPtr<FDesignNode>& Node,
		const float FontSize,
		const FString& Text,
		const float WrapTextAt,
		const bool bAutoWrapText)
	{
		TextMeasurementRuntimeProbe::FTextBlockMeasureRequest Request;
		if (!Node.IsValid())
		{
			return Request;
		}

		Request.Text = Text;
		Request.FontSize = FontSize;
		Request.FontAssetPath = FFontResolver::FindFontAssetPath(Node->Text.FontFamily);
		Request.TypefaceName = ResolveTypefaceName(Node);
		Request.bRequiresResolvedFontAsset = FFontResolver::HasInvalidProjectFontAsset(Node->Text.FontFamily);
		Request.LetterSpacing = (Node->Text.LetterSpacing != 0.0f && FontSize > 0.0f)
			? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing, Node->Text.FontSize, FontSize)
			: 0;
		Request.TextAlign = Node->Text.TextAlign;
		Request.bApplyExplicitLineHeight = TextMeasurement::ShouldApplyExplicitLineHeight(Node, FontSize, WrapTextAt);
		Request.LineHeightPercentage = Request.bApplyExplicitLineHeight ? Node->Text.LineHeight : 1.0f;
		Request.bAutoWrapText = bAutoWrapText;
		Request.WrapTextAt = WrapTextAt;
		return Request;
	}
}

namespace RuntimeProbe
{
const float FallbackMeasureWidthScale = 0.60f;
const float UnresolvedFontFallbackMeasureWidthScale = 1.05f;
const float FallbackMeasureHeightScale = 1.5f;

namespace
{
	constexpr float DeclaredFontProbeScale = 1.5f;
	constexpr float FallbackFontProbeScale = 2.0f;
}

int32 ResolveMaxProbeFontSize(float DeclaredFontSize, float FallbackFontSize)
{
	return FMath::Max(
		1,
		FMath::CeilToInt(FMath::Max(DeclaredFontSize * DeclaredFontProbeScale, FallbackFontSize * FallbackFontProbeScale)));
}

FSlateFontInfo BuildSlateFontInfoForSize(const TSharedPtr<FDesignNode>& Node, float FontSize)
{
	FSlateFontInfo FontInfo = FTextBlockStyle::GetDefault().Font;
	if (!Node.IsValid())
	{
		return FontInfo;
	}

	if (FontSize > 0.0f)
	{
		FontInfo.Size = FMath::Max(1, FMath::RoundToInt(FontSize));
	}

	if (!Node->Text.FontFamily.IsEmpty())
	{
		const FString AssetPath = FFontResolver::FindFontAssetPath(Node->Text.FontFamily);
		if (!AssetPath.IsEmpty())
		{
			if (UObject* FontObject = LoadObject<UObject>(nullptr, *AssetPath))
			{
				FontInfo.FontObject = FontObject;
			}
		}
	}

	const FString Typeface = ResolveTypefaceName(Node);
	if (!Typeface.IsEmpty())
	{
		FontInfo.TypefaceFontName = FName(*Typeface);
	}

	if (Node->Text.LetterSpacing != 0.0f && FontInfo.Size > 0)
	{
		FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing, Node->Text.FontSize, FontInfo.Size);
	}

	return FontInfo;
}

FVector2D MeasureTextSize(const FString& Text, float FontSize, const FString& FontFamily, const FString& FontWeight, int32 LetterSpacing)
{
	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Text);
	FVector2D MeasuredSize = FVector2D::ZeroVector;
	const TextMeasurementRuntimeProbe::FTextMeasureRequest Request =
		BuildTextMeasureRequest(RuntimeText, FontSize, FontFamily, FontWeight, LetterSpacing);
	if (TextMeasurementRuntimeProbe::TryMeasureText(Request, MeasuredSize))
	{
		return MeasuredSize;
	}

	return FVector2D(RuntimeText.Len() * FontSize * FallbackMeasureWidthScale, FontSize * FallbackMeasureHeightScale);
}

FVector2D MeasureUMGTextBlockDesiredSize(
	const TSharedPtr<FDesignNode>& Node,
	float FontSize,
	const FString& Text,
	float WrapTextAt,
	bool bAutoWrapText)
{
	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Text);
	if (!Node.IsValid() || FontSize <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}

	FVector2D MeasuredSize = FVector2D::ZeroVector;
	return TextMeasurementRuntimeProbe::TryMeasureTextBlockDesiredSize(
		BuildTextBlockMeasureRequest(Node, FontSize, RuntimeText, WrapTextAt, bAutoWrapText),
		MeasuredSize)
		? MeasuredSize
		: FVector2D::ZeroVector;
}

float MeasureSingleLineTextBlockHeight(const TSharedPtr<FDesignNode>& Node, float FontSize)
{
	if (!Node.IsValid() || FontSize <= 0.0f)
	{
		return 0.0f;
	}

	const int32 LetterSpacingTracking = (Node->Text.LetterSpacing != 0.0f && FontSize > 0.0f)
		? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing, Node->Text.FontSize, FontSize)
		: 0;
	FVector2D MeasuredSize = FVector2D::ZeroVector;
	if (TextMeasurementRuntimeProbe::TryMeasureText(
		BuildTextMeasureRequest(TEXT("Hg"), FontSize, Node->Text.FontFamily, Node->Text.FontWeight, LetterSpacingTracking),
		MeasuredSize))
	{
		return MeasuredSize.Y;
	}

	return MeasureUMGTextBlockDesiredSize(Node, FontSize, TEXT("Hg")).Y;
}
}

bool TextMeasurementRuntimeProbe::IsAvailable()
{
	return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer() != nullptr;
}

TextMeasurementRuntimeProbe::FSlateTextMetricFingerprint TextMeasurementRuntimeProbe::MeasureSlateTextMetricFingerprint(const FTextMeasureRequest& Request)
{
	FSlateTextMetricFingerprint Metric;
	if (!IsAvailable())
	{
		return Metric;
	}
	if (Request.bRequiresResolvedFontAsset && Request.FontAssetPath.IsEmpty())
	{
		return Metric;
	}

	FTextMeasureRequest ProbeRequest = Request;
	ProbeRequest.FontSize = MetricProbeFontSize;
	TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo FontInfo = BuildProbeFontInfo(ProbeRequest);
	constexpr float ProbeFontScale = 1.0f;
	const float Baseline = FontMeasure->GetBaseline(FontInfo, ProbeFontScale);
	const float MaxCharacterHeight = FontMeasure->GetMaxCharacterHeight(FontInfo, ProbeFontScale);
	if (!FMath::IsFinite(Baseline)
		|| !IsFinitePositive(MaxCharacterHeight)
		|| FMath::IsNearlyZero(Baseline)
		|| FMath::Abs(Baseline) > TNumericLimits<float>::Max() / MetricProbeFontSize
		|| MaxCharacterHeight > TNumericLimits<float>::Max() / MetricProbeFontSize)
	{
		return Metric;
	}

	Metric.SignedBaselineRatio = Baseline / MetricProbeFontSize;
	Metric.MaxHeightRatio = MaxCharacterHeight / MetricProbeFontSize;
	Metric.bIsAvailable = FMath::IsFinite(Metric.SignedBaselineRatio)
		&& IsFinitePositive(Metric.MaxHeightRatio)
		&& !FMath::IsNearlyZero(Metric.SignedBaselineRatio);
	return Metric;
}

FString TextMeasurementRuntimeProbe::SerializeSlateTextMetricToken(const FSlateTextMetricFingerprint& Metric)
{
	if (!Metric.bIsAvailable)
	{
		return TEXT("metric:unavailable");
	}

	const FString BaselineToken = SerializeQuantizedMetricRatio(Metric.SignedBaselineRatio);
	const FString MaxHeightToken = SerializeQuantizedMetricRatio(Metric.MaxHeightRatio);
	if (BaselineToken.IsEmpty() || MaxHeightToken.IsEmpty())
	{
		return TEXT("metric:unavailable");
	}
	return FString::Printf(TEXT("metric:%s:%s"), *BaselineToken, *MaxHeightToken);
}

bool TextMeasurementRuntimeProbe::TryMeasureText(const FTextMeasureRequest& Request, FVector2D& OutSize)
{
	OutSize = FVector2D::ZeroVector;
	if (Request.FontSize <= 0.0f || !IsAvailable())
	{
		return false;
	}
	if (Request.bRequiresResolvedFontAsset && Request.FontAssetPath.IsEmpty())
	{
		return false;
	}

	TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2D MeasuredSize = FontMeasure->Measure(Request.Text, BuildProbeFontInfo(Request));
	if (MeasuredSize.X <= 0.0f || MeasuredSize.Y <= 0.0f)
	{
		return false;
	}

	OutSize = MeasuredSize;
	return true;
}

bool TextMeasurementRuntimeProbe::TryMeasureTextBlockDesiredSize(const FTextBlockMeasureRequest& Request, FVector2D& OutSize)
{
	OutSize = FVector2D::ZeroVector;
	if (Request.FontSize <= 0.0f || !IsAvailable())
	{
		return false;
	}
	if (Request.bRequiresResolvedFontAsset && Request.FontAssetPath.IsEmpty())
	{
		return false;
	}

	UTextBlock* ProbeTextBlock = NewObject<UTextBlock>();
	if (!ProbeTextBlock)
	{
		return false;
	}

	ProbeTextBlock->SetText(FText::FromString(Request.Text));
	ProbeTextBlock->SetFont(BuildProbeFontInfo(Request));
	ProbeTextBlock->SetJustification(ToSlateJustification(Request.TextAlign));
	ProbeTextBlock->SetLineHeightPercentage(Request.LineHeightPercentage);
	ProbeTextBlock->SetApplyLineHeightToBottomLine(Request.bApplyExplicitLineHeight);
	ProbeTextBlock->SetAutoWrapText(Request.bAutoWrapText);
	ProbeTextBlock->SetWrapTextAt(Request.WrapTextAt);

	TSharedRef<SWidget> SlateWidget = ProbeTextBlock->TakeWidget();
	SlateWidget->SlatePrepass(1.0f);
	SlateWidget->SlatePrepass(1.0f);

	const FVector2D DesiredSize = SlateWidget->GetDesiredSize();
	if (DesiredSize.X <= 0.0f || DesiredSize.Y <= 0.0f)
	{
		return false;
	}

	OutSize = DesiredSize;
	return true;
}
