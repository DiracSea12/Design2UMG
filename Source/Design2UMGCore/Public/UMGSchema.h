// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Styling/SlateBrush.h"
#include "UMGSchema.generated.h"

/**
 * Pixel-space bounds kept in UMGSchema to avoid cyclic dependencies.
 */
struct FBounds
{
	float X = 0.0f;  // Top-left X in pixels
	float Y = 0.0f;  // Top-left Y in pixels
	float W = 0.0f;  // Width in pixels
	float H = 0.0f;  // Height in pixels

	float GetRight() const { return X + W; }
	float GetBottom() const { return Y + H; }
	float GetCenterX() const { return X + W / 2.0f; }
	float GetCenterY() const { return Y + H / 2.0f; }
};

/**
 * Single anchor point.
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
 * Anchor range (Min/Max).
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
 * Offsets data.
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
 * Slot layout data describing placement inside the parent container.
 */
USTRUCT()
struct FUMGSlotData
{
	GENERATED_BODY()

	// CanvasPanel-only data
	UPROPERTY()
	FUMGAnchorsData Anchors;

	UPROPERTY()
	FUMGAnchorData Alignment;

	UPROPERTY()
	FUMGOffsetsData Offsets;

	// Shared slot fields
	UPROPERTY()
	FMargin Padding = FMargin(0.0f);

	// VerticalBox / HorizontalBox / Overlay
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

	// GridPanel-only data
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
 * Typed text style fields such as font size and letter spacing.
 * These stay on the text widget when WrapWithBorder transfers visual fields.
 */
struct FUMGTextStyleData
{
	float FontSize = 0;              // 0 = unset
	float SourceFontSize = 0;        // Original design font size before px->UMG scaling
	float LetterSpacing = 0;
	bool bHasLetterSpacing = false;  // Distinguishes explicit zero from unset
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

/**
 * Typed render transform and clipping fields for the generated widget node.
 */
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
	FString EasingJson;

	UPROPERTY()
	bool bMatchLayers = false;

	UPROPERTY()
	bool bHasMatchLayers = false;

	UPROPERTY()
	FString OutgoingAnimationName;

	UPROPERTY()
	FString IncomingAnimationName;

	bool IsAnimated() const
	{
		return DurationSeconds > KINDA_SMALL_NUMBER;
	}

