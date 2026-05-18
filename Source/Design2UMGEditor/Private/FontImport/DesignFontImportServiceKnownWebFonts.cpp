// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignFontImportService.h"

#include "Design2UMGNotifications.h"
#include "Design2UMGSettings.h"
#include "DesignFontImportAutomationPaths.h"
#include "DesignFontImportCatalog.h"
#include "DesignFontImportServiceInternal.h"
#include "DesignSchema.h"
#include "FontResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "DesignFontImportService"

namespace
{
	bool QueryKnownWebFontTypefaces(
		const FString& FontFamily,
		const TArray<int32>& RequestedWeights,
		TMap<FString, FString>& OutTypefaceUrls)
	{
		OutTypefaceUrls.Reset();
		const FString NormalizedFamily = DesignFontImportCatalog::NormalizeFontFamilyKey(FontFamily);
		if (!DesignFontImportCatalog::GetKnownCssFontFamilies().Contains(NormalizedFamily))
		{
			return false;
		}

		TArray<FString> WeightValues;
		for (const int32 Weight : RequestedWeights)
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
		if (!DesignFontImportServiceInternal::DownloadToFile(CssUrl, TempCssPath))
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

			OutTypefaceUrls.FindOrAdd(DesignFontImportCatalog::WeightNameFromNumeric(WeightValue)) = TypefaceUrl;
		}

