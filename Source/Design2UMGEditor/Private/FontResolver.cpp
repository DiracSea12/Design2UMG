// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FontResolver.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "Design2UMGSettings.h"
#include "DesignSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "FontResolver"

TMap<FString, FString> FFontResolver::FontAssetCache;

namespace
{
	TMap<FString, FString> GNormalizedProjectFontAssetIndex;
	bool bProjectFontIndexBuilt = false;
	FCriticalSection GFontIndexMutex;

	FString NormalizeFontFamilyKeyForResolver(const FString& InFamily)
	{
		FString Key = InFamily.ToLower().TrimStartAndEnd();
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		Key.ReplaceInline(TEXT("-"), TEXT(""));
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		return Key;
	}

	void IndexFontAssetName(const FString& AssetName, const FString& ObjectPath)
	{
		const FString Normalized = NormalizeFontFamilyKeyForResolver(AssetName);
		if (!Normalized.IsEmpty())
		{
			GNormalizedProjectFontAssetIndex.FindOrAdd(Normalized, ObjectPath);
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

	const TMap<FString, FString>& GetKnownWebFontUrls()
	{
		static const TMap<FString, FString> KnownUrls = {
			{TEXT("inter"), TEXT("https://github.com/google/fonts/raw/main/ofl/inter/Inter%5Bopsz,wght%5D.ttf")},
			{TEXT("rubik"), TEXT("https://github.com/google/fonts/raw/main/ofl/rubik/Rubik%5Bwght%5D.ttf")},
			{TEXT("funneldisplay"), TEXT("https://github.com/google/fonts/raw/main/ofl/funneldisplay/FunnelDisplay%5Bwght%5D.ttf")},
			{TEXT("oxygen"), TEXT("https://github.com/google/fonts/raw/main/ofl/oxygen/Oxygen-Regular.ttf")},
			{TEXT("geist"), TEXT("https://github.com/google/fonts/raw/main/ofl/geist/Geist%5Bwght%5D.ttf")},
			{TEXT("jetbrainsmono"), TEXT("https://github.com/google/fonts/raw/main/ofl/jetbrainsmono/JetBrainsMono%5Bwght%5D.ttf")},
			{TEXT("materialsymbolssharp"), TEXT("https://github.com/google/material-design-icons/raw/master/variablefont/MaterialSymbolsSharp%5BFILL,GRAD,opsz,wght%5D.ttf")},
			{TEXT("pingfangsc"), TEXT("https://github.com/google/fonts/raw/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf")},
			// Buenos Aires is not publicly available in Google Fonts; closest fallback for layout parity.
			{TEXT("buenosaires"), TEXT("https://github.com/google/fonts/raw/main/ofl/inter/Inter%5Bopsz,wght%5D.ttf")},
		};
		return KnownUrls;
	}

	const TSet<FString>& GetKnownCssFontFamilies()
	{
		static const TSet<FString> KnownCssFamilies = {
			TEXT("inter"),
			TEXT("rubik"),
			TEXT("funneldisplay"),
			TEXT("geist"),
			TEXT("jetbrainsmono"),
			TEXT("oxygen"),
		};
		return KnownCssFamilies;
	}

	const TArray<int32>& GetKnownCssFontWeights()
	{
		static const TArray<int32> KnownCssWeights = {
			400, 500, 600, 700
		};
		return KnownCssWeights;
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

	FString EngineFallbackPath;
	if (TryResolveEngineFontFallback(FontFamily, EngineFallbackPath))
	{
		FontAssetCache.FindOrAdd(NormalizedFamily) = EngineFallbackPath;
		return EngineFallbackPath;
	}

		const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
		const bool bAllowKnownFontAutoImport = !Settings
			|| Settings->bAutoImportKnownWebFonts
			|| Settings->bAutoImportFonts;
		static TSet<FString> AttemptedKnownWebFonts;
		if (bAllowKnownFontAutoImport && !AttemptedKnownWebFonts.Contains(NormalizedFamily))
		{
			AttemptedKnownWebFonts.Add(NormalizedFamily);
			if (const FString* KnownUrl = GetKnownWebFontUrls().Find(NormalizedFamily))
			{
			UE_LOG(LogDesign2UMG, Log, TEXT("FontResolver: auto-import known web font '%s'"), *FontFamily);
				const FString ImportedPath = ResolveFont(FontFamily, *KnownUrl);
				if (!ImportedPath.IsEmpty())
				{
					FontAssetCache.FindOrAdd(NormalizedFamily) = ImportedPath;
					IndexFontAssetName(FontFamily, ImportedPath);
					return ImportedPath;
				}
			}
		}

		FontAssetCache.FindOrAdd(NormalizedFamily) = FString();
		return FString();
	}

void FFontResolver::ClearCache()
{
	FontAssetCache.Empty();
	InvalidateProjectFontAssetIndex();
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

bool FFontResolver::QueryKnownWebFontTypefaces(const FString& FontFamily, TMap<FString, FString>& OutTypefaceUrls)
{
	OutTypefaceUrls.Reset();

	const FString NormalizedFamily = NormalizeFontFamilyKeyForResolver(FontFamily);
	if (!GetKnownCssFontFamilies().Contains(NormalizedFamily))
	{
		return false;
	}

	TArray<FString> WeightValues;
	for (const int32 Weight : GetKnownCssFontWeights())
	{
		WeightValues.Add(FString::FromInt(Weight));
	}

	FString EncodedFamily = FontFamily.TrimStartAndEnd();
	EncodedFamily.ReplaceInline(TEXT(" "), TEXT("+"));
	const FString CssUrl = FString::Printf(
		TEXT("https://fonts.googleapis.com/css2?family=%s:wght@%s&display=swap"),
		*EncodedFamily,
		*FString::Join(WeightValues, TEXT(";")));

	const FString TempCssPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (NormalizedFamily + TEXT("_weights.css"));
	if (!DownloadToFile(CssUrl, TempCssPath))
	{
		return false;
	}

	FString CssContent;
	const bool bLoaded = FFileHelper::LoadFileToString(CssContent, *TempCssPath);
	IFileManager::Get().Delete(*TempCssPath);
	if (!bLoaded || CssContent.IsEmpty())
	{
		return false;
	}

	TArray<FString> Blocks;
	CssContent.ParseIntoArray(Blocks, TEXT("@font-face"), true);
	for (const FString& Block : Blocks)
	{
		FString WeightSection;
		if (!Block.Split(TEXT("font-weight:"), nullptr, &WeightSection))
		{
			continue;
		}

		const int32 WeightValue = FCString::Atoi(*WeightSection.TrimStartAndEnd());
		if (WeightValue <= 0)
		{
			continue;
		}

		FString SrcSection;
		if (!Block.Split(TEXT("src:"), nullptr, &SrcSection))
		{
			continue;
		}

		const int32 UrlStartMarker = SrcSection.Find(TEXT("url("), ESearchCase::IgnoreCase);
		if (UrlStartMarker == INDEX_NONE)
		{
			continue;
		}

		const int32 UrlStart = UrlStartMarker + 4;
		const int32 UrlEnd = SrcSection.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, UrlStart);
		if (UrlEnd == INDEX_NONE)
		{
			continue;
		}

		FString TypefaceUrl = SrcSection.Mid(UrlStart, UrlEnd - UrlStart).TrimStartAndEnd();
		TypefaceUrl.RemoveFromStart(TEXT("'"));
		TypefaceUrl.RemoveFromStart(TEXT("\""));
		TypefaceUrl.RemoveFromEnd(TEXT("'"));
		TypefaceUrl.RemoveFromEnd(TEXT("\""));
		if (TypefaceUrl.IsEmpty())
		{
			continue;
		}

		OutTypefaceUrls.FindOrAdd(WeightNameFromNumeric(WeightValue)) = TypefaceUrl;
	}

	return OutTypefaceUrls.Num() > 0;
}

bool FFontResolver::EnsureKnownWebFontTypefaces(const FString& FontFamily, UFont* Font, const FString& FontObjectPath)
{
	if (!Font)
	{
		return false;
	}

	const FCompositeFont* ExistingComposite = Font->GetCompositeFont();
	if (ExistingComposite && ExistingComposite->DefaultTypeface.Fonts.Num() > 0)
	{
		TSet<FString> ExistingTypefaces;
		for (const FTypefaceEntry& Entry : ExistingComposite->DefaultTypeface.Fonts)
		{
			ExistingTypefaces.Add(Entry.Name.ToString());
		}

		bool bHasAllKnownWeights = true;
		for (const int32 Weight : GetKnownCssFontWeights())
		{
			if (!ExistingTypefaces.Contains(WeightNameFromNumeric(Weight)))
			{
				bHasAllKnownWeights = false;
				break;
			}
		}

		if (bHasAllKnownWeights)
		{
			return false;
		}
	}

	const FString FontPackageName = FPackageName::ObjectPathToPackageName(FontObjectPath);
	const FString FontImportPath = FPaths::GetPath(FontPackageName);
	if (FontImportPath.IsEmpty())
	{
		return false;
	}

	const FString SafeName = Font->GetName();
	TMap<FString, FString> TypefaceUrls;
	if (!QueryKnownWebFontTypefaces(FontFamily, TypefaceUrls))
	{
		return false;
	}

	FCompositeFont& Composite = Font->GetMutableInternalCompositeFont();
	TMap<FString, int32> ExistingEntryIndices;
	for (int32 Index = 0; Index < Composite.DefaultTypeface.Fonts.Num(); ++Index)
	{
		ExistingEntryIndices.Add(Composite.DefaultTypeface.Fonts[Index].Name.ToString(), Index);
	}

	bool bModifiedFont = false;

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

	for (const TPair<FString, FString>& TypefacePair : TypefaceUrls)
	{
		const FString& TypefaceName = TypefacePair.Key;
		const FString& TypefaceUrl = TypefacePair.Value;
		if (TypefaceName.IsEmpty() || TypefaceUrl.IsEmpty())
		{
			continue;
		}

		const FString FaceAssetName = SafeName + TEXT("_") + TypefaceName + TEXT("_Face");
		const FString FacePackageName = FontImportPath / FaceAssetName;
		const FString FaceObjectPath = FacePackageName + TEXT(".") + FaceAssetName;

		UFontFace* TypefaceFace = LoadObject<UFontFace>(nullptr, *FaceObjectPath);
		if (!TypefaceFace)
		{
			FString Extension = FPaths::GetExtension(TypefaceUrl, false);
			if (Extension.IsEmpty())
			{
				Extension = TEXT("ttf");
			}

			const FString TempFontPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (FaceAssetName + TEXT(".") + Extension);
			if (!DownloadToFile(TypefaceUrl, TempFontPath))
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FontResolver: failed to download '%s' typeface '%s' from %s"),
					*FontFamily, *TypefaceName, *TypefaceUrl);
				continue;
			}

			TArray<uint8> FontData;
			const bool bLoadedData = FFileHelper::LoadFileToArray(FontData, *TempFontPath);
			IFileManager::Get().Delete(*TempFontPath);
			if (!bLoadedData || FontData.Num() == 0)
			{
				continue;
			}

			UPackage* FacePackage = CreatePackage(*FacePackageName);
			if (!FacePackage)
			{
				continue;
			}

			TypefaceFace = NewObject<UFontFace>(FacePackage, *FaceAssetName, RF_Public | RF_Standalone);
			if (!TypefaceFace)
			{
				continue;
			}

			TypefaceFace->FontFaceData->SetData(MoveTemp(FontData));
			TypefaceFace->CacheSubFaces();
			TypefaceFace->MarkPackageDirty();
			FacePackage->MarkPackageDirty();
			FAssetRegistryModule::AssetCreated(TypefaceFace);

			const FString FaceFilename = FPackageName::LongPackageNameToFilename(FacePackageName, FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(FaceFilename), true);
			UPackage::SavePackage(FacePackage, TypefaceFace, *FaceFilename, SaveArgs);
		}

		if (!TypefaceFace)
		{
			continue;
		}

		if (int32* ExistingIndex = ExistingEntryIndices.Find(TypefaceName))
		{
			FTypefaceEntry& ExistingEntry = Composite.DefaultTypeface.Fonts[*ExistingIndex];
			if (TypefaceName == TEXT("Regular"))
			{
				const FString LegacyFaceAssetName = SafeName + TEXT("_Face");
				const FString LegacyFacePackageName = FontImportPath / LegacyFaceAssetName;
				const FString LegacyFaceObjectPath = LegacyFacePackageName + TEXT(".") + LegacyFaceAssetName;
				if (UFontFace* LegacyFace = LoadObject<UFontFace>(nullptr, *LegacyFaceObjectPath))
				{
					if (ExistingEntry.Font.GetFontFaceAsset() != LegacyFace)
					{
						ExistingEntry.Name = *TypefaceName;
						ExistingEntry.Font = FFontData(LegacyFace);
						bModifiedFont = true;
					}
				}
				continue;
			}

			if (ExistingEntry.Font.GetFontFaceAsset() != TypefaceFace)
			{
				ExistingEntry.Name = *TypefaceName;
				ExistingEntry.Font = FFontData(TypefaceFace);
				bModifiedFont = true;
			}
		}
		else
		{
			FTypefaceEntry& NewEntry = Composite.DefaultTypeface.Fonts.AddDefaulted_GetRef();
			NewEntry.Name = *TypefaceName;
			NewEntry.Font = FFontData(TypefaceFace);
			ExistingEntryIndices.Add(TypefaceName, Composite.DefaultTypeface.Fonts.Num() - 1);
			bModifiedFont = true;
		}
	}

	if (!bModifiedFont)
	{
		return false;
	}

	UPackage* FontPackage = Font->GetOutermost();
	if (!FontPackage)
	{
		return false;
	}

	Font->MarkPackageDirty();
	FontPackage->MarkPackageDirty();

	const FString FontFilename = FPackageName::LongPackageNameToFilename(FontPackageName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FontFilename), true);
	const bool bSaved = UPackage::SavePackage(FontPackage, Font, *FontFilename, SaveArgs);
	if (bSaved)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("FontResolver: refreshed known web font '%s' with %d typefaces"),
			*FontFamily, Composite.DefaultTypeface.Fonts.Num());
	}
	return bSaved;
}

