// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementRuntimeProbe.h"

#include "Components/TextBlock.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateTypes.h"

namespace
{
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
}

bool TextMeasurementRuntimeProbe::IsAvailable()
{
	return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer() != nullptr;
}

bool TextMeasurementRuntimeProbe::TryMeasureText(const FTextMeasureRequest& Request, FVector2D& OutSize)
{
	OutSize = FVector2D::ZeroVector;
	if (Request.FontSize <= 0.0f || !IsAvailable())
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
