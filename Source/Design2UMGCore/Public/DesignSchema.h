// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "DesignSchema.generated.h"

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

	static FDesignSize Fixed(float InValue) { return {EDesignSizeMode::Fixed, InValue}; }
	static FDesignSize Fill(float Fallback = 0.0f) { return {EDesignSizeMode::FillContainer, Fallback}; }
	static FDesignSize Fit(float Min = 0.0f) { return {EDesignSizeMode::FitContent, Min}; }

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

	/** Original variable reference name, resolved again during theme switching. */
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
	float ScalingFactor = 1.0f;

	UPROPERTY()
	bool bHasScalingFactor = false;

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

	/** Original variable reference name (for example "$--background"), resolved again during theme switching. */
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
	bool bExplicitClear = false;  // Explicit clear via fill:[]
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

	float GetUniform() const { return bUniform ? Uniform : 0.0f; }
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
	FDesignFill Fill;

	bool bHasExplicitFill = false;

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

	bool IsZero() const { return Top == 0 && Right == 0 && Bottom == 0 && Left == 0; }
	FMargin ToMargin() const { return FMargin(Left, Top, Right, Bottom); }
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

/**
 * Source-specific layout metadata that stays attached to parser-owned node state.
 * Non-UPROPERTY `bHas*` fields are intentional transient presence markers and are
 * not meant to participate in reflected serialization.
 */
USTRUCT()
struct FDesignLayoutExtensions
{
	GENERATED_BODY()

	UPROPERTY()
	FString LayoutWrap;

	UPROPERTY()
	FString CounterAxisAlignContent;

	UPROPERTY()
	FString LayoutPositioning;

	UPROPERTY()
	FString LayoutAlign;

	UPROPERTY()
	FString PrimaryAxisSizingMode;

	UPROPERTY()
	FString CounterAxisSizingMode;

	UPROPERTY()
	FString GridRowsSizing;

	UPROPERTY()
	FString GridColumnsSizing;

	UPROPERTY()
	FString GridChildHorizontalAlign;

	UPROPERTY()
	FString GridChildVerticalAlign;

	UPROPERTY()
	FString BoundVariablesJson;

	UPROPERTY()
	float CounterAxisSpacing = 0.0f;

	bool bHasCounterAxisSpacing = false;

	UPROPERTY()
	float MinWidth = 0.0f;

	bool bHasMinWidth = false;

	UPROPERTY()
	float MaxWidth = 0.0f;

	bool bHasMaxWidth = false;

	UPROPERTY()
	float MinHeight = 0.0f;

	bool bHasMinHeight = false;

	UPROPERTY()
	float MaxHeight = 0.0f;

	bool bHasMaxHeight = false;

	UPROPERTY()
	int32 GridRowCount = 0;

	bool bHasGridRowCount = false;

	UPROPERTY()
	int32 GridColumnCount = 0;

	bool bHasGridColumnCount = false;

	UPROPERTY()
	float GridRowGap = 0.0f;

	bool bHasGridRowGap = false;

	UPROPERTY()
	float GridColumnGap = 0.0f;

	bool bHasGridColumnGap = false;

	UPROPERTY()
	int32 GridRowSpan = 0;

	bool bHasGridRowSpan = false;

	UPROPERTY()
	int32 GridColumnSpan = 0;

	bool bHasGridColumnSpan = false;

	UPROPERTY()
	int32 GridColumnAnchorIndex = 0;

	bool bHasGridColumnAnchorIndex = false;

	UPROPERTY()
	int32 GridRowAnchorIndex = 0;

	bool bHasGridRowAnchorIndex = false;

	UPROPERTY()
	bool bItemReverseZIndex = false;

	bool bHasItemReverseZIndex = false;

	UPROPERTY()
	bool bStrokesIncludedInLayout = false;

	bool bHasStrokesIncludedInLayout = false;
};

USTRUCT()
struct FDesignComponentInstanceData
{
	GENERATED_BODY()

