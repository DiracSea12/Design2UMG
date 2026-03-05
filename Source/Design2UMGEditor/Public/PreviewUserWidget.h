// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PreviewUserWidget.generated.h"

/**
 * 闈炴娊璞＄殑 UserWidget 瀛愮被锛屼笓闂ㄧ敤浜庣紪杈戝櫒棰勮
 * UUserWidget 鍦?UE5 涓鏍囪涓?Abstract锛屼笉鑳界洿鎺ュ疄渚嬪寲
 * 姝ょ被鎻愪緵涓€涓彲瀹炰緥鍖栫殑鏇夸唬鏂规鐢ㄤ簬涓存椂棰勮
 */
UCLASS(MinimalAPI)
class UPreviewUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPreviewUserWidget(const FObjectInitializer& ObjectInitializer);
};

