// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapperFillTransfer.h"
#include "UMGRetainerCapture.h"

#include "Design2UMGGeneratedNameSuffixes.h"
#include "DesignRoundedContour.h"
#include "ImageFillUtils.h"
#include "Design2UMGLog.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetTypes.h"
#include "Misc/FileHelper.h"
#include "PencilShaderSyntax.h"

namespace
{
    bool NonNormalBlend(const FString& Blend)
    {
        return !Blend.IsEmpty() && VisualMapperFillTransfer::CanonicalPaintBlendMode(Blend) != TEXT("normal");
    }

    bool ShaderNeedsBackdrop(const FDesignShaderData& Shader)
    {
        if (NonNormalBlend(Shader.BlendMode))
            return true;
        FString Source;
        if (Shader.Url.IsEmpty() || !FFileHelper::LoadFileToString(Source, *Shader.Url))
            return false;
        PencilShaderSyntax::FProgram Program;
        std::string Error;
        if (!PencilShaderSyntax::PrepareSource(TCHAR_TO_UTF8(*Source), Program, Error))
            return false;
        for (const auto& Uniform : Program.Uniforms)
            if (Uniform.Type == "sampler2D" && !Uniform.Sdf
                && PencilShaderSyntax::ResolveStringUniform(Shader, Uniform) == TEXT("pencil:background"))
                return true;
        return false;
    }

    bool PaintNeedsBackdrop(const FUMGWidgetNode& Widget)
    {
        if ((Widget.Brush.Shader.IsSet() && ShaderNeedsBackdrop(Widget.Brush.Shader.GetValue()))
            || (Widget.Effect.Shader.IsSet() && ShaderNeedsBackdrop(Widget.Effect.Shader.GetValue()))
            || NonNormalBlend(Widget.Render.PaintBlendMode.Get(FString()))
            || (Widget.Effect.EffectMaterial.Get(FString()) == UMGPropertyKeys::RetainerSubtreeShadowEffectRef
                && NonNormalBlend(Widget.Effect.InnerShadowBlendMode.Get(FString()))))
            return true;
        return Widget.Shadow.BoxShadows.ContainsByPredicate(
            [](const FUMGBoxShadowLayerData& Shadow) { return NonNormalBlend(Shadow.BlendMode); });
    }

    bool SubtreeNeedsBackdrop(const FUMGWidgetNode& Widget)
    {
        if (PaintNeedsBackdrop(Widget))
            return true;
        for (const auto& Child : Widget.Children)
            if (SubtreeNeedsBackdrop(Child))
                return true;
        return false;
    }

	FString MakeRoundedRectanglePath(const FVector2D& Size, const FVector4& Radii, const float Inset)
	{
		const float TL = FMath::Max(0.0f, Radii.X - Inset), TR = FMath::Max(0.0f, Radii.Y - Inset);
		const float BR = FMath::Max(0.0f, Radii.Z - Inset), BL = FMath::Max(0.0f, Radii.W - Inset);
		const float Right = Size.X - Inset, Bottom = Size.Y - Inset;
		return FString::Printf(TEXT("M%f %f L%f %f A%f %f 0 0 1 %f %f L%f %f A%f %f 0 0 1 %f %f L%f %f A%f %f 0 0 1 %f %f L%f %f A%f %f 0 0 1 %f %f Z"),
			Inset + TL, Inset, Right - TR, Inset, TR, TR, Right, Inset + TR,
			Right, Bottom - BR, BR, BR, Right - BR, Bottom,
			Inset + BL, Bottom, BL, BL, Inset, Bottom - BL,
			Inset, Inset + TL, TL, TL, Inset + TL, Inset);
	}

	namespace GradientHelpers
	{

		const TCHAR* ToTypedGradientTypeString(const EDesignGradientType Type)
		{
			switch (Type)
			{
			case EDesignGradientType::Mesh: return TEXT("Mesh");
			case EDesignGradientType::Radial:
				return TEXT("Radial");
			case EDesignGradientType::Angular:
				return TEXT("Angular");
			case EDesignGradientType::Linear:
			default:
				return TEXT("Linear");
			}
		}
	}

	void AddTextureSizeHints(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (Widget.Brush.Image.CropFitMode.IsSet() || Widget.Brush.Gradient.Mesh.IsSet() || Widget.Brush.Shader.IsSet())
		{
			// Measured paint sizes also belong to synthetic layers;
			// keep their raster extent separate from layout overrides.
			Widget.Bounds.W = Node->Transform.Width.Value;
			Widget.Bounds.H = Node->Transform.Height.Value;
		}
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
		Overlay.Type = UMGWidgetTypes::Overlay;
		Overlay.Id = Widget.Id;
		Overlay.Name = Widget.Name;
		Overlay.Bounds = Widget.Bounds;
		Overlay.Slot = Widget.Slot;
		return Overlay;
	}

	FUMGWidgetNode MakeMultiFillLayerShell(const FUMGWidgetNode& Widget, const int32 FillIndex)
	{
		FUMGWidgetNode Layer;
		const FString FillSuffix = Design2UMGGeneratedNameSuffixes::Fill(FillIndex);
		Layer.Id = Widget.Id + FillSuffix;
		Layer.Name = Widget.Name + FillSuffix;
		Layer.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Layer.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
		return Layer;
	}

	void PrepareMultiFillContentWidget(FUMGWidgetNode& Widget)
	{
		Widget.Id += Design2UMGGeneratedNameSuffixes::Content();
		Widget.Name += Design2UMGGeneratedNameSuffixes::Content();
		Widget.Bounds = FDesignPixelBounds();
		Widget.Slot = FUMGSlotData();
		Widget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Widget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
	}

	bool HasMultiFillBorderPaintBeyondTransferredLayers(const FUMGVisualBrushData& Brush)
	{
		const bool bHasVisibleBrushColor = Brush.BrushColor.IsSet()
			&& !Brush.BrushColor.GetValue().Equals(FLinearColor::Transparent);
		return bHasVisibleBrushColor
			|| Brush.OutlineThickness.IsSet()
			|| Brush.Margin.IsSet()
			|| Brush.OutlineColor.IsSet()
			|| Brush.MaterialRef.IsSet()
			|| Brush.MaterialParams.IsSet()
			|| Brush.Shape.IsSet()
			|| Brush.Image.HasAny()
			|| Brush.Gradient.HasAny();
	}

