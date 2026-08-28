// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Design2UMGMappedComponentTestWidget.generated.h"

/**
 * Component Mapping 自动化测试用的项目 WBP 替身类。
 * 真实项目里 mapping 指向 WBP 生成类（WBP_X_C）；测试用 C++ UUserWidget 子类
 * 以同样的反射路径暴露四类 design-facing input 目标属性（Text/Bool/Name/Float）。
 */
UCLASS(MinimalAPI)
class UDesign2UMGMappedComponentTestWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Text 输入目标：组件主体文本 / componentProperties 文本值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FText Label;

	/** Bool 输入目标（WBP 类自身属性，非 UWidget 继承属性）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	bool EnabledFlag = true;

	/** Name 输入目标（variant 类）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FName VariantName;

	/** Float 输入目标（value 类）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	double Value = 0.0;
};
