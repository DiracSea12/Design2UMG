// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FontResolver.h"
#include "FontResolverInternal.h"
#include "Design2UMGFontAccess.h"
#include "DesignTypeMapper.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "Design2UMGSettings.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Font.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeLock.h"

#define LOCTEXT_NAMESPACE "FontResolver"

using FontResolverInternal::EExplicitFontMappingResult;
using FontResolverInternal::NormalizeFontLookupKey;
using FontResolverInternal::IsProjectFontAssetPath;
using FontResolverInternal::IsEngineFontAssetPath;
using FontResolverInternal::TryResolveExplicitFontMapping;
using FontResolverInternal::HasExplicitFontMappingKey;

namespace
{
	enum class EKnownIconCache : uint8
	{
		GlyphByIconName,
		NameByGlyph
	};

	void CollectFontFamilies(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutFamilies);

	FString NormalizeIconNameKeyForResolver(const FString& InName)
	{
		FString Key = InName.ToLower().TrimStartAndEnd();
		Key.ReplaceInline(TEXT(" "), TEXT("-"));
		Key.ReplaceInline(TEXT("_"), TEXT("-"));

		// Static alias table: design-source icon names that map onto different
		// glyphs in the bundled icon fonts. Extend this table, not the lookup.
		static const TMap<FString, FString> IconNameAliases = {
			{TEXT("more-horizontal"), TEXT("ellipsis")},
			{TEXT("more-vertical"), TEXT("ellipsis-vertical")},
		};
		if (const FString* Alias = IconNameAliases.Find(Key))
		{
			return *Alias;
		}
		return Key;
	}

	bool FontHasUsableDefaultTypefaceEntry(const UFont* Font)
	{
		const FCompositeFont* Composite = Font ? Font->GetCompositeFont() : nullptr;
		if (!Composite)
		{
			return false;
		}

		for (const FTypefaceEntry& Entry : Composite->DefaultTypeface.Fonts)
		{
			if (Entry.Font.HasFont())
			{
				return true;
			}
		}

		return false;
	}

	bool IsUsableResolvedFontPath(const FString& ObjectPath)
	{
		if (!IsProjectFontAssetPath(ObjectPath) && !IsEngineFontAssetPath(ObjectPath))
		{
			return false;
		}

		return FontHasUsableDefaultTypefaceEntry(LoadObject<UFont>(nullptr, *ObjectPath));
	}

	bool IsLoadableProjectFontPath(const FString& ObjectPath)
	{
		return IsProjectFontAssetPath(ObjectPath) && IsUsableResolvedFontPath(ObjectPath);
	}

	bool TryResolveEngineFontAssetPath(const FString& NormalizedFamily, FString& OutPath)
	{
		if (NormalizedFamily.IsEmpty())
		{
			return false;
		}

		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> FontAssets;
		ARM.Get().GetAssetsByPath(FName(TEXT("/Engine/EngineFonts")), FontAssets, false);
		for (const FAssetData& Asset : FontAssets)
		{
			if (!Design2UMGFontAccess::IsFontAsset(Asset))
			{
				continue;
			}

			if (NormalizeFontLookupKey(Asset.AssetName.ToString()) != NormalizedFamily)
			{
				continue;
			}

			const FString ObjectPath = Asset.ToSoftObjectPath().ToString();
			if (IsUsableResolvedFontPath(ObjectPath))
			{
				OutPath = ObjectPath;
				return true;
			}
		}

		return false;
	}

	bool TryResolveDefaultFontPath(const TSoftObjectPtr<UFont>& FontAsset, FString& OutPath)
	{
		const FSoftObjectPath AssetPath = FontAsset.ToSoftObjectPath();
		if (AssetPath.IsNull())
		{
			return false;
		}

		const FString ObjectPath = AssetPath.ToString();
		if (!IsLoadableProjectFontPath(ObjectPath))
		{
			return false;
		}

		OutPath = ObjectPath;
		return true;
	}

	EExplicitFontMappingResult ResolveExplicitFontMappingAsset(const TSoftObjectPtr<UFont>& FontAsset, FString& OutPath)
	{
		const FSoftObjectPath AssetPath = FontAsset.ToSoftObjectPath();
		if (AssetPath.IsNull())
		{
			return EExplicitFontMappingResult::Invalid;
		}

		const FString ObjectPath = AssetPath.ToString();
		if (!IsLoadableProjectFontPath(ObjectPath))
		{
			return EExplicitFontMappingResult::Invalid;
		}

		OutPath = ObjectPath;
		return EExplicitFontMappingResult::Resolved;
	}

