// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignFontImportCatalog.h"

namespace DesignFontImportCatalog
{
	FString NormalizeFontFamilyKey(const FString& InFamily)
	{
		FString Key = InFamily.ToLower().TrimStartAndEnd();
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		Key.ReplaceInline(TEXT("-"), TEXT(""));
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		return Key;
	}

	const TMap<FString, FString>& GetKnownWebFontUrls()
	{
		static const TMap<FString, FString> KnownUrls = {
			{TEXT("inter"), TEXT("https://github.com/google/fonts/raw/main/ofl/inter/Inter%5Bopsz,wght%5D.ttf")},
			{TEXT("rubik"), TEXT("https://github.com/google/fonts/raw/main/ofl/rubik/Rubik%5Bwght%5D.ttf")},
			{TEXT("robotomono"), TEXT("https://github.com/google/fonts/raw/main/ofl/robotomono/RobotoMono%5Bwght%5D.ttf")},
			{TEXT("funneldisplay"), TEXT("https://github.com/google/fonts/raw/main/ofl/funneldisplay/FunnelDisplay%5Bwght%5D.ttf")},
			{TEXT("oxygen"), TEXT("https://github.com/google/fonts/raw/main/ofl/oxygen/Oxygen-Regular.ttf")},
			{TEXT("geist"), TEXT("https://github.com/google/fonts/raw/main/ofl/geist/Geist%5Bwght%5D.ttf")},
			{TEXT("jetbrainsmono"), TEXT("https://github.com/google/fonts/raw/main/ofl/jetbrainsmono/JetBrainsMono%5Bwght%5D.ttf")},
			{TEXT("lucide"), TEXT("https://unpkg.com/lucide-static/font/lucide.ttf")},
			{TEXT("materialsymbolssharp"), TEXT("https://github.com/google/material-design-icons/raw/master/variablefont/MaterialSymbolsSharp%5BFILL,GRAD,opsz,wght%5D.ttf")},
			{TEXT("pingfangsc"), TEXT("https://github.com/google/fonts/raw/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf")},
			{TEXT("buenosaires"), TEXT("https://github.com/google/fonts/raw/main/ofl/inter/Inter%5Bopsz,wght%5D.ttf")},
		};
		return KnownUrls;
	}

	const TMap<FString, FString>& GetKnownIconFontCssUrls()
	{
		static const TMap<FString, FString> KnownUrls = {
			{TEXT("lucide"), TEXT("https://unpkg.com/lucide-static/font/lucide.css")},
		};
		return KnownUrls;
	}

	const TSet<FString>& GetKnownCssFontFamilies()
	{
		static const TSet<FString> KnownCssFamilies = {
			TEXT("inter"),
			TEXT("roboto"),
			TEXT("rubik"),
			TEXT("funneldisplay"),
			TEXT("geist"),
			TEXT("jetbrainsmono"),
			TEXT("oxygen"),
			TEXT("materialsymbolssharp"),
			TEXT("materialsymbolsrounded"),
			TEXT("materialsymbolsoutlined"),
		};
		return KnownCssFamilies;
	}

	const TArray<int32>& GetDefaultKnownCssFontWeights()
	{
		static const TArray<int32> KnownCssWeights = { 400, 500, 600, 700 };
		return KnownCssWeights;
	}

	int32 NormalizeCssWeightValue(const int32 Weight)
	{
		return FMath::Clamp(FMath::RoundToInt(static_cast<float>(Weight) / 100.0f) * 100, 100, 900);
	}

	TArray<int32> ResolveKnownCssFontWeights(
		const FString& FontFamily,
		const TMap<FString, TSet<int32>>& RequestedWeightsByFamily)
	{
		TSet<int32> WeightSet;
		if (const TSet<int32>* RequestedWeights = RequestedWeightsByFamily.Find(NormalizeFontFamilyKey(FontFamily)))
		{
			WeightSet.Append(*RequestedWeights);
		}

		if (WeightSet.Num() == 0)
		{
			for (const int32 Weight : GetDefaultKnownCssFontWeights())
			{
				WeightSet.Add(Weight);
			}
		}
		else
		{
			WeightSet.Add(400);
		}

		TArray<int32> ResolvedWeights = WeightSet.Array();
		ResolvedWeights.Sort();
		return ResolvedWeights;
	}

	FString WeightNameFromNumeric(int32 Weight)
	{
		switch (FMath::Clamp(FMath::RoundToInt(Weight / 100.0f) * 100, 100, 900))
		{
		case 100: return TEXT("Thin");
		case 200: return TEXT("ExtraLight");
		case 300: return TEXT("Light");
		case 400: return TEXT("Regular");
		case 500: return TEXT("Medium");
		case 600: return TEXT("SemiBold");
		case 700: return TEXT("Bold");
		case 800: return TEXT("ExtraBold");
		case 900: return TEXT("Black");
		default: return TEXT("Regular");
		}
	}
}
