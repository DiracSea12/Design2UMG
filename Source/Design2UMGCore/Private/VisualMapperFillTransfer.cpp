// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapperFillTransfer.h"

#include "ImageFillUtils.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"

namespace
{
	namespace GradientHelpers
	{
		const TCHAR* MatRef_GradientLinear = TEXT("gradientlinear");
		const TCHAR* MatRef_Radial = TEXT("radialgradient");
		const TCHAR* MatRef_Angular = TEXT("angulargradient");

		const TCHAR* ToTypedGradientTypeString(const EDesignGradientType Type)
		{
			switch (Type)
			{
			case EDesignGradientType::Radial:
				return TEXT("Radial");
			case EDesignGradientType::Angular:
				return TEXT("Angular");
			case EDesignGradientType::Linear:
			default:
				return TEXT("Linear");
			}
		}

		FString BuildGradientMaterialParams(const FDesignGradient& Gradient)
		{
			if (Gradient.Colors.Num() == 0)
			{
				return FString();
			}

			const FString& StartColor = Gradient.Colors[0].Color;
			const FString& EndColor = Gradient.Colors.Last().Color;
			const float Rotation = Gradient.Rotation;
			const bool bHasLinearEndpoints = Gradient.GradientType == EDesignGradientType::Linear
				&& Gradient.StartPoint.IsSet()
				&& Gradient.EndPoint.IsSet();
			const FVector2D StartPoint = Gradient.StartPoint.Get(FVector2D::ZeroVector);
			const FVector2D EndPoint = Gradient.EndPoint.Get(FVector2D(1.0f, 0.0f));
			const FString LinearParams = bHasLinearEndpoints
				? FString::Printf(
					TEXT("\"StartX\":%.4f,\"StartY\":%.4f,\"EndX\":%.4f,\"EndY\":%.4f,"),
					StartPoint.X,
					StartPoint.Y,
					EndPoint.X,
					EndPoint.Y)
				: FString();
			const FVector2D Center = Gradient.Center.Get(FVector2D(0.5f, 0.5f));
			const FVector2D Size = Gradient.Size.Get(FVector2D(1.0f, 1.0f));
			const FString RadialParams = FString::Printf(
				TEXT("\"CenterX\":%.4f,\"CenterY\":%.4f,\"SizeWidth\":%.4f,\"SizeHeight\":%.4f,\"Rotation\":%.4f"),
				Center.X,
				Center.Y,
				Size.X,
				Size.Y,
				Rotation);

			if (Gradient.Colors.Num() > 2)
			{
				FString Stops = Gradient.GradientType == EDesignGradientType::Radial
					? FString::Printf(TEXT("{%s,\"Stops\":["), *RadialParams)
					: FString::Printf(TEXT("{%s\"Rotation\":%.4f,\"Stops\":["), *LinearParams, Rotation);
				for (int32 Index = 0; Index < Gradient.Colors.Num(); ++Index)
				{
					if (Index > 0)
					{
						Stops += TEXT(",");
					}
					Stops += FString::Printf(TEXT("{\"Pos\":%.4f,\"Color\":\"%s\"}"),
						Gradient.Colors[Index].Position, *Gradient.Colors[Index].Color);
				}
				Stops += TEXT("]}");
				return Stops;
			}

			if (Gradient.GradientType == EDesignGradientType::Radial)
			{
				return FString::Printf(TEXT("{%s,\"ColorCenter\":\"%s\",\"ColorEdge\":\"%s\"}"), *RadialParams, *StartColor, *EndColor);
			}

			return FString::Printf(
				TEXT("{%s\"Rotation\":%.4f,\"ColorStart\":\"%s\",\"ColorEnd\":\"%s\"}"),
				*LinearParams,
				Rotation,
				*StartColor,
				*EndColor);
		}

		void ApplyTypedGradientBrushData(FUMGVisualBrushData& Brush, const FDesignGradient& Gradient)
		{
			Brush.Gradient.Type = ToTypedGradientTypeString(Gradient.GradientType);
			Brush.Gradient.Rotation = Gradient.Rotation;
			if (Gradient.StartPoint.IsSet())
			{
				Brush.Gradient.StartPoint = Gradient.StartPoint.GetValue();
			}
			if (Gradient.EndPoint.IsSet())
			{
				Brush.Gradient.EndPoint = Gradient.EndPoint.GetValue();
			}
			if (Gradient.Center.IsSet())
			{
				Brush.Gradient.Center = Gradient.Center.GetValue();
			}
			if (Gradient.Size.IsSet())
			{
				Brush.Gradient.Size = Gradient.Size.GetValue();
			}
			Brush.Gradient.Stops.Reset();
			for (const FDesignGradientStop& Stop : Gradient.Colors)
			{
				FUMGVisualBrushData::FGradientStopData& TypedStop = Brush.Gradient.Stops.AddDefaulted_GetRef();
				TypedStop.Color = FUMGValueParser::ParseColor(Stop.Color);
				TypedStop.Position = Stop.Position;
			}
		}