FString FFontResolver::FindBestTypeface(const FString& FontFamily, const FString& RequestedWeight)
{
	FString AssetPath = FindFontAssetPath(FontFamily);
	if (AssetPath.IsEmpty())
		return FString();

	const FString NormalizedFamily = NormalizeFontFamilyKeyForResolver(FontFamily);
	UFont* Font = LoadObject<UFont>(nullptr, *AssetPath);
	if (GetKnownCssFontFamilies().Contains(NormalizedFamily))
	{
		if (Font)
		{
			const FString FontPackageName = FPackageName::ObjectPathToPackageName(AssetPath);
			const FString FontImportPath = FPaths::GetPath(FontPackageName);
			const FString LegacyFaceAssetName = Font->GetName() + TEXT("_Face");
			const FString LegacyFacePackageName = FontImportPath / LegacyFaceAssetName;
			const FString LegacyFaceObjectPath = LegacyFacePackageName + TEXT(".") + LegacyFaceAssetName;
			if (UFontFace* LegacyFace = LoadObject<UFontFace>(nullptr, *LegacyFaceObjectPath))
			{
				FCompositeFont& Composite = Font->GetMutableInternalCompositeFont();
				for (FTypefaceEntry& Entry : Composite.DefaultTypeface.Fonts)
				{
					if (Entry.Name == TEXT("Regular") && Entry.Font.GetFontFaceAsset() != LegacyFace)
					{
						Entry.Font = FFontData(LegacyFace);
						if (UPackage* FontPackage = Font->GetOutermost())
						{
							FSavePackageArgs SaveArgs;
							SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
							Font->MarkPackageDirty();
							FontPackage->MarkPackageDirty();
							const FString FontFilename = FPackageName::LongPackageNameToFilename(FontPackageName, FPackageName::GetAssetPackageExtension());
							UPackage::SavePackage(FontPackage, Font, *FontFilename, SaveArgs);
						}
						break;
					}
				}
			}
		}
	}

	EnsureKnownWebFontTypefaces(FontFamily, Font, AssetPath);
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

FString FFontResolver::WeightNameFromFile(const FString& FilePath, const FString& FamilyName)
{
	FString Base = FPaths::GetBaseFilename(FilePath);
	// Strip family prefix (e.g. "Roboto-Bold" -> "Bold", "Roboto_Light" -> "Light")
	FString Suffix = Base;
	if (Base.StartsWith(FamilyName, ESearchCase::IgnoreCase))
	{
		Suffix = Base.Mid(FamilyName.Len());
		// Strip separator
		if (Suffix.StartsWith(TEXT("-")) || Suffix.StartsWith(TEXT("_")))
			Suffix = Suffix.Mid(1);
	}
	if (Suffix.IsEmpty())
		return TEXT("Regular");

	// Map common suffixes to standard weight names
	FString S = Suffix.ToLower();
	if (S == TEXT("thin") || S == TEXT("hairline"))       return TEXT("Thin");
	if (S == TEXT("extralight") || S == TEXT("ultralight")) return TEXT("ExtraLight");
	if (S == TEXT("light"))      return TEXT("Light");
	if (S == TEXT("regular") || S == TEXT("normal"))       return TEXT("Regular");
	if (S == TEXT("medium"))     return TEXT("Medium");
	if (S == TEXT("semibold") || S == TEXT("demibold"))    return TEXT("SemiBold");
	if (S == TEXT("bold"))       return TEXT("Bold");
	if (S == TEXT("extrabold") || S == TEXT("ultrabold"))  return TEXT("ExtraBold");
	if (S == TEXT("black") || S == TEXT("heavy"))          return TEXT("Black");
	// Italic variants: strip "italic" and re-check
	if (S.Contains(TEXT("italic")))
	{
		FString Base2 = S.Replace(TEXT("italic"), TEXT("")).TrimStartAndEnd();
		Base2.ReplaceInline(TEXT("-"), TEXT(""));
		Base2.ReplaceInline(TEXT("_"), TEXT(""));
		if (Base2.IsEmpty() || Base2 == TEXT("regular"))
			return TEXT("Regular");
		// Recurse with cleaned name
		return WeightNameFromFile(Base2, TEXT(""));
	}
	return Suffix; // Use as-is if unrecognized
}

TArray<FString> FFontResolver::FindSiblingFontFiles(const FString& FilePath, const FString& FamilyName)
{
	TArray<FString> Result;
	FString Dir = FPaths::GetPath(FilePath);
	FString PrimaryFile = FPaths::GetCleanFilename(FilePath);

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.*")), true, false);

	FString SafeFamily = FamilyName;
	SafeFamily.ReplaceInline(TEXT(" "), TEXT(""));

	for (const FString& File : Files)
	{
		if (File == PrimaryFile)
			continue;
		FString Ext = FPaths::GetExtension(File, false).ToLower();
		if (Ext != TEXT("ttf") && Ext != TEXT("otf"))
			continue;
		FString Base = FPaths::GetBaseFilename(File);
		if (Base.StartsWith(FamilyName, ESearchCase::IgnoreCase)
			|| Base.StartsWith(SafeFamily, ESearchCase::IgnoreCase))
		{
			Result.Add(Dir / File);
		}
	}
	return Result;
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

bool FFontResolver::DownloadToFile(const FString& Url, const FString& LocalPath)
{
	bool bDone = false;
	bool bSuccess = false;
	TArray<uint8> Data;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(30.0f);
	Request->OnProcessRequestComplete().BindLambda(
		[&](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			bDone = true;
			if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300)
			{
				Data = Response->GetContent();
				bSuccess = Data.Num() > 0;
			}
		});

	if (!Request->ProcessRequest())
		return false;

	FScopedSlowTask SlowTask(30.0f, FText::Format(
		LOCTEXT("DownloadingFont", "Downloading font: {0}"), FText::FromString(Url)));
	SlowTask.MakeDialog(true);

	const double StartTime = FPlatformTime::Seconds();
	constexpr double Timeout = 30.0;
	while (!bDone)
	{
		const double Elapsed = FPlatformTime::Seconds() - StartTime;
		if (Elapsed > Timeout)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("FontResolver: Download timeout for %s"), *Url);
			Request->CancelRequest();
			return false;
		}
		if (SlowTask.ShouldCancel())
		{
			Request->CancelRequest();
			return false;
		}
		SlowTask.EnterProgressFrame(0.01f);
		FHttpModule::Get().GetHttpManager().Tick(0.01f);
		if (FSlateApplication::IsInitialized())
			FSlateApplication::Get().PumpMessages();
		FPlatformProcess::Sleep(0.01f);
	}

	if (!bSuccess)
		return false;

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(LocalPath), true);
	return FFileHelper::SaveArrayToFile(Data, *LocalPath);
}

