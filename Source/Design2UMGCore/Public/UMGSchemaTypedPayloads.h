// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignMeshGradient.h"
#include "DesignShaderData.h"
#include "DesignPathGeometry.h"
#include "Styling/SlateBrush.h"

/** Native browser settings and retained source viewport configuration. */
struct FUMGBrowserData
{
	FString InitialURL;
	FString DeviceId;
	TOptional<float> Zoom;
	TOptional<float> ScrollX;
	TOptional<float> ScrollY;
	// Pencil has no transparency field; absent leaves the native widget default.
	TOptional<bool> bSupportsTransparency;

	bool HasAny() const
	{
		return !InitialURL.IsEmpty() || !DeviceId.IsEmpty() || Zoom.IsSet()
			|| ScrollX.IsSet() || ScrollY.IsSet() || bSupportsTransparency.IsSet();
	}
};

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

struct FUMGRichTextStyleRowData
{
	FString FontFamily;
	FString TypefaceFontName;
	TOptional<float> FontSize;
	TOptional<float> LetterSpacing;
	TOptional<FLinearColor> Color;
	bool bUnderline = false;
	bool bStrikethrough = false;
	FString Href;

	bool operator==(const FUMGRichTextStyleRowData&) const = default;
};

struct FUMGTextData
{
	TOptional<FString> Text;
	TOptional<TMap<FString, FUMGRichTextStyleRowData>> RichTextStyleRows;
	TOptional<FString> HintText;
	TOptional<FString> FontFamily;
	TOptional<FString> TypefaceFontName;
	TOptional<FString> FontStyle;
	TOptional<FString> Justification;
	TOptional<bool> bAutoWrapText;
	TOptional<float> WrapTextAt;
	TOptional<float> LineHeightPercentage;
	// Render-space Y offset (px) compensating the Slate vs source half-leading
	// first-line baseline difference when an explicit line height is applied.
	TOptional<float> LineHeightBaselineShift;
	TOptional<bool> bUnderline;
	TOptional<bool> bStrikethrough;
	TOptional<FString> Href;
	TOptional<FString> TextAlignVertical;
	TOptional<FString> IconName;
	TOptional<FLinearColor> Color;

	bool HasAny() const
	{
		return Text.IsSet()
			|| RichTextStyleRows.IsSet()
			|| HintText.IsSet()
			|| FontFamily.IsSet()
			|| TypefaceFontName.IsSet()
			|| FontStyle.IsSet()
			|| Justification.IsSet()
		|| bAutoWrapText.IsSet()
		|| WrapTextAt.IsSet()
		|| LineHeightPercentage.IsSet()
		|| LineHeightBaselineShift.IsSet()
		|| bUnderline.IsSet()
			|| bStrikethrough.IsSet()
			|| Href.IsSet()
			|| TextAlignVertical.IsSet()
			|| IconName.IsSet()
			|| Color.IsSet();
	}
};

/**
 * Typed visual brush fields consumed directly by widget/resource application.
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
		TOptional<FString> TexturePath;
		TOptional<FString> Mode;
		TOptional<FImageTransformData> Transform;
		TOptional<FString> CropFitMode;

		bool HasAny() const
		{
			return TexturePath.IsSet() || Mode.IsSet() || Transform.IsSet() || CropFitMode.IsSet();
		}
	};

	struct FGradientStopData
	{
		FLinearColor Color = FLinearColor::Transparent;
		float Position = 0.0f;
	};

	TOptional<FDesignShaderData> Shader;

	struct FGradientData
	{
		TOptional<FString> Type;
		TOptional<float> Rotation;
		TOptional<FVector2D> StartPoint;
		TOptional<FVector2D> EndPoint;
		TOptional<FVector2D> Center;
		TOptional<FVector2D> Size;
		// Packed columns (Ux, Uy, Vx, Vy), normalized to the current paint box.
		TOptional<FVector4> AngularBasis;

		void TransformAngularPaintSpace(const FVector2D& Scale, const FVector2D& Translation)
		{
			if (!AngularBasis.IsSet()) return;
			Center = Center.Get(FVector2D(0.5, 0.5)) * Scale + Translation;
			const FVector4 Basis = AngularBasis.GetValue();
			AngularBasis = FVector4(Basis.X * Scale.X, Basis.Y * Scale.Y, Basis.Z * Scale.X, Basis.W * Scale.Y);
		}

		TArray<FGradientStopData> Stops;
		TOptional<FDesignMeshGradient> Mesh;

		bool HasAny() const
		{
			return Type.IsSet() || Rotation.IsSet() || StartPoint.IsSet() || EndPoint.IsSet()
				|| Center.IsSet() || Size.IsSet() || AngularBasis.IsSet() || Stops.Num() > 0 || (Mesh.IsSet() && Mesh->IsValid());
		}
	};

	struct FShapeData
	{
		TOptional<FLinearColor> Color;
		TOptional<float> Thickness;
		// Radii are normalized fractions of the half-minimum shape extent.
		TOptional<float> CornerRadius;
		TOptional<FVector4> CornerRadii;
		TOptional<float> BlurPadding;
		TOptional<FLinearColor> StripeColor;
		TOptional<int32> StripeWidth;
		TOptional<int32> StripeGap;
		TOptional<int32> Inset;

		bool operator==(const FShapeData&) const = default;
	};

	TOptional<float> OutlineCornerRadius;
	TOptional<FVector4> CornerRadii;
	TOptional<float> OutlineThickness;
	TOptional<FMargin> Margin;
	TOptional<ESlateBrushDrawType::Type> DrawAs;
	TOptional<FLinearColor> BrushColor;
	TOptional<FLinearColor> OutlineColor;
	TOptional<FString> MaterialRef;
	// Explicit user material parameters; generated shapes and gradients stay typed.
	TOptional<FString> MaterialParams;
	TOptional<FShapeData> Shape;
	FImageData Image;
	FGradientData Gradient;
};

/**
 * Typed effect fields such as BackgroundBlur and Retainer effects.
 * New effect contracts should land here as explicit typed payload fields.
 */
