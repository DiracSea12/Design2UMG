// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGGeneratorRootHandoff.h"

#include "Design2UMGLog.h"
#include "PreBuildResourceNormalizer.h"
#include "UMGWidgetUtils.h"
#include "Blueprint/WidgetTree.h"

namespace UMGGeneratorRootHandoff
{
	FPreparedRoot PrepareRootForBuild(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FString& AssetName)
	{
		FPreparedRoot Prepared;
		Prepared.Root = WidgetData.RootWidget;

		const int32 RootWidth = FMath::CeilToInt(Prepared.Root.Bounds.W);
		const int32 RootHeight = FMath::CeilToInt(Prepared.Root.Bounds.H);
		const bool bHasValidRootSize = RootWidth >= 2 && RootHeight >= 2;
		if (!bHasValidRootSize)
		{
			UE_LOG(LogDesign2UMG, Error,
				TEXT("Root design size is invalid (root bounds %.2fx%.2f); source data must define the layout size"),
				Prepared.Root.Bounds.W, Prepared.Root.Bounds.H);
			return Prepared;
		}

		Prepared.DesignSize = FIntPoint(RootWidth, RootHeight);
		UE_LOG(LogDesign2UMG, Log,
			TEXT("Using source root design size %dx%d (root bounds %.2fx%.2f)"),
			Prepared.DesignSize.X, Prepared.DesignSize.Y,
			Prepared.Root.Bounds.W, Prepared.Root.Bounds.H);

		FPreBuildResourceNormalizer::NormalizePathTextures(Prepared.Root, PackagePath / TEXT("D2U_PathTextures"), AssetName);
		FPreBuildResourceNormalizer::NormalizeProceduralTextures(Prepared.Root, PackagePath / TEXT("D2U_ProceduralTextures"), AssetName);
		return Prepared;
	}

	UWidget* CreateRootWidget(
		UWidgetBlueprint* WidgetBlueprint,
		const FPreparedRoot& PreparedRoot,
		TArray<FDesign2UMGFigmaWidgetMetadataRecord>& OutFigmaMetadataRecords)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return nullptr;
		}

		return FUMGWidgetUtils::CreateWidgetRecursive(
			WidgetBlueprint->WidgetTree,
			PreparedRoot.Root,
			nullptr,
			nullptr,
			&OutFigmaMetadataRecords,
			&PreparedRoot.DesignSize);
	}
}
