// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "ProceduralShapeGenerator.h"
#include "UMGWidgetUtils.h"
#include "Design2UMGLog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UTexture2D* FProceduralShapeGenerator::CreateTransientTexture(int32 Width, int32 Height, UObject* Outer)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None);
	if (Texture)
	{
		// 不要 Rename 到 widget 下面！CreateBlueprint 自动编译时深拷贝 WidgetTree，
		// RF_Transient 纹理的 PlatformData/Mips 在拷贝中丢失。
		// RF_DuplicateTransient 使 FDuplicateDataWriter 写 nullptr 而非复制，避免空 mip 副本。
		Texture->AddToRoot();
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

void FProceduralShapeGenerator::FillTextureData(UTexture2D* Texture, const TArray<FColor>& PixelData)
{
	if (!Texture || PixelData.Num() == 0) return;

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, PixelData.GetData(), PixelData.Num() * sizeof(FColor));
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	FlushRenderingCommands();
}

UTexture2D* FProceduralShapeGenerator::GenerateCircle(int32 Size, FLinearColor Color, UObject* Outer)
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
			float Alpha = FMath::Clamp(Radius - Dist + 0.5f, 0.0f, 1.0f); // 抗锯齿边缘

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

UTexture2D* FProceduralShapeGenerator::GenerateRing(int32 Size, FLinearColor Color, float Thickness, UObject* Outer)
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

			// 外边缘抗锯齿
			float OuterAlpha = FMath::Clamp(OuterRadius - Dist + 0.5f, 0.0f, 1.0f);
			// 内边缘抗锯齿
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

UTexture2D* FProceduralShapeGenerator::GenerateRoundedRect(int32 Width, int32 Height, FLinearColor Color, float CornerRadius, UObject* Outer)
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

			// 检查是否在圆角区域
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

UTexture2D* FProceduralShapeGenerator::GenerateGradientH(int32 Width, int32 Height, FLinearColor ColorStart, FLinearColor ColorEnd, UObject* Outer)
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

UTexture2D* FProceduralShapeGenerator::GenerateGradientV(int32 Width, int32 Height, FLinearColor ColorStart, FLinearColor ColorEnd, UObject* Outer)
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

UTexture2D* FProceduralShapeGenerator::GenerateRadialGradient(int32 Size, FLinearColor ColorCenter, FLinearColor ColorEdge, UObject* Outer)
{
	UTexture2D* Texture = CreateTransientTexture(Size, Size, Outer);
	if (!Texture) return nullptr;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Size * Size);

	const float Center = (Size - 1) / 2.0f;
	const float MaxDist = Center;

	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			float Dist = FMath::Sqrt(FMath::Square(X - Center) + FMath::Square(Y - Center));
			float T = (MaxDist > 0.0f) ? FMath::Clamp(Dist / MaxDist, 0.0f, 1.0f) : 0.0f;
			FLinearColor Blended = FMath::Lerp(ColorCenter, ColorEdge, T);
			Pixels[Y * Size + X] = Blended.ToFColor(true);
		}
	}

	FillTextureData(Texture, Pixels);
	return Texture;
}

UTexture2D* FProceduralShapeGenerator::GenerateFromMaterialRef(const FString& MaterialRef, const TMap<FString, FString>& Properties, UObject* Outer)
{
	FLinearColor Color = FLinearColor::White;
	FLinearColor ColorStart = FLinearColor::White;
	FLinearColor ColorEnd = FLinearColor::Black;
	float Thickness = 0.1f;  // 与 .h 默认参数一致
	float CornerRadius = 0.2f;

	if (const FString* ParamsStr = Properties.Find(TEXT("MaterialParams")))
	{
		TSharedPtr<FJsonObject> Params;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*ParamsStr);
		if (FJsonSerializer::Deserialize(Reader, Params) && Params.IsValid())
		{
			FString ColorStr;
			if (Params->TryGetStringField(TEXT("Color"), ColorStr))
				Color = FUMGWidgetUtils::ParseColor(ColorStr);
			if (Params->TryGetStringField(TEXT("ColorStart"), ColorStr) ||
				Params->TryGetStringField(TEXT("ColorCenter"), ColorStr))
				ColorStart = FUMGWidgetUtils::ParseColor(ColorStr);
			if (Params->TryGetStringField(TEXT("ColorEnd"), ColorStr) ||
				Params->TryGetStringField(TEXT("ColorEdge"), ColorStr))
				ColorEnd = FUMGWidgetUtils::ParseColor(ColorStr);

			double NumVal;
			// 归一化：>1.0 视为百分比值（如 10 → 0.10），<=1.0 视为已归一化的 0~1 比例
			if (Params->TryGetNumberField(TEXT("Thickness"), NumVal))
				Thickness = NumVal > 1.0 ? static_cast<float>(NumVal) / 100.0f : static_cast<float>(NumVal);
			if (Params->TryGetNumberField(TEXT("CornerRadius"), NumVal))
				CornerRadius = NumVal > 1.0 ? static_cast<float>(NumVal) / 100.0f : static_cast<float>(NumVal);
		}
	}

	const FString* WStr = Properties.Find(TEXT("WidthOverride"));
	const FString* HStr = Properties.Find(TEXT("HeightOverride"));
	const int32 Width = FMath::Max(1, WStr ? FMath::RoundToInt(FCString::Atof(**WStr)) : 64);
	const int32 Height = FMath::Max(1, HStr ? FMath::RoundToInt(FCString::Atof(**HStr)) : 64);

	const FString Shape = MaterialRef.ToLower();
	if (Shape == TEXT("circle") || Shape == TEXT("ellipse"))
		return GenerateCircle(FMath::Max(Width, Height), Color, Outer);
	if (Shape == TEXT("ring"))
		return GenerateRing(FMath::Max(Width, Height), Color, Thickness, Outer);
	if (Shape == TEXT("roundedrect") || Shape == TEXT("polygon"))
		return GenerateRoundedRect(Width, Height, Color, CornerRadius, Outer);
	if (Shape == TEXT("gradienth"))
		return GenerateGradientH(Width, Height, ColorStart, ColorEnd, Outer);
	if (Shape == TEXT("gradientv"))
		return GenerateGradientV(Width, Height, ColorStart, ColorEnd, Outer);
	if (Shape == TEXT("radialgradient"))
		return GenerateRadialGradient(FMath::Max(Width, Height), ColorStart, ColorEnd, Outer);

	return nullptr;
}
