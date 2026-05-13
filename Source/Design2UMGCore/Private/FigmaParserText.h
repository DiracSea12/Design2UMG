// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "FigmaParserInternal.h"
#include "Dom/JsonObject.h"

namespace FigmaParserInternal
{
	void ParseTextProperties(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode);
}
