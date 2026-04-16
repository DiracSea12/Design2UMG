// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportPipeline.h"
#include "UObject/StrongObjectPtr.h"

class UTexture2D;
class UUserWidget;

struct FDesignPreviewMaterializationRequest
{
	uint64 RequestId = 0;
	FPreviewApprovedSnapshot ApprovedSnapshot;
};

struct FDesignPreviewMaterializationResult
{
	bool bSucceeded = false;
	uint64 RequestId = 0;
	FVector2D PreviewDesignSize = FVector2D::ZeroVector;
	FString MaterializedWidgetName;
};

class IDesignPreviewMaterializationService
{
public:
	virtual ~IDesignPreviewMaterializationService() = default;

	virtual bool MaterializePreview(const FDesignPreviewMaterializationRequest& Request, FDesignPreviewMaterializationResult& OutResult) = 0;
	virtual UUserWidget* GetMaterializedPreviewWidget() const = 0;
	virtual void Reset() = 0;
};

class FDesignPreviewMaterializationService final : public IDesignPreviewMaterializationService
{
public:
	virtual ~FDesignPreviewMaterializationService() override;

	virtual bool MaterializePreview(const FDesignPreviewMaterializationRequest& Request, FDesignPreviewMaterializationResult& OutResult) override;
	virtual UUserWidget* GetMaterializedPreviewWidget() const override;
	virtual void Reset() override;

private:
	void CleanupMaterializedPreview();
	UUserWidget* CreatePreviewWidget(const FPreviewApprovedSnapshot& ApprovedSnapshot);

	TStrongObjectPtr<UUserWidget> MaterializedPreviewWidget;
	TMap<FString, UTexture2D*> TransientTextureMap;
};
