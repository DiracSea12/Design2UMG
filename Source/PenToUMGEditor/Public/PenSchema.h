// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PenSchema.generated.h"

/**
 * .pen 节点类型枚举
 */
UENUM()
enum class EPenNodeType : uint8
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
enum class EPenSizeMode : uint8
{
	Fixed,          // 固定像素值
	FillContainer,  // 填充父容器
	FitContent      // 适应内容
};

/**
 * 尺寸值结构
 */
USTRUCT()
struct FPenSize
{
	GENERATED_BODY()

	UPROPERTY()
	EPenSizeMode Mode = EPenSizeMode::FitContent;

	UPROPERTY()
	float Value = 0.0f;

	static FPenSize Fixed(float InValue) { return {EPenSizeMode::Fixed, InValue}; }
	static FPenSize Fill(float Fallback = 0.0f) { return {EPenSizeMode::FillContainer, Fallback}; }
	static FPenSize Fit(float Min = 0.0f) { return {EPenSizeMode::FitContent, Min}; }

	bool IsFixed() const { return Mode == EPenSizeMode::Fixed; }
	bool IsFill() const { return Mode == EPenSizeMode::FillContainer; }
	bool IsFit() const { return Mode == EPenSizeMode::FitContent; }
};

/**
 * 布局模式枚举
 */
UENUM()
enum class EPenLayout : uint8
{
	None,       // 绝对定位
	Vertical,   // 垂直布局
	Horizontal  // 水平布局
};

/**
 * 主轴对齐枚举
 */
UENUM()
enum class EPenJustifyContent : uint8
{
	Start,
	Center,
	End,
	SpaceBetween,
	SpaceAround
};

/**
 * 交叉轴对齐枚举
 */
UENUM()
enum class EPenAlignItems : uint8
{
	Start,
	Center,
	End
};

/**
 * 渐变类型枚举
 */
UENUM()
enum class EPenGradientType : uint8
{
	Linear,
	Radial,
	Angular
};

/**
 * 渐变色标
 */
USTRUCT()
struct FPenGradientStop
{
	GENERATED_BODY()

	UPROPERTY()
	FString Color;

	UPROPERTY()
	float Position = 0.0f;
};

/**
 * 渐变填充
 */
USTRUCT()
struct FPenGradient
{
	GENERATED_BODY()

	UPROPERTY()
	EPenGradientType GradientType = EPenGradientType::Linear;

	UPROPERTY()
	float Rotation = 0.0f;

	UPROPERTY()
	TArray<FPenGradientStop> Colors;
};

/**
 * 图片填充模式
 */
UENUM()
enum class EPenImageMode : uint8
{
	Stretch,
	Fill,
	Fit
};

/**
 * 图片填充
 */
USTRUCT()
struct FPenImageFill
{
	GENERATED_BODY()

	UPROPERTY()
	FString Url;

	UPROPERTY()
	EPenImageMode Mode = EPenImageMode::Stretch;

	UPROPERTY()
	float Opacity = 1.0f;
};

/**
 * 填充类型枚举
 */
UENUM()
enum class EPenFillType : uint8
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
struct FPenFill
{
	GENERATED_BODY()

	UPROPERTY()
	EPenFillType Type = EPenFillType::None;

	UPROPERTY()
	FString SolidColor;

	UPROPERTY()
	FPenGradient Gradient;

	UPROPERTY()
	FPenImageFill Image;

	UPROPERTY()
	bool bEnabled = true;

	bool IsNone() const { return Type == EPenFillType::None; }
	bool IsColor() const { return Type == EPenFillType::Color; }
	bool IsGradient() const { return Type == EPenFillType::Gradient; }
	bool IsImage() const { return Type == EPenFillType::Image; }

	static FPenFill FromColor(const FString& Color)
	{
		FPenFill Fill;
		Fill.Type = EPenFillType::Color;
		Fill.SolidColor = Color;
		return Fill;
	}
};

/**
 * 描边对齐枚举
 */
UENUM()
enum class EPenStrokeAlign : uint8
{
	Inside,
	Center,
	Outside
};

/**
 * 描边厚度（支持单值或四边）
 */
USTRUCT()
struct FPenStrokeThickness
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
struct FPenStroke
{
	GENERATED_BODY()

	UPROPERTY()
	EPenStrokeAlign Align = EPenStrokeAlign::Inside;

	UPROPERTY()
	FPenStrokeThickness Thickness;

	UPROPERTY()
	FPenFill Fill;

	bool HasStroke() const { return Thickness.bUniform ? Thickness.Uniform > 0 :
		(Thickness.Top > 0 || Thickness.Right > 0 || Thickness.Bottom > 0 || Thickness.Left > 0); }
};

/**
 * 内边距结构
 */
USTRUCT()
struct FPenPadding
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
 * 圆角结构（支持单值或四角）
 */
USTRUCT()
struct FPenCornerRadius
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
enum class EPenTextGrowth : uint8
{
	Auto,            // 自动适应内容
	FixedWidth,      // 固定宽度，高度自动
	FixedWidthHeight // 固定宽高
};

