// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapper.h"
#include "LayoutMapper.h"
#include "Design2UMGLog.h"
#include "UMGWidgetUtils.h"

namespace
{
	// 材质引用标识符（ProceduralShapeGenerator 内部 key，非资产路径）
	const TCHAR* MatRef_GradientH = TEXT("gradienth");
	const TCHAR* MatRef_GradientV = TEXT("gradientv");
	const TCHAR* MatRef_GradientLinear = TEXT("gradientlinear");
	const TCHAR* MatRef_Radial    = TEXT("radialgradient");
	const TCHAR* MatRef_Angular   = TEXT("angulargradient");
	const TCHAR* MatRef_RoundedRect = TEXT("roundedrect");

	FString BuildGradientMaterialParams(const FDesignGradient& Gradient)
	{
		if (Gradient.Colors.Num() == 0)
			return FString();

		const FString& StartColor = Gradient.Colors[0].Color;
		const FString& EndColor = Gradient.Colors.Last().Color;

		// Radial: center/edge pair
		if (Gradient.GradientType == EDesignGradientType::Radial)
			return FString::Printf(TEXT("{\"ColorCenter\":\"%s\",\"ColorEdge\":\"%s\"}"), *StartColor, *EndColor);

		// Linear: always include Rotation for angled rendering
		const float Rotation = Gradient.Rotation;

		// Multi-stop linear: output Stops array + Rotation
		if (Gradient.Colors.Num() > 2)
		{
			FString Stops = FString::Printf(TEXT("{\"Rotation\":%.4f,\"Stops\":["), Rotation);
			for (int32 i = 0; i < Gradient.Colors.Num(); ++i)
			{
				if (i > 0) Stops += TEXT(",");
				Stops += FString::Printf(TEXT("{\"Pos\":%.4f,\"Color\":\"%s\"}"),
					Gradient.Colors[i].Position, *Gradient.Colors[i].Color);
			}
			Stops += TEXT("]}");
			return Stops;
		}

		// 2-stop: include Rotation
		return FString::Printf(TEXT("{\"Rotation\":%.4f,\"ColorStart\":\"%s\",\"ColorEnd\":\"%s\"}"), Rotation, *StartColor, *EndColor);
	}

	void AddTextureSizeHints(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->Width.IsFixed() && Node->Width.Value > 0.0f)
			Widget.Properties.Add(TEXT("WidthOverride"), FString::SanitizeFloat(Node->Width.Value));
		if (Node->Height.IsFixed() && Node->Height.Value > 0.0f)
			Widget.Properties.Add(TEXT("HeightOverride"), FString::SanitizeFloat(Node->Height.Value));
	}

	void ApplyFillProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Node->Fill.bEnabled)
			return;

		// 多层填充由 WrapWithMultiFill 统一处理，这里跳过背景填充
		// 但文本颜色仍需写入，否则 Text 节点会丢失字形颜色
		if (Node->HasMultiFill())
		{
			if ((Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
				&& Node->Fill.Type == EDesignFillType::Color)
				Widget.Properties.FindOrAdd(TEXT("Color"), Node->Fill.SolidColor);
			return;
		}

		if (Node->Fill.Type == EDesignFillType::Color)
		{
			// Text / IconFont → 只设文字颜色，不设 BrushColor（避免被 Border 包裹后变色块）
			// CreateIconFontWidget/CreateTextWidget 可能已写入 Color，FindOrAdd 不覆盖已有值
			if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
			{
				Widget.Properties.FindOrAdd(TEXT("Color"), Node->Fill.SolidColor);
				return;
			}
			Widget.Properties.Add(TEXT("BrushColor"), Node->Fill.SolidColor);
			Widget.Brush.BrushColor = FUMGWidgetUtils::ParseColor(Node->Fill.SolidColor);
			Widget.Brush.bHasBrushColor = true;
			return;
		}

		if (Node->Fill.Type == EDesignFillType::Gradient && Node->Fill.Gradient.Colors.Num() > 0)
		{
			FString MaterialRef = MatRef_GradientH;
			switch (Node->Fill.Gradient.GradientType)
			{
			case EDesignGradientType::Radial:
				MaterialRef = MatRef_Radial;
				break;
			case EDesignGradientType::Angular:
				MaterialRef = MatRef_Angular;
				break;
			case EDesignGradientType::Linear:
			default:
				MaterialRef = MatRef_GradientLinear;
				break;
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

		if (Node->Fill.Type == EDesignFillType::Image && !Node->Fill.Image.Url.IsEmpty())
		{
			Widget.Properties.Add(TEXT("TexturePath"), Node->Fill.Image.Url);
			Widget.Brush.TexturePath = Node->Fill.Image.Url;

			// imageTransform → UMG UV 裁剪参数
			if (Node->Fill.Image.Mode == EDesignImageMode::Crop && !Node->Fill.Image.Transform.IsIdentity())
			{
				const FDesignImageTransform& T = Node->Fill.Image.Transform;
				Widget.Properties.Add(TEXT("ImageMode"), TEXT("Crop"));
				Widget.Properties.Add(TEXT("ImageTransform"),
					FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
						T.A, T.B, T.TX, T.C, T.D, T.TY));
			}
			else if (Node->Fill.Image.Mode == EDesignImageMode::Fill)
			{
				Widget.Properties.Add(TEXT("ImageMode"), TEXT("Fill"));
			}
			else if (Node->Fill.Image.Mode == EDesignImageMode::Fit)
			{
				Widget.Properties.Add(TEXT("ImageMode"), TEXT("Fit"));
			}

			if (Node->Fill.Image.Opacity < 1.0f)
				Widget.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Node->Fill.Image.Opacity));

			AddTextureSizeHints(Widget, Node);
		}
	}
}