enum class EUMGGeometryMaskShape : uint8
{
	Ellipse,
	RoundedRectangle
};

struct FUMGEffectData
{
	struct FRenderingPhaseData
	{
		int32 Phase = 0;
		int32 PhaseCount = 1;
	};

	TOptional<FDesignShaderData> Shader;
	TOptional<float> BlurStrength;
	TOptional<bool> bRetainRendering;
	TOptional<FString> EffectMaterial;
	TOptional<FString> TextureParameter;
	TOptional<FVector2D> EffectBlurUVStep;
	TOptional<FLinearColor> EffectColor;
	TOptional<FVector2D> EffectOffset;
	TOptional<FString> InnerShadowBlendMode;
	TOptional<EUMGGeometryMaskShape> GeometryMaskShape;
	TOptional<FVector2D> GeometryMaskSize;
	TOptional<FVector4> GeometryMaskCornerRadii;
	TOptional<FRenderingPhaseData> RenderingPhase;

	bool HasAny() const
	{
		return Shader.IsSet() || BlurStrength.IsSet()
			|| bRetainRendering.IsSet()
			|| EffectMaterial.IsSet()
			|| TextureParameter.IsSet()
			|| EffectBlurUVStep.IsSet()
			|| EffectColor.IsSet()
			|| EffectOffset.IsSet()
			|| InnerShadowBlendMode.IsSet()
			|| GeometryMaskShape.IsSet()
			|| GeometryMaskSize.IsSet()
			|| GeometryMaskCornerRadii.IsSet()
			|| RenderingPhase.IsSet();
	}
};

// One static paint; object opacity belongs to the owning native widget.
struct FUMGSurfacePaint
{
	FLinearColor Color = FLinearColor::White;
	FUMGVisualBrushData::FGradientData Gradient;
	FUMGVisualBrushData::FImageData Image;

	void WriteSignature(FArchive& Archive) const
	{
		auto Tint = Color;
		auto G = Gradient;
		auto I = Image;
		Archive << Tint << G.Type << G.Rotation << G.StartPoint << G.EndPoint << G.Center << G.Size << G.AngularBasis;
		bool HasMesh = G.Mesh.IsSet();
		Archive << HasMesh;
		if (HasMesh) G.Mesh->WriteSignature(Archive);
		int32 Count = G.Stops.Num();
		Archive << Count;
		for (auto& Stop : G.Stops) Archive << Stop.Position << Stop.Color;
		Archive << I.TexturePath << I.Mode << I.CropFitMode;
		bool HasTransform = I.Transform.IsSet();
		Archive << HasTransform;
		if (HasTransform)
		{
			auto T = I.Transform.GetValue();
			Archive << T.A << T.B << T.TX << T.C << T.D << T.TY;
		}
	}
};

struct FUMGSourcePaintGeometry
{
	FDesignPaintGeometry Geometry;
	FString StrokeAlignment = TEXT("center");
	TOptional<TArray<FUMGSurfacePaint>> FillPaints;
	TOptional<FUMGSurfacePaint> StrokePaint;

