// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FontResolver.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "DesignSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Font.h"

#define LOCTEXT_NAMESPACE "FontResolver"

TMap<FString, FString> FFontResolver::FontAssetCache;

namespace
{
	TMap<FString, FString> GNormalizedProjectFontAssetIndex;
	TMap<FString, TMap<FString, FString>> GKnownIconGlyphCache;
	TMap<FString, TMap<FString, FString>> GKnownIconNameCache;
	TFunction<FString(const FString&)> GMissingFontResolver;
	TFunction<void(const FString&)> GKnownIconGlyphPrimer;
	bool bProjectFontIndexBuilt = false;
	bool bResolvingMissingFontExternally = false;
	bool bPrimingKnownIconExternally = false;
	FCriticalSection GFontIndexMutex;

	FString NormalizeFontFamilyKeyForResolver(const FString& InFamily)
	{
		FString Key = InFamily.ToLower().TrimStartAndEnd();
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		Key.ReplaceInline(TEXT("-"), TEXT(""));
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		return Key;
	}

	FString NormalizeIconNameKeyForResolver(const FString& InName)
	{
		FString Key = InName.ToLower().TrimStartAndEnd();
		Key.ReplaceInline(TEXT(" "), TEXT("-"));
		Key.ReplaceInline(TEXT("_"), TEXT("-"));
		if (Key == TEXT("more-horizontal"))
		{
			return TEXT("ellipsis");
		}
		if (Key == TEXT("more-vertical"))
		{
			return TEXT("ellipsis-vertical");
		}
		return Key;
	}

	bool IsEngineAssetPath(const FString& ObjectPath)
	{
		return ObjectPath.StartsWith(TEXT("/Engine/"), ESearchCase::IgnoreCase);
	}

	bool IsAutomationImportedFontPath(const FString& ObjectPath)
	{
		return ObjectPath.Contains(TEXT("/Design2UMG/Automation/ImportedFonts/"), ESearchCase::IgnoreCase);
	}

	void IndexFontAssetName(const FString& AssetName, const FString& ObjectPath)
	{
		const FString Normalized = NormalizeFontFamilyKeyForResolver(AssetName);
		if (!Normalized.IsEmpty())
		{
			if (FString* ExistingPath = GNormalizedProjectFontAssetIndex.Find(Normalized))
			{
				const bool bExistingEngine = IsEngineAssetPath(*ExistingPath);
				const bool bIncomingEngine = IsEngineAssetPath(ObjectPath);
				const bool bExistingAutomation = IsAutomationImportedFontPath(*ExistingPath);
				const bool bIncomingAutomation = IsAutomationImportedFontPath(ObjectPath);
				if ((bExistingEngine && !bIncomingEngine)
					|| (bExistingAutomation && !bIncomingAutomation))
				{
					*ExistingPath = ObjectPath;
				}
			}
			else
			{
				GNormalizedProjectFontAssetIndex.Add(Normalized, ObjectPath);
			}
		}
	}

	bool IsLoadableFontPath(const FString& ObjectPath)
	{
		return !ObjectPath.IsEmpty() && LoadObject<UFont>(nullptr, *ObjectPath) != nullptr;
	}

	void BuildProjectFontAssetIndexIfNeeded()
	{
		FScopeLock Lock(&GFontIndexMutex);

		if (bProjectFontIndexBuilt)
			return;

		GNormalizedProjectFontAssetIndex.Reset();

		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		TArray<FAssetData> FontAssets;
		AR.GetAssetsByClass(UFont::StaticClass()->GetClassPathName(), FontAssets, true);
		for (const FAssetData& Asset : FontAssets)
		{
			IndexFontAssetName(Asset.AssetName.ToString(), Asset.GetObjectPathString());
		}

		bProjectFontIndexBuilt = true;
	}

	void InvalidateProjectFontAssetIndex()
	{
		bProjectFontIndexBuilt = false;
		GNormalizedProjectFontAssetIndex.Reset();
	}

