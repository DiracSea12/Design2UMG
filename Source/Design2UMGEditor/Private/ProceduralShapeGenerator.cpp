// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "ProceduralShapeGenerator.h"

#include "ProceduralShapeGeneratorAssets.h"
#include "DesignImageResourceLimits.h"
#include "Design2UMGLog.h"
#include "UMGValueParsing.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "TextureResource.h"
#include <cmath>

namespace
{
	using FGeneratedGradientStop = ProceduralShapeGeneratorAssets::FGradientStop;

	constexpr TCHAR MaterialParamColorKey[] = TEXT("Color");
	constexpr TCHAR MaterialParamColorStartKey[] = TEXT("ColorStart");
	constexpr TCHAR MaterialParamColorCenterKey[] = TEXT("ColorCenter");
	constexpr TCHAR MaterialParamColorEndKey[] = TEXT("ColorEnd");
	constexpr TCHAR MaterialParamColorEdgeKey[] = TEXT("ColorEdge");
	constexpr TCHAR MaterialParamCornerRadiiKey[] = TEXT("CornerRadii");
	constexpr TCHAR MaterialParamStartXKey[] = TEXT("StartX");
	constexpr TCHAR MaterialParamStartYKey[] = TEXT("StartY");
	constexpr TCHAR MaterialParamEndXKey[] = TEXT("EndX");
	constexpr TCHAR MaterialParamEndYKey[] = TEXT("EndY");
	constexpr TCHAR MaterialParamCenterXKey[] = TEXT("CenterX");
	constexpr TCHAR MaterialParamCenterYKey[] = TEXT("CenterY");
	constexpr TCHAR MaterialParamSizeWidthKey[] = TEXT("SizeWidth");
	constexpr TCHAR MaterialParamSizeHeightKey[] = TEXT("SizeHeight");
	constexpr TCHAR MaterialParamBlurPaddingKey[] = TEXT("BlurPadding");
	constexpr TCHAR MaterialParamThicknessKey[] = TEXT("Thickness");
	constexpr TCHAR MaterialParamCornerRadiusKey[] = TEXT("CornerRadius");
	constexpr TCHAR MaterialParamRotationKey[] = TEXT("Rotation");
	constexpr TCHAR MaterialParamStripeColorKey[] = TEXT("StripeColor");
	constexpr TCHAR MaterialParamStripeWidthKey[] = TEXT("StripeWidth");
	constexpr TCHAR MaterialParamStripeGapKey[] = TEXT("StripeGap");
	constexpr TCHAR MaterialParamInsetKey[] = TEXT("Inset");
	constexpr TCHAR MaterialParamStopsKey[] = TEXT("Stops");
	constexpr TCHAR MaterialParamStopPosKey[] = TEXT("Pos");

	bool ReadTexturePixels(UTexture2D* Texture, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		if (!Texture)
		{
			return false;
		}

#if WITH_EDITORONLY_DATA
		FImage SourceImage;
		if (Texture->Source.IsValid()
			&& !DesignImageResources::FImageResourceLimits().AllowsDimensions(Texture->Source.GetSizeX(), Texture->Source.GetSizeY())) return false;
		if (Texture->Source.IsValid() && Texture->Source.GetMipImage(SourceImage, 0))
		{
			SourceImage.ChangeFormat(ERawImageFormat::BGRA8, SourceImage.GammaSpace);
			OutWidth = SourceImage.SizeX;
			OutHeight = SourceImage.SizeY;
			const int64 PixelCount = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight);
			const int64 ExpectedBytes = PixelCount * sizeof(FColor);
			if (OutWidth <= 0 || OutHeight <= 0 || SourceImage.RawData.Num() < ExpectedBytes)
			{
				return false;
			}

			OutPixels.SetNumUninitialized(PixelCount);
			FMemory::Memcpy(OutPixels.GetData(), SourceImage.RawData.GetData(), ExpectedBytes);
			return true;
		}
#endif

		FTexturePlatformData* PlatformData = Texture->GetPlatformData();
		if (!PlatformData || PlatformData->Mips.Num() == 0 || Texture->GetPixelFormat() != PF_B8G8R8A8)
		{
			return false;
		}

