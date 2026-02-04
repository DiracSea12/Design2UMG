// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenTextureGenerator.h"
#include "Engine/Texture2D.h"
#include "PenTypeMapper.h"

UTexture2D* FPenTextureGenerator::CreateTexture(int32 Width, int32 Height)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (Texture)
	{
		Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
	}
	return Texture;
}

void FPenTextureGenerator::FillPixels(UTexture2D* Texture, TFunction<FColor(int32, int32)> PixelFunc)
{
	if (!Texture)
		return;

	int32 Width = Texture->GetSizeX();
	int32 Height = Texture->GetSizeY();

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FColor* Pixels = static_cast<FColor*>(Data);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
			Pixels[Y * Width + X] = PixelFunc(X, Y);
	}

	Mip.BulkData.Unlock();
	Texture->UpdateResource();
}

UTexture2D* FPenTextureGenerator::GenerateEllipseTexture(int32 Width, int32 Height, float InnerRadius, float StartAngle, float SweepAngle, const FLinearColor& Color)
{
	UTexture2D* Texture = CreateTexture(Width, Height);
	if (!Texture)
		return nullptr;

	float CenterX = Width * 0.5f;
	float CenterY = Height * 0.5f;
	float OuterRadius = FMath::Min(CenterX, CenterY);
	float Inner = OuterRadius * InnerRadius;

	FillPixels(Texture, [&](int32 X, int32 Y) -> FColor {
		float DX = X - CenterX;
		float DY = Y - CenterY;
		float Dist = FMath::Sqrt(DX * DX + DY * DY);

		if (Dist > OuterRadius || Dist < Inner)
			return FColor::Transparent;

		float Angle = FMath::Atan2(DY, DX) * 180.f / PI;
		if (Angle < 0) Angle += 360.f;

		if (SweepAngle < 360.f)
		{
			float End = StartAngle + SweepAngle;
			if (Angle < StartAngle || Angle > End)
				return FColor::Transparent;
		}

		return Color.ToFColor(true);
	});

	return Texture;
}

UTexture2D* FPenTextureGenerator::GenerateLineTexture(int32 Width, int32 Height, float Thickness, const FLinearColor& Color)
{
	UTexture2D* Texture = CreateTexture(Width, Height);
	if (!Texture)
		return nullptr;

	float HalfThick = Thickness * 0.5f;
	float CenterY = Height * 0.5f;

	FillPixels(Texture, [&](int32 X, int32 Y) -> FColor {
		if (FMath::Abs(Y - CenterY) <= HalfThick)
			return Color.ToFColor(true);
		return FColor::Transparent;
	});

	return Texture;
}

UTexture2D* FPenTextureGenerator::GeneratePolygonTexture(int32 Width, int32 Height, int32 PolygonCount, const FLinearColor& Color)
{
	UTexture2D* Texture = CreateTexture(Width, Height);
	if (!Texture)
		return nullptr;

	float CenterX = Width * 0.5f;
	float CenterY = Height * 0.5f;
	float Radius = FMath::Min(CenterX, CenterY);

	FillPixels(Texture, [&](int32 X, int32 Y) -> FColor {
		float DX = X - CenterX;
		float DY = Y - CenterY;
		float Angle = FMath::Atan2(DY, DX);
		float Dist = FMath::Sqrt(DX * DX + DY * DY);

		// 计算多边形边界
		float SegAngle = 2.f * PI / PolygonCount;
		float LocalAngle = FMath::Fmod(Angle + PI, SegAngle) - SegAngle * 0.5f;
		float MaxDist = Radius * FMath::Cos(SegAngle * 0.5f) / FMath::Cos(LocalAngle);

		if (Dist <= MaxDist)
			return Color.ToFColor(true);
		return FColor::Transparent;
	});

	return Texture;
}

UTexture2D* FPenTextureGenerator::GenerateGradientTexture(int32 Width, int32 Height, const FPenGradient& Gradient)
{
	UTexture2D* Texture = CreateTexture(Width, Height);
	if (!Texture || Gradient.Colors.Num() < 2)
		return Texture;

	FillPixels(Texture, [&](int32 X, int32 Y) -> FColor {
		float T = 0.f;

		switch (Gradient.GradientType)
		{
		case EPenGradientType::Linear:
			T = (float)X / Width;
			break;
		case EPenGradientType::Radial:
			{
				float DX = X - Width * 0.5f;
				float DY = Y - Height * 0.5f;
				T = FMath::Sqrt(DX * DX + DY * DY) / (FMath::Min(Width, Height) * 0.5f);
				T = FMath::Clamp(T, 0.f, 1.f);
			}
			break;
		case EPenGradientType::Angular:
			{
				float DX = X - Width * 0.5f;
				float DY = Y - Height * 0.5f;
				T = (FMath::Atan2(DY, DX) + PI) / (2.f * PI);
			}
			break;
		}

		// 插值颜色
		for (int32 i = 0; i < Gradient.Colors.Num() - 1; ++i)
		{
			if (T <= Gradient.Colors[i + 1].Position)
			{
				float LocalT = (T - Gradient.Colors[i].Position) /
					(Gradient.Colors[i + 1].Position - Gradient.Colors[i].Position);
				FLinearColor C1 = FPenTypeMapper::ParseColor(Gradient.Colors[i].Color);
				FLinearColor C2 = FPenTypeMapper::ParseColor(Gradient.Colors[i + 1].Color);
				return FLinearColor::LerpUsingHSV(C1, C2, LocalT).ToFColor(true);
			}
		}

		return FPenTypeMapper::ParseColor(Gradient.Colors.Last().Color).ToFColor(true);
	});

	return Texture;
}
