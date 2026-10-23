// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignSourceExportBounds.h"
#include "DesignSchema.h"
#include "VisualMapperStrokeSupport.h"

namespace
{
	using FExportRefResolver = TFunctionRef<TSharedPtr<FDesignNode>(const TSharedPtr<FDesignNode>&)>;

	struct FSourceChildLayout
	{
		TSharedPtr<FDesignNode> Node;
		FVector2D Size;
		FVector2D Position;
	};

	TArray<FSourceChildLayout> ResolveSourceChildLayout(const FDesignNode& Node, const FVector2D& Size, FExportRefResolver ResolveRef)
	{
		TArray<FSourceChildLayout> Children;
		const FDesignPadding Padding = Node.LayoutData.ExplicitPadding.Get(FDesignPadding{});
		const FVector2D Inner(FMath::Max(0.0, Size.X - Padding.Left - Padding.Right), FMath::Max(0.0, Size.Y - Padding.Top - Padding.Bottom));
		const bool bHorizontal = Node.LayoutData.Layout == EDesignLayout::Horizontal;
		const bool bLinear = bHorizontal || Node.LayoutData.Layout == EDesignLayout::Vertical;
		const int32 Main = bHorizontal ? 0 : 1;
		const int32 Cross = 1 - Main;
		int32 FlowCount = 0;
		double FixedMain = 0;
		double FillWeight = 0;
		const double Gap = Node.LayoutData.ExplicitGap.Get(0.0f);
		for (const auto& SourceChild : Node.Children)
		{
			if (!SourceChild.IsValid() || !SourceChild->Transform.bEnabled) continue;
			TSharedPtr<FDesignNode> Child = SourceChild->Type == EDesignNodeType::Ref ? ResolveRef(SourceChild) : SourceChild;
			if (!Child.IsValid()) continue;
			FSourceChildLayout Layout{Child, FVector2D(Child->Transform.Width.Value, Child->Transform.Height.Value), FVector2D(Child->Transform.X, Child->Transform.Y)};
			const bool bFlow = bLinear && !Child->LayoutData.bAbsoluteLayoutPositioning;
			if (Child->Transform.Width.IsFill() && (!bFlow || !bHorizontal)) Layout.Size.X = Inner.X;
			if (Child->Transform.Height.IsFill() && (!bFlow || bHorizontal)) Layout.Size.Y = Inner.Y;
			if (bFlow)
			{
				const bool bFill = bHorizontal ? Child->Transform.Width.IsFill() : Child->Transform.Height.IsFill();
				if (bFill) FillWeight += (Child->LayoutData.LayoutGrow > 0 ? Child->LayoutData.LayoutGrow : 1.0f);
				else FixedMain += Layout.Size[Main];
				++FlowCount;
			}
			Children.Add(MoveTemp(Layout));
		}
		const double FreeMain = FMath::Max(0.0, Inner[Main] - FixedMain - Gap * FMath::Max(0, FlowCount - 1));
		double Cursor = bHorizontal ? Padding.Left : Padding.Top;
		double Spacing = Gap;
		if (FillWeight == 0 && FlowCount > 0)
		{
			switch (Node.LayoutData.JustifyContent)
			{
			case EDesignJustifyContent::Center: Cursor += FreeMain * 0.5; break;
			case EDesignJustifyContent::End: Cursor += FreeMain; break;
			case EDesignJustifyContent::SpaceBetween: if (FlowCount > 1) Spacing += FreeMain / (FlowCount - 1); break;
			case EDesignJustifyContent::SpaceAround: Spacing += FreeMain / FlowCount; Cursor += FreeMain / (2 * FlowCount); break;
			default: break;
			}
		}
		for (FSourceChildLayout& Layout : Children)
		{
			const FDesignNode& Child = *Layout.Node;
			if (!bLinear || Child.LayoutData.bAbsoluteLayoutPositioning) continue;
			const bool bFill = bHorizontal ? Child.Transform.Width.IsFill() : Child.Transform.Height.IsFill();
			if (bFill) Layout.Size[Main] = FillWeight > 0 ? FreeMain * (Child.LayoutData.LayoutGrow > 0 ? Child.LayoutData.LayoutGrow : 1.0f) / FillWeight : 0;
			FVector2D Position;
			Position[Main] = Cursor;
			Position[Cross] = bHorizontal ? Padding.Top : Padding.Left;
			const double FreeCross = FMath::Max(0.0, Inner[Cross] - Layout.Size[Cross]);
			if (Node.LayoutData.AlignItems == EDesignAlignItems::Center) Position[Cross] += FreeCross * 0.5;
			else if (Node.LayoutData.AlignItems == EDesignAlignItems::End) Position[Cross] += FreeCross;
			if (!Child.Transform.HasExplicitX()) Layout.Position.X = Position.X;
			if (!Child.Transform.HasExplicitY()) Layout.Position.Y = Position.Y;
			Cursor += Layout.Size[Main] + Spacing;
		}
		return Children;
	}

