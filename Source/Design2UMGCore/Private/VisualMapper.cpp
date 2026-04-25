// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapper.h"
#include "ImageFillUtils.h"
#include "LayoutMapper.h"
#include "VisualMapperFillTransfer.h"
#include "Design2UMGLog.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGValueParser.h"

namespace
{
	// 材质引用标识符（ProceduralShapeGenerator 内部 key，非资产路径）
	const TCHAR* RetainerForegroundBlurEffectRef = TEXT("d2u.retainer.foreground_blur");
	const TCHAR* RetainerInnerShadowEffectRef = TEXT("d2u.retainer.inner_shadow");
	const TCHAR* MatRef_RoundedRect = TEXT("roundedrect");

	FVector2D ComputeForegroundBlurUVStep(const TSharedPtr<FDesignNode>& Node, float BlurStrength)
	{
		if (!Node.IsValid())
		{
			return FVector2D(1.0f / 512.0f, 1.0f / 512.0f);
		}

		const float Width = FMath::Max(1.0f, Node->Transform.Width.Value);
		const float Height = FMath::Max(1.0f, Node->Transform.Height.Value);
		const float EffectiveBlur = FMath::Max(0.5f, BlurStrength) * 0.35f;
		return FVector2D(EffectiveBlur / Width, EffectiveBlur / Height);
	}

	FVector2D ComputeEffectUVOffset(const TSharedPtr<FDesignNode>& Node, float OffsetX, float OffsetY)
	{
		if (!Node.IsValid())
		{
			return FVector2D::ZeroVector;
		}

		const float Width = FMath::Max(1.0f, Node->Transform.Width.Value);
		const float Height = FMath::Max(1.0f, Node->Transform.Height.Value);
		return FVector2D(OffsetX / Width, OffsetY / Height);
	}

	namespace StrokeHelpers
	{
		enum class EStrokeSide : uint8
		{
			Top,
			Right,
			Bottom,
			Left
		};

		float GetStrokeSideThickness(const FDesignStrokeThickness& Thickness, const EStrokeSide Side)
		{
			if (Thickness.bUniform)
			{
				return Thickness.Uniform;
			}

			switch (Side)
			{
			case EStrokeSide::Top:
				return Thickness.Top;
			case EStrokeSide::Right:
				return Thickness.Right;
			case EStrokeSide::Bottom:
				return Thickness.Bottom;
			case EStrokeSide::Left:
				return Thickness.Left;
			default:
				return 0.0f;
			}
		}

		FMargin GetStrokeHalfInsets(const FDesignStrokeThickness& Thickness)
		{
			return FMargin(
				GetStrokeSideThickness(Thickness, EStrokeSide::Left) * 0.5f,
				GetStrokeSideThickness(Thickness, EStrokeSide::Top) * 0.5f,
				GetStrokeSideThickness(Thickness, EStrokeSide::Right) * 0.5f,
				GetStrokeSideThickness(Thickness, EStrokeSide::Bottom) * 0.5f);
		}

		bool HasUniformRectStroke(const FDesignStroke& Stroke)
		{
			const float Top = GetStrokeSideThickness(Stroke.Thickness, EStrokeSide::Top);
			const float Right = GetStrokeSideThickness(Stroke.Thickness, EStrokeSide::Right);
			const float Bottom = GetStrokeSideThickness(Stroke.Thickness, EStrokeSide::Bottom);
			const float Left = GetStrokeSideThickness(Stroke.Thickness, EStrokeSide::Left);
			return Top > KINDA_SMALL_NUMBER
				&& Right > KINDA_SMALL_NUMBER
				&& Bottom > KINDA_SMALL_NUMBER
				&& Left > KINDA_SMALL_NUMBER
				&& FMath::IsNearlyEqual(Top, Right)
				&& FMath::IsNearlyEqual(Top, Bottom)
				&& FMath::IsNearlyEqual(Top, Left);
		}

		bool NeedsExplicitSideStroke(const TSharedPtr<FDesignNode>& Node)
		{
			return Node.IsValid()
				&& Node->Visual.Stroke.HasStroke()
				&& !Node->Visual.CornerRadius.HasRadius()
				&& !HasUniformRectStroke(Node->Visual.Stroke);
		}
	}

	bool HasOpaqueSolidFill(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Node->Visual.Fill.bEnabled || Node->Visual.Fill.Type != EDesignFillType::Color || Node->Visual.Fill.SolidColor.IsEmpty())
		{
			return false;
		}

