// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "UMGSchema.h"

class UWidget;

namespace UMGTextPropertyApplier
{
	void ApplyTextBlockProps(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyRichTextBlockProps(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyEditableTextBoxProps(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyMultiLineEditableTextBoxProps(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyEditableTextProps(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyMultiLineEditableTextProps(UWidget* Widget, const FUMGWidgetNode& Node);
}