		FString ResolveGradientMaterialRef(const FDesignGradient& Gradient)
		{
			switch (Gradient.GradientType)
			{
			case EDesignGradientType::Radial:
				return MatRef_Radial;
			case EDesignGradientType::Angular:
				return MatRef_Angular;
			case EDesignGradientType::Linear:
			default:
				return MatRef_GradientLinear;
			}
		}
	}

	bool HasVisualMapperPropertyPresence(const FUMGWidgetNode& Widget, const TCHAR* Key)
	{
		return Design2UMGWidgetNodeAccess::HasWidgetPropertyPresence(Widget, Key);
	}

	void AddTextureSizeHints(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->Transform.Width.IsFixed() && Node->Transform.Width.Value > 0.0f)
		{
			Widget.WidgetStyle.WidthOverride = Node->Transform.Width.Value;
		}
		if (Node->Transform.Height.IsFixed() && Node->Transform.Height.Value > 0.0f)
		{
			Widget.WidgetStyle.HeightOverride = Node->Transform.Height.Value;
		}
	}

	FUMGWidgetNode MakeMultiFillOverlayShell(const FUMGWidgetNode& Widget)
	{
		FUMGWidgetNode Overlay;
		Overlay.Type = TEXT("Overlay");
		Overlay.Id = Widget.Id;
		Overlay.Name = Widget.Name;
		Overlay.Bounds = Widget.Bounds;
		Overlay.Slot = Widget.Slot;
		return Overlay;
	}

	FUMGWidgetNode MakeMultiFillLayerShell(const FUMGWidgetNode& Widget, const int32 FillIndex)
	{
		FUMGWidgetNode Layer;
		Layer.Id = FString::Printf(TEXT("%s_fill%d"), *Widget.Id, FillIndex);
		Layer.Name = FString::Printf(TEXT("%s_fill%d"), *Widget.Name, FillIndex);
		Layer.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Layer.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
		return Layer;
	}

	void PrepareMultiFillContentWidget(FUMGWidgetNode& Widget)
	{
		Widget.Id += TEXT("_content");
		Widget.Name += TEXT("_content");
		Widget.Bounds = FDesignPixelBounds();
		Widget.Slot = FUMGSlotData();
		Widget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Widget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
	}

	void ApplyMultiFillImageChildOpacity(
		FUMGWidgetNode& ImageChild,
		const FDesignFill& Fill,
		const FVisualMapperOptions& Options)
	{
		const bool bLuminosityFit = Fill.BlendMode.Equals(TEXT("LUMINOSITY"), ESearchCase::IgnoreCase);
		if (bLuminosityFit)
		{
			const float EffectiveOpacity = FMath::Min(Fill.Image.Opacity, Options.LuminosityFitOpacityCap);
			ImageChild.Render.RenderOpacity = EffectiveOpacity;
		}
		else if (Fill.Image.Opacity < 1.0f)
		{
			ImageChild.Render.RenderOpacity = Fill.Image.Opacity;
		}
	}

	bool TryBuildMultiFillLayer(
		FUMGWidgetNode& Layer,
		const FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FDesignFill& Fill,
		const int32 FillIndex,
		const FVisualMapperOptions& Options)
	{
		if (!Fill.bEnabled || Fill.Type == EDesignFillType::None)
		{
			return false;
		}

		if (Fill.Type == EDesignFillType::Color)
		{
			Layer.Type = TEXT("Border");
			Layer.Brush.BrushColor = FUMGValueParser::ParseColor(Fill.SolidColor);
			return true;
		}

		if (Fill.Type == EDesignFillType::Gradient && Fill.Gradient.Colors.Num() > 0)
		{
			Layer.Type = TEXT("Image");
			Layer.Brush.MaterialRef = GradientHelpers::ResolveGradientMaterialRef(Fill.Gradient);
			GradientHelpers::ApplyTypedGradientBrushData(Layer.Brush, Fill.Gradient);
			const FString MaterialParams = GradientHelpers::BuildGradientMaterialParams(Fill.Gradient);
			if (!MaterialParams.IsEmpty())
			{
				Layer.Brush.MaterialParams = MaterialParams;
			}
			AddTextureSizeHints(Layer, Node);
			return true;
		}

		if (Fill.Type != EDesignFillType::Image || Fill.Image.Url.IsEmpty())
		{
			return false;
		}

		const bool bSquareNode =
			Node->Transform.Width.Value > 0.0f
			&& Node->Transform.Height.Value > 0.0f
			&& FMath::IsNearlyEqual(Node->Transform.Width.Value, Node->Transform.Height.Value, 1.0f);
		if (Fill.Image.Mode == EDesignImageMode::Fit)
		{
			Layer.Type = TEXT("ScaleBox");
			Layer.WidgetStyle.Stretch = TEXT("ScaleToFit");
			Layer.WidgetStyle.StretchDirection = TEXT("Both");

			FUMGWidgetNode ImageChild;
			ImageChild.Type = TEXT("Image");
			ImageChild.Id = FString::Printf(TEXT("%s_fill%d_img"), *Widget.Id, FillIndex);
			ImageChild.Name = FString::Printf(TEXT("%s_fill%d_img"), *Widget.Name, FillIndex);
			ImageChild.Brush.Image.TexturePath = Fill.Image.Url;
			ApplyMultiFillImageChildOpacity(ImageChild, Fill, Options);
			if (bSquareNode)
			{
				AddTextureSizeHints(ImageChild, Node);
			}
			Layer.Children.Add(MoveTemp(ImageChild));
			return true;
		}

		Layer.Type = TEXT("Image");
		Layer.Brush.Image.TexturePath = Fill.Image.Url;
		ImageFillUtils::ApplyImageFillProperties(Layer, Fill.Image);
		AddTextureSizeHints(Layer, Node);
		return true;
	}
}

