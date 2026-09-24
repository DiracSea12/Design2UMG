// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapperStrokeSupport.h"

#include "Design2UMGGeneratedNameSuffixes.h"
#include "DesignSchema.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetTypes.h"

namespace
{
	enum class EStrokeSide : uint8
	{
		Top,
		Right,
		Bottom,
		Left
	};

	struct FExplicitStrokeSideSpec
	{
		EStrokeSide Side = EStrokeSide::Top;
		const TCHAR* Suffix = TEXT("");
		EUMGSlotHorizontalAlignment HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		EUMGSlotVerticalAlignment VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
		bool bHorizontalLine = false;
	};

	const FExplicitStrokeSideSpec ExplicitStrokeSideSpecs[] = {
		{ EStrokeSide::Top, Design2UMGGeneratedNameSuffixes::StrokeTop(), EUMGSlotHorizontalAlignment::Fill, EUMGSlotVerticalAlignment::Top, true },
		{ EStrokeSide::Right, Design2UMGGeneratedNameSuffixes::StrokeRight(), EUMGSlotHorizontalAlignment::Right, EUMGSlotVerticalAlignment::Fill, false },
		{ EStrokeSide::Bottom, Design2UMGGeneratedNameSuffixes::StrokeBottom(), EUMGSlotHorizontalAlignment::Fill, EUMGSlotVerticalAlignment::Bottom, true },
		{ EStrokeSide::Left, Design2UMGGeneratedNameSuffixes::StrokeLeft(), EUMGSlotHorizontalAlignment::Left, EUMGSlotVerticalAlignment::Fill, false },
	};

	float GetStrokeSideThicknessInternal(const FDesignStrokeThickness& Thickness, const EStrokeSide Side)
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

	bool TryResolveStrokeColor(const FDesignStroke& Stroke, FLinearColor& OutColor)
	{
		if (!Stroke.Fill.IsSet() || Stroke.Fill->Type != EDesignFillType::Color || Stroke.Fill->SolidColor.IsEmpty())
		{
			return false;
		}

		OutColor = FUMGValueParser::ParseColor(Stroke.Fill->SolidColor);
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
		Widget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Widget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
	}

	void StripExplicitStrokeFromSurface(FUMGWidgetNode& BorderSurface)
	{
		BorderSurface.Brush.OutlineThickness = 0.0f;
		BorderSurface.Brush.OutlineThickness.Reset();
		BorderSurface.Brush.OutlineColor.Reset();
		BorderSurface.WidgetStyle.StrokePadding.Reset();
		BorderSurface.WidgetStyle.StrokeMargin.Reset();
	}

	bool IsPromotableStrokeContentType(const FString& Type)
	{
		return Type == UMGWidgetTypes::CanvasPanel
			|| Type == UMGWidgetTypes::HorizontalBox
			|| Type == UMGWidgetTypes::VerticalBox
			|| Type == UMGWidgetTypes::SizeBox;
	}

	bool HasExplicitStrokeSurfacePaint(const FUMGVisualBrushData& Brush)
	{
		return (Brush.BrushColor.IsSet() && Brush.BrushColor.GetValue().A > KINDA_SMALL_NUMBER)
			|| Brush.OutlineCornerRadius.IsSet()
			|| Brush.CornerRadii.IsSet()
			|| Brush.OutlineThickness.IsSet()
			|| Brush.Margin.IsSet()
			|| Brush.OutlineColor.IsSet()
			|| Brush.MaterialRef.IsSet()
			|| Brush.MaterialParams.IsSet()
			|| Brush.Shape.IsSet()
			|| Brush.Image.HasAny()
			|| Brush.Gradient.HasAny();
	}

	bool CanPromoteExplicitStrokeSurfaceContent(
		const FUMGWidgetNode& BorderSurface,
		const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid()
			|| Node->Visual.Fills.Num() > 0
			|| Node->Visual.CornerRadius.HasRadius()
			|| BorderSurface.Type != UMGWidgetTypes::Border
			|| BorderSurface.Children.Num() != 1
			|| !IsPromotableStrokeContentType(BorderSurface.Children[0].Type)
			|| BorderSurface.HasSourceData()
			|| HasExplicitStrokeSurfacePaint(BorderSurface.Brush))
		{
			return false;
		}

		FUMGWidgetStyleData AppliedBorderStyle = BorderSurface.WidgetStyle;
		// These size constraints were transferred from the content when the synthetic
		// Border was created; they do not give a paint-free surface a separate duty.
		if (BorderSurface.Children[0].Type != UMGWidgetTypes::SizeBox)
		{
			AppliedBorderStyle.WidthOverride.Reset();
			AppliedBorderStyle.HeightOverride.Reset();
		}

