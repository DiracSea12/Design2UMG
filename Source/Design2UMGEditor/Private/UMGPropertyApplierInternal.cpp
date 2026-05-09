// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
//
// Extension point: to add property handling for a new Widget type, register it in GetPropsApplierMap()
// with signature void(UWidget*, const FUMGPropertyBag&). Text widgets that also need typed-node
// data (FUMGWidgetNode) must additionally add a branch in ApplyWidgetProperties(UWidget*, const FUMGWidgetNode&)
// and call ApplyCommonProperties after the branch chain.

#include "UMGPropertyApplierInternal.h"
#include "UMGBrushPropertyApplier.h"
#include "UMGPropertyApplierSupport.h"
#include "UMGTextPropertyApplier.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeAccess.h"
#include "Design2UMGLog.h"

#include "Components/Widget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/RetainerBox.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/EditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/ProgressBar.h"
#include "Components/Throbber.h"
#include "Components/CircularThrobber.h"
#include "Components/Spacer.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/GridPanel.h"
#include "Components/UniformGridPanel.h"
#include "Components/WrapBox.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/InvalidationBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/ExpandableArea.h"
#include "Components/TileView.h"
#include "Components/MenuAnchor.h"
#include "Components/SafeZone.h"

namespace UMGPropertyApplierInternal
{
// ============================================================================
// Widget property application (function-pointer dispatch table)
// ============================================================================

namespace
{
	using namespace UMGPropertyApplierSupport;

	void ApplyButtonProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UButton* B = CastChecked<UButton>(W);
		FButtonStyle S = B->GetStyle();
		FLinearColor BackgroundColor;
		if (P.TryGetColor(UMGPropertyKeys::BackgroundColor, BackgroundColor))
		{
			S.Normal.TintColor = FSlateColor(BackgroundColor);
			S.Hovered.TintColor = FSlateColor(BackgroundColor * 1.1f);
			S.Pressed.TintColor = FSlateColor(BackgroundColor * 0.9f);
		}
		FLinearColor HoveredColor;
		if (P.TryGetColor(UMGPropertyKeys::HoveredColor, HoveredColor)) S.Hovered.TintColor = FSlateColor(HoveredColor);
		FLinearColor PressedColor;
		if (P.TryGetColor(UMGPropertyKeys::PressedColor, PressedColor)) S.Pressed.TintColor = FSlateColor(PressedColor);
		B->SetStyle(S);
	}

