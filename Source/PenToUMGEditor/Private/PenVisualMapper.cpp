// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenVisualMapper.h"
#include "PenLayoutMapper.h"
#include "PenToUMGLog.h"
#include "UMGWidgetUtils.h"

namespace
{
	FString BuildGradientMaterialParams(const FPenGradient& Gradient)
	{
		if (Gradient.Colors.Num() == 0)
			return FString();

		const FString& StartColor = Gradient.Colors[0].Color;
		const FString& EndColor = Gradient.Colors.Last().Color;

		if (Gradient.GradientType == EPenGradientType::Radial)
			return FString::Printf(TEXT("{\"ColorCenter\":\"%s\",\"ColorEdge\":\"%s\"}"), *StartColor, *EndColor);

		return FString::Printf(TEXT("{\"ColorStart\":\"%s\",\"ColorEnd\":\"%s\"}"), *StartColor, *EndColor);
	}

	void AddTextureSizeHints(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
	{
		if (Node->Width.IsFixed() && Node->Width.Value > 0.0f)
			Widget.Properties.Add(TEXT("WidthOverride"), FString::FromInt((int32)Node->Width.Value));
		if (Node->Height.IsFixed() && Node->Height.Value > 0.0f)
			Widget.Properties.Add(TEXT("HeightOverride"), FString::FromInt((int32)Node->Height.Value));
	}

	void ApplyFillProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
	{
		if (!Node.IsValid() || !Node->Fill.bEnabled)
			return;

		if (Node->Fill.Type == EPenFillType::Color)
		{
			Widget.Properties.Add(TEXT("BrushColor"), Node->Fill.SolidColor);
			Widget.Brush.BrushColor = FUMGWidgetUtils::ParseColor(Node->Fill.SolidColor);
			Widget.Brush.bHasBrushColor = true;
			if (Node->Type == EPenNodeType::Text)
				Widget.Properties.Add(TEXT("Color"), Node->Fill.SolidColor);
			return;
		}

		if (Node->Fill.Type == EPenFillType::Gradient && Node->Fill.Gradient.Colors.Num() > 0)
		{
			FString MaterialRef = TEXT("gradienth");
			switch (Node->Fill.Gradient.GradientType)
			{
			case EPenGradientType::Radial:
			case EPenGradientType::Angular:
				MaterialRef = TEXT("radialgradient");
				break;
			case EPenGradientType::Linear:
			default:
			{
				float Rotation = FMath::Fmod(Node->Fill.Gradient.Rotation, 360.0f);
				if (Rotation < 0.0f)
					Rotation += 360.0f;
				const bool bVertical = (Rotation > 45.0f && Rotation < 135.0f) || (Rotation > 225.0f && Rotation < 315.0f);
				MaterialRef = bVertical ? TEXT("gradientv") : TEXT("gradienth");
				break;
			}
			}

			Widget.Properties.Add(TEXT("MaterialRef"), MaterialRef);
			Widget.Brush.MaterialRef = MaterialRef;
			const FString MaterialParams = BuildGradientMaterialParams(Node->Fill.Gradient);
			if (!MaterialParams.IsEmpty())
			{
				Widget.Properties.Add(TEXT("MaterialParams"), MaterialParams);
				Widget.Brush.MaterialParams = MaterialParams;
			}
			AddTextureSizeHints(Widget, Node);
			return;
		}

		if (Node->Fill.Type == EPenFillType::Image && !Node->Fill.Image.Url.IsEmpty())
		{
			Widget.Properties.Add(TEXT("TexturePath"), Node->Fill.Image.Url);
			Widget.Brush.TexturePath = Node->Fill.Image.Url;
			AddTextureSizeHints(Widget, Node);
		}
	}
}

void FPenVisualMapper::ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
{
	ApplyFillProperties(Widget, Node);

	if (Node->Opacity < 1.0f)
		Widget.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Node->Opacity));

	ApplyRoundedCorner(Widget, Node->CornerRadius);
	ApplyStroke(Widget, Node->Stroke);

	if (!FMath::IsNearlyZero(Node->Rotation))
		Widget.Properties.Add(TEXT("RenderAngle"), FString::SanitizeFloat(Node->Rotation));

	if (Node->bFlipX || Node->bFlipY)
	{
		float ScaleX = Node->bFlipX ? -1.0f : 1.0f;
		float ScaleY = Node->bFlipY ? -1.0f : 1.0f;
		Widget.Properties.Add(TEXT("RenderTransformScale"), FString::Printf(TEXT("%.1f,%.1f"), ScaleX, ScaleY));
	}

	if (Node->bClip)
		Widget.Properties.Add(TEXT("Clipping"), TEXT("ClipToBounds"));

	for (const FPenEffect& Effect : Node->Effects)
	{
		if (!Effect.bEnabled)
			continue;

		switch (Effect.Type)
		{
		case EPenEffectType::Blur:
			UE_LOG(LogPenToUMG, Warning,
				TEXT("PenToUMG: Blur effect on '%s' ignored (UMG has no foreground blur widget)"),
				*Node->Name);
			break;
		case EPenEffectType::Shadow:
			if (Node->Type == EPenNodeType::Text)
			{
				Widget.Properties.Add(TEXT("ShadowOffset"),
					FString::Printf(TEXT("%.1f,%.1f"), Effect.Shadow.OffsetX, Effect.Shadow.OffsetY));
				if (!Effect.Shadow.Color.IsEmpty())
					Widget.Properties.Add(TEXT("ShadowColorAndOpacity"), Effect.Shadow.Color);
			}
			else
			{
				UE_LOG(LogPenToUMG, Warning,
					TEXT("PenToUMG: Shadow effect on '%s' ignored (UMG only supports text shadows)"),
					*Node->Name);
			}
			break;
		default:
			break;
		}
	}
}

