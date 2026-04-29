// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "DesignSchemaFoundation.h"

struct FDesignSourceNodeData;
struct FDesignSourceDocumentData;
struct FDesignParseDiagnostics;

/**
 * Typed text payload for a design node, independent from source format.
 */
struct FDesignTextNodeData
{
	FString Content;
	FString FontFamily;
	float FontSize = 16.0f;
	FString FontWeight = TEXT("normal");
	FString FontStyle = TEXT("normal");
	float LineHeight = 0.0f;
	float LetterSpacing = 0.0f;
	EDesignTextAlign TextAlign = EDesignTextAlign::Left;
	EDesignTextAlignVertical TextAlignVertical = EDesignTextAlignVertical::Top;
	EDesignTextGrowth TextGrowth = EDesignTextGrowth::Auto;
	bool bUnderline = false;
	bool bStrikethrough = false;
	FString Href;
	TArray<FDesignTextRun> TextRuns;
	FString IconFontName;
	FString IconFontFamily;
	int32 IconWeight = 400;
};

/**
 * Shared layout semantics consumed by the converter and layout mapper.
 */
struct FDesignLayoutNodeData
{
	EDesignLayout Layout = EDesignLayout::Horizontal;
	TOptional<float> ExplicitGap;
	TOptional<FDesignPadding> ExplicitPadding;
	bool bHasPaddingRuntimeWrapDecision = false;
	bool bPaddingRequiresRuntimeWrap = false;
	EDesignJustifyContent JustifyContent = EDesignJustifyContent::Start;
	EDesignAlignItems AlignItems = EDesignAlignItems::Start;
	bool bAbsoluteLayoutPositioning = false;
	TOptional<bool> ExplicitClip;
	bool bSemanticPlaceholderSurface = false;
	float LayoutGrow = -1.0f;
	TOptional<int32> LayoutRow;
	TOptional<int32> LayoutColumn;
	TOptional<int32> LayoutRowSpan;
	TOptional<int32> LayoutColumnSpan;
	TOptional<int32> LayoutRowCount;
	TOptional<int32> LayoutColumnCount;
	TOptional<float> LayoutRowGap;
	TOptional<float> LayoutColumnGap;
	TOptional<FString> LayoutChildHorizontalAlign;
	TOptional<FString> LayoutChildVerticalAlign;

	void SetGap(const float InGap)
	{
		ExplicitGap = InGap;
	}

	void SetPadding(const FDesignPadding& InPadding)
	{
		ExplicitPadding = InPadding;
	}

	void SetClipValue(const bool bInClip)
	{
		ExplicitClip = bInClip;
	}

	void SetLayoutRowValue(const int32 InLayoutRow)        { LayoutRow = InLayoutRow; }
	void ClearLayoutRowValue()                             { LayoutRow.Reset(); }
	void SetLayoutColumnValue(const int32 InLayoutColumn)  { LayoutColumn = InLayoutColumn; }
	void ClearLayoutColumnValue()                          { LayoutColumn.Reset(); }
	void SetLayoutRowSpanValue(const int32 InVal)          { LayoutRowSpan = InVal; }
	void SetLayoutColumnSpanValue(const int32 InVal)       { LayoutColumnSpan = InVal; }
	void SetLayoutRowCountValue(const int32 InVal)         { LayoutRowCount = InVal; }
	void SetLayoutColumnCountValue(const int32 InVal)      { LayoutColumnCount = InVal; }
	void SetLayoutRowGapValue(const float InVal)           { LayoutRowGap = InVal; }
	void SetLayoutColumnGapValue(const float InVal)        { LayoutColumnGap = InVal; }

	void SetLayoutChildHorizontalAlignValue(const FString& InVal) { LayoutChildHorizontalAlign = InVal; }
	void SetLayoutChildVerticalAlignValue(const FString& InVal)   { LayoutChildVerticalAlign = InVal; }

	bool HasExplicitGap() const      { return ExplicitGap.IsSet(); }
	bool HasExplicitPadding() const  { return ExplicitPadding.IsSet(); }
	bool HasExplicitClip() const     { return ExplicitClip.IsSet(); }

