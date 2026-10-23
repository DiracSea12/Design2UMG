// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignPreviewMaterializationService.h"

#include "Design2UMGGenerationSettingsSnapshot.h"
#include "UMGWidgetTreeBuilder.h"
#include "DesignRemoteImageImporter.h"
#include "DesignImageResourceLimits.h"
#include "PreBuildResourceNormalizer.h"
#include "Design2UMGLog.h"
#include "PreviewUserWidget.h"
#include "ProceduralShapeGenerator.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	bool TryResolvePreviewDesignSize(const FPreviewApprovedSnapshot& ApprovedSnapshot, FVector2D& OutPreviewDesignSize, FString& OutError)
	{
		const FDesignPixelBounds& RootBounds = ApprovedSnapshot.Data.RootWidget.Bounds;
		if (RootBounds.W > 0.0f && RootBounds.H > 0.0f)
		{
			OutPreviewDesignSize = FVector2D(RootBounds.W, RootBounds.H);
			return true;
		}

		UE_LOG(LogDesign2UMG, Error,
			TEXT("Preview design size is unavailable; source data must define the layout size"));
		OutError = FString::Printf(TEXT("根节点预览尺寸无效（%.3f × %.3f），宽高须大于 0。"), RootBounds.W, RootBounds.H);
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
	if (!TryResolvePreviewDesignSize(Request.ApprovedSnapshot, PreviewDesignSize, OutResult.ErrorMessage))
	{
		CleanupMaterializedPreview();
		return false;
	}

	CleanupMaterializedPreview();

	TStrongObjectPtr<UUserWidget> NewPreviewWidget = CreatePreviewWidget(Request.ApprovedSnapshot, PreviewDesignSize, OutResult.ErrorMessage);
	if (!NewPreviewWidget.IsValid())
	{
		// Release this failed attempt's procedural and remote transient textures immediately.
		ReleaseAttemptProceduralTextures();
		CleanupMaterializedPreview();
		return false;
	}

	// Transfer widget ownership before the builder's strong reference expires.
	MaterializedPreviewWidget = NewPreviewWidget;
	OutResult.bSucceeded = true;
	OutResult.PreviewDesignSize = PreviewDesignSize;
	OutResult.MaterializedWidgetName = Request.ApprovedSnapshot.Data.WidgetName;

	// Preview materialization completion point: the preview widget owns its procedural
	// textures through its brush UPROPERTYs (FSlateBrush::ResourceObject), so we drop
	// only this attempt's transient roots here. Textures from previous refreshes lost
	// their last strong reference with the destroyed preview widget and become
	// GC-eligible. We release by identity (not the global flush) so other active
	// previews' transient textures are never touched.
	ReleaseAttemptProceduralTextures();
	return true;
}

void FDesignPreviewMaterializationService::ReleaseAttemptProceduralTextures()
{
	for (UTexture2D* Texture : ThisAttemptProceduralTextures)
	{
		FProceduralShapeGenerator::ReleaseTransientTexture(Texture);
	}
	ThisAttemptProceduralTextures.Reset();
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
	if (ThisAttemptProceduralTextures.Num() > 0)
	{
		// Defensive: release any procedural textures still scoped to an attempt. On
		// the normal success/failure paths these are already released by identity,
		// so this only fires if cleanup is reached without a prior release.
		ReleaseAttemptProceduralTextures();
	}

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

TStrongObjectPtr<UUserWidget> FDesignPreviewMaterializationService::CreatePreviewWidget(
	const FPreviewApprovedSnapshot& ApprovedSnapshot,
	const FVector2D& PreviewDesignSize, FString& OutError)
{
	TransientTextureMap.Empty();
	FDesignRemoteImageImporter::ResolveTransientTextures(ApprovedSnapshot.Data.RootWidget, TransientTextureMap);

	FUMGWidgetNode FixedRoot = ApprovedSnapshot.Data.RootWidget;
	const FIntPoint DesignSize(
		FMath::Max(1, FMath::CeilToInt(PreviewDesignSize.X)),
		FMath::Max(1, FMath::CeilToInt(PreviewDesignSize.Y)));

	// Keep the widget strongly referenced throughout construction and ownership transfer.
	TStrongObjectPtr<UUserWidget> TempWidget(
		NewObject<UPreviewUserWidget>(GetTransientPackage(), UPreviewUserWidget::StaticClass(), NAME_None, RF_Transient));
	if (!TempWidget.IsValid())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create UUserWidget"));
		OutError = TEXT("创建预览界面（UUserWidget）失败。");
		return nullptr;
	}

	TempWidget->WidgetTree = NewObject<UWidgetTree>(TempWidget.Get(), UWidgetTree::StaticClass(), NAME_None, RF_Transient);
	if (!TempWidget->WidgetTree)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create WidgetTree"));
		OutError = TEXT("创建预览控件树（WidgetTree）失败。");
		return nullptr;
	}

	const FDesign2UMGGenerationSettingsSnapshot GenerationSettings =
		ApprovedSnapshot.MaterializationSettingsSnapshot.IsSet()
			? FDesign2UMGGenerationSettingsSnapshot::FromMaterializationSettings(
				ApprovedSnapshot.MaterializationSettingsSnapshot.GetValue())
			: FDesign2UMGGenerationSettingsSnapshot();

	// Track rooted textures by identity to isolate this preview's resource lifetime.
	const TArray<UTexture2D*> RootedBefore = FProceduralShapeGenerator::GetRootedTextureSnapshot();

	// Share one generated-texture budget across the preview.
	// Generators charge actual allocations; cached path rasters are charged only on a miss.
	FString PreviewBudgetError;
	if (!FPreBuildResourceNormalizer::ValidateGeneratedTextureRequests(
			FixedRoot, PreviewBudgetError))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Preview materialization rejected: %s"), *PreviewBudgetError);
		OutError = PreviewBudgetError;
		return nullptr;
	}

	// One explicit budget covers every allocation in this preview tree.
	DesignImageResources::FImageOperationBudget PreviewBudget(DesignImageResources::GetGeneratedTextureLimits());

	UWidget* RootWidget = FUMGWidgetTreeBuilder::CreateWidgetRecursive(
		TempWidget->WidgetTree,
		FixedRoot,
		nullptr,
		&TransientTextureMap,
		&DesignSize,
		nullptr,
		nullptr,
		&GenerationSettings, &PreviewBudget);

	// Collect this attempt's rooted textures before any failure return, including partial builds.
	const TArray<UTexture2D*> RootedAfter = FProceduralShapeGenerator::GetRootedTextureSnapshot();
	ThisAttemptProceduralTextures.Reset();
	for (UTexture2D* Texture : RootedAfter)
	{
		if (Texture && RootedBefore.Find(Texture) == INDEX_NONE)
		{
			ThisAttemptProceduralTextures.Add(Texture);
		}
	}

	// Reject incomplete widget trees when a raster allocation exceeds the budget.
	// MaterializePreview then releases this attempt's resources.
	const FString& RasterRefusal = PreviewBudget.GetRefusalError();
	if (!RasterRefusal.IsEmpty())
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Preview materialization rejected: %s"), *RasterRefusal);
		OutError = RasterRefusal;
		return nullptr;
	}

	if (!RootWidget)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create root widget of type: %s"), *ApprovedSnapshot.Data.RootWidget.Type);
		OutError = FString::Printf(TEXT("创建预览根控件失败，控件类型：%s。"), *ApprovedSnapshot.Data.RootWidget.Type);
		return nullptr;
	}

	TempWidget->WidgetTree->RootWidget = RootWidget;
	return TempWidget;
}