void FPenVisualMapper::ApplyRoundedCorner(FUMGWidgetNode& Widget, const FPenCornerRadius& Radius)
{
	if (Radius.bUniform)
	{
		if (Radius.Uniform > 0)
		{
			Widget.Properties.Add(TEXT("DrawAs"), TEXT("RoundedBox"));
			Widget.Properties.Add(TEXT("OutlineCornerRadius"), FString::SanitizeFloat(Radius.Uniform));
			Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			Widget.Brush.OutlineCornerRadius = Radius.Uniform;
		}
	}
	else if (Radius.TopLeft > 0 || Radius.TopRight > 0 || Radius.BottomRight > 0 || Radius.BottomLeft > 0)
	{
		Widget.Properties.Add(TEXT("DrawAs"), TEXT("RoundedBox"));
		Widget.Properties.Add(TEXT("CornerRadii"), FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"),
			Radius.TopLeft, Radius.TopRight, Radius.BottomRight, Radius.BottomLeft));
		Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Widget.Brush.CornerRadii = FVector4(Radius.TopLeft, Radius.TopRight, Radius.BottomRight, Radius.BottomLeft);
	}
}

void FPenVisualMapper::ApplyStroke(FUMGWidgetNode& Widget, const FPenStroke& Stroke)
{
	float Thickness = Stroke.Thickness.bUniform ? Stroke.Thickness.Uniform
		: FMath::Max(FMath::Max(Stroke.Thickness.Top, Stroke.Thickness.Bottom),
			FMath::Max(Stroke.Thickness.Left, Stroke.Thickness.Right));

	if (Thickness <= 0)
		return;

	if (!Widget.Properties.Contains(TEXT("DrawAs")))
	{
		Widget.Properties.Add(TEXT("DrawAs"), TEXT("RoundedBox"));
		if (Widget.Brush.DrawAs == ESlateBrushDrawType::NoDrawType)
			Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	}

	Widget.Properties.Add(TEXT("OutlineThickness"), FString::SanitizeFloat(Thickness));
	Widget.Brush.OutlineThickness = Thickness;

	if (Stroke.Fill.Type == EPenFillType::Color)
	{
		Widget.Properties.Add(TEXT("OutlineColor"), Stroke.Fill.SolidColor);
		Widget.Brush.OutlineColor = FUMGWidgetUtils::ParseColor(Stroke.Fill.SolidColor);
		Widget.Brush.bHasOutlineColor = true;
	}
}

