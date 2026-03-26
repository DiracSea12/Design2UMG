// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Styling/SlateBrush.h"
#include "UMGSchema.generated.h"

/**
 * 像素坐标Bounds（定义在UMGSchema避免循环依赖） */
struct FBounds
{
	float X = 0.0f;  // 左上角X（像素）
	float Y = 0.0f;  // 左上角Y（像素）
	float W = 0.0f;  // 宽度（像素）
	float H = 0.0f;  // 高度（像素）

	float GetRight() const { return X + W; }
	float GetBottom() const { return Y + H; }
	float GetCenterX() const { return X + W / 2.0f; }
	float GetCenterY() const { return Y + H / 2.0f; }
};

/**
 * Anchor 数据结构
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
 * Anchors 数据结构 (Min/Max)
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
 * Offsets 数据结构
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
 * Slot 数据结构 - 描述控件在父容器中的布局信息
 */
USTRUCT()
struct FUMGSlotData
{
	GENERATED_BODY()

	// CanvasPanel 专用
	UPROPERTY()
	FUMGAnchorsData Anchors;

	UPROPERTY()
	FUMGAnchorData Alignment;

	UPROPERTY()
	FUMGOffsetsData Offsets;

	// 通用
	UPROPERTY()
	FMargin Padding = FMargin(0.0f);

	// VerticalBox/HorizontalBox/Overlay 专用
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

	// GridPanel 专用
	UPROPERTY()
	int32 Row = 0;

	UPROPERTY()
	int32 Column = 0;

	UPROPERTY()
	int32 RowSpan = 1;

	UPROPERTY()
	int32 ColumnSpan = 1;
};

/**
 * 类型化文本样式属性（FontSize, LetterSpacing 等文本专属字段）
 * WrapWithBorder 时不随视觉属性转移，留在的 Widget  */
struct FUMGTextStyleData
{
	float FontSize = 0;              // 0 = unset
	float SourceFontSize = 0;        // Original design font size before px->UMG scaling
	float LetterSpacing = 0;
	bool bHasLetterSpacing = false;  // 区分"显式设置为"未设
};

struct FUMGWidgetStyleData
{
	float WidthOverride = 0.0f;
	bool bHasWidthOverride = false;

	float HeightOverride = 0.0f;
	bool bHasHeightOverride = false;

	float MinDesiredWidth = 0.0f;
	bool bHasMinDesiredWidth = false;

	float MinDesiredHeight = 0.0f;
	bool bHasMinDesiredHeight = false;

	FMargin Padding = FMargin(0.0f);
	bool bHasPadding = false;

	FVector2D InnerSlotPadding = FVector2D::ZeroVector;
	bool bHasInnerSlotPadding = false;

	FMargin SlotPadding = FMargin(0.0f);
	bool bHasSlotPadding = false;

	FMargin StrokePadding = FMargin(0.0f);
	bool bHasStrokePadding = false;

	FMargin StrokeMargin = FMargin(0.0f);
	bool bHasStrokeMargin = false;

	FString ImageMode;
	bool bHasImageMode = false;

	FString ImageTransform;
	bool bHasImageTransform = false;

	FString Stretch;
	bool bHasStretch = false;

	FString StretchDirection;
	bool bHasStretchDirection = false;

	FLinearColor BackgroundColor = FLinearColor::Transparent;
	bool bHasBackgroundColor = false;

	FLinearColor HoveredColor = FLinearColor::Transparent;
	bool bHasHoveredColor = false;

	FLinearColor PressedColor = FLinearColor::Transparent;
	bool bHasPressedColor = false;

	FString NamedSlot;
	bool bHasNamedSlot = false;

	FString SlotName;
	bool bHasSlotName = false;

	FString Visibility;
	bool bHasVisibility = false;

	FString ToolTipText;
	bool bHasToolTipText = false;

	bool bIsEnabled = true;
	bool bHasIsEnabled = false;

