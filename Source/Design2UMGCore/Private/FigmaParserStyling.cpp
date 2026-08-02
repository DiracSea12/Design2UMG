// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserStyling.h"
#include "DesignJsonUtils.h"

namespace FigmaParserInternal
{
using DesignJsonUtils::GetArray;
using DesignJsonUtils::GetBool;
using DesignJsonUtils::GetNumber;
using DesignJsonUtils::GetObject;
using DesignJsonUtils::GetString;

namespace
{
	FString ColorToHex(const TSharedPtr<FJsonObject>& ColorObj, const float Opacity = 1.0f)
	{
		if (!ColorObj.IsValid())
		{
			return TEXT("#000000");
		}

		const uint8 R = FMath::Clamp(FMath::RoundToInt(GetNumber(ColorObj, TEXT("r")) * 255.0), 0, 255);
		const uint8 G = FMath::Clamp(FMath::RoundToInt(GetNumber(ColorObj, TEXT("g")) * 255.0), 0, 255);
		const uint8 B = FMath::Clamp(FMath::RoundToInt(GetNumber(ColorObj, TEXT("b")) * 255.0), 0, 255);
		const float A = GetNumber(ColorObj, TEXT("a"), 1.0) * Opacity;
		const uint8 Alpha = FMath::Clamp(FMath::RoundToInt(A * 255.0), 0, 255);

		if (Alpha >= 255)
		{
			return FString::Printf(TEXT("#%02X%02X%02X"), R, G, B);
		}

		return FString::Printf(TEXT("#%02X%02X%02X%02X"), R, G, B, Alpha);
	}

	bool IsSyntheticVectorRasterFill(const TSharedPtr<FJsonObject>& FillObj)
	{
		if (!FillObj.IsValid() || GetString(FillObj, TEXT("type")) != TEXT("IMAGE"))
		{
			return false;
		}

		return GetString(FillObj, TEXT("imageRef")).StartsWith(TEXT("vec_"));
	}

