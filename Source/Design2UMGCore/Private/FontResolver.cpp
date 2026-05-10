// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FontResolver.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "DesignSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeLock.h"

#define LOCTEXT_NAMESPACE "FontResolver"

namespace
{
	TMap<FString, FString> GFontAssetCache;
	TMap<FString, FString> GNormalizedProjectFontAssetIndex;
	TMap<FString, TMap<FString, FString>> GKnownIconGlyphCache;
	TMap<FString, TMap<FString, FString>> GKnownIconNameCache;
	TFunction<FString(const FString&)> GMissingFontResolver;
	TFunction<void(const FString&)> GKnownIconGlyphPrimer;
	bool bProjectFontIndexBuilt = false;
	thread_local bool bResolvingMissingFontExternally = false;
	thread_local bool bPrimingKnownIconExternally = false;
	FCriticalSection GFontResolverCacheMutex;
	FCriticalSection GFontIndexMutex;

	void CollectFontFamilies(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutFamilies);

	FString NormalizeCompactResolverKey(const FString& InValue)
	{
		FString Key = InValue.ToLower().TrimStartAndEnd();
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		Key.ReplaceInline(TEXT("-"), TEXT(""));
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		return Key;
	}

	FString NormalizeFontFamilyKeyForResolver(const FString& InFamily)
	{
		return NormalizeCompactResolverKey(InFamily);
	}

	void AddUniqueLookupKey(TArray<FString>& Keys, const FString& Candidate)
	{
		if (!Candidate.IsEmpty())
		{
			Keys.AddUnique(Candidate);
		}
	}

	bool IsTypefaceQualifierToken(const FString& Token)
	{
		const FString Normalized = NormalizeCompactResolverKey(Token);
		static const TSet<FString> Qualifiers = {
			TEXT("thin"),
			TEXT("extralight"),
			TEXT("ultralight"),
			TEXT("light"),
			TEXT("regular"),
			TEXT("normal"),
			TEXT("book"),
			TEXT("roman"),
			TEXT("medium"),
			TEXT("semibold"),
			TEXT("demibold"),
			TEXT("bold"),
			TEXT("extrabold"),
			TEXT("ultrabold"),
			TEXT("black"),
			TEXT("heavy"),
			TEXT("italic"),
			TEXT("oblique"),
		};
		return Qualifiers.Contains(Normalized);
	}

