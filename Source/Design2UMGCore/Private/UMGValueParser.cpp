// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGValueParser.h"
#include "Design2UMGLog.h"
#include "DesignColorUtils.h"

namespace
{
	const TMap<FString, EUMGSlotHorizontalAlignment>& GetHAlignMap()
	{
		static const TMap<FString, EUMGSlotHorizontalAlignment> Map = {
			{TEXT("left"), EUMGSlotHorizontalAlignment::Left}, {TEXT("center"), EUMGSlotHorizontalAlignment::Center},
			{TEXT("right"), EUMGSlotHorizontalAlignment::Right}, {TEXT("fill"), EUMGSlotHorizontalAlignment::Fill}
		};
		return Map;
	}

	const TMap<FString, EUMGSlotVerticalAlignment>& GetVAlignMap()
	{
		static const TMap<FString, EUMGSlotVerticalAlignment> Map = {
			{TEXT("top"), EUMGSlotVerticalAlignment::Top}, {TEXT("center"), EUMGSlotVerticalAlignment::Center},
			{TEXT("bottom"), EUMGSlotVerticalAlignment::Bottom}, {TEXT("fill"), EUMGSlotVerticalAlignment::Fill}
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
}

namespace FUMGValueParser
{
	FLinearColor ParseColor(const FString& ColorStr)
	{
		FString Trimmed = ColorStr.TrimStartAndEnd();

		if (Trimmed.StartsWith(TEXT("#")))
		{
			return DesignColorUtils::ParseHexColor(Trimmed);
		}

	if (const FLinearColor* Named = DesignColorUtils::GetNamedColors().Find(Trimmed.ToLower()))
	{
		return *Named;
	}

	UE_LOG(LogDesign2UMG, Warning, TEXT("Unknown color '%s'; defaulting to White."), *ColorStr);
	return FLinearColor::White;
}

	EUMGSlotHorizontalAlignment ParseSlotHorizontalAlignment(const FString& Str)
	{
		const FString Normalized = NormalizeSlotToken(Str);
		const EUMGSlotHorizontalAlignment* Found = GetHAlignMap().Find(Normalized);
		if (!Found)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Unknown slot horizontal alignment '%s'; defaulting to Fill."), *Str);
			return EUMGSlotHorizontalAlignment::Fill;
		}
		return *Found;
	}

	EUMGSlotVerticalAlignment ParseSlotVerticalAlignment(const FString& Str)
	{
		const FString Normalized = NormalizeSlotToken(Str);
		const EUMGSlotVerticalAlignment* Found = GetVAlignMap().Find(Normalized);
		if (!Found)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Unknown slot vertical alignment '%s'; defaulting to Fill."), *Str);
			return EUMGSlotVerticalAlignment::Fill;
		}
		return *Found;
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

	bool IsNearlyEqualMargin(const FMargin& A, const FMargin& B)
	{
		return FMath::IsNearlyEqual(A.Left, B.Left)
			&& FMath::IsNearlyEqual(A.Top, B.Top)
			&& FMath::IsNearlyEqual(A.Right, B.Right)
			&& FMath::IsNearlyEqual(A.Bottom, B.Bottom);
	}
}