		return FUMGValueParser::ParseColor(Node->Visual.Fill.SolidColor).A > KINDA_SMALL_NUMBER;
	}

	bool HasPlaceholderSurfaceChildren(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			if (Child.IsValid() && Child->LayoutData.bSemanticPlaceholderSurface)
			{
				return true;
			}
		}

		return false;
	}

	bool HasVisualMapperWidgetStringProperty(const FUMGWidgetNode& Widget, const TCHAR* Key)
	{
		return Design2UMGWidgetNodeAccess::HasWidgetString(Widget, Key);
	}

	bool TryParseVisualMapperDrawAs(const FString& Value, ESlateBrushDrawType::Type& OutDrawAs)
	{
		static const TMap<FString, ESlateBrushDrawType::Type> DrawAsMap = {
			{ TEXT("NoDrawType"), ESlateBrushDrawType::NoDrawType },
			{ TEXT("Box"), ESlateBrushDrawType::Box },
			{ TEXT("Border"), ESlateBrushDrawType::Border },
			{ TEXT("Image"), ESlateBrushDrawType::Image },
			{ TEXT("RoundedBox"), ESlateBrushDrawType::RoundedBox },
		};

		if (const ESlateBrushDrawType::Type* Found = DrawAsMap.Find(Value))
		{
			OutDrawAs = *Found;
			return true;
		}

		return false;
	}

	namespace ExplicitSideStrokeHelpers
	{
		struct FExplicitStrokeSideSpec
		{
			StrokeHelpers::EStrokeSide Side = StrokeHelpers::EStrokeSide::Top;
			const TCHAR* Suffix = TEXT("");
			const TCHAR* HorizontalAlignment = TEXT("Fill");
			const TCHAR* VerticalAlignment = TEXT("Fill");
			bool bHorizontalLine = false;
		};

		const FExplicitStrokeSideSpec ExplicitStrokeSideSpecs[] = {
			{ StrokeHelpers::EStrokeSide::Top, TEXT("_stroke_top"), TEXT("Fill"), TEXT("Top"), true },
			{ StrokeHelpers::EStrokeSide::Right, TEXT("_stroke_right"), TEXT("Right"), TEXT("Fill"), false },
			{ StrokeHelpers::EStrokeSide::Bottom, TEXT("_stroke_bottom"), TEXT("Fill"), TEXT("Bottom"), true },
			{ StrokeHelpers::EStrokeSide::Left, TEXT("_stroke_left"), TEXT("Left"), TEXT("Fill"), false },
		};

		bool TryResolveStrokeColor(const FDesignStroke& Stroke, FLinearColor& OutColor)
		{
			if (Stroke.Fill.Type != EDesignFillType::Color || Stroke.Fill.SolidColor.IsEmpty())
			{
				return false;
			}

			OutColor = FUMGValueParser::ParseColor(Stroke.Fill.SolidColor);
			return true;
		}

		bool ContainsExplicitStrokeSuffixRecursive(const FUMGWidgetNode& Widget, const TCHAR* Suffix)
		{
			if (Widget.Name.EndsWith(Suffix))
			{
				return true;
			}

			for (const FUMGWidgetNode& Child : Widget.Children)
			{
				if (ContainsExplicitStrokeSuffixRecursive(Child, Suffix))
				{
					return true;
				}
			}

			return false;
		}

		bool HasConvertedChildStrokeCoverage(const FUMGWidgetNode& Border, const TCHAR* Suffix)
		{
			if (Border.Children.Num() == 0)
			{
				return false;
			}

			const FUMGWidgetNode& Content = Border.Children[0];
			if (Content.Children.Num() == 0)
			{
				return false;
			}

			for (const FUMGWidgetNode& Child : Content.Children)
			{
				if (!ContainsExplicitStrokeSuffixRecursive(Child, Suffix))
				{
					return false;
				}
			}

			return true;
		}

		bool HasAnyConvertedChildStrokeCoverage(const FUMGWidgetNode& Border)
		{
			for (const FExplicitStrokeSideSpec& SideSpec : ExplicitStrokeSideSpecs)
			{
				if (HasConvertedChildStrokeCoverage(Border, SideSpec.Suffix))
				{
					return true;
				}
			}

			return false;
		}

		void RemoveExplicitStrokeSuffixRecursive(FUMGWidgetNode& Widget, const TCHAR* Suffix)
		{
			for (int32 Index = Widget.Children.Num() - 1; Index >= 0; --Index)
			{
				if (Widget.Children[Index].Name.EndsWith(Suffix))
				{
					Widget.Children.RemoveAt(Index);
					continue;
				}

				RemoveExplicitStrokeSuffixRecursive(Widget.Children[Index], Suffix);
			}
		}

		void PruneConvertedChildStrokeCoverage(FUMGWidgetNode& Border, const TCHAR* Suffix)
		{
			if (Border.Children.Num() == 0)
			{
				return;
			}

			FUMGWidgetNode& Content = Border.Children[0];
			for (FUMGWidgetNode& Child : Content.Children)
			{
				RemoveExplicitStrokeSuffixRecursive(Child, Suffix);
			}
		}

		void ResetChildToFill(FUMGWidgetNode& Widget)
		{
			Widget.Bounds = FDesignPixelBounds();
			Widget.Slot = FUMGSlotData();
			Widget.Slot.HorizontalAlignment = TEXT("Fill");
			Widget.Slot.VerticalAlignment = TEXT("Fill");
		}

		void StripExplicitStrokeFromSurface(FUMGWidgetNode& BorderSurface)
		{
			BorderSurface.Brush.OutlineThickness = 0.0f;
			BorderSurface.Brush.bHasOutlineThickness = false;
			BorderSurface.Brush.bHasOutlineColor = false;
			BorderSurface.Brush.OutlineColor = FLinearColor::Transparent;
			BorderSurface.WidgetStyle.StrokePadding.Reset();
			BorderSurface.WidgetStyle.StrokeMargin.Reset();
		}

		FUMGWidgetNode MakeStrokeLineNode(
			const FString& Id,
			const FString& Name,
			const FLinearColor& Color,
			float Width,
			float Height,
			const FString& HorizontalAlignment,
			const FString& VerticalAlignment)
		{
			FUMGWidgetNode Line;
			Line.Type = TEXT("Image");
			Line.Id = Id;
			Line.Name = Name;
			Line.Brush.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
			Line.Brush.Image.TexturePath = Line.Brush.TexturePath;
			Line.Brush.Image.bHasTexturePath = true;
			Line.Brush.BrushColor = Color;
			Line.Brush.bHasBrushColor = true;
			Line.WidgetStyle.WidthOverride = FMath::Max(1.0f, Width);
			Line.WidgetStyle.HeightOverride = FMath::Max(1.0f, Height);
			Line.Slot.HorizontalAlignment = HorizontalAlignment;
			Line.Slot.VerticalAlignment = VerticalAlignment;
			return Line;
		}

		void AddExplicitStrokeLine(
			FUMGWidgetNode& Overlay,
			const FLinearColor& StrokeColor,
			float Width,
			float Height,
			const TCHAR* Suffix,
			const TCHAR* HorizontalAlignment,
			const TCHAR* VerticalAlignment)
		{
			if (Width <= KINDA_SMALL_NUMBER || Height <= KINDA_SMALL_NUMBER)
			{
				return;
			}

			Overlay.Children.Add(MakeStrokeLineNode(
				Overlay.Id + Suffix,
				Overlay.Name + Suffix,
				StrokeColor,
				Width,
				Height,
				HorizontalAlignment,
				VerticalAlignment));
		}

		FUMGWidgetNode WrapWithExplicitSideStroke(FUMGWidgetNode&& Border, const TSharedPtr<FDesignNode>& Node)
		{
			FLinearColor StrokeColor;
			if (!TryResolveStrokeColor(Node->Visual.Stroke, StrokeColor))
			{
				return MoveTemp(Border);
			}

			FUMGWidgetNode Overlay;
			Overlay.Type = TEXT("Overlay");
			Overlay.Id = Border.Id;
			Overlay.Name = Border.Name;
			Overlay.Bounds = Border.Bounds;
			Overlay.Slot = Border.Slot;
			Overlay.Render = Border.Render;
			Border.Render = FUMGRenderStyleData();

			if (Border.WidgetStyle.StrokePadding.IsSet())
			{
				Overlay.WidgetStyle.StrokePadding = Border.WidgetStyle.StrokePadding;
			}
			else
			{
				FMargin StrokePadding = FMargin(0.0f);
				if (Design2UMGWidgetNodeAccess::TryGetWidgetMargin(Border, UMGPropertyKeys::StrokePadding, StrokePadding))
				{
					Overlay.WidgetStyle.StrokePadding = StrokePadding;
				}
			}

			Border.Id += TEXT("_surface");
			Border.Name += TEXT("_surface");
			StripExplicitStrokeFromSurface(Border);

			if (!StrokeHelpers::HasUniformRectStroke(Node->Visual.Stroke))
			{
				for (const FExplicitStrokeSideSpec& SideSpec : ExplicitStrokeSideSpecs)
				{
					if (HasConvertedChildStrokeCoverage(Border, SideSpec.Suffix))
					{
						PruneConvertedChildStrokeCoverage(Border, SideSpec.Suffix);
					}
				}
			}

			ResetChildToFill(Border);
			Overlay.Children.Add(MoveTemp(Border));

			for (const FExplicitStrokeSideSpec& SideSpec : ExplicitStrokeSideSpecs)
			{
				const float Thickness = StrokeHelpers::GetStrokeSideThickness(Node->Visual.Stroke.Thickness, SideSpec.Side);
				const float Width = SideSpec.bHorizontalLine ? 1.0f : Thickness;
				const float Height = SideSpec.bHorizontalLine ? Thickness : 1.0f;
				AddExplicitStrokeLine(
					Overlay,
					StrokeColor,
					Width,
					Height,
					SideSpec.Suffix,
					SideSpec.HorizontalAlignment,
					SideSpec.VerticalAlignment);
			}

			return Overlay;
		}
	}

	void TransferBorderBackgroundProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		FString StringValue;
		if (!Border.Brush.bHasBrushColor
			&& Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::BrushColor, StringValue))
		{
			Border.Brush.BrushColor = FUMGValueParser::ParseColor(StringValue);
			Border.Brush.bHasBrushColor = true;
		}
		if (Border.Brush.TexturePath.IsEmpty())
		{
			Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::TexturePath, Border.Brush.TexturePath);
		}
		if (!Border.Brush.bHasMaterialRef && Border.Brush.MaterialRef.IsEmpty())
		{
			if (Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::MaterialRef, Border.Brush.MaterialRef))
			{
				Border.Brush.bHasMaterialRef = true;
			}
		}
		if (!Border.Brush.bHasMaterialParams && Border.Brush.MaterialParams.IsEmpty())
		{
			if (Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::MaterialParams, Border.Brush.MaterialParams))
			{
				Border.Brush.bHasMaterialParams = true;
			}
		}

		bool bHasBackgroundProperty = false;
		if (Widget.WidgetStyle.WidthOverride.IsSet())
		{
			Border.WidgetStyle.WidthOverride = Widget.WidgetStyle.WidthOverride;
			Widget.WidgetStyle.WidthOverride.Reset();
			bHasBackgroundProperty = true;
		}
		if (Widget.WidgetStyle.HeightOverride.IsSet())
		{
			Border.WidgetStyle.HeightOverride = Widget.WidgetStyle.HeightOverride;
			Widget.WidgetStyle.HeightOverride.Reset();
			bHasBackgroundProperty = true;
		}
		if (Widget.WidgetStyle.ImageMode.IsSet())
		{
			Border.WidgetStyle.ImageMode = Widget.WidgetStyle.ImageMode;
			Widget.WidgetStyle.ImageMode.Reset();
			bHasBackgroundProperty = true;
		}
		if (Widget.WidgetStyle.ImageTransform.IsSet())
		{
			Border.WidgetStyle.ImageTransform = Widget.WidgetStyle.ImageTransform;
			Widget.WidgetStyle.ImageTransform.Reset();
			bHasBackgroundProperty = true;
		}

		bHasBackgroundProperty = bHasBackgroundProperty
			|| Border.Brush.bHasBrushColor
			|| !Border.Brush.TexturePath.IsEmpty()
			|| Border.Brush.bHasMaterialRef
			|| !Border.Brush.MaterialRef.IsEmpty();

		if (!bHasBackgroundProperty)
		{
			if (!Border.Brush.bHasBrushColor)
			{
				Border.Brush.BrushColor = FLinearColor::Transparent;
				Border.Brush.bHasBrushColor = true;
			}
		}
	}

	void TransferBorderStrokeProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		FString StringValue;
		if (!Border.Brush.bHasDrawAs
			&& Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::DrawAs, StringValue))
		{
			ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType;
			if (TryParseVisualMapperDrawAs(StringValue, DrawAs))
			{
				Border.Brush.DrawAs = DrawAs;
				Border.Brush.bHasDrawAs = true;
			}
		}
		if (Border.Brush.OutlineCornerRadius < 0.0f)
		{
			float FloatValue = 0.0f;
			if (Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Widget, UMGPropertyKeys::OutlineCornerRadius, FloatValue))
			{
				Border.Brush.OutlineCornerRadius = FloatValue;
			}
		}
		if (!Border.Brush.HasCornerRadii())
		{
			FVector4 VectorValue;
			if (Design2UMGWidgetNodeAccess::TryGetWidgetVector4(Widget, UMGPropertyKeys::CornerRadii, VectorValue))
			{
				Border.Brush.CornerRadii = VectorValue;
			}
		}
		if (!Border.Brush.bHasOutlineThickness)
		{
			float FloatValue = 0.0f;
			if (Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Widget, UMGPropertyKeys::OutlineThickness, FloatValue))
			{
				Border.Brush.OutlineThickness = FloatValue;
				Border.Brush.bHasOutlineThickness = true;
			}
		}
		if (!Border.Brush.bHasOutlineColor
			&& Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::OutlineColor, StringValue))
		{
			Border.Brush.OutlineColor = FUMGValueParser::ParseColor(StringValue);
			Border.Brush.bHasOutlineColor = true;
		}

		if (Widget.WidgetStyle.StrokePadding.IsSet())
		{
			Border.WidgetStyle.StrokePadding = Widget.WidgetStyle.StrokePadding;
			Widget.WidgetStyle.StrokePadding.Reset();
		}
	}

	void ApplyOutsideStrokePaddingToBorder(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		FMargin Margin = FMargin(0.0f);
		bool bHasStrokeMargin = false;
		if (Widget.WidgetStyle.StrokeMargin.IsSet())
		{
			Margin = Widget.WidgetStyle.StrokeMargin.GetValue();
			bHasStrokeMargin = true;
		}
		else
		{
			bHasStrokeMargin = Design2UMGWidgetNodeAccess::TryGetWidgetMargin(Widget, UMGPropertyKeys::StrokeMargin, Margin);
		}

		if (!bHasStrokeMargin)
		{
			return;
		}

		const FMargin Outset(
			FMath::Max(0.0f, -Margin.Left),
			FMath::Max(0.0f, -Margin.Top),
			FMath::Max(0.0f, -Margin.Right),
			FMath::Max(0.0f, -Margin.Bottom));
		if (Outset.Left > 0.0f || Outset.Top > 0.0f || Outset.Right > 0.0f || Outset.Bottom > 0.0f)
		{
			FMargin Combined = Outset;
			if (Border.WidgetStyle.StrokePadding.IsSet())
			{
				const FMargin ExistingPadding = Border.WidgetStyle.StrokePadding.GetValue();
				Combined.Left += ExistingPadding.Left;
				Combined.Top += ExistingPadding.Top;
				Combined.Right += ExistingPadding.Right;
				Combined.Bottom += ExistingPadding.Bottom;
			}
			else
			{
				FMargin ExistingPadding = FMargin(0.0f);
				if (Design2UMGWidgetNodeAccess::TryGetWidgetMargin(Border, UMGPropertyKeys::StrokePadding, ExistingPadding))
				{
					Combined.Left += ExistingPadding.Left;
					Combined.Top += ExistingPadding.Top;
					Combined.Right += ExistingPadding.Right;
					Combined.Bottom += ExistingPadding.Bottom;
				}
			}

			Border.WidgetStyle.StrokePadding = Combined;
		}

		Widget.WidgetStyle.StrokeMargin.Reset();
	}

	void TransferBorderRenderProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		FString StringValue;
		float FloatValue = 0.0f;
		FVector2D Vector2DValue = FVector2D::ZeroVector;

		if (!Border.Render.bHasClipping
			&& Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::Clipping, StringValue)
			&& !StringValue.IsEmpty())
		{
			Border.Render.Clipping = StringValue;
			Border.Render.bHasClipping = true;
		}
		if (!Border.Render.bHasRenderOpacity
			&& Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Widget, UMGPropertyKeys::RenderOpacity, FloatValue))
		{
			Border.Render.RenderOpacity = FloatValue;
			Border.Render.bHasRenderOpacity = true;
		}
		if (!Border.Render.bHasRenderAngle
			&& Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Widget, UMGPropertyKeys::RenderAngle, FloatValue))
		{
			Border.Render.RenderAngle = FloatValue;
			Border.Render.bHasRenderAngle = true;
		}
		if (!Border.Render.bHasRenderTransformScale
			&& Design2UMGWidgetNodeAccess::TryGetWidgetVector2D(Widget, UMGPropertyKeys::RenderTransformScale, Vector2DValue))
		{
			Border.Render.RenderTransformScale = Vector2DValue;
			Border.Render.bHasRenderTransformScale = true;
		}
		if (!Border.Render.bHasRenderTranslation
			&& Design2UMGWidgetNodeAccess::TryGetWidgetVector2D(Widget, UMGPropertyKeys::RenderTranslation, Vector2DValue))
		{
			Border.Render.RenderTranslation = Vector2DValue;
			Border.Render.bHasRenderTranslation = true;
		}

		Widget.Render = FUMGRenderStyleData();

		if (!Border.Render.bHasRenderOpacity && Node->Transform.Opacity < 1.0f)
		{
			Border.Render.RenderOpacity = Node->Transform.Opacity;
			Border.Render.bHasRenderOpacity = true;
		}
	}

	void TransferBorderBoxShadowProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		if (Widget.Shadow.BoxShadows.Num() > 0)
		{
			Border.Shadow.BoxShadows = Widget.Shadow.BoxShadows;
			Widget.Shadow.BoxShadows.Reset();
		}
	}

	void PrepareWrappedContentWidget(FUMGWidgetNode& Widget)
	{
		Widget.Id += TEXT("_inner");
		Widget.Name += TEXT("_inner");
		Widget.Bounds = FDesignPixelBounds();
		Widget.Slot.HorizontalAlignment = TEXT("Fill");
		Widget.Slot.VerticalAlignment = TEXT("Fill");
	}

	void ApplyRenderTransformPropertiesInternal(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->Transform.Opacity < 1.0f)
		{
			if (Widget.Render.bHasRenderOpacity)
			{
				// 图片填充层 opacity 已写入，与节点级 opacity 合并（乘积）
				Widget.Render.RenderOpacity = Node->Transform.Opacity * Widget.Render.RenderOpacity;
			}
			else
			{
				Widget.Render.RenderOpacity = Node->Transform.Opacity;
				Widget.Render.bHasRenderOpacity = true;
			}
		}

		if (!FMath::IsNearlyZero(Node->Transform.Rotation))
		{
			Widget.Render.RenderAngle = Node->Transform.Rotation;
			Widget.Render.bHasRenderAngle = true;
		}

		if (Node->Transform.bFlipX || Node->Transform.bFlipY)
		{
			const float ScaleX = Node->Transform.bFlipX ? -1.0f : 1.0f;
			const float ScaleY = Node->Transform.bFlipY ? -1.0f : 1.0f;
			Widget.Render.RenderTransformScale = FVector2D(ScaleX, ScaleY);
			Widget.Render.bHasRenderTransformScale = true;
		}

		if (Node->LayoutData.HasExplicitClip() && Node->LayoutData.bClip)
		{
			Widget.Render.Clipping = TEXT("ClipToBounds");
			Widget.Render.bHasClipping = true;
		}
	}
}