		FTexture2DMipMap& Mip = PlatformData->Mips[0];
		OutWidth = Mip.SizeX;
		OutHeight = Mip.SizeY;
		if (!DesignImageResources::FImageResourceLimits().AllowsDimensions(OutWidth, OutHeight)) return false;
		const int64 PixelCount = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight);
		const int64 ExpectedBytes = PixelCount * sizeof(FColor);
		if (OutWidth <= 0 || OutHeight <= 0 || Mip.BulkData.GetBulkDataSize() < ExpectedBytes)
		{
			return false;
		}

		const void* RawData = Mip.BulkData.LockReadOnly();
		if (!RawData)
		{
			Mip.BulkData.Unlock();
			return false;
		}

		OutPixels.SetNumUninitialized(PixelCount);
		FMemory::Memcpy(OutPixels.GetData(), RawData, ExpectedBytes);
		Mip.BulkData.Unlock();
		return true;
	}
}

void FProceduralShapeGenerator::ReleaseTransientTexture(UTexture2D* Texture)
{
	ProceduralShapeGeneratorAssets::ReleaseTransientTexture(Texture);
}

void FProceduralShapeGenerator::ReleaseTransientTextures()
{
	ProceduralShapeGeneratorAssets::ReleaseTransientTextures();
}

void FProceduralShapeGenerator::FlushPendingTextureUpdates()
{
	ProceduralShapeGeneratorAssets::FlushPendingTextureUpdates();
}

UTexture2D* FProceduralShapeGenerator::ApplyAffineCrop(
	UTexture2D* Source,
	float A,
	float B,
	float TX,
	float C,
	float D,
	float TY,
	int32 OutW,
	int32 OutH,
	FString* OutError)
{
	if (OutError) OutError->Reset();
	if (!DesignImageResources::FImageResourceLimits().AllowsDimensions(OutW, OutH)
		|| !FMath::IsFinite(A) || !FMath::IsFinite(B) || !FMath::IsFinite(TX)
		|| !FMath::IsFinite(C) || !FMath::IsFinite(D) || !FMath::IsFinite(TY))
	{
		const FString Error = TEXT("Invalid or excessive affine crop dimensions or transform");
		if (OutError) *OutError = Error;
		else UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Error);
		return nullptr;
	}
	if (!Source)
	{
		return nullptr;
	}

	TArray<FColor> SourcePixels;
	int32 SrcW = 0;
	int32 SrcH = 0;
	if (!ReadTexturePixels(Source, SourcePixels, SrcW, SrcH))
	{
		return nullptr;
	}

	const FColor* SrcData = SourcePixels.GetData();
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(OutW * OutH);

	for (int32 Y = 0; Y < OutH; ++Y)
	{
		const float NY = static_cast<float>(Y) / FMath::Max(1, OutH - 1);
		for (int32 X = 0; X < OutW; ++X)
		{
			const float NX = static_cast<float>(X) / FMath::Max(1, OutW - 1);
			// Figma imageTransform maps destination-normalized coordinates to source UVs.
			const float SU = A * NX + B * NY + TX;
			const float SV = C * NX + D * NY + TY;
			const float SX = SU * (SrcW - 1);
			const float SY = SV * (SrcH - 1);

			if (SX < -0.5f || SX >= SrcW || SY < -0.5f || SY >= SrcH)
			{
				Pixels[Y * OutW + X] = FColor(0, 0, 0, 0);
				continue;
			}

			const int32 X0 = FMath::Clamp(FMath::FloorToInt(SX), 0, SrcW - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt(SY), 0, SrcH - 1);
			const int32 X1 = FMath::Min(X0 + 1, SrcW - 1);
			const int32 Y1 = FMath::Min(Y0 + 1, SrcH - 1);
			const float FX = SX - FMath::FloorToFloat(SX);
			const float FY = SY - FMath::FloorToFloat(SY);

			const FColor& C00 = SrcData[Y0 * SrcW + X0];
			const FColor& C10 = SrcData[Y0 * SrcW + X1];
			const FColor& C01 = SrcData[Y1 * SrcW + X0];
			const FColor& C11 = SrcData[Y1 * SrcW + X1];

			auto LerpChannel = [](uint8 V0, uint8 V1, float T) -> uint8
			{
				return FMath::RoundToInt(V0 + (V1 - V0) * T);
			};
			auto BilinearChannel = [&](uint8 V00, uint8 V10, uint8 V01, uint8 V11) -> uint8
			{
				const uint8 Top = LerpChannel(V00, V10, FX);
				const uint8 Bottom = LerpChannel(V01, V11, FX);
				return LerpChannel(Top, Bottom, FY);
			};

			FColor& OutPixel = Pixels[Y * OutW + X];
			OutPixel.R = BilinearChannel(C00.R, C10.R, C01.R, C11.R);
			OutPixel.G = BilinearChannel(C00.G, C10.G, C01.G, C11.G);
			OutPixel.B = BilinearChannel(C00.B, C10.B, C01.B, C11.B);
			OutPixel.A = BilinearChannel(C00.A, C10.A, C01.A, C11.A);
		}
	}

	UTexture2D* Result = ProceduralShapeGeneratorAssets::CreateTransientTexture(OutW, OutH, nullptr);
	if (!Result)
	{
		return nullptr;
	}

	ProceduralShapeGeneratorAssets::FillTextureData(Result, Pixels);
	return Result;
}

