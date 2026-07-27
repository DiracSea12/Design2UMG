// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserText.h"
#include "DesignJsonUtils.h"

namespace FigmaParserInternal
{
using DesignJsonUtils::GetNumber;
using DesignJsonUtils::GetObject;
using DesignJsonUtils::GetString;

void ParseTextProperties(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
{
	OutNode.Text.Content = GetString(NodeObj, TEXT("characters"));

	const TSharedPtr<FJsonObject> Style = GetObject(NodeObj, TEXT("style"));
	if (!Style.IsValid())
	{
		return;
	}

	OutNode.Text.FontFamily = GetString(Style, TEXT("fontFamily"));
	OutNode.Text.FontPostScriptName = GetString(Style, TEXT("fontPostScriptName"));
	OutNode.Text.FontSize = GetNumber(Style, TEXT("fontSize"), 16.0);
	OutNode.Text.FontStyle = GetString(Style, TEXT("fontStyle"), TEXT("normal"));

	int32 Weight = FMath::RoundToInt(GetNumber(Style, TEXT("fontWeight"), 400.0));
	Weight = FMath::Clamp(FMath::RoundToInt(Weight / 100.0f) * 100, 100, 900);
	OutNode.Text.FontWeight = FString::FromInt(Weight);

	OutNode.Text.LineHeight = 0.0f;
	const FString LineHeightUnit = GetString(Style, TEXT("lineHeightUnit"));
	if (LineHeightUnit != TEXT("AUTO"))
	{
		if (LineHeightUnit == TEXT("PIXELS"))
		{
			const float LineHeightPx = GetNumber(Style, TEXT("lineHeightPx"));
			if (LineHeightPx > 0.0f && OutNode.Text.FontSize > 0.0f)
			{
				OutNode.Text.LineHeight = LineHeightPx / OutNode.Text.FontSize;
			}
		}
		else
		{
			const float Percent = GetNumber(Style, TEXT("lineHeightPercentFontSize"));
			if (Percent > 0.0f)
			{
				OutNode.Text.LineHeight = Percent / 100.0f;
			}
		}
	}
	if (Style->HasField(TEXT("letterSpacing")))
	{
		OutNode.Text.SetLetterSpacingValue(GetNumber(Style, TEXT("letterSpacing")));
	}
	else
	{
		OutNode.Text.ClearLetterSpacingValue();
	}
	const FString TextDecoration = GetString(Style, TEXT("textDecoration"));
	OutNode.Text.bUnderline = (TextDecoration == TEXT("UNDERLINE"));
	OutNode.Text.bStrikethrough = (TextDecoration == TEXT("STRIKETHROUGH"));
	if (const TSharedPtr<FJsonObject> HyperlinkObj = GetObject(NodeObj, TEXT("hyperlink")))
	{
		OutNode.Text.Href = GetString(HyperlinkObj, TEXT("url"));
	}

	const FString HAlign = GetString(Style, TEXT("textAlignHorizontal"), TEXT("LEFT"));
	if (HAlign == TEXT("CENTER"))
	{
		OutNode.Text.TextAlign = EDesignTextAlign::Center;
	}
	else if (HAlign == TEXT("RIGHT"))
	{
		OutNode.Text.TextAlign = EDesignTextAlign::Right;
	}
	else if (HAlign == TEXT("JUSTIFIED"))
	{
		OutNode.Text.TextAlign = EDesignTextAlign::Justify;
	}
	else
	{
		OutNode.Text.TextAlign = EDesignTextAlign::Left;
	}

	const FString VAlign = GetString(Style, TEXT("textAlignVertical"), TEXT("TOP"));
	if (VAlign == TEXT("CENTER"))
	{
		OutNode.Text.TextAlignVertical = EDesignTextAlignVertical::Middle;
	}
	else if (VAlign == TEXT("BOTTOM"))
	{
		OutNode.Text.TextAlignVertical = EDesignTextAlignVertical::Bottom;
	}
	else
	{
		OutNode.Text.TextAlignVertical = EDesignTextAlignVertical::Top;
	}

	FString AutoResize = GetString(NodeObj, TEXT("textAutoResize"));
	if (AutoResize.IsEmpty())
	{
		AutoResize = GetString(Style, TEXT("textAutoResize"), TEXT("NONE"));
	}

	if (AutoResize == TEXT("WIDTH_AND_HEIGHT"))
	{
		OutNode.Text.TextGrowth = EDesignTextGrowth::Auto;
	}
	else if (AutoResize == TEXT("HEIGHT"))
	{
		OutNode.Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	}
	else
	{
		OutNode.Text.TextGrowth = EDesignTextGrowth::FixedWidthHeight;
		if (AutoResize == TEXT("TRUNCATE"))
		{
			OutNode.Metadata.Add(TEXT("source.text.truncate"), TEXT("true"));
		}
	}
}
}
