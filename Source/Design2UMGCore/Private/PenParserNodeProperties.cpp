// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParserInternal.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "ParserSharedSemantics.h"

namespace PenParser
{
void ApplyNodeBoxProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, EDesignNodeType SizeNodeType, const FDesignVariableResolver& Resolver)
{
	if (JsonObj->HasField(TEXT("width")))
	{
		OutNode.Transform.SetWidthValue(ParseSize(JsonObj->TryGetField(TEXT("width")), SizeNodeType, Resolver));
	}
	if (JsonObj->HasField(TEXT("height")))
	{
		OutNode.Transform.SetHeightValue(ParseSize(JsonObj->TryGetField(TEXT("height")), SizeNodeType, Resolver));
	}
	if (JsonObj->HasField(TEXT("x")))
	{
		if (TryGetResolvedNumberField(JsonObj, TEXT("x"), Resolver, OutNode.Transform.X))
			OutNode.Transform.ExplicitX = OutNode.Transform.X;
		else
			OutNode.Transform.ExplicitX.Reset();
	}
	if (JsonObj->HasField(TEXT("y")))
	{
		if (TryGetResolvedNumberField(JsonObj, TEXT("y"), Resolver, OutNode.Transform.Y))
			OutNode.Transform.ExplicitY = OutNode.Transform.Y;
		else
			OutNode.Transform.ExplicitY.Reset();
	}
}

void ApplyNodeLayoutProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver, bool bIncludeTextProperties)
{
	if (JsonObj->HasField(TEXT("opacity")))
	{
		if (TryGetResolvedNumberField(JsonObj, TEXT("opacity"), Resolver, OutNode.Transform.Opacity))
			OutNode.Transform.ExplicitOpacity = OutNode.Transform.Opacity;
		else
			OutNode.Transform.ExplicitOpacity.Reset();
	}
	if (JsonObj->HasField(TEXT("gap")))
	{
		float ResolvedGap = OutNode.LayoutData.ExplicitGap.Get(0.0f);
		if (TryGetResolvedNumberField(JsonObj, TEXT("gap"), Resolver, ResolvedGap))
			OutNode.LayoutData.SetGap(ResolvedGap);
	}
	if (JsonObj->HasField(TEXT("padding")))
	{
		OutNode.LayoutData.SetPadding(ParsePadding(JsonObj->TryGetField(TEXT("padding")), Resolver));
	}
	OutNode.LayoutData.PaddingRequiresRuntimeWrap =
		OutNode.LayoutData.Layout == EDesignLayout::None
		|| OutNode.LayoutData.Layout == EDesignLayout::Wrap
		|| OutNode.LayoutData.Layout == EDesignLayout::Grid;
	FString JustifyContent;
	if (JsonObj->TryGetStringField(TEXT("justifyContent"), JustifyContent))
		OutNode.LayoutData.JustifyContent = ParserSharedSemantics::ParseJustifyContentToken(JustifyContent);
	FString AlignItems;
	if (JsonObj->TryGetStringField(TEXT("alignItems"), AlignItems))
		OutNode.LayoutData.AlignItems = ParserSharedSemantics::ParseAlignItemsToken(AlignItems);
	if (JsonObj->HasField(TEXT("clip")))
	{
		bool bClipValue = false;
		if (TryGetResolvedBoolField(JsonObj, TEXT("clip"), Resolver, bClipValue))
			OutNode.LayoutData.SetClipValue(bClipValue);
		else
			OutNode.LayoutData.ExplicitClip.Reset();
	}
	if (!bIncludeTextProperties)
		return;
	FString TextAlign;
	if (JsonObj->TryGetStringField(TEXT("textAlign"), TextAlign))
		OutNode.Text.TextAlign = ParserSharedSemantics::ParseTextAlignToken(TextAlign);
	float LineHeight = 0.0f;
	if (TryGetResolvedNumberField(JsonObj, TEXT("lineHeight"), Resolver, LineHeight) && LineHeight > 0.0f)
	{
		OutNode.Text.LineHeight = LineHeight;
	}
	FString TextGrowth;
	if (JsonObj->TryGetStringField(TEXT("textGrowth"), TextGrowth))
		OutNode.Text.TextGrowth = ParseTextGrowth(TextGrowth);
}

void ApplyNodeTextStyleProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver)
{
	// Pencil's paragraph renderer distributes authored line-height leading on both sides.
	OutNode.Text.bSourceHalfLeadingBaseline = true;
	OutNode.Text.bSourceRoundLineHeightToPixels = true;
	FString FontFamily;
	if (JsonObj->TryGetStringField(TEXT("fontFamily"), FontFamily))
		OutNode.Text.FontFamily = Resolver.ResolveString(FontFamily);
	JsonObj->TryGetStringField(TEXT("fontPostScriptName"), OutNode.Text.FontPostScriptName);
	TryGetResolvedNumberField(JsonObj, TEXT("fontSize"), Resolver, OutNode.Text.FontSize);
	JsonObj->TryGetStringField(TEXT("fontWeight"), OutNode.Text.FontWeight);
	JsonObj->TryGetStringField(TEXT("fontStyle"), OutNode.Text.FontStyle);
	float LetterSpacing = 0.0f;
	if (TryGetResolvedNumberField(JsonObj, TEXT("letterSpacing"), Resolver, LetterSpacing))
	{
		OutNode.Text.SetLetterSpacingValue(LetterSpacing);
	}
	TryGetResolvedBoolField(JsonObj, TEXT("underline"), Resolver, OutNode.Text.bUnderline);
	TryGetResolvedBoolField(JsonObj, TEXT("strikethrough"), Resolver, OutNode.Text.bStrikethrough);
	JsonObj->TryGetStringField(TEXT("href"), OutNode.Text.Href);
}
}