	friend FArchive& operator<<(FArchive& Archive, FUMGSourcePaintGeometry& Source)
	{
		Archive << Source.Geometry << Source.StrokeAlignment;
		bool HasPaints = Source.FillPaints.IsSet();
		Archive << HasPaints;
		if (HasPaints)
		{
			int32 Count = Source.FillPaints->Num();
			Archive << Count;
			for (const auto& Paint : Source.FillPaints.GetValue()) Paint.WriteSignature(Archive);
		}
		bool HasStroke = Source.StrokePaint.IsSet();
		Archive << HasStroke;
		if (HasStroke) Source.StrokePaint->WriteSignature(Archive);
		return Archive;
	}
};

struct FUMGPathData
{
	// Drawing canvas can include ink outside the native widget's layout box.
	TOptional<FVector2D> CanvasSize;
	TOptional<FString> Geometry;
	// Source regions share local coordinates and retain their independent fill rules.
	TOptional<FUMGSourcePaintGeometry> SourcePaintGeometry;
	TOptional<FVector4> ViewBox;
	TOptional<bool> FitGeometryToBounds;
	// With source context and no Geometry, this is a filled region, not a stroke centerline.
	TOptional<FString> StrokeGeometry;
	FUMGVisualBrushData::FGradientData StrokeGradient;
	TOptional<FString> FillRule;
	TOptional<FLinearColor> FillColor;
	TOptional<FLinearColor> StrokeColor;
	TOptional<float> StrokeThickness;
	TOptional<FString> StrokeCap;
	TOptional<FString> StrokeJoin;
	TOptional<FString> StrokeAlignment;

	bool HasPaintGeometry() const
	{
		return Geometry.IsSet() || (SourcePaintGeometry.IsSet() && StrokeGeometry.IsSet());
	}

	bool HasAny() const
	{
		return CanvasSize.IsSet()
			|| Geometry.IsSet()
			|| SourcePaintGeometry.IsSet()
			|| ViewBox.IsSet()
			|| FitGeometryToBounds.IsSet()
			|| StrokeGeometry.IsSet()
			|| StrokeGradient.HasAny()
			|| FillRule.IsSet()
			|| FillColor.IsSet()
			|| StrokeColor.IsSet()
			|| StrokeThickness.IsSet()
			|| StrokeCap.IsSet()
			|| StrokeJoin.IsSet()
			|| StrokeAlignment.IsSet();
	}
};

struct FUMGBoxShadowLayerData
{
	FString BlendMode;
	FVector2D Offset = FVector2D::ZeroVector;
	float Blur = 0.0f;
	float Spread = 0.0f;
	FLinearColor Color = FLinearColor::Transparent;
	bool bShowShadowBehindNode = false;
};

struct FUMGShadowData
{
	TOptional<FVector2D> ShadowOffset;
	TOptional<FLinearColor> ShadowColorAndOpacity;
	TOptional<float> ShadowBlur;

	TArray<FUMGBoxShadowLayerData> BoxShadows;

	bool HasAny() const
	{
		return ShadowOffset.IsSet()
			|| ShadowColorAndOpacity.IsSet()
			|| ShadowBlur.IsSet()
			|| BoxShadows.Num() > 0;
	}
};

/**
 * Typed render transform and clipping fields for the generated widget node.
 */
struct FUMGRenderStyleData
{
	TOptional<FString> PaintBlendMode;
	// Logical pixel outward capture extent, only for an actual background prefix producer.
	TOptional<FMargin> BackgroundCaptureInsets;
	TOptional<float> RenderOpacity;
	TOptional<float> RenderAngle;
	TOptional<FVector2D> RenderTransformScale;
	TOptional<FVector2D> RenderTransformPivot;
	TOptional<FVector2D> RenderTranslation;
	TOptional<FString> Clipping;

	bool HasAny() const
	{
		return BackgroundCaptureInsets.IsSet()
			|| PaintBlendMode.IsSet()
			|| RenderOpacity.IsSet()
			|| RenderAngle.IsSet()
			|| RenderTransformScale.IsSet()
			|| RenderTransformPivot.IsSet()
			|| RenderTranslation.IsSet()
			|| Clipping.IsSet();
	}
};

struct FUMGWidgetStateData
{
	TOptional<int32> ActiveWidgetIndex;

	bool HasAny() const
	{
		return ActiveWidgetIndex.IsSet();
	}
};

struct FUMGScrollBoxData
{
	TOptional<FString> Orientation;
	TOptional<FString> ScrollBarVisibility;

	bool HasAny() const
	{
		return Orientation.IsSet() || ScrollBarVisibility.IsSet();
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