	// File-private cache state + locks behind FFontResolver. The instance lives
	// for the process; FDesign2UMGCoreModule::ShutdownModule drains it through
	// FFontResolver::ClearCache() so no resolver state outlives the module.
	struct FFontResolverCache
	{
		TMap<FString, FString> FontAssetCache;
		TMap<FString, TArray<FString>> NormalizedProjectFontAssetIndex;
		TMap<FString, TMap<FString, FString>> KnownIconGlyphCache;
		TMap<FString, TMap<FString, FString>> KnownIconNameCache;
		TSet<FString> WarnedMissingItalicTypefaces;
		bool bProjectFontIndexBuilt = false;
		FCriticalSection FontResolverCacheMutex;
		FCriticalSection FontIndexMutex;

		bool TryGetFontAssetCachePath(const FString& NormalizedFamily, FString& OutPath)
		{
			FScopeLock Lock(&FontResolverCacheMutex);
			if (const FString* Path = FontAssetCache.Find(NormalizedFamily))
			{
				OutPath = *Path;
				return true;
			}
			return false;
		}

		void RemoveFontAssetCachePath(const FString& NormalizedFamily)
		{
			FScopeLock Lock(&FontResolverCacheMutex);
			FontAssetCache.Remove(NormalizedFamily);
		}

		void StoreFontAssetCachePath(const FString& NormalizedFamily, const FString& ObjectPath)
		{
			FScopeLock Lock(&FontResolverCacheMutex);
			FontAssetCache.FindOrAdd(NormalizedFamily) = ObjectPath;
		}

		bool TryGetKnownIconCacheValue(EKnownIconCache CacheKind, const FString& NormalizedFamily, const FString& Key, FString& OutValue)
		{
			const TMap<FString, TMap<FString, FString>>& IconCache =
				(CacheKind == EKnownIconCache::GlyphByIconName) ? KnownIconGlyphCache : KnownIconNameCache;
			FScopeLock Lock(&FontResolverCacheMutex);
			if (const TMap<FString, FString>* Values = IconCache.Find(NormalizedFamily))
			{
				if (const FString* Value = Values->Find(Key))
				{
					OutValue = *Value;
					return true;
				}
			}
			return false;
		}

		void StoreKnownIconCaches(
			const FString& NormalizedFamily,
			const TMap<FString, FString>& Glyphs,
			const TMap<FString, FString>& IconNames)
		{
			FScopeLock Lock(&FontResolverCacheMutex);
			KnownIconGlyphCache.Add(NormalizedFamily, Glyphs);
			KnownIconNameCache.Add(NormalizedFamily, IconNames);
		}

		bool HasAnyRegisteredIconGlyph()
		{
			FScopeLock Lock(&FontResolverCacheMutex);
			return !KnownIconGlyphCache.IsEmpty();
		}

		void ClearResolverCaches()
		{
			FScopeLock Lock(&FontResolverCacheMutex);
			FontAssetCache.Empty();
			KnownIconGlyphCache.Reset();
			KnownIconNameCache.Reset();
			WarnedMissingItalicTypefaces.Reset();
		}

		/** Returns true exactly once per dedup key so the italic fallback warning cannot spam the log. */
		bool MarkMissingItalicTypefaceWarning(const FString& DedupKey)
		{
			FScopeLock Lock(&FontResolverCacheMutex);
			const bool bAlreadyWarned = WarnedMissingItalicTypefaces.Contains(DedupKey);
			if (!bAlreadyWarned)
			{
				WarnedMissingItalicTypefaces.Add(DedupKey);
			}
			return !bAlreadyWarned;
		}

		TArray<FString> GetIndexedFontAssetPathsSnapshot(const FString& NormalizedFamily)
		{
			FScopeLock Lock(&FontIndexMutex);
			if (const TArray<FString>* IndexedPaths = NormalizedProjectFontAssetIndex.Find(NormalizedFamily))
			{
				return *IndexedPaths;
			}
			return {};
		}

		void RemoveIndexedFontAssetPath(const FString& NormalizedFamily)
		{
			FScopeLock Lock(&FontIndexMutex);
			NormalizedProjectFontAssetIndex.Remove(NormalizedFamily);
		}