		return OutTypefaceUrls.Num() > 0;
	}

	bool EnsureKnownWebFontTypefaces(
		const FString& FontFamily,
		UFont* Font,
		const FString& FontObjectPath,
		const TArray<int32>& RequestedWeights)
	{
		if (!Font)
		{
			return false;
		}

		const FCompositeFont* ExistingComposite = Font->GetCompositeFont();
		if (ExistingComposite && ExistingComposite->DefaultTypeface.Fonts.Num() > 0)
		{
			TMap<FString, const FTypefaceEntry*> ExistingTypefaces;
			for (const FTypefaceEntry& Entry : ExistingComposite->DefaultTypeface.Fonts)
			{
				ExistingTypefaces.Add(Entry.Name.ToString(), &Entry);
			}

			bool bHasAllKnownWeightsReloadSafe = true;
			for (const int32 Weight : RequestedWeights)
			{
				const FTypefaceEntry* const* ExistingEntry = ExistingTypefaces.Find(DesignFontImportCatalog::WeightNameFromNumeric(Weight));
				if (!ExistingEntry
					|| !*ExistingEntry
					|| !DesignFontImportServiceInternal::TypefaceEntryHasReloadSafeData(**ExistingEntry))
				{
					bHasAllKnownWeightsReloadSafe = false;
					break;
				}
			}

			if (bHasAllKnownWeightsReloadSafe)
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
		if (!QueryKnownWebFontTypefaces(FontFamily, RequestedWeights, TypefaceUrls))
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
		bool bModifiedFace = false;

		for (const int32 Weight : RequestedWeights)
		{
			const FString TypefaceName = DesignFontImportCatalog::WeightNameFromNumeric(Weight);
			if (TypefaceName.IsEmpty())
			{
				continue;
			}

			const FString FaceAssetName = SafeName + TEXT("_") + TypefaceName + TEXT("_Face");
			const FString FacePackageName = FontImportPath / FaceAssetName;
			const FString FaceObjectPath = FacePackageName + TEXT(".") + FaceAssetName;

			UFontFace* TypefaceFace = LoadObject<UFontFace>(nullptr, *FaceObjectPath);
			if (!TypefaceFace
				|| !DesignFontImportServiceInternal::HasReloadSafeFontFaceData(TypefaceFace)
				|| TypefaceFace->LoadingPolicy != EFontLoadingPolicy::Inline)
			{
				const FString* TypefaceUrl = TypefaceUrls.Find(TypefaceName);
				TArray<uint8> FontData;
				if ((!TypefaceFace || !DesignFontImportServiceInternal::HasReloadSafeFontFaceData(TypefaceFace))
					&& (!TypefaceUrl || !DesignFontImportServiceInternal::LoadFontBytesFromSource(FaceAssetName, *TypefaceUrl, FontData)))
				{
					continue;
				}

				if (!TypefaceFace)
				{
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
					FAssetRegistryModule::AssetCreated(TypefaceFace);
				}

				const bool bFaceOk = FontData.Num() > 0
					? DesignFontImportServiceInternal::MakeFontFaceReloadSafe(TypefaceFace, MoveTemp(FontData))
					: DesignFontImportServiceInternal::MakeFontFaceReloadSafe(TypefaceFace);
				if (!bFaceOk)
				{
					continue;
				}
				bModifiedFace = true;
			}

			if (!TypefaceFace)
			{
				continue;
			}

			if (int32* ExistingIndex = ExistingEntryIndices.Find(TypefaceName))
			{
				FTypefaceEntry& ExistingEntry = Composite.DefaultTypeface.Fonts[*ExistingIndex];
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
			return bModifiedFace;
		}

		UPackage* FontPackage = Font->GetOutermost();
		if (!FontPackage)
		{
			return false;
		}

		Font->MarkPackageDirty();
		FontPackage->MarkPackageDirty();

		return DesignFontImportServiceInternal::SaveAssetPackage(Font, FontPackageName);
	}

	FString ImportKnownCssFontFamily(const FString& FontFamily, const TArray<int32>& RequestedWeights)
	{
		const FString NormalizedFamily = DesignFontImportCatalog::NormalizeFontFamilyKey(FontFamily);
		if (!DesignFontImportCatalog::GetKnownCssFontFamilies().Contains(NormalizedFamily))
		{
			return FString();
		}

		FString ImportedPath = DesignFontImportServiceInternal::ResolveFont(FontFamily, FString());
		if (!ImportedPath.IsEmpty())
		{
			if (UFont* Font = LoadObject<UFont>(nullptr, *ImportedPath))
			{
				EnsureKnownWebFontTypefaces(FontFamily, Font, ImportedPath, RequestedWeights);
			}
			return ImportedPath;
		}

		TMap<FString, FString> TypefaceUrls;
		if (!QueryKnownWebFontTypefaces(FontFamily, RequestedWeights, TypefaceUrls) || TypefaceUrls.Num() == 0)
		{
			return FString();
		}

		FString* PrimaryUrl = TypefaceUrls.Find(TEXT("Regular"));
		if (!PrimaryUrl)
		{
			PrimaryUrl = TypefaceUrls.Find(TEXT("Medium"));
		}
		if (!PrimaryUrl)
		{
			PrimaryUrl = TypefaceUrls.Find(TEXT("Bold"));
		}
		if (!PrimaryUrl)
		{
			for (TPair<FString, FString>& Pair : TypefaceUrls)
			{
				PrimaryUrl = &Pair.Value;
				break;
			}
		}
		if (!PrimaryUrl || PrimaryUrl->IsEmpty())
		{
			return FString();
		}

		ImportedPath = DesignFontImportServiceInternal::ResolveFont(FontFamily, *PrimaryUrl);
		if (ImportedPath.IsEmpty())
		{
			return FString();
		}

		if (UFont* Font = LoadObject<UFont>(nullptr, *ImportedPath))
		{
			EnsureKnownWebFontTypefaces(FontFamily, Font, ImportedPath, RequestedWeights);
		}
		return ImportedPath;
	}

	void CollectUsedFontFamilies(
		const TArray<TSharedPtr<FDesignNode>>& Nodes,
		TSet<FString>& OutFamilies,
		TSet<FString>& OutIconFamilies,
		TMap<FString, TSet<int32>>& OutRequestedWeights)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}

			if (!Node->Text.FontFamily.IsEmpty())
			{
				OutFamilies.Add(Node->Text.FontFamily);
				DesignFontImportServiceInternal::AddRequestedCssWeight(
					OutRequestedWeights,
					Node->Text.FontFamily,
					Node->Text.FontWeight);
			}
			if (!Node->Text.IconFontFamily.IsEmpty())
			{
				OutFamilies.Add(Node->Text.IconFontFamily);
				OutIconFamilies.Add(Node->Text.IconFontFamily);
				DesignFontImportServiceInternal::AddRequestedCssWeight(
					OutRequestedWeights,
					Node->Text.IconFontFamily,
					DesignFontImportServiceInternal::ResolveNodeRequestedWeightToken(Node));
			}

			CollectUsedFontFamilies(Node->Children, OutFamilies, OutIconFamilies, OutRequestedWeights);
		}
	}

	void PrimeKnownIconGlyphs(const TSet<FString>& IconFamilies)
	{
		for (const FString& Family : IconFamilies)
		{
			const FString NormalizedFamily = DesignFontImportCatalog::NormalizeFontFamilyKey(Family);
			if (NormalizedFamily.IsEmpty())
			{
				continue;
			}

			if (!DesignFontImportServiceInternal::GKnownIconGlyphCache.Contains(NormalizedFamily))
			{
				TMap<FString, FString> Glyphs;
				TMap<FString, FString> IconNames;
				if (const FString* CssUrl = DesignFontImportCatalog::GetKnownIconFontCssUrls().Find(NormalizedFamily))
				{
					const FString TempCssPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (NormalizedFamily + TEXT("_iconfont.css"));
					if (DesignFontImportServiceInternal::DownloadToFile(*CssUrl, TempCssPath))
					{
						FString CssContent;
						const bool bLoaded = FFileHelper::LoadFileToString(CssContent, *TempCssPath);
						IFileManager::Get().Delete(*TempCssPath);
						if (bLoaded && !CssContent.IsEmpty())
						{
							const FRegexPattern RulePattern(TEXT("\\.icon-([a-z0-9\\-]+)::before\\s*\\{\\s*content:\\s*\"\\\\([A-Fa-f0-9]+)\";"));
							FRegexMatcher Matcher(RulePattern, CssContent);
							while (Matcher.FindNext())
							{
								const FString ParsedIconName =
									DesignFontImportServiceInternal::NormalizeIconNameKey(Matcher.GetCaptureGroup(1));
								const FString HexCode = Matcher.GetCaptureGroup(2);
								if (ParsedIconName.IsEmpty() || HexCode.IsEmpty())
								{
									continue;
								}

								const int32 Codepoint = FCString::Strtoi(*HexCode, nullptr, 16);
								if (Codepoint <= 0)
								{
									continue;
								}

								FString Glyph;
								Glyph.AppendChar(static_cast<TCHAR>(Codepoint));
								Glyphs.FindOrAdd(ParsedIconName) = Glyph;
								IconNames.FindOrAdd(Glyph) = ParsedIconName;
							}
						}
					}
				}

				if (Glyphs.Num() > 0 || IconNames.Num() > 0)
				{
					DesignFontImportServiceInternal::GKnownIconGlyphCache.Add(NormalizedFamily, MoveTemp(Glyphs));
					DesignFontImportServiceInternal::GKnownIconNameCache.Add(NormalizedFamily, MoveTemp(IconNames));
				}
			}

			const TMap<FString, FString>* Glyphs =
				DesignFontImportServiceInternal::GKnownIconGlyphCache.Find(NormalizedFamily);
			const TMap<FString, FString>* IconNames =
				DesignFontImportServiceInternal::GKnownIconNameCache.Find(NormalizedFamily);
			if (Glyphs && IconNames)
			{
				FFontResolver::RegisterKnownIconGlyphs(Family, *Glyphs, *IconNames);
			}
		}
	}
}

