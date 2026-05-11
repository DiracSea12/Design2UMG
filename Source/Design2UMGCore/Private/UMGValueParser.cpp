// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGValueParser.h"

namespace
{
	const TMap<FString, EHorizontalAlignment>& GetHAlignMap()
	{
		static const TMap<FString, EHorizontalAlignment> Map = {
			{TEXT("Left"), HAlign_Left}, {TEXT("Center"), HAlign_Center},
			{TEXT("Right"), HAlign_Right}, {TEXT("Fill"), HAlign_Fill}
		};
		return Map;
	}

	const TMap<FString, EVerticalAlignment>& GetVAlignMap()
	{
		static const TMap<FString, EVerticalAlignment> Map = {
			{TEXT("Top"), VAlign_Top}, {TEXT("Center"), VAlign_Center},
			{TEXT("Bottom"), VAlign_Bottom}, {TEXT("Fill"), VAlign_Fill}
		};
		return Map;
	}

	const TMap<FString, ESlateVisibility>& GetVisibilityMap()
	{
		static const TMap<FString, ESlateVisibility> Map = {
			{TEXT("Visible"), ESlateVisibility::Visible},
			{TEXT("Hidden"), ESlateVisibility::Hidden},
			{TEXT("Collapsed"), ESlateVisibility::Collapsed},
			{TEXT("HitTestInvisible"), ESlateVisibility::HitTestInvisible},
			{TEXT("SelfHitTestInvisible"), ESlateVisibility::SelfHitTestInvisible}
		};
		return Map;
	}

	const TMap<FString, FLinearColor>& GetNamedColors()
	{
		static const TMap<FString, FLinearColor> Map = {
			{TEXT("white"), FLinearColor::White},
			{TEXT("black"), FLinearColor::Black},
			{TEXT("red"), FLinearColor::Red},
			{TEXT("green"), FLinearColor::Green},
			{TEXT("blue"), FLinearColor::Blue},
			{TEXT("yellow"), FLinearColor::Yellow},
			{TEXT("gray"), FLinearColor::Gray},
			{TEXT("grey"), FLinearColor::Gray},
			{TEXT("transparent"), FLinearColor::Transparent}
		};
		return Map;
	}
}

namespace FUMGValueParser
{
	FLinearColor ParseColor(const FString& ColorStr)
	{
		FString Trimmed = ColorStr.TrimStartAndEnd();

		if (Trimmed.StartsWith(TEXT("#")))
		{
			FString Hex = Trimmed.Mid(1);
			if (Hex.Len() == 3)
			{
				Hex = FString::Printf(TEXT("%c%c%c%c%c%c"),
					Hex[0], Hex[0], Hex[1], Hex[1], Hex[2], Hex[2]);
				return FColor::FromHex(Hex);
			}
			return FColor::FromHex(Trimmed);
		}

		if (const FLinearColor* Named = GetNamedColors().Find(Trimmed.ToLower()))
		{
			return *Named;
		}

		return FLinearColor::White;
	}

	EHorizontalAlignment ParseHAlign(const FString& Str)
	{
		const EHorizontalAlignment* Found = GetHAlignMap().Find(Str);
		return Found ? *Found : HAlign_Fill;
	}

	EVerticalAlignment ParseVAlign(const FString& Str)
	{
		const EVerticalAlignment* Found = GetVAlignMap().Find(Str);
		return Found ? *Found : VAlign_Fill;
	}

	ESlateVisibility ParseVisibility(const FString& Str)
	{
		const ESlateVisibility* Found = GetVisibilityMap().Find(Str);
		return Found ? *Found : ESlateVisibility::Visible;
	}

	FVector2D ParseVector2D(const FString& Str, FVector2D Default)
	{
		TArray<FString> Parts;
		Str.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 2)
		{
			return FVector2D(
				FCString::Atof(*Parts[0].TrimStartAndEnd()),
				FCString::Atof(*Parts[1].TrimStartAndEnd()));
		}
		return Default;
	}

	FMargin ParseMargin(const FString& Str)
	{
		TArray<FString> Parts;
		Str.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 1)
		{
			return FMargin(FCString::Atof(*Parts[0]));
		}
		if (Parts.Num() == 2)
		{
			const float H = FCString::Atof(*Parts[0].TrimStartAndEnd());
			const float V = FCString::Atof(*Parts[1].TrimStartAndEnd());
			return FMargin(H, V, H, V);
		}
		if (Parts.Num() == 4)
		{
			return FMargin(
				FCString::Atof(*Parts[0].TrimStartAndEnd()),
				FCString::Atof(*Parts[1].TrimStartAndEnd()),
				FCString::Atof(*Parts[2].TrimStartAndEnd()),
				FCString::Atof(*Parts[3].TrimStartAndEnd()));
		}
		return FMargin(0);
	}

	bool IsNearlyEqualMargin(const FMargin& A, const FMargin& B)
	{
		return FMath::IsNearlyEqual(A.Left, B.Left)
			&& FMath::IsNearlyEqual(A.Top, B.Top)
			&& FMath::IsNearlyEqual(A.Right, B.Right)
			&& FMath::IsNearlyEqual(A.Bottom, B.Bottom);
	}
}
