// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

/**
 * Typed text style fields such as font size and letter spacing.
 * These stay on the text widget when WrapWithBorder transfers visual fields.
 */
struct FUMGTextStyleData
{
	float FontSize = 0;              // 0 = unset
	float SourceFontSize = 0;        // Original design font size before px->UMG scaling
	TOptional<float> LetterSpacing;  // Distinguishes explicit zero from unset
};

/**
 * Typed widget-level style overrides applied during UMG generation.
 */
struct FUMGWidgetStyleData
{
	TOptional<float> WidthOverride;
	TOptional<float> HeightOverride;
	TOptional<float> MinDesiredWidth;
	TOptional<float> MinDesiredHeight;
	TOptional<FMargin> Padding;
	TOptional<FVector2D> InnerSlotPadding;
	TOptional<FMargin> SlotPadding;
	TOptional<FMargin> StrokePadding;
	TOptional<FMargin> StrokeMargin;
	TOptional<FString> ImageMode;
	TOptional<FString> ImageTransform;
	TOptional<FString> Stretch;
	TOptional<FString> StretchDirection;
	TOptional<FLinearColor> BackgroundColor;
	TOptional<FLinearColor> HoveredColor;
	TOptional<FLinearColor> PressedColor;
	TOptional<FString> NamedSlot;
	TOptional<FString> SlotName;
	TOptional<FString> Visibility;
	TOptional<FString> ToolTipText;
	TOptional<bool> IsEnabled;

	bool HasAny() const
	{
		return WidthOverride.IsSet()
			|| HeightOverride.IsSet()
			|| MinDesiredWidth.IsSet()
			|| MinDesiredHeight.IsSet()
			|| Padding.IsSet()
			|| InnerSlotPadding.IsSet()
			|| SlotPadding.IsSet()
			|| StrokePadding.IsSet()
			|| StrokeMargin.IsSet()
			|| ImageMode.IsSet()
			|| ImageTransform.IsSet()
			|| Stretch.IsSet()
			|| StretchDirection.IsSet()
			|| BackgroundColor.IsSet()
			|| HoveredColor.IsSet()
			|| PressedColor.IsSet()
			|| NamedSlot.IsSet()
			|| SlotName.IsSet()
			|| Visibility.IsSet()
			|| ToolTipText.IsSet()
			|| IsEnabled.IsSet();
	}
};

// Not migrated to TOptional: bHas* presence bits feed parser/serializer typed access
// paths that depend on explicit set/unset distinction (e.g. bHasText vs empty string).
struct FUMGTextData
{
	FString Text;
	bool bHasText = false;

	FString RichTextStyleRowsJson;
	bool bHasRichTextStyleRows = false;

	FString HintText;
	bool bHasHintText = false;

	FString FontFamily;
	bool bHasFontFamily = false;

	FString TypefaceFontName;
	bool bHasTypefaceFontName = false;

	FString FontStyle;
	bool bHasFontStyle = false;

	FString Justification;
	bool bHasJustification = false;

	bool bAutoWrapText = false;
	bool bHasAutoWrapText = false;

	float WrapTextAt = 0.0f;
	bool bHasWrapTextAt = false;

	float LineHeightPercentage = 0.0f;
	bool bHasLineHeightPercentage = false;

	bool bUnderline = false;
	bool bHasUnderline = false;

	bool bStrikethrough = false;
	bool bHasStrikethrough = false;

	FString Href;
	bool bHasHref = false;

	FString TextAlignVertical;
	bool bHasTextAlignVertical = false;

	FString IconName;
	bool bHasIconName = false;

	FLinearColor Color = FLinearColor::Transparent;
	bool bHasColor = false;

	bool HasAny() const
	{
		return bHasText
			|| bHasRichTextStyleRows
			|| bHasHintText
			|| bHasFontFamily
			|| bHasTypefaceFontName
			|| bHasFontStyle
			|| bHasJustification
			|| bHasAutoWrapText
			|| bHasWrapTextAt
			|| bHasLineHeightPercentage
			|| bHasUnderline
			|| bHasStrikethrough
			|| bHasHref
			|| bHasTextAlignVertical
			|| bHasIconName
			|| bHasColor;
	}
};

