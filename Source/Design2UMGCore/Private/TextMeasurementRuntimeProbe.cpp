// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementRuntimeProbe.h"

#include "Components/TextBlock.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateTypes.h"
#include "TextMeasurement.h"
#include "TextFontResolution.h"
#include "TextMeasurementInternal.h"
#include "TextStyleUtils.h"

namespace TextMeasurementRuntimeProbe
{
	FSlateFontInfo BuildFontInfo(const FTextMeasureRequest& Request)
	{
		FSlateFontInfo FontInfo = FTextBlockStyle::GetDefault().Font;
		if (FMath::IsFinite(Request.FontSize) && Request.FontSize > 0.0f)
		{
			FontInfo.Size = Request.FontSize;
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
}

namespace
{
	bool IsFinitePositive(const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
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

	TextMeasurementRuntimeProbe::FTextMeasureRequest BuildTextMeasureRequest(
		const FString& Text,
		const float FontSize,
		const FString& FontFamily,
		const FString& FontPostScriptName,
		const FString& FontWeight,
		const int32 LetterSpacing)
	{
		const TextFontResolution::FResolvedFontSelection ResolvedFont =
			TextFontResolution::Resolve(FontFamily, FontPostScriptName, FontWeight);
		TextMeasurementRuntimeProbe::FTextMeasureRequest Request;
		Request.Text = Text;
		Request.FontSize = FontSize;
		Request.FontAssetPath = ResolvedFont.FontAssetPath;
		Request.TypefaceName = ResolvedFont.TypefaceName;
		Request.LetterSpacing = LetterSpacing;
		Request.bRequiresResolvedFontAsset = ResolvedFont.bRequiresResolvedFontAsset;
		return Request;
	}

	TextMeasurementRuntimeProbe::FTextBlockMeasureRequest BuildTextBlockMeasureRequest(
		const TSharedPtr<FDesignNode>& Node,
		const float FontSize,
		const FString& Text,
		const float WrapTextAt,
		const bool bAutoWrapText,
		const TOptional<float> LineHeightPercentageOverride = {})
	{
		TextMeasurementRuntimeProbe::FTextBlockMeasureRequest Request;
		if (!Node.IsValid())
		{
			return Request;
		}

		const TextFontResolution::FResolvedFontSelection ResolvedFont = TextFontResolution::Resolve(
			Node->Text.FontFamily,
			Node->Text.FontPostScriptName,
			Node->Text.FontWeight);
		Request.Text = Text;
		Request.FontSize = FontSize;
		Request.FontAssetPath = ResolvedFont.FontAssetPath;
		Request.TypefaceName = ResolvedFont.TypefaceName;
		Request.bRequiresResolvedFontAsset = ResolvedFont.bRequiresResolvedFontAsset;
		Request.LetterSpacing = (Node->Text.LetterSpacing != 0.0f && FontSize > 0.0f)
			? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing, Node->Text.FontSize, FontSize)
			: 0;
		Request.TextAlign = Node->Text.TextAlign;
		Request.bApplyExplicitLineHeight = LineHeightPercentageOverride.IsSet();
		Request.LineHeightPercentage = LineHeightPercentageOverride.Get(1.0f);
		Request.bAutoWrapText = bAutoWrapText;
		Request.WrapTextAt = WrapTextAt;
		return Request;
	}

}

namespace RuntimeProbe
{
FVector2D MeasureTextSize(
	const FString& Text,
	float FontSize,
	const FString& FontFamily,
	const FString& FontPostScriptName,
	const FString& FontWeight,
	int32 LetterSpacing)
{
	if (!IsFinitePositive(FontSize))
	{
		return FVector2D::ZeroVector;
	}

	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Text);
	FVector2D MeasuredSize = FVector2D::ZeroVector;
	const TextMeasurementRuntimeProbe::FTextMeasureRequest Request =
		BuildTextMeasureRequest(RuntimeText, FontSize, FontFamily, FontPostScriptName, FontWeight, LetterSpacing);
	if (TextMeasurementRuntimeProbe::TryMeasureText(Request, MeasuredSize))
	{
		return MeasuredSize;
	}

	return FVector2D::ZeroVector;
}

FVector2D MeasureUMGTextBlockDesiredSize(
	const TSharedPtr<FDesignNode>& Node,
	float FontSize,
	const FString& Text,
	float WrapTextAt,
	bool bAutoWrapText,
	TOptional<float> LineHeightPercentageOverride)
{
	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Text);
	if (!Node.IsValid() || !IsFinitePositive(FontSize))
	{
		return FVector2D::ZeroVector;
	}

	FVector2D MeasuredSize = FVector2D::ZeroVector;
	return TextMeasurementRuntimeProbe::TryMeasureTextBlockDesiredSize(
		BuildTextBlockMeasureRequest(
			Node,
			FontSize,
			RuntimeText,
			WrapTextAt,
			bAutoWrapText,
			LineHeightPercentageOverride),
		MeasuredSize)
		? MeasuredSize
		: FVector2D::ZeroVector;
}

}

bool TextMeasurementRuntimeProbe::IsAvailable()
{
	return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer() != nullptr;
}

bool TextMeasurementRuntimeProbe::TryMeasureText(const FTextMeasureRequest& Request, FVector2D& OutSize)
{
	OutSize = FVector2D::ZeroVector;
	if (!IsFinitePositive(Request.FontSize) || !IsAvailable())
	{
		return false;
	}
	if (Request.bRequiresResolvedFontAsset && Request.FontAssetPath.IsEmpty())
	{
		return false;
	}

	TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2D MeasuredSize = FontMeasure->Measure(Request.Text, BuildFontInfo(Request));
	if (!IsFinitePositive(MeasuredSize.X) || !IsFinitePositive(MeasuredSize.Y))
	{
		return false;
	}

	OutSize = MeasuredSize;
	return true;
}

bool TextMeasurementRuntimeProbe::TryMeasureTextBlockDesiredSize(const FTextBlockMeasureRequest& Request, FVector2D& OutSize)
{
	OutSize = FVector2D::ZeroVector;
	if (!IsFinitePositive(Request.FontSize) || !IsAvailable())
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
	ProbeTextBlock->SetFont(BuildFontInfo(Request));
	ProbeTextBlock->SetJustification(ToSlateJustification(Request.TextAlign));
	ProbeTextBlock->SetLineHeightPercentage(Request.LineHeightPercentage);
	ProbeTextBlock->SetApplyLineHeightToBottomLine(Request.bApplyExplicitLineHeight);
	ProbeTextBlock->SetAutoWrapText(Request.bAutoWrapText);
	ProbeTextBlock->SetWrapTextAt(Request.WrapTextAt);

	TSharedRef<SWidget> SlateWidget = ProbeTextBlock->TakeWidget();
	SlateWidget->SlatePrepass(1.0f);
	SlateWidget->SlatePrepass(1.0f);

	const FVector2D DesiredSize = SlateWidget->GetDesiredSize();
	if (!IsFinitePositive(DesiredSize.X) || !IsFinitePositive(DesiredSize.Y))
	{
		return false;
	}

	OutSize = DesiredSize;
	return true;
}