	bool HasGeneratedAnimations() const
	{
		return !OutgoingAnimationName.IsEmpty() || !IncomingAnimationName.IsEmpty();
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

USTRUCT()
struct FUMGSourceNodeData
{
	GENERATED_BODY()

	UPROPERTY()
	FString SourceNodeId;

	UPROPERTY()
	FString SourceNodeName;

	UPROPERTY()
	FString SourceNodeType;

	UPROPERTY()
	FString ComponentId;

	UPROPERTY()
	FString ComponentSetId;

	UPROPERTY()
	FString MainComponentKey;

	UPROPERTY()
	FString ComponentPropertiesJson;

	UPROPERTY()
	FString VariantPropertiesJson;

	UPROPERTY()
	FString OverridesJson;

	UPROPERTY()
	bool bIsExposedInstance = false;

	UPROPERTY()
	bool bHasIsExposedInstance = false;

	bool IsInstance() const
	{
		return SourceNodeType.Equals(TEXT("INSTANCE"), ESearchCase::IgnoreCase)
			|| !ComponentId.IsEmpty()
			|| !ComponentPropertiesJson.IsEmpty()
			|| !VariantPropertiesJson.IsEmpty()
			|| !OverridesJson.IsEmpty();
	}

	bool IsComponentDefinition() const
	{
		return SourceNodeType.Equals(TEXT("COMPONENT"), ESearchCase::IgnoreCase);
	}

	bool IsComponentSet() const
	{
		return SourceNodeType.Equals(TEXT("COMPONENT_SET"), ESearchCase::IgnoreCase);
	}

	bool HasAnyComponentSemantics() const
	{
		return IsInstance() || IsComponentDefinition() || IsComponentSet();
	}
};

USTRUCT()
struct FUMGSourceDocumentData
{
	GENERATED_BODY()

	UPROPERTY()
	FString ComponentsJson;

	UPROPERTY()
	FString ComponentSetsJson;

	bool HasAnyComponentCatalog() const
	{
		return !ComponentsJson.IsEmpty() || !ComponentSetsJson.IsEmpty();
	}
};

UENUM()
enum class EUMGPrototypeNavigationAction : uint8
{
	Navigate,
	Overlay,
	Back,
	OpenLink,
	SetVariable,
	SetVariableMode,
	Conditional
};

UENUM()
enum class EUMGPrototypeNavigationStepKind : uint8
{
	Navigate,
	Overlay,
	Back,
	OpenLink,
	SetVariable,
	SetVariableMode,
	ConditionJump,
	Jump
};

USTRUCT()
struct FUMGPrototypeNavigationStep
{
	GENERATED_BODY()

	UPROPERTY()
	EUMGPrototypeNavigationStepKind Kind = EUMGPrototypeNavigationStepKind::Navigate;

	UPROPERTY()
	FString TargetPageId;

	UPROPERTY()
	int32 TargetPageIndex = INDEX_NONE;

	UPROPERTY()
	FString ExternalUrl;

	UPROPERTY()
	FUMGPrototypeTransitionData Transition;

	UPROPERTY()
	FString VariableId;

	UPROPERTY()
	FString VariableValueJson;

	UPROPERTY()
	FString VariableCollectionId;

	UPROPERTY()
	FString VariableModeId;

	UPROPERTY()
	FString ConditionJson;

	UPROPERTY()
	int32 JumpTargetIndex = INDEX_NONE;

	bool RequiresPageTarget() const
	{
		return Kind == EUMGPrototypeNavigationStepKind::Navigate
			|| Kind == EUMGPrototypeNavigationStepKind::Overlay;
	}

	bool RequiresSwitcher() const
	{
		return Kind == EUMGPrototypeNavigationStepKind::Navigate
			|| Kind == EUMGPrototypeNavigationStepKind::Overlay
			|| Kind == EUMGPrototypeNavigationStepKind::Back;
	}

	bool IsOverlayAction() const
	{
		return Kind == EUMGPrototypeNavigationStepKind::Overlay;
	}

	bool IsTerminalAction() const
	{
		return Kind == EUMGPrototypeNavigationStepKind::Navigate
			|| Kind == EUMGPrototypeNavigationStepKind::Overlay
			|| Kind == EUMGPrototypeNavigationStepKind::Back
			|| Kind == EUMGPrototypeNavigationStepKind::OpenLink;
	}

	bool IsValid() const
	{
		switch (Kind)
		{
		case EUMGPrototypeNavigationStepKind::Navigate:
		case EUMGPrototypeNavigationStepKind::Overlay:
			return !TargetPageId.IsEmpty() && TargetPageIndex >= 0;
		case EUMGPrototypeNavigationStepKind::Back:
			return true;
		case EUMGPrototypeNavigationStepKind::OpenLink:
			return !ExternalUrl.IsEmpty();
		case EUMGPrototypeNavigationStepKind::SetVariable:
			return !VariableId.IsEmpty() && !VariableValueJson.IsEmpty();
		case EUMGPrototypeNavigationStepKind::SetVariableMode:
			return !VariableCollectionId.IsEmpty() && !VariableModeId.IsEmpty();
		case EUMGPrototypeNavigationStepKind::ConditionJump:
		case EUMGPrototypeNavigationStepKind::Jump:
			return JumpTargetIndex >= 0;
		default:
			return false;
		}
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
	FString TriggerType;

	UPROPERTY()
	FString TriggerJson;

	UPROPERTY()
	TArray<FString> TriggerKeyCodes;

	UPROPERTY()
	FString TargetPageId;

	UPROPERTY()
	int32 TargetPageIndex = INDEX_NONE;

	UPROPERTY()
	EUMGPrototypeNavigationAction Action = EUMGPrototypeNavigationAction::Navigate;

	UPROPERTY()
	FString ExternalUrl;

	UPROPERTY()
	FUMGPrototypeTransitionData Transition;

	UPROPERTY()
	TArray<FUMGPrototypeNavigationStep> Steps;

	static bool IsClickTriggerType(const FString& InTriggerType)
	{
		return InTriggerType.IsEmpty()
			|| InTriggerType.Equals(TEXT("ON_CLICK"), ESearchCase::IgnoreCase);
	}

	static bool IsHoverTriggerType(const FString& InTriggerType)
	{
		return InTriggerType.Equals(TEXT("ON_HOVER"), ESearchCase::IgnoreCase);
	}

	static bool IsPressTriggerType(const FString& InTriggerType)
	{
		return InTriggerType.Equals(TEXT("ON_PRESS"), ESearchCase::IgnoreCase);
	}

	static bool IsDragTriggerType(const FString& InTriggerType)
	{
		return InTriggerType.Equals(TEXT("ON_DRAG"), ESearchCase::IgnoreCase);
	}

	static bool IsKeyDownTriggerType(const FString& InTriggerType)
	{
		return InTriggerType.Equals(TEXT("ON_KEY_DOWN"), ESearchCase::IgnoreCase);
	}

	static bool IsKeyUpTriggerType(const FString& InTriggerType)
	{
		return InTriggerType.Equals(TEXT("ON_KEY_UP"), ESearchCase::IgnoreCase);
	}

	bool UsesClickTrigger() const
	{
		return IsClickTriggerType(TriggerType);
	}

	bool UsesHoverTrigger() const
	{
		return IsHoverTriggerType(TriggerType);
	}

	bool UsesPressTrigger() const
	{
		return IsPressTriggerType(TriggerType);
	}

	bool UsesDragTrigger() const
	{
		return IsDragTriggerType(TriggerType);
	}

	bool UsesKeyDownTrigger() const
	{
		return IsKeyDownTriggerType(TriggerType);
	}

	bool UsesKeyUpTrigger() const
	{
		return IsKeyUpTriggerType(TriggerType);
	}

	bool UsesKeyboardTrigger() const
	{
		return UsesKeyDownTrigger() || UsesKeyUpTrigger();
	}

	bool RequiresPageTarget() const
	{
		if (Steps.Num() > 0)
		{
			for (const FUMGPrototypeNavigationStep& Step : Steps)
			{
				if (Step.RequiresPageTarget())
				{
					return true;
				}
			}
			return false;
		}

		return Action == EUMGPrototypeNavigationAction::Navigate
			|| Action == EUMGPrototypeNavigationAction::Overlay;
	}

	bool RequiresSwitcher() const
	{
		if (Steps.Num() > 0)
		{
			for (const FUMGPrototypeNavigationStep& Step : Steps)
			{
				if (Step.RequiresSwitcher())
				{
					return true;
				}
			}
			return false;
		}

		return Action != EUMGPrototypeNavigationAction::OpenLink;
	}

	bool HasOverlayAction() const
	{
		if (Steps.Num() > 0)
		{
			for (const FUMGPrototypeNavigationStep& Step : Steps)
			{
				if (Step.IsOverlayAction())
				{
					return true;
				}
			}
			return false;
		}

		return Action == EUMGPrototypeNavigationAction::Overlay;
	}

	bool IsValid() const
	{
		if (TriggerWidgetId.IsEmpty() || TriggerWidgetName.IsEmpty())
		{
			return false;
		}

		if (!UsesClickTrigger()
			&& !UsesHoverTrigger()
			&& !UsesPressTrigger()
			&& !UsesDragTrigger()
			&& !UsesKeyDownTrigger()
			&& !UsesKeyUpTrigger())
		{
			return false;
		}

		if (UsesKeyboardTrigger() && TriggerKeyCodes.Num() == 0)
		{
			return false;
		}

		if (Steps.Num() > 0)
		{
			for (const FUMGPrototypeNavigationStep& Step : Steps)
			{
				if (!Step.IsValid())
				{
					return false;
				}
			}
			return true;
		}

		switch (Action)
		{
		case EUMGPrototypeNavigationAction::Navigate:
		case EUMGPrototypeNavigationAction::Overlay:
			return !TargetPageId.IsEmpty() && TargetPageIndex >= 0;
		case EUMGPrototypeNavigationAction::Back:
			return true;
		case EUMGPrototypeNavigationAction::OpenLink:
			return !ExternalUrl.IsEmpty();
		case EUMGPrototypeNavigationAction::SetVariable:
		case EUMGPrototypeNavigationAction::SetVariableMode:
		case EUMGPrototypeNavigationAction::Conditional:
			return false;
		default:
			return false;
		}
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
	FString OverlayHostWidgetId;

	UPROPERTY()
	FString OverlayHostWidgetName;

	UPROPERTY()
	int32 InitialPageIndex = 0;

	UPROPERTY()
	TArray<FString> PageIds;

	UPROPERTY()
	TArray<FUMGPrototypeNavigationBinding> ClickBindings;

	bool HasOverlayBindings() const
	{
		for (const FUMGPrototypeNavigationBinding& Binding : ClickBindings)
		{
			if (Binding.HasOverlayAction())
			{
				return true;
			}
		}

		return false;
	}

	bool RequiresSwitcher() const
	{
		for (const FUMGPrototypeNavigationBinding& Binding : ClickBindings)
		{
			if (Binding.RequiresSwitcher())
			{
				return true;
			}
		}

		return false;
	}

	bool HasOverlayLayer() const
	{
		return !OverlayHostWidgetId.IsEmpty() && !OverlayHostWidgetName.IsEmpty();
	}

	bool IsValid() const
	{
		if (ClickBindings.Num() == 0)
		{
			return false;
		}

		for (const FUMGPrototypeNavigationBinding& Binding : ClickBindings)
		{
			if (!Binding.IsValid())
			{
				return false;
			}
		}

		if (!RequiresSwitcher())
		{
			return true;
		}

		const bool bHasValidSwitcher = !SwitcherWidgetId.IsEmpty()
			&& !SwitcherWidgetName.IsEmpty()
			&& PageIds.Num() > 1;
		if (!bHasValidSwitcher)
		{
			return false;
		}

		return !HasOverlayBindings() || HasOverlayLayer();
	}
};

/**
 * Generated widget node data for a single widget.
 */
USTRUCT()
struct FUMGWidgetNode
{
	GENERATED_BODY()

	/** Widget type: CanvasPanel, VerticalBox, HorizontalBox, Button, TextBlock, Image, etc. */
	UPROPERTY()
	FString Type;

	/** Stable path-like identifier: {ParentId}/{Type}_{Index}. */
	UPROPERTY()
	FString Id;

	/** Widget display name. */
	UPROPERTY()
	FString Name;

	/** Pixel-space bounds, projected by LayoutCalculator from slot data. */
	FBounds Bounds;

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

	/** Source component / variant / instance metadata when applicable. */
	UPROPERTY()
	FUMGSourceNodeData Source;

	/** Child widgets. Not serialized because recursive USTRUCT trees are unsupported by UHT. */
	TArray<FUMGWidgetNode> Children;
};

/**
 * Generated Widget Blueprint data.
 */
USTRUCT()
struct FUMGWidgetData
{
	GENERATED_BODY()

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

	/** Source-side component and variant metadata carried into generated output. */
	UPROPERTY()
	FUMGSourceDocumentData SourceDocument;

	/** Minimal prototype navigation plan derived from source data. */
	FUMGPrototypeNavigationData PrototypeNavigation;
};

