// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapper.h"
#include "ImageFillUtils.h"
#include "Design2UMGSettings.h"
#include "LayoutMapper.h"
#include "Design2UMGLog.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGValueParser.h"

namespace
{
	// 材质引用标识符（ProceduralShapeGenerator 内部 key，非资产路径）
	const TCHAR* RetainerForegroundBlurEffectRef = TEXT("d2u.retainer.foreground_blur");
	const TCHAR* RetainerInnerShadowEffectRef = TEXT("d2u.retainer.inner_shadow");
	const TCHAR* MatRef_GradientH = TEXT("gradienth");
	const TCHAR* MatRef_GradientV = TEXT("gradientv");
	const TCHAR* MatRef_GradientLinear = TEXT("gradientlinear");
	const TCHAR* MatRef_Radial    = TEXT("radialgradient");
	const TCHAR* MatRef_Angular   = TEXT("angulargradient");
	const TCHAR* MatRef_RoundedRect = TEXT("roundedrect");

	FVector2D ComputeForegroundBlurUVStep(const TSharedPtr<FDesignNode>& Node, float BlurStrength)
	{
		if (!Node.IsValid())
		{
			return FVector2D(1.0f / 512.0f, 1.0f / 512.0f);
		}

		const float Width = FMath::Max(1.0f, Node->Width.Value);
		const float Height = FMath::Max(1.0f, Node->Height.Value);
		const float EffectiveBlur = FMath::Max(0.5f, BlurStrength) * 0.35f;
		return FVector2D(EffectiveBlur / Width, EffectiveBlur / Height);
	}

	FVector2D ComputeEffectUVOffset(const TSharedPtr<FDesignNode>& Node, float OffsetX, float OffsetY)
	{
		if (!Node.IsValid())
		{
			return FVector2D::ZeroVector;
		}

		const float Width = FMath::Max(1.0f, Node->Width.Value);
		const float Height = FMath::Max(1.0f, Node->Height.Value);
		return FVector2D(OffsetX / Width, OffsetY / Height);
	}

	float GetLuminosityFitOpacityCap()
	{
		const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
		return Settings
			? Settings->ResolveLuminosityFitOpacityCap()
			: UDesign2UMGSettings::CanonicalLuminosityFitOpacityCap;
	}

	float GetStrokeSideThickness(const FDesignStrokeThickness& Thickness, TCHAR Side)
	{
		if (Thickness.bUniform)
		{
			return Thickness.Uniform;
		}

		switch (Side)
		{
		case 'T':
			return Thickness.Top;
		case 'R':
			return Thickness.Right;
		case 'B':
			return Thickness.Bottom;
		case 'L':
			return Thickness.Left;
		default:
			return 0.0f;
		}
	}

	FMargin GetStrokeHalfInsets(const FDesignStrokeThickness& Thickness)
	{
		return FMargin(
			GetStrokeSideThickness(Thickness, 'L') * 0.5f,
			GetStrokeSideThickness(Thickness, 'T') * 0.5f,
			GetStrokeSideThickness(Thickness, 'R') * 0.5f,
			GetStrokeSideThickness(Thickness, 'B') * 0.5f);
	}

	bool HasUniformRectStroke(const FDesignStroke& Stroke)
	{
		const float Top = GetStrokeSideThickness(Stroke.Thickness, 'T');
		const float Right = GetStrokeSideThickness(Stroke.Thickness, 'R');
		const float Bottom = GetStrokeSideThickness(Stroke.Thickness, 'B');
		const float Left = GetStrokeSideThickness(Stroke.Thickness, 'L');
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
			&& Node->Stroke.HasStroke()
			&& !Node->CornerRadius.HasRadius()
			&& !HasUniformRectStroke(Node->Stroke);
	}

	bool HasOpaqueSolidFill(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Node->Fill.bEnabled || Node->Fill.Type != EDesignFillType::Color || Node->Fill.SolidColor.IsEmpty())
		{
			return false;
		}