		void BuildProjectFontAssetIndexIfNeeded()
		{
			FScopeLock Lock(&FontIndexMutex);

			if (bProjectFontIndexBuilt)
				return;

			NormalizedProjectFontAssetIndex.Reset();

			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AR = ARM.Get();

			TArray<FAssetData> FontAssets;
			AR.GetAssetsByClass(Design2UMGFontAccess::GetAssetClassKey(), FontAssets, true);
			for (const FAssetData& Asset : FontAssets)
			{
				if (!Asset.PackagePath.ToString().StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				AddIndexedFontAssetName(Asset.AssetName.ToString(), Asset.ToSoftObjectPath().ToString());
			}

			bProjectFontIndexBuilt = true;
		}

		void InvalidateProjectFontAssetIndex()
		{
			FScopeLock Lock(&FontIndexMutex);
			bProjectFontIndexBuilt = false;
			NormalizedProjectFontAssetIndex.Reset();
		}

		void AddIndexedFontAssetPath(const FString& NormalizedFamily, const FString& ObjectPath)
		{
			FScopeLock Lock(&FontIndexMutex);
			NormalizedProjectFontAssetIndex.FindOrAdd(NormalizedFamily).AddUnique(ObjectPath);
		}

	private:
		/** Caller must hold FontIndexMutex. */
		void AddIndexedFontAssetName(const FString& AssetName, const FString& ObjectPath)
		{
			const FString Normalized = NormalizeFontLookupKey(AssetName);
			if (!Normalized.IsEmpty() && IsProjectFontAssetPath(ObjectPath))
			{
				NormalizedProjectFontAssetIndex.FindOrAdd(Normalized).AddUnique(ObjectPath);
			}
		}
	};

	FFontResolverCache& GetFontResolverCache()
	{
		static FFontResolverCache Cache;
		return Cache;
	}

	void IndexProjectFontsForFamily(const FString& Family)
	{
		FFontResolverCache& Cache = GetFontResolverCache();
		Cache.BuildProjectFontAssetIndexIfNeeded();
		const FString ImportedName = FPackageName::GetLongPackageAssetName(FFontResolver::MakeImportedFamilyPackageName(Family));
		for (const FString& Path : Cache.GetIndexedFontAssetPathsSnapshot(NormalizeFontLookupKey(ImportedName)))
		{
			Cache.AddIndexedFontAssetPath(NormalizeFontLookupKey(Family), Path);
		}
	}

	bool TryGetIndexedFontAssetPath(const FString& NormalizedFamily, FString& OutPath)
	{
		const TArray<FString> IndexedPaths = GetFontResolverCache().GetIndexedFontAssetPathsSnapshot(NormalizedFamily);
		if (IndexedPaths.Num() != 1)
		{
			return false;
		}

		if (IsLoadableProjectFontPath(IndexedPaths[0]))
		{
			OutPath = IndexedPaths[0];
			return true;
		}
		return false;
	}

	bool TryGetSingleLoadableProjectFontAssetPath(const FString& FontFamily, FString& OutPath)
	{
		const FString LookupKey = NormalizeFontLookupKey(FontFamily);
		if (LookupKey.IsEmpty())
		{
			return false;
		}

		IndexProjectFontsForFamily(FontFamily);
		return TryGetIndexedFontAssetPath(LookupKey, OutPath);
	}