void DesignFontImportService::PrepareDocumentFonts(const FDesignDocument& Document)
{
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TSet<FString> UsedFamilies;
	TSet<FString> UsedIconFamilies;
	TMap<FString, TSet<int32>> RequestedWeightsByFamily;

	for (const FDesignFont& Font : Document.Fonts)
	{
		if (!Font.Name.IsEmpty())
		{
			UsedFamilies.Add(Font.Name);
		}
	}
	CollectUsedFontFamilies(Document.Children, UsedFamilies, UsedIconFamilies, RequestedWeightsByFamily);

	int32 ImportedCount = 0;
	bool bImportedAny = false;
	TMap<FString, FString> ResolvedFontPathsByFamily;
	if (Settings && Settings->bAutoImportFonts)
	{
		for (const FDesignFont& Font : Document.Fonts)
		{
			if (Font.Name.IsEmpty() || Font.Url.IsEmpty())
			{
				continue;
			}

			const FString ExistingFontPath = FFontResolver::FindFontAssetPath(Font.Name);
			if (DesignFontImportServiceInternal::ShouldKeepExistingProjectFont(ExistingFontPath))
			{
				continue;
			}

			const FString ImportedPath = DesignFontImportServiceInternal::ResolveFont(Font.Name, Font.Url);
			if (!ImportedPath.IsEmpty())
			{
				ResolvedFontPathsByFamily.Add(Font.Name, ImportedPath);
				++ImportedCount;
				bImportedAny = true;
			}
		}
	}

	if (bImportedAny)
	{
		FFontResolver::ClearCache();
	}

	if (Settings && (Settings->bAutoImportKnownWebFonts || Settings->bAutoImportFonts))
	{
		for (const FString& Family : UsedFamilies)
		{
			const FString NormalizedFamily = DesignFontImportCatalog::NormalizeFontFamilyKey(Family);
			if (NormalizedFamily.IsEmpty())
			{
				continue;
			}

			const bool bKnownCssFamily = DesignFontImportCatalog::GetKnownCssFontFamilies().Contains(NormalizedFamily);
			const bool bKnownDirectFamily = DesignFontImportCatalog::GetKnownWebFontUrls().Contains(NormalizedFamily);
			const FString ExistingFontPath = FFontResolver::FindFontAssetPath(Family);
			const bool bExistingAutomationFont = DesignFontImportAutomationPaths::IsAutomationImportedFontPath(ExistingFontPath);
			const TArray<int32> RequestedWeights =
				DesignFontImportCatalog::ResolveKnownCssFontWeights(Family, RequestedWeightsByFamily);
			if (bKnownCssFamily
				&& !ExistingFontPath.IsEmpty()
				&& !DesignFontImportServiceInternal::IsEngineFallbackFontPath(ExistingFontPath)
				&& !bExistingAutomationFont)
			{
				if (UFont* ExistingFont = LoadObject<UFont>(nullptr, *ExistingFontPath))
				{
					if (EnsureKnownWebFontTypefaces(Family, ExistingFont, ExistingFontPath, RequestedWeights))
					{
						++ImportedCount;
						bImportedAny = true;
					}
				}
			}
			const bool bAllowUpgradeFromEngineFallback =
				(bKnownCssFamily || bKnownDirectFamily)
				&& DesignFontImportServiceInternal::IsEngineFallbackFontPath(ExistingFontPath);
			if (!ExistingFontPath.IsEmpty() && !bAllowUpgradeFromEngineFallback && !bExistingAutomationFont)
			{
				continue;
			}

			FString ImportedPath;
			if (bKnownCssFamily)
			{
				ImportedPath = ImportKnownCssFontFamily(Family, RequestedWeights);
			}
			else if (const FString* KnownUrl = DesignFontImportCatalog::GetKnownWebFontUrls().Find(NormalizedFamily))
			{
				ImportedPath = DesignFontImportServiceInternal::ResolveFont(Family, *KnownUrl);
			}
			if (ImportedPath.IsEmpty())
			{
				continue;
			}
			ResolvedFontPathsByFamily.Add(Family, ImportedPath);

			if (bKnownCssFamily)
			{
				if (UFont* Font = LoadObject<UFont>(nullptr, *ImportedPath))
				{
					EnsureKnownWebFontTypefaces(Family, Font, ImportedPath, RequestedWeights);
				}
			}

			++ImportedCount;
			bImportedAny = true;
		}
	}

	if (bImportedAny)
	{
		FFontResolver::ClearCache();
		for (const TPair<FString, FString>& Pair : ResolvedFontPathsByFamily)
		{
			FFontResolver::RegisterFontAssetPath(Pair.Key, Pair.Value);
		}
		FDesign2UMGNotification::Success(
			FText::Format(LOCTEXT("FontsImported", "Design2UMG: Imported {0} font(s)"),
				FText::AsNumber(ImportedCount)));
	}

	PrimeKnownIconGlyphs(UsedIconFamilies);
	FFontResolver::FindMissingFonts(Document);
}

