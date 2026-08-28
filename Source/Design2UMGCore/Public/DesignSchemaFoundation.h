// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchemaFoundation.generated.h"

/**
 * Supported design node types parsed from source documents.
 */
UENUM()
enum class EDesignNodeType : uint8
{
	Frame,
	Rectangle,
	Ellipse,
	Line,
	Polygon,
	Text,
	IconFont,
	Path,
	Ref,
	Group,
	Connection,
	Note,
	Prompt,
	Context,
	Unknown
};

/**
 * Size mode for layout sizing semantics.
 */
UENUM()
enum class EDesignSizeMode : uint8
{
	Fixed,
	FillContainer,
	FitContent
};

/**
 * Size value with mode-aware helpers.
 */
USTRUCT()
struct FDesignSize
{
	GENERATED_BODY()

	UPROPERTY()
	EDesignSizeMode Mode = EDesignSizeMode::FitContent;

	UPROPERTY()
	float Value = 0.0f;

	[[nodiscard]] static FDesignSize Fixed(float InValue) { return {EDesignSizeMode::Fixed, InValue}; }
	[[nodiscard]] static FDesignSize Fill(float Fallback = 0.0f) { return {EDesignSizeMode::FillContainer, Fallback}; }
	[[nodiscard]] static FDesignSize Fit(float Min = 0.0f) { return {EDesignSizeMode::FitContent, Min}; }

	bool IsFixed() const { return Mode == EDesignSizeMode::Fixed; }
	bool IsFill() const { return Mode == EDesignSizeMode::FillContainer; }
	bool IsFit() const { return Mode == EDesignSizeMode::FitContent; }
};

/**
 * Layout mode used by the shared design IR.
 */
UENUM()
enum class EDesignLayout : uint8
{
	None,       // Absolute positioning
	Vertical,   // Vertical auto layout
	Horizontal, // Horizontal auto layout
	Wrap,       // Wrapping auto layout
	Grid        // Grid layout
};

/**
 * Main-axis alignment options.
 */
UENUM()
enum class EDesignJustifyContent : uint8
{
	Start,
	Center,
	End,
	SpaceBetween,
	SpaceAround
};

/**
 * Cross-axis alignment options.
 */
UENUM()
enum class EDesignAlignItems : uint8
{
	Start,
	Center,
	End
};

/**
 * Axis-relative child alignment used after source-specific layout values are normalized.
 */
UENUM()
enum class EDesignAxisAlignment : uint8
{
	Start,
	Center,
	End,
	Fill
};

/**
 * Runtime overflow behavior after source-specific values are normalized.
 */
UENUM()
enum class EDesignOverflow : uint8
{
	None,
	HorizontalScrolling,
	VerticalScrolling
};

/**
 * Supported render-fallback replacement scope.
 */
UENUM()
enum class EDesignRenderFallbackScope : uint8
{
	Leaf,
	Subtree,
	Unsupported
};

/**
 * Gradient type enumeration.
 */
UENUM()
enum class EDesignGradientType : uint8
{
	Linear,
	Radial,
	Angular
};

/**
 * Gradient color stop.
 */
USTRUCT()
struct FDesignGradientStop
{
	GENERATED_BODY()

	UPROPERTY()
	FString Color;

	/** Transient source-extension state: original variable reference resolved again during theme switching. */
	FString SourceVar;

	UPROPERTY()
	float Position = 0.0f;
};

/**
 * Gradient fill payload.
 */
USTRUCT()
struct FDesignGradient
{
	GENERATED_BODY()

	UPROPERTY()
	EDesignGradientType GradientType = EDesignGradientType::Linear;

	UPROPERTY()
	float Rotation = 0.0f;

	UPROPERTY()
	TOptional<FVector2D> StartPoint;

	UPROPERTY()
	TOptional<FVector2D> EndPoint;

	UPROPERTY()
	TOptional<FVector2D> Center;

	UPROPERTY()
	TOptional<FVector2D> Size;

	UPROPERTY()
	TArray<FDesignGradientStop> Colors;
};

/**
 * Image fill mode.
 */
UENUM()
enum class EDesignImageMode : uint8
{
	Stretch,
	Fill,
	Fit,
	Crop
};

/**
 * 2x3 affine transform for image cropping.
 * Source imageTransform: [[a, b, tx], [c, d, ty]]
 * Maps source UV space [0,1]x[0,1] into node space.
 */