/**
 * 文本水平对齐
 */
UENUM()
enum class EPenTextAlign : uint8
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
enum class EPenTextAlignVertical : uint8
{
	Top,
	Middle,
	Bottom
};

/**
 * 变量类型枚举
 */
UENUM()
enum class EPenVariableType : uint8
{
	Boolean,
	Color,
	Number,
	String
};

/**
 * 变量定义结构
 */
USTRUCT()
struct FPenVariable
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	EPenVariableType Type = EPenVariableType::String;

	UPROPERTY()
	FString Value;  // 存储为字符串，使用时转换
};

/**
 * 特效类型枚举
 */
UENUM()
enum class EPenEffectType : uint8
{
	None,
	Blur,
	BackgroundBlur,
	Shadow
};

/**
 * 阴影特效
 */
USTRUCT()
struct FPenShadow
{
	GENERATED_BODY()

	UPROPERTY()
	bool bInner = false;  // inner vs outer

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
};

/**
 * 特效结构
 */
USTRUCT()
struct FPenEffect
{
	GENERATED_BODY()

	UPROPERTY()
	EPenEffectType Type = EPenEffectType::None;

	UPROPERTY()
	bool bEnabled = true;

	UPROPERTY()
	float Radius = 0.0f;  // for blur

	UPROPERTY()
	FPenShadow Shadow;
};

/**
 * .pen 节点结构
 */
struct FPenNode
{
	// 基础属性
	FString Id;
	FString Name;
	EPenNodeType Type = EPenNodeType::Unknown;

	// 位置和尺寸
	float X = 0.0f;
	float Y = 0.0f;
	bool bHasX = false;
	bool bHasY = false;
	FPenSize Width;
	FPenSize Height;
	float Rotation = 0.0f;

	// 可见性
	bool bEnabled = true;
	float Opacity = 1.0f;
	bool bFlipX = false;
	bool bFlipY = false;

	// 布局属性 (frame/group)
	EPenLayout Layout = EPenLayout::Horizontal;
	float Gap = 0.0f;
	FPenPadding Padding;
	EPenJustifyContent JustifyContent = EPenJustifyContent::Start;
	EPenAlignItems AlignItems = EPenAlignItems::Start;
	bool bClip = false;
	bool bPlaceholder = false;

	// 视觉属性
	FPenFill Fill;
	FPenStroke Stroke;
	FPenCornerRadius CornerRadius;
	TArray<FPenEffect> Effects;

	// 文本属性
	FString Content;
	FString FontFamily;
	float FontSize = 16.0f;
	FString FontWeight = TEXT("normal");
	float LineHeight = 0.0f;
	float LetterSpacing = 0.0f;
	EPenTextAlign TextAlign = EPenTextAlign::Left;
	EPenTextAlignVertical TextAlignVertical = EPenTextAlignVertical::Top;
	EPenTextGrowth TextGrowth = EPenTextGrowth::Auto;

	// 图标字体属性
	FString IconFontName;
	FString IconFontFamily;
	int32 IconWeight = 400;

	// 椭圆属性
	float InnerRadius = 0.0f;
	float StartAngle = 0.0f;
	float SweepAngle = 360.0f;

	// 多边形属性
	int32 PolygonCount = 6;

	// 路径属性
	FString Geometry;
	FString FillRule = TEXT("nonzero");

	// 组件引用属性
	FString Ref;
	bool bReusable = false;
	TMap<FString, TSharedPtr<FPenNode>> Descendants;

	// 子节点
	TArray<TSharedPtr<FPenNode>> Children;

	// 辅助方法
	bool IsContainer() const
	{
		return Type == EPenNodeType::Frame || Type == EPenNodeType::Group;
	}

	bool HasVisualProperties() const
	{
		return !Fill.IsNone() || Stroke.HasStroke() || CornerRadius.HasRadius();
	}

	bool IsCollapsed() const { return !bEnabled; }
};

/**
 * 字体定义
 */
USTRUCT()
struct FPenFont
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Url;
};

/**
 * .pen 文档结构
 */
struct FPenDocument
{
	FString Version;
	TArray<FPenFont> Fonts;
	TMap<FString, TArray<FString>> Themes;  // 主题轴定义
	TMap<FString, FPenVariable> Variables;
	TArray<TSharedPtr<FPenNode>> Children;

	// 查找可复用组件
	TSharedPtr<FPenNode> FindReusableNode(const FString& Id) const
	{
		return FindNodeRecursive(Children, Id, true);
	}

private:
	static TSharedPtr<FPenNode> FindNodeRecursive(
		const TArray<TSharedPtr<FPenNode>>& Nodes,
		const FString& Id,
		bool bReusableOnly)
	{
		for (const auto& Node : Nodes)
		{
			if (!Node) continue;
			if (Node->Id == Id && (!bReusableOnly || Node->bReusable))
				return Node;
			if (auto Found = FindNodeRecursive(Node->Children, Id, bReusableOnly))
				return Found;
		}
		return nullptr;
	}
};
