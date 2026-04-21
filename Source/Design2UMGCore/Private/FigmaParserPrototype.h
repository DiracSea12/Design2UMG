// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "FigmaParser.h"

namespace FigmaParserInternal
{
	void ParsePrototypeNodeData(const TSharedPtr<FJsonObject>& NodeObj, FDesignPrototypeNodeData& OutData);
	void MergePrototypeDocumentData(const TSharedPtr<FJsonObject>& SourceObj, FDesignSourceDocumentData& OutData);
}