	bool HasExplicitGridPosition() const
	{
		return LayoutRow.IsSet() || LayoutColumn.IsSet();
	}

	bool HasLayoutRowValue() const          { return LayoutRow.IsSet(); }
	bool HasLayoutColumnValue() const       { return LayoutColumn.IsSet(); }
	bool HasLayoutRowSpanValue() const      { return LayoutRowSpan.IsSet(); }
	bool HasLayoutColumnSpanValue() const   { return LayoutColumnSpan.IsSet(); }
	bool HasLayoutRowCountValue() const     { return LayoutRowCount.IsSet(); }
	bool HasLayoutColumnCountValue() const  { return LayoutColumnCount.IsSet(); }
	bool HasLayoutRowGapValue() const       { return LayoutRowGap.IsSet(); }
	bool HasLayoutColumnGapValue() const    { return LayoutColumnGap.IsSet(); }

	TOptional<int32> GetLayoutRowValue() const    { return LayoutRow; }

	int32 GetLayoutRowOr(const int32 DefaultValue) const         { return LayoutRow.Get(DefaultValue); }
	int32 GetLayoutColumnOr(const int32 DefaultValue) const      { return LayoutColumn.Get(DefaultValue); }
	int32 GetLayoutRowSpanOr(const int32 DefaultValue) const     { return LayoutRowSpan.Get(DefaultValue); }
	int32 GetLayoutColumnSpanOr(const int32 DefaultValue) const  { return LayoutColumnSpan.Get(DefaultValue); }
	float GetLayoutRowGapOr(const float DefaultValue) const      { return LayoutRowGap.Get(DefaultValue); }
	float GetLayoutColumnGapOr(const float DefaultValue) const   { return LayoutColumnGap.Get(DefaultValue); }

	FString GetLayoutChildHorizontalAlignOr(const FString& DefaultValue) const
	{
		return LayoutChildHorizontalAlign.Get(DefaultValue);
	}

	FString GetLayoutChildVerticalAlignOr(const FString& DefaultValue) const
	{
		return LayoutChildVerticalAlign.Get(DefaultValue);
	}
};

/**
 * Shared visual payload for fills, strokes, corners, and effects.
 */
struct FDesignVisualNodeData
{
	FDesignFill Fill;
	TArray<FDesignFill> Fills;
	FDesignStroke Stroke;
	FDesignCornerRadius CornerRadius;
	TArray<FDesignEffect> Effects;
	bool bEffectsExplicitClear = false;
};

struct FDesignShapeNodeData
{
	float InnerRadius = 0.0f;
	float StartAngle = 0.0f;
	float SweepAngle = 360.0f;
	int32 PolygonCount = 6;
	FString Geometry;
	FString FillRule = TEXT("nonzero");
};

/**
 * Shared transform and sizing payload for design-space positioning.
 */
struct FDesignTransformNodeData
{
	// Keep source-presence separate from explicit size: source width/height
	// being present is not the same as a runtime explicit size contract.
	float X = 0.0f;
	float Y = 0.0f;
	TOptional<float> ExplicitX;
	TOptional<float> ExplicitY;
	FDesignSize Width;
	FDesignSize Height;
	bool bHasWidth = false;
	bool bHasHeight = false;
	bool bSourceHasWidth = false;
	bool bSourceHasHeight = false;
	float Rotation = 0.0f;
	bool bEnabled = true;
	float Opacity = 1.0f;
	TOptional<float> ExplicitOpacity;
	bool bFlipX = false;
	bool bFlipY = false;

	void SetPosition(const float InX, const float InY)
	{
		X = InX;
		Y = InY;
		ExplicitX = InX;
		ExplicitY = InY;
	}

	void ClearPosition()
	{
		X = 0.0f;
		Y = 0.0f;
		ExplicitX.Reset();
		ExplicitY.Reset();
	}

	void SetWidthValue(const FDesignSize& InWidth)
	{
		Width = InWidth;
		bHasWidth = true;
		bSourceHasWidth = true;
	}

	void SetHeightValue(const FDesignSize& InHeight)
	{
		Height = InHeight;
		bHasHeight = true;
		bSourceHasHeight = true;
	}

