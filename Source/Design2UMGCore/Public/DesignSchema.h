// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "DesignSchema.generated.h"

/**
 * .pen 节点类型枚举
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
 * 尺寸模式枚举
 */
UENUM()
enum class EDesignSizeMode : uint8
{
	Fixed,
	FillContainer,
	FitContent
};

/**
 * 尺寸值结构 */
USTRUCT()
struct FDesignSize
{
	GENERATED_BODY()

	UPROPERTY()
	EDesignSizeMode Mode = EDesignSizeMode::FitContent;

	UPROPERTY()
	float Value = 0.0f;

	static FDesignSize Fixed(float InValue) { return {EDesignSizeMode::Fixed, InValue}; }
	static FDesignSize Fill(float Fallback = 0.0f) { return {EDesignSizeMode::FillContainer, Fallback}; }
	static FDesignSize Fit(float Min = 0.0f) { return {EDesignSizeMode::FitContent, Min}; }

	bool IsFixed() const { return Mode == EDesignSizeMode::Fixed; }
	bool IsFill() const { return Mode == EDesignSizeMode::FillContainer; }
	bool IsFit() const { return Mode == EDesignSizeMode::FitContent; }
};

/**
 * 布局模式枚举
 */
UENUM()
enum class EDesignLayout : uint8
{
	None,       // 绝对定位
	Vertical,   // 垂直布局
	Horizontal, // 水平布局
	Wrap,       // 换行自动布局
	Grid        // 网格布局
};

/**
 * 主轴对齐枚举
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
 * 交叉轴对齐枚举 */
UENUM()
enum class EDesignAlignItems : uint8
{
	Start,
	Center,
	End
};

/**
 * 渐变类型枚举
 */
UENUM()
enum class EDesignGradientType : uint8
{
	Linear,
	Radial,
	Angular
};

/**
 * 渐变色标
 */
USTRUCT()
struct FDesignGradientStop
{
	GENERATED_BODY()

	UPROPERTY()
	FString Color;

	/** 原始变量引用名，用于主题切换时重新解析*/
	FString SourceVar;

	UPROPERTY()
	float Position = 0.0f;
};

/**
 * 渐变填充
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
	float CenterX = 0.5f;

	UPROPERTY()
	float CenterY = 0.5f;

	UPROPERTY()
	float SizeWidth = 1.0f;

	UPROPERTY()
	float SizeHeight = 1.0f;

	UPROPERTY()
	bool bHasCenter = false;

	UPROPERTY()
	bool bHasSize = false;

	UPROPERTY()
	TArray<FDesignGradientStop> Colors;
};

USTRUCT()
struct FDesignMeshGradientHandle
{
	GENERATED_BODY()

	UPROPERTY()
	float X = 0.0f;

	UPROPERTY()
	float Y = 0.0f;

	UPROPERTY()
	bool bHasValue = false;
};

USTRUCT()
struct FDesignMeshGradientPoint
{
	GENERATED_BODY()

	UPROPERTY()
	float X = 0.0f;

	UPROPERTY()
	float Y = 0.0f;

	UPROPERTY()
	bool bHasPosition = false;

	UPROPERTY()
	FString Color;

	FString SourceVar;

	UPROPERTY()
	FDesignMeshGradientHandle InHandle;

	UPROPERTY()
	FDesignMeshGradientHandle OutHandle;

	UPROPERTY()
	FString RawJson;
};

USTRUCT()
struct FDesignMeshGradient
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Rows = 0;

	UPROPERTY()
	int32 Columns = 0;

	UPROPERTY()
	bool bHasRows = false;

	UPROPERTY()
	bool bHasColumns = false;

	UPROPERTY()
	TArray<FDesignMeshGradientPoint> Points;
};

/**
 * 图片填充模式
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
 * 图片裁剪变换x3 仿射矩阵） * Source imageTransform: [[a, b, tx], [c, d, ty]]
 * 将图UV 空间 [0,1]x[0,1] 映射到节点空间 */
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
 * 图片填充
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
	FDesignImageTransform Transform;
};

/**
 * 填充类型枚举
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
 * 填充结构（支持纯色、渐变、图片）
 */
USTRUCT()
struct FDesignFill
{
	GENERATED_BODY()

	UPROPERTY()
	EDesignFillType Type = EDesignFillType::None;

	UPROPERTY()
	FString SolidColor;

	/** 原始变量引用名（如"$--background"），用于主题切换时重新解析*/
	FString SourceVar;

	UPROPERTY()
	FDesignGradient Gradient;

	UPROPERTY()
	FDesignImageFill Image;

	UPROPERTY()
	FDesignMeshGradient MeshGradient;

	UPROPERTY()
	bool bEnabled = true;
	UPROPERTY()
	float Opacity = 1.0f;
	UPROPERTY()
	FString BlendMode;
	bool bExplicitClear = false;  // fill:[] 显式清除
	bool bFromMeshGradient = false;
	FString MeshGradientRaw;

	bool IsNone() const { return Type == EDesignFillType::None; }
	bool IsColor() const { return Type == EDesignFillType::Color; }
	bool IsGradient() const { return Type == EDesignFillType::Gradient; }
	bool IsImage() const { return Type == EDesignFillType::Image; }

	static FDesignFill FromColor(const FString& Color)
	{
		FDesignFill Fill;
		Fill.Type = EDesignFillType::Color;
		Fill.SolidColor = Color;
		return Fill;
	}
};

