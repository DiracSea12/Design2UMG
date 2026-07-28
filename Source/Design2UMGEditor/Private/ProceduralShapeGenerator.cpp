// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "ProceduralShapeGenerator.h"

#include "ProceduralShapeGeneratorAssets.h"
#include "UMGValueParsing.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using FGeneratedGradientStop = ProceduralShapeGeneratorAssets::FGradientStop;

	constexpr TCHAR MaterialParamColorKey[] = TEXT("Color");
	constexpr TCHAR MaterialParamColorStartKey[] = TEXT("ColorStart");
	constexpr TCHAR MaterialParamColorCenterKey[] = TEXT("ColorCenter");
	constexpr TCHAR MaterialParamColorEndKey[] = TEXT("ColorEnd");
	constexpr TCHAR MaterialParamColorEdgeKey[] = TEXT("ColorEdge");
	constexpr TCHAR MaterialParamCornerRadiiKey[] = TEXT("CornerRadii");
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
	int32 OutH)
{
	if (!Source || OutW <= 0 || OutH <= 0)
	{
		return nullptr;
	}

	const int32 SrcW = Source->GetSizeX();
	const int32 SrcH = Source->GetSizeY();
	if (SrcW <= 0 || SrcH <= 0)
	{
		return nullptr;
	}

	FTexturePlatformData* PlatformData = Source->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0)
	{
		return nullptr;
	}

	const void* RawData = PlatformData->Mips[0].BulkData.LockReadOnly();
	if (!RawData)
	{
		return nullptr;
	}

	const FColor* SrcData = static_cast<const FColor*>(RawData);
	const float Determinant = A * D - B * C;
	if (FMath::IsNearlyZero(Determinant))
	{
		PlatformData->Mips[0].BulkData.Unlock();
		return nullptr;
	}

	const float InvDeterminant = 1.0f / Determinant;
	const float IA = D * InvDeterminant;
	const float IB = -B * InvDeterminant;
	const float IC = -C * InvDeterminant;
	const float ID = A * InvDeterminant;
	const float ITX = (B * TY - D * TX) * InvDeterminant;
	const float ITY = (C * TX - A * TY) * InvDeterminant;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(OutW * OutH);

	for (int32 Y = 0; Y < OutH; ++Y)
	{
		const float NY = static_cast<float>(Y) / FMath::Max(1, OutH - 1);
		for (int32 X = 0; X < OutW; ++X)
		{
			const float NX = static_cast<float>(X) / FMath::Max(1, OutW - 1);
			const float SU = IA * NX + IB * NY + ITX;
			const float SV = IC * NX + ID * NY + ITY;
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

	PlatformData->Mips[0].BulkData.Unlock();

	UTexture2D* Result = ProceduralShapeGeneratorAssets::CreateTransientTexture(OutW, OutH, nullptr);
	if (!Result)
	{
		return nullptr;
	}

	ProceduralShapeGeneratorAssets::FillTextureData(Result, Pixels);
	return Result;
}

UTexture2D* FProceduralShapeGenerator::GenerateFromMaterialRef(const FUMGProceduralResourceRequest& Request, UObject* Outer)
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
	FVector2D RadialCenter(0.5f, 0.5f);
	FVector2D RadialSize(1.0f, 1.0f);

	if (Request.MaterialParams.IsSet())
	{
		TSharedPtr<FJsonObject> Params;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Request.MaterialParams.GetValue());
		if (FJsonSerializer::Deserialize(Reader, Params) && Params.IsValid())
		{
			FString ColorStr;
			if (Params->TryGetStringField(MaterialParamColorKey, ColorStr))
			{
				Color = FUMGValueParser::ParseColor(ColorStr);
			}
			if (Params->TryGetStringField(MaterialParamColorStartKey, ColorStr)
				|| Params->TryGetStringField(MaterialParamColorCenterKey, ColorStr))
			{
				ColorStart = FUMGValueParser::ParseColor(ColorStr);
			}
			if (Params->TryGetStringField(MaterialParamColorEndKey, ColorStr)
				|| Params->TryGetStringField(MaterialParamColorEdgeKey, ColorStr))
			{
				ColorEnd = FUMGValueParser::ParseColor(ColorStr);
			}

			double NumVal = 0.0;
			if (Params->TryGetNumberField(MaterialParamCenterXKey, NumVal))
			{
				RadialCenter.X = static_cast<float>(NumVal);
			}
			if (Params->TryGetNumberField(MaterialParamCenterYKey, NumVal))
			{
				RadialCenter.Y = static_cast<float>(NumVal);
			}
			if (Params->TryGetNumberField(MaterialParamSizeWidthKey, NumVal))
			{
				RadialSize.X = FMath::Max(KINDA_SMALL_NUMBER, static_cast<float>(NumVal));
			}
			if (Params->TryGetNumberField(MaterialParamSizeHeightKey, NumVal))
			{
				RadialSize.Y = FMath::Max(KINDA_SMALL_NUMBER, static_cast<float>(NumVal));
			}
			if (Params->TryGetNumberField(MaterialParamBlurPaddingKey, NumVal))
			{
				BoxShadowBlurPadding = FMath::Max(0.0f, static_cast<float>(NumVal));
			}
			if (Params->TryGetNumberField(MaterialParamThicknessKey, NumVal))
			{
				Thickness = NumVal > 1.0 ? static_cast<float>(NumVal) / 100.0f : static_cast<float>(NumVal);
			}
			if (Params->TryGetNumberField(MaterialParamCornerRadiusKey, NumVal))
			{
				CornerRadius = NumVal > 1.0 ? static_cast<float>(NumVal) / 100.0f : static_cast<float>(NumVal);
			}

			const TArray<TSharedPtr<FJsonValue>>* RadiiArr = nullptr;
			if (Params->TryGetArrayField(MaterialParamCornerRadiiKey, RadiiArr) && RadiiArr && RadiiArr->Num() == 4)
			{
				for (int32 Index = 0; Index < 4; ++Index)
				{
					const double RadiusValue = (*RadiiArr)[Index]->AsNumber();
					CornerRadii[Index] = RadiusValue > 1.0
						? static_cast<float>(RadiusValue) / 100.0f
						: static_cast<float>(RadiusValue);
				}
			}

			if (Params->TryGetNumberField(MaterialParamRotationKey, NumVal))
			{
				RotationDeg = static_cast<float>(NumVal);
			}
			if (Params->TryGetStringField(MaterialParamStripeColorKey, ColorStr))
			{
				StripeColor = FUMGValueParser::ParseColor(ColorStr);
			}
			if (Params->TryGetNumberField(MaterialParamStripeWidthKey, NumVal))
			{
				StripeWidthPx = FMath::Max(1, FMath::RoundToInt(static_cast<float>(NumVal)));
			}
			if (Params->TryGetNumberField(MaterialParamStripeGapKey, NumVal))
			{
				StripeGapPx = FMath::Max(1, FMath::RoundToInt(static_cast<float>(NumVal)));
			}
			if (Params->TryGetNumberField(MaterialParamInsetKey, NumVal))
			{
				InsetPx = FMath::Max(0, FMath::RoundToInt(static_cast<float>(NumVal)));
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

					MultiStops.Add(Stop);
				}

				MultiStops.Sort([](const FGeneratedGradientStop& Left, const FGeneratedGradientStop& Right)
				{
					return Left.Position < Right.Position;
				});
			}
		}
	}

	const int32 Width = FMath::Max(1, FMath::RoundToInt(Request.WidthOverride.Get(64.0f)));
	const int32 Height = FMath::Max(1, FMath::RoundToInt(Request.HeightOverride.Get(64.0f)));
	const FString Shape = Request.MaterialRef.ToLower();

	if (Shape == TEXT("circle") || Shape == TEXT("ellipse"))
	{
		return ProceduralShapeGeneratorAssets::GenerateCircle(FMath::Max(Width, Height), Color, Outer);
	}
	if (Shape == TEXT("ring"))
	{
		return ProceduralShapeGeneratorAssets::GenerateRing(FMath::Max(Width, Height), Color, Thickness, Outer);
	}
	if (Shape == TEXT("roundedrect") || Shape == TEXT("polygon"))
	{
		if (CornerRadii[0] >= 0.0f)
		{
			return ProceduralShapeGeneratorAssets::GenerateRoundedRect(
				Width,
				Height,
				Color,
				CornerRadii[0],
				CornerRadii[1],
				CornerRadii[2],
				CornerRadii[3],
				Outer);
		}

		return ProceduralShapeGeneratorAssets::GenerateRoundedRect(Width, Height, Color, CornerRadius, Outer);
	}
	if (Shape == TEXT("gradientlinear"))
	{
		if (MultiStops.Num() < 2)
		{
			MultiStops.Empty();
			MultiStops.Add({0.0f, ColorStart});
			MultiStops.Add({1.0f, ColorEnd});
		}
		return ProceduralShapeGeneratorAssets::GenerateGradientAngled(Width, Height, RotationDeg, MultiStops, Outer);
	}
	if (Shape == TEXT("gradienth"))
	{
		return MultiStops.Num() >= 2
			? ProceduralShapeGeneratorAssets::GenerateGradientHMultiStop(Width, Height, MultiStops, Outer)
			: ProceduralShapeGeneratorAssets::GenerateGradientH(Width, Height, ColorStart, ColorEnd, Outer);
	}
	if (Shape == TEXT("gradientv"))
	{
		return MultiStops.Num() >= 2
			? ProceduralShapeGeneratorAssets::GenerateGradientVMultiStop(Width, Height, MultiStops, Outer)
			: ProceduralShapeGeneratorAssets::GenerateGradientV(Width, Height, ColorStart, ColorEnd, Outer);
	}
	if (Shape == TEXT("radialgradient"))
	{
		return MultiStops.Num() >= 2
			? ProceduralShapeGeneratorAssets::GenerateRadialGradientMultiStop(
				Width,
				Height,
				MultiStops,
				RadialCenter,
				RadialSize,
				RotationDeg,
				Outer)
			: ProceduralShapeGeneratorAssets::GenerateRadialGradient(
				Width,
				Height,
				ColorStart,
				ColorEnd,
				RadialCenter,
				RadialSize,
				RotationDeg,
				Outer);
	}
	if (Shape == TEXT("angulargradient"))
	{
		if (MultiStops.Num() < 2)
		{
			MultiStops.Empty();
			MultiStops.Add({0.0f, ColorStart});
			MultiStops.Add({1.0f, ColorEnd});
		}
		return ProceduralShapeGeneratorAssets::GenerateAngularGradient(Width, Height, RotationDeg, MultiStops, Outer);
	}
	if (Shape == TEXT("boxshadow"))
	{
		const float BlurPx = Request.ShadowBlur.Get(0.0f);
		if (CornerRadii[0] >= 0.0f)
		{
			return ProceduralShapeGeneratorAssets::GenerateBoxShadow(
				Width,
				Height,
				Color,
				CornerRadii[0],
				CornerRadii[1],
				CornerRadii[2],
				CornerRadii[3],
				BlurPx,
				BoxShadowBlurPadding,
				Outer);
		}

		return ProceduralShapeGeneratorAssets::GenerateBoxShadow(
			Width,
			Height,
			Color,
			CornerRadius,
			BlurPx,
			BoxShadowBlurPadding,
			Outer);
	}
	if (Shape == TEXT("placeholderhatch"))
	{
		return ProceduralShapeGeneratorAssets::GeneratePlaceholderHatch(
			Width,
			Height,
			Color,
			StripeColor,
			StripeWidthPx,
			StripeGapPx,
			Outer);
	}
	if (Shape == TEXT("placeholdersurface"))
	{
		return ProceduralShapeGeneratorAssets::GeneratePlaceholderSurface(
			Width,
			Height,
			Color,
			StripeColor,
			InsetPx,
			StripeWidthPx,
			Outer);
	}

	return nullptr;
}
