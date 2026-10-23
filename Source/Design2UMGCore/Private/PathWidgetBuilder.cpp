// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PathWidgetBuilder.h"

#include "Design2UMGLog.h"
#include "DesignTypeMapper.h"
#include "UMGValueParser.h"
#include "UMGWidgetTypes.h"
#include "VisualMapperFillTransfer.h"

namespace
{
	FVector2D EllipsePoint(const FVector2D& Center, const FVector2D& Radius, float Angle)
	{
		const float Radians = FMath::DegreesToRadians(Angle);
		return Center + FVector2D(Radius.X * FMath::Cos(Radians), Radius.Y * FMath::Sin(Radians));
	}

	void AppendEllipseArc(FString& Geometry, const FVector2D& Center, const FVector2D& Radius, float Start, float Sweep)
	{
		// SVG needs separate arcs for a complete revolution with coincident endpoints.
		const int32 Segments = FMath::Max(1, FMath::CeilToInt(FMath::Abs(Sweep) / 180.0f));
		for (int32 Index = 1; Index <= Segments; ++Index)
		{
			const FVector2D End = EllipsePoint(Center, Radius, Start + Sweep * Index / Segments);
			Geometry += FString::Printf(TEXT(" A %.6f %.6f 0 0 %d %.6f %.6f"), Radius.X, Radius.Y, Sweep > 0 ? 1 : 0, End.X, End.Y);
		}
	}

	FString MakeEllipsePath(const FDesignNode& Node, float Width, float Height)
	{
		const FVector2D Center(Width / 2.0f, Height / 2.0f);
		const FVector2D Radius = Center;
		const float InnerRatio = FMath::Clamp(Node.Shape.InnerRadius, 0.0f, 1.0f);
		const bool bRing = InnerRatio > 0.0001f;
		const bool bFull = FMath::Abs(Node.Shape.SweepAngle) >= 359.9999f;
		const float Start = bFull ? 0.0f : -Node.Shape.StartAngle;
		const float Sweep = bFull ? 360.0f : -Node.Shape.SweepAngle;
		const FVector2D First = EllipsePoint(Center, Radius, Start);
		FString Geometry = FString::Printf(TEXT("M %.6f %.6f"), First.X, First.Y);
		AppendEllipseArc(Geometry, Center, Radius, Start, Sweep);
		if (bFull)
		{
			Geometry += TEXT(" Z");
			if (!bRing) return Geometry;
			const FVector2D InnerFirst = EllipsePoint(Center, Radius * InnerRatio, Start);
			Geometry += FString::Printf(TEXT(" M %.6f %.6f"), InnerFirst.X, InnerFirst.Y);
		}
		else
		{
			const FVector2D End = bRing ? EllipsePoint(Center, Radius * InnerRatio, Start + Sweep) : Center;
			Geometry += FString::Printf(TEXT(" L %.6f %.6f"), End.X, End.Y);
		}
		if (bRing) AppendEllipseArc(Geometry, Center, Radius * InnerRatio, bFull ? Start : Start + Sweep, -Sweep);
		return Geometry + TEXT(" Z");
	}