	bool CanElideMultiFillBorder(
		const FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& Widget.Type == UMGWidgetTypes::Border
			&& Widget.Children.Num() <= 1
			&& !Node->Visual.Stroke.HasStroke()
			&& !Widget.HasSourceData()
			&& !Widget.WidgetStyle.HasAny()
			&& Widget.TextStyle.FontSize == 0.0f
			&& Widget.TextStyle.SourceFontSize == 0.0f
			&& !Widget.TextStyle.LetterSpacing.IsSet()
			&& !Widget.Text.HasAny()
			&& !HasMultiFillBorderPaintBeyondTransferredLayers(Widget.Brush)
			&& !Widget.Effect.HasAny()
			&& !Widget.Path.HasAny()
			&& !Widget.Shadow.HasAny()
			&& !Widget.Render.HasAny()
			&& !Widget.WidgetState.HasAny()
			&& !Widget.ScrollBox.HasAny();
	}

	bool CanElideEmptyMultiFillContent(const FUMGWidgetNode& Widget)
	{
		return Widget.Children.Num() == 0
			&& !Widget.HasSourceData()
			&& !Widget.WidgetStyle.HasAny()
			&& !HasMultiFillBorderPaintBeyondTransferredLayers(Widget.Brush)
			&& !Widget.Brush.DrawAs.IsSet()
			&& !Widget.Brush.CornerRadii.IsSet()
			&& Widget.TextStyle.FontSize == 0.0f
			&& Widget.TextStyle.SourceFontSize == 0.0f
			&& !Widget.TextStyle.LetterSpacing.IsSet()
			&& !Widget.Text.HasAny()
			&& !Widget.Effect.HasAny()
			&& !Widget.Path.HasAny()
			&& !Widget.Shadow.HasAny()
			&& !Widget.Render.HasAny()
			&& !Widget.WidgetState.HasAny()
			&& !Widget.ScrollBox.HasAny();
	}

	bool PrepareMultiFillContentForAppend(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node)
	{
		if (CanElideMultiFillBorder(Widget, Node))
		{
			if (Widget.Children.Num() == 0)
			{
				return false;
			}

			const FString ContentId = Widget.Id;
			const FString ContentName = Widget.Name;
			FUMGWidgetNode PromotedContent = MoveTemp(Widget.Children[0]);
			Widget = MoveTemp(PromotedContent);
			Widget.Id = ContentId;
			Widget.Name = ContentName;
		}
		else if (CanElideEmptyMultiFillContent(Widget))
		{
			return false;
		}

		PrepareMultiFillContentWidget(Widget);
		return true;
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

        if(Fill.Type==EDesignFillType::Shader && Fill.Shader.IsSet())
        {
            Layer.Type=UMGWidgetTypes::Image;
            Layer.Brush.Shader=Fill.Shader;
            Layer.Brush.Shader->bFillPaint = true;
            Layer.Brush.Shader->CaptureInsets = UMGRetainerCapture::FillInsets(*Node);
            Layer.Brush.Shader->Size=FVector2D(Node->Transform.Width.Value,Node->Transform.Height.Value);
            Layer.Path=Widget.Path;
            Layer.Brush.OutlineCornerRadius = Widget.Brush.OutlineCornerRadius;
            Layer.Brush.CornerRadii = Widget.Brush.CornerRadii;
            if(Layer.Path.Geometry.IsSet()) Layer.Path.FillColor=FLinearColor::White;
            AddTextureSizeHints(Layer,Node);
            return true;
        }
		if (Widget.Path.Geometry.IsSet())
		{
			Layer.Type = UMGWidgetTypes::Image;
			Layer.Path.Geometry = Widget.Path.Geometry;
			Layer.Path.ViewBox = Widget.Path.ViewBox;
			Layer.Path.SourcePaintGeometry = Widget.Path.SourcePaintGeometry;
			Layer.Path.FitGeometryToBounds = Widget.Path.FitGeometryToBounds;
			Layer.Path.CanvasSize = Widget.Path.CanvasSize;
			Layer.Path.FillRule = Widget.Path.FillRule;
			Layer.Path.FillColor = FLinearColor::White;
			if (Fill.Type == EDesignFillType::Color)
				Layer.Path.FillColor = FUMGValueParser::ParseColor(Fill.SolidColor, Fill.Opacity);
			else if (Fill.Type == EDesignFillType::Gradient)
				VisualMapperFillTransfer::ApplyGradientBrushData(Layer.Brush, Fill.Gradient, Fill.Opacity);
			else if (Fill.Type == EDesignFillType::Image && !Fill.Image.Url.IsEmpty())
			{
				ImageFillUtils::ApplyImageFillProperties(Layer, Fill.Image);
				Layer.Brush.Image.TexturePath = Fill.Image.Url;
				ApplyMultiFillImageChildOpacity(Layer, Fill, Options);
			}
			else return false;
			AddTextureSizeHints(Layer, Node);
			return true;
		}

		if (Fill.Type == EDesignFillType::Color)
		{
			Layer.Type = UMGWidgetTypes::Border;
			Layer.Brush.BrushColor = FUMGValueParser::ParseColor(Fill.SolidColor, Fill.Opacity);
			return true;
		}

		if (Fill.Type == EDesignFillType::Gradient && Fill.Gradient.HasColors())
		{
			Layer.Type = UMGWidgetTypes::Image;
			VisualMapperFillTransfer::ApplyGradientBrushData(Layer.Brush, Fill.Gradient, Fill.Opacity);
			AddTextureSizeHints(Layer, Node);
			return true;
		}

		if (Fill.Type != EDesignFillType::Image || Fill.Image.Url.IsEmpty())
		{
			return false;
		}

		if (Fill.Image.Mode == EDesignImageMode::Fit && !Fill.Image.bFitCropToNode)
		{
			Layer.Type = UMGWidgetTypes::ScaleBox;
			Layer.WidgetStyle.Stretch = TEXT("ScaleToFit");
			Layer.WidgetStyle.StretchDirection = TEXT("Both");

			FUMGWidgetNode ImageChild;
			ImageChild.Type = UMGWidgetTypes::Image;
			const FString FillImageSuffix = Design2UMGGeneratedNameSuffixes::FillImage(FillIndex);
			ImageChild.Id = Widget.Id + FillImageSuffix;
			ImageChild.Name = Widget.Name + FillImageSuffix;
			ImageChild.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Center;
			ImageChild.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Center;
			ImageChild.Brush.Image.TexturePath = Fill.Image.Url;
			ApplyMultiFillImageChildOpacity(ImageChild, Fill, Options);
			Layer.Children.Add(MoveTemp(ImageChild));
			return true;
		}

		Layer.Type = UMGWidgetTypes::Image;
		Layer.Brush.Image.TexturePath = Fill.Image.Url;
		ImageFillUtils::ApplyImageFillProperties(Layer, Fill.Image);
		AddTextureSizeHints(Layer, Node);
		return true;
	}
}

