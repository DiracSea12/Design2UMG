// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"
#include "Components/TextWidgetTypes.h"
#include "Styling/SlateBrush.h"

class URichTextBlock;
class UTextLayoutWidget;
class UEditableTextBox;
struct FSlateFontInfo;

namespace UMGTextStyleApplication
{
	bool TryResolveTextJustification(const FString& Value, ETextJustify::Type& OutJustification);
	bool HasTypedTextValue(const TOptional<FString>& Value);
	bool IsTypedIconFontWidget(const FUMGWidgetNode& Node);
	FLinearColor NormalizeTypedTextColor(const FLinearColor& Color);

	void ApplyTypedFontInfo(FSlateFontInfo& FontInfo, const FUMGWidgetNode& Node, bool& bFontChanged);

	void ApplyTypedTextLayoutProps(
		UTextLayoutWidget* TextWidget,
		const FUMGWidgetNode& Node,
		const FShapedTextOptions* ShapedTextOptions = nullptr);

	FSlateBrush MakeTextDecorationBrush(const FLinearColor& Color);
	FTextBlockStyle ResolveRichTextBaseStyle(const URichTextBlock* RichTextBlock);
	void ApplyRichTextDefaultStyle(URichTextBlock* RichTextBlock, const FUMGWidgetNode& Node);
	void ApplyRichTextStyleRows(URichTextBlock* RichTextBlock, const TMap<FString, FUMGRichTextStyleRowData>& StyleRows);
}
