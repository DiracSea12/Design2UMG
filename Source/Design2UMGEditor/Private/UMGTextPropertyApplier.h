// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "UMGSchema.h"

class UWidget;

namespace UMGTextPropertyApplier
{
	void ApplyTextBlockProps(UWidget* Widget, const FUMGPropertyBag& Properties);
	void ApplyTextBlockProps(UWidget* Widget, const FUMGWidgetNode& Node, const FUMGPropertyBag& ResidualProperties);
	void ApplyRichTextBlockProps(UWidget* Widget, const FUMGPropertyBag& Properties);
	void ApplyRichTextBlockProps(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyEditableTextBoxProps(UWidget* Widget, const FUMGPropertyBag& Properties);
	void ApplyEditableTextBoxProps(UWidget* Widget, const FUMGWidgetNode& Node, const FUMGPropertyBag& ResidualProperties);
	void ApplyMultiLineEditableTextBoxProps(UWidget* Widget, const FUMGPropertyBag& Properties);
	void ApplyMultiLineEditableTextBoxProps(UWidget* Widget, const FUMGWidgetNode& Node, const FUMGPropertyBag& ResidualProperties);
	void ApplyEditableTextProps(UWidget* Widget, const FUMGPropertyBag& Properties);
	void ApplyEditableTextProps(UWidget* Widget, const FUMGWidgetNode& Node, const FUMGPropertyBag& ResidualProperties);
	void ApplyMultiLineEditableTextProps(UWidget* Widget, const FUMGPropertyBag& Properties);
	void ApplyMultiLineEditableTextProps(UWidget* Widget, const FUMGWidgetNode& Node, const FUMGPropertyBag& ResidualProperties);
}

