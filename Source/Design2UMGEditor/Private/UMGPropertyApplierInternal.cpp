// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
//
// Extension point: to add property handling for a new Widget type, register a
// typed-node applier in GetPropsApplierMap().

#include "UMGPropertyApplierInternal.h"
#include "Design2UMGWidgetMetadata.h"
#include "Design2UMGWidgetPropertyAccess.h"

#include "UMGBrushPropertyApplier.h"
#include "UMGPropertyApplierSupport.h"
#include "UMGTextPropertyApplier.h"
#include "UMGValueParsing.h"

#include "Misc/EngineVersionComparison.h"

#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/RetainerBox.h"
#include "Components/RichTextBlock.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"

namespace UMGPropertyApplierInternal
{
namespace
{
	using FApplyPropsFunc = void(*)(UWidget*, const FUMGWidgetNode&);

	void ApplyButtonProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UButton* Button = CastChecked<UButton>(Widget);
		FButtonStyle Style = Design2UMGWidgetPropertyAccess::GetButtonStyle(*Button);
		if (Node.WidgetStyle.BackgroundColor.IsSet())
		{
			const FLinearColor BackgroundColor = Node.WidgetStyle.BackgroundColor.GetValue();
			Style.Normal.TintColor = FSlateColor(BackgroundColor);
			Style.Hovered.TintColor = FSlateColor(UMGPropertyApplierSupport::MakeHoverTintColor(BackgroundColor));
			Style.Pressed.TintColor = FSlateColor(UMGPropertyApplierSupport::MakePressedTintColor(BackgroundColor));
		}
		if (Node.WidgetStyle.HoveredColor.IsSet())
		{
			Style.Hovered.TintColor = FSlateColor(Node.WidgetStyle.HoveredColor.GetValue());
		}
		if (Node.WidgetStyle.PressedColor.IsSet())
		{
			Style.Pressed.TintColor = FSlateColor(Node.WidgetStyle.PressedColor.GetValue());
		}
		Button->SetStyle(Style);
	}