void FVisualMapper::ApplyRenderTransformProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	ApplyRenderTransformPropertiesInternal(Widget, Node);
}

void FVisualMapper::ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	VisualMapperFillTransfer::ApplyFillProperties(Widget, Node);
	ApplyRenderTransformPropertiesInternal(Widget, Node);

	ApplyRoundedCorner(Widget, Node->Visual.CornerRadius, Node->Transform.Width.Value, Node->Transform.Height.Value);
	ApplyStroke(Widget, Node->Visual.Stroke);

	int32 ShadowCount = 0;
	for (const FDesignEffect& Effect : Node->Visual.Effects)
	{
		if (!Effect.bEnabled)
			continue;

		switch (Effect.Type)
		{
		case EDesignEffectType::Blur:
			break;
		case EDesignEffectType::Shadow:
			if (Effect.Shadow.bInner)
			{
				break;
			}
			if (Node->Type == EDesignNodeType::Text)
			{
				// TextBlock 只支持单阴影（UE5 限制），后者覆盖前者
				Widget.Shadow.ShadowOffset = FVector2D(Effect.Shadow.OffsetX, Effect.Shadow.OffsetY);
				Widget.Shadow.bHasShadowOffset = true;
				Widget.Shadow.ShadowColorAndOpacity = FUMGValueParser::ParseColor(
					Effect.Shadow.Color.IsEmpty() ? TEXT("#00000040") : Effect.Shadow.Color);
				Widget.Shadow.bHasShadowColorAndOpacity = true;
			}
			else
			{
				FUMGBoxShadowLayerData& Layer = Widget.Shadow.BoxShadows.AddDefaulted_GetRef();
				Layer.Offset = FVector2D(Effect.Shadow.OffsetX, Effect.Shadow.OffsetY);
				Layer.Blur = Effect.Shadow.Blur;
				Layer.Spread = Effect.Shadow.Spread;
				Layer.Color = FUMGValueParser::ParseColor(
					Effect.Shadow.Color.IsEmpty() ? TEXT("#00000040") : Effect.Shadow.Color);
				++ShadowCount;
			}
			break;
		default:
			break;
		}
	}

	if (ShadowCount <= 0)
	{
		Widget.Shadow.BoxShadows.Reset();
	}
}

