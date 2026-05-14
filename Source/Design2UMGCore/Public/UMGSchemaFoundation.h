// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
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
UENUM()
enum class EUMGSlotHorizontalAlignment : uint8
{
	// Keep zero-initialized slot data aligned with the semantic default.
	Fill = 0,
	Left = 1,
	Center = 2,
	Right = 3
};

UENUM()
enum class EUMGSlotVerticalAlignment : uint8
{
	// Keep zero-initialized slot data aligned with the semantic default.
	Fill = 0,
	Top = 1,
	Center = 2,
	Bottom = 3
};

UENUM()
enum class EUMGSlotSizeRule : uint8
{
	Auto = 0,
	Fill = 1
};

static_assert(static_cast<uint8>(EUMGSlotHorizontalAlignment::Fill) == 0,
	"EUMGSlotHorizontalAlignment::Fill must stay zero for default slot compatibility.");
static_assert(static_cast<uint8>(EUMGSlotVerticalAlignment::Fill) == 0,
	"EUMGSlotVerticalAlignment::Fill must stay zero for default slot compatibility.");
static_assert(static_cast<uint8>(EUMGSlotSizeRule::Auto) == 0,
	"EUMGSlotSizeRule::Auto must stay zero for default slot compatibility.");

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
	EUMGSlotHorizontalAlignment HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;

	UPROPERTY()
	EUMGSlotVerticalAlignment VerticalAlignment = EUMGSlotVerticalAlignment::Fill;

	UPROPERTY()
	EUMGSlotSizeRule SizeRule = EUMGSlotSizeRule::Auto;

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