/**
 * 描边对齐枚举
 */
UENUM()
enum class EDesignStrokeAlign : uint8
{
	Inside,
	Center,
	Outside
};

/**
 * 描边厚度（支持单值或四边） */
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

	float GetUniform() const { return bUniform ? Uniform : 0.0f; }
};

/**
 * 描边结构
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
	FDesignFill Fill;

	bool bHasExplicitFill = false;

	bool HasStroke() const { return Thickness.bUniform ? Thickness.Uniform > 0 :
		(Thickness.Top > 0 || Thickness.Right > 0 || Thickness.Bottom > 0 || Thickness.Left > 0); }
};

/**
 * 内边距结构 */
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

	bool IsZero() const { return Top == 0 && Right == 0 && Bottom == 0 && Left == 0; }
	FMargin ToMargin() const { return FMargin(Left, Top, Right, Bottom); }
};

/**
 * 圆角结构（支持单值或四角） */
USTRUCT()
struct FDesignCornerRadius
{
	GENERATED_BODY()

	UPROPERTY()
	bool bUniform = true;

	UPROPERTY()
	float Uniform = 0.0f;

	// [左上, 右上, 右下, 左下]
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

/**
 * 文本增长模式
 */
UENUM()
enum class EDesignTextGrowth : uint8
{
	Auto,
	FixedWidth,
	FixedWidthHeight
};

/**
 * 文本水平对齐
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
 * 文本垂直对齐
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
	float FontSize = 0.0f;

	UPROPERTY()
	bool bHasFontSize = false;

	UPROPERTY()
	FString FontWeight;

	UPROPERTY()
	FString FontStyle;

	UPROPERTY()
	float LineHeight = 0.0f;

	UPROPERTY()
	bool bHasLineHeight = false;

	UPROPERTY()
	float LetterSpacing = 0.0f;

	UPROPERTY()
	bool bHasLetterSpacing = false;

	UPROPERTY()
	bool bUnderline = false;

	UPROPERTY()
	bool bHasUnderline = false;

	UPROPERTY()
	bool bStrikethrough = false;

	UPROPERTY()
	bool bHasStrikethrough = false;

	UPROPERTY()
	FString Href;

	UPROPERTY()
	FString RawJson;
};

/**
 * 变量类型枚举
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
 * 主题值条目（变量在某个主题下的值）
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

struct FDesignNode
{
	FString Id;
	FString Name;
	EDesignNodeType Type = EDesignNodeType::Unknown;
	TMap<FString, FString> Theme;

	float X = 0.0f;
	float Y = 0.0f;
	bool bHasX = false;
	bool bHasY = false;
	FDesignSize Width;
	FDesignSize Height;
	bool bHasWidth = false;
	bool bHasHeight = false;
	float Rotation = 0.0f;

	bool bEnabled = true;
	float Opacity = 1.0f;
	bool bHasOpacity = false;
	bool bFlipX = false;
	bool bFlipY = false;

	EDesignLayout Layout = EDesignLayout::Horizontal;
	float Gap = 0.0f;
	bool bHasGap = false;
	FDesignPadding Padding;
	bool bHasPadding = false;
	EDesignJustifyContent JustifyContent = EDesignJustifyContent::Start;
	EDesignAlignItems AlignItems = EDesignAlignItems::Start;
	bool bAbsoluteLayoutPositioning = false;
	bool bClip = false;
	bool bHasClip = false;
	bool bSemanticPlaceholderSurface = false;
	TMap<FString, FString> Metadata;
	float LayoutGrow = -1.0f;

	FDesignFill Fill;
	TArray<FDesignFill> Fills;
	FDesignStroke Stroke;
	FDesignCornerRadius CornerRadius;
	TArray<FDesignEffect> Effects;
	bool bEffectsExplicitClear = false;

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

	float InnerRadius = 0.0f;
	float StartAngle = 0.0f;
	float SweepAngle = 360.0f;

	int32 PolygonCount = 6;

	FString Geometry;
	FString FillRule = TEXT("nonzero");

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
		return !Fill.IsNone() || Fills.Num() > 0 || Stroke.HasStroke() || CornerRadius.HasRadius();
	}

	bool HasMultiFill() const
	{
		return Fills.Num() > 1;
	}

	bool IsCollapsed() const
	{
		return !bEnabled;
	}
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

struct FDesignDocument
{
	FString Version;
	TArray<FDesignFont> Fonts;
	TMap<FString, TArray<FString>> Themes;
	TMap<FString, FDesignVariable> Variables;
	TMap<FString, FString> Metadata;
	TArray<TSharedPtr<FDesignNode>> Children;

	TSharedPtr<FDesignNode> FindReusableNode(const FString& Id) const
	{
		return FindNodeRecursive(Children, Id, true);
	}

private:
	static TSharedPtr<FDesignNode> FindNodeRecursive(
		const TArray<TSharedPtr<FDesignNode>>& Nodes,
		const FString& Id,
		bool bReusableOnly)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
				continue;
			if (Node->Id == Id && (!bReusableOnly || Node->bReusable))
				return Node;
			if (const TSharedPtr<FDesignNode> Found = FindNodeRecursive(Node->Children, Id, bReusableOnly))
				return Found;
		}
		return nullptr;
	}
};




