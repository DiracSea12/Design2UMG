// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchemaSourceExtensions.h"

struct FUMGWidgetNode;

namespace Design2UMGSourceMetadata
{
	// Raw metadata / source-extension access.
	bool HasNodeMetadataFlag(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key);
	FString GetSourceExtension(const TSharedPtr<FDesignNode>& Node, const FString& Key);
	bool HasMeaningfulSourceMetadata(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key);

	// Source-node responsibility mapping.
	FUMGSourceNodeData BuildSourceNodeData(const TSharedPtr<FDesignNode>& Node);
	bool IsSourceNodeType(const TSharedPtr<FDesignNode>& Node, const TCHAR* ExpectedType);

	// Render-fallback responsibility.
	bool ShouldUseRenderFallback(const TSharedPtr<FDesignNode>& Node);
	FUMGWidgetNode CreateRenderFallbackWidget(const TSharedPtr<FDesignNode>& Node);

	// Layout metadata helpers.
	float ResolveWrapCrossAxisGap(const TSharedPtr<FDesignNode>& Node);
}