namespace VisualMapperFillTransfer
{
    FString CanonicalPaintBlendMode(const FString& Mode)
    {
        static const TPair<const TCHAR*, const TCHAR*> FigmaModes[] = {
            {TEXT("NORMAL"), TEXT("normal")}, {TEXT("DARKEN"), TEXT("darken")},
            {TEXT("MULTIPLY"), TEXT("multiply")}, {TEXT("LINEAR_BURN"), TEXT("linearBurn")},
            {TEXT("COLOR_BURN"), TEXT("colorBurn")}, {TEXT("LIGHTEN"), TEXT("light")},
            {TEXT("SCREEN"), TEXT("screen")}, {TEXT("LINEAR_DODGE"), TEXT("linearDodge")},
            {TEXT("COLOR_DODGE"), TEXT("colorDodge")}, {TEXT("OVERLAY"), TEXT("overlay")},
            {TEXT("SOFT_LIGHT"), TEXT("softLight")}, {TEXT("HARD_LIGHT"), TEXT("hardLight")},
            {TEXT("DIFFERENCE"), TEXT("difference")}, {TEXT("EXCLUSION"), TEXT("exclusion")},
            {TEXT("HUE"), TEXT("hue")}, {TEXT("SATURATION"), TEXT("saturation")},
            {TEXT("COLOR"), TEXT("color")}, {TEXT("LUMINOSITY"), TEXT("luminosity")}
        };
        FString FigmaMode = Mode;
        FigmaMode.ReplaceInline(TEXT("-"), TEXT("_"));
        for (const auto& Pair : FigmaModes)
            if (FigmaMode.Equals(Pair.Key, ESearchCase::CaseSensitive))
                return Pair.Value;
        return Mode;
    }

