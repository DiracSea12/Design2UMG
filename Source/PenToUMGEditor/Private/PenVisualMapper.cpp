// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

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
			Widget.Properties.Add(TEXT("WidthOverride"), FString::SanitizeFloat(Node->Width.Value));
		if (Node->Height.IsFixed() && Node->Height.Value > 0.0f)
			Widget.Properties.Add(TEXT("HeightOverride"), FString::SanitizeFloat(Node->Height.Value));
	}

	void ApplyFillProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
	{
		if (!Node.IsValid() || !Node->Fill.bEnabled)
			return;

		// 多层填充由 WrapWithMultiFill 统一处理，这里跳过背景填充
		// 但文本颜色仍需写入，否则 Text 节点会丢失字形颜色
		if (Node->HasMultiFill())
		{
			if (Node->Type == EPenNodeType::Text && Node->Fill.Type == EPenFillType::Color)
				Widget.Properties.Add(TEXT("Color"), Node->Fill.SolidColor);
			return;
		}

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

			// imageTransform → UMG UV 裁剪参数
			if (Node->Fill.Image.Mode == EPenImageMode::Crop && !Node->Fill.Image.Transform.IsIdentity())
			{
				const FPenImageTransform& T = Node->Fill.Image.Transform;
				Widget.Properties.Add(TEXT("ImageMode"), TEXT("Crop"));
				Widget.Properties.Add(TEXT("ImageTransform"),
					FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
						T.A, T.B, T.TX, T.C, T.D, T.TY));
			}
			else if (Node->Fill.Image.Mode == EPenImageMode::Fill)
			{
				Widget.Properties.Add(TEXT("ImageMode"), TEXT("Fill"));
			}
			else if (Node->Fill.Image.Mode == EPenImageMode::Fit)
			{
				Widget.Properties.Add(TEXT("ImageMode"), TEXT("Fit"));
			}

			if (Node->Fill.Image.Opacity < 1.0f)
				Widget.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Node->Fill.Image.Opacity));

			AddTextureSizeHints(Widget, Node);
		}
	}
}

