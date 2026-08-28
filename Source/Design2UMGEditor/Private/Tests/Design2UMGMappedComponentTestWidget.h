// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Design2UMGMappedComponentTestWidget.generated.h"

/**
 * Project WBP stand-in class for Component Mapping automation tests.
 * In a real project the mapping points at a WBP generated class (WBP_X_C); tests use a C++ UUserWidget subclass
 * exposing the four design-facing input target properties (Text/Bool/Name/Float) through the same reflection path.
 */
UCLASS(MinimalAPI)
class UDesign2UMGMappedComponentTestWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Text input target: the component's main text / a componentProperties text value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FText Label;

	/** Bool input target (a property of the WBP class itself, not a UWidget inheritance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	bool EnabledFlag = true;

	/** Name input target (variant kind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FName VariantName;

	/** Float input target (value kind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	double Value = 0.0;
};
