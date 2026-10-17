// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"
#include "UObject/StrongObjectPtr.h"
#include "Materials/MaterialInterface.h"

namespace DesignImageResources { class FImageOperationBudget; }

class UWidget;
class UWidgetBlueprint;
class FDesign2UMGComponentMappingBuildState;
struct FDesign2UMGGenerationSettingsSnapshot;
struct FDesignImageImportSettingsSnapshot;

namespace UMGGeneratorRootHandoff
{
	struct FPreparedRoot
	{
		FUMGWidgetNode Root;
		FIntPoint DesignSize = FIntPoint::ZeroValue;
		FString ErrorMessage;
		// Own dependencies until the constructed WidgetTree takes over; all exit paths release them.
		TArray<TStrongObjectPtr<UMaterialInterface>> Materials;
	};

	FPreparedRoot PrepareRootForBuild(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FString& AssetName, DesignImageResources::FImageOperationBudget* Budget = nullptr,
		const FDesignImageImportSettingsSnapshot* ImageSettings = nullptr);

	UWidget* CreateRootWidget(
		UWidgetBlueprint* WidgetBlueprint,
		const FPreparedRoot& PreparedRoot,
		FDesign2UMGComponentMappingBuildState* ComponentMappingState = nullptr,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr);
}