namespace
{
	// Rendering parameters shared by typed gradients and explicit shape inputs.
	struct FMaterialRefParams
	{
		FLinearColor Color = FLinearColor::White;
		FLinearColor ColorStart = FLinearColor::White;
		FLinearColor ColorEnd = FLinearColor::Black;
		float Thickness = 0.1f;
		float CornerRadius = 0.2f;
		float CornerRadii[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
		float RotationDeg = 0.0f;
		FLinearColor StripeColor = FLinearColor(0.67f, 0.58f, 0.94f, 0.66f);
		int32 StripeWidthPx = 2;
		int32 StripeGapPx = 6;
		int32 InsetPx = 10;
		float BoxShadowBlurPadding = -1.0f;
		TArray<FGeneratedGradientStop> MultiStops;
		FVector2D LinearStart = FVector2D(0.0f, 0.0f);
		FVector2D LinearEnd = FVector2D(1.0f, 0.0f);
		bool bHasLinearStartX = false;
		bool bHasLinearStartY = false;
		bool bHasLinearEndX = false;
		bool bHasLinearEndY = false;
		FVector2D RadialCenter = FVector2D(0.5f, 0.5f);
		FVector2D RadialSize = FVector2D(1.0f, 1.0f);
	};

	// Thickness/CornerRadius/CornerRadii arrive in two source-side conventions with no
	// unit marker in the payload: design-tool percentages (0-100) and normalized
	// fractions (0-1). Values > 1.0 are treated as percentages and divided by 100;
	// values <= 1.0 are already normalized and pass through unchanged.
	// Known ambiguity: a value like 1.5 is read as 1.5%, not 150%, and lands near
	// zero after the division. This cannot be tightened to a smarter cutoff without
	// a unit flag in the source schema: any change to the >1.0 rule re-renders every
	// existing design that relies on the current interpretation.
	float NormalizePercentageOrFraction(const double Value)
	{
		return Value > 1.0 ? static_cast<float>(Value) / 100.0f : static_cast<float>(Value);
	}