void FVisualMapper::ApplyRoundedCorner(FUMGWidgetNode& Widget, const FDesignCornerRadius& Radius, float Width, float Height)
{
	if (Radius.bUniform)
	{
		if (Radius.Uniform > 0)
		{
			float ExportRadius = Radius.Uniform;
			const bool bHasResolvedSize = (Width > 0.0f && Height > 0.0f);
			const float MinDim = bHasResolvedSize ? FMath::Min(Width, Height) : 0.0f;
			if (MinDim > 0.0f && Radius.Uniform >= (MinDim * 0.5f - KINDA_SMALL_NUMBER))
			{
				// Treat large uniform radius as pill to match design tools that clamp radius to half-height.
				ExportRadius = 999.0f;
			}
			else if (!bHasResolvedSize && Radius.Uniform >= 100.0f)
			{
				// Height can be unresolved for fit-content nodes; large radii are typically intended as pill.
				ExportRadius = 999.0f;
			}
			Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			Widget.Brush.bHasDrawAs = true;
			Widget.Brush.OutlineCornerRadius = ExportRadius;
		}
	}
	else if (Radius.TopLeft > 0 || Radius.TopRight > 0 || Radius.BottomRight > 0 || Radius.BottomLeft > 0)
	{
		Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Widget.Brush.bHasDrawAs = true;
		Widget.Brush.CornerRadii = FVector4(Radius.TopLeft, Radius.TopRight, Radius.BottomRight, Radius.BottomLeft);
	}
}

