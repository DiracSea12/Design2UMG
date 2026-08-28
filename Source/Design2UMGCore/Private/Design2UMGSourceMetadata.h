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
	// "TEXT_PATH" is a Figma-only source type literal (only the Figma parser writes SourceNodeType), so this
	// predicate is false for every Pencil-sourced node.
	bool IsFigmaTextPathNode(const TSharedPtr<FDesignNode>& Node);
}