FString FFontResolver::ResolveFont(const FString& FontName, const FString& Url)
{
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	FString BasePath = Settings ? Settings->RemoteImageImportPath : TEXT("/Game/Design2UMG/ImportedImages");
	// Fonts go to sibling Fonts/ directory
	FString FontImportPath = FPaths::GetPath(BasePath) + TEXT("/Fonts");
	if (!FPackageName::IsValidLongPackageName(FontImportPath))
		FontImportPath = TEXT("/Game/Design2UMG/Fonts");

	// Sanitize name for asset
	FString SafeName = FontName;
	SafeName.ReplaceInline(TEXT(" "), TEXT("_"));
	SafeName.ReplaceInline(TEXT("."), TEXT("_"));

	FString FacePkgName = FontImportPath / (SafeName + TEXT("_Face"));
	FString FontPkgName = FontImportPath / SafeName;
	FString FontObjPath = FontPkgName + TEXT(".") + SafeName;

	// Check if already exists
	if (UFont* Existing = LoadObject<UFont>(nullptr, *FontObjPath))
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("Font '%s' already exists: %s"), *FontName, *FontObjPath);
		FontAssetCache.FindOrAdd(NormalizeFontFamilyKeyForResolver(FontName)) = FontObjPath;
		IndexFontAssetName(FontName, FontObjPath);
		return FontObjPath;
	}

	// Download
	bool bIsRemote = Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
		|| Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);

	TArray<uint8> FontData;
	if (bIsRemote)
	{
		FString Extension = FPaths::GetExtension(Url, false);
		if (Extension.IsEmpty() || !(Extension == TEXT("ttf") || Extension == TEXT("otf") || Extension == TEXT("woff2")))
			Extension = TEXT("ttf");
		FString TempPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / SafeName + TEXT(".") + Extension;
		if (!DownloadToFile(Url, TempPath))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Font download failed: %s (%s)"), *FontName, *Url);
			return FString();
		}
		FFileHelper::LoadFileToArray(FontData, *TempPath);
		IFileManager::Get().Delete(*TempPath);
	}
	else
	{
		if (!FFileHelper::LoadFileToArray(FontData, *Url))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Font file read failed: %s (%s)"), *FontName, *Url);
			return FString();
		}
	}

	if (FontData.Num() == 0)
		return FString();

	// Create FontFace asset
	UPackage* FacePkg = CreatePackage(*FacePkgName);
	if (!FacePkg) return FString();

	FString FaceAssetName = SafeName + TEXT("_Face");
	UFontFace* FontFace = NewObject<UFontFace>(FacePkg, *FaceAssetName, RF_Public | RF_Standalone);
	if (!FontFace) return FString();

	FontFace->FontFaceData->SetData(MoveTemp(FontData));
	FontFace->CacheSubFaces();
	FacePkg->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(FontFace);

	// Create UFont asset
	UPackage* FontPkg = CreatePackage(*FontPkgName);
	if (!FontPkg) return FString();

	UFont* Font = NewObject<UFont>(FontPkg, *SafeName, RF_Public | RF_Standalone);
	if (!Font) return FString();

	Font->FontCacheType = EFontCacheType::Runtime;

	// Primary typeface entry
	FString PrimaryWeightName = bIsRemote ? TEXT("Regular") : WeightNameFromFile(Url, FontName);
	FTypefaceEntry& Entry = Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
	Entry.Name = *PrimaryWeightName;
	Entry.Font = FFontData(FontFace);

	// Save primary FontFace package
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

	FString FaceFile = FPackageName::LongPackageNameToFilename(FacePkgName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FaceFile), true);
	UPackage::SavePackage(FacePkg, FontFace, *FaceFile, SaveArgs);

	// Scan sibling font files for additional weight variants (local files only)
	if (!bIsRemote)
	{
		TArray<FString> Siblings = FindSiblingFontFiles(Url, FontName);
		for (const FString& SibPath : Siblings)
		{
			TArray<uint8> SibData;
			if (!FFileHelper::LoadFileToArray(SibData, *SibPath) || SibData.Num() == 0)
				continue;

			FString SibWeightName = WeightNameFromFile(SibPath, FontName);
			FString SibFaceName = SafeName + TEXT("_") + SibWeightName + TEXT("_Face");
			FString SibFacePkgName = FontImportPath / SibFaceName;

			UPackage* SibFacePkg = CreatePackage(*SibFacePkgName);
			if (!SibFacePkg) continue;

			UFontFace* SibFace = NewObject<UFontFace>(SibFacePkg, *SibFaceName, RF_Public | RF_Standalone);
			if (!SibFace) continue;

			SibFace->FontFaceData->SetData(MoveTemp(SibData));
			SibFace->CacheSubFaces();
			SibFacePkg->MarkPackageDirty();
			FAssetRegistryModule::AssetCreated(SibFace);

			FTypefaceEntry& SibEntry = Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
			SibEntry.Name = *SibWeightName;
			SibEntry.Font = FFontData(SibFace);

			FString SibFaceFile = FPackageName::LongPackageNameToFilename(SibFacePkgName, FPackageName::GetAssetPackageExtension());
			UPackage::SavePackage(SibFacePkg, SibFace, *SibFaceFile, SaveArgs);

			UE_LOG(LogDesign2UMG, Log, TEXT("  Added typeface '%s' from %s"), *SibWeightName, *FPaths::GetCleanFilename(SibPath));
		}
	}

	FontPkg->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Font);

	FString FontFile = FPackageName::LongPackageNameToFilename(FontPkgName, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(FontPkg, Font, *FontFile, SaveArgs);

	UE_LOG(LogDesign2UMG, Log, TEXT("Imported font '%s' -> %s"), *FontName, *FontObjPath);
	FontAssetCache.FindOrAdd(NormalizeFontFamilyKeyForResolver(FontName)) = FontObjPath;
	IndexFontAssetName(FontName, FontObjPath);
	return FontObjPath;
}

void FFontResolver::AutoImportFonts(const FDesignDocument& Document)
{
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!Settings || !Settings->bAutoImportFonts)
		return;

	int32 Imported = 0;
	for (const FDesignFont& Font : Document.Fonts)
	{
		if (Font.Name.IsEmpty() || Font.Url.IsEmpty())
			continue;

		FString AssetPath = ResolveFont(Font.Name, Font.Url);
		if (!AssetPath.IsEmpty())
			++Imported;
	}

	if (Imported > 0)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("Auto-imported %d font(s)"), Imported);
		FDesign2UMGNotification::Success(
			FText::Format(LOCTEXT("FontsImported", "Design2UMG: Imported {0} font(s)"),
				FText::AsNumber(Imported)));
	}
}

#undef LOCTEXT_NAMESPACE


