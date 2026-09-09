// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignPreviewMaterializationService.h"

#include "Design2UMGGenerationSettingsSnapshot.h"
#include "UMGWidgetUtils.h"
#include "DesignRemoteImageImporter.h"
#include "Design2UMGLog.h"
#include "PreviewUserWidget.h"
#include "ProceduralShapeGenerator.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"

namespace
{
	bool TryResolvePreviewDesignSize(const FPreviewApprovedSnapshot& ApprovedSnapshot, FVector2D& OutPreviewDesignSize)
	{
		const FDesignPixelBounds& RootBounds = ApprovedSnapshot.Data.RootWidget.Bounds;
		if (RootBounds.W > 1.0f && RootBounds.H > 1.0f)
		{
			OutPreviewDesignSize = FVector2D(RootBounds.W, RootBounds.H);
			return true;
		}

		UE_LOG(LogDesign2UMG, Error,
			TEXT("Preview design size is unavailable; source data must define the layout size"));
		OutPreviewDesignSize = FVector2D::ZeroVector;
		return false;
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
	FVector2D PreviewDesignSize = FVector2D::ZeroVector;
	if (!TryResolvePreviewDesignSize(Request.ApprovedSnapshot, PreviewDesignSize))
	{
		CleanupMaterializedPreview();
		return false;
	}

	CleanupMaterializedPreview();

	UUserWidget* NewPreviewWidget = CreatePreviewWidget(Request.ApprovedSnapshot, PreviewDesignSize);
	if (!NewPreviewWidget)
	{
		return false;
	}

	MaterializedPreviewWidget.Reset(NewPreviewWidget);
	OutResult.bSucceeded = true;
	OutResult.PreviewDesignSize = PreviewDesignSize;
	OutResult.MaterializedWidgetName = Request.ApprovedSnapshot.Data.WidgetName;

	// Preview materialization completion point: the preview widget owns its procedural
	// textures through its brush UPROPERTYs (FSlateBrush::ResourceObject), so the
	// per-import root ledger can be dropped here - textures from previous refreshes
	// lost their last strong reference with the destroyed preview widget and become
	// GC-eligible. Failure paths above return early and keep the roots (old behavior).
	FProceduralShapeGenerator::ReleaseTransientTextures();
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

UUserWidget* FDesignPreviewMaterializationService::CreatePreviewWidget(
	const FPreviewApprovedSnapshot& ApprovedSnapshot,
	const FVector2D& PreviewDesignSize)
{
	TransientTextureMap.Empty();
	FDesignRemoteImageImporter::ResolveTransientTextures(ApprovedSnapshot.Data.RootWidget, TransientTextureMap);

	FUMGWidgetNode FixedRoot = ApprovedSnapshot.Data.RootWidget;
	const FIntPoint DesignSize(
		FMath::Max(1, FMath::CeilToInt(PreviewDesignSize.X)),
		FMath::Max(1, FMath::CeilToInt(PreviewDesignSize.Y)));

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

	const FDesign2UMGGenerationSettingsSnapshot GenerationSettings =
		ApprovedSnapshot.MaterializationSettingsSnapshot.IsSet()
			? FDesign2UMGGenerationSettingsSnapshot::FromMaterializationSettings(
				ApprovedSnapshot.MaterializationSettingsSnapshot.GetValue())
			: FDesign2UMGGenerationSettingsSnapshot();

	UWidget* RootWidget = FUMGWidgetUtils::CreateWidgetRecursive(
		TempWidget->WidgetTree,
		FixedRoot,
		nullptr,
		&TransientTextureMap,
		nullptr,
		&DesignSize,
		nullptr,
		nullptr,
		&GenerationSettings);
	if (!RootWidget)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create root widget of type: %s"), *ApprovedSnapshot.Data.RootWidget.Type);
		return nullptr;
	}

	TempWidget->WidgetTree->RootWidget = RootWidget;
	return TempWidget;
}
