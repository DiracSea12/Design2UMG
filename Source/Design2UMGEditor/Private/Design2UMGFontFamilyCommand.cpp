// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGLog.h"
#include "FontResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Font.h"
#include "Fonts/CompositeFont.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	void RunBuildFontFamilyCommand(const TArray<FString>& Args)
	{
		// Console input is whitespace-split before it reaches us; re-join the
		// tokens so FParse::Value can honor quoted values (Ttf="C:/My Fonts/F.ttf")
		// the same way the command line does.
		const FString ArgLine = FString::Join(Args, TEXT(" "));

		FString FontPackageName;
		FString TypefaceName;
		FString TtfPath;
		const bool bHaveFont = FParse::Value(*ArgLine, TEXT("Font="), FontPackageName);
		const bool bHaveTypeface = FParse::Value(*ArgLine, TEXT("Typeface="), TypefaceName);
		const bool bHaveTtf = FParse::Value(*ArgLine, TEXT("Ttf="), TtfPath);

		if (!bHaveFont || FontPackageName.IsEmpty() ||
			!bHaveTypeface || TypefaceName.IsEmpty() ||
			!bHaveTtf || TtfPath.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.BuildFontFamily Font=/Game/Path/MyFont Typeface=SemiBold Ttf=\"C:/Absolute/Font.ttf\""));
			return;
		}

		if (!IFileManager::Get().FileExists(*TtfPath))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("BuildFontFamily: TTF file not found: %s"), *TtfPath);
			return;
		}

		const FString FontAssetName = FPackageName::GetLongPackageAssetName(FontPackageName);

		UPackage* Package = nullptr;
		UFont* Font = Cast<UFont>(StaticLoadObject(
			UFont::StaticClass(), nullptr, *(FontPackageName + TEXT(".") + FontAssetName)));
		const bool bCreatedNew = Font == nullptr;
		if (Font)
		{
			Package = Font->GetOutermost();
		}
		else
		{
			Package = CreatePackage(*FontPackageName);
			Font = Package ? NewObject<UFont>(Package, FName(*FontAssetName), RF_Public | RF_Standalone) : nullptr;
		}

		if (!Font || !Package)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("BuildFontFamily: failed to create or load font asset '%s'"), *FontPackageName);
			return;
		}

		Font->FontCacheType = EFontCacheType::Runtime;

		const FName TypefaceKey(*TypefaceName);
		const FTypefaceEntry NewEntry(
			TypefaceKey,
			TtfPath,
			EFontHinting::Default,
			EFontLoadingPolicy::LazyLoad);

		FCompositeFont& CompositeFont = Font->GetMutableInternalCompositeFont();
		if (FTypefaceEntry* Existing = CompositeFont.DefaultTypeface.Fonts.FindByPredicate(
			[TypefaceKey](const FTypefaceEntry& Entry) { return Entry.Name == TypefaceKey; }))
		{
			*Existing = NewEntry;
		}
		else
		{
			CompositeFont.DefaultTypeface.Fonts.Add(NewEntry);
		}

		if (bCreatedNew)
		{
			FAssetRegistryModule::AssetCreated(Font);
		}

		Package->MarkPackageDirty();

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			FontPackageName,
			FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFileName), true);
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, Font, *PackageFileName, SaveArgs);

		UE_LOG(LogDesign2UMG, Display,
			TEXT("BuildFontFamily: %s font '%s' with typeface '%s' -> '%s' (save %s)"),
			bCreatedNew ? TEXT("created") : TEXT("updated"),
			*FontPackageName, *TypefaceName, *TtfPath,
			bSaved ? TEXT("succeeded") : TEXT("failed"));

		if (bSaved)
		{
			// Newly written assets must be visible to in-process font resolution.
			FFontResolver::ClearCache();
		}
	}

	FAutoConsoleCommand GBuildFontFamilyCommand(
		TEXT("Design2UMG.BuildFontFamily"),
		TEXT("Create or update a composite UFont family asset: Design2UMG.BuildFontFamily Font=/Game/Path/MyFont Typeface=SemiBold Ttf=\"C:/Absolute/Font.ttf\""),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunBuildFontFamilyCommand));
}