	// Resolve a font family through the cached project-asset index and the engine
	// font fallback, without consulting explicit settings mappings. Both public
	// FindFontAssetPath overloads call this after their explicit-mapping pass.
	FString FindFontAssetPathByFamilyLookup(const FString& FontFamily)
	{
		const FString NormalizedFamily = NormalizeFontLookupKey(FontFamily);
		FFontResolverCache& Cache = GetFontResolverCache();

		FString CachedPath;
		if (Cache.TryGetFontAssetCachePath(NormalizedFamily, CachedPath))
		{
			if (IsUsableResolvedFontPath(CachedPath))
			{
				return CachedPath;
			}
			Cache.RemoveFontAssetCachePath(NormalizedFamily);
		}

		IndexProjectFontsForFamily(FontFamily);
		if (!NormalizedFamily.IsEmpty())
		{
			FString IndexedPath;
			if (TryGetIndexedFontAssetPath(NormalizedFamily, IndexedPath))
			{
				if (IsLoadableProjectFontPath(IndexedPath))
				{
					Cache.StoreFontAssetCachePath(NormalizedFamily, IndexedPath);
					return IndexedPath;
				}

				Cache.RemoveIndexedFontAssetPath(NormalizedFamily);
			}

			FString EngineFontPath;
			if (TryResolveEngineFontAssetPath(NormalizedFamily, EngineFontPath))
			{
				Cache.StoreFontAssetCachePath(NormalizedFamily, EngineFontPath);
				return EngineFontPath;
			}
		}

		Cache.StoreFontAssetCachePath(NormalizedFamily, FString());
		return FString();
	}

}

namespace FontResolverInternal
{
// Single normalization for every font lookup key (family, PostScript name,
// asset name): lowercase, trimmed, spaces/dashes/underscores removed.
FString NormalizeFontLookupKey(const FString& InValue)
{
	FString Key = InValue.ToLower().TrimStartAndEnd();
	Key.ReplaceInline(TEXT(" "), TEXT(""));
	Key.ReplaceInline(TEXT("-"), TEXT(""));
	Key.ReplaceInline(TEXT("_"), TEXT(""));
	return Key;
}

bool IsProjectFontAssetPath(const FString& ObjectPath)
{
	return ObjectPath.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase);
}

bool IsEngineFontAssetPath(const FString& ObjectPath)
{
	return ObjectPath.StartsWith(TEXT("/Engine/EngineFonts/"), ESearchCase::IgnoreCase);
}

EExplicitFontMappingResult TryResolveExplicitFontMapping(const FString& Family, const FString& PostScriptName, FString& OutPath, const FDesign2UMGFontMappings* ExplicitMappings)
{
	const UDesign2UMGSettings* Settings = ExplicitMappings ? nullptr : UDesign2UMGSettings::Get();
	const FDesign2UMGFontMappings* Mappings = ExplicitMappings ? ExplicitMappings : Settings ? &Settings->ExplicitFontMappings : nullptr;
	if (!Mappings)
	{
		return EExplicitFontMappingResult::NoMapping;
	}

	const auto TryResolveMappingKey = [Mappings, &OutPath](const FString& Key)
	{
		const FString TrimmedKey = Key.TrimStartAndEnd();
		if (TrimmedKey.IsEmpty())
		{
			return EExplicitFontMappingResult::NoMapping;
		}
		if (const TSoftObjectPtr<UFont>* FontAsset = Mappings->Find(TrimmedKey))
		{
			return ResolveExplicitFontMappingAsset(*FontAsset, OutPath);
		}
		for (const TPair<FString, TSoftObjectPtr<UFont>>& Pair : *Mappings)
		{
			if (Pair.Key.Equals(TrimmedKey, ESearchCase::IgnoreCase))
			{
				return ResolveExplicitFontMappingAsset(Pair.Value, OutPath);
			}
		}
		return EExplicitFontMappingResult::NoMapping;
	};

	const EExplicitFontMappingResult PostScriptResult = TryResolveMappingKey(PostScriptName);
	if (PostScriptResult != EExplicitFontMappingResult::NoMapping)
	{
		return PostScriptResult;
	}

	return TryResolveMappingKey(Family);
}

bool HasExplicitFontMappingKey(const FString& Key, const FDesign2UMGFontMappings* ExplicitMappings)
{
	const UDesign2UMGSettings* Settings = ExplicitMappings ? nullptr : UDesign2UMGSettings::Get();
	const FDesign2UMGFontMappings* Mappings = ExplicitMappings ? ExplicitMappings : Settings ? &Settings->ExplicitFontMappings : nullptr;
	if (!Mappings)
	{
		return false;
	}

	const FString TrimmedKey = Key.TrimStartAndEnd();
	if (TrimmedKey.IsEmpty())
	{
		return false;
	}

	if (Mappings->Contains(TrimmedKey))
	{
		return true;
	}

	for (const TPair<FString, TSoftObjectPtr<UFont>>& Pair : *Mappings)
	{
		if (Pair.Key.Equals(TrimmedKey, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}
}

FString FFontResolver::MakeFontDeclarationKey(const FString& FontFamily, const FString& PostScriptName,
	const FString& FontWeight, const FString& FontStyle)
{
	return FString::Printf(TEXT("%d:%s%d:%s|%d|%d"), FontFamily.Len(), *FontFamily, PostScriptName.Len(), *PostScriptName,
		ParseWeightValue(FontWeight), FontStyle.Contains(TEXT("italic")) || FontStyle.Contains(TEXT("oblique")));
}

TextFontResolution::FResolvedFontSelection FFontResolver::ResolveFontSelection(
	const FString& FontFamily,
	const FString& FontPostScriptName,
	const FString& FontWeight,
	const FString& FontStyle,
	const FDesign2UMGFontMappings* FontMappings,
	const FDesignPreparedFont* PreparedFont)
{
	TextFontResolution::FResolvedFontSelection Result;
	Result.bRequiresResolvedFontAsset = FFontResolver::HasInvalidProjectFontAsset(FontFamily);

	const bool bExplicitMapping = FFontResolver::HasExplicitFontMapping(FontFamily, FontPostScriptName, FontMappings);
	const FString EffectiveWeight = FontWeight.IsEmpty() ? TEXT("normal") : FontWeight;
	const FString CandidateAssetPath = FFontResolver::FindFontAssetPath(FontFamily, FontPostScriptName, FontMappings);
	// Explicit mappings retain their exact-style contract; only this declaration's prepared document face applies.
	if (!bExplicitMapping && PreparedFont && !PreparedFont->DeclarationKey.IsEmpty()
		&& PreparedFont->DeclarationKey == MakeFontDeclarationKey(FontFamily, FontPostScriptName, FontWeight, FontStyle)
		&& PreparedFont->SourceAssetPath == CandidateAssetPath)
	{
		Result.FontAssetPath = CandidateAssetPath;
		Result.TypefaceName = PreparedFont->TypefaceName.ToString();
		return Result;
	}
	const FString ItalicTypefaceName = (FontStyle.Contains(TEXT("italic")) && !FontWeight.IsEmpty())
		? FFontResolver::FindItalicTypefaceForAssetPath(CandidateAssetPath, EffectiveWeight, FontFamily)
		: FString();
	if (bExplicitMapping)
	{
		if (FontWeight.IsEmpty())
		{
			Result.FontAssetPath = CandidateAssetPath;
			return Result;
		}

		if (!ItalicTypefaceName.IsEmpty())
		{
			Result.FontAssetPath = CandidateAssetPath;
			Result.TypefaceName = ItalicTypefaceName;
			return Result;
		}

		const FString ExactTypefaceName =
			FFontResolver::FindExactTypefaceForAssetPath(CandidateAssetPath, EffectiveWeight);
		if (!ExactTypefaceName.IsEmpty())
		{
			Result.FontAssetPath = CandidateAssetPath;
			Result.TypefaceName = ExactTypefaceName;
		}
		return Result;
	}

	Result.FontAssetPath = CandidateAssetPath;
	if (FontWeight.IsEmpty())
	{
		return Result;
	}
	if (!ItalicTypefaceName.IsEmpty())
	{
		Result.TypefaceName = ItalicTypefaceName;
		return Result;
	}
	Result.TypefaceName = FFontResolver::FindBestTypefaceForAssetPath(Result.FontAssetPath, EffectiveWeight);
	if (Result.TypefaceName.IsEmpty())
	{
		Result.TypefaceName = FFontResolver::FindBestTypefaceForAssetPath(FFontResolver::FindFontAssetPath(FontFamily, FString(), FontMappings), EffectiveWeight);
	}
	if (Result.TypefaceName.IsEmpty())
	{
		Result.TypefaceName = DesignTypeMapper::MapFontWeight(EffectiveWeight).ToString();
	}
	return Result;
}

FString FFontResolver::MakeImportedFamilyPackageName(const FString& FontFamily)
{
	FString Name = FontFamily;
	for (const TCHAR Invalid : FString(INVALID_OBJECTNAME_CHARACTERS)) Name.ReplaceCharInline(Invalid, TEXT('_'));
	const FTCHARToUTF8 Identity(*FontFamily.ToLower().TrimStartAndEnd());
	return TEXT("/Game/Design2UMG/Fonts/") + Name + TEXT("_")
		+ FMD5::HashBytes(reinterpret_cast<const uint8*>(Identity.Get()), Identity.Length());
}

FString FFontResolver::FindFontAssetPath(const FString& FontFamily, const FString& PostScriptName, const FDesign2UMGFontMappings* ExplicitMappings)
{
	FString ExplicitPath;
	const EExplicitFontMappingResult MappingResult = TryResolveExplicitFontMapping(FontFamily, PostScriptName, ExplicitPath, ExplicitMappings);
	if (MappingResult == EExplicitFontMappingResult::Resolved)
	{
		return ExplicitPath;
	}
	if (MappingResult == EExplicitFontMappingResult::Invalid)
	{
		return FString();
	}

	// Covers both a PostScript-only declaration and an all-empty declaration.
	if (FontFamily.TrimStartAndEnd().IsEmpty())
	{
		return FString();
	}

	return FindFontAssetPathByFamilyLookup(FontFamily);
}

FString FFontResolver::FindDefaultFontAssetPath()
{
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	FString DefaultPath;
	if (Settings && TryResolveDefaultFontPath(Settings->DefaultFontAsset, DefaultPath))
	{
		return DefaultPath;
	}
	return FString();
}

bool FFontResolver::HasExplicitFontMapping(const FString& FontFamily, const FString& PostScriptName, const FDesign2UMGFontMappings* ExplicitMappings)
{
	return HasExplicitFontMappingKey(PostScriptName, ExplicitMappings) || HasExplicitFontMappingKey(FontFamily, ExplicitMappings);
}

bool FFontResolver::HasInvalidProjectFontAsset(const FString& FontFamily)
{
	const FString LookupKey = NormalizeFontLookupKey(FontFamily);
	if (LookupKey.IsEmpty())
	{
		return false;
	}

	IndexProjectFontsForFamily(FontFamily);
	const TArray<FString> IndexedPaths = GetFontResolverCache().GetIndexedFontAssetPathsSnapshot(LookupKey);
	return IndexedPaths.Num() == 1 && !IsLoadableProjectFontPath(IndexedPaths[0]);
}

FString FFontResolver::FindFontAssetPath(const FString& FontFamily)
{
	if (FontFamily.TrimStartAndEnd().IsEmpty())
		return FString();

	FString ExplicitPath;
	const EExplicitFontMappingResult MappingResult = TryResolveExplicitFontMapping(FontFamily, FString(), ExplicitPath);
	if (MappingResult == EExplicitFontMappingResult::Resolved)
	{
		return ExplicitPath;
	}
	if (MappingResult == EExplicitFontMappingResult::Invalid)
	{
		return FString();
	}

	return FindFontAssetPathByFamilyLookup(FontFamily);
}

FString FFontResolver::NormalizeIconNameKey(const FString& IconName)
{
	return NormalizeIconNameKeyForResolver(IconName);
}

FString FFontResolver::FindKnownIconGlyph(const FString& FontFamily, const FString& IconName)
{
	if (FontFamily.TrimStartAndEnd().IsEmpty() || IconName.TrimStartAndEnd().IsEmpty())
	{
		return FString();
	}

	const FString NormalizedFamily = NormalizeFontLookupKey(FontFamily);
	const FString NormalizedIcon = NormalizeIconNameKeyForResolver(IconName);
	if (NormalizedIcon.IsEmpty())
	{
		return FString();
	}

	FString Glyph;
	if (GetFontResolverCache().TryGetKnownIconCacheValue(EKnownIconCache::GlyphByIconName, NormalizedFamily, NormalizedIcon, Glyph))
	{
		return Glyph;
	}

	return FString();
}

FString FFontResolver::FindKnownIconNameByGlyph(const FString& FontFamily, const FString& Glyph)
{
	if (FontFamily.TrimStartAndEnd().IsEmpty() || Glyph.TrimStartAndEnd().IsEmpty())
	{
		return FString();
	}

	const FString NormalizedFamily = NormalizeFontLookupKey(FontFamily);
	FString IconName;
	if (GetFontResolverCache().TryGetKnownIconCacheValue(EKnownIconCache::NameByGlyph, NormalizedFamily, Glyph, IconName))
	{
		return IconName;
	}

	return FString();
}

void FFontResolver::ClearCache()
{
	FFontResolverCache& Cache = GetFontResolverCache();
	Cache.ClearResolverCaches();
	Cache.InvalidateProjectFontAssetIndex();
}

void FFontResolver::RegisterFontAssetPath(const FString& FontFamily, const FString& ObjectPath)
{
	const FString NormalizedFamily = NormalizeFontLookupKey(FontFamily);
	if (NormalizedFamily.IsEmpty() || ObjectPath.IsEmpty())
	{
		return;
	}

	if (!IsLoadableProjectFontPath(ObjectPath))
	{
		return;
	}

	FFontResolverCache& Cache = GetFontResolverCache();
	Cache.StoreFontAssetCachePath(NormalizedFamily, ObjectPath);
	Cache.AddIndexedFontAssetPath(NormalizedFamily, ObjectPath);
}

bool FFontResolver::HasRegisteredIconGlyphs()
{
	return GetFontResolverCache().HasAnyRegisteredIconGlyph();
}

void FFontResolver::RegisterKnownIconGlyphs(
	const FString& FontFamily,
	const TMap<FString, FString>& Glyphs,
	const TMap<FString, FString>& IconNames)
{
	const FString NormalizedFamily = NormalizeFontLookupKey(FontFamily);
	if (NormalizedFamily.IsEmpty())
	{
		return;
	}

	GetFontResolverCache().StoreKnownIconCaches(NormalizedFamily, Glyphs, IconNames);
}

int32 FFontResolver::ParseWeightValue(const FString& Weight)
{
	// CSS font-weight scale: 100..900 in steps of 100 (https://drafts.csswg.org/css-fonts-4/#font-weight-numeric-values).
	constexpr int32 MinCssFontWeight = 100;
	constexpr int32 MaxCssFontWeight = 900;

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
	if (Num >= MinCssFontWeight && Num <= MaxCssFontWeight)
		return FMath::Clamp(FMath::RoundToInt(Num / 100.0f) * 100, MinCssFontWeight, MaxCssFontWeight);

	return 400;
}

FString FFontResolver::TypefaceNameForWeightValue(const int32 WeightValue)
{
	switch (WeightValue)
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

FString FFontResolver::TypefaceNameForExactWeight(int32 WeightValue)
{
	return WeightValue % 100 == 0 ? TypefaceNameForWeightValue(WeightValue) : FString::FromInt(WeightValue);
}

FString FFontResolver::FindBestIconTypefaceForAssetPath(const FString& FontAssetPath, const FString& RequestedWeight)
{
	const int32 Weight = RequestedWeight.IsNumeric() ? FCString::Atoi(*RequestedWeight) : ParseWeightValue(RequestedWeight);
	const FString ExactName = TypefaceNameForExactWeight(Weight);
	if (IsProjectFontAssetPath(FontAssetPath) || IsEngineFontAssetPath(FontAssetPath))
	{
		UFont* Font = LoadObject<UFont>(nullptr, *FontAssetPath);
		const FCompositeFont* Composite = Font ? Font->GetCompositeFont() : nullptr;
		if (Composite)
			for (const FTypefaceEntry& Entry : Composite->DefaultTypeface.Fonts)
				if (Entry.Name.ToString().Equals(ExactName, ESearchCase::IgnoreCase)) return Entry.Name.ToString();
	}
	return FindBestTypefaceForAssetPath(FontAssetPath, RequestedWeight);
}

FString FFontResolver::FindBestTypeface(const FString& FontFamily, const FString& RequestedWeight)
{
	const FString AssetPath = FindFontAssetPath(FontFamily);
	if (AssetPath.IsEmpty())
		return FString();

	return FindBestTypefaceForAssetPath(AssetPath, RequestedWeight);
}

FString FFontResolver::FindBestTypefaceForAssetPath(const FString& FontAssetPath, const FString& RequestedWeight)
{
	const FString AssetPath = FontAssetPath.TrimStartAndEnd();
	if (!FPackageName::IsValidObjectPath(AssetPath))
	{
		return FString();
	}

	UFont* Font = LoadObject<UFont>(nullptr, *AssetPath);
	const FCompositeFont* Composite = Font ? Font->GetCompositeFont() : nullptr;
	if (!Composite || Composite->DefaultTypeface.Fonts.Num() == 0)
		return FString();

	int32 TargetWeight = ParseWeightValue(RequestedWeight);
	int32 BestDist = INT_MAX;
	FString BestName;
	bool bBestIsItalic = true;

	for (const FTypefaceEntry& Entry : Composite->DefaultTypeface.Fonts)
	{
		FString EntryName = Entry.Name.ToString();
		int32 EntryWeight = WeightFromTypefaceName(EntryName);
		int32 Dist = FMath::Abs(EntryWeight - TargetWeight);
		const bool bIsItalic = EntryName.Contains(TEXT("Italic"));
		// Italic requests have a dedicated path; ordinary text prefers a roman face.
		if ((bBestIsItalic && !bIsItalic) || (bBestIsItalic == bIsItalic && Dist < BestDist))
		{
			BestDist = Dist;
			BestName = EntryName;
			bBestIsItalic = bIsItalic;
		}
	}

	UE_LOG(LogDesign2UMG, Verbose, TEXT("FontResolver: asset='%s' requested='%s' selected='%s' entries=%d"),
		*AssetPath, *RequestedWeight, *BestName, Composite->DefaultTypeface.Fonts.Num());
	return BestName;
}

FString FFontResolver::FindExactTypefaceForAssetPath(const FString& FontAssetPath, const FString& RequestedWeight)
{
	const FString AssetPath = FontAssetPath.TrimStartAndEnd();
	if (!FPackageName::IsValidObjectPath(AssetPath))
	{
		return FString();
	}

	UFont* Font = LoadObject<UFont>(nullptr, *AssetPath);
	const FCompositeFont* Composite = Font ? Font->GetCompositeFont() : nullptr;
	if (!Composite || Composite->DefaultTypeface.Fonts.Num() == 0)
	{
		return FString();
	}

	const FString ExpectedName = TypefaceNameForWeightValue(ParseWeightValue(RequestedWeight));
	for (const FTypefaceEntry& Entry : Composite->DefaultTypeface.Fonts)
	{
		if (Entry.Name.ToString().Equals(ExpectedName, ESearchCase::IgnoreCase))
		{
			return Entry.Name.ToString();
		}
	}

	return FString();
}

FString FFontResolver::FindItalicTypefaceForAssetPath(const FString& FontAssetPath, const FString& RequestedWeight, const FString& FontFamily)
{
	const FString AssetPath = FontAssetPath.TrimStartAndEnd();
	if (!FPackageName::IsValidObjectPath(AssetPath))
	{
		return FString();
	}

	UFont* Font = LoadObject<UFont>(nullptr, *AssetPath);
	const FCompositeFont* Composite = Font ? Font->GetCompositeFont() : nullptr;
	if (!Composite || Composite->DefaultTypeface.Fonts.Num() == 0)
	{
		return FString();
	}

	const FString DesiredName = TypefaceNameForWeightValue(ParseWeightValue(RequestedWeight)) + TEXT(" Italic");
	for (const FTypefaceEntry& Entry : Composite->DefaultTypeface.Fonts)
	{
		if (Entry.Name.ToString().Equals(DesiredName, ESearchCase::IgnoreCase))
		{
			return Entry.Name.ToString();
		}
	}

	if (GetFontResolverCache().MarkMissingItalicTypefaceWarning(AssetPath + TEXT("|") + DesiredName))
	{
		const FString FamilyLabel = FontFamily.TrimStartAndEnd().IsEmpty() ? AssetPath : FontFamily;
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("Font '%s' has no italic typeface '%s'; falling back to roman"),
			*FamilyLabel, *DesiredName);
	}
	return FString();
}

TArray<FString> FFontResolver::FindMissingFonts(const FDesignDocument& Document)
{
	TSet<FString> UsedFamilies;

	// Collect from node tree
	CollectFontFamilies(Document.Children, UsedFamilies);
	CollectFontFamilies(Document.SourceDocument().ImportedNodes, UsedFamilies);

	if (UsedFamilies.Num() == 0)
		return {};

	TArray<FString> Missing;
	TArray<FString> InvalidProjectFonts;
	for (const FString& Family : UsedFamilies)
	{
		FString ProjectFontPath;
		if (!TryGetSingleLoadableProjectFontAssetPath(Family, ProjectFontPath))
		{
			Missing.Add(Family);
			FFontResolverCache& Cache = GetFontResolverCache();
			Cache.BuildProjectFontAssetIndexIfNeeded();
			const FString LookupKey = NormalizeFontLookupKey(Family);
			if (!LookupKey.IsEmpty())
			{
				for (const FString& IndexedPath : Cache.GetIndexedFontAssetPathsSnapshot(LookupKey))
				{
					UFont* Font = IsProjectFontAssetPath(IndexedPath) ? LoadObject<UFont>(nullptr, *IndexedPath) : nullptr;
					if (Font && !FontHasUsableDefaultTypefaceEntry(Font))
					{
						InvalidProjectFonts.AddUnique(FString::Printf(TEXT("%s (%s)"), *Family, *IndexedPath));
					}
				}
			}
		}
	}

	if (Missing.Num() > 0)
	{
		FString List = FString::Join(Missing, TEXT(", "));
		UE_LOG(LogDesign2UMG, Warning, TEXT("Missing fonts: %s"), *List);
		FDesign2UMGNotification::Warning(
			FText::Format(LOCTEXT("MissingFonts", "Design2UMG: {0} font(s) not found in project: {1}"),
				FText::AsNumber(Missing.Num()), FText::FromString(List)));
	}
	if (InvalidProjectFonts.Num() > 0)
	{
		const FString List = FString::Join(InvalidProjectFonts, TEXT(", "));
		UE_LOG(LogDesign2UMG, Warning, TEXT("Invalid project font assets: %s"), *List);
		FDesign2UMGNotification::Warning(
			FText::Format(LOCTEXT("InvalidProjectFonts", "Design2UMG: {0} project font asset(s) are invalid. Reimport them as UFont assets with a valid default typeface: {1}"),
				FText::AsNumber(InvalidProjectFonts.Num()), FText::FromString(List)));
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
