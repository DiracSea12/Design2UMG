// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Components/ScrollBox.h"
#include "Framework/Text/TextLayout.h"

class UButton;
class UBorder;
class UDataTable;
class UEditableText;
class UEditableTextBox;
class UGridSlot;
class UImage;
class UMaterialInterface;
class UMultiLineEditableText;
class UMultiLineEditableTextBox;
class URetainerBox;
class URichTextBlock;
class UTextBlock;
class UUniformGridSlot;
class UUserWidget;
class UVerticalBoxSlot;
class UWidget;
struct FButtonStyle;
struct FEditableTextStyle;
struct FEditableTextBoxStyle;
struct FShapedTextOptions;
struct FSlateBrush;
struct FSlateFontInfo;
struct FTextBlockStyle;
struct FWidgetTransform;

namespace Design2UMGWidgetPropertyAccess
{
	FSlateFontInfo GetFont(const UTextBlock& Widget);
	FSlateFontInfo GetFont(const UEditableText& Widget);
	FSlateFontInfo GetFont(const UMultiLineEditableText& Widget);
	FSlateFontInfo GetFont(const FEditableTextBoxStyle& Style);
	void SetFont(FEditableTextBoxStyle& Style, const FSlateFontInfo& Font);
	FSlateColor GetTextColor(const FEditableTextBoxStyle& Style);
	void SetTextColor(FEditableTextBoxStyle& Style, const FSlateColor& Color);
	FText GetHintText(const UEditableText& Widget);
	FText GetHintText(const UEditableTextBox& Widget);
	FText GetHintText(const UMultiLineEditableTextBox& Widget);
	void SetWidgetStyle(UEditableText& Widget, const FEditableTextStyle& Style);
	void SetWidgetStyle(UEditableTextBox& Widget, const FEditableTextBoxStyle& Style);
	void SetWidgetStyle(UMultiLineEditableText& Widget, const FTextBlockStyle& Style);
	void SetWidgetStyle(UMultiLineEditableTextBox& Widget, const FEditableTextBoxStyle& Style);
	void SetShapedTextOptions(UEditableText& Widget, const FShapedTextOptions& Options);
	void SetShapedTextOptions(UEditableTextBox& Widget, const FShapedTextOptions& Options);
	ETextJustify::Type GetJustification(const UEditableText& Widget);
	ETextJustify::Type GetJustification(const UEditableTextBox& Widget);
	void SetMinimumDesiredWidth(UEditableText& Widget, float Width);
	void SetMinimumDesiredWidth(UEditableTextBox& Widget, float Width);
	FSlateColor GetColorAndOpacity(const UTextBlock& Widget);
	FLinearColor GetShadowColorAndOpacity(const UTextBlock& Widget);
	FVector2D GetShadowOffset(const UTextBlock& Widget);
	UDataTable* GetTextStyleSet(const URichTextBlock& Widget);
	FText GetToolTipText(const UWidget& Widget);
	FWidgetTransform GetRenderTransform(const UWidget& Widget);
	FMargin GetPadding(const UBorder& Widget);
	FMargin GetPadding(const UGridSlot& Slot);
	FMargin GetPadding(const UVerticalBoxSlot& Slot);
	int32 GetRow(const UGridSlot& Slot);
	int32 GetColumn(const UGridSlot& Slot);
	int32 GetColumnSpan(const UGridSlot& Slot);
	EHorizontalAlignment GetHorizontalAlignment(const UGridSlot& Slot);
	EVerticalAlignment GetVerticalAlignment(const UGridSlot& Slot);
	int32 GetRow(const UUniformGridSlot& Slot);
	int32 GetColumn(const UUniformGridSlot& Slot);
	FLinearColor GetBrushColor(const UBorder& Widget);
	FBox2f GetUVRegion(const FSlateBrush& Brush);
	void SetUVRegion(FSlateBrush& Brush, const FBox2f& UVRegion);
	const FSlateBrush& GetBrush(const UImage& Image);
	const FButtonStyle& GetButtonStyle(const UButton& Button);
	const UMaterialInterface* GetRetainerEffectMaterialInterface(const URetainerBox& RetainerBox);
	FName GetRetainerTextureParameter(const URetainerBox& RetainerBox);
	bool IsRetainRendering(const URetainerBox& RetainerBox);
	int32 GetRetainerPhase(const URetainerBox& RetainerBox);
	int32 GetRetainerPhaseCount(const URetainerBox& RetainerBox);
	bool IsUserWidgetFocusable(const UUserWidget& Widget);
	void SetUserWidgetFocusable(UUserWidget& Widget, bool bIsFocusable);
	EOrientation GetScrollBoxOrientation(const UScrollBox& ScrollBox);
	ESlateVisibility GetScrollBarVisibility(const UScrollBox& ScrollBox);
}