	void ApplyGradientParams(const FUMGVisualBrushData::FGradientData& Gradient, FMaterialRefParams& P)
	{
		if (Gradient.Stops.IsEmpty()) return;
		// Preserve established raster precision without serializing the internal request.
		// Typed IR retains full precision; quantization belongs at the rendering boundary.
		const auto Coordinate = [](double Value) -> float
		{
			return static_cast<float>(std::nearbyint(Value * 10000.0) / 10000.0);
		};
		const auto Color = [](const FLinearColor& Value)
		{
			return FLinearColor::FromSRGBColor(Value.ToFColor(true));
		};
		P.ColorStart = Color(Gradient.Stops[0].Color);
		P.ColorEnd = Color(Gradient.Stops.Last().Color);
		P.RotationDeg = Coordinate(Gradient.Rotation.Get(0.0f));
		const bool bLinear = !Gradient.Type.IsSet() || Gradient.Type.GetValue().Equals(TEXT("Linear"), ESearchCase::IgnoreCase);
		if (bLinear && Gradient.StartPoint.IsSet() && Gradient.EndPoint.IsSet())
		{
			P.LinearStart = FVector2D(Coordinate(Gradient.StartPoint.GetValue().X), Coordinate(Gradient.StartPoint.GetValue().Y));
			P.LinearEnd = FVector2D(Coordinate(Gradient.EndPoint.GetValue().X), Coordinate(Gradient.EndPoint.GetValue().Y));
			P.bHasLinearStartX = P.bHasLinearStartY = P.bHasLinearEndX = P.bHasLinearEndY = true;
		}
		if (Gradient.Type.IsSet() && Gradient.Type.GetValue().Equals(TEXT("Radial"), ESearchCase::IgnoreCase))
		{
			const FVector2D Center = Gradient.Center.Get(FVector2D(0.5f, 0.5f));
			const FVector2D Size = Gradient.Size.Get(FVector2D(1.0f, 1.0f));
			P.RadialCenter = FVector2D(Coordinate(Center.X), Coordinate(Center.Y));
			P.RadialSize = FVector2D(FMath::Max(KINDA_SMALL_NUMBER, Coordinate(Size.X)), FMath::Max(KINDA_SMALL_NUMBER, Coordinate(Size.Y)));
		}
		// The two-color renderer uses the first/last colors at its endpoints.
		if (Gradient.Stops.Num() > 2)
		{
			P.MultiStops.Reserve(Gradient.Stops.Num());
			for (const auto& Stop : Gradient.Stops) P.MultiStops.Add({Coordinate(Stop.Position), Color(Stop.Color)});
			P.MultiStops.Sort([](const FGeneratedGradientStop& Left, const FGeneratedGradientStop& Right)
			{
				return Left.Position < Right.Position;
			});
		}
	}

	void ApplyShapeParams(const FUMGVisualBrushData::FShapeData& Shape, FMaterialRefParams& P)
	{
		P.Color = Shape.Color.Get(P.Color);
		P.Thickness = Shape.Thickness.Get(P.Thickness);
		P.CornerRadius = Shape.CornerRadius.Get(P.CornerRadius);
		if (Shape.CornerRadii.IsSet())
		{
			for (int32 Index = 0; Index < 4; ++Index) P.CornerRadii[Index] = static_cast<float>(Shape.CornerRadii.GetValue()[Index]);
		}
		if (Shape.BlurPadding.IsSet()) P.BoxShadowBlurPadding = FMath::Max(0.0f, Shape.BlurPadding.GetValue());
		P.StripeColor = Shape.StripeColor.Get(P.StripeColor);
		P.StripeWidthPx = FMath::Max(1, Shape.StripeWidth.Get(P.StripeWidthPx));
		P.StripeGapPx = FMath::Max(1, Shape.StripeGap.Get(P.StripeGapPx));
		P.InsetPx = FMath::Max(0, Shape.Inset.Get(P.InsetPx));
	}