	bool HasAny() const
	{
		return bHasWidthOverride
			|| bHasHeightOverride
			|| bHasMinDesiredWidth
			|| bHasMinDesiredHeight
			|| bHasPadding
			|| bHasInnerSlotPadding
			|| bHasSlotPadding
			|| bHasStrokePadding
			|| bHasStrokeMargin
			|| bHasImageMode
			|| bHasImageTransform
			|| bHasStretch
			|| bHasStretchDirection
			|| bHasBackgroundColor
			|| bHasHoveredColor
			|| bHasPressedColor
			|| bHasNamedSlot
			|| bHasSlotName
			|| bHasVisibility
			|| bHasToolTipText
			|| bHasIsEnabled;
	}
};

struct FUMGTextData
{
	FString Text;
	bool bHasText = false;

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
 * 类型化视觉画刷属性（渐进式替代Properties 中的视觉相关 key * WrapWithBorder 时整体转移到 Border 节点
 */
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
	bool bHasDrawAs = false;         // 区分"显式NoDrawType"未设
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
 * 类型化特效属性（BackgroundBlur / Retainer effect 等）
 * 新接入的 effect contract 应优先落到这里，而不是继续写入 legacy Properties。
 */
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
			|| !EffectMaterial.IsEmpty()
			|| bHasTextureParameter
			|| !TextureParameter.IsEmpty()
			|| bHasEffectBlurUVStep
			|| bHasEffectColor
			|| bHasEffectOffset
			|| bHasRenderingPhase;
	}
};

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

	FVector2D RenderScale = FVector2D(1.0f, 1.0f);
	bool bHasRenderScale = false;

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
			|| bHasRenderScale
			|| bHasRenderTranslation
			|| bHasClipping;
	}
};

struct FUMGWidgetStateData
{
	int32 ActiveWidgetIndex = 0;
	bool bHasActiveWidgetIndex = false;

