// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetNodeAccess.h"

#include "GradientMaterialParams.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"

namespace
{
	void AppendVerboseProperty(FString& OutDump, const TCHAR* Key, const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return;
		}
		OutDump += FString::Printf(TEXT(" %s=%s"), Key, *Value.Left(40));
	}

	void AppendVerboseProperty(FString& OutDump, const TCHAR* Key, const TOptional<FString>& Value)
	{
		if (Value.IsSet())
		{
			AppendVerboseProperty(OutDump, Key, Value.GetValue());
		}
	}

	void AppendVerboseFloat(FString& OutDump, const TCHAR* Key, const float Value)
	{
		AppendVerboseProperty(OutDump, Key, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Value));
	}

	void AppendVerboseBool(FString& OutDump, const TCHAR* Key, const bool Value)
	{
		AppendVerboseProperty(OutDump, Key, Design2UMGWidgetNodeAccess::SerializeWidgetBool(Value));
	}

	void AppendVerboseInt(FString& OutDump, const TCHAR* Key, const int32 Value)
	{
		AppendVerboseProperty(OutDump, Key, FString::FromInt(Value));
	}

	void AppendVerboseVector2D(FString& OutDump, const TCHAR* Key, const FVector2D& Value)
	{
		AppendVerboseProperty(OutDump, Key, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Value));
	}

	void AppendVerboseColor(FString& OutDump, const TCHAR* Key, const FLinearColor& Value)
	{
		AppendVerboseProperty(OutDump, Key, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Value));
	}

	void AppendVerboseMargin(FString& OutDump, const TCHAR* Key, const FMargin& Value)
	{
		AppendVerboseProperty(OutDump, Key, Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Value));
	}
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetFloat(const float Value)
{
	return FString::SanitizeFloat(Value);
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetBool(const bool Value)
{
	return Value ? TEXT("true") : TEXT("false");
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(const FVector2D& Value)
{
	return FString::Printf(TEXT("%.6f,%.6f"), Value.X, Value.Y);
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetCompactVector2D(const FVector2D& Value)
{
	return FString::Printf(TEXT("%.1f,%.1f"), Value.X, Value.Y);
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetMargin(const FMargin& Value)
{
	return FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), Value.Left, Value.Top, Value.Right, Value.Bottom);
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetColor(const FLinearColor& Value)
{
	return FString::Printf(TEXT("#%s"), *Value.ToFColor(true).ToHex());
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetVector4(const FVector4& Value)
{
	return FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), Value.X, Value.Y, Value.Z, Value.W);
}

FString Design2UMGWidgetNodeAccess::SerializeImageTransform(const FUMGVisualBrushData::FImageTransformData& Value)
{
	return FString::Printf(
		TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
		Value.A,
		Value.B,
		Value.TX,
		Value.C,
		Value.D,
			Value.TY);
}

bool Design2UMGWidgetNodeAccess::TryParseImageTransform(const FString& Value, FUMGVisualBrushData::FImageTransformData& OutTransform)
{
	TArray<FString> Parts;
	Value.ParseIntoArray(Parts, TEXT(","), true);
	if (Parts.Num() != 6)
	{
		return false;
	}

	OutTransform.A = FCString::Atof(*Parts[0].TrimStartAndEnd());
	OutTransform.B = FCString::Atof(*Parts[1].TrimStartAndEnd());
	OutTransform.TX = FCString::Atof(*Parts[2].TrimStartAndEnd());
	OutTransform.C = FCString::Atof(*Parts[3].TrimStartAndEnd());
	OutTransform.D = FCString::Atof(*Parts[4].TrimStartAndEnd());
	OutTransform.TY = FCString::Atof(*Parts[5].TrimStartAndEnd());
	return true;
}

FString Design2UMGWidgetNodeAccess::GetGradientMaterialRef(const FUMGVisualBrushData::FGradientData& Gradient)
{
	if (Gradient.Type.IsSet() && Gradient.Type.GetValue().Equals(TEXT("Radial"), ESearchCase::IgnoreCase))
	{
		return TEXT("radialgradient");
	}
	if (Gradient.Type.IsSet() && Gradient.Type.GetValue().Equals(TEXT("Angular"), ESearchCase::IgnoreCase))
	{
		return TEXT("angulargradient");
	}
	return TEXT("gradientlinear");
}

FString Design2UMGWidgetNodeAccess::SerializeGradientMaterialParams(const FUMGVisualBrushData::FGradientData& Gradient)
{
	if (Gradient.Stops.Num() == 0)
	{
		return FString();
	}

	Design2UMGGradientMaterialParams::FGradientParams Params;
	Params.bRadial = Gradient.Type.IsSet() && Gradient.Type.GetValue().Equals(TEXT("Radial"), ESearchCase::IgnoreCase);
	const bool bIsLinear = !Gradient.Type.IsSet()
		|| Gradient.Type.GetValue().Equals(TEXT("Linear"), ESearchCase::IgnoreCase);
	Params.bHasLinearEndpoints = bIsLinear && Gradient.StartPoint.IsSet() && Gradient.EndPoint.IsSet();
	Params.StartPoint = Gradient.StartPoint.Get(FVector2D::ZeroVector);
	Params.EndPoint = Gradient.EndPoint.Get(FVector2D(1.0f, 0.0f));
	Params.Center = Gradient.Center.Get(FVector2D(0.5f, 0.5f));
	Params.Size = Gradient.Size.Get(FVector2D(1.0f, 1.0f));
	Params.Rotation = Gradient.Rotation.Get(0.0f);
	for (const FUMGVisualBrushData::FGradientStopData& Stop : Gradient.Stops)
	{
		Params.Stops.Add({Stop.Position, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Stop.Color)});
	}
	return Design2UMGGradientMaterialParams::BuildGradientMaterialParamsJson(Params);
}

FString Design2UMGWidgetNodeAccess::SerializeDrawAs(const ESlateBrushDrawType::Type DrawAs)
{
	switch (DrawAs)
	{
	case ESlateBrushDrawType::NoDrawType:
		return TEXT("NoDrawType");
	case ESlateBrushDrawType::Box:
		return TEXT("Box");
	case ESlateBrushDrawType::Border:
		return TEXT("Border");
	case ESlateBrushDrawType::Image:
		return TEXT("Image");
	case ESlateBrushDrawType::RoundedBox:
		return TEXT("RoundedBox");
	default:
		return FString();
	}
}

bool Design2UMGWidgetNodeAccess::TryParseDrawAs(const FString& Value, ESlateBrushDrawType::Type& OutDrawAs)
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

bool Design2UMGWidgetNodeAccess::NormalizeBrushTexturePathOwner(FUMGVisualBrushData& Brush, FString* OutDiagnostic)
{
	if (OutDiagnostic)
	{
		OutDiagnostic->Reset();
	}

	if (!Brush.Image.TexturePath.IsSet())
	{
		if (Brush.TexturePath.IsEmpty())
		{
			return false;
		}

		Brush.Image.TexturePath = Brush.TexturePath;
		Brush.TexturePath.Empty();
		return true;
	}

	if (!Brush.TexturePath.IsEmpty() && Brush.TexturePath != Brush.Image.TexturePath.GetValue())
	{
		if (OutDiagnostic)
		{
			*OutDiagnostic = FString::Printf(
				TEXT("Brush.TexturePath conflicts with Brush.Image.TexturePath; using Brush.Image.TexturePath ('%s') instead of legacy/root '%s'."),
				*Brush.Image.TexturePath.GetValue(),
				*Brush.TexturePath);
		}
	}

	Brush.TexturePath.Empty();
	return true;
}

namespace
{
	void AppendTextStyleVerboseDump(FString& Dump, const FUMGTextStyleData& TextStyle)
	{
	// Text style.
	if (TextStyle.FontSize > 0.0f)
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::FontSize, TextStyle.FontSize);
	}
	if (TextStyle.SourceFontSize > 0.0f)
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::DesignFontSize, TextStyle.SourceFontSize);
	}
	if (TextStyle.LetterSpacing.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::LetterSpacing, TextStyle.LetterSpacing.GetValue());
	}
	}

	void AppendWidgetStyleVerboseDump(FString& Dump, const FUMGWidgetNode& Node)
	{
	// Widget style. ImageMode/ImageTransform prefer the widget style payload
	// and fall back to the brush image payload, mirroring the old key lookup order.
	if (Node.WidgetStyle.WidthOverride.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::WidthOverride, Node.WidgetStyle.WidthOverride.GetValue());
	}
	if (Node.WidgetStyle.HeightOverride.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::HeightOverride, Node.WidgetStyle.HeightOverride.GetValue());
	}
	if (Node.WidgetStyle.MinDesiredWidth.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::MinDesiredWidth, Node.WidgetStyle.MinDesiredWidth.GetValue());
	}
	if (Node.WidgetStyle.MinDesiredHeight.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::MinDesiredHeight, Node.WidgetStyle.MinDesiredHeight.GetValue());
	}
	if (Node.WidgetStyle.Padding.IsSet())
	{
		AppendVerboseMargin(Dump, UMGPropertyKeys::Padding, Node.WidgetStyle.Padding.GetValue());
	}
	if (Node.WidgetStyle.InnerSlotPadding.IsSet())
	{
		AppendVerboseProperty(
			Dump, UMGPropertyKeys::InnerSlotPadding,
			Design2UMGWidgetNodeAccess::SerializeWidgetCompactVector2D(Node.WidgetStyle.InnerSlotPadding.GetValue()));
	}
	if (Node.WidgetStyle.SlotPadding.IsSet())
	{
		AppendVerboseMargin(Dump, UMGPropertyKeys::SlotPadding, Node.WidgetStyle.SlotPadding.GetValue());
	}
	if (Node.WidgetStyle.StrokePadding.IsSet())
	{
		AppendVerboseMargin(Dump, UMGPropertyKeys::StrokePadding, Node.WidgetStyle.StrokePadding.GetValue());
	}
	if (Node.WidgetStyle.StrokeMargin.IsSet())
	{
		AppendVerboseMargin(Dump, UMGPropertyKeys::StrokeMargin, Node.WidgetStyle.StrokeMargin.GetValue());
	}
	AppendVerboseProperty(
		Dump, UMGPropertyKeys::ImageMode,
		Node.WidgetStyle.ImageMode.IsSet() ? Node.WidgetStyle.ImageMode : Node.Brush.Image.Mode);
	if (Node.WidgetStyle.ImageTransform.IsSet())
	{
		AppendVerboseProperty(Dump, UMGPropertyKeys::ImageTransform, Node.WidgetStyle.ImageTransform.GetValue());
	}
	else if (Node.Brush.Image.Transform.IsSet())
	{
		AppendVerboseProperty(
			Dump, UMGPropertyKeys::ImageTransform,
			Design2UMGWidgetNodeAccess::SerializeImageTransform(Node.Brush.Image.Transform.GetValue()));
	}
	AppendVerboseProperty(Dump, UMGPropertyKeys::Stretch, Node.WidgetStyle.Stretch);
	AppendVerboseProperty(Dump, UMGPropertyKeys::StretchDirection, Node.WidgetStyle.StretchDirection);
	if (Node.WidgetStyle.BackgroundColor.IsSet())
	{
		AppendVerboseColor(Dump, UMGPropertyKeys::BackgroundColor, Node.WidgetStyle.BackgroundColor.GetValue());
	}
	if (Node.WidgetStyle.HoveredColor.IsSet())
	{
		AppendVerboseColor(Dump, UMGPropertyKeys::HoveredColor, Node.WidgetStyle.HoveredColor.GetValue());
	}
	if (Node.WidgetStyle.PressedColor.IsSet())
	{
		AppendVerboseColor(Dump, UMGPropertyKeys::PressedColor, Node.WidgetStyle.PressedColor.GetValue());
	}
	AppendVerboseProperty(Dump, UMGPropertyKeys::NamedSlot, Node.WidgetStyle.NamedSlot);
	AppendVerboseProperty(Dump, UMGPropertyKeys::SlotName, Node.WidgetStyle.SlotName);
	AppendVerboseProperty(Dump, UMGPropertyKeys::Visibility, Node.WidgetStyle.Visibility);
	AppendVerboseProperty(Dump, UMGPropertyKeys::ToolTipText, Node.WidgetStyle.ToolTipText);
	if (Node.WidgetStyle.IsEnabled.IsSet())
	{
		AppendVerboseBool(Dump, UMGPropertyKeys::IsEnabled, Node.WidgetStyle.IsEnabled.GetValue());
	}
	}

	void AppendTextVerboseDump(FString& Dump, const FUMGTextData& Text)
	{
	// Text.
	AppendVerboseProperty(Dump, UMGPropertyKeys::Text, Text.Text);
	AppendVerboseProperty(Dump, UMGPropertyKeys::RichTextStyleRows, Text.RichTextStyleRowsJson);
	AppendVerboseProperty(Dump, UMGPropertyKeys::HintText, Text.HintText);
	AppendVerboseProperty(Dump, UMGPropertyKeys::FontFamily, Text.FontFamily);
	AppendVerboseProperty(Dump, UMGPropertyKeys::TypefaceFontName, Text.TypefaceFontName);
	AppendVerboseProperty(Dump, UMGPropertyKeys::FontStyle, Text.FontStyle);
	AppendVerboseProperty(Dump, UMGPropertyKeys::Justification, Text.Justification);
	if (Text.bAutoWrapText.IsSet())
	{
		AppendVerboseBool(Dump, UMGPropertyKeys::AutoWrapText, Text.bAutoWrapText.GetValue());
	}
	if (Text.WrapTextAt.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::WrapTextAt, Text.WrapTextAt.GetValue());
	}
	if (Text.LineHeightPercentage.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::LineHeightPercentage, Text.LineHeightPercentage.GetValue());
	}
	if (Text.LineHeightBaselineShift.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::LineHeightBaselineShift, Text.LineHeightBaselineShift.GetValue());
	}
	if (Text.bUnderline.IsSet())
	{
		AppendVerboseBool(Dump, UMGPropertyKeys::Underline, Text.bUnderline.GetValue());
	}
	if (Text.bStrikethrough.IsSet())
	{
		AppendVerboseBool(Dump, UMGPropertyKeys::Strikethrough, Text.bStrikethrough.GetValue());
	}
	AppendVerboseProperty(Dump, UMGPropertyKeys::Href, Text.Href);
	AppendVerboseProperty(Dump, UMGPropertyKeys::TextAlignVertical, Text.TextAlignVertical);
	AppendVerboseProperty(Dump, UMGPropertyKeys::IconName, Text.IconName);
	if (Text.Color.IsSet())
	{
		AppendVerboseColor(Dump, UMGPropertyKeys::Color, Text.Color.GetValue());
	}
	}

	void AppendBrushVerboseDump(FString& Dump, const FUMGVisualBrushData& Brush)
	{
	// Brush. MaterialParams and PathGeometry stay out of the dump (payload noise).
	if (Brush.BrushColor.IsSet())
	{
		AppendVerboseColor(Dump, UMGPropertyKeys::BrushColor, Brush.BrushColor.GetValue());
	}
	if (Brush.OutlineColor.IsSet())
	{
		AppendVerboseColor(Dump, UMGPropertyKeys::OutlineColor, Brush.OutlineColor.GetValue());
	}
	if (Brush.DrawAs.IsSet())
	{
		AppendVerboseProperty(Dump, UMGPropertyKeys::DrawAs, Design2UMGWidgetNodeAccess::SerializeDrawAs(Brush.DrawAs.GetValue()));
	}
	if (Brush.OutlineCornerRadius.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::OutlineCornerRadius, Brush.OutlineCornerRadius.GetValue());
	}
	if (Brush.CornerRadii.IsSet())
	{
		AppendVerboseProperty(Dump, UMGPropertyKeys::CornerRadii, Design2UMGWidgetNodeAccess::SerializeWidgetVector4(Brush.CornerRadii.GetValue()));
	}
	if (Brush.OutlineThickness.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::OutlineThickness, Brush.OutlineThickness.GetValue());
	}
	if (Brush.Gradient.HasAny())
	{
		AppendVerboseProperty(Dump, UMGPropertyKeys::MaterialRef, Design2UMGWidgetNodeAccess::GetGradientMaterialRef(Brush.Gradient));
	}
	else if (Brush.MaterialRef.IsSet())
	{
		AppendVerboseProperty(Dump, UMGPropertyKeys::MaterialRef, Brush.MaterialRef.GetValue());
	}
	AppendVerboseProperty(Dump, UMGPropertyKeys::TexturePath, Brush.Image.TexturePath);
	}

	void AppendEffectVerboseDump(FString& Dump, const FUMGEffectData& Effect)
	{
	// Effect.
	if (Effect.BlurStrength.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::BlurStrength, Effect.BlurStrength.GetValue());
	}
	if (Effect.bRetainRendering.IsSet())
	{
		AppendVerboseBool(Dump, UMGPropertyKeys::RetainRendering, Effect.bRetainRendering.GetValue());
	}
	AppendVerboseProperty(Dump, UMGPropertyKeys::EffectMaterial, Effect.EffectMaterial);
	AppendVerboseProperty(Dump, UMGPropertyKeys::TextureParameter, Effect.TextureParameter);
	if (Effect.EffectBlurUVStep.IsSet())
	{
		AppendVerboseVector2D(Dump, UMGPropertyKeys::EffectBlurUVStep, Effect.EffectBlurUVStep.GetValue());
	}
	if (Effect.EffectColor.IsSet())
	{
		AppendVerboseColor(Dump, UMGPropertyKeys::EffectColor, Effect.EffectColor.GetValue());
	}
	if (Effect.EffectOffset.IsSet())
	{
		AppendVerboseVector2D(Dump, UMGPropertyKeys::EffectOffset, Effect.EffectOffset.GetValue());
	}
	if (Effect.RenderingPhase.IsSet())
	{
		AppendVerboseInt(Dump, UMGPropertyKeys::Phase, Effect.RenderingPhase.GetValue().Phase);
		AppendVerboseInt(Dump, UMGPropertyKeys::PhaseCount, Effect.RenderingPhase.GetValue().PhaseCount);
	}
	}

	void AppendPathVerboseDump(FString& Dump, const FUMGPathData& Path)
	{
	// Path (geometry payload excluded: same dump filter as before).
	AppendVerboseProperty(Dump, UMGPropertyKeys::PathFillRule, Path.FillRule);
	if (Path.FillColor.IsSet())
	{
		AppendVerboseColor(Dump, UMGPropertyKeys::PathFillColor, Path.FillColor.GetValue());
	}
	if (Path.StrokeColor.IsSet())
	{
		AppendVerboseColor(Dump, UMGPropertyKeys::PathStrokeColor, Path.StrokeColor.GetValue());
	}
	if (Path.StrokeThickness.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::PathStrokeThickness, Path.StrokeThickness.GetValue());
	}
	AppendVerboseProperty(Dump, UMGPropertyKeys::PathStrokeCap, Path.StrokeCap);
	AppendVerboseProperty(Dump, UMGPropertyKeys::PathStrokeJoin, Path.StrokeJoin);
	}

	void AppendShadowVerboseDump(FString& Dump, const FUMGShadowData& Shadow)
	{
	// Shadow.
	if (Shadow.ShadowOffset.IsSet())
	{
		AppendVerboseProperty(
			Dump, UMGPropertyKeys::ShadowOffset,
			Design2UMGWidgetNodeAccess::SerializeWidgetCompactVector2D(Shadow.ShadowOffset.GetValue()));
	}
	if (Shadow.ShadowColorAndOpacity.IsSet())
	{
		AppendVerboseColor(Dump, UMGPropertyKeys::ShadowColorAndOpacity, Shadow.ShadowColorAndOpacity.GetValue());
	}
	if (Shadow.ShadowBlur.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::ShadowBlur, Shadow.ShadowBlur.GetValue());
	}
	if (Shadow.BoxShadows.Num() > 0)
	{
		AppendVerboseInt(Dump, UMGPropertyKeys::BoxShadowCount, Shadow.BoxShadows.Num());
	}
	}

	void AppendRenderVerboseDump(FString& Dump, const FUMGRenderStyleData& Render)
	{
	// Render.
	AppendVerboseProperty(Dump, UMGPropertyKeys::Clipping, Render.Clipping);
	if (Render.RenderOpacity.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::RenderOpacity, Render.RenderOpacity.GetValue());
	}
	if (Render.RenderAngle.IsSet())
	{
		AppendVerboseFloat(Dump, UMGPropertyKeys::RenderAngle, Render.RenderAngle.GetValue());
	}
	if (Render.RenderTransformScale.IsSet())
	{
		AppendVerboseVector2D(Dump, UMGPropertyKeys::RenderTransformScale, Render.RenderTransformScale.GetValue());
	}
	if (Render.RenderTransformPivot.IsSet())
	{
		AppendVerboseVector2D(Dump, UMGPropertyKeys::RenderTransformPivot, Render.RenderTransformPivot.GetValue());
	}
	if (Render.RenderTranslation.IsSet())
	{
		AppendVerboseVector2D(Dump, UMGPropertyKeys::RenderTranslation, Render.RenderTranslation.GetValue());
	}
	}

	void AppendWidgetStateVerboseDump(FString& Dump, const FUMGWidgetStateData& WidgetState)
	{
	// Widget state.
	if (WidgetState.ActiveWidgetIndex.IsSet())
	{
		AppendVerboseInt(Dump, UMGPropertyKeys::ActiveWidgetIndex, WidgetState.ActiveWidgetIndex.GetValue());
	}
	}

	void AppendScrollBoxVerboseDump(FString& Dump, const FUMGScrollBoxData& ScrollBox)
	{
	// Scroll box.
	AppendVerboseProperty(Dump, UMGPropertyKeys::Orientation, ScrollBox.Orientation);
	AppendVerboseProperty(Dump, UMGPropertyKeys::ScrollBarVisibility, ScrollBox.ScrollBarVisibility);
	}
}

FString Design2UMGWidgetNodeAccess::BuildWidgetNodeVerbosePropertyDump(const FUMGWidgetNode& Node)
{
	FString Dump;

	AppendTextStyleVerboseDump(Dump, Node.TextStyle);
	AppendWidgetStyleVerboseDump(Dump, Node);
	AppendTextVerboseDump(Dump, Node.Text);
	AppendBrushVerboseDump(Dump, Node.Brush);
	AppendEffectVerboseDump(Dump, Node.Effect);
	AppendPathVerboseDump(Dump, Node.Path);
	AppendShadowVerboseDump(Dump, Node.Shadow);
	AppendRenderVerboseDump(Dump, Node.Render);
	AppendWidgetStateVerboseDump(Dump, Node.WidgetState);
	AppendScrollBoxVerboseDump(Dump, Node.ScrollBox);

	return Dump;
}
