// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "FontResolver.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "Design2UMGSettings.h"
#include "PenSchema.h"
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

#define LOCTEXT_NAMESPACE "FontResolver"

TMap<FString, FString> FFontResolver::FontAssetCache;

FString FFontResolver::FindFontAssetPath(const FString& FontFamily)
{
	if (const FString* Path = FontAssetCache.Find(FontFamily))
		return *Path;
	return FString();
}

void FFontResolver::ClearCache()
{
	FontAssetCache.Empty();
}

TArray<FString> FFontResolver::FindMissingFonts(const FPenDocument& Document)
{
	TSet<FString> UsedFamilies;

	// Collect from Document.Fonts[]
	for (const FPenFont& Font : Document.Fonts)
	{
		if (!Font.Name.IsEmpty())
			UsedFamilies.Add(Font.Name);
	}

	// Collect from node tree
	CollectFontFamilies(Document.Children, UsedFamilies);

	if (UsedFamilies.Num() == 0)
		return {};

	// Scan project font assets
	TSet<FString> ProjectFonts = ScanProjectFontAssets();

	// Find missing
	TArray<FString> Missing;
	for (const FString& Family : UsedFamilies)
	{
		if (!ProjectFonts.Contains(Family))
			Missing.Add(Family);
	}

	if (Missing.Num() > 0)
	{
		FString List = FString::Join(Missing, TEXT(", "));
		UE_LOG(LogDesign2UMG, Warning, TEXT("缺失字体: %s"), *List);
		FDesign2UMGNotification::Warning(
			FText::Format(LOCTEXT("MissingFonts", "Design2UMG: {0} font(s) not found in project: {1}"),
				FText::AsNumber(Missing.Num()), FText::FromString(List)));
	}

	return Missing;
}

void FFontResolver::CollectFontFamilies(const TArray<TSharedPtr<FPenNode>>& Nodes, TSet<FString>& OutFamilies)
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

	FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::FullFlush);
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
		FontAssetCache.Add(FontName, FontObjPath);
		return FontObjPath;
	}

	// Download
	bool bIsRemote = Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
		|| Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);

	TArray<uint8> FontData;
	if (bIsRemote)
	{
		FString TempPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / SafeName + TEXT(".ttf");
		if (!DownloadToFile(Url, TempPath))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("字体下载失败: %s (%s)"), *FontName, *Url);
			return FString();
		}
		FFileHelper::LoadFileToArray(FontData, *TempPath);
		IFileManager::Get().Delete(*TempPath);
	}
	else
	{
		if (!FFileHelper::LoadFileToArray(FontData, *Url))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("字体文件读取失败: %s (%s)"), *FontName, *Url);
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
	FTypefaceEntry& Entry = Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
	Entry.Name = "Default";
	Entry.Font = FFontData(FontFace);
	FontPkg->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Font);

	// Save both packages
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

	FString FaceFile = FPackageName::LongPackageNameToFilename(FacePkgName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FaceFile), true);
	UPackage::SavePackage(FacePkg, FontFace, *FaceFile, SaveArgs);

	FString FontFile = FPackageName::LongPackageNameToFilename(FontPkgName, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(FontPkg, Font, *FontFile, SaveArgs);

	UE_LOG(LogDesign2UMG, Log, TEXT("Imported font '%s' → %s"), *FontName, *FontObjPath);
	FontAssetCache.Add(FontName, FontObjPath);
	return FontObjPath;
}

void FFontResolver::AutoImportFonts(const FPenDocument& Document)
{
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!Settings || !Settings->bAutoImportFonts)
		return;

	int32 Imported = 0;
	for (const FPenFont& Font : Document.Fonts)
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
