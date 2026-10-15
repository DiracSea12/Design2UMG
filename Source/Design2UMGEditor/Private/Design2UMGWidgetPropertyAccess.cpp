// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGWidgetPropertyAccess.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/GridSlot.h"
#include "Components/Image.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/RetainerBox.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/DataTable.h"
#include "Materials/MaterialInterface.h"
#include "Misc/EngineVersionComparison.h"
#include "Styling/SlateBrush.h"
#include "UObject/UnrealType.h"

#if UE_VERSION_OLDER_THAN(5, 1, 0)
#include "Widgets/Input/SEditableText.h"
#endif

namespace Design2UMGWidgetPropertyAccess
{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
	namespace
	{
		TSharedPtr<SEditableText> GetNativeEditableText(const UEditableText& Widget)
		{
			const TSharedPtr<SWidget> CachedWidget = Widget.GetCachedWidget();
			// A native subclass may return a wrapper from RebuildWidget(). Only the
			// built-in root is safe to cast to SEditableText.
			if (CachedWidget.IsValid() && CachedWidget->GetType() == FName(TEXT("SEditableText")))
			{
				return StaticCastSharedPtr<SEditableText>(CachedWidget);
			}
			return nullptr;
		}
	}

	FSlateFontInfo GetFont(const UTextBlock& Widget) { return Widget.Font; }
	FSlateFontInfo GetFont(const UEditableText& Widget) { return Widget.WidgetStyle.Font; }
	FSlateFontInfo GetFont(const UMultiLineEditableText& Widget) { return Widget.WidgetStyle.Font; }
	FSlateFontInfo GetFont(const FEditableTextBoxStyle& Style) { return Style.Font; }
	void SetFont(FEditableTextBoxStyle& Style, const FSlateFontInfo& Font) { Style.SetFont(Font); }
	FSlateColor GetTextColor(const FEditableTextBoxStyle& Style) { return Style.ForegroundColor; }
	void SetTextColor(FEditableTextBoxStyle& Style, const FSlateColor& Color) { Style.SetForegroundColor(Color); }
	FText GetHintText(const UEditableText& Widget) { return Widget.HintText; }
	FText GetHintText(const UEditableTextBox& Widget) { return Widget.HintText; }
	FText GetHintText(const UMultiLineEditableTextBox& Widget) { return Widget.HintText; }
	ETextJustify::Type GetJustification(const UEditableText& Widget) { return Widget.Justification; }
	ETextJustify::Type GetJustification(const UEditableTextBox& Widget) { return Widget.Justification; }
	FSlateColor GetColorAndOpacity(const UTextBlock& Widget) { return Widget.ColorAndOpacity; }
	FLinearColor GetShadowColorAndOpacity(const UTextBlock& Widget) { return Widget.ShadowColorAndOpacity; }
	FVector2D GetShadowOffset(const UTextBlock& Widget) { return Widget.ShadowOffset; }
	UDataTable* GetTextStyleSet(const URichTextBlock& Widget)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(URichTextBlock::StaticClass(), TEXT("TextStyleSet"));
		check(Property);
		return Cast<UDataTable>(Property->GetObjectPropertyValue_InContainer(&Widget));
	}
	FText GetToolTipText(const UWidget& Widget) { return Widget.ToolTipText; }
	FWidgetTransform GetRenderTransform(const UWidget& Widget) { return Widget.RenderTransform; }
	FMargin GetPadding(const UBorder& Widget) { return Widget.Padding; }
	FMargin GetPadding(const UGridSlot& Slot) { return Slot.Padding; }
	FMargin GetPadding(const UVerticalBoxSlot& Slot) { return Slot.Padding; }
	int32 GetRow(const UGridSlot& Slot) { return Slot.Row; }
	int32 GetColumn(const UGridSlot& Slot) { return Slot.Column; }
	int32 GetColumnSpan(const UGridSlot& Slot) { return Slot.ColumnSpan; }
	EHorizontalAlignment GetHorizontalAlignment(const UGridSlot& Slot) { return Slot.HorizontalAlignment; }
	EVerticalAlignment GetVerticalAlignment(const UGridSlot& Slot) { return Slot.VerticalAlignment; }
	int32 GetRow(const UUniformGridSlot& Slot) { return Slot.Row; }
	int32 GetColumn(const UUniformGridSlot& Slot) { return Slot.Column; }
	FLinearColor GetBrushColor(const UBorder& Widget) { return Widget.BrushColor; }
	void SetMinimumDesiredWidth(UEditableText& Widget, float Width)
	{
		Widget.MinimumDesiredWidth = Width;
		if (const TSharedPtr<SEditableText> EditableText = GetNativeEditableText(Widget))
		{
			EditableText->SetMinDesiredWidth(Width);
		}
	}
	void SetMinimumDesiredWidth(UEditableTextBox& Widget, float Width)
	{
		Widget.MinimumDesiredWidth = Width;
		if (Widget.GetCachedWidget().IsValid()) Widget.SynchronizeProperties();
	}
