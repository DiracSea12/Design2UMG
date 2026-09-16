// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FontResolver.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "Design2UMGSettings.h"
#include "DesignSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeLock.h"

#define LOCTEXT_NAMESPACE "FontResolver"

namespace
{
	enum class EKnownIconCache : uint8
	{
		GlyphByIconName,
		NameByGlyph
	};

	enum class EExplicitFontMappingResult : uint8
	{
		NoMapping,
		Resolved,
		Invalid
	};

	void CollectFontFamilies(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutFamilies);

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

	bool IsAutomationImportedFontPath(const FString& ObjectPath)
	{
		return ObjectPath.Contains(TEXT("/Design2UMG/Automation/"), ESearchCase::IgnoreCase)
			&& ObjectPath.Contains(TEXT("/ImportedFonts/"), ESearchCase::IgnoreCase);
	}

	bool IsProjectFontAssetPath(const FString& ObjectPath)
	{
		return ObjectPath.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase)
			&& !IsAutomationImportedFontPath(ObjectPath);
	}

	bool IsEngineFontAssetPath(const FString& ObjectPath)
	{
		return ObjectPath.StartsWith(TEXT("/Engine/EngineFonts/"), ESearchCase::IgnoreCase);
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
			if (Asset.AssetClassPath != UFont::StaticClass()->GetClassPathName())
			{
				continue;
			}

			if (NormalizeFontLookupKey(Asset.AssetName.ToString()) != NormalizedFamily)
			{
				continue;
			}

			const FString ObjectPath = Asset.GetObjectPathString();
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

	EExplicitFontMappingResult TryResolveExplicitFontMapping(const FString& Family, const FString& PostScriptName, FString& OutPath, const FDesign2UMGFontMappings* ExplicitMappings = nullptr)
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

	bool HasExplicitFontMappingKey(const FString& Key, const FDesign2UMGFontMappings* ExplicitMappings = nullptr)
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
			AR.GetAssetsByClass(UFont::StaticClass()->GetClassPathName(), FontAssets, true);
			for (const FAssetData& Asset : FontAssets)
			{
				if (!Asset.PackagePath.ToString().StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				AddIndexedFontAssetName(Asset.AssetName.ToString(), Asset.GetObjectPathString());
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

	FString NormalizeFontStyleToken(const FString& Style)
	{
		const FString Normalized = Style.ToLower().TrimStartAndEnd();
		return (Normalized == TEXT("italic") || Normalized == TEXT("oblique")) ? Normalized : TEXT("normal");
	}

	FString BuildFontDiagnosticDeclarationKey(
		const FString& Family,
		const FString& PostScriptName,
		const FString& Weight,
		const FString& Style)
	{
		const FString FamilyKey = Family.TrimStartAndEnd().IsEmpty()
			? TEXT("unknown-family")
			: NormalizeFontLookupKey(Family);
		const FString PostScriptKey = PostScriptName.TrimStartAndEnd().IsEmpty()
			? TEXT("none")
			: NormalizeFontLookupKey(PostScriptName);
		const FString WeightKey = Weight.IsEmpty()
			? TEXT("none")
			: FString::FromInt(FFontResolver::ParseWeightValue(Weight));
		return FString::Printf(
			TEXT("%s|%s|%s|%s"),
			*FamilyKey,
			*PostScriptKey,
			*WeightKey,
			*NormalizeFontStyleToken(Style));
	}

	FString ResolveEngineDefaultFontAssetPath()
	{
		FString DefaultFontPackageName = UWidget::GetDefaultFontName();
		if (DefaultFontPackageName.IsEmpty())
		{
			return FString();
		}

		DefaultFontPackageName = FPackageName::ObjectPathToPackageName(DefaultFontPackageName);
		const FString DefaultFontAssetName = FPackageName::GetLongPackageAssetName(DefaultFontPackageName);
		if (DefaultFontAssetName.IsEmpty())
		{
			return FString();
		}

		return DefaultFontPackageName + TEXT(".") + DefaultFontAssetName;
	}

	int32 CountProjectFontAssetCandidates(const FString& Family)
	{
		const FString NormalizedFamily = NormalizeFontLookupKey(Family);
		if (NormalizedFamily.IsEmpty() || NormalizedFamily == TEXT("unknown-family"))
		{
			return 0;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> FontAssets;
		AssetRegistryModule.Get().GetAssetsByClass(UFont::StaticClass()->GetClassPathName(), FontAssets, true);

		TSet<FString> MatchingObjectPaths;
		const FString ImportedName = NormalizeFontLookupKey(FPackageName::GetLongPackageAssetName(FFontResolver::MakeImportedFamilyPackageName(Family)));
		for (const FAssetData& Asset : FontAssets)
		{
			const FString ObjectPath = Asset.GetObjectPathString();
			if (IsProjectFontAssetPath(ObjectPath)
				&& (NormalizeFontLookupKey(Asset.AssetName.ToString()) == NormalizedFamily
					|| NormalizeFontLookupKey(Asset.AssetName.ToString()) == ImportedName))
			{
				MatchingObjectPaths.Add(ObjectPath);
			}
		}
		return MatchingObjectPaths.Num();
	}

	void AddFontDiagnosticIssue(FFontResolutionDiagnostic& Diagnostic, const TCHAR* IssueCode)
	{
		Diagnostic.IssueCodes.AddUnique(FString(IssueCode));
	}

	void SetUnresolvedDiagnostic(
		FFontResolutionDiagnostic& Diagnostic,
		const FString& Token,
		const FString& FallbackState,
		const FString& MatchReason)
	{
		Diagnostic.ResolutionSourceKind = TEXT("unresolved");
		Diagnostic.FallbackState = FallbackState;
		Diagnostic.MatchReason = MatchReason;
		Diagnostic.UnresolvedToken = Token;
	}

	// Stage helpers for FFontResolver::ResolveFontDiagnostic: each one decides
	// a single diagnostic sub-domain and returns its decision; the public
	// function assembles those decisions into FFontResolutionDiagnostic.

	enum class EFontDeclarationResolutionKind : uint8
	{
		UnsupportedInput,
		InvalidExplicitMapping,
		EngineDefaultPath,
		RejectedTypeface,
		ExplicitMappingMatch,
		ProjectAssetMatch,
		AmbiguousProjectAssets,
		UnresolvedDeclaration
	};

	struct FFontDeclarationResolution
	{
		EFontDeclarationResolutionKind Kind = EFontDeclarationResolutionKind::UnresolvedDeclaration;
		FString ResolvedAssetPath;
		FString EngineFallbackAssetPath;
		FString ResolvedTypeface;
		bool bTypefaceSubstituted = false;
		bool bUseMappedAssetDefaultTypeface = false;
		bool bAllowFallbackAsset = true;
	};

	struct FExplicitMappedTypefaceDecision
	{
		bool bRejected = false;
		FString Typeface;
	};

	struct FProjectAssetTypefaceDecision
	{
		FString Typeface;
		bool bSubstituted = false;
	};

	struct FFontFallbackAssetDecision
	{
		FString FallbackAssetPath;
		FString FallbackToken;
		FString ResolutionSourceKind;
		FString FallbackState;
		FString MatchReason;
		const TCHAR* IssueCode = nullptr;
	};

	// Decide whether the resolved asset's default typeface is used as-is:
	// no weight declared and the asset lives under /Game.
	bool UsesMappedAssetDefaultTypeface(const FString& Weight, const FString& ResolvedAssetPath)
	{
		return Weight.IsEmpty() && IsProjectFontAssetPath(ResolvedAssetPath);
	}

	// Decide whether an explicitly mapped project asset carries the exact
	// typeface for the declared weight. An undeclared weight keeps the mapped
	// asset's default typeface and can never be rejected.
	FExplicitMappedTypefaceDecision DetermineExplicitMappedTypeface(
		const FString& ResolvedAssetPath,
		const FString& Weight,
		const bool bUseMappedAssetDefaultTypeface)
	{
		FExplicitMappedTypefaceDecision Decision;
		Decision.Typeface = bUseMappedAssetDefaultTypeface
			? FString()
			: FFontResolver::FindExactTypefaceForAssetPath(ResolvedAssetPath, Weight);
		Decision.bRejected = !bUseMappedAssetDefaultTypeface && Decision.Typeface.IsEmpty();
		return Decision;
	}

	// Decide the best-match typeface inside a same-name project asset and
	// whether it substitutes the declared weight's nominal typeface.
	FProjectAssetTypefaceDecision DetermineProjectAssetTypeface(
		const FString& ResolvedAssetPath,
		const FString& Weight,
		const bool bUseMappedAssetDefaultTypeface)
	{
		FProjectAssetTypefaceDecision Decision;
		Decision.Typeface = bUseMappedAssetDefaultTypeface
			? FString()
			: FFontResolver::FindBestTypefaceForAssetPath(ResolvedAssetPath, Weight);
		Decision.bSubstituted = !bUseMappedAssetDefaultTypeface
			&& Decision.Typeface != FFontResolver::TypefaceNameForWeightValue(FFontResolver::ParseWeightValue(Weight));
		return Decision;
	}

	// Decide how a family with no resolvable asset path failed: multiple
	// same-name project assets are ambiguous, everything else (including an
	// empty family) stays a plain unresolved declaration.
	EFontDeclarationResolutionKind ClassifyUnresolvedFontFamily(const FString& FontFamily)
	{
		if (FontFamily.TrimStartAndEnd().IsEmpty())
		{
			return EFontDeclarationResolutionKind::UnresolvedDeclaration;
		}
		if (CountProjectFontAssetCandidates(FontFamily) > 1)
		{
			return EFontDeclarationResolutionKind::AmbiguousProjectAssets;
		}
		return EFontDeclarationResolutionKind::UnresolvedDeclaration;
	}

	// Decide the primary resolution outcome for one declaration, before any
	// fallback asset is considered.
	FFontDeclarationResolution DetermineFontDeclarationResolution(
		const FString& FontFamily,
		const FString& PostScriptName,
		const FString& Weight,
		const bool bUnsupportedInput)
	{
		FFontDeclarationResolution Resolution;

		if (bUnsupportedInput)
		{
			Resolution.Kind = EFontDeclarationResolutionKind::UnsupportedInput;
			Resolution.bAllowFallbackAsset = false;
			return Resolution;
		}

		FString ExplicitPath;
		const EExplicitFontMappingResult ExplicitMappingResult = TryResolveExplicitFontMapping(FontFamily, PostScriptName, ExplicitPath);
		if (ExplicitMappingResult == EExplicitFontMappingResult::Invalid)
		{
			Resolution.Kind = EFontDeclarationResolutionKind::InvalidExplicitMapping;
			Resolution.bAllowFallbackAsset = false;
			return Resolution;
		}

		const FString ResolvedAssetPath = FFontResolver::FindFontAssetPath(FontFamily, PostScriptName);
		if (ResolvedAssetPath.IsEmpty())
		{
			Resolution.Kind = ClassifyUnresolvedFontFamily(FontFamily);
			Resolution.bAllowFallbackAsset =
				Resolution.Kind != EFontDeclarationResolutionKind::AmbiguousProjectAssets;
			return Resolution;
		}

		Resolution.bUseMappedAssetDefaultTypeface = UsesMappedAssetDefaultTypeface(Weight, ResolvedAssetPath);
		if (IsEngineFontAssetPath(ResolvedAssetPath))
		{
			Resolution.Kind = EFontDeclarationResolutionKind::EngineDefaultPath;
			Resolution.EngineFallbackAssetPath = ResolvedAssetPath;
			Resolution.bAllowFallbackAsset = false;
			return Resolution;
		}

		if (ExplicitMappingResult == EExplicitFontMappingResult::Resolved)
		{
			const FExplicitMappedTypefaceDecision TypefaceDecision =
				DetermineExplicitMappedTypeface(ResolvedAssetPath, Weight, Resolution.bUseMappedAssetDefaultTypeface);
			Resolution.Kind = TypefaceDecision.bRejected
				? EFontDeclarationResolutionKind::RejectedTypeface
				: EFontDeclarationResolutionKind::ExplicitMappingMatch;
			Resolution.ResolvedAssetPath = TypefaceDecision.bRejected ? FString() : ResolvedAssetPath;
			Resolution.ResolvedTypeface = TypefaceDecision.Typeface;
			if (TypefaceDecision.bRejected)
			{
				Resolution.bAllowFallbackAsset = false;
			}
			return Resolution;
		}

		const FProjectAssetTypefaceDecision TypefaceDecision =
			DetermineProjectAssetTypeface(ResolvedAssetPath, Weight, Resolution.bUseMappedAssetDefaultTypeface);
		Resolution.Kind = EFontDeclarationResolutionKind::ProjectAssetMatch;
		Resolution.ResolvedAssetPath = ResolvedAssetPath;
		Resolution.ResolvedTypeface = TypefaceDecision.Typeface;
		Resolution.bTypefaceSubstituted = TypefaceDecision.bSubstituted;
		return Resolution;
	}

	// Decide which fallback asset covers an unresolved declaration: the
	// project default font first, the engine default font for the rest.
	FFontFallbackAssetDecision SelectFallbackFontAsset()
	{
		FFontFallbackAssetDecision Decision;

		FString FallbackPath = FFontResolver::FindDefaultFontAssetPath();
		if (!FallbackPath.IsEmpty())
		{
			Decision.FallbackAssetPath = FallbackPath;
			Decision.ResolutionSourceKind = TEXT("default-font-asset");
			Decision.FallbackState = TEXT("default-font-asset");
			Decision.MatchReason = TEXT("default-font-asset-fallback");
			Decision.IssueCode = TEXT("default-font-asset-used");
			return Decision;
		}

		FallbackPath = ResolveEngineDefaultFontAssetPath();
		if (FallbackPath.IsEmpty())
		{
			Decision.FallbackToken = TEXT("engine-default");
		}
		else
		{
			Decision.FallbackAssetPath = FallbackPath;
		}
		Decision.ResolutionSourceKind = TEXT("engine-default");
		Decision.FallbackState = TEXT("engine-default");
		Decision.MatchReason = TEXT("engine-default-fallback");
		Decision.IssueCode = TEXT("engine-default-used");
		return Decision;
	}

	// Decide the diagnostic severity for a primary resolution kind.
	const TCHAR* FontDiagnosticSeverityForKind(const EFontDeclarationResolutionKind Kind)
	{
		switch (Kind)
		{
		case EFontDeclarationResolutionKind::UnsupportedInput:
		case EFontDeclarationResolutionKind::InvalidExplicitMapping:
		case EFontDeclarationResolutionKind::RejectedTypeface:
			return TEXT("error");
		case EFontDeclarationResolutionKind::ExplicitMappingMatch:
		case EFontDeclarationResolutionKind::ProjectAssetMatch:
			return TEXT("info");
		default:
			return TEXT("warning");
		}
	}

	// Decide the match reason for a resolved explicit mapping, keyed on which
	// side of the declaration carries the settings mapping.
	const TCHAR* ExplicitMappingMatchReason(const FString& PostScriptName)
	{
		return HasExplicitFontMappingKey(PostScriptName)
			? TEXT("explicit-postscript-mapping")
			: TEXT("explicit-family-mapping");
	}

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

FFontResolutionDiagnostic FFontResolver::ResolveFontDiagnostic(
	const FString& FontFamily,
	const FString& PostScriptName,
	const FString& Weight,
	const FString& Style,
	const bool bUnsupportedInput,
	const bool bFontUrlIgnored)
{
	FFontResolutionDiagnostic Diagnostic;
	Diagnostic.DeclarationKey = BuildFontDiagnosticDeclarationKey(FontFamily, PostScriptName, Weight, Style);
	Diagnostic.DeclaredFamily = FontFamily.TrimStartAndEnd().IsEmpty() ? TEXT("unknown-family") : FontFamily;
	Diagnostic.DeclaredPostScriptName = PostScriptName;
	Diagnostic.DeclaredWeight = Weight;
	Diagnostic.DeclaredStyle = Style;
	Diagnostic.ResolutionState = TEXT("default-font");
	Diagnostic.DeclarationSourceKind = TEXT("source-declaration");
	Diagnostic.ResolutionSourceKind = TEXT("unresolved");
	Diagnostic.FallbackState = TEXT("unresolved");
	Diagnostic.MatchReason = TEXT("unresolved-declaration");
	Diagnostic.Severity = TEXT("warning");

	if (bFontUrlIgnored)
	{
		AddFontDiagnosticIssue(Diagnostic, TEXT("font-url-ignored"));
	}

	const FFontDeclarationResolution Resolution = DetermineFontDeclarationResolution(
		FontFamily, PostScriptName, Weight, bUnsupportedInput);
	Diagnostic.ResolvedAssetPath = Resolution.ResolvedAssetPath;
	Diagnostic.ResolvedTypeface = Resolution.ResolvedTypeface;
	Diagnostic.Severity = FontDiagnosticSeverityForKind(Resolution.Kind);

	switch (Resolution.Kind)
	{
	case EFontDeclarationResolutionKind::UnsupportedInput:
		Diagnostic.ResolutionState = TEXT("unsupported-input");
		SetUnresolvedDiagnostic(Diagnostic, TEXT("unsupported-input"), TEXT("unsupported-input"), TEXT("declaration-only"));
		AddFontDiagnosticIssue(Diagnostic, TEXT("unsupported-font-file-input"));
		break;

	case EFontDeclarationResolutionKind::InvalidExplicitMapping:
		Diagnostic.ResolutionState = TEXT("rejected");
		SetUnresolvedDiagnostic(Diagnostic, TEXT("unresolved-declaration"), TEXT("unresolved"), TEXT("unresolved-declaration"));
		AddFontDiagnosticIssue(Diagnostic, TEXT("unresolved-declaration"));
		break;

	case EFontDeclarationResolutionKind::EngineDefaultPath:
		Diagnostic.FallbackAssetPath = Resolution.EngineFallbackAssetPath;
		Diagnostic.ResolutionState = TEXT("default-font");
		Diagnostic.ResolutionSourceKind = TEXT("engine-default");
		Diagnostic.FallbackState = TEXT("engine-default");
		Diagnostic.MatchReason = TEXT("engine-default-fallback");
		AddFontDiagnosticIssue(Diagnostic, TEXT("engine-default-used"));
		break;

	case EFontDeclarationResolutionKind::RejectedTypeface:
		Diagnostic.ResolutionState = TEXT("rejected");
		SetUnresolvedDiagnostic(Diagnostic, TEXT("unresolved-declaration"), TEXT("unresolved"), TEXT("unresolved-declaration"));
		AddFontDiagnosticIssue(Diagnostic, TEXT("rejected-typeface"));
		break;

	case EFontDeclarationResolutionKind::ExplicitMappingMatch:
		Diagnostic.ResolutionState = TEXT("exact");
		Diagnostic.ResolutionSourceKind = TEXT("explicit-project-mapping");
		Diagnostic.FallbackState = TEXT("exact");
		Diagnostic.MatchReason = ExplicitMappingMatchReason(PostScriptName);
		break;

	case EFontDeclarationResolutionKind::ProjectAssetMatch:
		Diagnostic.ResolutionState = TEXT("exact");
		Diagnostic.ResolutionSourceKind = TEXT("project-asset");
		Diagnostic.FallbackState = TEXT("exact");
		Diagnostic.MatchReason = TEXT("project-family-match");
		if (Resolution.bTypefaceSubstituted)
		{
			Diagnostic.FallbackState = TEXT("typeface-substituted");
			AddFontDiagnosticIssue(Diagnostic, TEXT("typeface-substituted"));
		}
		break;

	case EFontDeclarationResolutionKind::AmbiguousProjectAssets:
		Diagnostic.ResolutionState = TEXT("ambiguous");
		Diagnostic.ResolutionSourceKind = TEXT("project-asset");
		Diagnostic.FallbackState = TEXT("family-substituted");
		Diagnostic.MatchReason = TEXT("project-family-match");
		AddFontDiagnosticIssue(Diagnostic, TEXT("ambiguous-font-match"));
		break;

	case EFontDeclarationResolutionKind::UnresolvedDeclaration:
		SetUnresolvedDiagnostic(Diagnostic, TEXT("unresolved-declaration"), TEXT("unresolved"), TEXT("unresolved-declaration"));
		AddFontDiagnosticIssue(Diagnostic, TEXT("unresolved-declaration"));
		break;
	}

	if (Diagnostic.ResolvedAssetPath.IsEmpty() && Resolution.bAllowFallbackAsset)
	{
		const FFontFallbackAssetDecision Fallback = SelectFallbackFontAsset();
		Diagnostic.UnresolvedToken.Empty();
		Diagnostic.FallbackAssetPath = Fallback.FallbackAssetPath;
		Diagnostic.FallbackToken = Fallback.FallbackToken;
		Diagnostic.ResolutionSourceKind = Fallback.ResolutionSourceKind;
		Diagnostic.FallbackState = Fallback.FallbackState;
		Diagnostic.MatchReason = Fallback.MatchReason;
		AddFontDiagnosticIssue(Diagnostic, Fallback.IssueCode);
	}

	if (Diagnostic.ResolvedTypeface.IsEmpty()
		&& Diagnostic.ResolutionSourceKind != TEXT("unresolved")
		&& !Resolution.bUseMappedAssetDefaultTypeface)
	{
		Diagnostic.ResolvedTypeface = TEXT("Regular");
	}

	return Diagnostic;
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
	if (!IsProjectFontAssetPath(AssetPath) && !IsEngineFontAssetPath(AssetPath))
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

	UE_LOG(LogDesign2UMG, Verbose, TEXT("FontResolver: asset='%s' requested='%s' selected='%s' entries=%d"),
		*AssetPath, *RequestedWeight, *BestName, Composite->DefaultTypeface.Fonts.Num());
	return BestName;
}

FString FFontResolver::FindExactTypefaceForAssetPath(const FString& FontAssetPath, const FString& RequestedWeight)
{
	const FString AssetPath = FontAssetPath.TrimStartAndEnd();
	if (!IsProjectFontAssetPath(AssetPath) && !IsEngineFontAssetPath(AssetPath))
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
	if (!IsProjectFontAssetPath(AssetPath) && !IsEngineFontAssetPath(AssetPath))
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



