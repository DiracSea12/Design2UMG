// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PreviewUserWidget.generated.h"

/**
 * Concrete UserWidget subclass used for editor preview instantiation.
 * UUserWidget is abstract in UE5, so a concrete subclass is required.
 */
UCLASS()
class DESIGN2UMGEDITOR_API UPreviewUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPreviewUserWidget(const FObjectInitializer& ObjectInitializer);
};
