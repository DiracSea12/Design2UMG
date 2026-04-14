// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

bool IsSyntheticVectorRasterFill(const TSharedPtr<FJsonObject>& FillObj)
{
	if (!FillObj || GetString(FillObj, TEXT("type")) != TEXT("IMAGE"))
	{
		return false;
	}

	const FString ImageRef = GetString(FillObj, TEXT("imageRef"));
	return ImageRef.StartsWith(TEXT("vec_"));
}

FString ExtractSyntheticVectorRasterImageRef(const TArray<TSharedPtr<FJsonValue>>& Fills)
{
	for (const TSharedPtr<FJsonValue>& FillVal : Fills)
	{
		const TSharedPtr<FJsonObject> FillObj = FillVal->AsObject();
		if (!IsSyntheticVectorRasterFill(FillObj) || !GetBoolean(FillObj, TEXT("visible"), true))
		{
			continue;
		}

		const FString ImageRef = GetString(FillObj, TEXT("imageRef"));
		if (!ImageRef.IsEmpty())
		{
			return ImageRef;
		}
	}

	return FString();
}

bool IsIdentityImageTransform(const TArray<TSharedPtr<FJsonValue>>* TransformArr)
{
	if (!TransformArr || TransformArr->Num() == 0)
	{
		return true;
	}

	if (TransformArr->Num() != 2)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>> Row0 = (*TransformArr)[0]->AsArray();
	const TArray<TSharedPtr<FJsonValue>> Row1 = (*TransformArr)[1]->AsArray();
	if (Row0.Num() != 3 || Row1.Num() != 3)
	{
		return false;
	}

	return FMath::IsNearlyEqual(static_cast<float>(Row0[0]->AsNumber()), 1.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(Row0[1]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(Row0[2]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(Row1[0]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(Row1[1]->AsNumber()), 1.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(Row1[2]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER);
}

FDesignFill ParseSingleFill(const TSharedPtr<FJsonObject>& FillObj)
{
	FDesignFill Result;
	if (!FillObj || !GetBoolean(FillObj, TEXT("visible"), true))
		return Result;

	FString FillType = GetString(FillObj, TEXT("type"));
	float FillOpacity = GetNumber(FillObj, TEXT("opacity"), 1.0);
	Result.Opacity = FillOpacity;
	FillObj->TryGetStringField(TEXT("blendMode"), Result.BlendMode);

	if (FillType == TEXT("SOLID"))
	{
		auto ColorObj = GetObject(FillObj, TEXT("color"));
		Result.Type = EDesignFillType::Color;
		Result.SolidColor = ColorToHex(ColorObj, FillOpacity);
		return Result;
	}

	if (FillType == TEXT("GRADIENT_LINEAR") || FillType == TEXT("GRADIENT_RADIAL") || FillType == TEXT("GRADIENT_ANGULAR") || FillType == TEXT("GRADIENT_DIAMOND"))
	{
		Result.Type = EDesignFillType::Gradient;

		if (FillType == TEXT("GRADIENT_LINEAR"))
			Result.Gradient.GradientType = EDesignGradientType::Linear;
		else if (FillType == TEXT("GRADIENT_RADIAL") || FillType == TEXT("GRADIENT_DIAMOND"))
			Result.Gradient.GradientType = EDesignGradientType::Radial;
		else
			Result.Gradient.GradientType = EDesignGradientType::Angular;

		const auto* Handles = GetArray(FillObj, TEXT("gradientHandlePositions"));
		if (Handles && Handles->Num() >= 2)
		{
			auto P0 = (*Handles)[0]->AsObject();
			auto P1 = (*Handles)[1]->AsObject();
			if (P0 && P1)
			{
				const float CenterX = GetNumber(P0, TEXT("x"));
				const float CenterY = GetNumber(P0, TEXT("y"));
				const float DX = GetNumber(P1, TEXT("x")) - CenterX;
				const float DY = GetNumber(P1, TEXT("y")) - CenterY;
				Result.Gradient.Rotation = FMath::RadiansToDegrees(FMath::Atan2(DY, DX));

				if (Result.Gradient.GradientType == EDesignGradientType::Radial)
				{
					Result.Gradient.CenterX = CenterX;
					Result.Gradient.CenterY = CenterY;
					Result.Gradient.bHasCenter = true;
					Result.Gradient.SizeWidth = FMath::Max(KINDA_SMALL_NUMBER, 2.0f * FMath::Sqrt(DX * DX + DY * DY));
					Result.Gradient.SizeHeight = Result.Gradient.SizeWidth;
					Result.Gradient.bHasSize = true;

					if (Handles->Num() >= 3)
					{
						auto P2 = (*Handles)[2]->AsObject();
						if (P2)
						{
							const float DYHandleX = GetNumber(P2, TEXT("x")) - CenterX;
							const float DYHandleY = GetNumber(P2, TEXT("y")) - CenterY;
							Result.Gradient.SizeHeight = FMath::Max(
								KINDA_SMALL_NUMBER,
								2.0f * FMath::Sqrt(DYHandleX * DYHandleX + DYHandleY * DYHandleY));
						}
					}
				}
			}
		}

		const auto* Stops = GetArray(FillObj, TEXT("gradientStops"));
		if (Stops)
		{
			for (const auto& StopVal : *Stops)
			{
				auto StopObj = StopVal->AsObject();
				if (!StopObj) continue;

				FDesignGradientStop Stop;
				Stop.Color = ColorToHex(GetObject(StopObj, TEXT("color")), FillOpacity);
				Stop.Position = GetNumber(StopObj, TEXT("position"));
				Result.Gradient.Colors.Add(Stop);
			}
		}
		return Result;
	}

	if (FillType == TEXT("IMAGE"))
	{
		Result.Type = EDesignFillType::Image;
		Result.Image.Url = GetString(FillObj, TEXT("imageRef"));
		Result.Image.Opacity = FillOpacity;

		FString ScaleMode = GetString(FillObj, TEXT("scaleMode"));
		if (ScaleMode == TEXT("FILL"))
			Result.Image.Mode = EDesignImageMode::Fill;
		else if (ScaleMode == TEXT("FIT"))
			Result.Image.Mode = EDesignImageMode::Fit;
		else if (ScaleMode == TEXT("CROP"))
			Result.Image.Mode = EDesignImageMode::Crop;
		else
			Result.Image.Mode = EDesignImageMode::Stretch;

		double ScalingFactor = 0.0;
		if (FillObj->TryGetNumberField(TEXT("scalingFactor"), ScalingFactor))
		{
			Result.Image.ScalingFactor = static_cast<float>(ScalingFactor);
			Result.Image.bHasScalingFactor = true;
		}

		const auto* TransformArr = GetArray(FillObj, TEXT("imageTransform"));
		if (TransformArr && TransformArr->Num() == 2)
		{
			auto Row0 = (*TransformArr)[0]->AsArray();
			auto Row1 = (*TransformArr)[1]->AsArray();
			if (Row0.Num() == 3 && Row1.Num() == 3)
			{
				Result.Image.Transform.A = Row0[0]->AsNumber();
				Result.Image.Transform.B = Row0[1]->AsNumber();
				Result.Image.Transform.TX = Row0[2]->AsNumber();
				Result.Image.Transform.C = Row1[0]->AsNumber();
				Result.Image.Transform.D = Row1[1]->AsNumber();
				Result.Image.Transform.TY = Row1[2]->AsNumber();
			}
		}

		return Result;
	}

	return Result;
}

bool IsLuminosityPlaceholderImageFill(const TSharedPtr<FJsonObject>& FillObj)
{
	if (!FillObj || !GetBoolean(FillObj, TEXT("visible"), true))
	{
		return false;
	}

	if (GetString(FillObj, TEXT("type")) != TEXT("IMAGE"))
	{
		return false;
	}

	if (GetString(FillObj, TEXT("blendMode")) != TEXT("LUMINOSITY"))
	{
		return false;
	}

	if (GetString(FillObj, TEXT("scaleMode")) != TEXT("FIT"))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* TransformArr = GetArray(FillObj, TEXT("imageTransform"));
	return IsIdentityImageTransform(TransformArr);
}

TArray<FDesignFill> ParseFills(const TArray<TSharedPtr<FJsonValue>>& Fills)
{
	TArray<FDesignFill> Result;
	for (int32 Index = 0; Index < Fills.Num(); ++Index)
	{
		auto FillObj = Fills[Index]->AsObject();
		if (!FillObj || !GetBoolean(FillObj, TEXT("visible"), true))
			continue;
		if (IsSyntheticVectorRasterFill(FillObj))
			continue;

		FDesignFill Fill = ParseSingleFill(FillObj);
		if (Fill.Type != EDesignFillType::None)
			Result.Add(MoveTemp(Fill));
	}
	return Result;
}

FDesignFill ParseFill(const TArray<TSharedPtr<FJsonValue>>& Fills)
{
	for (int32 Index = Fills.Num() - 1; Index >= 0; --Index)
	{
		auto FillObj = Fills[Index]->AsObject();
		if (!FillObj || !GetBoolean(FillObj, TEXT("visible"), true))
			continue;
		if (IsSyntheticVectorRasterFill(FillObj))
			continue;

		FDesignFill Fill = ParseSingleFill(FillObj);
		if (Fill.Type != EDesignFillType::None)
			return Fill;
	}
	return FDesignFill();
}

FDesignStroke ParseStroke(const TSharedPtr<FJsonObject>& NodeObj)
{
	FDesignStroke Result;
	bool bHasVisibleStroke = false;

	const auto* Strokes = GetArray(NodeObj, TEXT("strokes"));
	if (Strokes)
	{
		for (const auto& StrokeVal : *Strokes)
		{
			auto StrokeObj = StrokeVal->AsObject();
			if (!StrokeObj || !GetBoolean(StrokeObj, TEXT("visible"), true))
				continue;

			bHasVisibleStroke = true;
			auto ColorObj = GetObject(StrokeObj, TEXT("color"));
			float StrokeOpacity = GetNumber(StrokeObj, TEXT("opacity"), 1.0);
			Result.Fill = FDesignFill::FromColor(ColorToHex(ColorObj, StrokeOpacity));
			break;
		}
	}

	if (!bHasVisibleStroke)
	{
		Result.Thickness = FDesignStrokeThickness();
		return Result;
	}

	FString Align = GetString(NodeObj, TEXT("strokeAlign"), TEXT("INSIDE"));
	if (Align == TEXT("OUTSIDE"))
		Result.Align = EDesignStrokeAlign::Outside;
	else if (Align == TEXT("CENTER"))
		Result.Align = EDesignStrokeAlign::Center;
	else
		Result.Align = EDesignStrokeAlign::Inside;

	auto IndividualWeights = GetObject(NodeObj, TEXT("individualStrokeWeights"));
	if (IndividualWeights)
	{
		Result.Thickness.bUniform = false;
		Result.Thickness.Top = GetNumber(IndividualWeights, TEXT("top"));
		Result.Thickness.Right = GetNumber(IndividualWeights, TEXT("right"));
		Result.Thickness.Bottom = GetNumber(IndividualWeights, TEXT("bottom"));
		Result.Thickness.Left = GetNumber(IndividualWeights, TEXT("left"));
	}
	else
	{
		Result.Thickness.bUniform = true;
		Result.Thickness.Uniform = GetNumber(NodeObj, TEXT("strokeWeight"));
	}

	return Result;
}

FDesignCornerRadius ParseCornerRadius(const TSharedPtr<FJsonObject>& NodeObj)
{
	FDesignCornerRadius Result;

	const auto* Radii = GetArray(NodeObj, TEXT("rectangleCornerRadii"));
	if (Radii && Radii->Num() == 4)
	{
		const float TopLeft = (*Radii)[0]->AsNumber();
		const float TopRight = (*Radii)[1]->AsNumber();
		const float BottomRight = (*Radii)[2]->AsNumber();
		const float BottomLeft = (*Radii)[3]->AsNumber();
		const bool bAllEqual =
			FMath::IsNearlyEqual(TopLeft, TopRight, KINDA_SMALL_NUMBER) &&
			FMath::IsNearlyEqual(TopLeft, BottomRight, KINDA_SMALL_NUMBER) &&
			FMath::IsNearlyEqual(TopLeft, BottomLeft, KINDA_SMALL_NUMBER);

		if (bAllEqual)
		{
			Result.bUniform = true;
			Result.Uniform = TopLeft;
		}
		else
		{
			Result.bUniform = false;
			Result.TopLeft = TopLeft;
			Result.TopRight = TopRight;
			Result.BottomRight = BottomRight;
			Result.BottomLeft = BottomLeft;
		}
	}
	else
	{
		Result.bUniform = true;
		Result.Uniform = GetNumber(NodeObj, TEXT("cornerRadius"));
	}

	return Result;
}

TArray<FDesignEffect> ParseEffects(const TArray<TSharedPtr<FJsonValue>>& Effects)
{
	TArray<FDesignEffect> Result;

	for (const auto& EffVal : Effects)
	{
		auto EffObj = EffVal->AsObject();
		if (!EffObj || !GetBoolean(EffObj, TEXT("visible"), true))
			continue;

		FString EffType = GetString(EffObj, TEXT("type"));
		FDesignEffect Eff;
		EffObj->TryGetStringField(TEXT("blendMode"), Eff.BlendMode);

		if (EffType == TEXT("DROP_SHADOW") || EffType == TEXT("INNER_SHADOW"))
		{
			Eff.Type = EDesignEffectType::Shadow;
			Eff.Shadow.bInner = (EffType == TEXT("INNER_SHADOW"));
			Eff.Shadow.BlendMode = Eff.BlendMode;

			auto Offset = GetObject(EffObj, TEXT("offset"));
			Eff.Shadow.OffsetX = Offset ? GetNumber(Offset, TEXT("x")) : 0.0f;
			Eff.Shadow.OffsetY = Offset ? GetNumber(Offset, TEXT("y")) : 0.0f;
			Eff.Shadow.Blur = GetNumber(EffObj, TEXT("radius"));
			Eff.Shadow.Spread = GetNumber(EffObj, TEXT("spread"));
			float ShadowOpacity = GetNumber(EffObj, TEXT("opacity"), 1.0);
			Eff.Shadow.Color = ColorToHex(GetObject(EffObj, TEXT("color")), ShadowOpacity);
		}
		else if (EffType == TEXT("LAYER_BLUR"))
		{
			Eff.Type = EDesignEffectType::Blur;
			Eff.Radius = GetNumber(EffObj, TEXT("radius"));
		}
		else if (EffType == TEXT("BACKGROUND_BLUR"))
		{
			Eff.Type = EDesignEffectType::BackgroundBlur;
			Eff.Radius = GetNumber(EffObj, TEXT("radius"));
		}
		else
		{
			continue;
		}

		Result.Add(Eff);
	}

	return Result;
}

void ParseTextProperties(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
{
	OutNode.Text.Content = GetString(NodeObj, TEXT("characters"));

	auto Style = GetObject(NodeObj, TEXT("style"));
	if (!Style) return;

	OutNode.Text.FontFamily = GetString(Style, TEXT("fontFamily"));
	OutNode.Text.FontSize = GetNumber(Style, TEXT("fontSize"), 16.0);
	OutNode.Text.FontStyle = GetString(Style, TEXT("fontStyle"), TEXT("normal"));

	int32 Weight = FMath::RoundToInt(GetNumber(Style, TEXT("fontWeight"), 400.0));
	Weight = FMath::Clamp(FMath::RoundToInt(Weight / 100.0f) * 100, 100, 900);
	OutNode.Text.FontWeight = FString::FromInt(Weight);

	FString LineHeightUnit = GetString(Style, TEXT("lineHeightUnit"));
	if (LineHeightUnit != TEXT("AUTO"))
	{
		if (LineHeightUnit == TEXT("PIXELS"))
		{
			const float LineHeightPx = GetNumber(Style, TEXT("lineHeightPx"));
			if (LineHeightPx > 0.0f && OutNode.Text.FontSize > 0.0f)
				OutNode.Text.LineHeight = LineHeightPx / OutNode.Text.FontSize;
		}
		else if (LineHeightUnit == TEXT("FONT_SIZE_%"))
		{
			const float Percent = GetNumber(Style, TEXT("lineHeightPercentFontSize"));
			if (Percent > 0.0f)
				OutNode.Text.LineHeight = Percent / 100.0f;
		}
		else
		{
			const float Percent = GetNumber(Style, TEXT("lineHeightPercentFontSize"));
			if (Percent > 0.0f)
				OutNode.Text.LineHeight = Percent / 100.0f;
		}
	}

	OutNode.Text.LetterSpacing = GetNumber(Style, TEXT("letterSpacing"));
	FString TextDecoration = GetString(Style, TEXT("textDecoration"));
	OutNode.Text.bUnderline = (TextDecoration == TEXT("UNDERLINE"));
	OutNode.Text.bStrikethrough = (TextDecoration == TEXT("STRIKETHROUGH"));
	if (const TSharedPtr<FJsonObject> HyperlinkObj = GetObject(NodeObj, TEXT("hyperlink")))
	{
		OutNode.Text.Href = GetString(HyperlinkObj, TEXT("url"));
	}

	FString HAlign = GetString(Style, TEXT("textAlignHorizontal"), TEXT("LEFT"));
	if (HAlign == TEXT("CENTER"))
		OutNode.Text.TextAlign = EDesignTextAlign::Center;
	else if (HAlign == TEXT("RIGHT"))
		OutNode.Text.TextAlign = EDesignTextAlign::Right;
	else if (HAlign == TEXT("JUSTIFIED"))
		OutNode.Text.TextAlign = EDesignTextAlign::Justify;
	else
		OutNode.Text.TextAlign = EDesignTextAlign::Left;

	FString VAlign = GetString(Style, TEXT("textAlignVertical"), TEXT("TOP"));
	if (VAlign == TEXT("CENTER"))
		OutNode.Text.TextAlignVertical = EDesignTextAlignVertical::Middle;
	else if (VAlign == TEXT("BOTTOM"))
		OutNode.Text.TextAlignVertical = EDesignTextAlignVertical::Bottom;
	else
		OutNode.Text.TextAlignVertical = EDesignTextAlignVertical::Top;

	FString AutoResize = GetString(NodeObj, TEXT("textAutoResize"));
	if (AutoResize.IsEmpty())
	{
		AutoResize = GetString(Style, TEXT("textAutoResize"), TEXT("NONE"));
	}
	if (AutoResize == TEXT("WIDTH_AND_HEIGHT"))
		OutNode.Text.TextGrowth = EDesignTextGrowth::Auto;
	else if (AutoResize == TEXT("HEIGHT"))
		OutNode.Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	else if (AutoResize == TEXT("TRUNCATE"))
	{
		OutNode.Text.TextGrowth = EDesignTextGrowth::FixedWidthHeight;
		OutNode.Metadata.Add(TEXT("source.text.truncate"), TEXT("true"));
	}
	else
		OutNode.Text.TextGrowth = EDesignTextGrowth::FixedWidthHeight;
}
