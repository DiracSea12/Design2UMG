// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Design2UMGFigmaMetadataExtensions.h"
#include "UMGSchema.h"

class UTexture2D;
class UWidget;
class UWidgetTree;

struct FWidgetTreeChildBuildContext
{
	const TMap<FString, UTexture2D*>* TextureMap = nullptr;
	TArray<FDesign2UMGFigmaWidgetMetadataRecord>* FigmaMetadataRecords = nullptr;
	const FIntPoint* ExplicitDesignSize = nullptr;
};

namespace FWidgetTreeChildBuilder
{
	void BuildChildren(
		UWidgetTree* WidgetTree,
		UWidget* ParentWidget,
		const FUMGWidgetNode& Node,
		const FWidgetTreeChildBuildContext& Context);
}
