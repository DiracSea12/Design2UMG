// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Shared spellings for FUMGWidgetNode::Type in conversion, JSON and widget lookup.
 * Keep the string representation for serialization; use these constants at call sites.
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
}
