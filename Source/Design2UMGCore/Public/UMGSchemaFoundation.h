// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Templates/Function.h"
#include "UMGSchemaFoundation.generated.h"

/**
 * Pixel-space bounds kept in UMGSchema foundations to avoid cyclic dependencies.
 */
struct FDesignPixelBounds
{
	float X = 0.0f;  // Top-left X in pixels
	float Y = 0.0f;  // Top-left Y in pixels
	float W = 0.0f;  // Width in pixels
	float H = 0.0f;  // Height in pixels

	float GetRight() const { return X + W; }
	float GetBottom() const { return Y + H; }
	float GetCenterX() const { return X + W / 2.0f; }
	float GetCenterY() const { return Y + H / 2.0f; }
};

/**
 * Single anchor point.
 */
USTRUCT()
struct FUMGAnchorData
{
	GENERATED_BODY()

	UPROPERTY()
	float X = 0.0f;

	UPROPERTY()
	float Y = 0.0f;
};

/**
 * Anchor range (Min/Max).
 */
USTRUCT()
struct FUMGAnchorsData
{
	GENERATED_BODY()

	UPROPERTY()
	FUMGAnchorData Min;

	UPROPERTY()
	FUMGAnchorData Max;
};

/**
 * Offsets data.
 */
USTRUCT()
struct FUMGOffsetsData
{
	GENERATED_BODY()

	UPROPERTY()
	float Left = 0.0f;

	UPROPERTY()
	float Top = 0.0f;

	UPROPERTY()
	float Right = 100.0f;

	UPROPERTY()
	float Bottom = 100.0f;
};

/**
 * Slot layout data describing placement inside the parent container.
 */
USTRUCT()
struct FUMGSlotData
{
	GENERATED_BODY()

	// CanvasPanel-only data
	UPROPERTY()
	FUMGAnchorsData Anchors;

	UPROPERTY()
	FUMGAnchorData Alignment;

	UPROPERTY()
	FUMGOffsetsData Offsets;

	// Shared slot fields
	UPROPERTY()
	FMargin Padding = FMargin(0.0f);

	// VerticalBox / HorizontalBox / Overlay
	UPROPERTY()
	FString HorizontalAlignment = TEXT("Fill"); // Left, Center, Right, Fill

	UPROPERTY()
	FString VerticalAlignment = TEXT("Fill"); // Top, Center, Bottom, Fill

	UPROPERTY()
	FString SizeRule = TEXT("Auto"); // Auto, Fill

	UPROPERTY()
	float FillWeight = 1.0f;

	// CanvasPanel: Size To Content
	UPROPERTY()
	bool bAutoSize = false;

	// GridPanel-only data
	UPROPERTY()
	int32 Row = 0;

	UPROPERTY()
	int32 Column = 0;

	UPROPERTY()
	int32 RowSpan = 1;

	UPROPERTY()
	int32 ColumnSpan = 1;
};

struct FUMGTypedProperty
{
	/** SerializedValue is the final-consumer bridge value; typed fields are transient parse caches. */
	TOptional<FString> SerializedValue;
	bool BoolValue = false;
	int32 IntValue = 0;
	float FloatValue = 0.0f;
	FVector2D Vector2DValue = FVector2D::ZeroVector;
	FVector4 Vector4Value = FVector4(0, 0, 0, 0);
	FMargin MarginValue = FMargin(0.0f);
	FLinearColor ColorValue = FLinearColor::Transparent;
};

struct DESIGN2UMGCORE_API FUMGPropertyBag
{
	// Runtime bridge for final UMG consumers. It is rebuilt from typed node data
	// at generation time and is not legacy serialized FUMGWidgetNode state.
	void Add(const FString& Key, const FString& Value);
	void Add(const TCHAR* Key, const FString& Value);
	void Add(const TCHAR* Key, const TCHAR* Value);

	int32 Remove(const FString& Key);
	int32 Remove(const TCHAR* Key);
	int32 Num() const;
	void Empty();

	bool TryGetBool(const FString& Key, bool& OutValue) const;
	bool TryGetBool(const TCHAR* Key, bool& OutValue) const { return TryGetBool(FString(Key), OutValue); }
	bool TryGetInt(const FString& Key, int32& OutValue) const;
	bool TryGetInt(const TCHAR* Key, int32& OutValue) const { return TryGetInt(FString(Key), OutValue); }
	bool TryGetFloat(const FString& Key, float& OutValue) const;
	bool TryGetFloat(const TCHAR* Key, float& OutValue) const { return TryGetFloat(FString(Key), OutValue); }
	bool TryGetString(const FString& Key, FString& OutValue) const;
	bool TryGetString(const TCHAR* Key, FString& OutValue) const { return TryGetString(FString(Key), OutValue); }
	bool TryGetColor(const FString& Key, FLinearColor& OutValue) const;
	bool TryGetColor(const TCHAR* Key, FLinearColor& OutValue) const { return TryGetColor(FString(Key), OutValue); }
	bool TryGetVector2D(const FString& Key, FVector2D& OutValue) const;
	bool TryGetVector2D(const TCHAR* Key, FVector2D& OutValue) const { return TryGetVector2D(FString(Key), OutValue); }
	bool TryGetVector4(const FString& Key, FVector4& OutValue) const;
	bool TryGetVector4(const TCHAR* Key, FVector4& OutValue) const { return TryGetVector4(FString(Key), OutValue); }
	bool TryGetMargin(const FString& Key, FMargin& OutValue) const;
	bool TryGetMargin(const TCHAR* Key, FMargin& OutValue) const { return TryGetMargin(FString(Key), OutValue); }

	void ForEachSerialized(TFunctionRef<void(const FString&, const FString&)> Visitor) const;

private:
	TMap<FString, FUMGTypedProperty> Values;
};
