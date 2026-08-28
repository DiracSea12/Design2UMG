// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Design2UMGMappingQAWidget.generated.h"

/**
 * QA 白盒结构测试自带的 mapped 组件替身类（独立于交付内开发测试的替身）。
 * 暴露四类 design-facing input 目标属性，并额外提供单精度 float 目标，
 * 用于锁定 Float 输入写入 FFloatProperty 属性的分支。
 */
UCLASS(MinimalAPI)
class UDesign2UMGMappingQAWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Text 输入目标属性。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FText TextValue;

	/** Bool 输入目标属性（默认 true，便于断言“非法值不覆写”）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	bool BoolValue = true;

	/** Name 输入目标属性（variant 类）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FName NameValue;

	/** Float 输入目标属性（double，精确声明类型）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	double DoubleValue = 0.0;

	/** Float 输入目标属性（单精度，验证 FFloatProperty 写入分支）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	float FloatValue = 0.0f;
};