	bool TryResolveEngineFontFallback(const FString& FontFamily, FString& OutAssetPath)
	{
		const FString Normalized = NormalizeFontFamilyKeyForResolver(FontFamily);
		if (Normalized == TEXT("roboto"))
		{
			const FString RobotoPath = TEXT("/Engine/EngineFonts/Roboto.Roboto");
			if (LoadObject<UFont>(nullptr, *RobotoPath))
			{
				OutAssetPath = RobotoPath;
				return true;
			}
		}
		return false;
	}

}

FString FFontResolver::FindFontAssetPath(const FString& FontFamily)
{
	if (FontFamily.TrimStartAndEnd().IsEmpty())
		return FString();

	const FString NormalizedFamily = NormalizeFontFamilyKeyForResolver(FontFamily);

	if (const FString* Path = FontAssetCache.Find(NormalizedFamily))
	{
		if (IsLoadableFontPath(*Path))
		{
			return *Path;
		}
		FontAssetCache.Remove(NormalizedFamily);
	}

	BuildProjectFontAssetIndexIfNeeded();
	if (const FString* IndexedPath = GNormalizedProjectFontAssetIndex.Find(NormalizedFamily))
	{
		if (IsLoadableFontPath(*IndexedPath))
		{
			FontAssetCache.FindOrAdd(NormalizedFamily) = *IndexedPath;
			return *IndexedPath;
		}
		GNormalizedProjectFontAssetIndex.Remove(NormalizedFamily);
	}

	// Material Symbols families differ by variant suffix (Sharp/Rounded/Outlined).
	// If exact family name is absent, resolve to any imported Material Symbols asset.
	if (NormalizedFamily.Contains(TEXT("materialsymbol")) || NormalizedFamily.Contains(TEXT("materialicon")))
	{
		for (const TPair<FString, FString>& Pair : GNormalizedProjectFontAssetIndex)
		{
			if (Pair.Key.Contains(TEXT("materialsymbol")) || Pair.Key.Contains(TEXT("materialicon")))
			{
				if (IsLoadableFontPath(Pair.Value))
				{
					FontAssetCache.FindOrAdd(NormalizedFamily) = Pair.Value;
					return Pair.Value;
				}
			}
		}
	}

	if (GMissingFontResolver && !bResolvingMissingFontExternally)
	{
		TGuardValue<bool> GuardResolvingExternal(bResolvingMissingFontExternally, true);
		FString ImportedPath = GMissingFontResolver(FontFamily);
		if (IsLoadableFontPath(ImportedPath))
		{
			FontAssetCache.FindOrAdd(NormalizedFamily) = ImportedPath;
			return ImportedPath;
		}
	}

	FString EngineFallbackPath;
	if (TryResolveEngineFontFallback(FontFamily, EngineFallbackPath))
	{
		FontAssetCache.FindOrAdd(NormalizedFamily) = EngineFallbackPath;
		return EngineFallbackPath;
	}

	FontAssetCache.FindOrAdd(NormalizedFamily) = FString();
	return FString();
}

FString FFontResolver::FindKnownIconGlyph(const FString& FontFamily, const FString& IconName)
{
	if (FontFamily.TrimStartAndEnd().IsEmpty() || IconName.TrimStartAndEnd().IsEmpty())
	{
		return FString();
	}

	const FString NormalizedFamily = NormalizeFontFamilyKeyForResolver(FontFamily);
	const FString NormalizedIcon = NormalizeIconNameKeyForResolver(IconName);
	if (NormalizedIcon.IsEmpty())
	{
		return FString();
	}

	if (const TMap<FString, FString>* Glyphs = GKnownIconGlyphCache.Find(NormalizedFamily))
	{
		if (const FString* Glyph = Glyphs->Find(NormalizedIcon))
		{
			return *Glyph;
		}
	}

	if (GKnownIconGlyphPrimer && !bPrimingKnownIconExternally)
	{
		TGuardValue<bool> GuardPrimingExternal(bPrimingKnownIconExternally, true);
		GKnownIconGlyphPrimer(FontFamily);
		if (const TMap<FString, FString>* Glyphs = GKnownIconGlyphCache.Find(NormalizedFamily))
		{
			if (const FString* Glyph = Glyphs->Find(NormalizedIcon))
			{
				return *Glyph;
			}
		}
	}

	return FString();
}