USTRUCT()
struct FDesignImageTransform
{
	GENERATED_BODY()

	UPROPERTY()
	float A = 1.0f;
	UPROPERTY()
	float B = 0.0f;
	UPROPERTY()
	float TX = 0.0f;
	UPROPERTY()
	float C = 0.0f;
	UPROPERTY()
	float D = 1.0f;
	UPROPERTY()
	float TY = 0.0f;

	bool IsIdentity() const
	{
		return FMath::IsNearlyEqual(A, 1.0f) && FMath::IsNearlyZero(B) && FMath::IsNearlyZero(TX)
			&& FMath::IsNearlyZero(C) && FMath::IsNearlyEqual(D, 1.0f) && FMath::IsNearlyZero(TY);
	}
};

/**
 * Image fill payload.
 */
USTRUCT()
struct FDesignImageFill
{
	GENERATED_BODY()

	UPROPERTY()
	FString Url;

	UPROPERTY()
	EDesignImageMode Mode = EDesignImageMode::Stretch;

	UPROPERTY()
	float Opacity = 1.0f;

	UPROPERTY()
	TOptional<float> ScalingFactor;

	UPROPERTY()
	FDesignImageTransform Transform;
};

/**
 * Fill type enumeration.
 */
UENUM()
enum class EDesignFillType : uint8
{
	None,
	Color,
	Gradient,
	Image
};

/**
 * Fill payload supporting solid colors, gradients, and images.
 */
USTRUCT()
struct FDesignFill
{
	GENERATED_BODY()

	UPROPERTY()
	EDesignFillType Type = EDesignFillType::None;

	UPROPERTY()
	FString SolidColor;

	/** Transient source-extension state: original variable reference resolved again during theme switching. */
	FString SourceVar;

	UPROPERTY()
	FDesignGradient Gradient;

	UPROPERTY()
	FDesignImageFill Image;

	UPROPERTY()
	bool bEnabled = true;
	UPROPERTY()
	float Opacity = 1.0f;
	UPROPERTY()
	FString BlendMode;

	/** Transient parser/runtime state: source declared an explicit empty fill list. */
	bool bExplicitClear = false;

	bool IsNone() const { return Type == EDesignFillType::None; }
	bool IsColor() const { return Type == EDesignFillType::Color; }
	bool IsImage() const { return Type == EDesignFillType::Image; }

	[[nodiscard]] static FDesignFill FromColor(const FString& Color)
	{
		FDesignFill Fill;
		Fill.Type = EDesignFillType::Color;
		Fill.SolidColor = Color;
		return Fill;
	}
};

/**
 * Stroke alignment enumeration.
 */
UENUM()
enum class EDesignStrokeAlign : uint8
{
	Inside,
	Center,
	Outside
};

/**
 * Stroke thickness supporting either a uniform value or per-edge values.
 */
USTRUCT()
struct FDesignStrokeThickness
{
	GENERATED_BODY()

	UPROPERTY()
	bool bUniform = true;

	UPROPERTY()
	float Uniform = 0.0f;

	UPROPERTY()
	float Top = 0.0f;

	UPROPERTY()
	float Right = 0.0f;

	UPROPERTY()
	float Bottom = 0.0f;

	UPROPERTY()
	float Left = 0.0f;
};

/**
 * Stroke payload.
 */
USTRUCT()
struct FDesignStroke
{
	GENERATED_BODY()

	UPROPERTY()
	EDesignStrokeAlign Align = EDesignStrokeAlign::Inside;

	UPROPERTY()
	FDesignStrokeThickness Thickness;

	UPROPERTY()
	TOptional<FDesignFill> Fill;

	UPROPERTY()
	FString Cap = TEXT("butt");

	UPROPERTY()
	FString Join = TEXT("miter");

	UPROPERTY()
	TArray<float> DashPattern;

	bool HasStroke() const { return Thickness.bUniform ? Thickness.Uniform > 0 :
		(Thickness.Top > 0 || Thickness.Right > 0 || Thickness.Bottom > 0 || Thickness.Left > 0); }
};

/**
 * Padding payload.
 */
USTRUCT()
struct FDesignPadding
{
	GENERATED_BODY()

	UPROPERTY()
	float Top = 0.0f;

	UPROPERTY()
	float Right = 0.0f;

	UPROPERTY()
	float Bottom = 0.0f;

	UPROPERTY()
	float Left = 0.0f;
};

/**
 * Corner-radius payload supporting either a uniform value or per-corner values.
 */
