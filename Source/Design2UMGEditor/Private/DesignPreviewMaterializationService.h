// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignPreviewContracts.h"
#include "UObject/StrongObjectPtr.h"

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
	UUserWidget* CreatePreviewWidget(const FPreviewApprovedSnapshot& ApprovedSnapshot);

	TStrongObjectPtr<UUserWidget> MaterializedPreviewWidget;
	TMap<FString, UTexture2D*> TransientTextureMap;
};