	void ApplyProgressBarProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UProgressBar* PB = CastChecked<UProgressBar>(W);
		float Percent = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::Percent, Percent)) PB->SetPercent(Percent);
		FLinearColor FillColor;
		if (P.TryGetColor(UMGPropertyKeys::FillColor, FillColor)) PB->SetFillColorAndOpacity(FillColor);
	}

	void ApplyThrobberProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UThrobber* T = CastChecked<UThrobber>(W);
		int32 NumberOfPieces = 0;
		if (P.TryGetInt(UMGPropertyKeys::NumberOfPieces, NumberOfPieces)) T->SetNumberOfPieces(NumberOfPieces);
		bool bAnimate = false;
		if (P.TryGetBool(UMGPropertyKeys::Animate, bAnimate))
		{
			T->SetAnimateHorizontally(bAnimate);
			T->SetAnimateVertically(bAnimate);
			T->SetAnimateOpacity(bAnimate);
		}
		bool bAnimateHorizontally = false;
		if (P.TryGetBool(UMGPropertyKeys::AnimateHorizontally, bAnimateHorizontally)) T->SetAnimateHorizontally(bAnimateHorizontally);
		bool bAnimateVertically = false;
		if (P.TryGetBool(UMGPropertyKeys::AnimateVertically, bAnimateVertically)) T->SetAnimateVertically(bAnimateVertically);
		bool bAnimateOpacity = false;
		if (P.TryGetBool(UMGPropertyKeys::AnimateOpacity, bAnimateOpacity)) T->SetAnimateOpacity(bAnimateOpacity);
	}

	void ApplyCircularThrobberProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UCircularThrobber* T = CastChecked<UCircularThrobber>(W);
		int32 NumberOfPieces = 0;
		if (P.TryGetInt(UMGPropertyKeys::NumberOfPieces, NumberOfPieces)) T->SetNumberOfPieces(NumberOfPieces);
		float Period = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::Period, Period)) T->SetPeriod(Period);
		float Radius = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::Radius, Radius)) T->SetRadius(Radius);
	}

	void ApplySliderProps(UWidget* W, const FUMGPropertyBag& P)
	{
		float Value = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::Value, Value)) CastChecked<USlider>(W)->SetValue(Value);
	}

	void ApplyCheckBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		bool bIsChecked = false;
		if (P.TryGetBool(UMGPropertyKeys::IsChecked, bIsChecked)) CastChecked<UCheckBox>(W)->SetIsChecked(bIsChecked);
	}

	void ApplyEditableTextBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UMGTextPropertyApplier::ApplyEditableTextBoxProps(W, P);
	}

	void ApplyMultiLineEditableTextBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UMGTextPropertyApplier::ApplyMultiLineEditableTextBoxProps(W, P);
	}

	void ApplyEditableTextProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UMGTextPropertyApplier::ApplyEditableTextProps(W, P);
	}

	void ApplyMultiLineEditableTextProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UMGTextPropertyApplier::ApplyMultiLineEditableTextProps(W, P);
	}

	void ApplySizeBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		USizeBox* SB = CastChecked<USizeBox>(W);
		float WidthOverride = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::WidthOverride, WidthOverride)) SB->SetWidthOverride(WidthOverride);
		float HeightOverride = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::HeightOverride, HeightOverride)) SB->SetHeightOverride(HeightOverride);
		float MinDesiredWidth = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::MinDesiredWidth, MinDesiredWidth)) SB->SetMinDesiredWidth(MinDesiredWidth);
		float MinDesiredHeight = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::MinDesiredHeight, MinDesiredHeight)) SB->SetMinDesiredHeight(MinDesiredHeight);
		float MaxDesiredWidth = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::MaxDesiredWidth, MaxDesiredWidth)) SB->SetMaxDesiredWidth(MaxDesiredWidth);
		float MaxDesiredHeight = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::MaxDesiredHeight, MaxDesiredHeight)) SB->SetMaxDesiredHeight(MaxDesiredHeight);
	}

	void ApplyScrollBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UScrollBox* Sc = CastChecked<UScrollBox>(W);
		FString Value;
		if (TryGetProp(P, UMGPropertyKeys::Orientation, Value))
			Sc->SetOrientation(Value == TEXT("Horizontal") ? Orient_Horizontal : Orient_Vertical);
		if (TryGetProp(P, UMGPropertyKeys::ScrollBarVisibility, Value))
		{
			static const TMap<FString, ESlateVisibility> VisMap = {
				{TEXT("Visible"), ESlateVisibility::Visible}, {TEXT("Collapsed"), ESlateVisibility::Collapsed}, {TEXT("Hidden"), ESlateVisibility::Hidden}
			};
			if (const ESlateVisibility* Vis = VisMap.Find(Value)) Sc->SetScrollBarVisibility(*Vis);
		}
	}

	void ApplySpacerProps(UWidget* W, const FUMGPropertyBag& P)
	{
		FVector2D Size(0, 0);
		float Width = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::Width, Width)) Size.X = Width;
		float Height = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::Height, Height)) Size.Y = Height;
		CastChecked<USpacer>(W)->SetSize(Size);
	}

	void ApplyComboBoxStringProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UComboBoxString* CB = CastChecked<UComboBoxString>(W);
		FString Value;
		if (TryGetProp(P, UMGPropertyKeys::Options, Value))
		{
			TArray<FString> Opts;
			Value.ParseIntoArray(Opts, TEXT(","));
			for (const FString& O : Opts) CB->AddOption(O.TrimStartAndEnd());
		}
		if (TryGetPropAllowEmpty(P, UMGPropertyKeys::SelectedOption, Value)) CB->SetSelectedOption(Value);
	}

	void ApplySpinBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		USpinBox* SpB = CastChecked<USpinBox>(W);
		float Value = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::Value, Value)) SpB->SetValue(Value);
		float MinValue = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::MinValue, MinValue)) SpB->SetMinValue(MinValue);
		float MaxValue = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::MaxValue, MaxValue)) SpB->SetMaxValue(MaxValue);
	}

	void ApplyInvalidationBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UInvalidationBox* IB = CastChecked<UInvalidationBox>(W);
		bool bCanCache = false;
		if (P.TryGetBool(UMGPropertyKeys::CanCache, bCanCache)) IB->SetCanCache(bCanCache);
	}

	void ApplyGridPanelProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UGridPanel* G = CastChecked<UGridPanel>(W);
		FString Value;
		if (TryGetProp(P, UMGPropertyKeys::ColumnFill, Value))
		{
			TArray<FString> Fills;
			Value.ParseIntoArray(Fills, TEXT(","));
			for (int32 i = 0; i < Fills.Num(); ++i)
			{
				G->SetColumnFill(i, FCString::Atof(*Fills[i].TrimStartAndEnd()));
			}
		}
		if (TryGetProp(P, UMGPropertyKeys::RowFill, Value))
		{
			TArray<FString> Fills;
			Value.ParseIntoArray(Fills, TEXT(","));
			for (int32 i = 0; i < Fills.Num(); ++i)
			{
				G->SetRowFill(i, FCString::Atof(*Fills[i].TrimStartAndEnd()));
			}
		}
	}

	void ApplyUniformGridPanelProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UUniformGridPanel* UG = CastChecked<UUniformGridPanel>(W);
		FMargin SlotPadding;
		if (P.TryGetMargin(UMGPropertyKeys::SlotPadding, SlotPadding)) UG->SetSlotPadding(SlotPadding);
		float MinDesiredSlotWidth = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::MinDesiredSlotWidth, MinDesiredSlotWidth)) UG->SetMinDesiredSlotWidth(MinDesiredSlotWidth);
		float MinDesiredSlotHeight = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::MinDesiredSlotHeight, MinDesiredSlotHeight)) UG->SetMinDesiredSlotHeight(MinDesiredSlotHeight);
	}

	void ApplyWrapBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UWrapBox* WB = CastChecked<UWrapBox>(W);
		FVector2D InnerSlotPadding;
		if (P.TryGetVector2D(UMGPropertyKeys::InnerSlotPadding, InnerSlotPadding)) WB->SetInnerSlotPadding(InnerSlotPadding);
		FString Value;
		if (TryGetProp(P, UMGPropertyKeys::Orientation, Value))
			WB->SetOrientation(Value == TEXT("Vertical") ? Orient_Vertical : Orient_Horizontal);
	}

	void ApplyScaleBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UScaleBox* SB = CastChecked<UScaleBox>(W);
		FString Value;
		if (TryGetProp(P, UMGPropertyKeys::Stretch, Value))
		{
			static const TMap<FString, EStretch::Type> StretchMap = {
				{TEXT("None"), EStretch::None}, {TEXT("Fill"), EStretch::Fill},
				{TEXT("ScaleToFit"), EStretch::ScaleToFit}, {TEXT("ScaleToFitX"), EStretch::ScaleToFitX},
				{TEXT("ScaleToFitY"), EStretch::ScaleToFitY}, {TEXT("ScaleToFill"), EStretch::ScaleToFill},
				{TEXT("UserSpecified"), EStretch::UserSpecified},
				{TEXT("UserSpecifiedWithClipping"), EStretch::UserSpecifiedWithClipping}
			};
			if (const EStretch::Type* S = StretchMap.Find(Value)) SB->SetStretch(*S);
		}
		if (TryGetProp(P, UMGPropertyKeys::StretchDirection, Value))
		{
			static const TMap<FString, EStretchDirection::Type> DirMap = {
				{TEXT("Both"), EStretchDirection::Both}, {TEXT("DownOnly"), EStretchDirection::DownOnly}, {TEXT("UpOnly"), EStretchDirection::UpOnly}
			};
			if (const EStretchDirection::Type* D = DirMap.Find(Value)) SB->SetStretchDirection(*D);
		}
		float UserSpecifiedScale = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::UserSpecifiedScale, UserSpecifiedScale)) SB->SetUserSpecifiedScale(UserSpecifiedScale);
	}

	void ApplyWidgetSwitcherProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UWidgetSwitcher* WS = CastChecked<UWidgetSwitcher>(W);
		int32 ActiveWidgetIndex = 0;
		if (P.TryGetInt(UMGPropertyKeys::ActiveWidgetIndex, ActiveWidgetIndex)) WS->SetActiveWidgetIndex(ActiveWidgetIndex);
	}

	void ApplyTypedWidgetSwitcherProps(UWidgetSwitcher* WS, const FUMGWidgetStateData& State)
	{
		if (WS && State.bHasActiveWidgetIndex)
		{
			WS->SetActiveWidgetIndex(State.ActiveWidgetIndex);
		}
	}

	void ApplyTypedScrollBoxProps(UScrollBox* Sc, const FUMGScrollBoxData& ScrollBox)
	{
		if (!Sc)
		{
			return;
		}

		if (ScrollBox.bHasOrientation)
		{
			Sc->SetOrientation(ScrollBox.Orientation == TEXT("Horizontal") ? Orient_Horizontal : Orient_Vertical);
		}

		if (ScrollBox.bHasScrollBarVisibility)
		{
			static const TMap<FString, ESlateVisibility> VisMap = {
				{TEXT("Visible"), ESlateVisibility::Visible},
				{TEXT("Collapsed"), ESlateVisibility::Collapsed},
				{TEXT("Hidden"), ESlateVisibility::Hidden},
			};
			if (const ESlateVisibility* Vis = VisMap.Find(ScrollBox.ScrollBarVisibility))
			{
				Sc->SetScrollBarVisibility(*Vis);
			}
		}
	}

	void ApplyExpandableAreaProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UExpandableArea* EA = CastChecked<UExpandableArea>(W);
		bool bIsExpanded = false;
		if (P.TryGetBool(UMGPropertyKeys::IsExpanded, bIsExpanded)) EA->SetIsExpanded(bIsExpanded);
	}

	void ApplyTileViewProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UTileView* TV = CastChecked<UTileView>(W);
		float EntryWidth = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::EntryWidth, EntryWidth)) TV->SetEntryWidth(EntryWidth);
		float EntryHeight = 0.0f;
		if (P.TryGetFloat(UMGPropertyKeys::EntryHeight, EntryHeight)) TV->SetEntryHeight(EntryHeight);
	}

	void ApplyMenuAnchorProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UMenuAnchor* MA = CastChecked<UMenuAnchor>(W);
		FString Value;
		if (TryGetProp(P, UMGPropertyKeys::Placement, Value))
		{
			static const TMap<FString, EMenuPlacement> PlacementMap = {
				{TEXT("MenuPlacement_BelowAnchor"), MenuPlacement_BelowAnchor},
				{TEXT("MenuPlacement_AboveAnchor"), MenuPlacement_AboveAnchor},
				{TEXT("MenuPlacement_ComboBox"), MenuPlacement_ComboBox},
				{TEXT("MenuPlacement_ComboBoxRight"), MenuPlacement_ComboBoxRight},
				{TEXT("MenuPlacement_MenuRight"), MenuPlacement_MenuRight},
				{TEXT("MenuPlacement_MenuLeft"), MenuPlacement_MenuLeft},
				{TEXT("BelowAnchor"), MenuPlacement_BelowAnchor},
				{TEXT("AboveAnchor"), MenuPlacement_AboveAnchor},
			};
			if (const EMenuPlacement* Pl = PlacementMap.Find(Value)) MA->SetPlacement(*Pl);
		}
	}

	void ApplyBackgroundBlurProps(UWidget* W, const FUMGPropertyBag& P)
	{
		float S = 0.f;
		if (P.TryGetFloat(UMGPropertyKeys::BlurStrength, S)) CastChecked<UBackgroundBlur>(W)->SetBlurStrength(S);
	}

	void ApplySafeZoneProps(UWidget* W, const FUMGPropertyBag& P)
	{
		USafeZone* SZ = CastChecked<USafeZone>(W);
		bool bLeft = true, bRight = true, bTop = true, bBottom = true;
		P.TryGetBool(UMGPropertyKeys::PadLeft, bLeft);
		P.TryGetBool(UMGPropertyKeys::PadRight, bRight);
		P.TryGetBool(UMGPropertyKeys::PadTop, bTop);
		P.TryGetBool(UMGPropertyKeys::PadBottom, bBottom);
		SZ->SetSidesToPad(bLeft, bRight, bTop, bBottom);
	}

	// Property application dispatch table
	using FApplyPropsFunc = void(*)(UWidget*, const FUMGPropertyBag&);
	const TMap<UClass*, FApplyPropsFunc>& GetPropsApplierMap()
	{
		static const TMap<UClass*, FApplyPropsFunc> Map = {
			// Text and display widgets
			{UTextBlock::StaticClass(), &UMGTextPropertyApplier::ApplyTextBlockProps},
			{URichTextBlock::StaticClass(), &UMGTextPropertyApplier::ApplyRichTextBlockProps},
			{UImage::StaticClass(), &UMGBrushPropertyApplier::ApplyImageProps},
			{UProgressBar::StaticClass(), &ApplyProgressBarProps},
			{UThrobber::StaticClass(), &ApplyThrobberProps},
			{UCircularThrobber::StaticClass(), &ApplyCircularThrobberProps},
			{USpacer::StaticClass(), &ApplySpacerProps},
			// Interactive widgets
			{UButton::StaticClass(), &ApplyButtonProps},
			{USlider::StaticClass(), &ApplySliderProps},
			{UCheckBox::StaticClass(), &ApplyCheckBoxProps},
			{UEditableTextBox::StaticClass(), &ApplyEditableTextBoxProps},
			{UMultiLineEditableTextBox::StaticClass(), &ApplyMultiLineEditableTextBoxProps},
			{UEditableText::StaticClass(), &ApplyEditableTextProps},
			{UMultiLineEditableText::StaticClass(), &ApplyMultiLineEditableTextProps},
			{UComboBoxString::StaticClass(), &ApplyComboBoxStringProps},
			{USpinBox::StaticClass(), &ApplySpinBoxProps},
			// Container widgets
			{UGridPanel::StaticClass(), &ApplyGridPanelProps},
			{UUniformGridPanel::StaticClass(), &ApplyUniformGridPanelProps},
			{UWrapBox::StaticClass(), &ApplyWrapBoxProps},
			{UScrollBox::StaticClass(), &ApplyScrollBoxProps},
			{UWidgetSwitcher::StaticClass(), &ApplyWidgetSwitcherProps},
			// Wrapper widgets
			{UBorder::StaticClass(), &UMGBrushPropertyApplier::ApplyBorderProps},
			{USizeBox::StaticClass(), &ApplySizeBoxProps},
			{UScaleBox::StaticClass(), &ApplyScaleBoxProps},
			{UBackgroundBlur::StaticClass(), &ApplyBackgroundBlurProps},
			{URetainerBox::StaticClass(), &UMGBrushPropertyApplier::ApplyRetainerBoxProps},
			{UInvalidationBox::StaticClass(), &ApplyInvalidationBoxProps},
			{UExpandableArea::StaticClass(), &ApplyExpandableAreaProps},
			// List widgets
			{UTileView::StaticClass(), &ApplyTileViewProps},
			// Other widgets
			{UMenuAnchor::StaticClass(), &ApplyMenuAnchorProps},
			{USafeZone::StaticClass(), &ApplySafeZoneProps},
		};
		return Map;
	}

}

	void ApplyCommonProperties(UWidget* W, const FUMGPropertyBag& Properties)
	{
		FString StringValue;
		if (Properties.TryGetString(UMGPropertyKeys::Visibility, StringValue))
		{
			W->SetVisibility(FUMGValueParser::ParseVisibility(StringValue));
		}
		if (Properties.TryGetString(UMGPropertyKeys::Clipping, StringValue))
		{
			EWidgetClipping ClippingType;
			if (TryParseWidgetClipping(StringValue, ClippingType))
			{
				W->SetClipping(ClippingType);
			}
		}
		if (Properties.TryGetString(UMGPropertyKeys::ToolTipText, StringValue))
		{
			W->SetToolTipText(FText::FromString(StringValue));
		}
		bool bIsEnabled = false;
		if (Properties.TryGetBool(UMGPropertyKeys::IsEnabled, bIsEnabled)) W->SetIsEnabled(bIsEnabled);
		float RenderOpacity = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::RenderOpacity, RenderOpacity)) W->SetRenderOpacity(RenderOpacity);
		float RenderAngle = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::RenderAngle, RenderAngle)) W->SetRenderTransformAngle(RenderAngle);
		FVector2D RenderTransformPivot;
		if (Properties.TryGetVector2D(UMGPropertyKeys::RenderTransformPivot, RenderTransformPivot))
		{
			W->SetRenderTransformPivot(RenderTransformPivot);
		}
		FVector2D TransformScale;
		if (Properties.TryGetVector2D(UMGPropertyKeys::RenderTransformScale, TransformScale))
		{
			W->SetRenderScale(TransformScale);
		}
		FVector2D RenderTranslation;
		if (Properties.TryGetVector2D(UMGPropertyKeys::RenderTranslation, RenderTranslation)) W->SetRenderTranslation(RenderTranslation);
	}

	void ApplyWidgetProperties(UWidget* Widget, const FUMGPropertyBag& Properties)
	{
		if (!Widget || Properties.Num() == 0) return;

		const auto& ApplierMap = GetPropsApplierMap();
		for (UClass* Class = Widget->GetClass(); Class; Class = Class->GetSuperClass())
		{
			if (const FApplyPropsFunc* Func = ApplierMap.Find(Class))
			{
				(*Func)(Widget, Properties);
				break;
			}
		}

		ApplyCommonProperties(Widget, Properties);
	}

	void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		if (!Widget)
		{
			return;
		}

		const FUMGPropertyBag TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);

		// Text widgets: apply typed-node props, then fall through to shared ApplyCommonProperties below.
		if (Widget->IsA(UTextBlock::StaticClass()))
		{
			UMGTextPropertyApplier::ApplyTextBlockProps(Widget, Node, TypedProperties);
		}
		else if (Widget->IsA(URichTextBlock::StaticClass()))
		{
			UMGTextPropertyApplier::ApplyRichTextBlockProps(Widget, Node);
		}
		else if (Widget->IsA(UEditableTextBox::StaticClass()))
		{
			UMGTextPropertyApplier::ApplyEditableTextBoxProps(Widget, Node, TypedProperties);
		}
		else if (Widget->IsA(UMultiLineEditableTextBox::StaticClass()))
		{
			UMGTextPropertyApplier::ApplyMultiLineEditableTextBoxProps(Widget, Node, TypedProperties);
		}
		else if (Widget->IsA(UEditableText::StaticClass()))
		{
			UMGTextPropertyApplier::ApplyEditableTextProps(Widget, Node, TypedProperties);
		}
		else if (Widget->IsA(UMultiLineEditableText::StaticClass()))
		{
			UMGTextPropertyApplier::ApplyMultiLineEditableTextProps(Widget, Node, TypedProperties);
		}
		else
		{
			// Non-text widgets: delegate to property-bag overload (which calls ApplyCommonProperties internally).
			ApplyWidgetProperties(Widget, TypedProperties);
			return;
		}

		// All text widget branches share a single ApplyCommonProperties call.
		ApplyCommonProperties(Widget, TypedProperties);
	}

	void ApplyTypedOverrides(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		if (UBorder* BorderWidget = Cast<UBorder>(Widget))
		{
			UMGBrushPropertyApplier::ApplyTypedBorderBrush(BorderWidget, Node.Brush);
		}
		if (UBackgroundBlur* BackgroundBlurWidget = Cast<UBackgroundBlur>(Widget))
		{
			UMGBrushPropertyApplier::ApplyTypedBackgroundBlurProps(BackgroundBlurWidget, Node.Effect);
		}
		if (URetainerBox* RetainerBoxWidget = Cast<URetainerBox>(Widget))
		{
			UMGBrushPropertyApplier::ApplyTypedRetainerBoxProps(RetainerBoxWidget, Node.Effect);
		}
		if (UWidgetSwitcher* WidgetSwitcherWidget = Cast<UWidgetSwitcher>(Widget))
		{
			ApplyTypedWidgetSwitcherProps(WidgetSwitcherWidget, Node.WidgetState);
		}
		if (UScrollBox* ScrollBoxWidget = Cast<UScrollBox>(Widget))
		{
			ApplyTypedScrollBoxProps(ScrollBoxWidget, Node.ScrollBox);
		}
	}
}