	FString MakePolygonPath(const FDesignNode& Node, float Width, float Height)
	{
		const int32 Count = Node.Shape.PolygonCount;
		if (Count < 3) return FString();
		TArray<FVector2D> Vertices;
		const FVector2D Center(Width / 2.0f, Height / 2.0f);
		for (int32 Index = 0; Index < Count; ++Index)
			Vertices.Add(EllipsePoint(Center, Center, -90.0f + 360.0f * Index / Count));
		TArray<double> InsetFactors;
		InsetFactors.SetNumUninitialized(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D Before = (Vertices[(Index + Count - 1) % Count] - Vertices[Index]).GetSafeNormal();
			const FVector2D After = (Vertices[(Index + 1) % Count] - Vertices[Index]).GetSafeNormal();
			const double HalfAngle = FMath::Acos(FMath::Clamp(FVector2D::DotProduct(Before, After), -1.0, 1.0)) / 2.0;
			InsetFactors[Index] = HalfAngle >= 0.000001 && HalfAngle <= PI / 2.0 - 0.000001 ? 1.0 / FMath::Tan(HalfAngle) : 0.0;
		}
		double Radius = Node.Visual.CornerRadius.ToVector4().X;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Next = (Index + 1) % Count;
			const double InsetFactorSum = InsetFactors[Index] + InsetFactors[Next];
			// Both tangent distances must fit on their shared edge, including stretched polygons.
			if (InsetFactorSum > 0.0)
				Radius = FMath::Min(Radius, (Vertices[Next] - Vertices[Index]).Size() / InsetFactorSum);
		}
		FString Geometry;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D Vertex = Vertices[Index];
			const FVector2D Before = (Vertices[(Index + Count - 1) % Count] - Vertex).GetSafeNormal();
			const FVector2D After = (Vertices[(Index + 1) % Count] - Vertex).GetSafeNormal();
			const bool bRounded = Radius > 0.0 && InsetFactors[Index] > 0.0;
			const double Inset = bRounded ? Radius * InsetFactors[Index] : 0.0;
			const FVector2D Entry = Vertex + Before * Inset;
			Geometry += FString::Printf(TEXT("%s %.6f %.6f"), Index == 0 ? TEXT("M") : TEXT(" L"), Entry.X, Entry.Y);
			if (bRounded)
			{
				const FVector2D Exit = Vertex + After * Inset;
				Geometry += FString::Printf(TEXT(" A %.6f %.6f 0 0 1 %.6f %.6f"), Radius, Radius, Exit.X, Exit.Y);
			}
		}
		return Geometry + TEXT(" Z");
	}

	TSharedPtr<FDesignNode> WithPrimitiveGeometry(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node->Shape.Geometry.IsEmpty()) return Node;
		FString Geometry;
		const float Width = Node->Transform.Width.Value;
		const float Height = Node->Transform.Height.Value;
		if (Node->Type == EDesignNodeType::Polygon) Geometry = MakePolygonPath(*Node, Width, Height);
		else if (Node->Type == EDesignNodeType::Ellipse) Geometry = MakeEllipsePath(*Node, Width, Height);
		if (Geometry.IsEmpty()) return Node;
		TSharedPtr<FDesignNode> Result = MakeShared<FDesignNode>(*Node);
		Result->Shape.Geometry = MoveTemp(Geometry);
		if (Width > 0 && Height > 0) Result->Shape.ViewBox = FVector4(0, 0, Width, Height);
		return Result;
	}

	FString GetStrokeCapString(const EDesignStrokeCap Cap)
	{
		switch (Cap)
		{
		case EDesignStrokeCap::Circular:
			return TEXT("round");
		case EDesignStrokeCap::Square:
			return TEXT("square");
		default:
			return TEXT("butt");
		}
	}

	FString GetStrokeJoinString(const EDesignStrokeJoin Join)
	{
		switch (Join)
		{
		case EDesignStrokeJoin::Circular:
			return TEXT("round");
		case EDesignStrokeJoin::Bevel:
			return TEXT("bevel");
		default:
			return TEXT("miter");
		}
	}

	void ApplyPathSizeOverrides(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		Widget.WidgetStyle.WidthOverride = FMath::Max(1, FMath::RoundToInt(Node->Transform.Width.Value));
		Widget.WidgetStyle.HeightOverride = FMath::Max(1, FMath::RoundToInt(Node->Transform.Height.Value));
	}

	bool HasGradientPathFill(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		for (const FDesignFill& Fill : Node->Visual.Fills)
		{
			if (Fill.Type == EDesignFillType::Gradient)
			{
				return true;
			}
		}

		return false;
	}

	FString GetVectorRasterFallbackTexturePath(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || Node->Type != EDesignNodeType::Ellipse || !HasGradientPathFill(Node))
		{
			return FString();
		}

		return Node->Visual.VectorRasterFallbackImageRef;
	}

	FString ResolvePathRasterFillColor(const TSharedPtr<FDesignNode>& Node)
	{
		const FDesignFill& PrimaryFill = Node->Visual.TopmostFill();
		if (PrimaryFill.Type == EDesignFillType::Shader) return TEXT("#FFFFFFFF");
		if (PrimaryFill.Type == EDesignFillType::Color)
		{
			return PrimaryFill.SolidColor;
		}

		if (PrimaryFill.Type == EDesignFillType::Gradient && PrimaryFill.Gradient.HasColors())
		{
			return PrimaryFill.Gradient.Mesh.IsSet() ? TEXT("#FFFFFFFF") : PrimaryFill.Gradient.Colors[0].Color;
		}

		for (const FDesignFill& Fill : Node->Visual.Fills)
		{
			if (Fill.Type == EDesignFillType::Color)
			{
				return Fill.SolidColor;
			}

			if (Fill.Type == EDesignFillType::Gradient && Fill.Gradient.HasColors())
			{
				return Fill.Gradient.Mesh.IsSet() ? TEXT("#FFFFFFFF") : Fill.Gradient.Colors[0].Color;
			}
		}

		return FString();
	}

	FString ResolvePathRasterStrokeColor(const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->Visual.Stroke.HasStroke()
			&& Node->Visual.Stroke.Fill.IsSet()
			&& Node->Visual.Stroke.Fill->Type == EDesignFillType::Color)
		{
			return Node->Visual.Stroke.Fill->SolidColor;
		}

		return FString();
	}

	float ResolvePathRasterStrokeThickness(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node->Visual.Stroke.HasStroke())
		{
			return 0.0f;
		}

		if (Node->Visual.Stroke.Thickness.bUniform)
		{
			return Node->Visual.Stroke.Thickness.Uniform;
		}

		return FMath::Max(
			FMath::Max(Node->Visual.Stroke.Thickness.Top, Node->Visual.Stroke.Thickness.Right),
			FMath::Max(Node->Visual.Stroke.Thickness.Bottom, Node->Visual.Stroke.Thickness.Left));
	}

}

