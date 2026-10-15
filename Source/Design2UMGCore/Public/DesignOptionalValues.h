// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignOptionalValues.generated.h"

USTRUCT()
struct FDesignOptionalFloat
{
	GENERATED_BODY()

	FDesignOptionalFloat() = default;
	FDesignOptionalFloat(const float InValue) : bIsSet(true), Value(InValue) {}

	UPROPERTY()
	bool bIsSet = false;

	UPROPERTY()
	float Value = 0.0f;

	FDesignOptionalFloat& operator=(const float InValue)
	{
		bIsSet = true;
		Value = InValue;
		return *this;
	}

	bool IsSet() const { return bIsSet; }

	float& GetValue()
	{
		checkf(bIsSet, TEXT("FDesignOptionalFloat has no value."));
		return Value;
	}

	const float& GetValue() const
	{
		checkf(bIsSet, TEXT("FDesignOptionalFloat has no value."));
		return Value;
	}

	float Get(const float DefaultValue) const { return bIsSet ? Value : DefaultValue; }

	void Reset()
	{
		bIsSet = false;
		Value = 0.0f;
	}
};

USTRUCT()
struct FDesignOptionalBool
{
	GENERATED_BODY()

	FDesignOptionalBool() = default;
	FDesignOptionalBool(const bool InValue) : bIsSet(true), Value(InValue) {}

	UPROPERTY()
	bool bIsSet = false;

	UPROPERTY()
	bool Value = false;

	FDesignOptionalBool& operator=(const bool InValue)
	{
		bIsSet = true;
		Value = InValue;
		return *this;
	}

	bool IsSet() const { return bIsSet; }

	bool& GetValue()
	{
		checkf(bIsSet, TEXT("FDesignOptionalBool has no value."));
		return Value;
	}

	const bool& GetValue() const
	{
		checkf(bIsSet, TEXT("FDesignOptionalBool has no value."));
		return Value;
	}

	bool Get(const bool DefaultValue) const { return bIsSet ? Value : DefaultValue; }

	void Reset()
	{
		bIsSet = false;
		Value = false;
	}
};

USTRUCT()
struct FDesignOptionalVector2D
{
	GENERATED_BODY()

	FDesignOptionalVector2D() = default;
	FDesignOptionalVector2D(const FVector2D& InValue) : bIsSet(true), Value(InValue) {}

	UPROPERTY()
	bool bIsSet = false;

	UPROPERTY()
	FVector2D Value = FVector2D::ZeroVector;

	FDesignOptionalVector2D& operator=(const FVector2D& InValue)
	{
		bIsSet = true;
		Value = InValue;
		return *this;
	}

	bool IsSet() const { return bIsSet; }

	FVector2D& GetValue()
	{
		checkf(bIsSet, TEXT("FDesignOptionalVector2D has no value."));
		return Value;
	}

	const FVector2D& GetValue() const
	{
		checkf(bIsSet, TEXT("FDesignOptionalVector2D has no value."));
		return Value;
	}

	FVector2D Get(const FVector2D& DefaultValue) const { return bIsSet ? Value : DefaultValue; }

	FVector2D* operator->() { return &GetValue(); }
	const FVector2D* operator->() const { return &GetValue(); }

	void Reset()
	{
		bIsSet = false;
		Value = FVector2D::ZeroVector;
	}
};