USTRUCT()
struct FDesignCornerRadius
{
	GENERATED_BODY()

	UPROPERTY()
	bool bUniform = true;

	UPROPERTY()
	float Uniform = 0.0f;

	// [top-left, top-right, bottom-right, bottom-left]
	UPROPERTY()
	float TopLeft = 0.0f;

	UPROPERTY()
	float TopRight = 0.0f;

	UPROPERTY()
	float BottomRight = 0.0f;

	UPROPERTY()
	float BottomLeft = 0.0f;

	FVector4 ToVector4() const
	{
		if (bUniform)
			return FVector4(Uniform, Uniform, Uniform, Uniform);
		return FVector4(TopLeft, TopRight, BottomRight, BottomLeft);
	}

	bool HasRadius() const
	{
		if (bUniform) return Uniform > 0;
		return TopLeft > 0 || TopRight > 0 || BottomRight > 0 || BottomLeft > 0;
	}
};

UENUM()
enum class EDesignGeometryMaskShape : uint8
{
	None,
	Ellipse,
	RoundedRectangle
};

USTRUCT()
struct FDesignGeometryMask
{
	GENERATED_BODY()

	UPROPERTY()
	EDesignGeometryMaskShape Shape = EDesignGeometryMaskShape::None;

	UPROPERTY()
	FDesignCornerRadius CornerRadius;

	bool HasMask() const
	{
		return Shape != EDesignGeometryMaskShape::None;
	}
};

/**
 * Text growth mode.
 */
UENUM()
enum class EDesignTextGrowth : uint8
{
	Auto,
	FixedWidth,
	FixedWidthHeight
};

/**
 * Horizontal text alignment.
 */
UENUM()
enum class EDesignTextAlign : uint8
{
	Left,
	Center,
	Right,
	Justify
};

/**
 * Vertical text alignment.
 */
UENUM()
enum class EDesignTextAlignVertical : uint8
{
	Top,
	Middle,
	Bottom
};

USTRUCT()
struct FDesignTextRun
{
	GENERATED_BODY()

	UPROPERTY()
	FString Content;

	UPROPERTY()
	FString FontFamily;

	UPROPERTY()
	FString FontPostScriptName;

	UPROPERTY()
	TOptional<float> FontSize;

	UPROPERTY()
	FString FontWeight;

	UPROPERTY()
	FString FontStyle;

	UPROPERTY()
	TOptional<float> LineHeight;

	UPROPERTY()
	TOptional<float> LetterSpacing;

	UPROPERTY()
	TOptional<bool> bUnderline;

	UPROPERTY()
	TOptional<bool> bStrikethrough;

	UPROPERTY()
	FString Href;

	UPROPERTY()
	FString RawJson;
};

/**
 * Variable type enumeration.
 */
UENUM()
enum class EDesignVariableType : uint8
{
	Boolean,
	Color,
	Number,
	String
};

/**
 * Theme-specific variable value entry.
 */
USTRUCT()
struct FDesignThemeValue
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FString, FString> ThemeMatch;

	UPROPERTY()
	FString Value;
};

USTRUCT()
struct FDesignVariable
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	EDesignVariableType Type = EDesignVariableType::String;

	UPROPERTY()
	FString Value;

	UPROPERTY()
	TArray<FDesignThemeValue> ThemedValues;
};

UENUM()
enum class EDesignEffectType : uint8
{
	None,
	Blur,
	BackgroundBlur,
	Shadow
};

USTRUCT()
struct FDesignShadow
{
	GENERATED_BODY()

	UPROPERTY()
	bool bInner = false;

	UPROPERTY()
	float OffsetX = 0.0f;

	UPROPERTY()
	float OffsetY = 0.0f;

	UPROPERTY()
	float Blur = 0.0f;

	UPROPERTY()
	float Spread = 0.0f;

	UPROPERTY()
	FString Color;

	UPROPERTY()
	FString BlendMode;

	FString ColorSourceVar;
};

USTRUCT()
struct FDesignEffect
{
	GENERATED_BODY()

	UPROPERTY()
	EDesignEffectType Type = EDesignEffectType::None;

	UPROPERTY()
	bool bEnabled = true;

	UPROPERTY()
	float Radius = 0.0f;

	UPROPERTY()
	FString BlendMode;

	UPROPERTY()
	FDesignShadow Shadow;
};

USTRUCT()
struct FDesignFont
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Url;
};