	FBox2D TransformSourceBounds(const FBox2D& Bounds, const FDesignNode& Node, const FVector2D& Size)
	{
		const FVector2D Pivot = Size * Node.Transform.TransformOrigin;
		FBox2D Result(ForceInit);
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			FVector2D Point((Corner & 1) ? Bounds.Max.X : Bounds.Min.X, (Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y);
			Point -= Pivot;
			if (Node.Transform.bFlipX) Point.X = -Point.X;
			if (Node.Transform.bFlipY) Point.Y = -Point.Y;
			Result += Point.GetRotated(Node.Transform.Rotation) + Pivot;
		}
		return Result;
	}

	struct FSourcePaintBounds
	{
		FBox2D Silhouette;
		FBox2D Visual;
	};

	FSourcePaintBounds ResolveSourcePaintBounds(const FDesignNode& Node, const FVector2D& Size, FExportRefResolver ResolveRef)
	{
		FBox2D Silhouette(FVector2D::ZeroVector, Size);
		if (Node.Visual.Stroke.Align != EDesignStrokeAlign::Inside)
		{
			FMargin Stroke = VisualMapperStrokeSupport::GetStrokeHalfInsets(Node.Visual.Stroke.Thickness);
			if (Node.Visual.Stroke.Align == EDesignStrokeAlign::Outside) Stroke = Stroke * 2.0f;
			Silhouette += FBox2D(FVector2D(-Stroke.Left, -Stroke.Top), Size + FVector2D(Stroke.Right, Stroke.Bottom));
		}
		FBox2D Visual = Silhouette;
		if (!Node.LayoutData.ExplicitClip.Get(false))
		{
			for (const FSourceChildLayout& Child : ResolveSourceChildLayout(Node, Size, ResolveRef))
			{
				const FSourcePaintBounds Bounds = ResolveSourcePaintBounds(*Child.Node, Child.Size, ResolveRef);
				const FBox2D Shape = TransformSourceBounds(Bounds.Silhouette, *Child.Node, Child.Size);
				const FBox2D Paint = TransformSourceBounds(Bounds.Visual, *Child.Node, Child.Size);
				Silhouette += FBox2D(Shape.Min + Child.Position, Shape.Max + Child.Position);
				Visual += FBox2D(Paint.Min + Child.Position, Paint.Max + Child.Position);
			}
		}
		Visual += Silhouette;
		double BlurVariance = 0;
		for (const FDesignEffect& Effect : Node.Visual.Effects)
		{
			if (!Effect.bEnabled) continue;
			if (Effect.Type == EDesignEffectType::Shadow && !Effect.Shadow.bInner)
			{
				const double Radius = Effect.Shadow.Blur * 1.5 + Effect.Shadow.Spread;
				const FVector2D Offset(Effect.Shadow.OffsetX, Effect.Shadow.OffsetY);
				Visual += FBox2D(Silhouette.Min + Offset - FVector2D(Radius), Silhouette.Max + Offset + FVector2D(Radius));
			}
			else if (Effect.Type == EDesignEffectType::Blur) BlurVariance += FMath::Square(Effect.Radius * 0.5);
		}
		if (BlurVariance > 0)
		{
			const FVector2D Radius(3 * FMath::Sqrt(BlurVariance));
			Visual = FBox2D(Visual.Min - Radius, Visual.Max + Radius);
		}
		return {Silhouette, Visual};
	}
}

FBox2D DesignSourceExportBounds::Calculate(const TSharedPtr<FDesignNode>& SourceNode,
	TFunctionRef<TSharedPtr<FDesignNode>(const TSharedPtr<FDesignNode>&)> ResolveRef)
{
	const TSharedPtr<FDesignNode> Node = SourceNode->Type == EDesignNodeType::Ref ? ResolveRef(SourceNode) : SourceNode;
	if (!Node.IsValid()) return FBox2D(ForceInit);
	const FVector2D Size(Node->Transform.Width.Value, Node->Transform.Height.Value);
	FBox2D Result = TransformSourceBounds(ResolveSourcePaintBounds(*Node, Size, ResolveRef).Visual, *Node, Size);
	Result.Min.X = FMath::FloorToDouble(Result.Min.X);
	Result.Min.Y = FMath::FloorToDouble(Result.Min.Y);
	Result.Max.X = FMath::CeilToDouble(Result.Max.X);
	Result.Max.Y = FMath::CeilToDouble(Result.Max.Y);
	return Result;
}