	// Explicit external MaterialParams input only; internal producers use typed payloads.
	void ParseMaterialRefParams(const FUMGProceduralResourceRequest& Request, FMaterialRefParams& P)
	{
		if (!Request.MaterialParams.IsSet())
		{
			return;
		}

		TSharedPtr<FJsonObject> Params;
		if (!DesignJsonUtils::TryParseObject(Request.MaterialParams.GetValue(), Params))
		{
			return;
		}

		FString ColorStr;
		if (Params->TryGetStringField(MaterialParamColorKey, ColorStr))
		{
			P.Color = FUMGValueParser::ParseColor(ColorStr);
		}
		if (Params->TryGetStringField(MaterialParamColorStartKey, ColorStr)
			|| Params->TryGetStringField(MaterialParamColorCenterKey, ColorStr))
		{
			P.ColorStart = FUMGValueParser::ParseColor(ColorStr);
		}
		if (Params->TryGetStringField(MaterialParamColorEndKey, ColorStr)
			|| Params->TryGetStringField(MaterialParamColorEdgeKey, ColorStr))
		{
			P.ColorEnd = FUMGValueParser::ParseColor(ColorStr);
		}

		double NumVal = 0.0;
		P.bHasLinearStartX = Params->TryGetNumberField(MaterialParamStartXKey, NumVal);
		if (P.bHasLinearStartX)
		{
			P.LinearStart.X = static_cast<float>(NumVal);
		}
		P.bHasLinearStartY = Params->TryGetNumberField(MaterialParamStartYKey, NumVal);
		if (P.bHasLinearStartY)
		{
			P.LinearStart.Y = static_cast<float>(NumVal);
		}
		P.bHasLinearEndX = Params->TryGetNumberField(MaterialParamEndXKey, NumVal);
		if (P.bHasLinearEndX)
		{
			P.LinearEnd.X = static_cast<float>(NumVal);
		}
		P.bHasLinearEndY = Params->TryGetNumberField(MaterialParamEndYKey, NumVal);
		if (P.bHasLinearEndY)
		{
			P.LinearEnd.Y = static_cast<float>(NumVal);
		}
		if (Params->TryGetNumberField(MaterialParamCenterXKey, NumVal))
		{
			P.RadialCenter.X = static_cast<float>(NumVal);
		}
		if (Params->TryGetNumberField(MaterialParamCenterYKey, NumVal))
		{
			P.RadialCenter.Y = static_cast<float>(NumVal);
		}
		if (Params->TryGetNumberField(MaterialParamSizeWidthKey, NumVal))
		{
			P.RadialSize.X = FMath::Max(KINDA_SMALL_NUMBER, static_cast<float>(NumVal));
		}
		if (Params->TryGetNumberField(MaterialParamSizeHeightKey, NumVal))
		{
			P.RadialSize.Y = FMath::Max(KINDA_SMALL_NUMBER, static_cast<float>(NumVal));
		}
		if (Params->TryGetNumberField(MaterialParamBlurPaddingKey, NumVal))
		{
			P.BoxShadowBlurPadding = FMath::Max(0.0f, static_cast<float>(NumVal));
		}
		if (Params->TryGetNumberField(MaterialParamThicknessKey, NumVal))
		{
			P.Thickness = NormalizePercentageOrFraction(NumVal);
		}
		if (Params->TryGetNumberField(MaterialParamCornerRadiusKey, NumVal))
		{
			P.CornerRadius = NormalizePercentageOrFraction(NumVal);
		}

		const TArray<TSharedPtr<FJsonValue>>* RadiiArr = nullptr;
		if (Params->TryGetArrayField(MaterialParamCornerRadiiKey, RadiiArr) && RadiiArr && RadiiArr->Num() == 4)
		{
			for (int32 Index = 0; Index < 4; ++Index)
			{
				P.CornerRadii[Index] = NormalizePercentageOrFraction((*RadiiArr)[Index]->AsNumber());
			}
		}

		if (Params->TryGetNumberField(MaterialParamRotationKey, NumVal))
		{
			P.RotationDeg = static_cast<float>(NumVal);
		}
		if (Params->TryGetStringField(MaterialParamStripeColorKey, ColorStr))
		{
			P.StripeColor = FUMGValueParser::ParseColor(ColorStr);
		}
		if (Params->TryGetNumberField(MaterialParamStripeWidthKey, NumVal))
		{
			P.StripeWidthPx = FMath::Max(1, FMath::RoundToInt(static_cast<float>(NumVal)));
		}
		if (Params->TryGetNumberField(MaterialParamStripeGapKey, NumVal))
		{
			P.StripeGapPx = FMath::Max(1, FMath::RoundToInt(static_cast<float>(NumVal)));
		}
		if (Params->TryGetNumberField(MaterialParamInsetKey, NumVal))
		{
			P.InsetPx = FMath::Max(0, FMath::RoundToInt(static_cast<float>(NumVal)));
		}

		const TArray<TSharedPtr<FJsonValue>>* StopsArr = nullptr;
		if (Params->TryGetArrayField(MaterialParamStopsKey, StopsArr) && StopsArr)
		{
			for (const TSharedPtr<FJsonValue>& StopValue : *StopsArr)
			{
				const TSharedPtr<FJsonObject> StopObject = StopValue->AsObject();
				if (!StopObject.IsValid())
				{
					continue;
				}

				FGeneratedGradientStop Stop;
				double Position = 0.0;
				StopObject->TryGetNumberField(MaterialParamStopPosKey, Position);
				Stop.Position = static_cast<float>(Position);

				FString StopColor;
				if (StopObject->TryGetStringField(MaterialParamColorKey, StopColor))
				{
					Stop.Color = FUMGValueParser::ParseColor(StopColor);
				}

				P.MultiStops.Add(Stop);
			}

			P.MultiStops.Sort([](const FGeneratedGradientStop& Left, const FGeneratedGradientStop& Right)
			{
				return Left.Position < Right.Position;
			});
		}
	}

