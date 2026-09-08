// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Central vocabulary for UMG widget type names carried by FUMGWidgetNode::Type.
 *
 * Type stays a string on purpose: it is the wire format of the IR JSON bridge
 * (dump/reimport/automation) and of the editor widget-class map, and it is what
 * the verbose tree dumps print. The cost is that no compiler catches a typo, so
 * every producer and consumer must take the spelling from this header instead
 * of writing a literal - a rename then has exactly one place to change.
 *
 * Same role for widget types that UMGPropertyKeys.h plays for property keys.
 */
namespace UMGWidgetTypes
{
	inline constexpr const TCHAR* CanvasPanel = TEXT("CanvasPanel");
	inline constexpr const TCHAR* VerticalBox = TEXT("VerticalBox");
	inline constexpr const TCHAR* HorizontalBox = TEXT("HorizontalBox");
	inline constexpr const TCHAR* Overlay = TEXT("Overlay");
	inline constexpr const TCHAR* WrapBox = TEXT("WrapBox");
	inline constexpr const TCHAR* GridPanel = TEXT("GridPanel");
	inline constexpr const TCHAR* UniformGridPanel = TEXT("UniformGridPanel");
	inline constexpr const TCHAR* ScrollBox = TEXT("ScrollBox");
	inline constexpr const TCHAR* WidgetSwitcher = TEXT("WidgetSwitcher");
	inline constexpr const TCHAR* SafeZone = TEXT("SafeZone");
	inline constexpr const TCHAR* Border = TEXT("Border");
	inline constexpr const TCHAR* SizeBox = TEXT("SizeBox");
	inline constexpr const TCHAR* ScaleBox = TEXT("ScaleBox");
	inline constexpr const TCHAR* BackgroundBlur = TEXT("BackgroundBlur");
	inline constexpr const TCHAR* InvalidationBox = TEXT("InvalidationBox");
	inline constexpr const TCHAR* RetainerBox = TEXT("RetainerBox");
	inline constexpr const TCHAR* ExpandableArea = TEXT("ExpandableArea");
	inline constexpr const TCHAR* NamedSlot = TEXT("NamedSlot");
	inline constexpr const TCHAR* Button = TEXT("Button");
	inline constexpr const TCHAR* CheckBox = TEXT("CheckBox");
	inline constexpr const TCHAR* Slider = TEXT("Slider");
	inline constexpr const TCHAR* SpinBox = TEXT("SpinBox");
	inline constexpr const TCHAR* TextBlock = TEXT("TextBlock");
	inline constexpr const TCHAR* RichTextBlock = TEXT("RichTextBlock");
	inline constexpr const TCHAR* EditableTextBox = TEXT("EditableTextBox");
	inline constexpr const TCHAR* MultiLineEditableTextBox = TEXT("MultiLineEditableTextBox");
	inline constexpr const TCHAR* Image = TEXT("Image");
	inline constexpr const TCHAR* ProgressBar = TEXT("ProgressBar");
	inline constexpr const TCHAR* Throbber = TEXT("Throbber");
	inline constexpr const TCHAR* CircularThrobber = TEXT("CircularThrobber");
	inline constexpr const TCHAR* Spacer = TEXT("Spacer");
	inline constexpr const TCHAR* ListView = TEXT("ListView");
	inline constexpr const TCHAR* TileView = TEXT("TileView");
	inline constexpr const TCHAR* TreeView = TEXT("TreeView");
	inline constexpr const TCHAR* MenuAnchor = TEXT("MenuAnchor");
	inline constexpr const TCHAR* EditableText = TEXT("EditableText");
	inline constexpr const TCHAR* MultiLineEditableText = TEXT("MultiLineEditableText");
	inline constexpr const TCHAR* ComboBox = TEXT("ComboBox");
	inline constexpr const TCHAR* ComboBoxString = TEXT("ComboBoxString");

	/**
	 * Panels with box-style flow slots whose empty wrapped instances can be
	 * elided during conversion (see VisualMapper's CanElideEmptyWrappedPanel).
	 * Deliberately excludes Overlay/ScrollBox/WidgetSwitcher/SafeZone: those
	 * carry different slot semantics and must survive even when empty.
	 */
	inline bool IsLayoutPanel(const FString& Type)
	{
		return Type == CanvasPanel
			|| Type == VerticalBox
			|| Type == HorizontalBox
			|| Type == WrapBox
			|| Type == UniformGridPanel
			|| Type == GridPanel;
	}

	/**
	 * Pure layout containers: panels that render no visual content of their own.
	 * Used by the JSON parse bridge to reject an "empty UI result" whose root is
	 * a bare container. Wider than IsLayoutPanel because an empty root of any
	 * pure container shape is still an empty result.
	 */
	inline bool IsPureLayoutContainer(const FString& Type)
	{
		return IsLayoutPanel(Type)
			|| Type == Overlay
			|| Type == ScrollBox
			|| Type == WidgetSwitcher
			|| Type == SafeZone;
	}

	/** Type prefix applied by LayoutMapper size-constraint fusion; resolved to the UDesign2UMGConstrained* classes in the editor class map. */
	inline constexpr const TCHAR* ConstrainedTypePrefix = TEXT("Design2UMGConstrained");

	inline FString MakeConstrainedTypeName(const FString& BaseWidgetType)
	{
		return FString(ConstrainedTypePrefix) + BaseWidgetType;
	}

	inline bool IsConstrainedTypeName(const FString& Type)
	{
		return Type.StartsWith(ConstrainedTypePrefix);
	}

	/** Base type without the fusion prefix; returns the input unchanged when not fused. */
	inline FString StripConstrainedTypeName(const FString& Type)
	{
		if (IsConstrainedTypeName(Type))
		{
			return Type.RightChop(FString(ConstrainedTypePrefix).Len());
		}
		return Type;
	}
}