		return !AppliedBorderStyle.HasAny()
			&& BorderSurface.TextStyle.FontSize == 0.0f
			&& BorderSurface.TextStyle.SourceFontSize == 0.0f
			&& !BorderSurface.TextStyle.LetterSpacing.IsSet()
			&& !BorderSurface.Text.HasAny()
			&& !BorderSurface.Effect.HasAny()
			&& !BorderSurface.Path.HasAny()
			&& !BorderSurface.Shadow.HasAny()
			&& !BorderSurface.Render.HasAny()
			&& !BorderSurface.WidgetState.HasAny()
			&& !BorderSurface.ScrollBox.HasAny();
	}

	FUMGWidgetNode PromoteExplicitStrokeSurfaceContent(FUMGWidgetNode& BorderSurface)
	{
		FUMGWidgetNode Content = MoveTemp(BorderSurface.Children[0]);
		// A SizeBox already owns the child's constraints. The removable surface
		// has no style in this branch, so do not clear those constraints with it.
		if (Content.Type == UMGWidgetTypes::SizeBox) return Content;
		Content.WidgetStyle.WidthOverride = BorderSurface.WidgetStyle.WidthOverride;
		Content.WidgetStyle.HeightOverride = BorderSurface.WidgetStyle.HeightOverride;
		return Content;
	}

	FUMGWidgetNode MakeStrokeLineNode(
		const FString& Id,
		const FString& Name,
		const FLinearColor& Color,
		const float Width,
		const float Height,
		const EUMGSlotHorizontalAlignment HorizontalAlignment,
		const EUMGSlotVerticalAlignment VerticalAlignment)
	{
		FUMGWidgetNode Line;
		Line.Type = UMGWidgetTypes::Image;
		Line.Id = Id;
		Line.Name = Name;
		Line.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
		Line.Brush.BrushColor = Color;
		Line.WidgetStyle.WidthOverride = FMath::Max(1.0f, Width);
		Line.WidgetStyle.HeightOverride = FMath::Max(1.0f, Height);
		Line.Slot.HorizontalAlignment = HorizontalAlignment;
		Line.Slot.VerticalAlignment = VerticalAlignment;
		return Line;
	}

	void AddExplicitStrokeLine(
		FUMGWidgetNode& Overlay,
		const FLinearColor& StrokeColor,
		const float Width,
		const float Height,
		const TCHAR* Suffix,
		const EUMGSlotHorizontalAlignment HorizontalAlignment,
		const EUMGSlotVerticalAlignment VerticalAlignment)
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

	void FoldStrokeOverlayIntoCanvas(FUMGWidgetNode& Overlay, const FDesignNode& Source)
	{
		if (!Source.Transform.Width.IsFixed() || !Source.Transform.Height.IsFixed()
			|| Overlay.Children.IsEmpty()) return;
		const FUMGWidgetNode& Content = Overlay.Children[0];
		if (Content.Type != UMGWidgetTypes::CanvasPanel || Content.HasSourceData()
			|| Content.WidgetStyle.HasAny()
			|| Content.Render.HasAny() || Content.Effect.HasAny() || Content.Shadow.HasAny()
			|| Content.WidgetState.HasAny()) return;

		// Only the synthetic, fill-aligned canvas is removed. Authored children keep
		// their identities and canvas slots; decorations are appended in paint order.
		TArray<FUMGWidgetNode> Children = MoveTemp(Overlay.Children[0].Children);
		for (int32 Index = 1; Index < Overlay.Children.Num(); ++Index)
		{
			FUMGWidgetNode& Stroke = Overlay.Children[Index];
			const EUMGSlotHorizontalAlignment H = Stroke.Slot.HorizontalAlignment;
			const EUMGSlotVerticalAlignment V = Stroke.Slot.VerticalAlignment;
			Stroke.Slot = FUMGSlotData();
			Stroke.Slot.Offsets = FUMGOffsetsData();
			Stroke.Slot.Offsets.Right = 0.f;
			Stroke.Slot.Offsets.Bottom = 0.f;
			if (H == EUMGSlotHorizontalAlignment::Fill)
			{
				Stroke.Slot.Anchors.Max.X = 1.f;
			}
			else
			{
				const float Anchor = H == EUMGSlotHorizontalAlignment::Right ? 1.f : 0.f;
				Stroke.Slot.Anchors.Min.X = Stroke.Slot.Anchors.Max.X = Anchor;
				Stroke.Slot.Alignment.X = Anchor;
				Stroke.Slot.Offsets.Right = FMath::Min(Source.Transform.Width.Value, Stroke.WidgetStyle.WidthOverride.Get(1.f));
			}
			if (V == EUMGSlotVerticalAlignment::Fill)
			{
				Stroke.Slot.Anchors.Max.Y = 1.f;
			}
			else
			{
				const float Anchor = V == EUMGSlotVerticalAlignment::Bottom ? 1.f : 0.f;
				Stroke.Slot.Anchors.Min.Y = Stroke.Slot.Anchors.Max.Y = Anchor;
				Stroke.Slot.Alignment.Y = Anchor;
				Stroke.Slot.Offsets.Bottom = FMath::Min(Source.Transform.Height.Value, Stroke.WidgetStyle.HeightOverride.Get(1.f));
			}
			Children.Add(MoveTemp(Stroke));
		}
		Overlay.Type = UMGWidgetTypes::CanvasPanel;
		Overlay.Children = MoveTemp(Children);
	}
}

