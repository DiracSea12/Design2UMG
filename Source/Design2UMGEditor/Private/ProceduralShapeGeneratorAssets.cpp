// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "ProceduralShapeGeneratorAssets.h"

#include "Design2UMGLog.h"
#include "Engine/Texture2D.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace
{
	TArray<TWeakObjectPtr<UTexture2D>> GRootedTextures;
	bool GbHasPendingTextureUpdates = false;

	FLinearColor SampleGradientStops(const TArray<ProceduralShapeGeneratorAssets::FGradientStop>& Stops, float T)
	{
		if (Stops.Num() == 0)
		{
			return FLinearColor::Transparent;
		}

		if (Stops.Num() == 1)
		{
			return Stops[0].Color;
		}

		if (T <= Stops[0].Position)
		{
			return Stops[0].Color;
		}

		for (int32 Index = 1; Index < Stops.Num(); ++Index)
		{
			if (T <= Stops[Index].Position || Index == Stops.Num() - 1)
			{
				const float Span = Stops[Index].Position - Stops[Index - 1].Position;
				float LocalT = Span > KINDA_SMALL_NUMBER ? (T - Stops[Index - 1].Position) / Span : 0.0f;
				LocalT = FMath::Clamp(LocalT, 0.0f, 1.0f);
				return FMath::Lerp(Stops[Index - 1].Color, Stops[Index].Color, LocalT);
			}
		}

		return Stops.Last().Color;
	}

	float ComputeRadialGradientDistance(
		int32 X,
		int32 Y,
		int32 Width,
		int32 Height,
		const FVector2D& CenterNormalized,
		const FVector2D& SizeScale,
		float RotationDeg)
	{
		const float CenterX = FMath::Clamp(CenterNormalized.X, 0.0f, 1.0f) * (Width - 1);
		const float CenterY = FMath::Clamp(CenterNormalized.Y, 0.0f, 1.0f) * (Height - 1);
		const float RadiusX = FMath::Max(0.5f, ((Width - 1) * 0.5f) * FMath::Max(SizeScale.X, KINDA_SMALL_NUMBER));
		const float RadiusY = FMath::Max(0.5f, ((Height - 1) * 0.5f) * FMath::Max(SizeScale.Y, KINDA_SMALL_NUMBER));

		float LocalX = X - CenterX;
		float LocalY = Y - CenterY;
		if (!FMath::IsNearlyZero(RotationDeg))
		{
			const float RotationRad = FMath::DegreesToRadians(RotationDeg);
			const float CosTheta = FMath::Cos(-RotationRad);
			const float SinTheta = FMath::Sin(-RotationRad);
			const float RotatedX = LocalX * CosTheta - LocalY * SinTheta;
			const float RotatedY = LocalX * SinTheta + LocalY * CosTheta;
			LocalX = RotatedX;
			LocalY = RotatedY;
		}

		const float NX = LocalX / RadiusX;
		const float NY = LocalY / RadiusY;
		return FMath::Clamp(FMath::Sqrt(NX * NX + NY * NY), 0.0f, 1.0f);
	}

	void GenerateRoundedRectMask(
		TArray<float>& OutAlpha,
		int32 Width,
		int32 Height,
		float RTL,
		float RTR,
		float RBR,
		float RBL)
	{
		OutAlpha.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				float Alpha = 1.0f;
				float Radius = 0.0f;
				FVector2D Center = FVector2D::ZeroVector;
				bool bCorner = false;

				if (X < RTL && Y < RTL)
				{
					Radius = RTL;
					Center = FVector2D(RTL, RTL);
					bCorner = true;
				}
				else if (X >= Width - RTR && Y < RTR)
				{
					Radius = RTR;
					Center = FVector2D(Width - RTR - 1, RTR);
					bCorner = true;
				}
				else if (X >= Width - RBR && Y >= Height - RBR)
				{
					Radius = RBR;
					Center = FVector2D(Width - RBR - 1, Height - RBR - 1);
					bCorner = true;
				}
				else if (X < RBL && Y >= Height - RBL)
				{
					Radius = RBL;
					Center = FVector2D(RBL, Height - RBL - 1);
					bCorner = true;
				}

				if (bCorner)
				{
					const float Distance = FMath::Sqrt(FMath::Square(X - Center.X) + FMath::Square(Y - Center.Y));
					Alpha = FMath::Clamp(Radius - Distance + 0.5f, 0.0f, 1.0f);
				}

				OutAlpha[Y * Width + X] = Alpha;
			}
		}
	}

	void GaussianBlurAlpha(TArray<float>& Alpha, int32 Width, int32 Height, float Sigma)
	{
		if (Sigma < 0.5f)
		{
			return;
		}

		const int32 KernelRadius = FMath::CeilToInt(Sigma * 3.0f);
		TArray<float> Kernel;
		Kernel.SetNumUninitialized(KernelRadius * 2 + 1);
		float Sum = 0.0f;
		for (int32 Index = -KernelRadius; Index <= KernelRadius; ++Index)
		{
			const float Value = FMath::Exp(-(Index * Index) / (2.0f * Sigma * Sigma));
			Kernel[Index + KernelRadius] = Value;
			Sum += Value;
		}

		for (float& Value : Kernel)
		{
			Value /= Sum;
		}

		TArray<float> Temp;
		Temp.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				float Accumulator = 0.0f;
				for (int32 KernelIndex = -KernelRadius; KernelIndex <= KernelRadius; ++KernelIndex)
				{
					const int32 SampleX = FMath::Clamp(X + KernelIndex, 0, Width - 1);
					Accumulator += Alpha[Y * Width + SampleX] * Kernel[KernelIndex + KernelRadius];
				}
				Temp[Y * Width + X] = Accumulator;
			}
		}

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				float Accumulator = 0.0f;
				for (int32 KernelIndex = -KernelRadius; KernelIndex <= KernelRadius; ++KernelIndex)
				{
					const int32 SampleY = FMath::Clamp(Y + KernelIndex, 0, Height - 1);
					Accumulator += Temp[SampleY * Width + X] * Kernel[KernelIndex + KernelRadius];
				}
				Alpha[Y * Width + X] = Accumulator;
			}
		}
	}
}

