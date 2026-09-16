// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UObject;
class UTexture2D;

namespace ProceduralShapeGeneratorAssets
{
	struct FGradientStop
	{
		float Position = 0.0f;
		FLinearColor Color = FLinearColor::Transparent;
	};

	void ReleaseTransientTextures();
	void FlushPendingTextureUpdates();

	UTexture2D* CreateTransientTexture(int32 Width, int32 Height, UObject* Outer);
	void FillTextureData(UTexture2D* Texture, const TArray<FColor>& PixelData);

	UTexture2D* GenerateCircle(int32 Size, FLinearColor Color, UObject* Outer);
	UTexture2D* GenerateRing(int32 Size, FLinearColor Color, float Thickness, UObject* Outer);
	UTexture2D* GenerateRoundedRect(int32 Width, int32 Height, FLinearColor Color, float CornerRadius, UObject* Outer);
	UTexture2D* GenerateRoundedRect(
		int32 Width,
		int32 Height,
		FLinearColor Color,
		float RadiusTL,
		float RadiusTR,
		float RadiusBR,
		float RadiusBL,
		UObject* Outer);

	UTexture2D* GenerateGradientH(int32 Width, int32 Height, FLinearColor ColorStart, FLinearColor ColorEnd, UObject* Outer);
	UTexture2D* GenerateGradientHMultiStop(int32 Width, int32 Height, const TArray<FGradientStop>& Stops, UObject* Outer);
	UTexture2D* GenerateGradientV(int32 Width, int32 Height, FLinearColor ColorStart, FLinearColor ColorEnd, UObject* Outer);
	UTexture2D* GenerateGradientVMultiStop(int32 Width, int32 Height, const TArray<FGradientStop>& Stops, UObject* Outer);
	UTexture2D* GenerateGradientLinear(int32 Width, int32 Height, const FVector2D& StartNormalized, const FVector2D& EndNormalized, const TArray<FGradientStop>& Stops, UObject* Outer);
	UTexture2D* GenerateGradientAngled(int32 Width, int32 Height, float RotationDeg, const TArray<FGradientStop>& Stops, UObject* Outer);
	UTexture2D* GenerateAngularGradient(int32 Width, int32 Height, float StartAngleDeg, const TArray<FGradientStop>& Stops, UObject* Outer);
	UTexture2D* GenerateRadialGradient(
		int32 Width,
		int32 Height,
		FLinearColor ColorCenter,
		FLinearColor ColorEdge,
		const FVector2D& CenterNormalized,
		const FVector2D& SizeScale,
		float RotationDeg,
		UObject* Outer);
	UTexture2D* GenerateRadialGradientMultiStop(
		int32 Width,
		int32 Height,
		const TArray<FGradientStop>& Stops,
		const FVector2D& CenterNormalized,
		const FVector2D& SizeScale,
		float RotationDeg,
		UObject* Outer);

	UTexture2D* GenerateBoxShadow(
		int32 Width,
		int32 Height,
		FLinearColor ShadowColor,
		float CornerRadius,
		float BlurRadius,
		float BlurPadding,
		UObject* Outer,
		bool bEllipse = false);
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
		UObject* Outer,
		bool bEllipse = false);

	UTexture2D* GeneratePlaceholderHatch(
		int32 Width,
		int32 Height,
		FLinearColor BackgroundColor,
		FLinearColor StripeColor,
		int32 StripeWidthPx,
		int32 StripeGapPx,
		UObject* Outer);
	UTexture2D* GeneratePlaceholderSurface(
		int32 Width,
		int32 Height,
		FLinearColor FillColor,
		FLinearColor OutlineColor,
		int32 InsetPx,
		int32 StripeWidthPx,
		UObject* Outer);
}