void FPenVisualMapper::ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
{
	ApplyFillProperties(Widget, Node);

	if (Node->Opacity < 1.0f)
	{
		if (FString* Existing = Widget.Properties.Find(TEXT("RenderOpacity")))
		{
			// 图片填充层 opacity 已写入，与节点级 opacity 合并（乘积）
			const float Combined = Node->Opacity * FCString::Atof(**Existing);
			*Existing = FString::SanitizeFloat(Combined);
		}
		else
		{
			Widget.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Node->Opacity));
		}
	}

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
				Widget.Properties.Add(TEXT("BoxShadowOffsetX"), FString::SanitizeFloat(Effect.Shadow.OffsetX));
				Widget.Properties.Add(TEXT("BoxShadowOffsetY"), FString::SanitizeFloat(Effect.Shadow.OffsetY));
				Widget.Properties.Add(TEXT("BoxShadowBlur"), FString::SanitizeFloat(Effect.Shadow.Blur));
				Widget.Properties.Add(TEXT("BoxShadowSpread"), FString::SanitizeFloat(Effect.Shadow.Spread));
				Widget.Properties.Add(TEXT("BoxShadowColor"),
					Effect.Shadow.Color.IsEmpty() ? TEXT("#00000040") : Effect.Shadow.Color);
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
			Widget.Brush.bHasDrawAs = true;
			Widget.Brush.OutlineCornerRadius = Radius.Uniform;
		}
	}
	else if (Radius.TopLeft > 0 || Radius.TopRight > 0 || Radius.BottomRight > 0 || Radius.BottomLeft > 0)
	{
		Widget.Properties.Add(TEXT("DrawAs"), TEXT("RoundedBox"));
		Widget.Properties.Add(TEXT("CornerRadii"), FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"),
			Radius.TopLeft, Radius.TopRight, Radius.BottomRight, Radius.BottomLeft));
		Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Widget.Brush.bHasDrawAs = true;
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
		if (!Widget.Brush.bHasDrawAs)
		{
			Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			Widget.Brush.bHasDrawAs = true;
		}
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

	// 转移视觉画刷到 Border（TextStyle 留在原 Widget 上）
	Border.Brush = Widget.Brush;
	Widget.Brush = FUMGVisualBrushData();

	bool bHasBackgroundProperty = false;
	for (const FString& Key : {TEXT("BrushColor"), TEXT("TexturePath"), TEXT("MaterialRef"), TEXT("MaterialParams"), TEXT("WidthOverride"), TEXT("HeightOverride"), TEXT("ImageMode"), TEXT("ImageTransform")})
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

	// BoxShadow 属性上卷到 Border，确保 ConvertNode 能检测到并包裹阴影
	for (const FString& Key : {TEXT("BoxShadowOffsetX"), TEXT("BoxShadowOffsetY"), TEXT("BoxShadowBlur"), TEXT("BoxShadowSpread"), TEXT("BoxShadowColor")})
	{
		if (const FString* V = Widget.Properties.Find(Key))
		{
			Border.Properties.Add(Key, *V);
			Widget.Properties.Remove(Key);
		}
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

FUMGWidgetNode FPenVisualMapper::WrapWithBoxShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FPenNode>& Node)
{
	// 从 Widget 属性中提取阴影参数
	const FString* OffXStr = Widget.Properties.Find(TEXT("BoxShadowOffsetX"));
	const FString* OffYStr = Widget.Properties.Find(TEXT("BoxShadowOffsetY"));
	const FString* BlurStr = Widget.Properties.Find(TEXT("BoxShadowBlur"));
	const FString* SpreadStr = Widget.Properties.Find(TEXT("BoxShadowSpread"));
	const FString* ColorStr = Widget.Properties.Find(TEXT("BoxShadowColor"));

	if (!OffXStr || !OffYStr) return MoveTemp(Widget);

	float OffX = FCString::Atof(**OffXStr);
	float OffY = FCString::Atof(**OffYStr);
	float Blur = BlurStr ? FCString::Atof(**BlurStr) : 0.0f;
	float Spread = SpreadStr ? FCString::Atof(**SpreadStr) : 0.0f;
	FString ShadowColor = ColorStr ? *ColorStr : TEXT("#00000040");

	// 清除 Widget 上的 BoxShadow 属性
	Widget.Properties.Remove(TEXT("BoxShadowOffsetX"));
	Widget.Properties.Remove(TEXT("BoxShadowOffsetY"));
	Widget.Properties.Remove(TEXT("BoxShadowBlur"));
	Widget.Properties.Remove(TEXT("BoxShadowSpread"));
	Widget.Properties.Remove(TEXT("BoxShadowColor"));

	// Overlay 容纳阴影 Image + 原 Widget（Overlay 子节点的 Fill alignment 有效）
	FUMGWidgetNode Wrapper;
	Wrapper.Type = TEXT("Overlay");
	Wrapper.Id = Widget.Id;
	Wrapper.Name = Widget.Name;
	Wrapper.Bounds = Widget.Bounds;
	Wrapper.Slot = Widget.Slot;

	// 阴影 Image 节点
	float Extent = Blur + FMath::Max(0.0f, Spread);
	FUMGWidgetNode Shadow;
	Shadow.Type = TEXT("Image");
	Shadow.Id = Widget.Id + TEXT("_shadow");
	Shadow.Name = Widget.Name + TEXT("_shadow");
	Shadow.Properties.Add(TEXT("BrushColor"), ShadowColor);

	// 阴影纹理参数
	float CornerRadius = 0.0f;
	if (Node->CornerRadius.bUniform)
		CornerRadius = Node->CornerRadius.Uniform;
	else
		CornerRadius = (Node->CornerRadius.TopLeft + Node->CornerRadius.TopRight
			+ Node->CornerRadius.BottomRight + Node->CornerRadius.BottomLeft) * 0.25f;

	int32 TexW = FMath::Max(1, FMath::RoundToInt(Node->Width.Value + Extent * 2));
	int32 TexH = FMath::Max(1, FMath::RoundToInt(Node->Height.Value + Extent * 2));
	Shadow.Properties.Add(TEXT("WidthOverride"), FString::FromInt(TexW));
	Shadow.Properties.Add(TEXT("HeightOverride"), FString::FromInt(TexH));
	Shadow.Properties.Add(TEXT("MaterialRef"), TEXT("roundedrect"));

	float NormRadius = (TexW > 0 && TexH > 0) ? (CornerRadius + Extent) / (FMath::Min(TexW, TexH) * 0.5f) : 0.0f;
	Shadow.Properties.Add(TEXT("MaterialParams"),
		FString::Printf(TEXT("{\"Color\":\"%s\",\"CornerRadius\":%.3f}"), *ShadowColor, NormRadius));

	// 阴影偏移定位
	Shadow.Slot.HorizontalAlignment = TEXT("Fill");
	Shadow.Slot.VerticalAlignment = TEXT("Fill");
	float ShadowMarginLeft = OffX - Extent;
	float ShadowMarginTop = OffY - Extent;
	Shadow.Properties.Add(TEXT("RenderTranslation"),
		FString::Printf(TEXT("%.1f,%.1f"), ShadowMarginLeft, ShadowMarginTop));

	Wrapper.Children.Add(MoveTemp(Shadow));

	// 原 Widget 叠在阴影上方
	Widget.Id = Widget.Id + TEXT("_content");
	Widget.Name = Widget.Name + TEXT("_content");
	Widget.Bounds = FBounds();
	Widget.Slot = FUMGSlotData();
	Widget.Slot.HorizontalAlignment = TEXT("Fill");
	Widget.Slot.VerticalAlignment = TEXT("Fill");

	Wrapper.Children.Add(MoveTemp(Widget));
	return Wrapper;
}

