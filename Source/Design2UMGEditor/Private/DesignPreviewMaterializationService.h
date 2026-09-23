// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignPreviewContracts.h"
#include "UObject/StrongObjectPtr.h"

#include "Blueprint/UserWidget.h"

class UTexture2D;

class FDesignPreviewMaterializationService final : public IDesignPreviewMaterializationService
{
public:
	virtual ~FDesignPreviewMaterializationService() override;

	virtual bool MaterializePreview(const FDesignPreviewMaterializationRequest& Request, FDesignPreviewMaterializationResult& OutResult) override;
	virtual UUserWidget* GetMaterializedPreviewWidget() const override;
	virtual void Reset() override;

private:
	void CleanupMaterializedPreview();
	/**
	 * Release roots held for this attempt's procedural textures, then clear the list.
	 * Other active previews retain their own textures.
	 */
	void ReleaseAttemptProceduralTextures();
	// Return a strong reference that owns the widget until preview handover.
	TStrongObjectPtr<UUserWidget> CreatePreviewWidget(
		const FPreviewApprovedSnapshot& ApprovedSnapshot,
		const FVector2D& PreviewDesignSize, FString& OutError);

	TStrongObjectPtr<UUserWidget> MaterializedPreviewWidget;
	TMap<FString, UTexture2D*> TransientTextureMap;
	// Procedural textures rooted by this generator during the most recent
	// CreatePreviewWidget attempt. Used to release exactly this attempt's textures
	// on failure (or hand them to the widget on success) without touching other
	// active previews' transient roots.
	TArray<UTexture2D*> ThisAttemptProceduralTextures;
};