	TArray<FString> BuildFontFamilyLookupKeys(const FString& FontFamily)
	{
		TArray<FString> Keys;
		AddUniqueLookupKey(Keys, NormalizeFontFamilyKeyForResolver(FontFamily));

		FString Tokenized = FontFamily;
		Tokenized.ReplaceInline(TEXT("-"), TEXT(" "));
		Tokenized.ReplaceInline(TEXT("_"), TEXT(" "));

		TArray<FString> Tokens;
		Tokenized.ParseIntoArrayWS(Tokens);
		while (Tokens.Num() > 1 && IsTypefaceQualifierToken(Tokens.Last()))
		{
			Tokens.Pop();
		}

		if (Tokens.Num() > 0)
		{
			AddUniqueLookupKey(Keys, NormalizeFontFamilyKeyForResolver(FString::Join(Tokens, TEXT(" "))));
		}

		return Keys;
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
		return ObjectPath.Contains(TEXT("/Design2UMG/Automation/"), ESearchCase::IgnoreCase)
			&& ObjectPath.Contains(TEXT("/ImportedFonts/"), ESearchCase::IgnoreCase);
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

	bool TryGetIndexedFontAssetPath(const FString& NormalizedFamily, FString& OutPath)
	{
		FScopeLock Lock(&GFontIndexMutex);
		if (const FString* IndexedPath = GNormalizedProjectFontAssetIndex.Find(NormalizedFamily))
		{
			OutPath = *IndexedPath;
			return true;
		}
		return false;
	}

	void RemoveIndexedFontAssetPath(const FString& NormalizedFamily)
	{
		FScopeLock Lock(&GFontIndexMutex);
		GNormalizedProjectFontAssetIndex.Remove(NormalizedFamily);
	}

	TArray<FString> SnapshotMaterialSymbolAssetPaths()
	{
		TArray<FString> CandidatePaths;
		FScopeLock Lock(&GFontIndexMutex);
		for (const TPair<FString, FString>& Pair : GNormalizedProjectFontAssetIndex)
		{
			if (Pair.Key.Contains(TEXT("materialsymbol")) || Pair.Key.Contains(TEXT("materialicon")))
			{
				CandidatePaths.Add(Pair.Value);
			}
		}
		return CandidatePaths;
	}

	bool TryGetKnownIconCacheValue(
		const TMap<FString, TMap<FString, FString>>& Cache,
		const FString& NormalizedFamily,
		const FString& Key,
		FString& OutValue)
	{
		FScopeLock Lock(&GFontResolverCacheMutex);
		if (const TMap<FString, FString>* Values = Cache.Find(NormalizedFamily))
		{
			if (const FString* Value = Values->Find(Key))
			{
				OutValue = *Value;
				return true;
			}
		}
		return false;
	}

	TFunction<FString(const FString&)> GetMissingFontResolverCallback()
	{
		FScopeLock Lock(&GFontResolverCacheMutex);
		return GMissingFontResolver;
	}

	TFunction<void(const FString&)> GetKnownIconGlyphPrimerCallback()
	{
		FScopeLock Lock(&GFontResolverCacheMutex);
		return GKnownIconGlyphPrimer;
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
		FScopeLock Lock(&GFontIndexMutex);
		bProjectFontIndexBuilt = false;
		GNormalizedProjectFontAssetIndex.Reset();
	}

	bool TryResolveEngineFontFallback(const FString& FontFamily, FString& OutAssetPath)
	{
		const TArray<FString> LookupKeys = BuildFontFamilyLookupKeys(FontFamily);
		if (LookupKeys.Num() == 0)
		{
			return false;
		}

		FString DefaultFontPackageName = UWidget::GetDefaultFontName();
		if (DefaultFontPackageName.IsEmpty())
		{
			return false;
		}

		DefaultFontPackageName = FPackageName::ObjectPathToPackageName(DefaultFontPackageName);
		const FString DefaultFontAssetName = FPackageName::GetLongPackageAssetName(DefaultFontPackageName);
		if (DefaultFontAssetName.IsEmpty())
		{
			return false;
		}

		const FString DefaultFontPath = DefaultFontPackageName + TEXT(".") + DefaultFontAssetName;
		const FString DefaultFontKey = NormalizeFontFamilyKeyForResolver(DefaultFontAssetName);
		if (!DefaultFontPath.IsEmpty()
			&& LookupKeys.Contains(DefaultFontKey)
			&& IsLoadableFontPath(DefaultFontPath))
		{
			OutAssetPath = DefaultFontPath;
			return true;
		}
		return false;
	}

}

FString FFontResolver::FindFontAssetPath(const FString& FontFamily)
{
	if (FontFamily.TrimStartAndEnd().IsEmpty())
		return FString();

	const FString NormalizedFamily = NormalizeFontFamilyKeyForResolver(FontFamily);
	const TArray<FString> LookupKeys = BuildFontFamilyLookupKeys(FontFamily);

	FString CachedPath;
	bool bHasCachedPath = false;
	{
		FScopeLock Lock(&GFontResolverCacheMutex);
		if (const FString* Path = GFontAssetCache.Find(NormalizedFamily))
		{
			CachedPath = *Path;
			bHasCachedPath = true;
		}
	}
	if (bHasCachedPath)
	{
		if (IsLoadableFontPath(CachedPath))
		{
			return CachedPath;
		}
		FScopeLock Lock(&GFontResolverCacheMutex);
		GFontAssetCache.Remove(NormalizedFamily);
	}

	BuildProjectFontAssetIndexIfNeeded();
	for (const FString& LookupKey : LookupKeys)
	{
		FString IndexedPath;
		if (!TryGetIndexedFontAssetPath(LookupKey, IndexedPath))
		{
			continue;
		}

		if (IsLoadableFontPath(IndexedPath))
		{
			FScopeLock Lock(&GFontResolverCacheMutex);
			GFontAssetCache.FindOrAdd(NormalizedFamily) = IndexedPath;
			return IndexedPath;
		}

		RemoveIndexedFontAssetPath(LookupKey);
	}

	// Material Symbols families differ by variant suffix (Sharp/Rounded/Outlined).
	// If exact family name is absent, resolve to any imported Material Symbols asset.
	bool bRequestsMaterialSymbolFamily = false;
	for (const FString& LookupKey : LookupKeys)
	{
		if (LookupKey.Contains(TEXT("materialsymbol")) || LookupKey.Contains(TEXT("materialicon")))
		{
			bRequestsMaterialSymbolFamily = true;
			break;
		}
	}
	if (bRequestsMaterialSymbolFamily)
	{
		for (const FString& CandidatePath : SnapshotMaterialSymbolAssetPaths())
		{
			if (IsLoadableFontPath(CandidatePath))
			{
				FScopeLock Lock(&GFontResolverCacheMutex);
				GFontAssetCache.FindOrAdd(NormalizedFamily) = CandidatePath;
				return CandidatePath;
			}
		}
	}

	FString EngineFallbackPath;
	if (TryResolveEngineFontFallback(FontFamily, EngineFallbackPath))
	{
		FScopeLock Lock(&GFontResolverCacheMutex);
		GFontAssetCache.FindOrAdd(NormalizedFamily) = EngineFallbackPath;
		return EngineFallbackPath;
	}

	const TFunction<FString(const FString&)> MissingFontResolver = GetMissingFontResolverCallback();
	if (MissingFontResolver && !bResolvingMissingFontExternally)
	{
		TGuardValue<bool> GuardResolvingExternal(bResolvingMissingFontExternally, true);
		FString ImportedPath = MissingFontResolver(FontFamily);
		if (IsLoadableFontPath(ImportedPath))
		{
			FScopeLock Lock(&GFontResolverCacheMutex);
			GFontAssetCache.FindOrAdd(NormalizedFamily) = ImportedPath;
			return ImportedPath;
		}
	}

	{
		FScopeLock Lock(&GFontResolverCacheMutex);
		GFontAssetCache.FindOrAdd(NormalizedFamily) = FString();
	}
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

	FString Glyph;
	if (TryGetKnownIconCacheValue(GKnownIconGlyphCache, NormalizedFamily, NormalizedIcon, Glyph))
	{
		return Glyph;
	}

	const TFunction<void(const FString&)> KnownIconGlyphPrimer = GetKnownIconGlyphPrimerCallback();
	if (KnownIconGlyphPrimer && !bPrimingKnownIconExternally)
	{
		TGuardValue<bool> GuardPrimingExternal(bPrimingKnownIconExternally, true);
		KnownIconGlyphPrimer(FontFamily);
		if (TryGetKnownIconCacheValue(GKnownIconGlyphCache, NormalizedFamily, NormalizedIcon, Glyph))
		{
			return Glyph;
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
	FString IconName;
	if (TryGetKnownIconCacheValue(GKnownIconNameCache, NormalizedFamily, Glyph, IconName))
	{
		return IconName;
	}

	const TFunction<void(const FString&)> KnownIconGlyphPrimer = GetKnownIconGlyphPrimerCallback();
	if (KnownIconGlyphPrimer && !bPrimingKnownIconExternally)
	{
		TGuardValue<bool> GuardPrimingExternal(bPrimingKnownIconExternally, true);
		KnownIconGlyphPrimer(FontFamily);
		if (TryGetKnownIconCacheValue(GKnownIconNameCache, NormalizedFamily, Glyph, IconName))
		{
			return IconName;
		}
	}

	return FString();
}

void FFontResolver::ClearCache()
{
	{
		FScopeLock Lock(&GFontResolverCacheMutex);
		GFontAssetCache.Empty();
		GKnownIconGlyphCache.Reset();
		GKnownIconNameCache.Reset();
	}
	InvalidateProjectFontAssetIndex();
}

void FFontResolver::RegisterFontAssetPath(const FString& FontFamily, const FString& ObjectPath)
{
	const FString NormalizedFamily = NormalizeFontFamilyKeyForResolver(FontFamily);
	if (NormalizedFamily.IsEmpty() || ObjectPath.IsEmpty())
	{
		return;
	}

	{
		FScopeLock Lock(&GFontResolverCacheMutex);
		GFontAssetCache.FindOrAdd(NormalizedFamily) = ObjectPath;
	}

	{
		FScopeLock Lock(&GFontIndexMutex);
		GNormalizedProjectFontAssetIndex.FindOrAdd(NormalizedFamily) = ObjectPath;
	}
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

	FScopeLock Lock(&GFontResolverCacheMutex);
	GKnownIconGlyphCache.Add(NormalizedFamily, Glyphs);
	GKnownIconNameCache.Add(NormalizedFamily, IconNames);
}

void FFontResolver::RegisterMissingFontResolver(TFunction<FString(const FString&)> InResolver)
{
	FScopeLock Lock(&GFontResolverCacheMutex);
	GMissingFontResolver = MoveTemp(InResolver);
}

void FFontResolver::RegisterKnownIconGlyphPrimer(TFunction<void(const FString&)> InPrimer)
{
	FScopeLock Lock(&GFontResolverCacheMutex);
	GKnownIconGlyphPrimer = MoveTemp(InPrimer);
}

void FFontResolver::ResetExternalResolvers()
{
	FScopeLock Lock(&GFontResolverCacheMutex);
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

namespace
{
int32 WeightFromTypefaceName(const FString& Name)
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

namespace
{
void CollectFontFamilies(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutFamilies)
{
	for (const auto& Node : Nodes)
	{
		if (!Node.IsValid()) continue;

		if (!Node->Text.FontFamily.IsEmpty())
			OutFamilies.Add(Node->Text.FontFamily);
		if (!Node->Text.IconFontFamily.IsEmpty())
			OutFamilies.Add(Node->Text.IconFontFamily);

		CollectFontFamilies(Node->Children, OutFamilies);
	}
}
}

#undef LOCTEXT_NAMESPACE



