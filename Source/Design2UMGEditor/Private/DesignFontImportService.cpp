// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignFontImportService.h"

#include "Design2UMGLog.h"
#include "Design2UMGNotifications.h"
#include "Design2UMGSettings.h"
#include "DesignFontImportAutomationPaths.h"
#include "DesignFontImportCatalog.h"
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
			&& !DesignFontImportAutomationPaths::IsAutomationImportedFontPath(ExistingFontPath);
	}

	bool HasExplicitNonDefaultFontWeight(const FString& FontWeight)
	{
		const FString Normalized = FontWeight.TrimStartAndEnd();
		return !Normalized.IsEmpty()
			&& !Normalized.Equals(TEXT("normal"), ESearchCase::IgnoreCase)
			&& !Normalized.Equals(TEXT("regular"), ESearchCase::IgnoreCase)
			&& !Normalized.Equals(TEXT("400"), ESearchCase::IgnoreCase);
	}

	FString ResolveNodeRequestedWeightToken(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		if (Node->Type == EDesignNodeType::IconFont)
		{
			if (HasExplicitNonDefaultFontWeight(Node->Text.FontWeight))
			{
				return Node->Text.FontWeight;
			}

			return FString::FromInt(FMath::Clamp(Node->Text.IconWeight, 100, 900));
		}

		return Node->Text.FontWeight;
	}

	void AddRequestedCssWeight(TMap<FString, TSet<int32>>& OutRequestedWeights, const FString& FontFamily, const FString& WeightToken)
	{
		if (FontFamily.IsEmpty() || WeightToken.TrimStartAndEnd().IsEmpty())
		{
			return;
		}

		const int32 ParsedWeight = DesignFontImportCatalog::NormalizeCssWeightValue(FFontResolver::ParseWeightValue(WeightToken));
		OutRequestedWeights.FindOrAdd(DesignFontImportCatalog::NormalizeFontFamilyKey(FontFamily)).Add(ParsedWeight);
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

	FString MakeSafeFontAssetName(const FString& FontName)
	{
		FString SafeName = FontName;
		SafeName.ReplaceInline(TEXT(" "), TEXT("_"));
		SafeName.ReplaceInline(TEXT("."), TEXT("_"));
		return SafeName;
	}

	bool SaveAssetPackage(UObject* Asset, const FString& PackageName)
	{
		if (!Asset || PackageName.IsEmpty())
		{
			return false;
		}

		UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		return UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
	}

	bool HasReloadSafeFontFaceData(const UFontFace* FontFace)
	{
		return FontFace
			&& FontFace->FontFaceData->HasData();
	}

	bool MakeFontFaceReloadSafeInternal(UFontFace* FontFace, TArray<uint8>* ReplacementData)
	{
		if (!FontFace)
		{
			return false;
		}

		bool bModified = false;
		if (ReplacementData && ReplacementData->Num() > 0)
		{
			FontFace->FontFaceData->SetData(MoveTemp(*ReplacementData));
			FontFace->CacheSubFaces();
			bModified = true;
		}

		if (FontFace->LoadingPolicy != EFontLoadingPolicy::Inline)
		{
			FontFace->LoadingPolicy = EFontLoadingPolicy::Inline;
			bModified = true;
		}

		if (!HasReloadSafeFontFaceData(FontFace))
		{
			return false;
		}

		if (!bModified)
		{
			return true;
		}

		FontFace->MarkPackageDirty();
		if (UPackage* FacePackage = FontFace->GetOutermost())
		{
			FacePackage->MarkPackageDirty();
			return SaveAssetPackage(FontFace, FacePackage->GetName());
		}

		return false;
	}

	bool MakeFontFaceReloadSafe(UFontFace* FontFace)
	{
		return MakeFontFaceReloadSafeInternal(FontFace, nullptr);
	}

	bool MakeFontFaceReloadSafe(UFontFace* FontFace, TArray<uint8>&& ReplacementData)
	{
		return MakeFontFaceReloadSafeInternal(FontFace, &ReplacementData);
	}

	bool TypefaceEntryHasReloadSafeData(const FTypefaceEntry& Entry)
	{
		const UFontFace* FontFace = Cast<UFontFace>(Entry.Font.GetFontFaceAsset());
		return HasReloadSafeFontFaceData(FontFace);
	}

	bool LoadFontBytesFromSource(const FString& SafeName, const FString& Source, TArray<uint8>& OutFontData)
	{
		OutFontData.Reset();
		if (Source.IsEmpty())
		{
			return false;
		}

		const bool bIsRemote = Source.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
			|| Source.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
		if (bIsRemote)
		{
			FString Extension = FPaths::GetExtension(Source, false);
			if (Extension.IsEmpty() || !(Extension == TEXT("ttf") || Extension == TEXT("otf") || Extension == TEXT("woff2")))
			{
				Extension = TEXT("ttf");
			}
			const FString TempPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / SafeName + TEXT(".") + Extension;
			if (!DownloadToFile(Source, TempPath))
			{
				return false;
			}
			const bool bLoaded = FFileHelper::LoadFileToArray(OutFontData, *TempPath);
			IFileManager::Get().Delete(*TempPath);
			return bLoaded && OutFontData.Num() > 0;
		}

		return FFileHelper::LoadFileToArray(OutFontData, *Source) && OutFontData.Num() > 0;
	}

	bool EnsureExistingFontReloadSafe(UFont* Font, const FString& FontObjectPath, const FString& RepairSource, const FString& PrimaryTypefaceName)
	{
		if (!Font)
		{
			return false;
		}

		FCompositeFont& Composite = Font->GetMutableInternalCompositeFont();
		bool bHasAnyReloadSafeEntry = false;
		bool bModifiedFont = false;
		for (FTypefaceEntry& Entry : Composite.DefaultTypeface.Fonts)
		{
			if (UFontFace* FontFace = const_cast<UFontFace*>(Cast<UFontFace>(Entry.Font.GetFontFaceAsset())))
			{
				if (MakeFontFaceReloadSafe(FontFace))
				{
					bHasAnyReloadSafeEntry = true;
				}
			}
		}

		if (bHasAnyReloadSafeEntry)
		{
			return true;
		}

		TArray<uint8> FontBytes;
		if (!LoadFontBytesFromSource(Font->GetName(), RepairSource, FontBytes))
		{
			return false;
		}

		const FString FontPackageName = FPackageName::ObjectPathToPackageName(FontObjectPath);
		const FString FontImportPath = FPaths::GetPath(FontPackageName);
		const FString FaceAssetName = Font->GetName() + TEXT("_Face");
		const FString FacePackageName = FontImportPath / FaceAssetName;
		const FString FaceObjectPath = FacePackageName + TEXT(".") + FaceAssetName;

		UFontFace* FontFace = LoadObject<UFontFace>(nullptr, *FaceObjectPath);
		if (!FontFace)
		{
			UPackage* FacePackage = CreatePackage(*FacePackageName);
			if (!FacePackage)
			{
				return false;
			}

			FontFace = NewObject<UFontFace>(FacePackage, *FaceAssetName, RF_Public | RF_Standalone);
			if (!FontFace)
			{
				return false;
			}
			FAssetRegistryModule::AssetCreated(FontFace);
		}

		if (!MakeFontFaceReloadSafe(FontFace, MoveTemp(FontBytes)))
		{
			return false;
		}

		FTypefaceEntry* EntryToRepair = nullptr;
		for (FTypefaceEntry& Entry : Composite.DefaultTypeface.Fonts)
		{
			if (Entry.Name == FName(*PrimaryTypefaceName))
			{
				EntryToRepair = &Entry;
				break;
			}
		}
		if (!EntryToRepair)
		{
			EntryToRepair = &Composite.DefaultTypeface.Fonts.AddDefaulted_GetRef();
			EntryToRepair->Name = *PrimaryTypefaceName;
			bModifiedFont = true;
		}
		if (EntryToRepair->Font.GetFontFaceAsset() != FontFace)
		{
			EntryToRepair->Font = FFontData(FontFace);
			bModifiedFont = true;
		}

		if (bModifiedFont)
		{
			Font->MarkPackageDirty();
			if (UPackage* FontPackage = Font->GetOutermost())
			{
				FontPackage->MarkPackageDirty();
				SaveAssetPackage(Font, FontPackage->GetName());
			}
		}

		return TypefaceEntryHasReloadSafeData(*EntryToRepair);
	}

	bool TryResolvePinnedAutomationFontAssetPath(const FString& FontName, const FString& RepairSource, FString& OutFontObjectPath)
	{
		OutFontObjectPath.Reset();
		if (!Design2UMGAutomationRender::IsAutomationRenderActive())
		{
			return false;
		}

		if (!DesignFontImportService::CanReusePinnedAutomationFontPath(Design2UMGAutomationRender::GetAutomationFontImportPath()))
		{
			return false;
		}

		const FString SafeName = MakeSafeFontAssetName(FontName);
		if (SafeName.IsEmpty())
		{
			return false;
		}

		const FString PinnedFontPackageName = FString(TEXT("/Game/Design2UMG/Automation/ImportedFonts")) / SafeName;
		const FString PinnedFontObjectPath = PinnedFontPackageName + TEXT(".") + SafeName;
		UFont* PinnedFont = LoadObject<UFont>(nullptr, *PinnedFontObjectPath);
		if (!PinnedFont)
		{
			return false;
		}

		const bool bIsRemote = RepairSource.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
			|| RepairSource.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
		const FString PrimaryWeightName = bIsRemote ? TEXT("Regular") : WeightNameFromFile(RepairSource, FontName);
		if (!EnsureExistingFontReloadSafe(PinnedFont, PinnedFontObjectPath, RepairSource, PrimaryWeightName))
		{
			return false;
		}

		OutFontObjectPath = PinnedFontObjectPath;
		return true;
	}

	bool TryCloneExistingFontIntoAutomationImportPath(
		const FString& FontName,
		const FString& FontImportPath,
		FString& OutFontObjectPath)
	{
		OutFontObjectPath.Reset();
		if (!Design2UMGAutomationRender::IsAutomationRenderActive()
			|| DesignFontImportService::CanReusePinnedAutomationFontPath(FontImportPath))
		{
			return false;
		}

		const FString ExistingFontObjectPath = FFontResolver::FindFontAssetPath(FontName);
		if (ExistingFontObjectPath.IsEmpty())
		{
			return false;
		}

		const FString SafeName = MakeSafeFontAssetName(FontName);
		if (SafeName.IsEmpty())
		{
			return false;
		}

		const FString TargetFontPackageName = FontImportPath / SafeName;
		const FString TargetFontObjectPath = TargetFontPackageName + TEXT(".") + SafeName;
		if (ExistingFontObjectPath.Equals(TargetFontObjectPath, ESearchCase::IgnoreCase))
		{
			return false;
		}

		UFont* ExistingFont = LoadObject<UFont>(nullptr, *ExistingFontObjectPath);
		if (!ExistingFont)
		{
			return false;
		}

		const FCompositeFont* CompositeFont = ExistingFont->GetCompositeFont();
		if (!CompositeFont || CompositeFont->DefaultTypeface.Fonts.Num() == 0)
		{
			return false;
		}

		const FTypefaceEntry* PreferredEntry = nullptr;
		for (const FTypefaceEntry& Entry : CompositeFont->DefaultTypeface.Fonts)
		{
			if (Entry.Name.IsEqual(TEXT("Regular"), ENameCase::IgnoreCase))
			{
				PreferredEntry = &Entry;
				break;
			}
		}
		if (!PreferredEntry)
		{
			PreferredEntry = &CompositeFont->DefaultTypeface.Fonts[0];
		}

		const UFontFace* ExistingFontFace = Cast<UFontFace>(PreferredEntry->Font.GetFontFaceAsset());
		if (!ExistingFontFace || !ExistingFontFace->FontFaceData->HasData())
		{
			return false;
		}

		TArray<uint8> ClonedFontData = ExistingFontFace->FontFaceData->GetData();
		if (ClonedFontData.Num() == 0)
		{
			return false;
		}

		const FString FaceAssetName = SafeName + TEXT("_Face");
		const FString FacePackageName = FontImportPath / FaceAssetName;
		const FString PrimaryTypefaceName = PreferredEntry->Name.IsNone()
			? TEXT("Regular")
			: PreferredEntry->Name.ToString();

		UPackage* FacePackage = CreatePackage(*FacePackageName);
		if (!FacePackage)
		{
			return false;
		}

		UFontFace* FontFace = NewObject<UFontFace>(FacePackage, *FaceAssetName, RF_Public | RF_Standalone);
		if (!FontFace)
		{
			return false;
		}

		if (!MakeFontFaceReloadSafe(FontFace, MoveTemp(ClonedFontData)))
		{
			return false;
		}
		FacePackage->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(FontFace);

		UPackage* FontPackage = CreatePackage(*TargetFontPackageName);
		if (!FontPackage)
		{
			return false;
		}

		UFont* Font = NewObject<UFont>(FontPackage, *SafeName, RF_Public | RF_Standalone);
		if (!Font)
		{
			return false;
		}

		Font->FontCacheType = EFontCacheType::Runtime;
		FTypefaceEntry& Entry = Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = *PrimaryTypefaceName;
		Entry.Font = FFontData(FontFace);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

		const FString FaceFile = FPackageName::LongPackageNameToFilename(FacePackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FaceFile), true);
		UPackage::SavePackage(FacePackage, FontFace, *FaceFile, SaveArgs);

		const FString FontFile = FPackageName::LongPackageNameToFilename(TargetFontPackageName, FPackageName::GetAssetPackageExtension());
		UPackage::SavePackage(FontPackage, Font, *FontFile, SaveArgs);

		UE_LOG(
			LogDesign2UMG,
			Log,
			TEXT("DesignFontImportService: cloned existing font '%s' from '%s' into case-local automation path '%s'"),
			*FontName,
			*ExistingFontObjectPath,
			*TargetFontObjectPath);

		OutFontObjectPath = TargetFontObjectPath;
		return true;
	}

	FString ResolveFont(const FString& FontName, const FString& Url)
	{
		FString FontImportPath;
		if (Design2UMGAutomationRender::IsAutomationRenderActive())
		{
			FontImportPath = Design2UMGAutomationRender::GetAutomationFontImportPath();
			if (FontImportPath.IsEmpty())
			{
				FontImportPath = TEXT("/Game/Design2UMG/Automation/ImportedFonts");
			}
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

		const FString SafeName = MakeSafeFontAssetName(FontName);

		const FString FaceAssetName = SafeName + TEXT("_Face");
		const FString FacePkgName = FontImportPath / (SafeName + TEXT("_Face"));
		const FString FontPkgName = FontImportPath / SafeName;
		const FString FontObjPath = FontPkgName + TEXT(".") + SafeName;

		FString PinnedFontObjectPath;
		if (TryResolvePinnedAutomationFontAssetPath(FontName, Url, PinnedFontObjectPath)
			&& !PinnedFontObjectPath.Equals(FontObjPath, ESearchCase::IgnoreCase))
		{
			return PinnedFontObjectPath;
		}

		if (UFont* Existing = LoadObject<UFont>(nullptr, *FontObjPath))
		{
			const bool bIsRemoteExisting = Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
				|| Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
			const FString PrimaryWeightName = bIsRemoteExisting ? TEXT("Regular") : WeightNameFromFile(Url, FontName);
			EnsureExistingFontReloadSafe(Existing, FontObjPath, Url, PrimaryWeightName);
			return FontObjPath;
		}

		FString ClonedFontObjectPath;
		if (TryCloneExistingFontIntoAutomationImportPath(FontName, FontImportPath, ClonedFontObjectPath))
		{
			return ClonedFontObjectPath;
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
		else if (Url.IsEmpty() || !FFileHelper::LoadFileToArray(FontData, *Url))
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

		UFontFace* FontFace = NewObject<UFontFace>(FacePkg, *FaceAssetName, RF_Public | RF_Standalone);
		if (!FontFace)
		{
			return FString();
		}

		MakeFontFaceReloadSafe(FontFace, MoveTemp(FontData));
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

				MakeFontFaceReloadSafe(SibFace, MoveTemp(SibData));
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
				if (!ExistingEntry || !*ExistingEntry || !TypefaceEntryHasReloadSafeData(**ExistingEntry))
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
			if (!TypefaceFace || !HasReloadSafeFontFaceData(TypefaceFace) || TypefaceFace->LoadingPolicy != EFontLoadingPolicy::Inline)
			{
				const FString* TypefaceUrl = TypefaceUrls.Find(TypefaceName);
				TArray<uint8> FontData;
				if ((!TypefaceFace || !HasReloadSafeFontFaceData(TypefaceFace))
					&& (!TypefaceUrl || !LoadFontBytesFromSource(FaceAssetName, *TypefaceUrl, FontData)))
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
					? MakeFontFaceReloadSafe(TypefaceFace, MoveTemp(FontData))
					: MakeFontFaceReloadSafe(TypefaceFace);
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

		return SaveAssetPackage(Font, FontPackageName);
	}

	FString ImportKnownCssFontFamily(const FString& FontFamily, const TArray<int32>& RequestedWeights)
	{
		const FString NormalizedFamily = DesignFontImportCatalog::NormalizeFontFamilyKey(FontFamily);
		if (!DesignFontImportCatalog::GetKnownCssFontFamilies().Contains(NormalizedFamily))
		{
			return FString();
		}

		FString ImportedPath = ResolveFont(FontFamily, FString());
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

		ImportedPath = ResolveFont(FontFamily, *PrimaryUrl);
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
				AddRequestedCssWeight(OutRequestedWeights, Node->Text.FontFamily, Node->Text.FontWeight);
			}
			if (!Node->Text.IconFontFamily.IsEmpty())
			{
				OutFamilies.Add(Node->Text.IconFontFamily);
				OutIconFamilies.Add(Node->Text.IconFontFamily);
				AddRequestedCssWeight(OutRequestedWeights, Node->Text.IconFontFamily, ResolveNodeRequestedWeightToken(Node));
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

			if (!GKnownIconGlyphCache.Contains(NormalizedFamily))
			{
				TMap<FString, FString> Glyphs;
				TMap<FString, FString> IconNames;
				if (const FString* CssUrl = DesignFontImportCatalog::GetKnownIconFontCssUrls().Find(NormalizedFamily))
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
			if (ShouldKeepExistingProjectFont(ExistingFontPath))
			{
				continue;
			}

			const FString ImportedPath = ResolveFont(Font.Name, Font.Url);
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
			const TArray<int32> RequestedWeights = DesignFontImportCatalog::ResolveKnownCssFontWeights(Family, RequestedWeightsByFamily);
			if (bKnownCssFamily
				&& !ExistingFontPath.IsEmpty()
				&& !IsEngineFallbackFontPath(ExistingFontPath)
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
				(bKnownCssFamily || bKnownDirectFamily) && IsEngineFallbackFontPath(ExistingFontPath);
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
				ImportedPath = ResolveFont(Family, *KnownUrl);
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

	const TArray<int32> RequestedWeights = DesignFontImportCatalog::ResolveKnownCssFontWeights(FontFamily, TMap<FString, TSet<int32>>());
	FString ImportedPath;
	if (DesignFontImportCatalog::GetKnownCssFontFamilies().Contains(NormalizedFamily))
	{
		ImportedPath = ImportKnownCssFontFamily(FontFamily, RequestedWeights);
	}
	else if (const FString* KnownUrl = DesignFontImportCatalog::GetKnownWebFontUrls().Find(NormalizedFamily))
	{
		ImportedPath = ResolveFont(FontFamily, *KnownUrl);
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

bool DesignFontImportService::CanReusePinnedAutomationFontPath(const FString& AutomationFontImportPath)
{
	return DesignFontImportAutomationPaths::CanReusePinnedFontImportPath(AutomationFontImportPath);
}

void DesignFontImportService::ResetTransientState()
{
	GKnownIconGlyphCache.Reset();
	GKnownIconNameCache.Reset();
}

#undef LOCTEXT_NAMESPACE
