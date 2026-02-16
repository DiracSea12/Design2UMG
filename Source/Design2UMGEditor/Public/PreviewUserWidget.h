// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PreviewUserWidget.generated.h"

/**
 * 非抽象的 UserWidget 子类，专门用于编辑器预览
 * UUserWidget 在 UE5 中被标记为 Abstract，不能直接实例化
 * 此类提供一个可实例化的替代方案用于临时预览
 */
UCLASS(MinimalAPI)
class UPreviewUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPreviewUserWidget(const FObjectInitializer& ObjectInitializer);
};