void FVisualMapper::ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
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

	ApplyRoundedCorner(Widget, Node->CornerRadius, Node->Width.Value, Node->Height.Value);
	ApplyStroke(Widget, Node->Stroke);

	if (!FMath::IsNearlyZero(Node->Rotation))
		Widget.Properties.Add(TEXT("RenderAngle"), FString::SanitizeFloat(Node->Rotation));

	if (Node->bFlipX || Node->bFlipY)
	{
		float ScaleX = Node->bFlipX ? -1.0f : 1.0f;
		float ScaleY = Node->bFlipY ? -1.0f : 1.0f;
		Widget.Properties.Add(TEXT("RenderTransformScale"), FString::Printf(TEXT("%.1f,%.1f"), ScaleX, ScaleY));
	}

	if (Node->bHasClip && Node->bClip)
		Widget.Properties.Add(TEXT("Clipping"), TEXT("ClipToBounds"));

	int32 ShadowCount = 0;
	for (const FDesignEffect& Effect : Node->Effects)
	{
		if (!Effect.bEnabled)
			continue;

		switch (Effect.Type)
		{
		case EDesignEffectType::Blur:
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Design2UMG: Blur effect on '%s' ignored (UMG has no foreground blur widget)"),
				*Node->Name);
			break;
		case EDesignEffectType::Shadow:
			if (Effect.Shadow.bInner)
			{
				UE_LOG(LogDesign2UMG, Warning,
					TEXT("Design2UMG: Inner shadow on '%s' ignored (UMG has no inner shadow support)"),
					*Node->Name);
				break;
			}
			if (Node->Type == EDesignNodeType::Text)
			{
				// TextBlock 只支持单阴影（UE5 限制），后者覆盖前者
				Widget.Properties.Add(TEXT("ShadowOffset"),
					FString::Printf(TEXT("%.1f,%.1f"), Effect.Shadow.OffsetX, Effect.Shadow.OffsetY));
				Widget.Properties.Add(TEXT("ShadowColorAndOpacity"),
					Effect.Shadow.Color.IsEmpty() ? TEXT("#00000040") : Effect.Shadow.Color);
			}
			else
			{
				FString Idx = FString::FromInt(ShadowCount);
				Widget.Properties.Add(FString::Printf(TEXT("BoxShadow_%s_OffsetX"), *Idx), FString::SanitizeFloat(Effect.Shadow.OffsetX));
				Widget.Properties.Add(FString::Printf(TEXT("BoxShadow_%s_OffsetY"), *Idx), FString::SanitizeFloat(Effect.Shadow.OffsetY));
				Widget.Properties.Add(FString::Printf(TEXT("BoxShadow_%s_Blur"), *Idx), FString::SanitizeFloat(Effect.Shadow.Blur));
				Widget.Properties.Add(FString::Printf(TEXT("BoxShadow_%s_Spread"), *Idx), FString::SanitizeFloat(Effect.Shadow.Spread));
				Widget.Properties.Add(FString::Printf(TEXT("BoxShadow_%s_Color"), *Idx),
					Effect.Shadow.Color.IsEmpty() ? TEXT("#00000040") : Effect.Shadow.Color);
				++ShadowCount;
			}
			break;
		default:
			break;
		}
	}

	if (ShadowCount > 0)
		Widget.Properties.Add(TEXT("BoxShadowCount"), FString::FromInt(ShadowCount));
}

