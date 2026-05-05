// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "ProceduralShapeGenerator.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetUtils.h"
#include "UMGValueParser.h"
#include "Design2UMGLog.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "RenderingThread.h"
#include "TextureResource.h"

struct FGradientStop
{
	float Position = 0.0f;
	FLinearColor Color = FLinearColor::Transparent;
};

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
	UObject* Outer);

namespace
{
	TArray<TWeakObjectPtr<UTexture2D>> GRootedTextures;
	bool GbHasPendingTextureUpdates = false;
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
	FlushPendingTextureUpdates();

	for (const TWeakObjectPtr<UTexture2D>& Weak : GRootedTextures)
	{
		if (UTexture2D* Tex = Weak.Get())
			Tex->RemoveFromRoot();
	}
	GRootedTextures.Empty();
	GbHasPendingTextureUpdates = false;
}

UTexture2D* CreateTransientTexture(int32 Width, int32 Height, UObject* Outer)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None);
	if (Texture)
	{
		// Do not rename under the widget. CreateBlueprint deep-copies WidgetTree during automatic compilation,
		// and RF_Transient texture PlatformData/Mips are lost during that copy.
		// RF_DuplicateTransient makes FDuplicateDataWriter write nullptr instead of copying, avoiding empty mip duplicates.
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
	if (!Texture || PixelData.Num() == 0) return;

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, PixelData.GetData(), PixelData.Num() * sizeof(FColor));
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	GbHasPendingTextureUpdates = true;
}

void FProceduralShapeGenerator::FlushPendingTextureUpdates()
{
	if (!GbHasPendingTextureUpdates)
		return;

	if (!GUsingNullRHI)
		FlushRenderingCommands();
	GbHasPendingTextureUpdates = false;
}

UTexture2D* GenerateCircle(int32 Size, FLinearColor Color, UObject* Outer)
{
	UTexture2D* Texture = CreateTransientTexture(Size, Size, Outer);
	if (!Texture) return nullptr;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Size * Size);

	const float Center = (Size - 1) / 2.0f;
	const float Radius = Center;
	const FColor FillColor = Color.ToFColor(true);

	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			float Dist = FMath::Sqrt(FMath::Square(X - Center) + FMath::Square(Y - Center));
			float Alpha = FMath::Clamp(Radius - Dist + 0.5f, 0.0f, 1.0f); // Anti-aliased edge.

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
	if (!Texture) return nullptr;

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
			float Dist = FMath::Sqrt(FMath::Square(X - Center) + FMath::Square(Y - Center));

			// Outer edge anti-aliasing.
			float OuterAlpha = FMath::Clamp(OuterRadius - Dist + 0.5f, 0.0f, 1.0f);
			// Inner edge anti-aliasing.
			float InnerAlpha = FMath::Clamp(Dist - InnerRadius + 0.5f, 0.0f, 1.0f);

			float Alpha = OuterAlpha * InnerAlpha;

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
	if (!Texture) return nullptr;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);

	const float Radius = FMath::Min(Width, Height) * CornerRadius * 0.5f;
	const FColor FillColor = Color.ToFColor(true);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			float Alpha = 1.0f;

			// Check whether the point is inside a rounded corner region.
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
				float Dist = FMath::Sqrt(FMath::Square(X - CornerCenter.X) + FMath::Square(Y - CornerCenter.Y));
				Alpha = FMath::Clamp(Radius - Dist + 0.5f, 0.0f, 1.0f);
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

