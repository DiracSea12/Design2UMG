// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "DesignSchema.generated.h"

/**
 * .pen 鑺傜偣绫诲瀷鏋氫妇
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
 * 灏哄妯″紡鏋氫妇
 */
UENUM()
enum class EDesignSizeMode : uint8
{
	Fixed,
	FillContainer,
	FitContent
};

/**
 * 灏哄鍊肩粨鏋? */
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
 * 甯冨眬妯″紡鏋氫妇
 */
UENUM()
enum class EDesignLayout : uint8
{
	None,       // 缁濆瀹氫綅
	Vertical,   // 鍨傜洿甯冨眬
	Horizontal, // 姘村钩甯冨眬
	Wrap,       // 鎹㈣鑷姩甯冨眬
	Grid        // 缃戞牸甯冨眬
};

/**
 * 涓昏酱瀵归綈鏋氫妇
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
 * 浜ゅ弶杞村榻愭灇涓? */
UENUM()
enum class EDesignAlignItems : uint8
{
	Start,
	Center,
	End
};

/**
 * 娓愬彉绫诲瀷鏋氫妇
 */
UENUM()
enum class EDesignGradientType : uint8
{
	Linear,
	Radial,
	Angular
};

/**
 * 娓愬彉鑹叉爣
 */
USTRUCT()
struct FDesignGradientStop
{
	GENERATED_BODY()

	UPROPERTY()
	FString Color;

	/** 鍘熷鍙橀噺寮曠敤鍚嶏紝鐢ㄤ簬涓婚鍒囨崲鏃堕噸鏂拌В鏋?*/
	FString SourceVar;

	UPROPERTY()
	float Position = 0.0f;
};

/**
 * 娓愬彉濉厖
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
 * 鍥剧墖濉厖妯″紡
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
 * 鍥剧墖瑁佸壀鍙樻崲锛?x3 浠垮皠鐭╅樀锛? * Source imageTransform: [[a, b, tx], [c, d, ty]]
 * 灏嗗浘鐗?UV 绌洪棿 [0,1]x[0,1] 鏄犲皠鍒拌妭鐐圭┖闂? */
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
 * 鍥剧墖濉厖
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
 * 濉厖绫诲瀷鏋氫妇
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
 * 濉厖缁撴瀯锛堟敮鎸佺函鑹层€佹笎鍙樸€佸浘鐗囷級
 */
USTRUCT()
struct FDesignFill
{
	GENERATED_BODY()

	UPROPERTY()
	EDesignFillType Type = EDesignFillType::None;

	UPROPERTY()
	FString SolidColor;

	/** 鍘熷鍙橀噺寮曠敤鍚嶏紙濡?"$--background"锛夛紝鐢ㄤ簬涓婚鍒囨崲鏃堕噸鏂拌В鏋?*/
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
	bool bExplicitClear = false;  // fill:[] 鏄惧紡娓呴櫎
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
 * 鎻忚竟瀵归綈鏋氫妇
 */
UENUM()
enum class EDesignStrokeAlign : uint8
{
	Inside,
	Center,
	Outside
};

/**
 * 鎻忚竟鍘氬害锛堟敮鎸佸崟鍊兼垨鍥涜竟锛? */
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
 * 鎻忚竟缁撴瀯
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

	bool HasStroke() const { return Thickness.bUniform ? Thickness.Uniform > 0 :
		(Thickness.Top > 0 || Thickness.Right > 0 || Thickness.Bottom > 0 || Thickness.Left > 0); }
};

/**
 * 鍐呰竟璺濈粨鏋? */
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
 * 鍦嗚缁撴瀯锛堟敮鎸佸崟鍊兼垨鍥涜锛? */
USTRUCT()
struct FDesignCornerRadius
{
	GENERATED_BODY()

	UPROPERTY()
	bool bUniform = true;

	UPROPERTY()
	float Uniform = 0.0f;

	// [宸︿笂, 鍙充笂, 鍙充笅, 宸︿笅]
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
 * 鏂囨湰澧為暱妯″紡
 */
UENUM()
enum class EDesignTextGrowth : uint8
{
	Auto,
	FixedWidth,
	FixedWidthHeight
};

/**
 * 鏂囨湰姘村钩瀵归綈
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
 * 鏂囨湰鍨傜洿瀵归綈
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
 * 鍙橀噺绫诲瀷鏋氫妇
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
 * 涓婚鍊兼潯鐩紙鍙橀噺鍦ㄦ煇涓富棰樹笅鐨勫€硷級
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