	void SetOpacityValue(const float InOpacity)
	{
		Opacity = InOpacity;
		ExplicitOpacity = InOpacity;
	}

	bool HasExplicitX() const       { return ExplicitX.IsSet(); }
	bool HasExplicitY() const       { return ExplicitY.IsSet(); }
	bool HasExplicitOpacity() const { return ExplicitOpacity.IsSet(); }

	bool HasExplicitPosition() const
	{
		return HasExplicitX() || HasExplicitY();
	}

	bool HasExplicitSize() const
	{
		return bHasWidth || bHasHeight;
	}

	bool HasSourceExplicitSize() const
	{
		return bSourceHasWidth || bSourceHasHeight;
	}
};

/**
 * Shared source-agnostic design IR node used by the whole conversion pipeline.
 * This intentionally remains a plain C++ struct instead of a reflected USTRUCT so
 * the transient IR stays lightweight and independent from editor-facing reflection.
 */
struct DESIGN2UMGCORE_API FDesignNode
{
	FDesignNode();
	FDesignNode(const FDesignNode& Other);
	FDesignNode(FDesignNode&& Other) noexcept;
	~FDesignNode();

	FDesignNode& operator=(const FDesignNode& Other);
	FDesignNode& operator=(FDesignNode&& Other) noexcept;

	FString Id;
	FString Name;
	EDesignNodeType Type = EDesignNodeType::Unknown;
	TMap<FString, FString> Theme;
	FDesignTransformNodeData Transform;
	TMap<FString, FString> Metadata;
	FDesignLayoutNodeData LayoutData;
	FDesignVisualNodeData Visual;
	FDesignTextNodeData Text;
	FDesignShapeNodeData Shape;

	FString Ref;
	bool bReusable = false;
	TMap<FString, TSharedPtr<FDesignNode>> Descendants;
	TArray<TSharedPtr<FDesignNode>> Children;

	bool IsContainer() const
	{
		return Type == EDesignNodeType::Frame || Type == EDesignNodeType::Group;
	}

	bool HasVisualProperties() const
	{
		return !Visual.Fill.IsNone() || Visual.Fills.Num() > 0 || Visual.Stroke.HasStroke() || Visual.CornerRadius.HasRadius();
	}

	bool HasMultiFill() const
	{
		return Visual.Fills.Num() > 1;
	}

	bool IsCollapsed() const
	{
		return !Transform.bEnabled;
	}

	bool HasSourceData() const;
	const FDesignSourceNodeData& Source() const;
	FDesignSourceNodeData& MutableSource();

private:
	TUniquePtr<FDesignSourceNodeData> SourceData;
};

/**
 * Root parsed document for a source design export.
 * This is also intentionally non-reflected for the same lightweight transient-IR reasons.
 */
struct DESIGN2UMGCORE_API FDesignDocument
{
	FDesignDocument();
	FDesignDocument(const FDesignDocument& Other);
	FDesignDocument(FDesignDocument&& Other) noexcept;
	~FDesignDocument();

	FDesignDocument& operator=(const FDesignDocument& Other);
	FDesignDocument& operator=(FDesignDocument&& Other) noexcept;

	FString Version;
	TArray<FDesignFont> Fonts;
	TMap<FString, TArray<FString>> Themes;
	TMap<FString, FDesignVariable> Variables;
	TMap<FString, FString> Metadata;
	TArray<TSharedPtr<FDesignNode>> Children;

	bool HasSourceDocumentData() const;
	const FDesignSourceDocumentData& SourceDocument() const;
	FDesignSourceDocumentData& MutableSourceDocument();

	bool HasParseDiagnostics() const;
	const FDesignParseDiagnostics& ParseDiagnostics() const;
	FDesignParseDiagnostics& MutableParseDiagnostics();

	TSharedPtr<FDesignNode> FindReusableNode(const FString& Id) const;

private:
	static TSharedPtr<FDesignNode> FindNodeRecursive(
		const TArray<TSharedPtr<FDesignNode>>& Nodes,
		const FString& Id,
		bool bReusableOnly);

	TUniquePtr<FDesignSourceDocumentData> SourceDocumentData;
	TUniquePtr<FDesignParseDiagnostics> ParseDiagnosticsData;
};