UTexture2D* GenerateRoundedRect(int32 Width, int32 Height, FLinearColor Color, float RadiusTL, float RadiusTR, float RadiusBR, float RadiusBL, UObject* Outer)
{
	if (FMath::IsNearlyEqual(RadiusTL, RadiusTR) && FMath::IsNearlyEqual(RadiusTR, RadiusBR) && FMath::IsNearlyEqual(RadiusBR, RadiusBL))
		return GenerateRoundedRect(Width, Height, Color, RadiusTL, Outer);

	UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
	if (!Texture) return nullptr;

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
			float R = 0.0f;
			FVector2D Center = FVector2D::ZeroVector;
			bool bInCorner = false;

			if (X < RTL && Y < RTL)
			{
				R = RTL; Center = FVector2D(RTL, RTL); bInCorner = true;
			}
			else if (X >= Width - RTR && Y < RTR)
			{
				R = RTR; Center = FVector2D(Width - RTR - 1, RTR); bInCorner = true;
			}
			else if (X >= Width - RBR && Y >= Height - RBR)
			{
				R = RBR; Center = FVector2D(Width - RBR - 1, Height - RBR - 1); bInCorner = true;
			}
			else if (X < RBL && Y >= Height - RBL)
			{
				R = RBL; Center = FVector2D(RBL, Height - RBL - 1); bInCorner = true;
			}

			if (bInCorner)
			{
				float Dist = FMath::Sqrt(FMath::Square(X - Center.X) + FMath::Square(Y - Center.Y));
				Alpha = FMath::Clamp(R - Dist + 0.5f, 0.0f, 1.0f);
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

FLinearColor SampleGradientStops(const TArray<FGradientStop>& Stops, float T)
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

	for (int32 i = 1; i < Stops.Num(); ++i)
	{
		if (T <= Stops[i].Position || i == Stops.Num() - 1)
		{
			const float Span = Stops[i].Position - Stops[i - 1].Position;
			float LocalT = (Span > KINDA_SMALL_NUMBER) ? (T - Stops[i - 1].Position) / Span : 0.0f;
			LocalT = FMath::Clamp(LocalT, 0.0f, 1.0f);
			return FMath::Lerp(Stops[i - 1].Color, Stops[i].Color, LocalT);
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

UTexture2D* GenerateGradientH(int32 Width, int32 Height, FLinearColor ColorStart, FLinearColor ColorEnd, UObject* Outer)
{
	UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
	if (!Texture) return nullptr;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			float T = (Width > 1) ? static_cast<float>(X) / (Width - 1) : 0.0f;
			FLinearColor Blended = FMath::Lerp(ColorStart, ColorEnd, T);
			Pixels[Y * Width + X] = Blended.ToFColor(true);
		}
	}

	FillTextureData(Texture, Pixels);
	return Texture;
}

UTexture2D* GenerateGradientHMultiStop(int32 Width, int32 Height, const TArray<FGradientStop>& Stops, UObject* Outer)
{
	if (Stops.Num() < 2)
		return nullptr;

	UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
	if (!Texture) return nullptr;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			float T = (Width > 1) ? static_cast<float>(X) / (Width - 1) : 0.0f;
			Pixels[Y * Width + X] = SampleGradientStops(Stops, T).ToFColor(true);
		}
	}

	FillTextureData(Texture, Pixels);
	return Texture;
}

UTexture2D* GenerateGradientV(int32 Width, int32 Height, FLinearColor ColorStart, FLinearColor ColorEnd, UObject* Outer)
{
	UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
	if (!Texture) return nullptr;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		float T = (Height > 1) ? static_cast<float>(Y) / (Height - 1) : 0.0f;
		FLinearColor Blended = FMath::Lerp(ColorStart, ColorEnd, T);
		FColor RowColor = Blended.ToFColor(true);

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
		return nullptr;

	UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
	if (!Texture) return nullptr;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		float T = (Height > 1) ? static_cast<float>(Y) / (Height - 1) : 0.0f;
		const FColor RowColor = SampleGradientStops(Stops, T).ToFColor(true);
		for (int32 X = 0; X < Width; ++X)
			Pixels[Y * Width + X] = RowColor;
	}

	FillTextureData(Texture, Pixels);
	return Texture;
}

UTexture2D* GenerateGradientAngled(int32 Width, int32 Height, float RotationDeg, const TArray<FGradientStop>& Stops, UObject* Outer)
{
	if (Stops.Num() < 2)
		return nullptr;

	UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
	if (!Texture) return nullptr;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);

	// Gradient direction from rotation angle (degrees, 0° = left-to-right)
	const float Rad = FMath::DegreesToRadians(RotationDeg);
	const float DirX = FMath::Cos(Rad);
	const float DirY = FMath::Sin(Rad);

	// Project corners onto gradient axis to find min/max T for normalization
	const float T00 = 0.0f;
	const float T10 = DirX;
	const float T01 = DirY;
	const float T11 = DirX + DirY;
	const float TMin = FMath::Min(FMath::Min(T00, T10), FMath::Min(T01, T11));
	const float TMax = FMath::Max(FMath::Max(T00, T10), FMath::Max(T01, T11));
	const float TRange = TMax - TMin;

	for (int32 Y = 0; Y < Height; ++Y)
	{
		const float NY = (Height > 1) ? static_cast<float>(Y) / (Height - 1) : 0.0f;
		for (int32 X = 0; X < Width; ++X)
		{
			const float NX = (Width > 1) ? static_cast<float>(X) / (Width - 1) : 0.0f;

			// Project normalized pixel position onto gradient axis
			float T = (NX * DirX + NY * DirY - TMin);
			T = (TRange > KINDA_SMALL_NUMBER) ? FMath::Clamp(T / TRange, 0.0f, 1.0f) : 0.0f;
			Pixels[Y * Width + X] = SampleGradientStops(Stops, T).ToFColor(true);
		}
	}

	FillTextureData(Texture, Pixels);
	return Texture;
}