	// Dispatches by MaterialRef shape name. Takes a mutable params ref: the gradient
	// branches may fill fallback two-stop gradients when the payload declared none.
	UTexture2D* DispatchMaterialRefShape(
		const FUMGProceduralResourceRequest& Request,
		FMaterialRefParams& P,
		const int32 Width,
		const int32 Height,
		UObject* Outer,
		bool& bRecognized)
	{
		bRecognized = true;
		const FString Shape = Request.MaterialRef.ToLower();
		if (Shape == TEXT("horizontalstroke"))
		{
			UTexture2D* Texture = ProceduralShapeGeneratorAssets::CreateTransientTexture(Width, Height, Outer);
			if (!Texture) return nullptr;
			Texture->Filter = TF_Nearest;
			TArray<FColor> Pixels;
			Pixels.SetNumUninitialized(Width * Height);
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					const bool Edge = Y < Height / 3 || Y >= Height - Height / 3;
					Pixels[Y * Width + X] = Edge ? FColor::White : FColor::Transparent;
				}
			}
			ProceduralShapeGeneratorAssets::FillTextureData(Texture, Pixels);
			return Texture;
		}

		if (Shape == TEXT("circle") || Shape == TEXT("ellipse"))
		{
			return ProceduralShapeGeneratorAssets::GenerateCircle(FMath::Max(Width, Height), P.Color, Outer);
		}
		if (Shape == TEXT("ring"))
		{
			return ProceduralShapeGeneratorAssets::GenerateRing(FMath::Max(Width, Height), P.Color, P.Thickness, Outer);
		}
		if (Shape == TEXT("roundedrect") || Shape == TEXT("polygon"))
		{
			if (P.CornerRadii[0] >= 0.0f)
			{
				return ProceduralShapeGeneratorAssets::GenerateRoundedRect(
					Width,
					Height,
					P.Color,
					P.CornerRadii[0],
					P.CornerRadii[1],
					P.CornerRadii[2],
					P.CornerRadii[3],
					Outer);
			}

			return ProceduralShapeGeneratorAssets::GenerateRoundedRect(Width, Height, P.Color, P.CornerRadius, Outer);
		}
		if (Shape == TEXT("gradientlinear"))
		{
			if (P.MultiStops.Num() < 2)
			{
				P.MultiStops.Empty();
				P.MultiStops.Add({0.0f, P.ColorStart});
				P.MultiStops.Add({1.0f, P.ColorEnd});
			}
			if (P.bHasLinearStartX && P.bHasLinearStartY && P.bHasLinearEndX && P.bHasLinearEndY)
			{
				return ProceduralShapeGeneratorAssets::GenerateGradientLinear(
					Width,
					Height,
					P.LinearStart,
					P.LinearEnd,
					P.MultiStops,
					Outer);
			}
			return ProceduralShapeGeneratorAssets::GenerateGradientAngled(Width, Height, P.RotationDeg, P.MultiStops, Outer);
		}
		if (Shape == TEXT("gradienth"))
		{
			return P.MultiStops.Num() >= 2
				? ProceduralShapeGeneratorAssets::GenerateGradientHMultiStop(Width, Height, P.MultiStops, Outer)
				: ProceduralShapeGeneratorAssets::GenerateGradientH(Width, Height, P.ColorStart, P.ColorEnd, Outer);
		}
		if (Shape == TEXT("gradientv"))
		{
			return P.MultiStops.Num() >= 2
				? ProceduralShapeGeneratorAssets::GenerateGradientVMultiStop(Width, Height, P.MultiStops, Outer)
				: ProceduralShapeGeneratorAssets::GenerateGradientV(Width, Height, P.ColorStart, P.ColorEnd, Outer);
		}
		if (Shape == TEXT("radialgradient"))
		{
			return P.MultiStops.Num() >= 2
				? ProceduralShapeGeneratorAssets::GenerateRadialGradientMultiStop(
					Width,
					Height,
					P.MultiStops,
					P.RadialCenter,
					P.RadialSize,
					P.RotationDeg,
					Outer)
				: ProceduralShapeGeneratorAssets::GenerateRadialGradient(
					Width,
					Height,
					P.ColorStart,
					P.ColorEnd,
					P.RadialCenter,
					P.RadialSize,
					P.RotationDeg,
					Outer);
		}
		if (Shape == TEXT("angulargradient"))
		{
			if (P.MultiStops.Num() < 2)
			{
				P.MultiStops.Empty();
				P.MultiStops.Add({0.0f, P.ColorStart});
				P.MultiStops.Add({1.0f, P.ColorEnd});
			}
			return ProceduralShapeGeneratorAssets::GenerateAngularGradient(Width, Height, P.RotationDeg, P.MultiStops, Outer);
		}
		if (Shape == TEXT("boxshadow") || Shape == TEXT("ellipseshadow"))
		{
			const float BlurPx = Request.ShadowBlur.Get(0.0f);
			if (P.CornerRadii[0] >= 0.0f)
			{
				return ProceduralShapeGeneratorAssets::GenerateBoxShadow(
					Width,
					Height,
					P.Color,
					P.CornerRadii[0],
					P.CornerRadii[1],
					P.CornerRadii[2],
					P.CornerRadii[3],
					BlurPx,
					P.BoxShadowBlurPadding,
					Outer,
					Shape == TEXT("ellipseshadow"));
			}

			return ProceduralShapeGeneratorAssets::GenerateBoxShadow(
				Width,
				Height,
				P.Color,
				P.CornerRadius,
				BlurPx,
				P.BoxShadowBlurPadding,
				Outer,
				Shape == TEXT("ellipseshadow"));
		}
		if (Shape == TEXT("placeholderhatch"))
		{
			return ProceduralShapeGeneratorAssets::GeneratePlaceholderHatch(
				Width,
				Height,
				P.Color,
				P.StripeColor,
				P.StripeWidthPx,
				P.StripeGapPx,
				Outer);
		}
		if (Shape == TEXT("placeholdersurface"))
		{
			return ProceduralShapeGeneratorAssets::GeneratePlaceholderSurface(
				Width,
				Height,
				P.Color,
				P.StripeColor,
				P.InsetPx,
				P.StripeWidthPx,
				Outer);
		}