FString FFontResolver::FindKnownIconNameByGlyph(const FString& FontFamily, const FString& Glyph)
{
	if (FontFamily.TrimStartAndEnd().IsEmpty() || Glyph.TrimStartAndEnd().IsEmpty())
	{
		return FString();
	}

	const FString NormalizedFamily = NormalizeFontFamilyKeyForResolver(FontFamily);
	if (const TMap<FString, FString>* IconNames = GKnownIconNameCache.Find(NormalizedFamily))
	{
		if (const FString* IconName = IconNames->Find(Glyph))
		{
			return *IconName;
		}
	}

	if (GKnownIconGlyphPrimer && !bPrimingKnownIconExternally)
	{
		TGuardValue<bool> GuardPrimingExternal(bPrimingKnownIconExternally, true);
		GKnownIconGlyphPrimer(FontFamily);
		if (const TMap<FString, FString>* IconNames = GKnownIconNameCache.Find(NormalizedFamily))
		{
			if (const FString* IconName = IconNames->Find(Glyph))
			{
				return *IconName;
			}
		}
	}

	return FString();
}

void FFontResolver::ClearCache()
{
	FontAssetCache.Empty();
	InvalidateProjectFontAssetIndex();
	GKnownIconGlyphCache.Reset();
	GKnownIconNameCache.Reset();
}

void FFontResolver::RegisterKnownIconGlyphs(
	const FString& FontFamily,
	const TMap<FString, FString>& Glyphs,
	const TMap<FString, FString>& IconNames)
{
	const FString NormalizedFamily = NormalizeFontFamilyKeyForResolver(FontFamily);
	if (NormalizedFamily.IsEmpty())
	{
		return;
	}

	GKnownIconGlyphCache.Add(NormalizedFamily, Glyphs);
	GKnownIconNameCache.Add(NormalizedFamily, IconNames);
}

void FFontResolver::RegisterMissingFontResolver(TFunction<FString(const FString&)> InResolver)
{
	GMissingFontResolver = MoveTemp(InResolver);
}

void FFontResolver::RegisterKnownIconGlyphPrimer(TFunction<void(const FString&)> InPrimer)
{
	GKnownIconGlyphPrimer = MoveTemp(InPrimer);
}

void FFontResolver::ResetExternalResolvers()
{
	GMissingFontResolver = nullptr;
	GKnownIconGlyphPrimer = nullptr;
}

int32 FFontResolver::ParseWeightValue(const FString& Weight)
{
	FString W = Weight.ToLower().TrimStartAndEnd();
	if (W == TEXT("thin"))       return 100;
	if (W == TEXT("extralight") || W == TEXT("ultralight")) return 200;
	if (W == TEXT("light"))      return 300;
	if (W == TEXT("normal") || W == TEXT("regular")) return 400;
	if (W == TEXT("medium"))     return 500;
	if (W == TEXT("semibold") || W == TEXT("demibold")) return 600;
	if (W == TEXT("bold"))       return 700;
	if (W == TEXT("extrabold") || W == TEXT("ultrabold")) return 800;
	if (W == TEXT("black") || W == TEXT("heavy")) return 900;

	int32 Num = FCString::Atoi(*W);
	if (Num >= 100 && Num <= 900)
		return FMath::Clamp(FMath::RoundToInt(Num / 100.0f) * 100, 100, 900);

	return 400;
}