	UPROPERTY() bool bIsInstance = false;
	UPROPERTY() bool bInInstanceSubtree = false;
	UPROPERTY() FString ComponentId;
	UPROPERTY() FString ComponentSetId;
	UPROPERTY() FString MainComponentKey;
	UPROPERTY() FString ComponentPropertiesJson;
	UPROPERTY() FString VariantPropertiesJson;
	UPROPERTY() FString OverridesJson;
	UPROPERTY()
	bool bIsExposedInstance = false;

	bool bHasIsExposedInstance = false;
};

/**
 * Prototype transition payload parsed from source design tools.
 */
USTRUCT()
struct FDesignPrototypeTransitionData
{
	GENERATED_BODY()

	UPROPERTY() FString Type;
	UPROPERTY() FString Direction;
	UPROPERTY() float Duration = 0.0f;
	bool bHasDuration = false;
	UPROPERTY() FString EasingType;
	UPROPERTY() FString EasingJson;
	UPROPERTY() bool bMatchLayers = false;
	bool bHasMatchLayers = false;

	bool HasAny() const
	{
		return !Type.IsEmpty()
			|| !Direction.IsEmpty()
			|| bHasDuration
			|| !EasingType.IsEmpty()
			|| !EasingJson.IsEmpty()
			|| bHasMatchLayers;
	}
};

USTRUCT()
struct FDesignPrototypeAction
{
	GENERATED_BODY()

	UPROPERTY() FString Type;
	UPROPERTY() FString DestinationId;
	UPROPERTY() FString Navigation;
	UPROPERTY() FString TransitionNodeId;
	UPROPERTY() FString TransitionJson;
	UPROPERTY() FString RawJson;
	UPROPERTY() FDesignPrototypeTransitionData Transition;
};

USTRUCT()
struct FDesignPrototypeInteraction
{
	GENERATED_BODY()

	UPROPERTY() FString TriggerType;
	UPROPERTY() FString TriggerJson;
	UPROPERTY() FString RawJson;
	UPROPERTY() TArray<FDesignPrototypeAction> Actions;
};

USTRUCT()
struct FDesignFlowStartingPoint
{
	GENERATED_BODY()

	UPROPERTY() FString NodeId;
	UPROPERTY() FString Name;
};

USTRUCT()
struct FDesignPrototypeNodeData
{
	GENERATED_BODY()

	UPROPERTY() FString OverflowDirection;
	UPROPERTY() FString TransitionNodeId;
	UPROPERTY() FDesignPrototypeTransitionData Transition;
	UPROPERTY() TArray<FDesignPrototypeInteraction> Interactions;
	UPROPERTY() TArray<FDesignFlowStartingPoint> FlowStartingPoints;
};

USTRUCT()
struct FDesignSourceDocumentData
{
	GENERATED_BODY()

	UPROPERTY() FString ComponentsJson;
	UPROPERTY() FString ComponentSetsJson;
	UPROPERTY() FString PrototypeStartNodeId;
	UPROPERTY() FString PrototypeDeviceJson;
	UPROPERTY() TArray<FDesignFlowStartingPoint> FlowStartingPoints;
};

USTRUCT()
struct FDesignRenderFallbackData
{
	GENERATED_BODY()

	UPROPERTY() FString ImageRef;
	UPROPERTY() FString Scope;
	UPROPERTY() FString Source;
	UPROPERTY() FString Reason;
	UPROPERTY() FString OffsetX;
	UPROPERTY() FString OffsetY;
	UPROPERTY() FString Width;
	UPROPERTY() FString Height;

	bool HasAny() const
	{
		return !ImageRef.IsEmpty()
			|| !Scope.IsEmpty()
			|| !Source.IsEmpty()
			|| !Reason.IsEmpty()
			|| !OffsetX.IsEmpty()
			|| !OffsetY.IsEmpty()
			|| !Width.IsEmpty()
			|| !Height.IsEmpty();
	}
};

USTRUCT()
struct FDesignSkippedNodeReport
{
	GENERATED_BODY()

	UPROPERTY() FString NodeId;
	UPROPERTY() FString NodeName;
	UPROPERTY() FString NodeType;
	UPROPERTY() FString Reason;
};

USTRUCT()
struct FDesignParseDiagnostics
{
	GENERATED_BODY()