#else
	FSlateFontInfo GetFont(const UTextBlock& Widget) { return Widget.GetFont(); }
	FSlateFontInfo GetFont(const UEditableText& Widget) { return Widget.GetFont(); }
	FSlateFontInfo GetFont(const UMultiLineEditableText& Widget) { return Widget.GetFont(); }
	FSlateFontInfo GetFont(const FEditableTextBoxStyle& Style) { return Style.TextStyle.Font; }
	void SetFont(FEditableTextBoxStyle& Style, const FSlateFontInfo& Font) { Style.TextStyle.SetFont(Font); }
	FSlateColor GetTextColor(const FEditableTextBoxStyle& Style) { return Style.TextStyle.ColorAndOpacity; }
	void SetTextColor(FEditableTextBoxStyle& Style, const FSlateColor& Color)
	{
		Style.TextStyle.SetColorAndOpacity(Color);
		Style.SetForegroundColor(Color);
	}
	FText GetHintText(const UEditableText& Widget) { return Widget.GetHintText(); }
	FText GetHintText(const UEditableTextBox& Widget) { return Widget.GetHintText(); }
	FText GetHintText(const UMultiLineEditableTextBox& Widget) { return Widget.GetHintText(); }
	ETextJustify::Type GetJustification(const UEditableText& Widget) { return Widget.GetJustification(); }
	ETextJustify::Type GetJustification(const UEditableTextBox& Widget) { return Widget.GetJustification(); }
	FSlateColor GetColorAndOpacity(const UTextBlock& Widget) { return Widget.GetColorAndOpacity(); }
	FLinearColor GetShadowColorAndOpacity(const UTextBlock& Widget) { return Widget.GetShadowColorAndOpacity(); }
	FVector2D GetShadowOffset(const UTextBlock& Widget) { return Widget.GetShadowOffset(); }
	UDataTable* GetTextStyleSet(const URichTextBlock& Widget) { return Widget.GetTextStyleSet(); }
	FText GetToolTipText(const UWidget& Widget) { return Widget.GetToolTipText(); }
	FWidgetTransform GetRenderTransform(const UWidget& Widget) { return Widget.GetRenderTransform(); }
	FMargin GetPadding(const UBorder& Widget) { return Widget.GetPadding(); }
	FMargin GetPadding(const UGridSlot& Slot) { return Slot.GetPadding(); }
	FMargin GetPadding(const UVerticalBoxSlot& Slot) { return Slot.GetPadding(); }
	int32 GetRow(const UGridSlot& Slot) { return Slot.GetRow(); }
	int32 GetColumn(const UGridSlot& Slot) { return Slot.GetColumn(); }
	int32 GetColumnSpan(const UGridSlot& Slot) { return Slot.GetColumnSpan(); }
	EHorizontalAlignment GetHorizontalAlignment(const UGridSlot& Slot) { return Slot.GetHorizontalAlignment(); }
	EVerticalAlignment GetVerticalAlignment(const UGridSlot& Slot) { return Slot.GetVerticalAlignment(); }
	int32 GetRow(const UUniformGridSlot& Slot) { return Slot.GetRow(); }
	int32 GetColumn(const UUniformGridSlot& Slot) { return Slot.GetColumn(); }
	FLinearColor GetBrushColor(const UBorder& Widget) { return Widget.GetBrushColor(); }
	void SetMinimumDesiredWidth(UEditableText& Widget, float Width) { Widget.SetMinimumDesiredWidth(Width); }
	void SetMinimumDesiredWidth(UEditableTextBox& Widget, float Width) { Widget.SetMinDesiredWidth(Width); }
#endif

	void SetWidgetStyle(UEditableText& Widget, const FEditableTextStyle& Style)
	{
#if UE_VERSION_OLDER_THAN(5, 2, 0)
	Widget.WidgetStyle = Style;
	if (Widget.GetCachedWidget().IsValid())
	{
		Widget.SynchronizeProperties();
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		if (const TSharedPtr<SEditableText> EditableText = GetNativeEditableText(Widget))
		{
			EditableText->SetFont(Style.Font);
			EditableText->SetColorAndOpacity(Style.ColorAndOpacity);
		}
#endif
	}
#else
		Widget.SetWidgetStyle(Style);
#endif
	}

	void SetWidgetStyle(UEditableTextBox& Widget, const FEditableTextBoxStyle& Style)
	{
#if UE_VERSION_OLDER_THAN(5, 7, 0)
		Widget.WidgetStyle = Style;
		if (Widget.GetCachedWidget().IsValid())
		{
			Widget.SynchronizeProperties();
		}
#else
		Widget.SetWidgetStyle(Style);
#endif
	}

	void SetWidgetStyle(UMultiLineEditableText& Widget, const FTextBlockStyle& Style)
	{
		Widget.SetWidgetStyle(Style);
	}

	void SetWidgetStyle(UMultiLineEditableTextBox& Widget, const FEditableTextBoxStyle& Style)
	{
		Widget.WidgetStyle = Style;
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		FTextBlockStyle TextStyle = Widget.TextStyle;
		TextStyle.SetFont(Style.Font);
		TextStyle.SetColorAndOpacity(Style.ForegroundColor);
		Widget.SetTextStyle(TextStyle);
		if (Widget.GetCachedWidget().IsValid())
		{
			Widget.SynchronizeProperties();
		}
#else
		Widget.SetTextStyle(Style.TextStyle);
#endif
	}

	void SetShapedTextOptions(UEditableText& Widget, const FShapedTextOptions& Options)
	{
		Widget.ShapedTextOptions = Options;
		if (Widget.GetCachedWidget().IsValid())
		{
			Widget.SynchronizeProperties();
		}
	}

	void SetShapedTextOptions(UEditableTextBox& Widget, const FShapedTextOptions& Options)
	{
		Widget.ShapedTextOptions = Options;
		if (Widget.GetCachedWidget().IsValid())
		{
			Widget.SynchronizeProperties();
		}
	}

	FBox2f GetUVRegion(const FSlateBrush& Brush)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		const FBox2D UVRegion = Brush.GetUVRegion();
		return FBox2f(
			FVector2f(static_cast<float>(UVRegion.Min.X), static_cast<float>(UVRegion.Min.Y)),
			FVector2f(static_cast<float>(UVRegion.Max.X), static_cast<float>(UVRegion.Max.Y)));