void FVisualMapper::ApplyStroke(FUMGWidgetNode& Widget, const FDesignStroke& Stroke)
{
	const FMargin HalfInsets = StrokeHelpers::GetStrokeHalfInsets(Stroke.Thickness);
	float Thickness = Stroke.Thickness.bUniform ? Stroke.Thickness.Uniform
		: FMath::Max(FMath::Max(Stroke.Thickness.Top, Stroke.Thickness.Bottom),
			FMath::Max(Stroke.Thickness.Left, Stroke.Thickness.Right));

	if (Thickness <= 0)
		return;

	if (!HasVisualMapperWidgetStringProperty(Widget, UMGPropertyKeys::DrawAs))
	{
		if (!Widget.Brush.bHasDrawAs)
		{
			Widget.Brush.DrawAs = ESlateBrushDrawType::Box;
			Widget.Brush.bHasDrawAs = true;
		}
	}

	Widget.Brush.OutlineThickness = Thickness;
	Widget.Brush.bHasOutlineThickness = true;

	if (Stroke.Fill.Type == EDesignFillType::Color)
	{
		Widget.Brush.OutlineColor = FUMGValueParser::ParseColor(Stroke.Fill.SolidColor);
		Widget.Brush.bHasOutlineColor = true;
	}

	// Stroke alignment compensation:
	// UMG outline is always centered on the edge. Adjust padding to simulate inside/outside.
	if (Stroke.Align == EDesignStrokeAlign::Inside)
	{
		// Inside: only inset sides that actually carry a stroke.
		Widget.WidgetStyle.StrokePadding = FMargin(HalfInsets.Left, HalfInsets.Top, HalfInsets.Right, HalfInsets.Bottom);
	}
	else if (Stroke.Align == EDesignStrokeAlign::Outside)
	{
		// Outside: only compensate on stroked sides, otherwise we shift content for no reason.
		Widget.WidgetStyle.StrokeMargin = FMargin(-HalfInsets.Left, -HalfInsets.Top, -HalfInsets.Right, -HalfInsets.Bottom);
	}
	// Center: default UMG behavior, no adjustment needed
}

