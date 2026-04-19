// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "UMGSchema.h"

class UWidget;

namespace UMGTextPropertyApplier
{
	void ApplyTextBlockProps(UWidget* Widget, const FUMGPropertyBag& Properties);
	void ApplyRichTextBlockProps(UWidget* Widget, const FUMGPropertyBag& Properties);
}