UTexture2D* GenerateAngularGradient(int32 Width, int32 Height, float StartAngleDeg, const TArray<FGradientStop>& Stops, UObject* Outer)
{
	if (Stops.Num() < 2)
		return nullptr;

	UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
	if (!Texture) return nullptr;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);

	const float CX = (Width - 1) / 2.0f;
	const float CY = (Height - 1) / 2.0f;
	const float StartRad = FMath::DegreesToRadians(StartAngleDeg);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			float Angle = FMath::Atan2(Y - CY, X - CX) - StartRad;
			// Normalize to [0, 1)
			float T = Angle / (2.0f * PI);
			T = T - FMath::FloorToFloat(T); // wrap to [0, 1)
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
	if (!Texture) return nullptr;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const float T = ComputeRadialGradientDistance(X, Y, Width, Height, CenterNormalized, SizeScale, RotationDeg);
			FLinearColor Blended = FMath::Lerp(ColorCenter, ColorEdge, T);
			Pixels[Y * Width + X] = Blended.ToFColor(true);
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
	if (!Texture) return nullptr;

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

// --- Box Shadow with Gaussian Blur ---

// Helper: generate rounded rect alpha mask into float buffer
static void GenerateRoundedRectMask(
	TArray<float>& OutAlpha,
	int32 W,
	int32 H,
	float RTL,
	float RTR,
	float RBR,
	float RBL)
{
	OutAlpha.SetNumUninitialized(W * H);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			float A = 1.0f;
			float R = 0.0f;
			FVector2D Center = FVector2D::Zero();
			bool bCorner = false;

			if (X < RTL && Y < RTL)
			{
				R = RTL;
				Center = FVector2D(RTL, RTL);
				bCorner = true;
			}
			else if (X >= W - RTR && Y < RTR)
			{
				R = RTR;
				Center = FVector2D(W - RTR - 1, RTR);
				bCorner = true;
			}
			else if (X >= W - RBR && Y >= H - RBR)
			{
				R = RBR;
				Center = FVector2D(W - RBR - 1, H - RBR - 1);
				bCorner = true;
			}
			else if (X < RBL && Y >= H - RBL)
			{
				R = RBL;
				Center = FVector2D(RBL, H - RBL - 1);
				bCorner = true;
			}

			if (bCorner)
			{
				const float Dist = FMath::Sqrt(FMath::Square(X - Center.X) + FMath::Square(Y - Center.Y));
				A = FMath::Clamp(R - Dist + 0.5f, 0.0f, 1.0f);
			}
			OutAlpha[Y * W + X] = A;
		}
	}
}

// Helper: separable gaussian blur on float alpha buffer
static void GaussianBlurAlpha(TArray<float>& Alpha, int32 W, int32 H, float Sigma)
{
	if (Sigma < 0.5f) return;

	const int32 KernelRadius = FMath::CeilToInt(Sigma * 3.0f);
	TArray<float> Kernel;
	Kernel.SetNumUninitialized(KernelRadius * 2 + 1);
	float Sum = 0.0f;
	for (int32 i = -KernelRadius; i <= KernelRadius; ++i)
	{
		float V = FMath::Exp(-(i * i) / (2.0f * Sigma * Sigma));
		Kernel[i + KernelRadius] = V;
		Sum += V;
	}
	for (float& V : Kernel) V /= Sum;

	// Horizontal pass
	TArray<float> Temp;
	Temp.SetNumUninitialized(W * H);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			float Acc = 0.0f;
			for (int32 k = -KernelRadius; k <= KernelRadius; ++k)
			{
				int32 SX = FMath::Clamp(X + k, 0, W - 1);
				Acc += Alpha[Y * W + SX] * Kernel[k + KernelRadius];
			}
			Temp[Y * W + X] = Acc;
		}
	}

	// Vertical pass
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			float Acc = 0.0f;
			for (int32 k = -KernelRadius; k <= KernelRadius; ++k)
			{
				int32 SY = FMath::Clamp(Y + k, 0, H - 1);
				Acc += Temp[SY * W + X] * Kernel[k + KernelRadius];
			}
			Alpha[Y * W + X] = Acc;
		}
	}
}