FUMGWidgetNode FVisualMapper::WrapWithBorder(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Border;
	Border.Type = TEXT("Border");
	Border.Id = Widget.Id;
	Border.Name = Widget.Name;
	Border.Bounds = Widget.Bounds;

	// 转移视觉画刷到 Border（TextStyle 留在原 Widget 上）
	Border.Brush = Widget.Brush;
	Widget.Brush = FUMGVisualBrushData();

	TransferBorderBackgroundProperties(Border, Widget);

	if (FLayoutMapper::HasPadding(Node))
		FLayoutMapper::ApplyPadding(Border, Node->LayoutData.Padding);

	TransferBorderStrokeProperties(Border, Widget);

	// Outside stroke: visual bounds from the parser already include the stroke expansion,
	// so do NOT expand Border.Bounds again. Only add StrokePadding so the centered UMG
	// outline doesn't eat into content space.
	ApplyOutsideStrokePaddingToBorder(Border, Widget);
	TransferBorderRenderProperties(Border, Widget, Node);
	TransferBorderBoxShadowProperties(Border, Widget);
	PrepareWrappedContentWidget(Widget);

	Border.Children.Add(MoveTemp(Widget));

	const bool bNeedsExplicitStroke =
		(StrokeHelpers::NeedsExplicitSideStroke(Node)
			&& !HasPlaceholderSurfaceChildren(Node)
			&& !HasOpaqueSolidFill(Node))
		|| (Node.IsValid()
			&& Node->Visual.Stroke.HasStroke()
			&& !Node->Visual.CornerRadius.HasRadius()
			&& StrokeHelpers::HasUniformRectStroke(Node->Visual.Stroke)
			&& !HasPlaceholderSurfaceChildren(Node)
			&& ExplicitSideStrokeHelpers::HasAnyConvertedChildStrokeCoverage(Border));

	if (bNeedsExplicitStroke)
	{
		return ExplicitSideStrokeHelpers::WrapWithExplicitSideStroke(MoveTemp(Border), Node);
	}

	return Border;
}