    void MarkBackdropProducers(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
    {
        if (!Node.IsValid() || !SubtreeNeedsBackdrop(Widget)) return;
        TMap<FString, const FDesignNode*> Sources;
        TFunction<void(const FDesignNode&)> AddSource = [&](const FDesignNode& Source)
        {
            Sources.Add(Source.Id, &Source);
            for (const auto& Child : Source.Children) if (Child.IsValid()) AddSource(*Child);
        };
        AddSource(*Node);
        TFunction<void(FUMGWidgetNode&, const FDesignNode&, bool)> Mark =
            [&](FUMGWidgetNode& Parent, const FDesignNode& Scope, bool CapturedBranch)
        {
            const FDesignNode* Source = Sources.FindRef(Parent.Id);
            const FDesignNode& Current = Source ? *Source : Scope;
            const bool OpacityCapture = Parent.Render.RenderOpacity.IsSet()
                && Parent.Render.RenderOpacity.GetValue() != 1.0f
                && (SubtreeNeedsBackdrop(Parent) || (CapturedBranch && !Parent.Children.IsEmpty()));
            if (OpacityCapture)
                Parent.Render.BackgroundCaptureInsets = BackgroundCaptureInsets(Current);
            const bool CaptureChildren = CapturedBranch || PaintNeedsBackdrop(Parent) || OpacityCapture;
            bool HasLaterBackdrop = false;
            for (int32 Index = Parent.Children.Num() - 1; Index >= 0; --Index)
            {
                FUMGWidgetNode& Child = Parent.Children[Index];
                const FDesignNode* ChildSource = Sources.FindRef(Child.Id);
                const FDesignNode& ChildScope = ChildSource ? *ChildSource : Current;
                if (HasLaterBackdrop)
                    Child.Render.BackgroundCaptureInsets = BackgroundCaptureInsets(ChildScope);
                Mark(Child, ChildScope, CaptureChildren || HasLaterBackdrop);
                HasLaterBackdrop |= SubtreeNeedsBackdrop(Child);
            }
        };
        Mark(Widget, *Node, false);
    }

    bool RequiresBackdropLayers(const FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
    {
        if (!Node.IsValid()) return false;
        // Glyph color is painted by its native text widget, not a rectangular fill layer.
        if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont) return false;
        for (const auto& Fill : Node->Visual.Fills)
            if (Fill.bEnabled && (NonNormalBlend(Fill.BlendMode) || (Fill.Shader.IsSet() && ShaderNeedsBackdrop(Fill.Shader.GetValue())))) return true;
        for (const auto& Effect : Node->Visual.Effects)
            if (Effect.bEnabled && ((Effect.Type == EDesignEffectType::Shadow && !Effect.Shadow.bInner && NonNormalBlend(Effect.BlendMode))
                || (Effect.Shader.IsSet() && ShaderNeedsBackdrop(Effect.Shader.GetValue())))) return true;
        const bool Paints = !Node->Visual.Fills.IsEmpty() || Node->Visual.Stroke.HasStroke() || !Widget.Shadow.BoxShadows.IsEmpty();
        return Paints && SubtreeNeedsBackdrop(Widget);
    }

	void ApplyGradientBrushData(FUMGVisualBrushData& Brush, const FDesignGradient& Gradient, float Opacity)
	{
		Brush.Gradient.Type = GradientHelpers::ToTypedGradientTypeString(Gradient.GradientType);
		Brush.Gradient.Rotation = Gradient.Rotation;
		Brush.Gradient.Mesh = Gradient.Mesh;
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
		Brush.Gradient.AngularBasis = Gradient.AngularBasis;
		for (const FDesignGradientStop& Stop : Gradient.Colors)
		{
			FUMGVisualBrushData::FGradientStopData& TypedStop = Brush.Gradient.Stops.AddDefaulted_GetRef();
			TypedStop.Color = FUMGValueParser::ParseColor(Stop.Color, Opacity);
			TypedStop.Position = Stop.Position;
		}
	}

	void ApplyFillProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return;
		}
		const FDesignFill& PrimaryFill = Node->Visual.TopmostFill();
		if (!PrimaryFill.bEnabled)
		{
			return;
		}
        if ((Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
            && PrimaryFill.Type == EDesignFillType::Color && NonNormalBlend(PrimaryFill.BlendMode))
            Widget.Render.PaintBlendMode = CanonicalPaintBlendMode(PrimaryFill.BlendMode);

		if (Node->HasMultiFill())
		{
			if ((Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
				&& PrimaryFill.Type == EDesignFillType::Color
				&& !Widget.Text.Color.IsSet())
			{
				Widget.Text.Color = FUMGValueParser::ParseColor(PrimaryFill.SolidColor, PrimaryFill.Opacity);
			}
			return;
		}

        if(PrimaryFill.Type==EDesignFillType::Shader && PrimaryFill.Shader.IsSet())
        {
            Widget.Brush.Shader=PrimaryFill.Shader;
            Widget.Brush.Shader->Size=FVector2D(Node->Transform.Width.Value,Node->Transform.Height.Value);
            AddTextureSizeHints(Widget,Node);
            return;
        }
		if (PrimaryFill.Type == EDesignFillType::Color)
		{
			if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
			{
				if (!Widget.Text.Color.IsSet())
				{
					Widget.Text.Color = FUMGValueParser::ParseColor(PrimaryFill.SolidColor, PrimaryFill.Opacity);
				}
				return;
			}

			// Path textures already own the fill color; applying a brush tint would multiply it twice.
			if (!Widget.Path.Geometry.IsSet())
			{
				Widget.Brush.BrushColor = FUMGValueParser::ParseColor(PrimaryFill.SolidColor, PrimaryFill.Opacity);
			}
			return;
		}

		if (PrimaryFill.Type == EDesignFillType::Gradient && PrimaryFill.Gradient.HasColors())
		{
			VisualMapperFillTransfer::ApplyGradientBrushData(Widget.Brush, PrimaryFill.Gradient, PrimaryFill.Opacity);
			AddTextureSizeHints(Widget, Node);
			return;
		}

		if (PrimaryFill.Type == EDesignFillType::Image && !PrimaryFill.Image.Url.IsEmpty())
		{
			ImageFillUtils::ApplyImageFillProperties(Widget, PrimaryFill.Image);
			AddTextureSizeHints(Widget, Node);
		}
	}

	bool PrepareSourceContourPaint(FUMGWidgetNode& Widget, const FUMGEffectData& Contour, int32 PaintLayerCount, FString& Error)
	{
		if (PaintLayerCount == 0) return true;
		FUMGSourcePaintGeometry Source;
		Source.Geometry.FillPaths = Contour.GeometryMaskPaths.GetValue();
		Source.Geometry.StrokePaths = TArray<FDesignGeometryPath>();
		Source.FillPaints = TArray<FUMGSurfacePaint>();
		FUMGSurfacePaint Stroke;
		Stroke.Color = FLinearColor::Transparent;
		Source.StrokePaint = Stroke;
		for (int32 Index = 0; Index < PaintLayerCount; ++Index)
		{
			const auto& Layer = Widget.Children[Index];
			if ((Layer.Type != UMGWidgetTypes::Image && Layer.Type != UMGWidgetTypes::Border) || Layer.Brush.Shader.IsSet()
				|| (!Layer.Render.PaintBlendMode.Get(FString()).IsEmpty() && Layer.Render.PaintBlendMode.GetValue() != TEXT("normal")))
			{ Error = TEXT("source contour raster paint requires native normal-blend Image/Border paint layers"); return false; }
			FUMGSurfacePaint Paint;
			Paint.Color = Layer.Brush.BrushColor.Get(FLinearColor::White);
			Paint.Color.A *= Layer.Render.RenderOpacity.Get(1.0f);
			Paint.Image = Layer.Brush.Image;
			Paint.Gradient = Layer.Brush.Gradient;
			Source.FillPaints->Add(MoveTemp(Paint));
		}
		FUMGWidgetNode Paint;
		Paint.Type = UMGWidgetTypes::Image;
		Paint.Id = Widget.Id + TEXT("__contour_paints");
		Paint.Name = Widget.Name + TEXT("__contour_paints");
		Paint.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Paint.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
		Paint.Effect.GeometryMaskPaths = Contour.GeometryMaskPaths;
		Paint.Effect.GeometryMaskSize = Contour.GeometryMaskSize;
		Paint.Path.CanvasSize = Contour.GeometryMaskSize;
		Paint.Path.ViewBox = FVector4(0, 0, Contour.GeometryMaskSize->X, Contour.GeometryMaskSize->Y);
		Paint.Path.Geometry = Contour.GeometryMaskPaths.GetValue()[0].Path;
		Paint.Path.SourcePaintGeometry = MoveTemp(Source);
		Paint.Path.FillColor = FLinearColor::White;
		Paint.Brush.BrushColor = FLinearColor::White;
		Widget.Children.RemoveAt(0, PaintLayerCount);
		Widget.Children.Insert(MoveTemp(Paint), 0);
		return true;
	}

    FUMGWidgetNode WrapWithPaintedStroke(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, int32 PaintLayerCount)
    {
        if (!Node->Visual.Stroke.HasStroke() || !Node->Visual.Stroke.Fill.IsSet())
            return MoveTemp(Widget);
        const FDesignFill& StrokeFill = Node->Visual.Stroke.Fill.GetValue();
        const bool bHasSourceStrokePaths = Node->Shape.PaintGeometry.StrokePaths.IsSet();
        const bool bShaderStroke = StrokeFill.Type == EDesignFillType::Shader;
        // Generated shadow/fill layers precede the original content surface.
        FUMGWidgetNode* StrokeCarrier = &Widget;
        if (PaintLayerCount > 0 && Widget.Children.IsValidIndex(PaintLayerCount))
            StrokeCarrier = &Widget.Children[PaintLayerCount];
        if (StrokeFill.Type != EDesignFillType::Gradient && StrokeFill.Type != EDesignFillType::Shader &&
            StrokeFill.Type != EDesignFillType::Color && StrokeFill.Type != EDesignFillType::Image)
            return MoveTemp(Widget);
        TOptional<FUMGPathData> StrokeMask;
        if (!Node->Shape.PaintGeometry.StrokePaths.IsSet() && StrokeCarrier->Path.Geometry.IsSet())
        {
            StrokeMask = StrokeCarrier->Path;
            StrokeMask->FillColor.Reset();
            StrokeMask->StrokeColor = FLinearColor::White;
            StrokeMask->StrokeGradient = FUMGVisualBrushData::FGradientData();
        }
        TArray<FDesignGeometryPath> Paths;
        bool bUsesSourceStrokeGeometry = false;
        const bool bRectangularShape = Node->Type == EDesignNodeType::Rectangle || Node->Type == EDesignNodeType::Frame;
        const bool bCanonicalOutline = Node->Visual.CornerRadius.Smoothing == 0.0f && Node->Visual.Stroke.Thickness.bUniform &&
            (bRectangularShape || Node->Type == EDesignNodeType::Line) &&
            !bHasSourceStrokePaths;
        const float Thickness = Node->Visual.Stroke.Thickness.Uniform;
        FVector2D PaintSize(FMath::Max(1.0f, Node->Transform.Width.Value),
                            FMath::Max(1.0f, Node->Transform.Height.Value));
        FVector2D PaintOffset = FVector2D::ZeroVector;
        const FVector4 RawRadii = Node->Visual.CornerRadius.ToVector4();
        const float RadiusLimit = FMath::Min(Node->Transform.Width.Value, Node->Transform.Height.Value) / 2.0f;
        const FVector4 Radii(FMath::Clamp(RawRadii.X, 0.0f, RadiusLimit), FMath::Clamp(RawRadii.Y, 0.0f, RadiusLimit),
                             FMath::Clamp(RawRadii.Z, 0.0f, RadiusLimit), FMath::Clamp(RawRadii.W, 0.0f, RadiusLimit));

        if (VisualMapper::RequiresSourceContour(*Node) && !bHasSourceStrokePaths)
        {
            FUMGPathData Mask;
            Mask.CanvasSize = FVector2D(Node->Shape.ViewBox->Z, Node->Shape.ViewBox->W);
            Mask.ViewBox = Node->Shape.ViewBox;
            const auto& FillPaths = Node->Shape.PaintGeometry.FillPaths.GetValue();
            FString Geometry;
            for (const auto& Path : FillPaths) Geometry += Path.Path + TEXT(" ");
            Mask.Geometry = MoveTemp(Geometry);
            Mask.FillColor = FLinearColor::Transparent;
            Mask.StrokeColor = FLinearColor::White;
            if (Node->Visual.Stroke.Thickness.bUniform)
            {
                Mask.StrokeThickness = Thickness;
                Mask.StrokeAlignment = Node->Visual.Stroke.Align == EDesignStrokeAlign::Inside ? TEXT("inner")
                    : (Node->Visual.Stroke.Align == EDesignStrokeAlign::Outside ? TEXT("outer") : TEXT("center"));
            }
            else
            {
                FDesignGeometryPath Region;
                FString Error;
                // BuildSourceContour validates this supported source edge junction before conversion.
                if (!DesignRoundedContour::BuildInsideStroke(Mask.CanvasSize.GetValue(), Node->Visual.CornerRadius,
                        Node->Visual.Stroke.Thickness, Region, Error))
                {
                    UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *Error);
                    return MoveTemp(Widget);
                }
                Mask.Geometry = Region.Path;
                Mask.FillRule = Region.FillRule;
                Mask.FillColor = FLinearColor::White;
                Mask.StrokeColor.Reset();
            }
            StrokeMask = MoveTemp(Mask);
        }
        if (StrokeMask.IsSet())
        {
            Paths = {{StrokeMask->Geometry.GetValue(), StrokeMask->FillRule.Get(TEXT("nonzero"))}};
        }
        else if (bCanonicalOutline && Node->Type != EDesignNodeType::Line)
        {
            FUMGPathData Mask;
            Mask.Geometry = MakeRoundedRectanglePath(PaintSize, Radii, 0.0f);
            Mask.FillRule = TEXT("nonzero");
            Mask.CanvasSize = PaintSize;
            Mask.StrokeColor = FLinearColor::White;
            Mask.StrokeThickness = Thickness;
            Mask.StrokeAlignment = Node->Visual.Stroke.Align == EDesignStrokeAlign::Inside    ? TEXT("inner")
                                   : Node->Visual.Stroke.Align == EDesignStrokeAlign::Outside ? TEXT("outer")
                                                                                              : TEXT("center");
            Mask.StrokeCap = Node->Visual.Stroke.Cap == EDesignStrokeCap::Circular ? TEXT("round")
                             : Node->Visual.Stroke.Cap == EDesignStrokeCap::Square ? TEXT("square")
                                                                                   : TEXT("butt");
            Mask.StrokeJoin = Node->Visual.Stroke.Join == EDesignStrokeJoin::Miter   ? TEXT("miter")
                              : Node->Visual.Stroke.Join == EDesignStrokeJoin::Bevel ? TEXT("bevel")
                                                                                     : TEXT("round");
            StrokeMask = MoveTemp(Mask);
            Paths = {{StrokeMask->Geometry.GetValue(), TEXT("nonzero")}};
        }
        else if (bCanonicalOutline)
        {
            const float Half = Thickness / 2.0f;
            const float CapExtent = Node->Visual.Stroke.Cap == EDesignStrokeCap::Circular ||
                                            Node->Visual.Stroke.Cap == EDesignStrokeCap::Square
                                        ? Half
                                        : 0.0f;
            const float LineWidth = PaintSize.X;
            // An integral canvas keeps both raster axes at the same supersample scale.
            PaintSize = FVector2D(FMath::CeilToFloat(LineWidth + 2.0f * CapExtent), FMath::CeilToFloat(Thickness));
            PaintOffset = FVector2D((LineWidth - PaintSize.X) * 0.5f, -PaintSize.Y * 0.5f);
            Paths = {{FString::Printf(TEXT("M%f %f L%f %f"), -PaintOffset.X, PaintSize.Y * 0.5f,
                LineWidth - PaintOffset.X, PaintSize.Y * 0.5f), TEXT("nonzero")}};
        }
        else
        {
            Paths = Node->Shape.PaintGeometry.StrokePaths.Get(TArray<FDesignGeometryPath>());
            bUsesSourceStrokeGeometry = true;
        }
        if (Paths.IsEmpty())
        {
            UE_LOG(LogDesign2UMG, Warning,
                   TEXT("Painted stroke '%s' requires a source contour or exported stroke geometry."), *Node->Name);
            return MoveTemp(Widget);
        }
        FUMGSourcePaintGeometry SourcePaintGeometry;
        const TOptional<FVector4> SourceViewBox = Node->Shape.ViewBox;
        if (bUsesSourceStrokeGeometry)
        {
            SourcePaintGeometry.Geometry = Node->Shape.PaintGeometry;
            SourcePaintGeometry.StrokeAlignment = Node->Visual.Stroke.Align == EDesignStrokeAlign::Inside ? TEXT("inner")
                : (Node->Visual.Stroke.Align == EDesignStrokeAlign::Outside ? TEXT("outer") : TEXT("center"));
            if (Widget.Path.Geometry.IsSet()) Widget.Path.SourcePaintGeometry = SourcePaintGeometry;
            if (Node->HasMultiFill())
                for (auto& Layer : Widget.Children)
                    if (Layer.Path.Geometry.IsSet()) Layer.Path.SourcePaintGeometry = SourcePaintGeometry;
        }
        StrokeCarrier->Brush.OutlineThickness.Reset();
        StrokeCarrier->Brush.OutlineColor.Reset();
        if (bUsesSourceStrokeGeometry)
        {
            StrokeCarrier->WidgetStyle.StrokePadding.Reset();
            StrokeCarrier->WidgetStyle.StrokeMargin.Reset();
        }
        StrokeCarrier->Path.StrokeColor.Reset();
        StrokeCarrier->Path.StrokeThickness.Reset();
        StrokeCarrier->Path.StrokeGradient = FUMGVisualBrushData::FGradientData();
        StrokeCarrier->Path.StrokeGeometry.Reset();
        StrokeCarrier->Path.StrokeAlignment.Reset();
        StrokeCarrier->Path.StrokeCap.Reset();
        StrokeCarrier->Path.StrokeJoin.Reset();
        if (StrokeCarrier != &Widget && StrokeCarrier->Path.Geometry.IsSet())
        {
            // Generated layers own the fills; only child content can remain on the path carrier.
            StrokeCarrier->Path = FUMGPathData();
            if (StrokeCarrier->Children.IsEmpty())
                Widget.Children.RemoveAt(PaintLayerCount);
        }
        const bool bStrokeOnly = Node->Children.IsEmpty() && Node->Visual.Fills.IsEmpty();
        const bool bLine = !StrokeMask.IsSet() && bCanonicalOutline && Node->Type == EDesignNodeType::Line;
        const FVector2D LayoutSize = bLine ? FVector2D(PaintSize.X + 2.0f * PaintOffset.X, Thickness) : PaintSize;
        const auto ApplyLineCanvas = [&](FUMGWidgetNode& Layer)
        {
            if (!bLine)
                return;
            Layer.Path.CanvasSize = PaintSize;
            Layer.WidgetStyle.WidthOverride = LayoutSize.X;
            Layer.WidgetStyle.HeightOverride = LayoutSize.Y;
            // Expand ink around the layout center without changing the slot's desired width.
            const FVector2D Expansion = PaintSize / LayoutSize;
            const FVector2D SourceScale = Layer.Render.RenderTransformScale.Get(FVector2D(1.0f, 1.0f));
            const FVector2D Pivot = Layer.Render.RenderTransformPivot.Get(FVector2D(0.5f, 0.5f));
            const FVector2D PivotCorrection =
                (Expansion - FVector2D(1.0f, 1.0f)) * (Pivot - FVector2D(0.5f, 0.5f)) * LayoutSize * SourceScale;
            const float Angle = FMath::DegreesToRadians(Layer.Render.RenderAngle.Get(0.0f));
            const FVector2D RotatedCorrection(
                PivotCorrection.X * FMath::Cos(Angle) - PivotCorrection.Y * FMath::Sin(Angle),
                PivotCorrection.X * FMath::Sin(Angle) + PivotCorrection.Y * FMath::Cos(Angle));
            Layer.Render.RenderTransformScale = SourceScale * Expansion;
            Layer.Render.RenderTranslation =
                Layer.Render.RenderTranslation.Get(FVector2D::ZeroVector) + RotatedCorrection;
        };
        FUMGWidgetNode Overlay = MakeMultiFillOverlayShell(Widget);
        Overlay.Render = MoveTemp(Widget.Render);
        Overlay.Shadow = MoveTemp(Widget.Shadow);
        Widget.Shadow = FUMGShadowData();
        Widget.Render = FUMGRenderStyleData();
        if (!bStrokeOnly)
        {
            PrepareMultiFillContentWidget(Widget);
            Widget.Id = Overlay.Id + TEXT("__stroke_content");
            Widget.Name = Overlay.Name + TEXT("__stroke_content");
            Overlay.Children.Add(MoveTemp(Widget));
        }
        const FDesignGeometryPath& Path = Paths[0];
        FUMGWidgetNode Layer;
        Layer.Type = UMGWidgetTypes::Image;
        const FString Suffix = TEXT("_stroke0");
        Layer.WidgetStyle.Visibility = TEXT("HitTestInvisible");
        Layer.Id = Overlay.Id + Suffix;
        Layer.Name = Overlay.Name + Suffix;
        Layer.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
        Layer.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
        Layer.Path.Geometry = Path.Path;
        Layer.Path.FillRule = Path.FillRule;
        if (bUsesSourceStrokeGeometry)
        {
            Layer.Path.ViewBox = SourceViewBox;
            Layer.Path.CanvasSize = PaintSize;
            Layer.Path.SourcePaintGeometry = SourcePaintGeometry;
        }
        if (StrokeMask.IsSet())
            Layer.Path = StrokeMask.GetValue();
        else if (bCanonicalOutline)
        {
            Layer.Path.StrokeColor = FLinearColor::White;
            Layer.Path.StrokeThickness = Thickness;
            Layer.Path.StrokeCap =
                Node->Visual.Stroke.Cap == EDesignStrokeCap::Circular
                    ? TEXT("round")
                    : (Node->Visual.Stroke.Cap == EDesignStrokeCap::Square ? TEXT("square") : TEXT("butt"));
        }
        else
            Layer.Path.FillColor = FLinearColor::White;
        Layer.Render.RenderTranslation = bLine ? FVector2D(0.0f, -LayoutSize.Y * 0.5f) : PaintOffset;
        if (!bShaderStroke && NonNormalBlend(StrokeFill.BlendMode))
            Layer.Render.PaintBlendMode = CanonicalPaintBlendMode(StrokeFill.BlendMode);
        if (bShaderStroke)
        {
            Layer.Brush.Shader = StrokeFill.Shader;
            Layer.Brush.Shader->Size =
                bLine ? LayoutSize : FVector2D(Node->Transform.Width.Value, Node->Transform.Height.Value);
            if (bLine)
                Layer.Brush.Shader->CaptureInsets = FVector4(-PaintOffset.X, 0, -PaintOffset.X, 0);
        }
        else if (StrokeFill.Type == EDesignFillType::Gradient)
            VisualMapperFillTransfer::ApplyGradientBrushData(Layer.Brush, StrokeFill.Gradient, StrokeFill.Opacity);
        else if (StrokeFill.Type == EDesignFillType::Image)
        {
            ImageFillUtils::ApplyImageFillProperties(Layer, StrokeFill.Image);
            Layer.Brush.Image.TexturePath = StrokeFill.Image.Url;
        }
        else
            Layer.Brush.BrushColor = FUMGValueParser::ParseColor(StrokeFill.SolidColor, StrokeFill.Opacity);
        Layer.WidgetStyle.WidthOverride = PaintSize.X;
        Layer.WidgetStyle.HeightOverride = PaintSize.Y;
        if (bLine && PaintOffset.X != 0.0f &&
            Layer.Brush.Gradient.Type.Get(FString()).Equals(TEXT("Linear"), ESearchCase::IgnoreCase) &&
            (!Layer.Brush.Gradient.StartPoint.IsSet() || !Layer.Brush.Gradient.EndPoint.IsSet()))
        {
            // Convert an angle gradient to the same projection in the unpadded paint box.
            const float Angle = FMath::DegreesToRadians(Layer.Brush.Gradient.Rotation.Get(0.0f));
            const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
            const float Minimum = FMath::Min(0.0f, static_cast<float>(Direction.X)) +
                                  FMath::Min(0.0f, static_cast<float>(Direction.Y));
            const float Range = FMath::Abs(Direction.X) + FMath::Abs(Direction.Y);
            const FVector2D Projection = Direction / LayoutSize / Range;
            const FVector2D Span = Projection / Projection.SizeSquared();
            const FVector2D Start = Span * (Minimum / Range);
            Layer.Brush.Gradient.StartPoint = (Start + (PaintSize - LayoutSize) * 0.5f) / PaintSize;
            Layer.Brush.Gradient.EndPoint = (Start + Span + (PaintSize - LayoutSize) * 0.5f) / PaintSize;
        }
        else
        {
            if (Layer.Brush.Gradient.StartPoint.IsSet())
                Layer.Brush.Gradient.StartPoint =
                    (Layer.Brush.Gradient.StartPoint.GetValue() *
                         FVector2D(Node->Transform.Width.Value, Node->Transform.Height.Value) -
                     PaintOffset) /
                    PaintSize;
            if (Layer.Brush.Gradient.EndPoint.IsSet())
                Layer.Brush.Gradient.EndPoint =
                    (Layer.Brush.Gradient.EndPoint.GetValue() *
                         FVector2D(Node->Transform.Width.Value, Node->Transform.Height.Value) -
                     PaintOffset) /
                    PaintSize;
        }
        if (bLine && Layer.Brush.Gradient.Type.Get(FString()).Equals(TEXT("Radial"), ESearchCase::IgnoreCase))
        {
            FVector2D Center = Layer.Brush.Gradient.Center.Get(FVector2D(0.5f, 0.5f));
            FVector2D Size = Layer.Brush.Gradient.Size.Get(FVector2D(1.0f, 1.0f));
            Center = (Center * LayoutSize + (PaintSize - LayoutSize) * 0.5f) / PaintSize;
            Size *= LayoutSize / PaintSize;
            Layer.Brush.Gradient.Center = Center;
            Layer.Brush.Gradient.Size = Size;
        }
        if (Layer.Brush.Gradient.AngularBasis.IsSet())
        {
            const FVector2D SourceSize =
                bLine ? LayoutSize : FVector2D(Node->Transform.Width.Value, Node->Transform.Height.Value);
            const FVector2D Origin = bLine ? (PaintSize - LayoutSize) * 0.5f : -PaintOffset;
            Layer.Brush.Gradient.TransformAngularPaintSpace(SourceSize / PaintSize, Origin / PaintSize);
        }
        if (bUsesSourceStrokeGeometry)
        {
            auto& Source = Layer.Path.SourcePaintGeometry.GetValue();
            Source.FillPaints = TArray<FUMGSurfacePaint>();
            FUMGSurfacePaint Stroke;
            Stroke.Color = FLinearColor::White;
            Swap(Stroke.Gradient, Layer.Brush.Gradient);
            Swap(Stroke.Image, Layer.Brush.Image);
            Source.StrokePaint = MoveTemp(Stroke);
        }
        if (bStrokeOnly)
        {
            Layer.Id = Overlay.Id;
            Layer.Name = Overlay.Name;
            Layer.Bounds = Overlay.Bounds;
            Layer.Slot = Overlay.Slot;
            Layer.WidgetStyle.Visibility = Widget.WidgetStyle.Visibility;
            auto PaintBlendMode = MoveTemp(Layer.Render.PaintBlendMode);
            Layer.Render = MoveTemp(Overlay.Render);
            Layer.Render.PaintBlendMode = MoveTemp(PaintBlendMode);
            Layer.Render.RenderTranslation = Layer.Render.RenderTranslation.Get(FVector2D::ZeroVector) +
                                             (bLine ? FVector2D(0.0f, -LayoutSize.Y * 0.5f) : PaintOffset);
            ApplyLineCanvas(Layer);
            Layer.Shadow = MoveTemp(Overlay.Shadow);
            return Layer;
        }
        ApplyLineCanvas(Layer);
        Overlay.Children.Add(MoveTemp(Layer));
        return Overlay;
    }