	void ApplySizeBoxProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		USizeBox* SizeBox = CastChecked<USizeBox>(Widget);
		if (Node.WidgetStyle.WidthOverride.IsSet())
		{
			SizeBox->SetWidthOverride(Node.WidgetStyle.WidthOverride.GetValue());
		}
		if (Node.WidgetStyle.HeightOverride.IsSet())
		{
			SizeBox->SetHeightOverride(Node.WidgetStyle.HeightOverride.GetValue());
		}
		if (Node.WidgetStyle.MinDesiredWidth.IsSet())
		{
			SizeBox->SetMinDesiredWidth(Node.WidgetStyle.MinDesiredWidth.GetValue());
		}
		if (Node.WidgetStyle.MinDesiredHeight.IsSet())
		{
			SizeBox->SetMinDesiredHeight(Node.WidgetStyle.MinDesiredHeight.GetValue());
		}
	}

	void ApplyScrollBoxProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UScrollBox* ScrollBox = CastChecked<UScrollBox>(Widget);
		if (Node.ScrollBox.Orientation.IsSet())
		{
			ScrollBox->SetOrientation(Node.ScrollBox.Orientation.GetValue() == TEXT("Horizontal") ? Orient_Horizontal : Orient_Vertical);
		}
		if (Node.ScrollBox.ScrollBarVisibility.IsSet())
		{
			static const TMap<FString, ESlateVisibility> VisibilityMap = {
				{TEXT("Visible"), ESlateVisibility::Visible},
				{TEXT("Collapsed"), ESlateVisibility::Collapsed},
				{TEXT("Hidden"), ESlateVisibility::Hidden},
			};
			if (const ESlateVisibility* Visibility = VisibilityMap.Find(Node.ScrollBox.ScrollBarVisibility.GetValue()))
			{
				ScrollBox->SetScrollBarVisibility(*Visibility);
			}
		}
	}

	void ApplyUniformGridPanelProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UUniformGridPanel* UniformGrid = CastChecked<UUniformGridPanel>(Widget);
		if (Node.WidgetStyle.SlotPadding.IsSet())
		{
			UniformGrid->SetSlotPadding(Node.WidgetStyle.SlotPadding.GetValue());
		}
		if (Node.WidgetStyle.MinDesiredWidth.IsSet())
		{
			UniformGrid->SetMinDesiredSlotWidth(Node.WidgetStyle.MinDesiredWidth.GetValue());
		}
		if (Node.WidgetStyle.MinDesiredHeight.IsSet())
		{
			UniformGrid->SetMinDesiredSlotHeight(Node.WidgetStyle.MinDesiredHeight.GetValue());
		}
	}

	void ApplyWrapBoxProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UWrapBox* WrapBox = CastChecked<UWrapBox>(Widget);
		if (Node.WidgetStyle.InnerSlotPadding.IsSet())
		{
			WrapBox->SetInnerSlotPadding(Node.WidgetStyle.InnerSlotPadding.GetValue());
		}
	}

	void ApplyScaleBoxProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UScaleBox* ScaleBox = CastChecked<UScaleBox>(Widget);
		if (Node.WidgetStyle.Stretch.IsSet())
		{
			static const TMap<FString, EStretch::Type> StretchMap = {
				{TEXT("None"), EStretch::None},
				{TEXT("Fill"), EStretch::Fill},
				{TEXT("ScaleToFit"), EStretch::ScaleToFit},
				{TEXT("ScaleToFitX"), EStretch::ScaleToFitX},
				{TEXT("ScaleToFitY"), EStretch::ScaleToFitY},
				{TEXT("ScaleToFill"), EStretch::ScaleToFill},
				{TEXT("UserSpecified"), EStretch::UserSpecified},
#if !UE_VERSION_OLDER_THAN(5, 1, 0)
				{TEXT("UserSpecifiedWithClipping"), EStretch::UserSpecifiedWithClipping},
#endif
			};
			if (const EStretch::Type* Stretch = StretchMap.Find(Node.WidgetStyle.Stretch.GetValue()))
			{
				ScaleBox->SetStretch(*Stretch);
			}
#if UE_VERSION_OLDER_THAN(5, 1, 0)
			else if (Node.WidgetStyle.Stretch.GetValue() == TEXT("UserSpecifiedWithClipping"))
			{
				ScaleBox->SetStretch(EStretch::UserSpecified);
			}
#endif
		}
		if (Node.WidgetStyle.StretchDirection.IsSet())
		{
			static const TMap<FString, EStretchDirection::Type> DirectionMap = {
				{TEXT("Both"), EStretchDirection::Both},
				{TEXT("DownOnly"), EStretchDirection::DownOnly},
				{TEXT("UpOnly"), EStretchDirection::UpOnly},
			};
			if (const EStretchDirection::Type* Direction = DirectionMap.Find(Node.WidgetStyle.StretchDirection.GetValue()))
			{
				ScaleBox->SetStretchDirection(*Direction);
			}
		}
	}

#if UE_VERSION_OLDER_THAN(5, 1, 0)
	void ApplyScaleBoxCompatibilityClipping(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UScaleBox* ScaleBox = Cast<UScaleBox>(Widget);
		if (ScaleBox
			&& Node.WidgetStyle.Stretch.Get(FString()) == TEXT("UserSpecifiedWithClipping")
			&& ScaleBox->GetClipping() == EWidgetClipping::Inherit)
		{
			ScaleBox->SetClipping(EWidgetClipping::ClipToBounds);
		}
	}