FUMGWidgetNode FVisualMapper::WrapWithBackgroundBlur(FUMGWidgetNode&& Widget, float BlurStrength)
{
	FUMGWidgetNode Blur;
	Blur.Type = TEXT("BackgroundBlur");
	Blur.Id = Widget.Id + TEXT("_blur");
	Blur.Name = Widget.Name + TEXT("_blur");
	Blur.Bounds = Widget.Bounds;
	Blur.Slot = Widget.Slot;
	Blur.Effect.BlurStrength = BlurStrength;
	Blur.Effect.bHasBlurStrength = true;

	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Widget.Slot.HorizontalAlignment = TEXT("Fill");
	Widget.Slot.VerticalAlignment = TEXT("Fill");

	Blur.Children.Add(MoveTemp(Widget));
	return Blur;
}

FUMGWidgetNode FVisualMapper::WrapWithForegroundBlur(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, float BlurStrength)
{
	FUMGWidgetNode Retainer;
	Retainer.Type = TEXT("RetainerBox");
	Retainer.Id = Widget.Id + TEXT("_fxblur");
	Retainer.Name = Widget.Name + TEXT("_fxblur");
	Retainer.Bounds = Widget.Bounds;
	Retainer.Slot = Widget.Slot;
	Retainer.Effect.bRetainRendering = true;
	Retainer.Effect.bHasRetainRendering = true;
	Retainer.Effect.EffectMaterial = RetainerForegroundBlurEffectRef;
	Retainer.Effect.bHasEffectMaterial = true;
	Retainer.Effect.TextureParameter = TEXT("Texture");
	Retainer.Effect.bHasTextureParameter = true;

	const FVector2D BlurUVStep = ComputeForegroundBlurUVStep(Node, BlurStrength);
	Retainer.Effect.EffectBlurUVStep = BlurUVStep;
	Retainer.Effect.bHasEffectBlurUVStep = true;

	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Retainer.Children.Add(MoveTemp(Widget));
	return Retainer;
}

FUMGWidgetNode FVisualMapper::WrapWithInnerShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FDesignShadow& Shadow)
{
	FUMGWidgetNode Retainer;
	Retainer.Type = TEXT("RetainerBox");
	Retainer.Id = Widget.Id + TEXT("_innershadow");
	Retainer.Name = Widget.Name + TEXT("_innershadow");
	Retainer.Bounds = Widget.Bounds;
	Retainer.Slot = Widget.Slot;
	Retainer.Effect.bRetainRendering = true;
	Retainer.Effect.bHasRetainRendering = true;
	Retainer.Effect.EffectMaterial = RetainerInnerShadowEffectRef;
	Retainer.Effect.bHasEffectMaterial = true;
	Retainer.Effect.TextureParameter = TEXT("Texture");
	Retainer.Effect.bHasTextureParameter = true;
	Retainer.Effect.EffectColor = FUMGValueParser::ParseColor(
		Shadow.Color.IsEmpty() ? TEXT("#00000040") : Shadow.Color);
	Retainer.Effect.bHasEffectColor = true;
	Retainer.Effect.EffectOffset = ComputeEffectUVOffset(Node, Shadow.OffsetX, Shadow.OffsetY);
	Retainer.Effect.bHasEffectOffset = true;
	Retainer.Effect.EffectBlurUVStep = ComputeForegroundBlurUVStep(Node, Shadow.Blur);
	Retainer.Effect.bHasEffectBlurUVStep = true;

	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Retainer.Children.Add(MoveTemp(Widget));
	return Retainer;
}