		return FUMGValueParser::ParseColor(Node->Fill.SolidColor).A > KINDA_SMALL_NUMBER;
	}

	bool HasPlaceholderSurfaceChildren(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			if (Child.IsValid() && Child->bSemanticPlaceholderSurface)
			{
				return true;
			}
		}

		return false;
	}

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
		return HasConvertedChildStrokeCoverage(Border, TEXT("_stroke_top"))
			|| HasConvertedChildStrokeCoverage(Border, TEXT("_stroke_right"))
			|| HasConvertedChildStrokeCoverage(Border, TEXT("_stroke_bottom"))
			|| HasConvertedChildStrokeCoverage(Border, TEXT("_stroke_left"));
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
		Widget.Bounds = FBounds();
		Widget.Slot = FUMGSlotData();
		Widget.Slot.HorizontalAlignment = TEXT("Fill");
		Widget.Slot.VerticalAlignment = TEXT("Fill");
	}

	void StripExplicitStrokeFromSurface(FUMGWidgetNode& BorderSurface)
	{
		for (const FString& Key : {TEXT("OutlineThickness"), TEXT("OutlineColor"), TEXT("StrokePadding"), TEXT("StrokeMargin")})
		{
			BorderSurface.Properties.Remove(Key);
		}

		BorderSurface.Brush.OutlineThickness = 0.0f;
		BorderSurface.Brush.bHasOutlineThickness = false;
		BorderSurface.Brush.bHasOutlineColor = false;
		BorderSurface.Brush.OutlineColor = FLinearColor::Transparent;
		BorderSurface.WidgetStyle.StrokePadding = FMargin(0.0f);
		BorderSurface.WidgetStyle.bHasStrokePadding = false;
		BorderSurface.WidgetStyle.StrokeMargin = FMargin(0.0f);
		BorderSurface.WidgetStyle.bHasStrokeMargin = false;
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

	bool TryMoveVisualMapperStringProperty(FUMGPropertyBag& From, FUMGPropertyBag& To, const TCHAR* Key)
	{
		FString Value;
		if (!From.TryGetString(Key, Value) || Value.IsEmpty())
		{
			return false;
		}

		To.Add(Key, Value);
		From.Remove(Key);
		return true;
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
		Line.WidgetStyle.bHasWidthOverride = true;
		Line.WidgetStyle.HeightOverride = FMath::Max(1.0f, Height);
		Line.WidgetStyle.bHasHeightOverride = true;
		Line.Slot.HorizontalAlignment = HorizontalAlignment;
		Line.Slot.VerticalAlignment = VerticalAlignment;
		return Line;
	}

	FUMGWidgetNode WrapWithExplicitSideStroke(FUMGWidgetNode&& Border, const TSharedPtr<FDesignNode>& Node)
	{
		FLinearColor StrokeColor;
		if (!TryResolveStrokeColor(Node->Stroke, StrokeColor))
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

		if (Border.WidgetStyle.bHasStrokePadding)
		{
			Overlay.WidgetStyle.StrokePadding = Border.WidgetStyle.StrokePadding;
			Overlay.WidgetStyle.bHasStrokePadding = true;
		}
		else
		{
			FMargin StrokePadding = FMargin(0.0f);
			if (Design2UMGWidgetNodeAccess::TryGetWidgetMargin(Border, UMGPropertyKeys::StrokePadding, StrokePadding))
			{
				Overlay.WidgetStyle.StrokePadding = StrokePadding;
				Overlay.WidgetStyle.bHasStrokePadding = true;
			}
		}

		Border.Id += TEXT("_surface");
		Border.Name += TEXT("_surface");
		StripExplicitStrokeFromSurface(Border);

		if (!HasUniformRectStroke(Node->Stroke))
		{
			const bool bRedundantTop = HasConvertedChildStrokeCoverage(Border, TEXT("_stroke_top"));
			const bool bRedundantRight = HasConvertedChildStrokeCoverage(Border, TEXT("_stroke_right"));
			const bool bRedundantBottom = HasConvertedChildStrokeCoverage(Border, TEXT("_stroke_bottom"));
			const bool bRedundantLeft = HasConvertedChildStrokeCoverage(Border, TEXT("_stroke_left"));
			if (bRedundantTop)
			{
				PruneConvertedChildStrokeCoverage(Border, TEXT("_stroke_top"));
			}
			if (bRedundantRight)
			{
				PruneConvertedChildStrokeCoverage(Border, TEXT("_stroke_right"));
			}
			if (bRedundantBottom)
			{
				PruneConvertedChildStrokeCoverage(Border, TEXT("_stroke_bottom"));
			}
			if (bRedundantLeft)
			{
				PruneConvertedChildStrokeCoverage(Border, TEXT("_stroke_left"));
			}
		}

		ResetChildToFill(Border);
		Overlay.Children.Add(MoveTemp(Border));

		const float Top = GetStrokeSideThickness(Node->Stroke.Thickness, 'T');
		const float Right = GetStrokeSideThickness(Node->Stroke.Thickness, 'R');
		const float Bottom = GetStrokeSideThickness(Node->Stroke.Thickness, 'B');
		const float Left = GetStrokeSideThickness(Node->Stroke.Thickness, 'L');

		if (Top > KINDA_SMALL_NUMBER)
		{
			Overlay.Children.Add(MakeStrokeLineNode(
				Overlay.Id + TEXT("_stroke_top"),
				Overlay.Name + TEXT("_stroke_top"),
				StrokeColor,
				1.0f,
				Top,
				TEXT("Fill"),
				TEXT("Top")));
		}
		if (Right > KINDA_SMALL_NUMBER)
		{
			Overlay.Children.Add(MakeStrokeLineNode(
				Overlay.Id + TEXT("_stroke_right"),
				Overlay.Name + TEXT("_stroke_right"),
				StrokeColor,
				Right,
				1.0f,
				TEXT("Right"),
				TEXT("Fill")));
		}
		if (Bottom > KINDA_SMALL_NUMBER)
		{
			Overlay.Children.Add(MakeStrokeLineNode(
				Overlay.Id + TEXT("_stroke_bottom"),
				Overlay.Name + TEXT("_stroke_bottom"),
				StrokeColor,
				1.0f,
				Bottom,
				TEXT("Fill"),
				TEXT("Bottom")));
		}
		if (Left > KINDA_SMALL_NUMBER)
		{
			Overlay.Children.Add(MakeStrokeLineNode(
				Overlay.Id + TEXT("_stroke_left"),
				Overlay.Name + TEXT("_stroke_left"),
				StrokeColor,
				Left,
				1.0f,
				TEXT("Left"),
				TEXT("Fill")));
		}

		return Overlay;
	}

	FString BuildGradientMaterialParams(const FDesignGradient& Gradient)
	{
		if (Gradient.Colors.Num() == 0)
			return FString();

		const FString& StartColor = Gradient.Colors[0].Color;
		const FString& EndColor = Gradient.Colors.Last().Color;

		// Linear / Angular: include Rotation for angled rendering.
		const float Rotation = Gradient.Rotation;
		const float CenterX = Gradient.bHasCenter ? Gradient.CenterX : 0.5f;
		const float CenterY = Gradient.bHasCenter ? Gradient.CenterY : 0.5f;
		const float SizeWidth = Gradient.bHasSize ? Gradient.SizeWidth : 1.0f;
		const float SizeHeight = Gradient.bHasSize ? Gradient.SizeHeight : 1.0f;
		const FString RadialParams = FString::Printf(
			TEXT("\"CenterX\":%.4f,\"CenterY\":%.4f,\"SizeWidth\":%.4f,\"SizeHeight\":%.4f,\"Rotation\":%.4f"),
			CenterX,
			CenterY,
			SizeWidth,
			SizeHeight,
			Rotation);

		// Multi-stop gradients: output Stops array, and include Rotation for shapes that use it.
		if (Gradient.Colors.Num() > 2)
		{
			FString Stops;
			if (Gradient.GradientType == EDesignGradientType::Radial)
			{
				Stops = FString::Printf(TEXT("{%s,\"Stops\":["), *RadialParams);
			}
			else
			{
				Stops = FString::Printf(TEXT("{\"Rotation\":%.4f,\"Stops\":["), Rotation);
			}
			for (int32 i = 0; i < Gradient.Colors.Num(); ++i)
			{
				if (i > 0) Stops += TEXT(",");
				Stops += FString::Printf(TEXT("{\"Pos\":%.4f,\"Color\":\"%s\"}"),
					Gradient.Colors[i].Position, *Gradient.Colors[i].Color);
			}
			Stops += TEXT("]}");
			return Stops;
		}

		// Radial: 2-stop center/edge pair
		if (Gradient.GradientType == EDesignGradientType::Radial)
			return FString::Printf(TEXT("{%s,\"ColorCenter\":\"%s\",\"ColorEdge\":\"%s\"}"), *RadialParams, *StartColor, *EndColor);

		// 2-stop: include Rotation
		return FString::Printf(TEXT("{\"Rotation\":%.4f,\"ColorStart\":\"%s\",\"ColorEnd\":\"%s\"}"), Rotation, *StartColor, *EndColor);
	}

	void ApplyTypedGradientBrushData(FUMGVisualBrushData& Brush, const FDesignGradient& Gradient)
	{
		auto GradientTypeToString = [](const EDesignGradientType Type)
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
		};

		Brush.Gradient.Type = GradientTypeToString(Gradient.GradientType);
		Brush.Gradient.bHasType = true;
		Brush.Gradient.Rotation = Gradient.Rotation;
		Brush.Gradient.bHasRotation = true;
		if (Gradient.bHasCenter)
		{
			Brush.Gradient.Center = FVector2D(Gradient.CenterX, Gradient.CenterY);
			Brush.Gradient.bHasCenter = true;
		}
		if (Gradient.bHasSize)
		{
			Brush.Gradient.Size = FVector2D(Gradient.SizeWidth, Gradient.SizeHeight);
			Brush.Gradient.bHasSize = true;
		}
		Brush.Gradient.Stops.Reset();
		for (const FDesignGradientStop& Stop : Gradient.Colors)
		{
			FUMGVisualBrushData::FGradientStopData& TypedStop = Brush.Gradient.Stops.AddDefaulted_GetRef();
			TypedStop.Color = FUMGValueParser::ParseColor(Stop.Color);
			TypedStop.Position = Stop.Position;
		}
	}

	void AddTextureSizeHints(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->Width.IsFixed() && Node->Width.Value > 0.0f)
		{
			Widget.WidgetStyle.WidthOverride = Node->Width.Value;
			Widget.WidgetStyle.bHasWidthOverride = true;
		}
		if (Node->Height.IsFixed() && Node->Height.Value > 0.0f)
		{
			Widget.WidgetStyle.HeightOverride = Node->Height.Value;
			Widget.WidgetStyle.bHasHeightOverride = true;
		}
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
				if (!HasVisualMapperWidgetStringProperty(Widget, TEXT("Color")))
				{
					Widget.Text.Color = FUMGValueParser::ParseColor(Node->Fill.SolidColor);
					Widget.Text.bHasColor = true;
				}
			return;
		}

		if (Node->Fill.Type == EDesignFillType::Color)
		{
			// Text / IconFont → 只设文字颜色，不设 BrushColor（避免被 Border 包裹后变色块）
			// CreateIconFontWidget/CreateTextWidget 可能已写入 Color，FindOrAdd 不覆盖已有值
			if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
			{
				if (!HasVisualMapperWidgetStringProperty(Widget, TEXT("Color")))
				{
					Widget.Text.Color = FUMGValueParser::ParseColor(Node->Fill.SolidColor);
					Widget.Text.bHasColor = true;
				}
				return;
			}
			Widget.Brush.BrushColor = FUMGValueParser::ParseColor(Node->Fill.SolidColor);
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

			Widget.Brush.MaterialRef = MaterialRef;
			Widget.Brush.bHasMaterialRef = true;
			ApplyTypedGradientBrushData(Widget.Brush, Node->Fill.Gradient);
			const FString MaterialParams = BuildGradientMaterialParams(Node->Fill.Gradient);
			if (!MaterialParams.IsEmpty())
			{
				Widget.Brush.MaterialParams = MaterialParams;
				Widget.Brush.bHasMaterialParams = true;
			}
			AddTextureSizeHints(Widget, Node);
			return;
		}

		if (Node->Fill.Type == EDesignFillType::Image && !Node->Fill.Image.Url.IsEmpty())
		{
			Widget.Brush.TexturePath = Node->Fill.Image.Url;

			ImageFillUtils::ApplyImageFillProperties(Widget, Node->Fill.Image);

			AddTextureSizeHints(Widget, Node);
		}
	}

	void MovePropertyIfPresent(FUMGPropertyBag& From, FUMGPropertyBag& To, const TCHAR* Key)
	{
		TryMoveVisualMapperStringProperty(From, To, Key);
	}

	void MoveProperties(FUMGPropertyBag& From, FUMGPropertyBag& To, std::initializer_list<const TCHAR*> Keys, bool* bMovedAny = nullptr)
	{
		bool bMoved = false;
		for (const TCHAR* Key : Keys)
		{
			if (TryMoveVisualMapperStringProperty(From, To, Key))
			{
				bMoved = true;
			}
		}
		if (bMovedAny)
		{
			*bMovedAny = bMoved;
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
		if (Widget.WidgetStyle.bHasWidthOverride)
		{
			Border.WidgetStyle.WidthOverride = Widget.WidgetStyle.WidthOverride;
			Border.WidgetStyle.bHasWidthOverride = true;
			Widget.WidgetStyle.bHasWidthOverride = false;
			bHasBackgroundProperty = true;
		}
		if (Widget.WidgetStyle.bHasHeightOverride)
		{
			Border.WidgetStyle.HeightOverride = Widget.WidgetStyle.HeightOverride;
			Border.WidgetStyle.bHasHeightOverride = true;
			Widget.WidgetStyle.bHasHeightOverride = false;
			bHasBackgroundProperty = true;
		}
		if (Widget.WidgetStyle.bHasImageMode)
		{
			Border.WidgetStyle.ImageMode = Widget.WidgetStyle.ImageMode;
			Border.WidgetStyle.bHasImageMode = true;
			Widget.WidgetStyle.ImageMode.Empty();
			Widget.WidgetStyle.bHasImageMode = false;
			bHasBackgroundProperty = true;
		}
		if (Widget.WidgetStyle.bHasImageTransform)
		{
			Border.WidgetStyle.ImageTransform = Widget.WidgetStyle.ImageTransform;
			Border.WidgetStyle.bHasImageTransform = true;
			Widget.WidgetStyle.ImageTransform.Empty();
			Widget.WidgetStyle.bHasImageTransform = false;
			bHasBackgroundProperty = true;
		}
		MoveProperties(
			Widget.Properties,
			Border.Properties,
			{
				UMGPropertyKeys::WidthOverride,
				UMGPropertyKeys::HeightOverride,
				UMGPropertyKeys::ImageMode,
				UMGPropertyKeys::ImageTransform,
			},
			&bHasBackgroundProperty);
		Widget.Properties.Remove(UMGPropertyKeys::BrushColor);
		Widget.Properties.Remove(UMGPropertyKeys::TexturePath);
		Widget.Properties.Remove(UMGPropertyKeys::MaterialRef);
		Widget.Properties.Remove(UMGPropertyKeys::MaterialParams);

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

		if (Widget.WidgetStyle.bHasStrokePadding)
		{
			Border.WidgetStyle.StrokePadding = Widget.WidgetStyle.StrokePadding;
			Border.WidgetStyle.bHasStrokePadding = true;
			Widget.WidgetStyle.StrokePadding = FMargin(0.0f);
			Widget.WidgetStyle.bHasStrokePadding = false;
		}

		MoveProperties(
			Widget.Properties,
			Border.Properties,
			{
				UMGPropertyKeys::StrokePadding,
			});
		Widget.Properties.Remove(UMGPropertyKeys::DrawAs);
		Widget.Properties.Remove(UMGPropertyKeys::OutlineCornerRadius);
		Widget.Properties.Remove(UMGPropertyKeys::CornerRadii);
		Widget.Properties.Remove(UMGPropertyKeys::OutlineThickness);
		Widget.Properties.Remove(UMGPropertyKeys::OutlineColor);
	}

	void ApplyOutsideStrokePaddingToBorder(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		FMargin Margin = FMargin(0.0f);
		bool bHasStrokeMargin = false;
		if (Widget.WidgetStyle.bHasStrokeMargin)
		{
			Margin = Widget.WidgetStyle.StrokeMargin;
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
			if (Border.WidgetStyle.bHasStrokePadding)
			{
				const FMargin ExistingPadding = Border.WidgetStyle.StrokePadding;
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
					Border.Properties.Remove(UMGPropertyKeys::StrokePadding);
				}
			}

			Border.WidgetStyle.StrokePadding = Combined;
			Border.WidgetStyle.bHasStrokePadding = true;
		}

		Widget.WidgetStyle.StrokeMargin = FMargin(0.0f);
		Widget.WidgetStyle.bHasStrokeMargin = false;
		Widget.Properties.Remove(UMGPropertyKeys::StrokeMargin);
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
		Widget.Properties.Remove(UMGPropertyKeys::Clipping);
		Widget.Properties.Remove(UMGPropertyKeys::RenderOpacity);
		Widget.Properties.Remove(UMGPropertyKeys::RenderAngle);
		Widget.Properties.Remove(UMGPropertyKeys::RenderTransformScale);
		Widget.Properties.Remove(UMGPropertyKeys::RenderTranslation);

		if (!Border.Render.bHasRenderOpacity && Node->Opacity < 1.0f)
		{
			Border.Render.RenderOpacity = Node->Opacity;
			Border.Render.bHasRenderOpacity = true;
		}
	}

	void TransferBorderBoxShadowProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		if (Widget.Shadow.BoxShadows.Num() > 0)
		{
			Border.Shadow.BoxShadows = Widget.Shadow.BoxShadows;
			Widget.Shadow.BoxShadows.Reset();
			return;
		}

		int32 Count = 0;
		if (!Design2UMGWidgetNodeAccess::TryGetWidgetInt(Widget, UMGPropertyKeys::BoxShadowCount, Count) || Count <= 0)
		{
			return;
		}

		Border.Properties.Add(UMGPropertyKeys::BoxShadowCount, FString::FromInt(Count));
		Widget.Properties.Remove(UMGPropertyKeys::BoxShadowCount);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FString Prefix = FString::Printf(TEXT("BoxShadow_%d_"), Index);
			for (const TCHAR* Suffix : {TEXT("OffsetX"), TEXT("OffsetY"), TEXT("Blur"), TEXT("Spread"), TEXT("Color")})
			{
				MovePropertyIfPresent(Widget.Properties, Border.Properties, *(Prefix + Suffix));
			}
		}
	}

	void PrepareWrappedContentWidget(FUMGWidgetNode& Widget)
	{
		Widget.Id += TEXT("_inner");
		Widget.Name += TEXT("_inner");
		Widget.Bounds = FBounds();
		Widget.Slot.HorizontalAlignment = TEXT("Fill");
		Widget.Slot.VerticalAlignment = TEXT("Fill");
	}
}

