// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UWidget;
class UWidgetBlueprint;
class FDesign2UMGComponentMappingBuildState;
struct FDesign2UMGGenerationSettingsSnapshot;

namespace UMGGeneratorRootHandoff
{
	struct FPreparedRoot
	{
		FUMGWidgetNode Root;
		FIntPoint DesignSize = FIntPoint::ZeroValue;
	};

	FPreparedRoot PrepareRootForBuild(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FString& AssetName);

	UWidget* CreateRootWidget(
		UWidgetBlueprint* WidgetBlueprint,
		const FPreparedRoot& PreparedRoot,
		FDesign2UMGComponentMappingBuildState* ComponentMappingState = nullptr,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr);
}
