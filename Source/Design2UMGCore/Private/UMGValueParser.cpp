// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGValueParser.h"
#include "Design2UMGLog.h"

namespace
{
	const TMap<FString, EHorizontalAlignment>& GetHAlignMap()
	{
		static const TMap<FString, EHorizontalAlignment> Map = {
			{TEXT("left"), HAlign_Left}, {TEXT("center"), HAlign_Center},
			{TEXT("right"), HAlign_Right}, {TEXT("fill"), HAlign_Fill}
		};
		return Map;
	}

	const TMap<FString, EVerticalAlignment>& GetVAlignMap()
	{
		static const TMap<FString, EVerticalAlignment> Map = {
			{TEXT("top"), VAlign_Top}, {TEXT("center"), VAlign_Center},
			{TEXT("bottom"), VAlign_Bottom}, {TEXT("fill"), VAlign_Fill}
		};
		return Map;
	}

	FString NormalizeSlotToken(const FString& Str)
	{
		return Str.TrimStartAndEnd().ToLower();
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

	UE_LOG(LogDesign2UMG, Warning, TEXT("Unknown color '%s'; defaulting to White."), *ColorStr);
	return FLinearColor::White;
}

	EHorizontalAlignment ParseHAlign(const FString& Str)
	{
		return ToSlateHorizontalAlignment(ParseSlotHorizontalAlignment(Str));
	}

	EVerticalAlignment ParseVAlign(const FString& Str)
	{
		return ToSlateVerticalAlignment(ParseSlotVerticalAlignment(Str));
	}

	EUMGSlotHorizontalAlignment ParseSlotHorizontalAlignment(const FString& Str)
	{
		const FString Normalized = NormalizeSlotToken(Str);
		const EHorizontalAlignment* Found = GetHAlignMap().Find(Normalized);
		if (!Found)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Unknown slot horizontal alignment '%s'; defaulting to Fill."), *Str);
			return EUMGSlotHorizontalAlignment::Fill;
		}

		switch (*Found)
		{
		case HAlign_Left:
			return EUMGSlotHorizontalAlignment::Left;
		case HAlign_Center:
			return EUMGSlotHorizontalAlignment::Center;
		case HAlign_Right:
			return EUMGSlotHorizontalAlignment::Right;
		case HAlign_Fill:
		default:
			return EUMGSlotHorizontalAlignment::Fill;
		}
	}

	EUMGSlotVerticalAlignment ParseSlotVerticalAlignment(const FString& Str)
	{
		const FString Normalized = NormalizeSlotToken(Str);
		const EVerticalAlignment* Found = GetVAlignMap().Find(Normalized);
		if (!Found)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Unknown slot vertical alignment '%s'; defaulting to Fill."), *Str);
			return EUMGSlotVerticalAlignment::Fill;
		}

		switch (*Found)
		{
		case VAlign_Top:
			return EUMGSlotVerticalAlignment::Top;
		case VAlign_Center:
			return EUMGSlotVerticalAlignment::Center;
		case VAlign_Bottom:
			return EUMGSlotVerticalAlignment::Bottom;
		case VAlign_Fill:
		default:
			return EUMGSlotVerticalAlignment::Fill;
		}
	}

	EUMGSlotSizeRule ParseSlotSizeRule(const FString& Str)
	{
		const FString Normalized = NormalizeSlotToken(Str);
		if (Normalized == TEXT("fill"))
		{
			return EUMGSlotSizeRule::Fill;
		}
		if (Normalized == TEXT("auto"))
		{
			return EUMGSlotSizeRule::Auto;
		}

		UE_LOG(LogDesign2UMG, Warning, TEXT("Unknown slot size rule '%s'; defaulting to Auto."), *Str);
		return EUMGSlotSizeRule::Auto;
	}

	const TCHAR* FormatSlotHorizontalAlignment(EUMGSlotHorizontalAlignment Value)
	{
		switch (Value)
		{
		case EUMGSlotHorizontalAlignment::Left:
			return TEXT("Left");
		case EUMGSlotHorizontalAlignment::Center:
			return TEXT("Center");
		case EUMGSlotHorizontalAlignment::Right:
			return TEXT("Right");
		case EUMGSlotHorizontalAlignment::Fill:
		default:
			return TEXT("Fill");
		}
	}

	const TCHAR* FormatSlotVerticalAlignment(EUMGSlotVerticalAlignment Value)
	{
		switch (Value)
		{
		case EUMGSlotVerticalAlignment::Top:
			return TEXT("Top");
		case EUMGSlotVerticalAlignment::Center:
			return TEXT("Center");
		case EUMGSlotVerticalAlignment::Bottom:
			return TEXT("Bottom");
		case EUMGSlotVerticalAlignment::Fill:
		default:
			return TEXT("Fill");
		}
	}

	const TCHAR* FormatSlotSizeRule(EUMGSlotSizeRule Value)
	{
		return Value == EUMGSlotSizeRule::Fill ? TEXT("Fill") : TEXT("Auto");
	}

	EHorizontalAlignment ToSlateHorizontalAlignment(EUMGSlotHorizontalAlignment Value)
	{
		switch (Value)
		{
		case EUMGSlotHorizontalAlignment::Left:
			return HAlign_Left;
		case EUMGSlotHorizontalAlignment::Center:
			return HAlign_Center;
		case EUMGSlotHorizontalAlignment::Right:
			return HAlign_Right;
		case EUMGSlotHorizontalAlignment::Fill:
		default:
			return HAlign_Fill;
		}
	}

	EVerticalAlignment ToSlateVerticalAlignment(EUMGSlotVerticalAlignment Value)
	{
		switch (Value)
		{
		case EUMGSlotVerticalAlignment::Top:
			return VAlign_Top;
		case EUMGSlotVerticalAlignment::Center:
			return VAlign_Center;
		case EUMGSlotVerticalAlignment::Bottom:
			return VAlign_Bottom;
		case EUMGSlotVerticalAlignment::Fill:
		default:
			return VAlign_Fill;
		}
	}

	bool IsFillSizeRule(EUMGSlotSizeRule Value)
	{
		return Value == EUMGSlotSizeRule::Fill;
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
