// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignFontImportService.h"

#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "Design2UMGSettings.h"
#include "Automation/Design2UMGAutomationRender.h"
#include "DesignSchema.h"
#include "FontResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "DesignFontImportService"

namespace
{
	TMap<FString, TMap<FString, FString>> GKnownIconGlyphCache;
	TMap<FString, TMap<FString, FString>> GKnownIconNameCache;

	FString NormalizeFontFamilyKey(const FString& InFamily)
	{
		FString Key = InFamily.ToLower().TrimStartAndEnd();
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		Key.ReplaceInline(TEXT("-"), TEXT(""));
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		return Key;
	}

	FString NormalizeIconNameKey(const FString& InName)
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

	bool IsEngineFallbackFontPath(const FString& AssetPath)
	{
		return AssetPath.StartsWith(TEXT("/Engine/EngineFonts/"), ESearchCase::IgnoreCase);
	}

	bool ShouldKeepExistingProjectFont(const FString& ExistingFontPath)
	{
		return !ExistingFontPath.IsEmpty()
			&& !IsEngineFallbackFontPath(ExistingFontPath)
			&& !ExistingFontPath.Contains(TEXT("/Design2UMG/Automation/ImportedFonts/"), ESearchCase::IgnoreCase);
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
		};
		return KnownCssFamilies;
	}

	const TArray<int32>& GetKnownCssFontWeights()
	{
		static const TArray<int32> KnownCssWeights = { 400, 500, 600, 700 };
		return KnownCssWeights;
	}

	bool DownloadToFile(const FString& Url, const FString& LocalPath)
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
		{
			return false;
		}

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
				UE_LOG(LogDesign2UMG, Warning, TEXT("DesignFontImportService: download timeout for %s"), *Url);
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
			{
				FSlateApplication::Get().PumpMessages();
			}
			FPlatformProcess::Sleep(0.01f);
		}

		if (!bSuccess)
		{
			return false;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(LocalPath), true);
		return FFileHelper::SaveArrayToFile(Data, *LocalPath);
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

	FString WeightNameFromFile(const FString& FilePath, const FString& FamilyName)
	{
		FString Base = FPaths::GetBaseFilename(FilePath);
		FString Suffix = Base;
		if (Base.StartsWith(FamilyName, ESearchCase::IgnoreCase))
		{
			Suffix = Base.Mid(FamilyName.Len());
			if (Suffix.StartsWith(TEXT("-")) || Suffix.StartsWith(TEXT("_")))
			{
				Suffix = Suffix.Mid(1);
			}
		}
		if (Suffix.IsEmpty())
		{
			return TEXT("Regular");
		}

		FString S = Suffix.ToLower();
		if (S == TEXT("thin") || S == TEXT("hairline")) return TEXT("Thin");
		if (S == TEXT("extralight") || S == TEXT("ultralight")) return TEXT("ExtraLight");
		if (S == TEXT("light")) return TEXT("Light");
		if (S == TEXT("regular") || S == TEXT("normal")) return TEXT("Regular");
		if (S == TEXT("medium")) return TEXT("Medium");
		if (S == TEXT("semibold") || S == TEXT("demibold")) return TEXT("SemiBold");
		if (S == TEXT("bold")) return TEXT("Bold");
		if (S == TEXT("extrabold") || S == TEXT("ultrabold")) return TEXT("ExtraBold");
		if (S == TEXT("black") || S == TEXT("heavy")) return TEXT("Black");
		if (S.Contains(TEXT("italic")))
		{
			FString Base2 = S.Replace(TEXT("italic"), TEXT("")).TrimStartAndEnd();
			Base2.ReplaceInline(TEXT("-"), TEXT(""));
			Base2.ReplaceInline(TEXT("_"), TEXT(""));
			if (Base2.IsEmpty() || Base2 == TEXT("regular"))
			{
				return TEXT("Regular");
			}
			return WeightNameFromFile(Base2, TEXT(""));
		}
		return Suffix;
	}

	TArray<FString> FindSiblingFontFiles(const FString& FilePath, const FString& FamilyName)
	{
		TArray<FString> Result;
		const FString Dir = FPaths::GetPath(FilePath);
		const FString PrimaryFile = FPaths::GetCleanFilename(FilePath);

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.*")), true, false);

		FString SafeFamily = FamilyName;
		SafeFamily.ReplaceInline(TEXT(" "), TEXT(""));

		for (const FString& File : Files)
		{
			if (File == PrimaryFile)
			{
				continue;
			}
			const FString Ext = FPaths::GetExtension(File, false).ToLower();
			if (Ext != TEXT("ttf") && Ext != TEXT("otf"))
			{
				continue;
			}
			const FString Base = FPaths::GetBaseFilename(File);
			if (Base.StartsWith(FamilyName, ESearchCase::IgnoreCase)
				|| Base.StartsWith(SafeFamily, ESearchCase::IgnoreCase))
			{
				Result.Add(Dir / File);
			}
		}
		return Result;
	}

	FString ResolveFont(const FString& FontName, const FString& Url)
	{
		FString FontImportPath;
		if (Design2UMGAutomationRender::IsAutomationRenderActive())
		{
			FontImportPath = TEXT("/Game/Design2UMG/Automation/ImportedFonts");
		}
		else
		{
			const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
			const FString BasePath = Settings ? Settings->RemoteImageImportPath : TEXT("/Game/Design2UMG/ImportedImages");
			FontImportPath = FPaths::GetPath(BasePath) + TEXT("/Fonts");
		}
		if (!FPackageName::IsValidLongPackageName(FontImportPath))
		{
			FontImportPath = TEXT("/Game/Design2UMG/Fonts");
		}

		FString SafeName = FontName;
		SafeName.ReplaceInline(TEXT(" "), TEXT("_"));
		SafeName.ReplaceInline(TEXT("."), TEXT("_"));

		const FString FacePkgName = FontImportPath / (SafeName + TEXT("_Face"));
		const FString FontPkgName = FontImportPath / SafeName;
		const FString FontObjPath = FontPkgName + TEXT(".") + SafeName;

		if (UFont* Existing = LoadObject<UFont>(nullptr, *FontObjPath))
		{
			return FontObjPath;
		}

		const bool bIsRemote = Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
			|| Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);

		TArray<uint8> FontData;
		if (bIsRemote)
		{
			FString Extension = FPaths::GetExtension(Url, false);
			if (Extension.IsEmpty() || !(Extension == TEXT("ttf") || Extension == TEXT("otf") || Extension == TEXT("woff2")))
			{
				Extension = TEXT("ttf");
			}
			const FString TempPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / SafeName + TEXT(".") + Extension;
			if (!DownloadToFile(Url, TempPath))
			{
				return FString();
			}
			FFileHelper::LoadFileToArray(FontData, *TempPath);
			IFileManager::Get().Delete(*TempPath);
		}
		else if (!FFileHelper::LoadFileToArray(FontData, *Url))
		{
			return FString();
		}

		if (FontData.Num() == 0)
		{
			return FString();
		}

		UPackage* FacePkg = CreatePackage(*FacePkgName);
		if (!FacePkg)
		{
			return FString();
		}

		const FString FaceAssetName = SafeName + TEXT("_Face");
		UFontFace* FontFace = NewObject<UFontFace>(FacePkg, *FaceAssetName, RF_Public | RF_Standalone);
		if (!FontFace)
		{
			return FString();
		}

		FontFace->FontFaceData->SetData(MoveTemp(FontData));
		FontFace->CacheSubFaces();
		FacePkg->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(FontFace);

		UPackage* FontPkg = CreatePackage(*FontPkgName);
		if (!FontPkg)
		{
			return FString();
		}

		UFont* Font = NewObject<UFont>(FontPkg, *SafeName, RF_Public | RF_Standalone);
		if (!Font)
		{
			return FString();
		}

		Font->FontCacheType = EFontCacheType::Runtime;
		const FString PrimaryWeightName = bIsRemote ? TEXT("Regular") : WeightNameFromFile(Url, FontName);
		FTypefaceEntry& Entry = Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = *PrimaryWeightName;
		Entry.Font = FFontData(FontFace);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

		const FString FaceFile = FPackageName::LongPackageNameToFilename(FacePkgName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FaceFile), true);
		UPackage::SavePackage(FacePkg, FontFace, *FaceFile, SaveArgs);

		if (!bIsRemote)
		{
			for (const FString& SibPath : FindSiblingFontFiles(Url, FontName))
			{
				TArray<uint8> SibData;
				if (!FFileHelper::LoadFileToArray(SibData, *SibPath) || SibData.Num() == 0)
				{
					continue;
				}

				const FString SibWeightName = WeightNameFromFile(SibPath, FontName);
				const FString SibFaceName = SafeName + TEXT("_") + SibWeightName + TEXT("_Face");
				const FString SibFacePkgName = FontImportPath / SibFaceName;

				UPackage* SibFacePkg = CreatePackage(*SibFacePkgName);
				if (!SibFacePkg)
				{
					continue;
				}

				UFontFace* SibFace = NewObject<UFontFace>(SibFacePkg, *SibFaceName, RF_Public | RF_Standalone);
				if (!SibFace)
				{
					continue;
				}

				SibFace->FontFaceData->SetData(MoveTemp(SibData));
				SibFace->CacheSubFaces();
				SibFacePkg->MarkPackageDirty();
				FAssetRegistryModule::AssetCreated(SibFace);

				FTypefaceEntry& SibEntry = Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
				SibEntry.Name = *SibWeightName;
				SibEntry.Font = FFontData(SibFace);

				const FString SibFaceFile = FPackageName::LongPackageNameToFilename(SibFacePkgName, FPackageName::GetAssetPackageExtension());
				UPackage::SavePackage(SibFacePkg, SibFace, *SibFaceFile, SaveArgs);
			}
		}

		FontPkg->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Font);

		const FString FontFile = FPackageName::LongPackageNameToFilename(FontPkgName, FPackageName::GetAssetPackageExtension());
		UPackage::SavePackage(FontPkg, Font, *FontFile, SaveArgs);
		return FontObjPath;
	}

	bool QueryKnownWebFontTypefaces(const FString& FontFamily, TMap<FString, FString>& OutTypefaceUrls)
	{
		OutTypefaceUrls.Reset();
		const FString NormalizedFamily = NormalizeFontFamilyKey(FontFamily);
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

	bool EnsureKnownWebFontTypefaces(const FString& FontFamily, UFont* Font, const FString& FontObjectPath)
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
		return UPackage::SavePackage(FontPackage, Font, *FontFilename, SaveArgs);
	}

	FString ImportKnownCssFontFamily(const FString& FontFamily)
	{
		const FString NormalizedFamily = NormalizeFontFamilyKey(FontFamily);
		if (!GetKnownCssFontFamilies().Contains(NormalizedFamily))
		{
			return FString();
		}

		TMap<FString, FString> TypefaceUrls;
		if (!QueryKnownWebFontTypefaces(FontFamily, TypefaceUrls) || TypefaceUrls.Num() == 0)
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

		const FString ImportedPath = ResolveFont(FontFamily, *PrimaryUrl);
		if (ImportedPath.IsEmpty())
		{
			return FString();
		}

		if (UFont* Font = LoadObject<UFont>(nullptr, *ImportedPath))
		{
			EnsureKnownWebFontTypefaces(FontFamily, Font, ImportedPath);
		}
		return ImportedPath;
	}

	void CollectUsedFontFamilies(
		const TArray<TSharedPtr<FDesignNode>>& Nodes,
		TSet<FString>& OutFamilies,
		TSet<FString>& OutIconFamilies)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}

			if (!Node->FontFamily.IsEmpty())
			{
				OutFamilies.Add(Node->FontFamily);
			}
			if (!Node->IconFontFamily.IsEmpty())
			{
				OutFamilies.Add(Node->IconFontFamily);
				OutIconFamilies.Add(Node->IconFontFamily);
			}

			CollectUsedFontFamilies(Node->Children, OutFamilies, OutIconFamilies);
		}
	}

	void PrimeKnownIconGlyphs(const TSet<FString>& IconFamilies)
	{
		for (const FString& Family : IconFamilies)
		{
			const FString NormalizedFamily = NormalizeFontFamilyKey(Family);
			if (NormalizedFamily.IsEmpty())
			{
				continue;
			}

			if (!GKnownIconGlyphCache.Contains(NormalizedFamily))
			{
				TMap<FString, FString> Glyphs;
				TMap<FString, FString> IconNames;
				if (const FString* CssUrl = GetKnownIconFontCssUrls().Find(NormalizedFamily))
				{
					const FString TempCssPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (NormalizedFamily + TEXT("_iconfont.css"));
					if (DownloadToFile(*CssUrl, TempCssPath))
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
								const FString ParsedIconName = NormalizeIconNameKey(Matcher.GetCaptureGroup(1));
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
					GKnownIconGlyphCache.Add(NormalizedFamily, MoveTemp(Glyphs));
					GKnownIconNameCache.Add(NormalizedFamily, MoveTemp(IconNames));
				}
			}

			const TMap<FString, FString>* Glyphs = GKnownIconGlyphCache.Find(NormalizedFamily);
			const TMap<FString, FString>* IconNames = GKnownIconNameCache.Find(NormalizedFamily);
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

	for (const FDesignFont& Font : Document.Fonts)
	{
		if (!Font.Name.IsEmpty())
		{
			UsedFamilies.Add(Font.Name);
		}
	}
	CollectUsedFontFamilies(Document.Children, UsedFamilies, UsedIconFamilies);

	int32 ImportedCount = 0;
	bool bImportedAny = false;
	if (Settings && Settings->bAutoImportFonts)
	{
		for (const FDesignFont& Font : Document.Fonts)
		{
			if (Font.Name.IsEmpty() || Font.Url.IsEmpty())
			{
				continue;
			}

			const FString ExistingFontPath = FFontResolver::FindFontAssetPath(Font.Name);
			if (ShouldKeepExistingProjectFont(ExistingFontPath))
			{
				continue;
			}

			if (!ResolveFont(Font.Name, Font.Url).IsEmpty())
			{
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
			const FString NormalizedFamily = NormalizeFontFamilyKey(Family);
			if (NormalizedFamily.IsEmpty())
			{
				continue;
			}

			const bool bKnownCssFamily = GetKnownCssFontFamilies().Contains(NormalizedFamily);
			const bool bKnownDirectFamily = GetKnownWebFontUrls().Contains(NormalizedFamily);
			const FString ExistingFontPath = FFontResolver::FindFontAssetPath(Family);
			const bool bAllowUpgradeFromEngineFallback =
				(bKnownCssFamily || bKnownDirectFamily) && IsEngineFallbackFontPath(ExistingFontPath);
			if (!ExistingFontPath.IsEmpty() && !bAllowUpgradeFromEngineFallback)
			{
				continue;
			}

			FString ImportedPath;
			if (const FString* KnownUrl = GetKnownWebFontUrls().Find(NormalizedFamily))
			{
				ImportedPath = ResolveFont(Family, *KnownUrl);
			}
			else if (bKnownCssFamily)
			{
				ImportedPath = ImportKnownCssFontFamily(Family);
			}
			if (ImportedPath.IsEmpty())
			{
				continue;
			}

			if (bKnownCssFamily)
			{
				if (UFont* Font = LoadObject<UFont>(nullptr, *ImportedPath))
				{
					EnsureKnownWebFontTypefaces(Family, Font, ImportedPath);
				}
			}

			++ImportedCount;
			bImportedAny = true;
		}
	}

	if (bImportedAny)
	{
		FFontResolver::ClearCache();
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

	const FString NormalizedFamily = NormalizeFontFamilyKey(FontFamily);
	if (NormalizedFamily.IsEmpty())
	{
		return FString();
	}

	if (!Settings->bAutoImportKnownWebFonts && !Settings->bAutoImportFonts)
	{
		return FString();
	}

	const FString* KnownUrl = GetKnownWebFontUrls().Find(NormalizedFamily);
	FString ImportedPath;
	if (KnownUrl)
	{
		ImportedPath = ResolveFont(FontFamily, *KnownUrl);
	}
	else if (GetKnownCssFontFamilies().Contains(NormalizedFamily))
	{
		ImportedPath = ImportKnownCssFontFamily(FontFamily);
	}
	else
	{
		return FString();
	}
	if (ImportedPath.IsEmpty())
	{
		return FString();
	}

	if (GetKnownCssFontFamilies().Contains(NormalizedFamily))
	{
		if (UFont* Font = LoadObject<UFont>(nullptr, *ImportedPath))
		{
			EnsureKnownWebFontTypefaces(FontFamily, Font, ImportedPath);
		}
	}

	FFontResolver::ClearCache();
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

void DesignFontImportService::ResetTransientState()
{
	GKnownIconGlyphCache.Reset();
	GKnownIconNameCache.Reset();
}

#undef LOCTEXT_NAMESPACE
