// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "UMGSchemaFoundation.h"
#include "UMGSchemaTypedPayloads.h"
#include "UMGSchema.generated.h"

struct FUMGSourceNodeData;
struct FUMGSourceDocumentData;
struct FUMGPrototypeNavigationData;

/**
 * Generated widget node data for a single widget.
 * Source-specific extensions stay opt-in so aggregate consumers do not have to
 * carry source/prototype ownership by default.
 */
USTRUCT()
struct DESIGN2UMGCORE_API FUMGWidgetNode
{
	GENERATED_BODY()

	FUMGWidgetNode();
	FUMGWidgetNode(const FUMGWidgetNode& Other);
	FUMGWidgetNode(FUMGWidgetNode&& Other) noexcept;
	~FUMGWidgetNode();

	FUMGWidgetNode& operator=(const FUMGWidgetNode& Other);
	FUMGWidgetNode& operator=(FUMGWidgetNode&& Other) noexcept;

	/** Widget type: CanvasPanel, VerticalBox, HorizontalBox, Button, TextBlock, Image, etc. */
	UPROPERTY()
	FString Type;

	/** Stable path-like identifier: {ParentId}/{Type}_{Index}. */
	UPROPERTY()
	FString Id;

	/** Widget display name. */
	UPROPERTY()
	FString Name;

	/** Pixel-space bounds, populated during Core conversion by NodeWidgetFactory/LayoutMapper; LayoutCalculator is not on this path (it only projects bounds->slot data for the Editor-side WidgetLayoutAdapter). */
	FDesignPixelBounds Bounds;

	/** Slot layout data, derived by LayoutCalculator from bounds. */
	UPROPERTY()
	FUMGSlotData Slot;

	/** Typed text style data. */
	FUMGTextStyleData TextStyle;

	/** Typed widget-level style and wrapper properties. */
	FUMGWidgetStyleData WidgetStyle;

	/** Typed text content and behavior. */
	FUMGTextData Text;

	/** Typed visual brush data. */
	FUMGVisualBrushData Brush;

	/** Typed effect data. */
	FUMGEffectData Effect;

	/** Typed path and vector payload. */
	FUMGPathData Path;

	/** Typed shadow payload. */
	FUMGShadowData Shadow;

	/** Typed render style payload. */
	FUMGRenderStyleData Render;

	/** Typed widget state payload. */
	FUMGWidgetStateData WidgetState;

	/** Typed ScrollBox configuration. */
	FUMGScrollBoxData ScrollBox;

	/** Child widgets. Not serialized because recursive USTRUCT trees are unsupported by UHT. */
	TArray<FUMGWidgetNode> Children;

	/** Set only by LayoutMapper::ApplyJustifyContent: this child-less Spacer was synthesized to express justify-content, not mapped from a source node. Plain member like Children (UHT recursion limit); the JSON serializer/parser bridge it as "SyntheticJustifySpacer". */
	bool bSyntheticJustifySpacer = false;

	bool HasSourceData() const;
	const FUMGSourceNodeData& Source() const;
	FUMGSourceNodeData& MutableSource();

	/** Design identity key (the single rule for manifest/reimport reconciliation): prefer the source node id; wrapper nodes fall back to the deterministic IR Id. */
	FString GetStableDesignNodeId() const;

private:
	TUniquePtr<FUMGSourceNodeData> SourceData;
};

/**
 * Generated Widget Blueprint data.
 * Source/prototype extensions remain explicit opt-ins instead of living on the
 * aggregate surface by default.
 */
USTRUCT()
struct DESIGN2UMGCORE_API FUMGWidgetData
{
	GENERATED_BODY()

	FUMGWidgetData();
	FUMGWidgetData(const FUMGWidgetData& Other);
	FUMGWidgetData(FUMGWidgetData&& Other) noexcept;
	~FUMGWidgetData();

	FUMGWidgetData& operator=(const FUMGWidgetData& Other);
	FUMGWidgetData& operator=(FUMGWidgetData&& Other) noexcept;

	/** Generated Widget Blueprint asset name. */
	UPROPERTY()
	FString WidgetName = TEXT("WBP_Generated");

	/** Root widget node. */
	UPROPERTY()
	FUMGWidgetNode RootWidget;

	/** Degradation warnings produced during conversion, e.g. Ellipse -> Border. */
	TArray<FString> Warnings;

	/** Icon-font mapping suggestions emitted during conversion. */
	FUMGIconFontMappingChanges IconFontMappingChanges;

	bool HasSourceDocumentData() const;
	const FUMGSourceDocumentData& SourceDocument() const;
	FUMGSourceDocumentData& MutableSourceDocument();

	bool HasPrototypeNavigationData() const;
	const FUMGPrototypeNavigationData& PrototypeNavigation() const;
	FUMGPrototypeNavigationData& MutablePrototypeNavigation();

private:
	TUniquePtr<FUMGSourceDocumentData> SourceDocumentData;
	TUniquePtr<FUMGPrototypeNavigationData> PrototypeNavigationData;
};