	bool HasAny() const
	{
		return bHasActiveWidgetIndex;
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

struct FUMGTypedProperty
{
	FString SerializedValue;
	bool bHasValue = false;
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
	void Add(const FString& Key, const FString& Value);
	void Add(const TCHAR* Key, const FString& Value);
	void Add(const TCHAR* Key, const TCHAR* Value);

	int32 Remove(const FString& Key);
	int32 Remove(const TCHAR* Key);
	int32 Num() const;
	void Empty();

	// Typed property accessors used by production code.
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

USTRUCT()
struct FUMGPrototypeTransitionData
{
	GENERATED_BODY()

	UPROPERTY()
	FString Type;

	UPROPERTY()
	FString Direction;

	UPROPERTY()
	float DurationSeconds = 0.0f;

	UPROPERTY()
	FString EasingType;

	UPROPERTY()
	bool bMatchLayers = false;

	UPROPERTY()
	bool bHasMatchLayers = false;

	bool IsAnimated() const
	{
		return DurationSeconds > KINDA_SMALL_NUMBER;
	}

	bool UsesFadeOnly() const
	{
		return Type.IsEmpty()
			|| Type.Equals(TEXT("DISSOLVE"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("SMART_ANIMATE"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("SCROLL_ANIMATE"), ESearchCase::IgnoreCase);
	}

	bool UsesIncomingSlideOnly() const
	{
		return Type.Equals(TEXT("MOVE_IN"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("SLIDE_IN"), ESearchCase::IgnoreCase);
	}

	bool UsesOutgoingSlideOnly() const
	{
		return Type.Equals(TEXT("MOVE_OUT"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("SLIDE_OUT"), ESearchCase::IgnoreCase);
	}

	bool UsesBidirectionalSlide() const
	{
		return Type.Equals(TEXT("PUSH"), ESearchCase::IgnoreCase);
	}

	bool UsesSlide() const
	{
		return UsesIncomingSlideOnly() || UsesOutgoingSlideOnly() || UsesBidirectionalSlide();
	}
};

USTRUCT()
struct FUMGPrototypeNavigationBinding
{
	GENERATED_BODY()

	UPROPERTY()
	FString TriggerWidgetId;

	UPROPERTY()
	FString TriggerWidgetName;

	UPROPERTY()
	FString TargetPageId;

	UPROPERTY()
	int32 TargetPageIndex = INDEX_NONE;

	UPROPERTY()
	FUMGPrototypeTransitionData Transition;

	bool IsValid() const
	{
		return !TriggerWidgetId.IsEmpty()
			&& !TriggerWidgetName.IsEmpty()
			&& !TargetPageId.IsEmpty()
			&& TargetPageIndex >= 0;
	}
};

USTRUCT()
struct FUMGPrototypeNavigationData
{
	GENERATED_BODY()

	UPROPERTY()
	FString SwitcherWidgetId;

	UPROPERTY()
	FString SwitcherWidgetName;

	UPROPERTY()
	int32 InitialPageIndex = 0;

	UPROPERTY()
	TArray<FString> PageIds;

	UPROPERTY()
	TArray<FUMGPrototypeNavigationBinding> ClickBindings;

	bool IsValid() const
	{
		return !SwitcherWidgetId.IsEmpty()
			&& !SwitcherWidgetName.IsEmpty()
			&& PageIds.Num() > 1
			&& ClickBindings.Num() > 0;
	}
};

/**
 * Widget 节点数据结构 - 描述单个控件（v2.4更新） */
USTRUCT()
struct FUMGWidgetNode
{
	GENERATED_BODY()

	/** 控件类型: CanvasPanel, VerticalBox, HorizontalBox, Button, TextBlock, Image, etc. */
	UPROPERTY()
	FString Type;

	/** 稳定唯一标识，路径式：{ParentId}/{Type}_{Index}（v2.2新增）*/
	UPROPERTY()
	FString Id;

	/** 控件名称 */
	UPROPERTY()
	FString Name;

	/** 像素坐标Bounds（由LayoutCalculator从Slot计算）*/
	FBounds Bounds;

	/** 布局槽数据（由LayoutCalculator从Bounds计算）*/
	UPROPERTY()
	FUMGSlotData Slot;

	/** 
	 * 属性键值对（legacy，逐步Brush 替代
	 * @note [TODO] 完全重构为强类型数据，消除字符串 Map 带来的类型安全和维护风险。	 *       为向后兼容当前版本保留，但新属性系统不应再添加到此 Map 中。	 */
	FUMGPropertyBag Properties;

	/** 类型化文本样式（v2.4 拆分自Brush）*/
	FUMGTextStyleData TextStyle;

	/** 类型化控件公共样式/包装层属性（v2.8 继续迁移自 Properties） */
	FUMGWidgetStyleData WidgetStyle;

	/** 类型化文本内容与行为（v2.8 继续迁移自 Properties） */
	FUMGTextData Text;

	/** 类型化视觉画刷（v2.4 拆分自Brush，WrapWithBorder 时转移） */
	FUMGVisualBrushData Brush;

	/** 类型化特效属性（v2.5 拆分自 Properties） */
	FUMGEffectData Effect;

	/** 类型化路径/向量数据（v2.8 继续迁移自 Properties） */
	FUMGPathData Path;

	/** 类型化阴影数据（v2.8 继续迁移自 Properties） */
	FUMGShadowData Shadow;

	/** 类型化渲染属性（v2.7 继续迁移自 Properties） */
	FUMGRenderStyleData Render;

	/** 类型化控件状态（v2.6 继续迁移自 Properties） */
	FUMGWidgetStateData WidgetState;

	/** 子控件(不可序列化，UHT 不支持递归 USTRUCT) */
	TArray<FUMGWidgetNode> Children;
};

/**
 * Widget 蓝图数据结构 - 描述整个 Widget Blueprint
 */
USTRUCT()
struct FUMGWidgetData
{
	GENERATED_BODY()

	/** 生成的 Widget Blueprint 名称 */
	UPROPERTY()
	FString WidgetName = TEXT("WBP_Generated");

	/** 根控件*/
	UPROPERTY()
	FUMGWidgetNode RootWidget;

	/** 转换过程中的降级警告（如 Ellipse→Border）*/
	TArray<FString> Warnings;

	/** 转换阶段产生的图标字体映射建议，由 Editor 导入链决定是否持久化。 */
	FUMGIconFontMappingChanges IconFontMappingChanges;

	/** Figma prototype 最小导航闭环计划。 */
	FUMGPrototypeNavigationData PrototypeNavigation;
};

