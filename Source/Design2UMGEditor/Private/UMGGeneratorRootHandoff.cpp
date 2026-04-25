// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGGeneratorRootHandoff.h"

#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "PreBuildResourceNormalizer.h"
#include "UMGWidgetUtils.h"
#include "Blueprint/WidgetTree.h"

namespace
{
	FUMGSlotData MakeCanvasFillSlot()
	{
		FUMGSlotData Slot;
		Slot.Anchors.Min.X = 0.0f;
		Slot.Anchors.Min.Y = 0.0f;
		Slot.Anchors.Max.X = 1.0f;
		Slot.Anchors.Max.Y = 1.0f;
		Slot.Alignment.X = 0.0f;
		Slot.Alignment.Y = 0.0f;
		Slot.Offsets.Left = 0.0f;
		Slot.Offsets.Top = 0.0f;
		Slot.Offsets.Right = 0.0f;
		Slot.Offsets.Bottom = 0.0f;
		Slot.bAutoSize = false;
		return Slot;
	}

	FUMGWidgetNode WrapRootForViewport(
		FUMGWidgetNode&& InRoot,
		const FIntPoint& ViewportSize,
		const FIntPoint& ContentSize,
		bool bDownOnly)
	{
		FUMGWidgetNode SizeBoxNode;
		SizeBoxNode.Type = TEXT("SizeBox");
		SizeBoxNode.Id = TEXT("__d2u_root_sizebox");
		SizeBoxNode.Name = TEXT("D2U_RootSizeBox");
		SizeBoxNode.Bounds.X = 0.0f;
		SizeBoxNode.Bounds.Y = 0.0f;
		SizeBoxNode.Bounds.W = static_cast<float>(ContentSize.X);
		SizeBoxNode.Bounds.H = static_cast<float>(ContentSize.Y);
		SizeBoxNode.WidgetStyle.WidthOverride = ContentSize.X;
		SizeBoxNode.WidgetStyle.HeightOverride = ContentSize.Y;
		SizeBoxNode.Children.Add(MoveTemp(InRoot));

		FUMGWidgetNode ScaleBoxNode;
		ScaleBoxNode.Type = TEXT("ScaleBox");
		ScaleBoxNode.Id = TEXT("__d2u_root_scalebox");
		ScaleBoxNode.Name = TEXT("D2U_RootScaleBox");
		ScaleBoxNode.Bounds.X = 0.0f;
		ScaleBoxNode.Bounds.Y = 0.0f;
		ScaleBoxNode.Bounds.W = static_cast<float>(ViewportSize.X);
		ScaleBoxNode.Bounds.H = static_cast<float>(ViewportSize.Y);
		ScaleBoxNode.Slot = MakeCanvasFillSlot();
		ScaleBoxNode.WidgetStyle.Stretch = TEXT("ScaleToFit");
		ScaleBoxNode.WidgetStyle.StretchDirection = bDownOnly ? TEXT("DownOnly") : TEXT("Both");
		ScaleBoxNode.Children.Add(MoveTemp(SizeBoxNode));

		FUMGWidgetNode CanvasRoot;
		CanvasRoot.Type = TEXT("CanvasPanel");
		CanvasRoot.Id = TEXT("__d2u_viewport_root");
		CanvasRoot.Name = TEXT("D2U_ViewportRoot");
		CanvasRoot.Bounds.X = 0.0f;
		CanvasRoot.Bounds.Y = 0.0f;
		CanvasRoot.Bounds.W = static_cast<float>(ViewportSize.X);
		CanvasRoot.Bounds.H = static_cast<float>(ViewportSize.Y);
		CanvasRoot.Children.Add(MoveTemp(ScaleBoxNode));
		return CanvasRoot;
	}
}

namespace UMGGeneratorRootHandoff
{
	FPreparedRoot PrepareRootForBuild(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FString& AssetName)
	{
		FPreparedRoot Prepared;
		Prepared.Root = WidgetData.RootWidget;

		const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
		const FIntPoint ConfigDesignSize = Settings->GetDesignSize();
		const int32 RootWidth = FMath::CeilToInt(Prepared.Root.Bounds.W);
		const int32 RootHeight = FMath::CeilToInt(Prepared.Root.Bounds.H);
		const bool bHasValidRootSize = RootWidth >= 2 && RootHeight >= 2;
		Prepared.DesignSize = bHasValidRootSize
			? FIntPoint(RootWidth, RootHeight)
			: ConfigDesignSize;

		if (bHasValidRootSize && (Prepared.DesignSize != ConfigDesignSize))
		{
			UE_LOG(LogDesign2UMG, Log,
				TEXT("Using root design size %dx%d (config %dx%d, root bounds %.0fx%.0f)"),
				Prepared.DesignSize.X, Prepared.DesignSize.Y,
				ConfigDesignSize.X, ConfigDesignSize.Y,
				Prepared.Root.Bounds.W, Prepared.Root.Bounds.H);
		}
		else if (!bHasValidRootSize)
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Root bounds invalid (%.2fx%.2f), fallback to config design size %dx%d"),
				Prepared.Root.Bounds.W, Prepared.Root.Bounds.H,
				ConfigDesignSize.X, ConfigDesignSize.Y);
		}

		if (Settings->bEnableLayoutFixer)
		{
			FUMGWidgetUtils::ApplyFixer(Prepared.Root, Prepared.DesignSize);
		}

		const bool bOversizedRoot =
			(Prepared.Root.Bounds.W > static_cast<float>(ConfigDesignSize.X)) ||
			(Prepared.Root.Bounds.H > static_cast<float>(ConfigDesignSize.Y));
		if (Settings->bAutoWrapOversizedRootForViewport && bOversizedRoot)
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("Wrapping oversized root for viewport fit: root=%.0fx%.0f config=%dx%d stretch=%s"),
				Prepared.Root.Bounds.W, Prepared.Root.Bounds.H,
				ConfigDesignSize.X, ConfigDesignSize.Y,
				Settings->bViewportScaleDownOnly ? TEXT("DownOnly") : TEXT("Both"));
			Prepared.Root = WrapRootForViewport(
				MoveTemp(Prepared.Root),
				ConfigDesignSize,
				Prepared.DesignSize,
				Settings->bViewportScaleDownOnly);
		}

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
