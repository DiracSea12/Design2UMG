// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignPreviewMaterializationService.h"

#include "UMGWidgetUtils.h"
#include "DesignRemoteImageImporter.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "PreviewUserWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"

namespace
{
	FVector2D ResolvePreviewDesignSize(const FPreviewApprovedSnapshot& ApprovedSnapshot)
	{
		const FDesignPixelBounds& RootBounds = ApprovedSnapshot.Data.RootWidget.Bounds;
		if (RootBounds.W > 0.0f && RootBounds.H > 0.0f)
		{
			return FVector2D(RootBounds.W, RootBounds.H);
		}

		const FVector2D CalculatedSize = FUMGWidgetUtils::CalculateDesignSize(ApprovedSnapshot.Data.RootWidget, FVector2D::ZeroVector);
		if (CalculatedSize.X > 0.0f && CalculatedSize.Y > 0.0f)
		{
			return CalculatedSize;
		}

		const FIntPoint FallbackSize = UDesign2UMGSettings::Get()->GetDesignSize();
		return FVector2D((float)FallbackSize.X, (float)FallbackSize.Y);
	}
}

FDesignPreviewMaterializationService::~FDesignPreviewMaterializationService()
{
	Reset();
}

bool FDesignPreviewMaterializationService::MaterializePreview(const FDesignPreviewMaterializationRequest& Request, FDesignPreviewMaterializationResult& OutResult)
{
	OutResult = FDesignPreviewMaterializationResult();
	OutResult.RequestId = Request.RequestId;
	const FVector2D PreviewDesignSize = ResolvePreviewDesignSize(Request.ApprovedSnapshot);

	CleanupMaterializedPreview();

	UUserWidget* NewPreviewWidget = CreatePreviewWidget(Request.ApprovedSnapshot);
	if (!NewPreviewWidget)
	{
		return false;
	}

	MaterializedPreviewWidget.Reset(NewPreviewWidget);
	OutResult.bSucceeded = true;
	OutResult.PreviewDesignSize = PreviewDesignSize;
	OutResult.MaterializedWidgetName = Request.ApprovedSnapshot.Data.WidgetName;
	return true;
}

UUserWidget* FDesignPreviewMaterializationService::GetMaterializedPreviewWidget() const
{
	return MaterializedPreviewWidget.Get();
}

void FDesignPreviewMaterializationService::Reset()
{
	CleanupMaterializedPreview();
}

void FDesignPreviewMaterializationService::CleanupMaterializedPreview()
{
	if (TransientTextureMap.Num() > 0)
	{
		FDesignRemoteImageImporter::ReleaseTransientTextures(TransientTextureMap);
	}

	if (MaterializedPreviewWidget.Get())
	{
		MaterializedPreviewWidget->RemoveFromParent();
		if (MaterializedPreviewWidget->WidgetTree)
		{
			MaterializedPreviewWidget->WidgetTree->RootWidget = nullptr;
		}
		MaterializedPreviewWidget.Reset();
	}
}

UUserWidget* FDesignPreviewMaterializationService::CreatePreviewWidget(const FPreviewApprovedSnapshot& ApprovedSnapshot)
{
	TransientTextureMap.Empty();
	FDesignRemoteImageImporter::ResolveTransientTextures(ApprovedSnapshot.Data.RootWidget, TransientTextureMap);

	FUMGWidgetNode FixedRoot = ApprovedSnapshot.Data.RootWidget;
	const FVector2D PreviewSize = ResolvePreviewDesignSize(ApprovedSnapshot);
	const FIntPoint DesignSize(FMath::Max(1, FMath::RoundToInt(PreviewSize.X)), FMath::Max(1, FMath::RoundToInt(PreviewSize.Y)));
	if (UDesign2UMGSettings::Get()->bEnableLayoutFixer)
	{
		FUMGWidgetUtils::ApplyFixer(FixedRoot, DesignSize);
	}

	UUserWidget* TempWidget = NewObject<UPreviewUserWidget>(GetTransientPackage(), UPreviewUserWidget::StaticClass(), NAME_None, RF_Transient);
	if (!TempWidget)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create UUserWidget"));
		return nullptr;
	}

	TempWidget->WidgetTree = NewObject<UWidgetTree>(TempWidget, UWidgetTree::StaticClass(), NAME_None, RF_Transient);
	if (!TempWidget->WidgetTree)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create WidgetTree"));
		return nullptr;
	}

	const FVector2D MaterializedDesignSize = FUMGWidgetUtils::CalculateDesignSize(FixedRoot);
	const FIntPoint MaterializedDesignSizeInt(
		FMath::Max(1, FMath::RoundToInt(MaterializedDesignSize.X)),
		FMath::Max(1, FMath::RoundToInt(MaterializedDesignSize.Y)));
	UWidget* RootWidget = FUMGWidgetUtils::CreateWidgetRecursive(
		TempWidget->WidgetTree,
		FixedRoot,
		nullptr,
		&TransientTextureMap,
		nullptr,
		&MaterializedDesignSizeInt);
	if (!RootWidget)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create root widget of type: %s"), *ApprovedSnapshot.Data.RootWidget.Type);
		return nullptr;
	}

	TempWidget->WidgetTree->RootWidget = RootWidget;
	return TempWidget;
}