FUMGWidgetNode FVisualMapper::WrapWithBoxShadow(
	FUMGWidgetNode&& Widget,
	const TSharedPtr<FDesignNode>& Node,
	const FUMGBoxShadowLayerData& ShadowLayer,
	int32 ShadowIndex)
{
	const float OffX = ShadowLayer.Offset.X;
	const float OffY = ShadowLayer.Offset.Y;
	const float Blur = ShadowLayer.Blur;
	const float Spread = ShadowLayer.Spread;
	const FString ShadowColor = Design2UMGWidgetNodeAccess::SerializeWidgetColor(ShadowLayer.Color);

	// Overlay 容纳阴影 Image + 原 Widget（Overlay 子节点的 Fill alignment 有效）
	FUMGWidgetNode Wrapper;
	Wrapper.Type = TEXT("Overlay");
	Wrapper.Id = Widget.Id;
	Wrapper.Name = Widget.Name;
	Wrapper.Bounds = Widget.Bounds;
	Wrapper.Slot = Widget.Slot;

	// 阴影 Image 节点
	const float PositiveSpread = FMath::Max(0.0f, Spread);
	const float NegativeSpreadInset = FMath::Max(0.0f, -Spread);
	const float Extent = Blur + PositiveSpread;
	const float ShapeWidth = FMath::Max(1.0f, Node->Transform.Width.Value - NegativeSpreadInset * 2.0f);
	const float ShapeHeight = FMath::Max(1.0f, Node->Transform.Height.Value - NegativeSpreadInset * 2.0f);
	FUMGWidgetNode Shadow;
	Shadow.Type = TEXT("Image");
	FString ShadowSuffix = FString::Printf(TEXT("_shadow%d"), ShadowIndex);
	Shadow.Id = Widget.Id + ShadowSuffix;
	Shadow.Name = Widget.Name + ShadowSuffix;
	Shadow.Brush.BrushColor = ShadowLayer.Color;
	Shadow.Brush.bHasBrushColor = true;

	// 阴影纹理参数
	const int32 TexW = FMath::Max(1, FMath::RoundToInt(ShapeWidth + Extent * 2.0f));
	const int32 TexH = FMath::Max(1, FMath::RoundToInt(ShapeHeight + Extent * 2.0f));
	Shadow.WidgetStyle.WidthOverride = TexW;
	Shadow.WidgetStyle.HeightOverride = TexH;
	Shadow.Brush.MaterialRef = (Blur > 0.0f) ? TEXT("boxshadow") : MatRef_RoundedRect;
	Shadow.Brush.bHasMaterialRef = true;
	Shadow.Shadow.ShadowBlur = Blur;
	Shadow.Shadow.bHasShadowBlur = true;

	const float HalfMin = FMath::Min(TexW, TexH) * 0.5f;
	if (Node->Visual.CornerRadius.bUniform)
	{
		const float NormalizedRadius = (HalfMin > 0.0f) ? (Node->Visual.CornerRadius.Uniform + Extent) / HalfMin : 0.0f;
		Shadow.Brush.MaterialParams = FString::Printf(
			TEXT("{\"Color\":\"%s\",\"CornerRadius\":%.3f}"),
			*ShadowColor,
			NormalizedRadius);
		Shadow.Brush.bHasMaterialParams = true;
	}
	else
	{
		auto Norm = [&](float RadiusPx)
		{
			return (HalfMin > 0.0f) ? (RadiusPx + Extent) / HalfMin : 0.0f;
		};
		Shadow.Brush.MaterialParams = FString::Printf(
			TEXT("{\"Color\":\"%s\",\"CornerRadii\":[%.3f,%.3f,%.3f,%.3f]}"),
			*ShadowColor,
			Norm(Node->Visual.CornerRadius.TopLeft), Norm(Node->Visual.CornerRadius.TopRight),
			Norm(Node->Visual.CornerRadius.BottomRight), Norm(Node->Visual.CornerRadius.BottomLeft));
		Shadow.Brush.bHasMaterialParams = true;
	}

	// 阴影偏移定位
	Shadow.Slot.HorizontalAlignment = TEXT("Fill");
	Shadow.Slot.VerticalAlignment = TEXT("Fill");
	const float ShadowMarginLeft = OffX - Extent + NegativeSpreadInset;
	const float ShadowMarginTop = OffY - Extent + NegativeSpreadInset;
	Shadow.Render.RenderTranslation = FVector2D(ShadowMarginLeft, ShadowMarginTop);
	Shadow.Render.bHasRenderTranslation = true;

	Wrapper.Children.Add(MoveTemp(Shadow));

	// 原 Widget 叠在阴影上方
	Widget.Id = Widget.Id + TEXT("_content");
	Widget.Name = Widget.Name + TEXT("_content");
	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Widget.Slot.HorizontalAlignment = TEXT("Fill");
	Widget.Slot.VerticalAlignment = TEXT("Fill");

	Wrapper.Children.Add(MoveTemp(Widget));
	return Wrapper;
}

FUMGWidgetNode FVisualMapper::WrapWithMultiFill(
	FUMGWidgetNode&& Widget,
	const TSharedPtr<FDesignNode>& Node,
	const FVisualMapperOptions& Options)
{
	return VisualMapperFillTransfer::WrapWithMultiFill(MoveTemp(Widget), Node, Options);
}