void FVisualMapper::ApplyRoundedCorner(FUMGWidgetNode& Widget, const FDesignCornerRadius& Radius, float Width, float Height)
{
	if (Radius.bUniform)
	{
		if (Radius.Uniform > 0)
		{
			Widget.Properties.Add(TEXT("DrawAs"), TEXT("RoundedBox"));
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
			Widget.Properties.Add(TEXT("OutlineCornerRadius"), FString::SanitizeFloat(ExportRadius));
			Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			Widget.Brush.bHasDrawAs = true;
			Widget.Brush.OutlineCornerRadius = ExportRadius;
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

void FVisualMapper::ApplyStroke(FUMGWidgetNode& Widget, const FDesignStroke& Stroke)
{
	float Thickness = Stroke.Thickness.bUniform ? Stroke.Thickness.Uniform
		: FMath::Max(FMath::Max(Stroke.Thickness.Top, Stroke.Thickness.Bottom),
			FMath::Max(Stroke.Thickness.Left, Stroke.Thickness.Right));

	if (Thickness <= 0)
		return;

	if (!Widget.Properties.Contains(TEXT("DrawAs")))
	{
		Widget.Properties.Add(TEXT("DrawAs"), TEXT("Box"));
		if (!Widget.Brush.bHasDrawAs)
		{
			Widget.Brush.DrawAs = ESlateBrushDrawType::Box;
			Widget.Brush.bHasDrawAs = true;
		}
	}

	Widget.Properties.Add(TEXT("OutlineThickness"), FString::SanitizeFloat(Thickness));
	Widget.Brush.OutlineThickness = Thickness;

	if (Stroke.Fill.Type == EDesignFillType::Color)
	{
		Widget.Properties.Add(TEXT("OutlineColor"), Stroke.Fill.SolidColor);
		Widget.Brush.OutlineColor = FUMGWidgetUtils::ParseColor(Stroke.Fill.SolidColor);
		Widget.Brush.bHasOutlineColor = true;
	}

	// Stroke alignment compensation:
	// UMG outline is always centered on the edge. Adjust padding to simulate inside/outside.
	if (Stroke.Align == EDesignStrokeAlign::Inside)
	{
		// Inside: entire stroke within bounds → add padding = Thickness/2
		float Inset = Thickness * 0.5f;
		Widget.Properties.Add(TEXT("StrokePadding"),
			FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), Inset, Inset, Inset, Inset));
	}
	else if (Stroke.Align == EDesignStrokeAlign::Outside)
	{
		// Outside: entire stroke outside bounds → expand by Thickness/2
		float Outset = Thickness * 0.5f;
		Widget.Properties.Add(TEXT("StrokeMargin"),
			FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), -Outset, -Outset, -Outset, -Outset));
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

	if (FLayoutMapper::HasPadding(Node))
		FLayoutMapper::ApplyPadding(Border, Node->Padding);

	for (const FString& Key : {TEXT("DrawAs"), TEXT("OutlineCornerRadius"), TEXT("CornerRadii"), TEXT("OutlineThickness"), TEXT("OutlineColor"), TEXT("StrokePadding")})
	{
		if (const FString* V = Widget.Properties.Find(Key))
		{
			Border.Properties.Add(Key, *V);
			Widget.Properties.Remove(Key);
		}
	}

	// Outside stroke: visual bounds from the parser already include the stroke expansion,
	// so do NOT expand Border.Bounds again. Only add StrokePadding so the centered UMG
	// outline doesn't eat into content space.
	if (const FString* SM = Widget.Properties.Find(TEXT("StrokeMargin")))
	{
		FMargin M = FUMGWidgetUtils::ParseMargin(*SM);
		float Outset = -M.Left; // StrokeMargin stores negative values
		if (Outset > 0)
		{
			FString Pad = FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), Outset, Outset, Outset, Outset);
			if (const FString* Existing = Border.Properties.Find(TEXT("StrokePadding")))
			{
				FMargin EP = FUMGWidgetUtils::ParseMargin(*Existing);
				Pad = FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"),
					EP.Left + Outset, EP.Top + Outset, EP.Right + Outset, EP.Bottom + Outset);
				Border.Properties.Remove(TEXT("StrokePadding"));
			}
			Border.Properties.Add(TEXT("StrokePadding"), Pad);
		}
		Widget.Properties.Remove(TEXT("StrokeMargin"));
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

	// 包装后仍需保持节点级裁剪与变换语义，避免外层背景与内层内容表现不一致。
	for (const FString& Key : {TEXT("Clipping"), TEXT("RenderAngle"), TEXT("RenderTransformScale"), TEXT("RenderTranslation")})
	{
		if (const FString* V = Widget.Properties.Find(Key))
		{
			Border.Properties.Add(Key, *V);
			Widget.Properties.Remove(Key);
		}
	}

	// BoxShadow 索引属性上卷到 Border（键格式: BoxShadow_N_OffsetX 等）
	if (const FString* CountStr = Widget.Properties.Find(TEXT("BoxShadowCount")))
	{
		int32 Count = FCString::Atoi(**CountStr);
		Border.Properties.Add(TEXT("BoxShadowCount"), *CountStr);
		Widget.Properties.Remove(TEXT("BoxShadowCount"));
		for (int32 i = 0; i < Count; ++i)
		{
			FString Prefix = FString::Printf(TEXT("BoxShadow_%d_"), i);
			for (const TCHAR* Suffix : {TEXT("OffsetX"), TEXT("OffsetY"), TEXT("Blur"), TEXT("Spread"), TEXT("Color")})
			{
				FString Key = Prefix + Suffix;
				if (const FString* V = Widget.Properties.Find(Key))
				{
					Border.Properties.Add(Key, *V);
					Widget.Properties.Remove(Key);
				}
			}
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

FUMGWidgetNode FVisualMapper::WrapWithBackgroundBlur(FUMGWidgetNode&& Widget, float BlurStrength)
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

FUMGWidgetNode FVisualMapper::WrapWithBoxShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, int32 ShadowIndex)
{
	FString Prefix = FString::Printf(TEXT("BoxShadow_%d_"), ShadowIndex);
	const FString* OffXStr = Widget.Properties.Find(Prefix + TEXT("OffsetX"));
	const FString* OffYStr = Widget.Properties.Find(Prefix + TEXT("OffsetY"));
	const FString* BlurStr = Widget.Properties.Find(Prefix + TEXT("Blur"));
	const FString* SpreadStr = Widget.Properties.Find(Prefix + TEXT("Spread"));
	const FString* ColorStr = Widget.Properties.Find(Prefix + TEXT("Color"));

	if (!OffXStr || !OffYStr) return MoveTemp(Widget);

	float OffX = FCString::Atof(**OffXStr);
	float OffY = FCString::Atof(**OffYStr);
	float Blur = BlurStr ? FCString::Atof(**BlurStr) : 0.0f;
	float Spread = SpreadStr ? FCString::Atof(**SpreadStr) : 0.0f;
	FString ShadowColor = ColorStr ? *ColorStr : TEXT("#00000040");

	Widget.Properties.Remove(Prefix + TEXT("OffsetX"));
	Widget.Properties.Remove(Prefix + TEXT("OffsetY"));
	Widget.Properties.Remove(Prefix + TEXT("Blur"));
	Widget.Properties.Remove(Prefix + TEXT("Spread"));
	Widget.Properties.Remove(Prefix + TEXT("Color"));

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
	FString ShadowSuffix = FString::Printf(TEXT("_shadow%d"), ShadowIndex);
	Shadow.Id = Widget.Id + ShadowSuffix;
	Shadow.Name = Widget.Name + ShadowSuffix;
	Shadow.Properties.Add(TEXT("BrushColor"), ShadowColor);

	// 阴影纹理参数
	int32 TexW = FMath::Max(1, FMath::RoundToInt(Node->Width.Value + Extent * 2));
	int32 TexH = FMath::Max(1, FMath::RoundToInt(Node->Height.Value + Extent * 2));
	Shadow.Properties.Add(TEXT("WidthOverride"), FString::FromInt(TexW));
	Shadow.Properties.Add(TEXT("HeightOverride"), FString::FromInt(TexH));
	Shadow.Properties.Add(TEXT("MaterialRef"), (Blur > 0.0f) ? TEXT("boxshadow") : MatRef_RoundedRect);
	Shadow.Properties.Add(TEXT("ShadowBlur"), FString::SanitizeFloat(Blur));

	const float HalfMin = FMath::Min(TexW, TexH) * 0.5f;
	if (Node->CornerRadius.bUniform)
	{
		float NR = (HalfMin > 0.f) ? (Node->CornerRadius.Uniform + Extent) / HalfMin : 0.f;
		Shadow.Properties.Add(TEXT("MaterialParams"),
			FString::Printf(TEXT("{\"Color\":\"%s\",\"CornerRadius\":%.3f}"), *ShadowColor, NR));
	}
	else
	{
		auto Norm = [&](float R) { return (HalfMin > 0.f) ? (R + Extent) / HalfMin : 0.f; };
		Shadow.Properties.Add(TEXT("MaterialParams"),
			FString::Printf(TEXT("{\"Color\":\"%s\",\"CornerRadii\":[%.3f,%.3f,%.3f,%.3f]}"),
				*ShadowColor,
				Norm(Node->CornerRadius.TopLeft), Norm(Node->CornerRadius.TopRight),
				Norm(Node->CornerRadius.BottomRight), Norm(Node->CornerRadius.BottomLeft)));
	}

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

FUMGWidgetNode FVisualMapper::WrapWithMultiFill(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node)
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
		const FDesignFill& Fill = Node->Fills[i];
		if (!Fill.bEnabled || Fill.Type == EDesignFillType::None)
			continue;

		FUMGWidgetNode Layer;
		Layer.Id = FString::Printf(TEXT("%s_fill%d"), *Widget.Id, i);
		Layer.Name = FString::Printf(TEXT("%s_fill%d"), *Widget.Name, i);
		Layer.Slot.HorizontalAlignment = TEXT("Fill");
		Layer.Slot.VerticalAlignment = TEXT("Fill");

		if (Fill.Type == EDesignFillType::Color)
		{
			Layer.Type = TEXT("Border");
			Layer.Properties.Add(TEXT("BrushColor"), Fill.SolidColor);
			Layer.Brush.BrushColor = FUMGWidgetUtils::ParseColor(Fill.SolidColor);
			Layer.Brush.bHasBrushColor = true;
		}
		else if (Fill.Type == EDesignFillType::Gradient && Fill.Gradient.Colors.Num() > 0)
		{
			Layer.Type = TEXT("Image");
			FString MaterialRef = MatRef_GradientH;
			switch (Fill.Gradient.GradientType)
			{
			case EDesignGradientType::Radial:
				MaterialRef = MatRef_Radial;
				break;
			case EDesignGradientType::Angular:
				MaterialRef = MatRef_Angular;
				break;
			case EDesignGradientType::Linear:
			default:
				MaterialRef = MatRef_GradientLinear;
				break;
			}
			Layer.Properties.Add(TEXT("MaterialRef"), MaterialRef);
			Layer.Brush.MaterialRef = MaterialRef;
			const FString MaterialParams = BuildGradientMaterialParams(Fill.Gradient);
			if (!MaterialParams.IsEmpty())
			{
				Layer.Properties.Add(TEXT("MaterialParams"), MaterialParams);
				Layer.Brush.MaterialParams = MaterialParams;
			}
			AddTextureSizeHints(Layer, Node);
		}
		else if (Fill.Type == EDesignFillType::Image && !Fill.Image.Url.IsEmpty())
		{
			Layer.Type = TEXT("Image");
			Layer.Properties.Add(TEXT("TexturePath"), Fill.Image.Url);
			Layer.Brush.TexturePath = Fill.Image.Url;

			if (Fill.Image.Mode == EDesignImageMode::Crop && !Fill.Image.Transform.IsIdentity())
			{
				const FDesignImageTransform& T = Fill.Image.Transform;
				Layer.Properties.Add(TEXT("ImageMode"), TEXT("Crop"));
				Layer.Properties.Add(TEXT("ImageTransform"),
					FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
						T.A, T.B, T.TX, T.C, T.D, T.TY));
			}
			else if (Fill.Image.Mode == EDesignImageMode::Fill)
			{
				Layer.Properties.Add(TEXT("ImageMode"), TEXT("Fill"));
			}
			else if (Fill.Image.Mode == EDesignImageMode::Fit)
			{
				Layer.Properties.Add(TEXT("ImageMode"), TEXT("Fit"));
			}

			if (Fill.Image.Opacity < 1.0f)
				Layer.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Fill.Image.Opacity));
			AddTextureSizeHints(Layer, Node);
		}
		else
		{
			continue;
		}

		// 圆角应用到每层
		if (Node->CornerRadius.HasRadius())
			ApplyRoundedCorner(Layer, Node->CornerRadius, Node->Width.Value, Node->Height.Value);

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