namespace VisualMapperStrokeSupport
{
	FMargin GetStrokeHalfInsets(const FDesignStrokeThickness& Thickness)
	{
		return FMargin(
			GetStrokeSideThicknessInternal(Thickness, EStrokeSide::Left) * 0.5f,
			GetStrokeSideThicknessInternal(Thickness, EStrokeSide::Top) * 0.5f,
			GetStrokeSideThicknessInternal(Thickness, EStrokeSide::Right) * 0.5f,
			GetStrokeSideThicknessInternal(Thickness, EStrokeSide::Bottom) * 0.5f);
	}

	bool HasUniformRectStroke(const FDesignStroke& Stroke)
	{
		const float Top = GetStrokeSideThicknessInternal(Stroke.Thickness, EStrokeSide::Top);
		const float Right = GetStrokeSideThicknessInternal(Stroke.Thickness, EStrokeSide::Right);
		const float Bottom = GetStrokeSideThicknessInternal(Stroke.Thickness, EStrokeSide::Bottom);
		const float Left = GetStrokeSideThicknessInternal(Stroke.Thickness, EStrokeSide::Left);
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

	FUMGWidgetNode WrapWithExplicitSideStroke(FUMGWidgetNode&& Border, const TSharedPtr<FDesignNode>& Node)
	{
		FLinearColor StrokeColor;
		if (!TryResolveStrokeColor(Node->Visual.Stroke, StrokeColor))
		{
			return MoveTemp(Border);
		}

		FUMGWidgetNode Overlay;
		Overlay.Type = UMGWidgetTypes::Overlay;
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

		Border.Id += Design2UMGGeneratedNameSuffixes::Surface();
		Border.Name += Design2UMGGeneratedNameSuffixes::Surface();
		StripExplicitStrokeFromSurface(Border);

		if (!HasUniformRectStroke(Node->Visual.Stroke))
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
		if (CanPromoteExplicitStrokeSurfaceContent(Border, Node))
		{
			Overlay.Children.Add(PromoteExplicitStrokeSurfaceContent(Border));
		}
		else
		{
			Overlay.Children.Add(MoveTemp(Border));
		}

		const float TopThickness = GetStrokeSideThicknessInternal(Node->Visual.Stroke.Thickness, EStrokeSide::Top);
		const float BottomThickness = GetStrokeSideThicknessInternal(Node->Visual.Stroke.Thickness, EStrokeSide::Bottom);
		const bool bCombineHorizontalSides = TopThickness >= 1.0f
			&& TopThickness == BottomThickness
			&& TopThickness == FMath::RoundToFloat(TopThickness)
			&& Overlay.Bounds.H >= 2.0f * TopThickness;
		if (bCombineHorizontalSides)
		{
			// Opposite horizontal sides share a mask. Keep left/right draws independent
			// for their individual widths and overlapping translucent corners.
			FUMGWidgetNode Outline;
			Outline.Type = UMGWidgetTypes::Border;
			Outline.Id = Overlay.Id + TEXT("_stroke_horizontal");
			Outline.Name = Overlay.Name + TEXT("_stroke_horizontal");
			Outline.Brush.DrawAs = ESlateBrushDrawType::Box;
			Outline.Brush.MaterialRef = TEXT("horizontalstroke");
			Outline.Brush.Margin = FMargin(1.0f / 3.0f);
			Outline.WidgetStyle.WidthOverride = TopThickness * 3.0f;
			Outline.WidgetStyle.HeightOverride = TopThickness * 3.0f;
			Outline.WidgetStyle.Visibility = TEXT("HitTestInvisible");
			Outline.Brush.BrushColor = StrokeColor;
			ResetChildToFill(Outline);
			Overlay.Children.Add(MoveTemp(Outline));
		}

		for (const FExplicitStrokeSideSpec& SideSpec : ExplicitStrokeSideSpecs)
		{
			if (bCombineHorizontalSides && SideSpec.bHorizontalLine) continue;
			const float Thickness = GetStrokeSideThicknessInternal(Node->Visual.Stroke.Thickness, SideSpec.Side);
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

		FoldStrokeOverlayIntoCanvas(Overlay, *Node);
		return Overlay;
	}
}