#else
		return Brush.GetUVRegion();
#endif
	}

	void SetUVRegion(FSlateBrush& Brush, const FBox2f& UVRegion)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		Brush.SetUVRegion(FBox2D(
			FVector2D(UVRegion.Min.X, UVRegion.Min.Y),
			FVector2D(UVRegion.Max.X, UVRegion.Max.Y)));
#else
		Brush.SetUVRegion(UVRegion);
#endif
	}

#if UE_VERSION_OLDER_THAN(5, 2, 0)
	const FSlateBrush& GetBrush(const UImage& Image) { return Image.Brush; }
	const FButtonStyle& GetButtonStyle(const UButton& Button) { return Button.WidgetStyle; }

	const UMaterialInterface* GetRetainerEffectMaterialInterface(const URetainerBox& RetainerBox)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(URetainerBox::StaticClass(), TEXT("EffectMaterial"));
		check(Property);
		return Cast<UMaterialInterface>(Property->GetObjectPropertyValue_InContainer(&RetainerBox));
	}

	FName GetRetainerTextureParameter(const URetainerBox& RetainerBox)
	{
		const FNameProperty* Property = FindFProperty<FNameProperty>(URetainerBox::StaticClass(), TEXT("TextureParameter"));
		check(Property);
		return *Property->ContainerPtrToValuePtr<FName>(&RetainerBox);
	}

	bool IsRetainRendering(const URetainerBox& RetainerBox)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(URetainerBox::StaticClass(), TEXT("bRetainRender"));
		check(Property);
		return Property->GetPropertyValue_InContainer(&RetainerBox);
	}

	int32 GetRetainerPhase(const URetainerBox& RetainerBox) { return RetainerBox.Phase; }
	int32 GetRetainerPhaseCount(const URetainerBox& RetainerBox) { return RetainerBox.PhaseCount; }
	bool IsUserWidgetFocusable(const UUserWidget& Widget) { return Widget.bIsFocusable; }
	void SetUserWidgetFocusable(UUserWidget& Widget, const bool bIsFocusable) { Widget.bIsFocusable = bIsFocusable; }
	EOrientation GetScrollBoxOrientation(const UScrollBox& ScrollBox) { return ScrollBox.Orientation; }
	ESlateVisibility GetScrollBarVisibility(const UScrollBox& ScrollBox) { return ScrollBox.ScrollBarVisibility; }
#else
	const FSlateBrush& GetBrush(const UImage& Image) { return Image.GetBrush(); }
	const FButtonStyle& GetButtonStyle(const UButton& Button) { return Button.GetStyle(); }
	const UMaterialInterface* GetRetainerEffectMaterialInterface(const URetainerBox& RetainerBox) { return RetainerBox.GetEffectMaterialInterface(); }
	FName GetRetainerTextureParameter(const URetainerBox& RetainerBox) { return RetainerBox.GetTextureParameter(); }
	bool IsRetainRendering(const URetainerBox& RetainerBox) { return RetainerBox.IsRetainRendering(); }
	int32 GetRetainerPhase(const URetainerBox& RetainerBox) { return RetainerBox.GetPhase(); }
	int32 GetRetainerPhaseCount(const URetainerBox& RetainerBox) { return RetainerBox.GetPhaseCount(); }
	bool IsUserWidgetFocusable(const UUserWidget& Widget) { return Widget.IsFocusable(); }
	void SetUserWidgetFocusable(UUserWidget& Widget, const bool bIsFocusable) { Widget.SetIsFocusable(bIsFocusable); }
	EOrientation GetScrollBoxOrientation(const UScrollBox& ScrollBox) { return ScrollBox.GetOrientation(); }
	ESlateVisibility GetScrollBarVisibility(const UScrollBox& ScrollBox) { return ScrollBox.GetScrollBarVisibility(); }
#endif
}