		bRecognized = false;
		return nullptr;
	}
}

UTexture2D* FProceduralShapeGenerator::GenerateFromMaterialRef(
	const FUMGProceduralResourceRequest& Request, UObject* Outer, FString* OutError)
{
	if (OutError) OutError->Reset();
	const DesignImageResources::FImageResourceLimits Limits;
	FIntPoint Size;
	if (!Limits.TryGetPixelSize(Request.WidthOverride.Get(64.0f), Request.HeightOverride.Get(64.0f), Size))
	{
		const FString Error = TEXT("Invalid or excessive procedural texture dimensions");
		if (OutError) *OutError = Error;
		else UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Error);
		return nullptr;
	}
	FMaterialRefParams P;
	if (Request.Gradient.HasAny()) ApplyGradientParams(Request.Gradient, P);
	else if (Request.Shape.IsSet()) ApplyShapeParams(Request.Shape.GetValue(), P);
	else ParseMaterialRefParams(Request, P);

	bool bRecognized = false;
	UTexture2D* Texture = DispatchMaterialRefShape(Request, P, Size.X, Size.Y, Outer, bRecognized);
	if (!Texture && bRecognized)
	{
		const FString Error = FString::Printf(TEXT("Failed to generate procedural texture '%s'"), *Request.MaterialRef);
		if (OutError) *OutError = Error;
		else UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Error);
	}
	return Texture;
}