namespace ProceduralShapeGeneratorAssets
{
	void ReleaseTransientTextures()
	{
		FlushPendingTextureUpdates();

		for (const TWeakObjectPtr<UTexture2D>& WeakTexture : GRootedTextures)
		{
			if (UTexture2D* Texture = WeakTexture.Get())
			{
				Texture->RemoveFromRoot();
			}
		}

		GRootedTextures.Empty();
		GbHasPendingTextureUpdates = false;
	}

	void FlushPendingTextureUpdates()
	{
		if (!GbHasPendingTextureUpdates)
		{
			return;
		}

		if (!GUsingNullRHI)
		{
			FlushRenderingCommands();
		}

		GbHasPendingTextureUpdates = false;
	}

	UTexture2D* CreateTransientTexture(int32 Width, int32 Height, UObject* Outer)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None);
		if (Texture)
		{
			Texture->AddToRoot();
			GRootedTextures.Add(Texture);
			Texture->SetFlags(RF_DuplicateTransient);
			Texture->SRGB = true;
			Texture->CompressionSettings = TC_EditorIcon;
			Texture->MipGenSettings = TMGS_NoMipmaps;
			Texture->LODGroup = TEXTUREGROUP_UI;
			Texture->Filter = TF_Bilinear;
			Texture->NeverStream = true;
		}

		return Texture;
	}

	void FillTextureData(UTexture2D* Texture, const TArray<FColor>& PixelData)
	{
		if (!Texture || PixelData.Num() == 0)
		{
			return;
		}

		void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(TextureData, PixelData.GetData(), PixelData.Num() * sizeof(FColor));
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
		GbHasPendingTextureUpdates = true;
	}

	UTexture2D* GenerateCircle(int32 Size, FLinearColor Color, UObject* Outer)
	{
		UTexture2D* Texture = CreateTransientTexture(Size, Size, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Size * Size);

		const float Center = (Size - 1) / 2.0f;
		const float Radius = Center;
		const FColor FillColor = Color.ToFColor(true);

		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const float Distance = FMath::Sqrt(FMath::Square(X - Center) + FMath::Square(Y - Center));
				const float Alpha = FMath::Clamp(Radius - Distance + 0.5f, 0.0f, 1.0f);

				FColor& Pixel = Pixels[Y * Size + X];
				Pixel.R = FillColor.R;
				Pixel.G = FillColor.G;
				Pixel.B = FillColor.B;
				Pixel.A = FMath::RoundToInt(FillColor.A * Alpha);
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateRing(int32 Size, FLinearColor Color, float Thickness, UObject* Outer)
	{
		UTexture2D* Texture = CreateTransientTexture(Size, Size, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Size * Size);

		const float Center = (Size - 1) / 2.0f;
		const float OuterRadius = Center;
		const float InnerRadius = Center * (1.0f - Thickness * 2.0f);
		const FColor FillColor = Color.ToFColor(true);

		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const float Distance = FMath::Sqrt(FMath::Square(X - Center) + FMath::Square(Y - Center));
				const float OuterAlpha = FMath::Clamp(OuterRadius - Distance + 0.5f, 0.0f, 1.0f);
				const float InnerAlpha = FMath::Clamp(Distance - InnerRadius + 0.5f, 0.0f, 1.0f);
				const float Alpha = OuterAlpha * InnerAlpha;

				FColor& Pixel = Pixels[Y * Size + X];
				Pixel.R = FillColor.R;
				Pixel.G = FillColor.G;
				Pixel.B = FillColor.B;
				Pixel.A = FMath::RoundToInt(FillColor.A * Alpha);
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateRoundedRect(int32 Width, int32 Height, FLinearColor Color, float CornerRadius, UObject* Outer)
	{
		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		const float Radius = FMath::Min(Width, Height) * CornerRadius * 0.5f;
		const FColor FillColor = Color.ToFColor(true);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				float Alpha = 1.0f;
				FVector2D CornerCenter = FVector2D::ZeroVector;
				bool bInCorner = false;

				if (X < Radius && Y < Radius)
				{
					CornerCenter = FVector2D(Radius, Radius);
					bInCorner = true;
				}
				else if (X >= Width - Radius && Y < Radius)
				{
					CornerCenter = FVector2D(Width - Radius - 1, Radius);
					bInCorner = true;
				}
				else if (X < Radius && Y >= Height - Radius)
				{
					CornerCenter = FVector2D(Radius, Height - Radius - 1);
					bInCorner = true;
				}
				else if (X >= Width - Radius && Y >= Height - Radius)
				{
					CornerCenter = FVector2D(Width - Radius - 1, Height - Radius - 1);
					bInCorner = true;
				}

				if (bInCorner)
				{
					const float Distance = FMath::Sqrt(FMath::Square(X - CornerCenter.X) + FMath::Square(Y - CornerCenter.Y));
					Alpha = FMath::Clamp(Radius - Distance + 0.5f, 0.0f, 1.0f);
				}

				FColor& Pixel = Pixels[Y * Width + X];
				Pixel.R = FillColor.R;
				Pixel.G = FillColor.G;
				Pixel.B = FillColor.B;
				Pixel.A = FMath::RoundToInt(FillColor.A * Alpha);
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateRoundedRect(
		int32 Width,
		int32 Height,
		FLinearColor Color,
		float RadiusTL,
		float RadiusTR,
		float RadiusBR,
		float RadiusBL,
		UObject* Outer)
	{
		if (FMath::IsNearlyEqual(RadiusTL, RadiusTR)
			&& FMath::IsNearlyEqual(RadiusTR, RadiusBR)
			&& FMath::IsNearlyEqual(RadiusBR, RadiusBL))
		{
			return GenerateRoundedRect(Width, Height, Color, RadiusTL, Outer);
		}

		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		const float HalfMin = FMath::Min(Width, Height) * 0.5f;
		const float RTL = HalfMin * RadiusTL;
		const float RTR = HalfMin * RadiusTR;
		const float RBR = HalfMin * RadiusBR;
		const float RBL = HalfMin * RadiusBL;
		const FColor FillColor = Color.ToFColor(true);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				float Alpha = 1.0f;
				float Radius = 0.0f;
				FVector2D Center = FVector2D::ZeroVector;
				bool bInCorner = false;

				if (X < RTL && Y < RTL)
				{
					Radius = RTL;
					Center = FVector2D(RTL, RTL);
					bInCorner = true;
				}
				else if (X >= Width - RTR && Y < RTR)
				{
					Radius = RTR;
					Center = FVector2D(Width - RTR - 1, RTR);
					bInCorner = true;
				}
				else if (X >= Width - RBR && Y >= Height - RBR)
				{
					Radius = RBR;
					Center = FVector2D(Width - RBR - 1, Height - RBR - 1);
					bInCorner = true;
				}
				else if (X < RBL && Y >= Height - RBL)
				{
					Radius = RBL;
					Center = FVector2D(RBL, Height - RBL - 1);
					bInCorner = true;
				}

				if (bInCorner)
				{
					const float Distance = FMath::Sqrt(FMath::Square(X - Center.X) + FMath::Square(Y - Center.Y));
					Alpha = FMath::Clamp(Radius - Distance + 0.5f, 0.0f, 1.0f);
				}

				FColor& Pixel = Pixels[Y * Width + X];
				Pixel.R = FillColor.R;
				Pixel.G = FillColor.G;
				Pixel.B = FillColor.B;
				Pixel.A = FMath::RoundToInt(FillColor.A * Alpha);
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateGradientH(int32 Width, int32 Height, FLinearColor ColorStart, FLinearColor ColorEnd, UObject* Outer)
	{
		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float T = Width > 1 ? static_cast<float>(X) / (Width - 1) : 0.0f;
				Pixels[Y * Width + X] = FMath::Lerp(ColorStart, ColorEnd, T).ToFColor(true);
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateGradientHMultiStop(int32 Width, int32 Height, const TArray<FGradientStop>& Stops, UObject* Outer)
	{
		if (Stops.Num() < 2)
		{
			return nullptr;
		}

		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float T = Width > 1 ? static_cast<float>(X) / (Width - 1) : 0.0f;
				Pixels[Y * Width + X] = SampleGradientStops(Stops, T).ToFColor(true);
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateGradientV(int32 Width, int32 Height, FLinearColor ColorStart, FLinearColor ColorEnd, UObject* Outer)
	{
		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			const float T = Height > 1 ? static_cast<float>(Y) / (Height - 1) : 0.0f;
			const FColor RowColor = FMath::Lerp(ColorStart, ColorEnd, T).ToFColor(true);
			for (int32 X = 0; X < Width; ++X)
			{
				Pixels[Y * Width + X] = RowColor;
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateGradientVMultiStop(int32 Width, int32 Height, const TArray<FGradientStop>& Stops, UObject* Outer)
	{
		if (Stops.Num() < 2)
		{
			return nullptr;
		}

		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			const float T = Height > 1 ? static_cast<float>(Y) / (Height - 1) : 0.0f;
			const FColor RowColor = SampleGradientStops(Stops, T).ToFColor(true);
			for (int32 X = 0; X < Width; ++X)
			{
				Pixels[Y * Width + X] = RowColor;
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateGradientAngled(int32 Width, int32 Height, float RotationDeg, const TArray<FGradientStop>& Stops, UObject* Outer)
	{
		if (Stops.Num() < 2)
		{
			return nullptr;
		}

		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		const float RotationRad = FMath::DegreesToRadians(RotationDeg);
		const float DirX = FMath::Cos(RotationRad);
		const float DirY = FMath::Sin(RotationRad);
		const float T00 = 0.0f;
		const float T10 = DirX;
		const float T01 = DirY;
		const float T11 = DirX + DirY;
		const float TMin = FMath::Min(FMath::Min(T00, T10), FMath::Min(T01, T11));
		const float TMax = FMath::Max(FMath::Max(T00, T10), FMath::Max(T01, T11));
		const float TRange = TMax - TMin;

		for (int32 Y = 0; Y < Height; ++Y)
		{
			const float NY = Height > 1 ? static_cast<float>(Y) / (Height - 1) : 0.0f;
			for (int32 X = 0; X < Width; ++X)
			{
				const float NX = Width > 1 ? static_cast<float>(X) / (Width - 1) : 0.0f;
				float T = NX * DirX + NY * DirY - TMin;
				T = TRange > KINDA_SMALL_NUMBER ? FMath::Clamp(T / TRange, 0.0f, 1.0f) : 0.0f;
				Pixels[Y * Width + X] = SampleGradientStops(Stops, T).ToFColor(true);
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateAngularGradient(int32 Width, int32 Height, float StartAngleDeg, const TArray<FGradientStop>& Stops, UObject* Outer)
	{
		if (Stops.Num() < 2)
		{
			return nullptr;
		}

		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		const float CenterX = (Width - 1) / 2.0f;
		const float CenterY = (Height - 1) / 2.0f;
		const float StartRad = FMath::DegreesToRadians(StartAngleDeg);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float Angle = FMath::Atan2(Y - CenterY, X - CenterX) - StartRad;
				float T = Angle / (2.0f * PI);
				T -= FMath::FloorToFloat(T);
				Pixels[Y * Width + X] = SampleGradientStops(Stops, T).ToFColor(true);
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateRadialGradient(
		int32 Width,
		int32 Height,
		FLinearColor ColorCenter,
		FLinearColor ColorEdge,
		const FVector2D& CenterNormalized,
		const FVector2D& SizeScale,
		float RotationDeg,
		UObject* Outer)
	{
		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float T = ComputeRadialGradientDistance(X, Y, Width, Height, CenterNormalized, SizeScale, RotationDeg);
				Pixels[Y * Width + X] = FMath::Lerp(ColorCenter, ColorEdge, T).ToFColor(true);
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateRadialGradientMultiStop(
		int32 Width,
		int32 Height,
		const TArray<FGradientStop>& Stops,
		const FVector2D& CenterNormalized,
		const FVector2D& SizeScale,
		float RotationDeg,
		UObject* Outer)
	{
		if (Stops.Num() < 2)
		{
			return nullptr;
		}

		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float T = ComputeRadialGradientDistance(X, Y, Width, Height, CenterNormalized, SizeScale, RotationDeg);
				Pixels[Y * Width + X] = SampleGradientStops(Stops, T).ToFColor(true);
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GenerateBoxShadow(
		int32 Width,
		int32 Height,
		FLinearColor ShadowColor,
		float CornerRadius,
		float BlurRadius,
		float BlurPadding,
		UObject* Outer)
	{
		return GenerateBoxShadow(
			Width,
			Height,
			ShadowColor,
			CornerRadius,
			CornerRadius,
			CornerRadius,
			CornerRadius,
			BlurRadius,
			BlurPadding,
			Outer);
	}

	UTexture2D* GenerateBoxShadow(
		int32 Width,
		int32 Height,
		FLinearColor ShadowColor,
		float RTL,
		float RTR,
		float RBR,
		float RBL,
		float BlurRadius,
		float BlurPadding,
		UObject* Outer)
	{
		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<float> Alpha;
		const bool bHasExplicitBlurPadding = BlurPadding >= 0.0f;
		if (!bHasExplicitBlurPadding)
		{
			const float HalfMin = FMath::Min(Width, Height) * 0.5f;
			GenerateRoundedRectMask(Alpha, Width, Height, HalfMin * RTL, HalfMin * RTR, HalfMin * RBR, HalfMin * RBL);
		}
		else
		{
			Alpha.Init(0.0f, Width * Height);

			const int32 PaddingPx = FMath::Clamp(
				FMath::RoundToInt(FMath::Max(0.0f, BlurPadding)),
				0,
				FMath::Max(0, FMath::Min(Width, Height) / 2 - 1));
			const int32 ShapeWidth = FMath::Max(1, Width - PaddingPx * 2);
			const int32 ShapeHeight = FMath::Max(1, Height - PaddingPx * 2);
			const float ShapeHalfMin = FMath::Min(ShapeWidth, ShapeHeight) * 0.5f;

			TArray<float> InnerAlpha;
			GenerateRoundedRectMask(
				InnerAlpha,
				ShapeWidth,
				ShapeHeight,
				ShapeHalfMin * RTL,
				ShapeHalfMin * RTR,
				ShapeHalfMin * RBR,
				ShapeHalfMin * RBL);

			for (int32 Y = 0; Y < ShapeHeight; ++Y)
			{
				const int32 DestY = PaddingPx + Y;
				if (DestY < 0 || DestY >= Height)
				{
					continue;
				}

				for (int32 X = 0; X < ShapeWidth; ++X)
				{
					const int32 DestX = PaddingPx + X;
					if (DestX < 0 || DestX >= Width)
					{
						continue;
					}

					Alpha[DestY * Width + DestX] = InnerAlpha[Y * ShapeWidth + X];
				}
			}
		}

		GaussianBlurAlpha(Alpha, Width, Height, BlurRadius * 0.5f);

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		const FColor Shadow = ShadowColor.ToFColor(true);
		for (int32 Index = 0; Index < Width * Height; ++Index)
		{
			Pixels[Index].R = Shadow.R;
			Pixels[Index].G = Shadow.G;
			Pixels[Index].B = Shadow.B;
			Pixels[Index].A = FMath::RoundToInt(Shadow.A * FMath::Clamp(Alpha[Index], 0.0f, 1.0f));
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GeneratePlaceholderHatch(
		int32 Width,
		int32 Height,
		FLinearColor BackgroundColor,
		FLinearColor StripeColor,
		int32 StripeWidthPx,
		int32 StripeGapPx,
		UObject* Outer)
	{
		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		const FColor FillColor = BackgroundColor.ToFColor(true);
		const FColor LineColor = StripeColor.ToFColor(true);
		const int32 Period = FMath::Max(1, StripeWidthPx + StripeGapPx);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const bool bOnStripe = ((X + Y) % Period) < StripeWidthPx;
				Pixels[Y * Width + X] = bOnStripe ? LineColor : FillColor;
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	UTexture2D* GeneratePlaceholderSurface(
		int32 Width,
		int32 Height,
		FLinearColor FillColor,
		FLinearColor OutlineColor,
		int32 InsetPx,
		int32 StripeWidthPx,
		UObject* Outer)
	{
		constexpr int32 PlaceholderSupersample = 2;
		const int32 RasterWidth = FMath::Max(1, Width * PlaceholderSupersample);
		const int32 RasterHeight = FMath::Max(1, Height * PlaceholderSupersample);
		const int32 RasterInsetPx = InsetPx * PlaceholderSupersample;
		const int32 RasterStripeWidthPx = FMath::Max(1, StripeWidthPx * PlaceholderSupersample);

		UE_LOG(
			LogDesign2UMG,
			Verbose,
			TEXT("ProceduralShapeGenerator: placeholdersurface size=%dx%d raster=%dx%d color=(%.3f,%.3f,%.3f,%.3f) outline=(%.3f,%.3f,%.3f,%.3f) inset=%d outlineWidth=%d"),
			Width,
			Height,
			RasterWidth,
			RasterHeight,
			FillColor.R,
			FillColor.G,
			FillColor.B,
			FillColor.A,
			OutlineColor.R,
			OutlineColor.G,
			OutlineColor.B,
			OutlineColor.A,
			InsetPx,
			StripeWidthPx);

		UTexture2D* Texture = CreateTransientTexture(RasterWidth, RasterHeight, Outer);
		if (!Texture)
		{
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.Init(FColor(0, 0, 0, 0), RasterWidth * RasterHeight);

		const FColor FillPixel = FillColor.ToFColor(true);
		const FColor OutlinePixel = OutlineColor.ToFColor(true);
		const int32 OutlineWidth = FMath::Max(1, RasterStripeWidthPx);
		const int32 EffectiveInset = FMath::Clamp(RasterInsetPx, 0, FMath::Max(0, FMath::Min(RasterWidth, RasterHeight) / 2 - 1));
		const int32 MinX = EffectiveInset;
		const int32 MinY = EffectiveInset;
		const int32 MaxX = RasterWidth - EffectiveInset - 1;
		const int32 MaxY = RasterHeight - EffectiveInset - 1;

		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const bool bOutline = (X - MinX) < OutlineWidth
					|| (MaxX - X) < OutlineWidth
					|| (Y - MinY) < OutlineWidth
					|| (MaxY - Y) < OutlineWidth;
				Pixels[Y * RasterWidth + X] = bOutline ? OutlinePixel : FillPixel;
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}
}