FUMGWidgetNode PathWidgetBuilder::CreatePathWidget(const TSharedPtr<FDesignNode>& SourceNode)
{
	const TSharedPtr<FDesignNode> Node = WithPrimitiveGeometry(SourceNode);
	FUMGWidgetNode Widget;

	if (const FString VectorRasterFallback = GetVectorRasterFallbackTexturePath(Node); !VectorRasterFallback.IsEmpty())
	{
		Widget.Type = UMGWidgetTypes::Image;
		Widget.Brush.Image.TexturePath = VectorRasterFallback;
		ApplyPathSizeOverrides(Widget, Node);
		return Widget;
	}

	const FDesignFill& PrimaryFill = Node->Visual.TopmostFill();
	if (PrimaryFill.Type == EDesignFillType::Image && !PrimaryFill.Image.Url.IsEmpty() && Node->Shape.Geometry.IsEmpty())
	{
		Widget.Type = UMGWidgetTypes::Image;
		Widget.Brush.Image.TexturePath = PrimaryFill.Image.Url;
		// Shared visual mapping applies the image paint once.
		ApplyPathSizeOverrides(Widget, Node);
		return Widget;
	}

	if (!Node->Shape.Geometry.IsEmpty())
	{
		const FString FillColor = PrimaryFill.IsImage() ? TEXT("#FFFFFF") : ResolvePathRasterFillColor(Node);
		const bool bNativePaintedStroke = !Node->Shape.PaintGeometry.StrokePaths.IsSet()
            && Node->Visual.Stroke.Fill.IsSet()
            && (Node->Visual.Stroke.Fill->Type == EDesignFillType::Gradient || Node->Visual.Stroke.Fill->Type == EDesignFillType::Shader);
		const FString StrokeColor = bNativePaintedStroke ? TEXT("#FFFFFF") : ResolvePathRasterStrokeColor(Node);
		const float StrokeThickness = ResolvePathRasterStrokeThickness(Node);
		const bool bExpandedStroke = Node->Shape.PaintGeometry.StrokePaths.IsSet();
		if (!FillColor.IsEmpty() || (!StrokeColor.IsEmpty() && StrokeThickness > 0.0f)
			|| (bExpandedStroke && Node->Visual.Stroke.HasStroke()))
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("CreatePathWidget: '%s' rasterize %dx%d (raw=%.1fx%.1f) geometry=%d chars fill=%d stroke=%.2f"),
				*Node->Name,
				FMath::Max(1, FMath::RoundToInt(Node->Transform.Width.Value)),
				FMath::Max(1, FMath::RoundToInt(Node->Transform.Height.Value)),
				Node->Transform.Width.Value, Node->Transform.Height.Value, Node->Shape.Geometry.Len(),
				FillColor.IsEmpty() ? 0 : 1,
				StrokeThickness);

			Widget.Type = UMGWidgetTypes::Image;
			Widget.Path.Geometry = Node->Shape.Geometry;
			Widget.Path.ViewBox = Node->Shape.ViewBox;
			if (Node->Shape.bFitGeometryToBounds) Widget.Path.FitGeometryToBounds = true;
			if (Node->Shape.ViewBox.IsSet() || Node->Shape.bFitGeometryToBounds) Widget.Path.CanvasSize = FVector2D(Node->Transform.Width.Value, Node->Transform.Height.Value);
			if (PrimaryFill.IsImage()) Widget.Brush.Image.TexturePath = PrimaryFill.Image.Url;
			Widget.Path.FillRule = Node->Shape.FillRule;
			if (!FillColor.IsEmpty())
			{
				Widget.Path.FillColor = FUMGValueParser::ParseColor(FillColor);
			}
			if (!bExpandedStroke && !StrokeColor.IsEmpty() && StrokeThickness > 0.0f)
			{
				Widget.Path.StrokeColor = FUMGValueParser::ParseColor(StrokeColor);
				Widget.Path.StrokeThickness = StrokeThickness;
				Widget.Path.StrokeAlignment = Node->Visual.Stroke.Align == EDesignStrokeAlign::Inside ? TEXT("inner")
					: (Node->Visual.Stroke.Align == EDesignStrokeAlign::Outside ? TEXT("outer") : TEXT("center"));
				Widget.Path.StrokeCap = GetStrokeCapString(Node->Visual.Stroke.Cap);
				Widget.Path.StrokeJoin = GetStrokeJoinString(Node->Visual.Stroke.Join);
				if (bNativePaintedStroke && Node->Visual.Stroke.Fill->Type == EDesignFillType::Gradient)
				{
					FUMGVisualBrushData StrokeBrush;
					VisualMapperFillTransfer::ApplyGradientBrushData(StrokeBrush, Node->Visual.Stroke.Fill->Gradient);
					Widget.Path.StrokeGradient = MoveTemp(StrokeBrush.Gradient);
				}
			}
			ApplyPathSizeOverrides(Widget, Node);
			return Widget;
		}
	}

	Widget.Type = DesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->LayoutData.Layout);
	return Widget;
}
