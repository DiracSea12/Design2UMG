// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchemaSourceExtensions.h"

namespace Design2UMGSourceMetadata
{
	// Source-node responsibility mapping.
	FUMGSourceNodeData BuildSourceNodeData(const TSharedPtr<FDesignNode>& Node);
	bool IsSourceNodeType(const TSharedPtr<FDesignNode>& Node, const TCHAR* ExpectedType);
}
