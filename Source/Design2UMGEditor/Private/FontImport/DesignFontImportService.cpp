// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignFontImportService.h"

#include "Design2UMGLog.h"
#include "Design2UMGNotifications.h"
#include "Design2UMGSettings.h"
#include "DesignFontImportAutomationPaths.h"
#include "DesignFontImportCatalog.h"
#include "DesignFontImportServiceInternal.h"
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

namespace DesignFontImportServiceInternal
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

} // namespace DesignFontImportServiceInternal

bool DesignFontImportService::CanReusePinnedAutomationFontPath(const FString& AutomationFontImportPath)
{
	return DesignFontImportAutomationPaths::CanReusePinnedFontImportPath(AutomationFontImportPath);
}

void DesignFontImportService::ResetTransientState()
{
	DesignFontImportServiceInternal::GKnownIconGlyphCache.Reset();
	DesignFontImportServiceInternal::GKnownIconNameCache.Reset();
}

#undef LOCTEXT_NAMESPACE