FString DesignFontImportService::EnsureFontFamilyAvailable(const FString& FontFamily)
{
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!Settings)
	{
		return FString();
	}

	const FString NormalizedFamily = DesignFontImportCatalog::NormalizeFontFamilyKey(FontFamily);
	if (NormalizedFamily.IsEmpty())
	{
		return FString();
	}

	if (!Settings->bAutoImportKnownWebFonts && !Settings->bAutoImportFonts)
	{
		return FString();
	}

	const TArray<int32> RequestedWeights =
		DesignFontImportCatalog::ResolveKnownCssFontWeights(FontFamily, TMap<FString, TSet<int32>>());
	FString ImportedPath;
	if (DesignFontImportCatalog::GetKnownCssFontFamilies().Contains(NormalizedFamily))
	{
		ImportedPath = ImportKnownCssFontFamily(FontFamily, RequestedWeights);
	}
	else if (const FString* KnownUrl = DesignFontImportCatalog::GetKnownWebFontUrls().Find(NormalizedFamily))
	{
		ImportedPath = DesignFontImportServiceInternal::ResolveFont(FontFamily, *KnownUrl);
	}
	else
	{
		return FString();
	}
	if (ImportedPath.IsEmpty())
	{
		return FString();
	}
	FFontResolver::RegisterFontAssetPath(FontFamily, ImportedPath);

	if (DesignFontImportCatalog::GetKnownCssFontFamilies().Contains(NormalizedFamily))
	{
		if (UFont* Font = LoadObject<UFont>(nullptr, *ImportedPath))
		{
			EnsureKnownWebFontTypefaces(FontFamily, Font, ImportedPath, RequestedWeights);
		}
	}

	FFontResolver::ClearCache();
	FFontResolver::RegisterFontAssetPath(FontFamily, ImportedPath);
	return ImportedPath;
}

void DesignFontImportService::PrimeKnownIconFamily(const FString& FontFamily)
{
	TSet<FString> Families;
	if (!FontFamily.IsEmpty())
	{
		Families.Add(FontFamily);
	}
	PrimeKnownIconGlyphs(Families);
}

#undef LOCTEXT_NAMESPACE
