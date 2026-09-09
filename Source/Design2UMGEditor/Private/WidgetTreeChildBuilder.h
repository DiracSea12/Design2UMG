// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Design2UMGFigmaMetadataExtensions.h"
#include "UMGSchema.h"

class UTexture2D;
class UWidget;
class UWidgetTree;
class UClass;
class FDesign2UMGComponentMappingBuildState;
struct FDesign2UMGGenerationSettingsSnapshot;

struct FWidgetTreeChildBuildContext
{
	const TMap<FString, UTexture2D*>* TextureMap = nullptr;
	TArray<FDesign2UMGFigmaWidgetMetadataRecord>* FigmaMetadataRecords = nullptr;
	const FIntPoint* ExplicitDesignSize = nullptr;
	FDesign2UMGComponentMappingBuildState* ComponentMappingState = nullptr;
	/** Per-tree class-override resolution cache; owned by the root BuildWidgetTree call. */
	TMap<FString, UClass*>* ClassOverrideCache = nullptr;
	/** Generation settings for name-based class overrides; null skips the override block like the caller had none. */
	const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr;
};

namespace FWidgetTreeChildBuilder
{
	void BuildChildren(
		UWidgetTree* WidgetTree,
		UWidget* ParentWidget,
		const FUMGWidgetNode& Node,
		const FWidgetTreeChildBuildContext& Context);
}