    FUMGWidgetNode WrapWithMultiFill(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node,
                                     const FVisualMapperOptions& Options, int32* OutPaintLayerCount)
    {
        if (OutPaintLayerCount) *OutPaintLayerCount = 0;
        const bool BackdropLayers = RequiresBackdropLayers(Widget, Node);
        if (!Node.IsValid() || (!VisualMapper::RequiresSourceContour(*Node) && !BackdropLayers && Node->Visual.Fills.Num() <= 1 && !Node->Visual.Fills.ContainsByPredicate(
            [](const FDesignFill& Fill) { return Fill.bEnabled && Fill.Type == EDesignFillType::Shader; })))
        {
            return MoveTemp(Widget);
        }

        FUMGWidgetNode Overlay = MakeMultiFillOverlayShell(Widget);
        const bool ShaderFill = Node->Visual.Fills.ContainsByPredicate(
            [](const FDesignFill& Fill) { return Fill.bEnabled && Fill.Type == EDesignFillType::Shader; });
        if ((ShaderFill || (BackdropLayers && !Node->Visual.Fills.IsEmpty())) && !Widget.Shadow.BoxShadows.IsEmpty())
        {
            FUMGWidgetNode ShadowSurface;
            ShadowSurface.Type = UMGWidgetTypes::Overlay;
            ShadowSurface.Id = Widget.Id + TEXT("_fillshadow");
            ShadowSurface.Name = Widget.Name + TEXT("_fillshadow");
            ShadowSurface.Path = Widget.Path;
            ShadowSurface.Path.FillColor.Reset();
            ShadowSurface.Path.StrokeColor.Reset();
            ShadowSurface.Path.StrokeGradient = FUMGVisualBrushData::FGradientData();
            ShadowSurface.Path.StrokeThickness.Reset();
            ShadowSurface.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
            ShadowSurface.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
            FUMGWidgetNode ShadowPaint = VisualMapper::WrapWithBoxShadow(MoveTemp(ShadowSurface), Node, Widget.Shadow.BoxShadows);
        ShadowPaint.Render = FUMGRenderStyleData();
        Overlay.Children.Add(MoveTemp(ShadowPaint));
            Widget.Shadow.BoxShadows.Reset();
            if (OutPaintLayerCount) ++*OutPaintLayerCount;
        }
        else
            Overlay.Shadow.BoxShadows = MoveTemp(Widget.Shadow.BoxShadows);
        for (int32 FillIndex = 0; FillIndex < Node->Visual.Fills.Num(); ++FillIndex)
        {
            const FDesignFill& Fill = Node->Visual.Fills[FillIndex];
            FUMGWidgetNode Layer = MakeMultiFillLayerShell(Widget, FillIndex);
            if (!TryBuildMultiFillLayer(Layer, Widget, Node, Fill, FillIndex, Options))
            {
                continue;
            }

            if (!VisualMapper::RequiresSourceContour(*Node) && !Layer.Path.Geometry.IsSet() && Node->Visual.CornerRadius.HasRadius())
            {
                VisualMapper::ApplyRoundedCorner(Layer, Node->Visual.CornerRadius, Node->Transform.Width.Value,
                                                 Node->Transform.Height.Value);
            }

            if (NonNormalBlend(Fill.BlendMode) && Fill.Type != EDesignFillType::Shader)
                Layer.Render.PaintBlendMode = CanonicalPaintBlendMode(Fill.BlendMode);
            Overlay.Children.Add(MoveTemp(Layer));
            if (OutPaintLayerCount) ++*OutPaintLayerCount;
        }

        Overlay.Render = Widget.Render;
        Widget.Render = FUMGRenderStyleData();
        Widget.Brush.Shader.Reset();
        if (!Node->Visual.Fills.IsEmpty())
        {
            // Paint layers own the fills; the content carrier keeps only its independent outline.
            Widget.Brush.BrushColor = FLinearColor::Transparent;
            Widget.Brush.Image = FUMGVisualBrushData::FImageData();
            Widget.Brush.Gradient = FUMGVisualBrushData::FGradientData();
        }
        if (Widget.Path.Geometry.IsSet())
        {
            // Fill layers own every fill; retain only the independent stroke or child content.
            Widget.Path.FillColor.Reset();
            Widget.Brush.Image = FUMGVisualBrushData::FImageData();
            Widget.Brush.Gradient = FUMGVisualBrushData::FGradientData();
        Widget.Brush.BrushColor.Reset();
            if (!Widget.Path.StrokeColor.IsSet() && !Widget.Path.StrokeGradient.HasAny())
            {
                Widget.Path = FUMGPathData();
                if (Widget.Children.IsEmpty())
                    return Overlay;
            }
        }
        if (PrepareMultiFillContentForAppend(Widget, Node))
        {
            Overlay.Children.Add(MoveTemp(Widget));
        }
        return Overlay;
    }
    } // namespace VisualMapperFillTransfer
