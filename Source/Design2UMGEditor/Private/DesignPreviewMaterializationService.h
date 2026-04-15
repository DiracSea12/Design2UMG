// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

struct FUMGWidgetData;
class UTexture2D;
class UUserWidget;

struct FDesignPreviewMaterializationResult
{
	FVector2D PreviewDesignSize = FVector2D::ZeroVector;
};

class IDesignPreviewMaterializationService
{
public:
	virtual ~IDesignPreviewMaterializationService() = default;

	virtual bool MaterializePreview(const FUMGWidgetData& WidgetData, FDesignPreviewMaterializationResult& OutResult) = 0;
	virtual UUserWidget* GetMaterializedPreviewWidget() const = 0;
	virtual void Reset() = 0;
};

class FDesignPreviewMaterializationService final : public IDesignPreviewMaterializationService
{
public:
	virtual ~FDesignPreviewMaterializationService() override;

	virtual bool MaterializePreview(const FUMGWidgetData& WidgetData, FDesignPreviewMaterializationResult& OutResult) override;
	virtual UUserWidget* GetMaterializedPreviewWidget() const override;
	virtual void Reset() override;

private:
	void CleanupMaterializedPreview();
	UUserWidget* CreatePreviewWidget(const FUMGWidgetData& WidgetData);

	TStrongObjectPtr<UUserWidget> MaterializedPreviewWidget;
	TMap<FString, UTexture2D*> TransientTextureMap;
};
