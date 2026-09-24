// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "DesignSchemaFoundation.h"

struct FDesignNode;

/**
 * Authored partial state: unset preserves the base; present empty strings/arrays clear it.
 *
 * Field contract: every field listed here must be handled by both RefExpander.cpp merge
 * points — MergePatch (patch-on-patch) and ApplyPatch (patch-on-node). Adding a field
 * without updating both leaves it silently dropped during reference-instance expansion.
 */
struct FDesignNodePatch
{
	TOptional<FString> Name;
	TOptional<float> X;
	TOptional<float> Y;
	TOptional<FDesignSize> Width;
	TOptional<FDesignSize> Height;
	TOptional<float> Rotation;
	TOptional<bool> Enabled;
	TOptional<float> Opacity;
	TOptional<bool> FlipX;
	TOptional<bool> FlipY;
	TOptional<EDesignLayout> Layout;
	TOptional<float> Gap;
	TOptional<FDesignPadding> Padding;
	TOptional<bool> PaddingRequiresRuntimeWrap;
	TOptional<EDesignJustifyContent> JustifyContent;
	TOptional<EDesignAlignItems> AlignItems;
	TOptional<bool> Clip;
	TOptional<float> LayoutGrow;
	TOptional<bool> SemanticPlaceholderSurface;
	TOptional<TArray<FDesignFill>> Fills;
	// Outer unset: inherit paint. Inner unset: explicitly clear paint.
	TOptional<TOptional<FDesignFill>> StrokeFill;
	TOptional<FDesignStrokeThickness> StrokeThickness;
	TOptional<EDesignStrokeAlign> StrokeAlign;
	TOptional<EDesignStrokeCap> StrokeCap;
	TOptional<EDesignStrokeJoin> StrokeJoin;
	TOptional<FDesignCornerRadius> CornerRadius;
	TOptional<TArray<FDesignEffect>> Effects;
	TOptional<FString> Content;
	TOptional<TArray<FDesignTextRun>> TextRuns;
	TOptional<FString> FontFamily;
	TOptional<FString> FontPostScriptName;
	TOptional<float> FontSize;
	TOptional<FString> FontWeight;
	TOptional<FString> FontStyle;
	TOptional<float> LineHeight;
	TOptional<float> LetterSpacing;
	TOptional<EDesignTextAlign> TextAlign;
	TOptional<EDesignTextAlignVertical> TextAlignVertical;
	TOptional<EDesignTextGrowth> TextGrowth;
	TOptional<bool> Underline;
	TOptional<bool> Strikethrough;
	TOptional<FString> Href;
	TOptional<FString> IconFontName;
	TOptional<FString> IconFontFamily;
	TOptional<int32> IconWeight;
	TOptional<float> InnerRadius;
	TOptional<float> StartAngle;
	TOptional<float> SweepAngle;
	TOptional<int32> PolygonCount;
	TOptional<FString> Geometry;
	TOptional<FString> FillRule;
	TOptional<TArray<TSharedPtr<FDesignNode>>> Children;
	TMap<FString, FString> Metadata;
};

/** A typed descendant replaces the node; subsequent property patches still apply to that replacement. */
struct FDesignNodeOverride
{
	TSharedPtr<FDesignNode> Replacement;
	FDesignNodePatch Patch;
};