/**
 * Typed visual brush fields that progressively replace visual keys from the legacy property bag.
 * They are transferred to the Border node as a whole when WrapWithBorder is used.
 */
// Not migrated to TOptional: parser, serializer, and typed descriptors depend on the
// same bHas* presence-bit shape for FUMGVisualBrushData.
struct FUMGVisualBrushData
{
	struct FImageTransformData
	{
		float A = 1.0f;
		float B = 0.0f;
		float TX = 0.0f;
		float C = 0.0f;
		float D = 1.0f;
		float TY = 0.0f;

		bool IsIdentity() const
		{
			return FMath::IsNearlyEqual(A, 1.0f) && FMath::IsNearlyZero(B) && FMath::IsNearlyZero(TX)
				&& FMath::IsNearlyZero(C) && FMath::IsNearlyEqual(D, 1.0f) && FMath::IsNearlyZero(TY);
		}
	};

	struct FImageData
	{
		FString TexturePath;
		bool bHasTexturePath = false;

		FString Mode;
		bool bHasMode = false;

		FImageTransformData Transform;
		bool bHasTransform = false;

		bool HasAny() const
		{
			return bHasTexturePath || bHasMode || bHasTransform;
		}
	};

	struct FGradientStopData
	{
		FLinearColor Color = FLinearColor::Transparent;
		float Position = 0.0f;
	};

	struct FGradientData
	{
		FString Type;
		bool bHasType = false;

		float Rotation = 0.0f;
		bool bHasRotation = false;

		FVector2D Center = FVector2D(0.5f, 0.5f);
		bool bHasCenter = false;

		FVector2D Size = FVector2D(1.0f, 1.0f);
		bool bHasSize = false;

		TArray<FGradientStopData> Stops;

		bool HasAny() const
		{
			return bHasType || bHasRotation || bHasCenter || bHasSize || Stops.Num() > 0;
		}
	};

	float OutlineCornerRadius = -1;  // -1 = unset
	FVector4 CornerRadii = FVector4(-1, -1, -1, -1);
	float OutlineThickness = 0;
	bool bHasOutlineThickness = false;

	ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType;
	bool bHasDrawAs = false;         // Distinguishes an explicit NoDrawType from "unset"
	FLinearColor BrushColor = FLinearColor::Transparent;
	bool bHasBrushColor = false;
	FLinearColor OutlineColor = FLinearColor::Transparent;
	bool bHasOutlineColor = false;

	FString MaterialRef;
	bool bHasMaterialRef = false;
	FString MaterialParams;
	bool bHasMaterialParams = false;
	FString TexturePath;
	FImageData Image;
	FGradientData Gradient;

	bool HasCornerRadii() const { return CornerRadii.X >= 0; }
};

/**
 * Typed effect fields such as BackgroundBlur and Retainer effects.
 * New effect contracts should land here instead of extending the legacy property bag.
 */
// Not migrated to TOptional: unset vs explicit value distinction is required by the
// typed effect contract (e.g. bHasBlurStrength distinguishes zero from absent).
struct FUMGEffectData
{
	float BlurStrength = 0.0f;
	bool bHasBlurStrength = false;

	bool bRetainRendering = false;
	bool bHasRetainRendering = false;

	FString EffectMaterial;
	bool bHasEffectMaterial = false;
	FString TextureParameter;
	bool bHasTextureParameter = false;

	FVector2D EffectBlurUVStep = FVector2D::ZeroVector;
	bool bHasEffectBlurUVStep = false;

	FLinearColor EffectColor = FLinearColor::Transparent;
	bool bHasEffectColor = false;

	FVector2D EffectOffset = FVector2D::ZeroVector;
	bool bHasEffectOffset = false;

	int32 Phase = 0;
	int32 PhaseCount = 1;
	bool bHasRenderingPhase = false;