void FVisualMapper::ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	ApplyFillProperties(Widget, Node);

	if (Node->Opacity < 1.0f)
	{
		if (Widget.Render.bHasRenderOpacity)
		{
			// 图片填充层 opacity 已写入，与节点级 opacity 合并（乘积）
			Widget.Render.RenderOpacity = Node->Opacity * Widget.Render.RenderOpacity;
		}
		else
		{
			Widget.Render.RenderOpacity = Node->Opacity;
			Widget.Render.bHasRenderOpacity = true;
		}
	}

	ApplyRoundedCorner(Widget, Node->CornerRadius, Node->Width.Value, Node->Height.Value);
	ApplyStroke(Widget, Node->Stroke);

	if (!FMath::IsNearlyZero(Node->Rotation))
	{
		Widget.Render.RenderAngle = Node->Rotation;
		Widget.Render.bHasRenderAngle = true;
	}

	if (Node->bFlipX || Node->bFlipY)
	{
		float ScaleX = Node->bFlipX ? -1.0f : 1.0f;
		float ScaleY = Node->bFlipY ? -1.0f : 1.0f;
		Widget.Render.RenderTransformScale = FVector2D(ScaleX, ScaleY);
		Widget.Render.bHasRenderTransformScale = true;
	}

	if (Node->bHasClip && Node->bClip)
	{
		Widget.Render.Clipping = TEXT("ClipToBounds");
		Widget.Render.bHasClipping = true;
	}

	int32 ShadowCount = 0;
	for (const FDesignEffect& Effect : Node->Effects)
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
	const FMargin HalfInsets = GetStrokeHalfInsets(Stroke.Thickness);
	float Thickness = Stroke.Thickness.bUniform ? Stroke.Thickness.Uniform
		: FMath::Max(FMath::Max(Stroke.Thickness.Top, Stroke.Thickness.Bottom),
			FMath::Max(Stroke.Thickness.Left, Stroke.Thickness.Right));

	if (Thickness <= 0)
		return;

	if (!HasVisualMapperWidgetStringProperty(Widget, TEXT("DrawAs")))
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
		Widget.WidgetStyle.bHasStrokePadding = true;
	}
	else if (Stroke.Align == EDesignStrokeAlign::Outside)
	{
		// Outside: only compensate on stroked sides, otherwise we shift content for no reason.
		Widget.WidgetStyle.StrokeMargin = FMargin(-HalfInsets.Left, -HalfInsets.Top, -HalfInsets.Right, -HalfInsets.Bottom);
		Widget.WidgetStyle.bHasStrokeMargin = true;
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
		FLayoutMapper::ApplyPadding(Border, Node->Padding);

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
		(NeedsExplicitSideStroke(Node)
			&& !HasPlaceholderSurfaceChildren(Node)
			&& !HasOpaqueSolidFill(Node))
		|| (Node.IsValid()
			&& Node->Stroke.HasStroke()
			&& !Node->CornerRadius.HasRadius()
			&& HasUniformRectStroke(Node->Stroke)
			&& !HasPlaceholderSurfaceChildren(Node)
			&& HasAnyConvertedChildStrokeCoverage(Border));

	if (bNeedsExplicitStroke)
	{
		return WrapWithExplicitSideStroke(MoveTemp(Border), Node);
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

	Widget.Bounds = FBounds();
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

	Widget.Bounds = FBounds();
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

	Widget.Bounds = FBounds();
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
	const float ShapeWidth = FMath::Max(1.0f, Node->Width.Value - NegativeSpreadInset * 2.0f);
	const float ShapeHeight = FMath::Max(1.0f, Node->Height.Value - NegativeSpreadInset * 2.0f);
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
	Shadow.WidgetStyle.bHasWidthOverride = true;
	Shadow.WidgetStyle.HeightOverride = TexH;
	Shadow.WidgetStyle.bHasHeightOverride = true;
	Shadow.Brush.MaterialRef = (Blur > 0.0f) ? TEXT("boxshadow") : MatRef_RoundedRect;
	Shadow.Brush.bHasMaterialRef = true;
	Shadow.Shadow.ShadowBlur = Blur;
	Shadow.Shadow.bHasShadowBlur = true;

	const float HalfMin = FMath::Min(TexW, TexH) * 0.5f;
	if (Node->CornerRadius.bUniform)
	{
		const float NormalizedRadius = (HalfMin > 0.0f) ? (Node->CornerRadius.Uniform + Extent) / HalfMin : 0.0f;
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
			Norm(Node->CornerRadius.TopLeft), Norm(Node->CornerRadius.TopRight),
			Norm(Node->CornerRadius.BottomRight), Norm(Node->CornerRadius.BottomLeft));
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
			Layer.Brush.BrushColor = FUMGValueParser::ParseColor(Fill.SolidColor);
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
			Layer.Brush.MaterialRef = MaterialRef;
			Layer.Brush.bHasMaterialRef = true;
			ApplyTypedGradientBrushData(Layer.Brush, Fill.Gradient);
			const FString MaterialParams = BuildGradientMaterialParams(Fill.Gradient);
			if (!MaterialParams.IsEmpty())
			{
				Layer.Brush.MaterialParams = MaterialParams;
				Layer.Brush.bHasMaterialParams = true;
			}
			AddTextureSizeHints(Layer, Node);
		}
		else if (Fill.Type == EDesignFillType::Image && !Fill.Image.Url.IsEmpty())
		{
			const bool bSquareNode =
				Node->Width.Value > 0.0f && Node->Height.Value > 0.0f
				&& FMath::IsNearlyEqual(Node->Width.Value, Node->Height.Value, 1.0f);

			if (Fill.Image.Mode == EDesignImageMode::Fit)
			{
				Layer.Type = TEXT("ScaleBox");
				Layer.WidgetStyle.Stretch = TEXT("ScaleToFit");
				Layer.WidgetStyle.bHasStretch = true;
				Layer.WidgetStyle.StretchDirection = TEXT("Both");
				Layer.WidgetStyle.bHasStretchDirection = true;
				const bool bLuminosityFit = Fill.BlendMode.Equals(TEXT("LUMINOSITY"), ESearchCase::IgnoreCase);

				FUMGWidgetNode ImageChild;
				ImageChild.Type = TEXT("Image");
				ImageChild.Id = FString::Printf(TEXT("%s_fill%d_img"), *Widget.Id, i);
				ImageChild.Name = FString::Printf(TEXT("%s_fill%d_img"), *Widget.Name, i);
				ImageChild.Brush.TexturePath = Fill.Image.Url;
				ImageChild.Brush.Image.TexturePath = Fill.Image.Url;
				ImageChild.Brush.Image.bHasTexturePath = true;
				if (bLuminosityFit)
				{
					// Keep FIT sizing intact and approximate the luminance wash via a capped
					// overlay opacity. The cap is configurable because it is an approximation,
					// not a source-authored geometric transform.
					const float EffectiveOpacity = FMath::Min(Fill.Image.Opacity, GetLuminosityFitOpacityCap());
					ImageChild.Render.RenderOpacity = EffectiveOpacity;
					ImageChild.Render.bHasRenderOpacity = true;
				}
				else if (Fill.Image.Opacity < 1.0f)
				{
					ImageChild.Render.RenderOpacity = Fill.Image.Opacity;
					ImageChild.Render.bHasRenderOpacity = true;
				}
				if (bSquareNode)
				{
					AddTextureSizeHints(ImageChild, Node);
				}
				Layer.Children.Add(MoveTemp(ImageChild));
			}
			else
			{
				Layer.Type = TEXT("Image");
				Layer.Brush.TexturePath = Fill.Image.Url;
				Layer.Brush.Image.TexturePath = Fill.Image.Url;
				Layer.Brush.Image.bHasTexturePath = true;

				ImageFillUtils::ApplyImageFillProperties(Layer, Fill.Image);
				AddTextureSizeHints(Layer, Node);
			}
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
	Overlay.Render = Widget.Render;
	Widget.Render = FUMGRenderStyleData();

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