namespace VisualMapperFillTransfer
{
	void ApplyFillProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return;
		}
		const FDesignFill& PrimaryFill = Node->Visual.PrimaryFill();
		if (!PrimaryFill.bEnabled)
		{
			return;
		}

		if (Node->HasMultiFill())
		{
			if ((Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
				&& PrimaryFill.Type == EDesignFillType::Color
				&& !HasVisualMapperPropertyPresence(Widget, UMGPropertyKeys::Color))
			{
				Widget.Text.Color = FUMGValueParser::ParseColor(PrimaryFill.SolidColor);
			}
			return;
		}

		if (PrimaryFill.Type == EDesignFillType::Color)
		{
			if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
			{
				if (!HasVisualMapperPropertyPresence(Widget, UMGPropertyKeys::Color))
				{
					Widget.Text.Color = FUMGValueParser::ParseColor(PrimaryFill.SolidColor);
				}
				return;
			}

			Widget.Brush.BrushColor = FUMGValueParser::ParseColor(PrimaryFill.SolidColor);
			return;
		}

		if (PrimaryFill.Type == EDesignFillType::Gradient && PrimaryFill.Gradient.Colors.Num() > 0)
		{
			Widget.Brush.MaterialRef = GradientHelpers::ResolveGradientMaterialRef(PrimaryFill.Gradient);
			GradientHelpers::ApplyTypedGradientBrushData(Widget.Brush, PrimaryFill.Gradient);
			const FString MaterialParams = GradientHelpers::BuildGradientMaterialParams(PrimaryFill.Gradient);
			if (!MaterialParams.IsEmpty())
			{
				Widget.Brush.MaterialParams = MaterialParams;
			}
			AddTextureSizeHints(Widget, Node);
			return;
		}

		if (PrimaryFill.Type == EDesignFillType::Image && !PrimaryFill.Image.Url.IsEmpty())
		{
			ImageFillUtils::ApplyImageFillProperties(Widget, PrimaryFill.Image);
			AddTextureSizeHints(Widget, Node);
		}
	}

	FUMGWidgetNode WrapWithMultiFill(
		FUMGWidgetNode&& Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FVisualMapperOptions& Options)
	{
		if (!Node.IsValid() || Node->Visual.Fills.Num() <= 1)
		{
			return MoveTemp(Widget);
		}

		FUMGWidgetNode Overlay = MakeMultiFillOverlayShell(Widget);
		for (int32 FillIndex = 0; FillIndex < Node->Visual.Fills.Num(); ++FillIndex)
		{
			const FDesignFill& Fill = Node->Visual.Fills[FillIndex];
			FUMGWidgetNode Layer = MakeMultiFillLayerShell(Widget, FillIndex);
			if (!TryBuildMultiFillLayer(Layer, Widget, Node, Fill, FillIndex, Options))
			{
				continue;
			}

			if (Node->Visual.CornerRadius.HasRadius())
			{
				VisualMapper::ApplyRoundedCorner(
					Layer,
					Node->Visual.CornerRadius,
					Node->Transform.Width.Value,
					Node->Transform.Height.Value);
			}

			Overlay.Children.Add(MoveTemp(Layer));
		}

		Overlay.Render = Widget.Render;
		Widget.Render = FUMGRenderStyleData();
		PrepareMultiFillContentWidget(Widget);
		Overlay.Children.Add(MoveTemp(Widget));
		return Overlay;
	}
}
