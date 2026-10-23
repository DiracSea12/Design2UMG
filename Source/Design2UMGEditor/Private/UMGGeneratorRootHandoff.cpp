// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGGeneratorRootHandoff.h"

#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGLog.h"
#include "PreBuildResourceNormalizer.h"
#include "RetainerCaptureBounds.h"
#include "DesignImportPlanning.h"
#include "Design2UMGEffectMaterialBuilder.h"
#include "UMGWidgetTreeBuilder.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"

namespace UMGGeneratorRootHandoff
{
	FPreparedRoot PrepareRootForBuild(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FString& AssetName, DesignImageResources::FImageOperationBudget* Budget,
		const FDesignImageImportSettingsSnapshot* ImageSettings)
	{
		FPreparedRoot Prepared;
		Prepared.Root = WidgetData.RootWidget;
		if (!FPreBuildResourceNormalizer::ValidateGeneratedTextureRequests(
				Prepared.Root, Prepared.ErrorMessage)
			|| !RetainerCaptureBounds::Prepare(Prepared.Root, Prepared.ErrorMessage)
			|| !Design2UMGEffectMaterialBuilder::PrepareSubtreeShadowMaterials(Prepared.Root, Prepared.ErrorMessage)
			|| !Design2UMGEffectMaterialBuilder::PrepareRetainerEffectMaterials(Prepared.Root, Prepared.Materials, Prepared.ErrorMessage))
		{
			return Prepared;
		}

		const int32 RootWidth = FMath::CeilToInt(Prepared.Root.Bounds.W);
		const int32 RootHeight = FMath::CeilToInt(Prepared.Root.Bounds.H);
		const bool bHasValidRootSize = RootWidth > 0 && RootHeight > 0;
		if (!bHasValidRootSize)
		{
			UE_LOG(LogDesign2UMG, Error,
				TEXT("Root design size is invalid (root bounds %.2fx%.2f); source data must define the layout size"),
				Prepared.Root.Bounds.W, Prepared.Root.Bounds.H);
			return Prepared;
		}

		Prepared.DesignSize = FIntPoint(RootWidth, RootHeight);
		FUMGWidgetData ImageData;
		ImageData.RootWidget = MoveTemp(Prepared.Root);
		DesignImportPlanning::ResolveImages(ImageData, nullptr, ImageSettings);
		Prepared.Root = MoveTemp(ImageData.RootWidget);
		if (!FPreBuildResourceNormalizer::NormalizeGeneratedTextures(
			Prepared.Root, PackagePath, AssetName, Prepared.ErrorMessage, Budget)) return Prepared;

		UE_LOG(LogDesign2UMG, Log,
			TEXT("Using source root design size %dx%d (root bounds %.2fx%.2f)"),
			Prepared.DesignSize.X, Prepared.DesignSize.Y,
			Prepared.Root.Bounds.W, Prepared.Root.Bounds.H);

		return Prepared;
	}

	UWidget* CreateRootWidget(
		UWidgetBlueprint* WidgetBlueprint,
		const FPreparedRoot& PreparedRoot,
		FDesign2UMGComponentMappingBuildState* ComponentMappingState,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings, DesignImageResources::FImageOperationBudget* Budget)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return nullptr;
		}

		// Flag before the root widget is built: the build state uses it to capture the root pointer before children
		// are built, so the manifest can recognize the root's direct children as top-level generation relations.
		if (ComponentMappingState)
		{
			ComponentMappingState->BeginRootBuild();
		}

		UWidget* RootWidget = FUMGWidgetTreeBuilder::CreateWidgetRecursive(
			WidgetBlueprint->WidgetTree,
			PreparedRoot.Root,
			nullptr,
			nullptr,
			&PreparedRoot.DesignSize,
			ComponentMappingState,
			nullptr,
			GenerationSettings, Budget);
		return RootWidget;
	}
}