FUMGWidgetNode FPenVisualMapper::WrapWithBorder(FUMGWidgetNode&& Widget, const TSharedPtr<FPenNode>& Node)
{
	FUMGWidgetNode Border;
	Border.Type = TEXT("Border");
	Border.Id = Widget.Id;
	Border.Name = Widget.Name;
	Border.Bounds = Widget.Bounds;

	// 转移 Brush typed 数据到 Border
	Border.Brush = Widget.Brush;
	Widget.Brush = FUMGBrushData();

	bool bHasBackgroundProperty = false;
	for (const FString& Key : {TEXT("BrushColor"), TEXT("TexturePath"), TEXT("MaterialRef"), TEXT("MaterialParams"), TEXT("WidthOverride"), TEXT("HeightOverride")})
	{
		if (const FString* Value = Widget.Properties.Find(Key))
		{
			Border.Properties.Add(Key, *Value);
			Widget.Properties.Remove(Key);
			bHasBackgroundProperty = true;
		}
	}
	if (!bHasBackgroundProperty)
	{
		Border.Properties.Add(TEXT("BrushColor"), TEXT("#00000000"));
		if (!Border.Brush.bHasBrushColor)
		{
			Border.Brush.BrushColor = FLinearColor::Transparent;
			Border.Brush.bHasBrushColor = true;
		}
	}

	if (FPenLayoutMapper::HasPadding(Node))
		FPenLayoutMapper::ApplyPadding(Border, Node->Padding);

	for (const FString& Key : {TEXT("DrawAs"), TEXT("OutlineCornerRadius"), TEXT("CornerRadii"), TEXT("OutlineThickness"), TEXT("OutlineColor")})
	{
		if (const FString* V = Widget.Properties.Find(Key))
		{
			Border.Properties.Add(Key, *V);
			Widget.Properties.Remove(Key);
		}
	}

	if (const FString* Opacity = Widget.Properties.Find(TEXT("RenderOpacity")))
	{
		Border.Properties.Add(TEXT("RenderOpacity"), *Opacity);
		Widget.Properties.Remove(TEXT("RenderOpacity"));
	}
	else if (Node->Opacity < 1.0f)
	{
		Border.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Node->Opacity));
	}

	Widget.Id = Widget.Id + TEXT("_inner");
	Widget.Name = Widget.Name + TEXT("_inner");
	Widget.Bounds = FBounds();
	Widget.Slot.HorizontalAlignment = TEXT("Fill");
	Widget.Slot.VerticalAlignment = TEXT("Fill");

	Border.Children.Add(MoveTemp(Widget));
	return Border;
}

FUMGWidgetNode FPenVisualMapper::WrapWithBackgroundBlur(FUMGWidgetNode&& Widget, float BlurStrength)
{
	FUMGWidgetNode Blur;
	Blur.Type = TEXT("BackgroundBlur");
	Blur.Id = Widget.Id + TEXT("_blur");
	Blur.Name = Widget.Name + TEXT("_blur");
	Blur.Bounds = Widget.Bounds;
	Blur.Slot = Widget.Slot;
	Blur.Properties.Add(TEXT("BlurStrength"), FString::SanitizeFloat(BlurStrength));

	Widget.Bounds = FBounds();
	Widget.Slot = FUMGSlotData();
	Widget.Slot.HorizontalAlignment = TEXT("Fill");
	Widget.Slot.VerticalAlignment = TEXT("Fill");

	Blur.Children.Add(MoveTemp(Widget));
	return Blur;
}