UTexture2D* GenerateBoxShadow(int32 Width, int32 Height, FLinearColor ShadowColor, float CornerRadius, float BlurRadius, float BlurPadding, UObject* Outer)
{
	return GenerateBoxShadow(Width, Height, ShadowColor, CornerRadius, CornerRadius, CornerRadius, CornerRadius, BlurRadius, BlurPadding, Outer);
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
	if (!Texture) return nullptr;

	TArray<float> Alpha;
	const bool bHasExplicitBlurPadding = BlurPadding >= 0.0f;
	if (!bHasExplicitBlurPadding)
	{
		// Keep the historical mask semantics for persisted procedural textures.
		const float HalfMin = FMath::Min(Width, Height) * 0.5f;
		const float PxTL = HalfMin * RTL;
		const float PxTR = HalfMin * RTR;
		const float PxBR = HalfMin * RBR;
		const float PxBL = HalfMin * RBL;
		GenerateRoundedRectMask(Alpha, Width, Height, PxTL, PxTR, PxBR, PxBL);
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
		const float PxTL = ShapeHalfMin * RTL;
		const float PxTR = ShapeHalfMin * RTR;
		const float PxBR = ShapeHalfMin * RBR;
		const float PxBL = ShapeHalfMin * RBL;

		TArray<float> InnerAlpha;
		GenerateRoundedRectMask(InnerAlpha, ShapeWidth, ShapeHeight, PxTL, PxTR, PxBR, PxBL);
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

	// Apply gaussian blur (sigma = BlurRadius / 2)
	GaussianBlurAlpha(Alpha, Width, Height, BlurRadius * 0.5f);

	// Compose final pixels: shadow color * blurred alpha
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	const FColor SC = ShadowColor.ToFColor(true);
	for (int32 i = 0; i < Width * Height; ++i)
	{
		Pixels[i].R = SC.R;
		Pixels[i].G = SC.G;
		Pixels[i].B = SC.B;
		Pixels[i].A = FMath::RoundToInt(SC.A * FMath::Clamp(Alpha[i], 0.0f, 1.0f));
	}

	FillTextureData(Texture, Pixels);
	return Texture;
}

UTexture2D* FProceduralShapeGenerator::ApplyAffineCrop(UTexture2D* Source, float A, float B, float TX, float C, float D, float TY, int32 OutW, int32 OutH)
{
	if (!Source || OutW <= 0 || OutH <= 0) return nullptr;

	const int32 SrcW = Source->GetSizeX();
	const int32 SrcH = Source->GetSizeY();
	if (SrcW <= 0 || SrcH <= 0) return nullptr;

	// Read source pixels
	const FColor* SrcData = nullptr;
	FTexturePlatformData* PlatformData = Source->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0) return nullptr;

	const void* RawData = PlatformData->Mips[0].BulkData.LockReadOnly();
	if (!RawData) return nullptr;
	SrcData = static_cast<const FColor*>(RawData);

	// Invert affine matrix [[a,b,tx],[c,d,ty]]
	const float Det = A * D - B * C;
	if (FMath::IsNearlyZero(Det))
	{
		PlatformData->Mips[0].BulkData.Unlock();
		return nullptr;
	}
	const float InvDet = 1.0f / Det;
	const float IA =  D * InvDet, IB = -B * InvDet;
	const float IC = -C * InvDet, ID =  A * InvDet;
	const float ITX = (B * TY - D * TX) * InvDet;
	const float ITY = (C * TX - A * TY) * InvDet;

	// Resample with bilinear interpolation
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(OutW * OutH);

	for (int32 Y = 0; Y < OutH; ++Y)
	{
		const float NY = static_cast<float>(Y) / FMath::Max(1, OutH - 1);
		for (int32 X = 0; X < OutW; ++X)
		{
			const float NX = static_cast<float>(X) / FMath::Max(1, OutW - 1);

			// Map output normalized coords back to source UV
			const float SU = IA * NX + IB * NY + ITX;
			const float SV = IC * NX + ID * NY + ITY;

			// Source pixel coords (float)
			const float SX = SU * (SrcW - 1);
			const float SY = SV * (SrcH - 1);

			// Bilinear sample
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

			auto Lerp = [](uint8 V0, uint8 V1, float T) -> uint8 {
				return FMath::RoundToInt(V0 + (V1 - V0) * T);
			};
			auto BiLerp = [&](uint8 V00, uint8 V10, uint8 V01, uint8 V11) -> uint8 {
				uint8 Top = Lerp(V00, V10, FX);
				uint8 Bot = Lerp(V01, V11, FX);
				return Lerp(Top, Bot, FY);
			};

			FColor& Out = Pixels[Y * OutW + X];
			Out.R = BiLerp(C00.R, C10.R, C01.R, C11.R);
			Out.G = BiLerp(C00.G, C10.G, C01.G, C11.G);
			Out.B = BiLerp(C00.B, C10.B, C01.B, C11.B);
			Out.A = BiLerp(C00.A, C10.A, C01.A, C11.A);
		}
	}

	PlatformData->Mips[0].BulkData.Unlock();

	UTexture2D* Result = CreateTransientTexture(OutW, OutH, nullptr);
	if (!Result) return nullptr;
	FillTextureData(Result, Pixels);
	return Result;
}

