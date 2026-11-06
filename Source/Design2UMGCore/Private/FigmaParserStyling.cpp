// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserStyling.h"
#include "FigmaParserLayout.h"
#include "DesignColorUtils.h"
#include "DesignJsonUtils.h"
#include "ParserSharedSemantics.h"
#include "Design2UMGLog.h"

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

		return DesignColorUtils::FormatLiteral(FColor(R, G, B, Alpha));
	}

	TArray<FVector2D> ReadGradientHandles(const TSharedPtr<FJsonObject>& Paint, bool bLinear)
	{
		TArray<FVector2D> Handles;
		if (const auto Field = Paint->TryGetField(TEXT("gradientTransform")); Field.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Matrix = nullptr;
			FDesignImageTransform Transform;
			if (!Field->TryGetArray(Matrix) || !ParserSharedSemantics::TryParseImageTransformMatrix(*Matrix,
				[](const TSharedPtr<FJsonValue>& Value, float& Number) { return Value->Type == EJson::Number && Value->TryGetNumber(Number); }, Transform))
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaParser: Invalid gradientTransform; expected a finite 2x3 matrix."));
				return Handles;
			}
			const double Determinant = double(Transform.A) * Transform.D - double(Transform.B) * Transform.C;
			if (Determinant == 0)
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaParser: Singular gradientTransform cannot define gradient coordinates."));
				return Handles;
			}
			// Paint matrices map object coordinates into gradient space; invert to recover handles.
			for (const FVector2D Point : {FVector2D(bLinear ? 0 : 0.5, 0.5), FVector2D(1, 0.5), FVector2D(bLinear ? 0 : 0.5, 1)})
			{
				const double X = Point.X - Transform.TX, Y = Point.Y - Transform.TY;
				Handles.Emplace((Transform.D * X - Transform.B * Y) / Determinant,
					(Transform.A * Y - Transform.C * X) / Determinant);
			}
		}
		else if (const auto* Positions = GetArray(Paint, TEXT("gradientHandlePositions")))
		{
			for (const auto& Position : *Positions)
			{
				if (const auto Point = Position->AsObject(); Point.IsValid())
					Handles.Emplace(GetNumber(Point, TEXT("x")), GetNumber(Point, TEXT("y")));
			}
		}
		return Handles;
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
			Result.SolidColor = ColorToHex(GetObject(FillObj, TEXT("color")));
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

			const bool bLinear = Result.Gradient.GradientType == EDesignGradientType::Linear;
			const TArray<FVector2D> Handles = ReadGradientHandles(FillObj, bLinear);
			if (Handles.Num() >= 2)
			{
				const FVector2D Axis = Handles[1] - Handles[0];
				Result.Gradient.Rotation = FMath::RadiansToDegrees(FMath::Atan2(Axis.Y, Axis.X));
				if (bLinear)
				{
					Result.Gradient.StartPoint = Handles[0];
					Result.Gradient.EndPoint = Handles[1];
				}
				else
				{
					Result.Gradient.Center = Handles[0];
					if (Result.Gradient.GradientType == EDesignGradientType::Radial)
					{
						const double Width = FMath::Max(double(KINDA_SMALL_NUMBER), 2 * Axis.Size());
						const double Height = Handles.Num() >= 3
							? FMath::Max(double(KINDA_SMALL_NUMBER), 2 * (Handles[2] - Handles[0]).Size()) : Width;
						Result.Gradient.Size = FVector2D(Width, Height);
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
					Stop.Color = ColorToHex(GetObject(StopObj, TEXT("color")));
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
				Result.Image.bFitCropToNode = true;
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

			if (const TSharedPtr<FJsonValue> TransformField = FillObj->TryGetField(TEXT("imageTransform")); TransformField.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* Matrix = nullptr;
				if (!TransformField->TryGetArray(Matrix) || !ParserSharedSemantics::TryParseImageTransformMatrix(
					*Matrix,
					[](const TSharedPtr<FJsonValue>& Value, float& OutNumber) { return Value->Type == EJson::Number && Value->TryGetNumber(OutNumber); },
					Result.Image.Transform))
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaParser: Invalid imageTransform; expected a 2x3 matrix of finite numbers. Ignoring transform."));
				}
			}

			return Result;
		}

		return Result;
	}
}

TArray<FDesignFill> ParseFills(const TArray<TSharedPtr<FJsonValue>>& Fills)
{
	TArray<FDesignFill> Result;
	for (const TSharedPtr<FJsonValue>& FillVal : Fills)
	{
		const TSharedPtr<FJsonObject> FillObj = FillVal->AsObject();
		if (!FillObj.IsValid() || !GetBool(FillObj, TEXT("visible"), true))
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

			const FString PaintType = GetString(StrokeObj, TEXT("type"));
			if (PaintType != TEXT("SOLID") && !PaintType.StartsWith(TEXT("GRADIENT_")))
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaParser: Unsupported stroke paint '%s' on node '%s'; skipping paint."),
					*PaintType, *GetString(NodeObj, TEXT("id")));
				continue;
			}

			bHasVisibleStroke = true;
			Result.Fill = ParseSingleFill(StrokeObj);
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
	// Figma strokes overlay content unless an auto layout frame includes them in layout.
	Result.bIncludedInLayout = HasAutoLayout(NodeObj) && GetBool(NodeObj, TEXT("strokesIncludedInLayout"), false);

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
	Result.Smoothing = GetNumber(NodeObj, TEXT("cornerSmoothing"));

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
			Effect.Shadow.bShowShadowBehindNode = GetBool(EffObj, TEXT("showShadowBehindNode"), false);
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
