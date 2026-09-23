// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Design2UMGComponentInputTestWidget.generated.h"

/** Component-input fixture with text, bool, name and both floating-point property types. */
UCLASS(MinimalAPI)
class UDesign2UMGComponentInputTestWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Text input target property. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FText TextValue;

	/** Bool input target property (defaults to true, so "invalid value does not overwrite" can be asserted). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	bool BoolValue = true;

	/** Name input target property (variant kind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FName NameValue;

	/** Float input target property (double, the exact declared type). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	double DoubleValue = 0.0;

	/** Float input target property (single precision, verifying the FFloatProperty write branch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	float FloatValue = 0.0f;
};