UTexture2D* FProceduralShapeGenerator::GenerateFromMaterialRef(const FString& MaterialRef, const FUMGPropertyBag& Properties, UObject* Outer)
{
	FLinearColor Color = FLinearColor::White;
	FLinearColor ColorStart = FLinearColor::White;
	FLinearColor ColorEnd = FLinearColor::Black;
	float Thickness = 0.1f;
	float CornerRadius = 0.2f;
	float CornerRadii[4] = { -1.f, -1.f, -1.f, -1.f }; // TL, TR, BR, BL; -1 = not set
	float RotationDeg = 0.0f;
	FLinearColor StripeColor = FLinearColor(0.67f, 0.58f, 0.94f, 0.66f);
	int32 StripeWidthPx = 2;
	int32 StripeGapPx = 6;
	int32 InsetPx = 10;
	float BoxShadowBlurPadding = -1.0f;
	TArray<FGradientStop> MultiStops;
	FVector2D RadialCenter(0.5f, 0.5f);
	FVector2D RadialSize(1.0f, 1.0f);

	FString ParamsStr;
	if (Properties.TryGetString(UMGPropertyKeys::MaterialParams, ParamsStr))
	{
		TSharedPtr<FJsonObject> Params;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsStr);
		if (FJsonSerializer::Deserialize(Reader, Params) && Params.IsValid())
		{
			FString ColorStr;
			if (Params->TryGetStringField(MaterialParamColorKey, ColorStr))
				Color = FUMGValueParser::ParseColor(ColorStr);
			if (Params->TryGetStringField(MaterialParamColorStartKey, ColorStr) ||
				Params->TryGetStringField(MaterialParamColorCenterKey, ColorStr))
				ColorStart = FUMGValueParser::ParseColor(ColorStr);
			if (Params->TryGetStringField(MaterialParamColorEndKey, ColorStr) ||
				Params->TryGetStringField(MaterialParamColorEdgeKey, ColorStr))
				ColorEnd = FUMGValueParser::ParseColor(ColorStr);

			double NumVal;
			if (Params->TryGetNumberField(MaterialParamCenterXKey, NumVal))
				RadialCenter.X = static_cast<float>(NumVal);
			if (Params->TryGetNumberField(MaterialParamCenterYKey, NumVal))
				RadialCenter.Y = static_cast<float>(NumVal);
			if (Params->TryGetNumberField(MaterialParamSizeWidthKey, NumVal))
				RadialSize.X = FMath::Max(KINDA_SMALL_NUMBER, static_cast<float>(NumVal));
			if (Params->TryGetNumberField(MaterialParamSizeHeightKey, NumVal))
				RadialSize.Y = FMath::Max(KINDA_SMALL_NUMBER, static_cast<float>(NumVal));
			if (Params->TryGetNumberField(MaterialParamBlurPaddingKey, NumVal))
				BoxShadowBlurPadding = FMath::Max(0.0f, static_cast<float>(NumVal));
			if (Params->TryGetNumberField(MaterialParamThicknessKey, NumVal))
				Thickness = NumVal > 1.0 ? static_cast<float>(NumVal) / 100.0f : static_cast<float>(NumVal);
			if (Params->TryGetNumberField(MaterialParamCornerRadiusKey, NumVal))
				CornerRadius = NumVal > 1.0 ? static_cast<float>(NumVal) / 100.0f : static_cast<float>(NumVal);

			const TArray<TSharedPtr<FJsonValue>>* RadiiArr = nullptr;
			if (Params->TryGetArrayField(MaterialParamCornerRadiiKey, RadiiArr) && RadiiArr && RadiiArr->Num() == 4)
			{
				for (int32 i = 0; i < 4; ++i)
				{
					double V = (*RadiiArr)[i]->AsNumber();
					CornerRadii[i] = V > 1.0 ? static_cast<float>(V) / 100.0f : static_cast<float>(V);
				}
			}

			if (Params->TryGetNumberField(MaterialParamRotationKey, NumVal))
				RotationDeg = static_cast<float>(NumVal);
			if (Params->TryGetStringField(MaterialParamStripeColorKey, ColorStr))
				StripeColor = FUMGValueParser::ParseColor(ColorStr);
			if (Params->TryGetNumberField(MaterialParamStripeWidthKey, NumVal))
				StripeWidthPx = FMath::Max(1, FMath::RoundToInt(static_cast<float>(NumVal)));
			if (Params->TryGetNumberField(MaterialParamStripeGapKey, NumVal))
				StripeGapPx = FMath::Max(1, FMath::RoundToInt(static_cast<float>(NumVal)));
			if (Params->TryGetNumberField(MaterialParamInsetKey, NumVal))
				InsetPx = FMath::Max(0, FMath::RoundToInt(static_cast<float>(NumVal)));

			// Multi-stop gradient Stops array.
			const TArray<TSharedPtr<FJsonValue>>* StopsArr = nullptr;
			if (Params->TryGetArrayField(MaterialParamStopsKey, StopsArr) && StopsArr)
			{
				for (const TSharedPtr<FJsonValue>& StopVal : *StopsArr)
				{
					const TSharedPtr<FJsonObject> StopObj = StopVal->AsObject();
					if (!StopObj.IsValid()) continue;

					FGradientStop Stop;
					double Pos = 0.0;
					StopObj->TryGetNumberField(MaterialParamStopPosKey, Pos);
					Stop.Position = static_cast<float>(Pos);

					FString StopColor;
					if (StopObj->TryGetStringField(MaterialParamColorKey, StopColor))
						Stop.Color = FUMGValueParser::ParseColor(StopColor);

					MultiStops.Add(Stop);
				}

				MultiStops.Sort([](const FGradientStop& A, const FGradientStop& B)
				{
					return A.Position < B.Position;
				});
			}
		}
	}

	float WidthValue = 64.0f;
	float HeightValue = 64.0f;
	Properties.TryGetFloat(UMGPropertyKeys::WidthOverride, WidthValue);
	Properties.TryGetFloat(UMGPropertyKeys::HeightOverride, HeightValue);
	const int32 Width = FMath::Max(1, FMath::RoundToInt(WidthValue));
	const int32 Height = FMath::Max(1, FMath::RoundToInt(HeightValue));

	const FString Shape = MaterialRef.ToLower();
	if (Shape == TEXT("circle") || Shape == TEXT("ellipse"))
		return GenerateCircle(FMath::Max(Width, Height), Color, Outer);
	if (Shape == TEXT("ring"))
		return GenerateRing(FMath::Max(Width, Height), Color, Thickness, Outer);
	if (Shape == TEXT("roundedrect") || Shape == TEXT("polygon"))
	{
		if (CornerRadii[0] >= 0.f)
			return GenerateRoundedRect(Width, Height, Color, CornerRadii[0], CornerRadii[1], CornerRadii[2], CornerRadii[3], Outer);
		return GenerateRoundedRect(Width, Height, Color, CornerRadius, Outer);
	}
	if (Shape == TEXT("gradientlinear"))
	{
		// Build stops from 2-color fallback if no multi-stop array
		if (MultiStops.Num() < 2)
		{
			MultiStops.Empty();
			MultiStops.Add({0.0f, ColorStart});
			MultiStops.Add({1.0f, ColorEnd});
		}
		return GenerateGradientAngled(Width, Height, RotationDeg, MultiStops, Outer);
	}
	if (Shape == TEXT("gradienth"))
	{
		if (MultiStops.Num() >= 2)
			return GenerateGradientHMultiStop(Width, Height, MultiStops, Outer);
		return GenerateGradientH(Width, Height, ColorStart, ColorEnd, Outer);
	}
	if (Shape == TEXT("gradientv"))
	{
		if (MultiStops.Num() >= 2)
			return GenerateGradientVMultiStop(Width, Height, MultiStops, Outer);
		return GenerateGradientV(Width, Height, ColorStart, ColorEnd, Outer);
	}
	if (Shape == TEXT("radialgradient"))
	{
		if (MultiStops.Num() >= 2)
			return GenerateRadialGradientMultiStop(Width, Height, MultiStops, RadialCenter, RadialSize, RotationDeg, Outer);
		return GenerateRadialGradient(Width, Height, ColorStart, ColorEnd, RadialCenter, RadialSize, RotationDeg, Outer);
	}
	if (Shape == TEXT("angulargradient"))
	{
		if (MultiStops.Num() < 2)
		{
			MultiStops.Empty();
			MultiStops.Add({0.0f, ColorStart});
			MultiStops.Add({1.0f, ColorEnd});
		}
		return GenerateAngularGradient(Width, Height, RotationDeg, MultiStops, Outer);
	}
	if (Shape == TEXT("boxshadow"))
	{
		float BlurPx = 0.0f;
		Properties.TryGetFloat(UMGPropertyKeys::ShadowBlur, BlurPx);
		const float BlurPadding = BoxShadowBlurPadding;

		if (CornerRadii[0] >= 0.f)
			return GenerateBoxShadow(Width, Height, Color, CornerRadii[0], CornerRadii[1], CornerRadii[2], CornerRadii[3], BlurPx, BlurPadding, Outer);
		return GenerateBoxShadow(Width, Height, Color, CornerRadius, BlurPx, BlurPadding, Outer);
	}
	if (Shape == TEXT("placeholderhatch"))
	{
		UTexture2D* Texture = CreateTransientTexture(Width, Height, Outer);
		if (!Texture)
			return nullptr;

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		const FColor BgColor = Color.ToFColor(true);
		const FColor LineColor = StripeColor.ToFColor(true);
		const int32 Period = FMath::Max(1, StripeWidthPx + StripeGapPx);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const bool bOnStripe = ((X + Y) % Period) < StripeWidthPx;
				Pixels[Y * Width + X] = bOnStripe ? LineColor : BgColor;
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}
	if (Shape == TEXT("placeholdersurface"))
	{
		constexpr int32 PlaceholderSupersample = 2;
		const int32 RasterWidth = FMath::Max(1, Width * PlaceholderSupersample);
		const int32 RasterHeight = FMath::Max(1, Height * PlaceholderSupersample);
		const int32 RasterInsetPx = InsetPx * PlaceholderSupersample;
		const int32 RasterStripeWidthPx = FMath::Max(1, StripeWidthPx * PlaceholderSupersample);
		UE_LOG(LogDesign2UMG, Verbose,
			TEXT("ProceduralShapeGenerator: placeholdersurface size=%dx%d raster=%dx%d color=(%.3f,%.3f,%.3f,%.3f) outline=(%.3f,%.3f,%.3f,%.3f) inset=%d outlineWidth=%d"),
			Width, Height, RasterWidth, RasterHeight,
			Color.R, Color.G, Color.B, Color.A,
			StripeColor.R, StripeColor.G, StripeColor.B, StripeColor.A,
			InsetPx, StripeWidthPx);
		UTexture2D* Texture = CreateTransientTexture(RasterWidth, RasterHeight, Outer);
		if (!Texture)
			return nullptr;

		TArray<FColor> Pixels;
		Pixels.Init(FColor(0, 0, 0, 0), RasterWidth * RasterHeight);

		const FColor FillColor = Color.ToFColor(true);
		const FColor OutlineColor = StripeColor.ToFColor(true);
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
				const bool bOutline =
					(X - MinX) < OutlineWidth
					|| (MaxX - X) < OutlineWidth
					|| (Y - MinY) < OutlineWidth
					|| (MaxY - Y) < OutlineWidth;
				Pixels[Y * RasterWidth + X] = bOutline ? OutlineColor : FillColor;
			}
		}

		FillTextureData(Texture, Pixels);
		return Texture;
	}

	return nullptr;
}