	UPROPERTY() TArray<FDesignSkippedNodeReport> SkippedDesignerNodes;
};

/**
 * Source-only metadata carried alongside the shared design node.
 */
struct FDesignSourceNodeData
{
	FDesignLayoutExtensions LayoutExtensions;
	FDesignComponentInstanceData ComponentInstance;
	FDesignPrototypeNodeData Prototype;
	FString SourceNodeType;
	bool bExpandedRefInstance = false;
	FString VectorRasterFallbackImageRef;
	FDesignRenderFallbackData RenderFallback;
};

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
	float Gap = 0.0f;
	TOptional<float> ExplicitGap;
	FDesignPadding Padding;
	TOptional<FDesignPadding> ExplicitPadding;
	bool bHasPaddingRuntimeWrapDecision = false;
	bool bPaddingRequiresRuntimeWrap = false;
	EDesignJustifyContent JustifyContent = EDesignJustifyContent::Start;
	EDesignAlignItems AlignItems = EDesignAlignItems::Start;
	bool bAbsoluteLayoutPositioning = false;
	bool bClip = false;
	TOptional<bool> ExplicitClip;
	bool bSemanticPlaceholderSurface = false;
	float LayoutGrow = -1.0f;
	int32 LayoutRow = 0;
	int32 LayoutColumn = 0;
	int32 LayoutRowSpan = 1;
	int32 LayoutColumnSpan = 1;
	int32 LayoutRowCount = 0;
	int32 LayoutColumnCount = 0;
	float LayoutRowGap = 0.0f;
	float LayoutColumnGap = 0.0f;
	FString LayoutChildHorizontalAlign;
	FString LayoutChildVerticalAlign;
	bool bHasLayoutRow = false;
	bool bHasLayoutColumn = false;
	bool bHasLayoutRowSpan = false;
	bool bHasLayoutColumnSpan = false;
	bool bHasLayoutRowCount = false;
	bool bHasLayoutColumnCount = false;
	bool bHasLayoutRowGap = false;
	bool bHasLayoutColumnGap = false;
	bool bHasLayoutChildHorizontalAlign = false;
	bool bHasLayoutChildVerticalAlign = false;

	void SetGap(const float InGap)
	{
		Gap = InGap;
		ExplicitGap = InGap;
	}

	void SetPadding(const FDesignPadding& InPadding)
	{
		Padding = InPadding;
		ExplicitPadding = InPadding;
	}

	void SetClipValue(const bool bInClip)
	{
		bClip = bInClip;
		ExplicitClip = bInClip;
	}

	bool HasExplicitGap() const
	{
		return ExplicitGap.IsSet();
	}

	bool HasExplicitPadding() const
	{
		return ExplicitPadding.IsSet();
	}

	bool HasExplicitClip() const
	{
		return ExplicitClip.IsSet();
	}

	bool HasExplicitGridPosition() const
	{
		return bHasLayoutRow || bHasLayoutColumn;
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

	bool HasExplicitX() const
	{
		return ExplicitX.IsSet();
	}

	bool HasExplicitY() const
	{
		return ExplicitY.IsSet();
	}

	bool HasExplicitOpacity() const
	{
		return ExplicitOpacity.IsSet();
	}

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
struct FDesignNode
{
	FString Id;
	FString Name;
	EDesignNodeType Type = EDesignNodeType::Unknown;
	TMap<FString, FString> Theme;
	FDesignTransformNodeData Transform;
	TMap<FString, FString> Metadata;
	FDesignLayoutNodeData LayoutData;
	FDesignSourceNodeData Source;
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

/**
 * Root parsed document for a source design export.
 * This is also intentionally non-reflected for the same lightweight transient-IR reasons.
 */
struct FDesignDocument
{
	FString Version;
	TArray<FDesignFont> Fonts;
	TMap<FString, TArray<FString>> Themes;
	TMap<FString, FDesignVariable> Variables;
	TMap<FString, FString> Metadata;
	FDesignSourceDocumentData SourceDocument;
	FDesignParseDiagnostics ParseDiagnostics;
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