FUMGWidgetNode FPenVisualMapper::WrapWithMultiFill(FUMGWidgetNode&& Widget, const TSharedPtr<FPenNode>& Node)
{
	if (!Node.IsValid() || Node->Fills.Num() <= 1)
		return MoveTemp(Widget);

	// Overlay 容纳多层填充 + 原 Widget 内容
	FUMGWidgetNode Overlay;
	Overlay.Type = TEXT("Overlay");
	Overlay.Id = Widget.Id;
	Overlay.Name = Widget.Name;
	Overlay.Bounds = Widget.Bounds;
	Overlay.Slot = Widget.Slot;

	// 每层填充创建一个 Image/Border 子节点（底→顶顺序）
	for (int32 i = 0; i < Node->Fills.Num(); ++i)
	{
		const FPenFill& Fill = Node->Fills[i];
		if (!Fill.bEnabled || Fill.Type == EPenFillType::None)
			continue;

		FUMGWidgetNode Layer;
		Layer.Id = FString::Printf(TEXT("%s_fill%d"), *Widget.Id, i);
		Layer.Name = FString::Printf(TEXT("%s_fill%d"), *Widget.Name, i);
		Layer.Slot.HorizontalAlignment = TEXT("Fill");
		Layer.Slot.VerticalAlignment = TEXT("Fill");

		if (Fill.Type == EPenFillType::Color)
		{
			Layer.Type = TEXT("Border");
			Layer.Properties.Add(TEXT("BrushColor"), Fill.SolidColor);
			Layer.Brush.BrushColor = FUMGWidgetUtils::ParseColor(Fill.SolidColor);
			Layer.Brush.bHasBrushColor = true;
		}
		else if (Fill.Type == EPenFillType::Gradient && Fill.Gradient.Colors.Num() > 0)
		{
			Layer.Type = TEXT("Image");
			FString MaterialRef = TEXT("gradienth");
			switch (Fill.Gradient.GradientType)
			{
			case EPenGradientType::Radial:
			case EPenGradientType::Angular:
				MaterialRef = TEXT("radialgradient");
				break;
			case EPenGradientType::Linear:
			default:
			{
				float Rotation = FMath::Fmod(Fill.Gradient.Rotation, 360.0f);
				if (Rotation < 0.0f) Rotation += 360.0f;
				const bool bVertical = (Rotation > 45.0f && Rotation < 135.0f) || (Rotation > 225.0f && Rotation < 315.0f);
				MaterialRef = bVertical ? TEXT("gradientv") : TEXT("gradienth");
				break;
			}
			}
			Layer.Properties.Add(TEXT("MaterialRef"), MaterialRef);
			Layer.Brush.MaterialRef = MaterialRef;
			const FString MaterialParams = BuildGradientMaterialParams(Fill.Gradient);
			if (!MaterialParams.IsEmpty())
			{
				Layer.Properties.Add(TEXT("MaterialParams"), MaterialParams);
				Layer.Brush.MaterialParams = MaterialParams;
			}
		}
		else if (Fill.Type == EPenFillType::Image && !Fill.Image.Url.IsEmpty())
		{
			Layer.Type = TEXT("Image");
			Layer.Properties.Add(TEXT("TexturePath"), Fill.Image.Url);
			Layer.Brush.TexturePath = Fill.Image.Url;

			if (Fill.Image.Mode == EPenImageMode::Crop && !Fill.Image.Transform.IsIdentity())
			{
				const FPenImageTransform& T = Fill.Image.Transform;
				Layer.Properties.Add(TEXT("ImageMode"), TEXT("Crop"));
				Layer.Properties.Add(TEXT("ImageTransform"),
					FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
						T.A, T.B, T.TX, T.C, T.D, T.TY));
			}
			else if (Fill.Image.Mode == EPenImageMode::Fill)
			{
				Layer.Properties.Add(TEXT("ImageMode"), TEXT("Fill"));
			}
			else if (Fill.Image.Mode == EPenImageMode::Fit)
			{
				Layer.Properties.Add(TEXT("ImageMode"), TEXT("Fit"));
			}

			if (Fill.Image.Opacity < 1.0f)
				Layer.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Fill.Image.Opacity));
		}
		else
		{
			continue;
		}

		// 圆角应用到每层
		if (Node->CornerRadius.HasRadius())
			ApplyRoundedCorner(Layer, Node->CornerRadius);

		Overlay.Children.Add(MoveTemp(Layer));
	}

	// 节点级渲染属性上卷到 Overlay
	for (const FString& Key : {TEXT("RenderOpacity"), TEXT("RenderAngle"), TEXT("RenderTransformScale"), TEXT("Clipping")})
	{
		if (const FString* V = Widget.Properties.Find(Key))
		{
			Overlay.Properties.Add(Key, *V);
			Widget.Properties.Remove(Key);
		}
	}

	// 原 Widget 叠在所有填充层上方
	Widget.Id = Widget.Id + TEXT("_content");
	Widget.Name = Widget.Name + TEXT("_content");
	Widget.Bounds = FBounds();
	Widget.Slot = FUMGSlotData();
	Widget.Slot.HorizontalAlignment = TEXT("Fill");
	Widget.Slot.VerticalAlignment = TEXT("Fill");

	Overlay.Children.Add(MoveTemp(Widget));
	return Overlay;
}