#endif

	void ApplyWidgetSwitcherProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UWidgetSwitcher* WidgetSwitcher = CastChecked<UWidgetSwitcher>(Widget);
		if (Node.WidgetState.ActiveWidgetIndex.IsSet())
		{
			WidgetSwitcher->SetActiveWidgetIndex(Node.WidgetState.ActiveWidgetIndex.GetValue());
		}
	}

	void ApplyBackgroundBlurProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UMGBrushPropertyApplier::ApplyTypedBackgroundBlurProps(CastChecked<UBackgroundBlur>(Widget), Node.Effect);
	}

	const TMap<UClass*, FApplyPropsFunc>& GetPropsApplierMap()
	{
		static const TMap<UClass*, FApplyPropsFunc> Map = {
			{UTextBlock::StaticClass(), &UMGTextPropertyApplier::ApplyTextBlockProps},
			{URichTextBlock::StaticClass(), &UMGTextPropertyApplier::ApplyRichTextBlockProps},
			{UImage::StaticClass(), &UMGBrushPropertyApplier::ApplyImageProps},
			{UButton::StaticClass(), &ApplyButtonProps},
			{UEditableTextBox::StaticClass(), &UMGTextPropertyApplier::ApplyEditableTextBoxProps},
			{UMultiLineEditableTextBox::StaticClass(), &UMGTextPropertyApplier::ApplyMultiLineEditableTextBoxProps},
			{UEditableText::StaticClass(), &UMGTextPropertyApplier::ApplyEditableTextProps},
			{UMultiLineEditableText::StaticClass(), &UMGTextPropertyApplier::ApplyMultiLineEditableTextProps},
			{UUniformGridPanel::StaticClass(), &ApplyUniformGridPanelProps},
			{UWrapBox::StaticClass(), &ApplyWrapBoxProps},
			{UScrollBox::StaticClass(), &ApplyScrollBoxProps},
			{UWidgetSwitcher::StaticClass(), &ApplyWidgetSwitcherProps},
			{USizeBox::StaticClass(), &ApplySizeBoxProps},
			{UScaleBox::StaticClass(), &ApplyScaleBoxProps},
			{UBackgroundBlur::StaticClass(), &ApplyBackgroundBlurProps},
			{URetainerBox::StaticClass(), &UMGBrushPropertyApplier::ApplyRetainerBoxProps},
		};
		return Map;
	}
}

	void ApplyCommonProperties(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		if (!Widget)
		{
			return;
		}

		if (Node.Render.BackgroundCaptureInsets.IsSet())
			Design2UMGWidgetMetadata::SetBackgroundCaptureInsets(Widget, Node.Render.BackgroundCaptureInsets.GetValue());

		if (Node.WidgetStyle.Visibility.IsSet())
		{
			Widget->SetVisibility(FUMGValueParser::ParseVisibility(Node.WidgetStyle.Visibility.GetValue()));
		}
		if (Node.Render.Clipping.IsSet())
		{
			EWidgetClipping ClippingType;
			if (UMGPropertyApplierSupport::TryParseWidgetClipping(Node.Render.Clipping.GetValue(), ClippingType))
			{
				Widget->SetClipping(ClippingType);
			}
		}
		if (Node.WidgetStyle.ToolTipText.IsSet())
		{
			Widget->SetToolTipText(FText::FromString(Node.WidgetStyle.ToolTipText.GetValue()));
		}
		if (Node.WidgetStyle.IsEnabled.IsSet())
		{
			Widget->SetIsEnabled(Node.WidgetStyle.IsEnabled.GetValue());
		}
		if (Node.Render.RenderOpacity.IsSet())
		{
			Widget->SetRenderOpacity(Node.Render.RenderOpacity.GetValue());
		}
		if (Node.Render.RenderAngle.IsSet())
		{
			Widget->SetRenderTransformAngle(Node.Render.RenderAngle.GetValue());
		}
		if (Node.Render.RenderTransformPivot.IsSet())
		{
			Widget->SetRenderTransformPivot(Node.Render.RenderTransformPivot.GetValue());
		}
		if (Node.Render.RenderTransformScale.IsSet())
		{
			Widget->SetRenderScale(Node.Render.RenderTransformScale.GetValue());
		}
		if (Node.Render.RenderTranslation.IsSet() || Node.Text.LineHeightBaselineShift.IsSet())
		{
			FVector2D Translation = Node.Render.RenderTranslation.Get(FVector2D::ZeroVector);
			if (Node.Text.LineHeightBaselineShift.IsSet())
			{
				// Explicit line-height baseline compensation travels with any authored
				// render translation; it must not move wrapper backgrounds, so it is
				// folded in here rather than going through the shared render channel.
				Translation.Y += Node.Text.LineHeightBaselineShift.GetValue();
			}
			Widget->SetRenderTranslation(Translation);
		}
	}

	void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node, DesignImageResources::FImageOperationBudget* Budget, const TMap<FString, UTexture2D*>* TextureMap)
	{
		if (!Widget)
		{
			return;
		}

		const TMap<UClass*, FApplyPropsFunc>& ApplierMap = GetPropsApplierMap();
		for (UClass* Class = Widget->GetClass(); Class; Class = Class->GetSuperClass())
		{
			if (Class == UBorder::StaticClass())
			{
				UMGBrushPropertyApplier::ApplyBorderProps(Widget, Node, Budget, TextureMap);
				break;
			}
			if (const FApplyPropsFunc* Func = ApplierMap.Find(Class))
			{
				(*Func)(Widget, Node);
				break;
			}
		}

		ApplyCommonProperties(Widget, Node);
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		ApplyScaleBoxCompatibilityClipping(Widget, Node);
#endif
	}
}