	FDesignFill ParseSingleFill(const TSharedPtr<FJsonObject>& FillObj)
	{
		FDesignFill Result;
		if (!FillObj.IsValid() || !GetBool(FillObj, TEXT("visible"), true))
		{
			return Result;
		}

		const FString FillType = GetString(FillObj, TEXT("type"));
		const float FillOpacity = GetNumber(FillObj, TEXT("opacity"), 1.0);
		Result.Opacity = FillOpacity;
		FillObj->TryGetStringField(TEXT("blendMode"), Result.BlendMode);

		if (FillType == TEXT("SOLID"))
		{
			Result.Type = EDesignFillType::Color;
			Result.SolidColor = ColorToHex(GetObject(FillObj, TEXT("color")), FillOpacity);
			return Result;
		}

		if (FillType == TEXT("GRADIENT_LINEAR")
			|| FillType == TEXT("GRADIENT_RADIAL")
			|| FillType == TEXT("GRADIENT_ANGULAR")
			|| FillType == TEXT("GRADIENT_DIAMOND"))
		{
			Result.Type = EDesignFillType::Gradient;

			if (FillType == TEXT("GRADIENT_LINEAR"))
			{
				Result.Gradient.GradientType = EDesignGradientType::Linear;
			}
			else if (FillType == TEXT("GRADIENT_RADIAL") || FillType == TEXT("GRADIENT_DIAMOND"))
			{
				Result.Gradient.GradientType = EDesignGradientType::Radial;
			}
			else
			{
				Result.Gradient.GradientType = EDesignGradientType::Angular;
			}

			const TArray<TSharedPtr<FJsonValue>>* Handles = GetArray(FillObj, TEXT("gradientHandlePositions"));
			if (Handles && Handles->Num() >= 2)
			{
				const TSharedPtr<FJsonObject> P0 = (*Handles)[0]->AsObject();
				const TSharedPtr<FJsonObject> P1 = (*Handles)[1]->AsObject();
				if (P0.IsValid() && P1.IsValid())
				{
					const float CenterX = GetNumber(P0, TEXT("x"));
					const float CenterY = GetNumber(P0, TEXT("y"));
					const float DX = GetNumber(P1, TEXT("x")) - CenterX;
					const float DY = GetNumber(P1, TEXT("y")) - CenterY;
					Result.Gradient.Rotation = FMath::RadiansToDegrees(FMath::Atan2(DY, DX));

					if (Result.Gradient.GradientType == EDesignGradientType::Linear)
					{
						Result.Gradient.StartPoint = FVector2D(CenterX, CenterY);
						Result.Gradient.EndPoint = FVector2D(CenterX + DX, CenterY + DY);
					}
					else if (Result.Gradient.GradientType == EDesignGradientType::Radial)
					{
						Result.Gradient.Center = FVector2D(CenterX, CenterY);
						FVector2D Size(FMath::Max(KINDA_SMALL_NUMBER, 2.0f * FMath::Sqrt(DX * DX + DY * DY)), 0.0f);
						Size.Y = Size.X;

						if (Handles->Num() >= 3)
						{
							const TSharedPtr<FJsonObject> P2 = (*Handles)[2]->AsObject();
							if (P2.IsValid())
							{
								const float DYHandleX = GetNumber(P2, TEXT("x")) - CenterX;
								const float DYHandleY = GetNumber(P2, TEXT("y")) - CenterY;
								Size.Y = FMath::Max(
									KINDA_SMALL_NUMBER,
									2.0f * FMath::Sqrt(DYHandleX * DYHandleX + DYHandleY * DYHandleY));
							}
						}

						Result.Gradient.Size = Size;
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* Stops = GetArray(FillObj, TEXT("gradientStops"));
			if (Stops)
			{
				for (const TSharedPtr<FJsonValue>& StopVal : *Stops)
				{
					const TSharedPtr<FJsonObject> StopObj = StopVal->AsObject();
					if (!StopObj.IsValid())
					{
						continue;
					}

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

			const FString ScaleMode = GetString(FillObj, TEXT("scaleMode"));
			if (ScaleMode == TEXT("FILL"))
			{
				Result.Image.Mode = EDesignImageMode::Fill;
			}
			else if (ScaleMode == TEXT("FIT"))
			{
				Result.Image.Mode = EDesignImageMode::Fit;
			}
			else if (ScaleMode == TEXT("CROP"))
			{
				Result.Image.Mode = EDesignImageMode::Crop;
			}
			else
			{
				Result.Image.Mode = EDesignImageMode::Stretch;
			}

			double ScalingFactor = 0.0;
			if (FillObj->TryGetNumberField(TEXT("scalingFactor"), ScalingFactor))
			{
				Result.Image.ScalingFactor = static_cast<float>(ScalingFactor);
			}

			const TArray<TSharedPtr<FJsonValue>>* TransformArr = GetArray(FillObj, TEXT("imageTransform"));
			if (TransformArr && TransformArr->Num() == 2)
			{
				const TArray<TSharedPtr<FJsonValue>> Row0 = (*TransformArr)[0]->AsArray();
				const TArray<TSharedPtr<FJsonValue>> Row1 = (*TransformArr)[1]->AsArray();
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
}

FString ExtractSyntheticVectorRasterImageRef(const TArray<TSharedPtr<FJsonValue>>& Fills)
{
	for (const TSharedPtr<FJsonValue>& FillVal : Fills)
	{
		const TSharedPtr<FJsonObject> FillObj = FillVal->AsObject();
		if (!IsSyntheticVectorRasterFill(FillObj) || !GetBool(FillObj, TEXT("visible"), true))
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

TArray<FDesignFill> ParseFills(const TArray<TSharedPtr<FJsonValue>>& Fills)
{
	TArray<FDesignFill> Result;
	for (const TSharedPtr<FJsonValue>& FillVal : Fills)
	{
		const TSharedPtr<FJsonObject> FillObj = FillVal->AsObject();
		if (!FillObj.IsValid() || !GetBool(FillObj, TEXT("visible"), true) || IsSyntheticVectorRasterFill(FillObj))
		{
			continue;
		}

		FDesignFill Fill = ParseSingleFill(FillObj);
		if (Fill.Type != EDesignFillType::None)
		{
			Result.Add(MoveTemp(Fill));
		}
	}

	return Result;
}

FDesignStroke ParseStroke(const TSharedPtr<FJsonObject>& NodeObj)
{
	FDesignStroke Result;
	bool bHasVisibleStroke = false;

	const TArray<TSharedPtr<FJsonValue>>* Strokes = GetArray(NodeObj, TEXT("strokes"));
	if (Strokes)
	{
		for (const TSharedPtr<FJsonValue>& StrokeVal : *Strokes)
		{
			const TSharedPtr<FJsonObject> StrokeObj = StrokeVal->AsObject();
			if (!StrokeObj.IsValid() || !GetBool(StrokeObj, TEXT("visible"), true))
			{
				continue;
			}

			bHasVisibleStroke = true;
			const float StrokeOpacity = GetNumber(StrokeObj, TEXT("opacity"), 1.0);
			Result.Fill = FDesignFill::FromColor(ColorToHex(GetObject(StrokeObj, TEXT("color")), StrokeOpacity));
			break;
		}
	}

	if (!bHasVisibleStroke)
	{
		Result.Thickness = FDesignStrokeThickness();
		return Result;
	}

	const FString Align = GetString(NodeObj, TEXT("strokeAlign"), TEXT("INSIDE"));
	if (Align == TEXT("OUTSIDE"))
	{
		Result.Align = EDesignStrokeAlign::Outside;
	}
	else if (Align == TEXT("CENTER"))
	{
		Result.Align = EDesignStrokeAlign::Center;
	}
	else
	{
		Result.Align = EDesignStrokeAlign::Inside;
	}

	const TSharedPtr<FJsonObject> IndividualWeights = GetObject(NodeObj, TEXT("individualStrokeWeights"));
	if (IndividualWeights.IsValid())
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

	const TArray<TSharedPtr<FJsonValue>>* Radii = GetArray(NodeObj, TEXT("rectangleCornerRadii"));
	if (Radii && Radii->Num() == 4)
	{
		const float TopLeft = (*Radii)[0]->AsNumber();
		const float TopRight = (*Radii)[1]->AsNumber();
		const float BottomRight = (*Radii)[2]->AsNumber();
		const float BottomLeft = (*Radii)[3]->AsNumber();
		const bool bAllEqual =
			FMath::IsNearlyEqual(TopLeft, TopRight, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(TopLeft, BottomRight, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(TopLeft, BottomLeft, KINDA_SMALL_NUMBER);

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

	for (const TSharedPtr<FJsonValue>& EffVal : Effects)
	{
		const TSharedPtr<FJsonObject> EffObj = EffVal->AsObject();
		if (!EffObj.IsValid() || !GetBool(EffObj, TEXT("visible"), true))
		{
			continue;
		}

		const FString EffType = GetString(EffObj, TEXT("type"));
		FDesignEffect Effect;
		EffObj->TryGetStringField(TEXT("blendMode"), Effect.BlendMode);

		if (EffType == TEXT("DROP_SHADOW") || EffType == TEXT("INNER_SHADOW"))
		{
			Effect.Type = EDesignEffectType::Shadow;
			Effect.Shadow.bInner = (EffType == TEXT("INNER_SHADOW"));
			Effect.Shadow.BlendMode = Effect.BlendMode;

			const TSharedPtr<FJsonObject> Offset = GetObject(EffObj, TEXT("offset"));
			Effect.Shadow.OffsetX = Offset.IsValid() ? GetNumber(Offset, TEXT("x")) : 0.0f;
			Effect.Shadow.OffsetY = Offset.IsValid() ? GetNumber(Offset, TEXT("y")) : 0.0f;
			Effect.Shadow.Blur = GetNumber(EffObj, TEXT("radius"));
			Effect.Shadow.Spread = GetNumber(EffObj, TEXT("spread"));
			const float ShadowOpacity = GetNumber(EffObj, TEXT("opacity"), 1.0);
			Effect.Shadow.Color = ColorToHex(GetObject(EffObj, TEXT("color")), ShadowOpacity);
		}
		else if (EffType == TEXT("LAYER_BLUR"))
		{
			Effect.Type = EDesignEffectType::Blur;
			Effect.Radius = GetNumber(EffObj, TEXT("radius"));
		}
		else if (EffType == TEXT("BACKGROUND_BLUR"))
		{
			Effect.Type = EDesignEffectType::BackgroundBlur;
			Effect.Radius = GetNumber(EffObj, TEXT("radius"));
		}
		else
		{
			continue;
		}

		Result.Add(MoveTemp(Effect));
	}

	return Result;
}
}
