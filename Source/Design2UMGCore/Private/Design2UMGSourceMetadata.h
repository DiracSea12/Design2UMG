// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchemaSourceExtensions.h"

struct FUMGWidgetNode;

namespace Design2UMGSourceMetadata
{
	bool HasNodeMetadataFlag(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key);

	FString GetSourceExtension(const TSharedPtr<FDesignNode>& Node, const FString& Key);

	bool HasMeaningfulSourceMetadata(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key);

	FUMGSourceNodeData BuildSourceNodeData(const TSharedPtr<FDesignNode>& Node);

	bool IsSourceNodeType(const TSharedPtr<FDesignNode>& Node, const TCHAR* ExpectedType);

	bool ShouldUseRenderFallback(const TSharedPtr<FDesignNode>& Node);

	FUMGWidgetNode CreateRenderFallbackWidget(const TSharedPtr<FDesignNode>& Node);

	float ResolveWrapCrossAxisGap(const TSharedPtr<FDesignNode>& Node);
}