	bool HasAny() const
	{
		return bHasBlurStrength
			|| bHasRetainRendering
			|| bHasEffectMaterial
			|| bHasTextureParameter
			|| bHasEffectBlurUVStep
			|| bHasEffectColor
			|| bHasEffectOffset
			|| bHasRenderingPhase;
	}
};

// Not migrated to TOptional: path/shadow/widget-state/scroll-box presence bits are
// retained payload ownership, not optional cleanup candidates.
struct FUMGPathData
{
	FString Geometry;
	bool bHasGeometry = false;

	FString FillRule;
	bool bHasFillRule = false;

	FLinearColor FillColor = FLinearColor::Transparent;
	bool bHasFillColor = false;

	FLinearColor StrokeColor = FLinearColor::Transparent;
	bool bHasStrokeColor = false;

	float StrokeThickness = 0.0f;
	bool bHasStrokeThickness = false;

	FString StrokeCap;
	bool bHasStrokeCap = false;

	FString StrokeJoin;
	bool bHasStrokeJoin = false;

	bool HasAny() const
	{
		return bHasGeometry
			|| bHasFillRule
			|| bHasFillColor
			|| bHasStrokeColor
			|| bHasStrokeThickness
			|| bHasStrokeCap
			|| bHasStrokeJoin;
	}
};

struct FUMGBoxShadowLayerData
{
	FVector2D Offset = FVector2D::ZeroVector;
	float Blur = 0.0f;
	float Spread = 0.0f;
	FLinearColor Color = FLinearColor::Transparent;
};

struct FUMGShadowData
{
	FVector2D ShadowOffset = FVector2D::ZeroVector;
	bool bHasShadowOffset = false;

	FLinearColor ShadowColorAndOpacity = FLinearColor::Transparent;
	bool bHasShadowColorAndOpacity = false;

	float ShadowBlur = 0.0f;
	bool bHasShadowBlur = false;

	TArray<FUMGBoxShadowLayerData> BoxShadows;

	bool HasAny() const
	{
		return bHasShadowOffset
			|| bHasShadowColorAndOpacity
			|| bHasShadowBlur
			|| BoxShadows.Num() > 0;
	}
};

/**
 * Typed render transform and clipping fields for the generated widget node.
 */
// Not migrated to TOptional: generated widget distinguishes unset from explicit value
// for render transform fields.
struct FUMGRenderStyleData
{
	float RenderOpacity = 0.0f;
	bool bHasRenderOpacity = false;

	float RenderAngle = 0.0f;
	bool bHasRenderAngle = false;

	FVector2D RenderTransformScale = FVector2D::ZeroVector;
	bool bHasRenderTransformScale = false;

	FVector2D RenderTransformPivot = FVector2D(0.5f, 0.5f);
	bool bHasRenderTransformPivot = false;

	FVector2D RenderTranslation = FVector2D::ZeroVector;
	bool bHasRenderTranslation = false;

	FString Clipping;
	bool bHasClipping = false;

	bool HasAny() const
	{
		return bHasRenderOpacity
			|| bHasRenderAngle
			|| bHasRenderTransformScale
			|| bHasRenderTransformPivot
			|| bHasRenderTranslation
			|| bHasClipping;
	}
};

// Not migrated to TOptional: active-widget index carries presence semantics.
struct FUMGWidgetStateData
{
	int32 ActiveWidgetIndex = 0;
	bool bHasActiveWidgetIndex = false;

	bool HasAny() const
	{
		return bHasActiveWidgetIndex;
	}
};

// Not migrated to TOptional: scroll-box contract relies on explicit presence markers.
struct FUMGScrollBoxData
{
	FString Orientation;
	bool bHasOrientation = false;

	FString ScrollBarVisibility;
	bool bHasScrollBarVisibility = false;

	bool HasAny() const
	{
		return bHasOrientation || bHasScrollBarVisibility;
	}
};

struct FUMGIconFontMappingChanges
{
	TMap<FString, FString> Upserts;
	TSet<FString> Removals;

	bool HasChanges() const
	{
		return Upserts.Num() > 0 || Removals.Num() > 0;
	}
};