int32 FFontResolver::WeightFromTypefaceName(const FString& Name)
{
	FString N = Name.ToLower();
	if (N.Contains(TEXT("black")) || N.Contains(TEXT("heavy")))  return 900;
	if (N.Contains(TEXT("extrabold")) || N.Contains(TEXT("ultrabold"))) return 800;
	if (N.Contains(TEXT("semibold")) || N.Contains(TEXT("demibold"))) return 600;
	if (N.Contains(TEXT("bold")))     return 700;
	if (N.Contains(TEXT("medium")))   return 500;
	if (N.Contains(TEXT("light")) && N.Contains(TEXT("extra")))  return 200;
	if (N.Contains(TEXT("light")) && N.Contains(TEXT("ultra")))  return 200;
	if (N.Contains(TEXT("thin")))     return 100;
	if (N.Contains(TEXT("light")))    return 300;
	return 400;
}

FString FFontResolver::WeightNameFromNumeric(int32 Weight)
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

FString FFontResolver::FindBestTypeface(const FString& FontFamily, const FString& RequestedWeight)
{
	FString AssetPath = FindFontAssetPath(FontFamily);
	if (AssetPath.IsEmpty())
		return FString();

	UFont* Font = LoadObject<UFont>(nullptr, *AssetPath);
	const FCompositeFont* Composite = Font ? Font->GetCompositeFont() : nullptr;
	if (!Composite || Composite->DefaultTypeface.Fonts.Num() == 0)
		return FString();

	int32 TargetWeight = ParseWeightValue(RequestedWeight);
	int32 BestDist = INT_MAX;
	FString BestName;

	for (const FTypefaceEntry& Entry : Composite->DefaultTypeface.Fonts)
	{
		FString EntryName = Entry.Name.ToString();
		int32 EntryWeight = WeightFromTypefaceName(EntryName);
		int32 Dist = FMath::Abs(EntryWeight - TargetWeight);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			BestName = EntryName;
		}
	}

	UE_LOG(LogDesign2UMG, Verbose, TEXT("FontResolver: family='%s' requested='%s' selected='%s' entries=%d"),
		*FontFamily, *RequestedWeight, *BestName, Composite->DefaultTypeface.Fonts.Num());
	return BestName;
}

TArray<FString> FFontResolver::FindMissingFonts(const FDesignDocument& Document)
{
	TSet<FString> UsedFamilies;

	// Collect from Document.Fonts[]
	for (const FDesignFont& Font : Document.Fonts)
	{
		if (!Font.Name.IsEmpty())
			UsedFamilies.Add(Font.Name);
	}

	// Collect from node tree
	CollectFontFamilies(Document.Children, UsedFamilies);

	if (UsedFamilies.Num() == 0)
		return {};

	// Find missing fonts by using FindFontAssetPath fuzzy matching.
	// This keeps result consistent with runtime resolution behavior.
	TArray<FString> Missing;
	for (const FString& Family : UsedFamilies)
	{
		if (FindFontAssetPath(Family).IsEmpty())
			Missing.Add(Family);
	}

	if (Missing.Num() > 0)
	{
		FString List = FString::Join(Missing, TEXT(", "));
		UE_LOG(LogDesign2UMG, Warning, TEXT("Missing fonts: %s"), *List);
		FDesign2UMGNotification::Warning(
			FText::Format(LOCTEXT("MissingFonts", "Design2UMG: {0} font(s) not found in project: {1}"),
				FText::AsNumber(Missing.Num()), FText::FromString(List)));
	}

	return Missing;
}

void FFontResolver::CollectFontFamilies(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutFamilies)
{
	for (const auto& Node : Nodes)
	{
		if (!Node.IsValid()) continue;

		if (!Node->FontFamily.IsEmpty())
			OutFamilies.Add(Node->FontFamily);
		if (!Node->IconFontFamily.IsEmpty())
			OutFamilies.Add(Node->IconFontFamily);

		CollectFontFamilies(Node->Children, OutFamilies);
	}
}

TSet<FString> FFontResolver::ScanProjectFontAssets()
{
	TSet<FString> Result;
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> FontAssets;
	AR.GetAssetsByClass(UFont::StaticClass()->GetClassPathName(), FontAssets, true);

	for (const FAssetData& Asset : FontAssets)
	{
		Result.Add(Asset.AssetName.ToString());
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE



