// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "DesignLocalFontImport.h"
#include "Design2UMGFontAccess.h"
#include "Design2UMGSettings.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "Design2UMGLog.h"
#include "FontResolver.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"

THIRD_PARTY_INCLUDES_START
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_TABLES_H
THIRD_PARTY_INCLUDES_END

namespace DesignLocalFontImport
{
namespace
{
	FString ReadName(FT_Face Face, const FT_UShort NameId)
	{
		// OpenType Unicode/Windows names are UTF-16BE. Prefer English when localized records exist.
		FString Result;
		for (FT_UInt Index = 0; Index < FT_Get_Sfnt_Name_Count(Face); ++Index)
		{
			FT_SfntName Name{};
			if (FT_Get_Sfnt_Name(Face, Index, &Name) || Name.name_id != NameId
				|| (Name.platform_id != 0 && Name.platform_id != 3) || Name.string_len % 2 != 0) continue;
			FString Value;
			for (FT_UInt Byte = 0; Byte < Name.string_len; Byte += 2)
			{
				Value.AppendChar(static_cast<TCHAR>((Name.string[Byte] << 8) | Name.string[Byte + 1]));
			}
			if (Result.IsEmpty() || Name.language_id == 0x409) Result = MoveTemp(Value);
			if (Name.language_id == 0x409) break;
		}
		return Result;
	}

	FString IdentityKey(FString Value)
	{
		Value.TrimStartAndEndInline();
		return Value.ToLower();
	}

	FString Typeface(const FFace& Face)
	{
		return FFontResolver::TypefaceNameForWeightValue(Face.Weight)
			+ (Face.bItalic ? TEXT(" Italic") : TEXT(""));
	}

	struct FRequest
	{
		FString Family;
		FString PostScript;
		int32 Weight = 400;
		bool bItalic = false;
	};

	void Collect(const TArray<TSharedPtr<FDesignNode>>& Nodes, TArray<FRequest>& Requests)
	{
		auto Add = [&Requests](const FString& Family, const FString& PostScript, const FString& Weight, const FString& Style)
		{
			if (Family.IsEmpty()) return;
			FRequest Request{Family, PostScript, FFontResolver::ParseWeightValue(Weight),
				Style.Contains(TEXT("italic"), ESearchCase::IgnoreCase) || Style.Contains(TEXT("oblique"), ESearchCase::IgnoreCase)};
			if (!Requests.ContainsByPredicate([&](const FRequest& Existing)
				{ return Existing.Family == Request.Family && Existing.PostScript == Request.PostScript
					&& Existing.Weight == Request.Weight && Existing.bItalic == Request.bItalic; }))
			{
				Requests.Add(MoveTemp(Request));
			}
		};
		for (const auto& Node : Nodes)
		{
			if (!Node) continue;
			const auto& Text = Node->Text;
			Add(Text.FontFamily, Text.FontPostScriptName, Text.FontWeight, Text.FontStyle);
			if (!Text.IconFontFamily.IsEmpty()) Add(Text.IconFontFamily, FString(), FString::FromInt(Text.IconWeight), TEXT("normal"));
			for (const auto& Run : Text.TextRuns)
			{
				Add(Run.FontFamily.IsEmpty() ? Text.FontFamily : Run.FontFamily,
					Run.FontPostScriptName.IsEmpty() && Run.FontFamily.IsEmpty() ? Text.FontPostScriptName : Run.FontPostScriptName,
					Run.FontWeight.IsEmpty() ? Text.FontWeight : Run.FontWeight,
					Run.FontStyle.IsEmpty() ? Text.FontStyle : Run.FontStyle);
			}
			Collect(Node->Children, Requests);
		}
	}

	TArray<FString> SourceRoots()
	{
		TArray<FString> Roots;
		if (const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG")))
		{
			Roots.Add(Plugin->GetBaseDir() / TEXT("Fonts"));
		}
		Roots.Add(FPaths::ProjectSavedDir() / TEXT("Design2UMG/ManualFonts"));
		Roots.Add(FPaths::ProjectSavedDir() / TEXT("Design2UMG/figma-font-candidates"));
		Roots.Add(FPaths::EngineContentDir() / TEXT("Slate/Fonts"));
		const FString Windows = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
		if (!Windows.IsEmpty()) Roots.Add(Windows / TEXT("Fonts"));
		const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		if (!LocalAppData.IsEmpty()) Roots.Add(LocalAppData / TEXT("Microsoft/Windows/Fonts"));
		return Roots;
	}

	bool SaveEmbeddedFont(UFont& Font, bool bSaveRequired, FString& OutError)
	{
		TMap<UFontFace*, EFontLoadingPolicy> PreviousPolicies;
		ForEachObjectWithOuter(&Font, [&](UObject* Object)
		{
			UFontFace* Face = Cast<UFontFace>(Object);
			if (IsValid(Face) && !Face->HasAnyFlags(RF_Transient) && Face->LoadingPolicy != EFontLoadingPolicy::Inline)
			{
				PreviousPolicies.Add(Face, Face->LoadingPolicy);
				Face->LoadingPolicy = EFontLoadingPolicy::Inline;
			}
		}, false);
		if (!bSaveRequired && PreviousPolicies.IsEmpty()) return true;
		UPackage* Package = Font.GetOutermost();
		const bool bWasDirty = Package->IsDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		const FString Directory = FPaths::GetPath(Filename);
		const bool bCreatedDirectory = !IFileManager::Get().DirectoryExists(*Directory);
		IFileManager::Get().MakeDirectory(*Directory, true);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, &Font, *Filename, Args))
		{
			for (const auto& Policy : PreviousPolicies) Policy.Key->LoadingPolicy = Policy.Value;
			Package->SetDirtyFlag(bWasDirty);
			if (bCreatedDirectory) IFileManager::Get().DeleteDirectory(*Directory, false, false);
			OutError = TEXT("Failed to save local font: ") + Package->GetName();
			return false;
		}
		return true;
	}

}

bool ReadFace(const FString& Path, FFace& OutFace, FString& OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		OutError = TEXT("Could not read font file: ") + Path;
		return false;
	}
	FT_Library Library = nullptr;
	if (FT_Init_FreeType(&Library))
	{
		OutError = TEXT("FreeType initialization failed");
		return false;
	}
	ON_SCOPE_EXIT { FT_Done_FreeType(Library); };
	FT_Face Face = nullptr;
	if (FT_New_Memory_Face(Library, Bytes.GetData(), Bytes.Num(), 0, &Face))
	{
		OutError = TEXT("Invalid font file: ") + Path;
		return false;
	}
	ON_SCOPE_EXIT { FT_Done_Face(Face); };
	if (Face->num_faces != 1 || FT_HAS_MULTIPLE_MASTERS(Face))
	{
		OutError = TEXT("Font collections and variable fonts require a static single-face export: ") + Path;
		return false;
	}
	OutFace = FFace();
	OutFace.Path = Path;
	OutFace.Family = ReadName(Face, 16); // Typographic family groups weights that legacy family names split.
	if (OutFace.Family.IsEmpty()) OutFace.Family = ReadName(Face, 1);
	if (OutFace.Family.IsEmpty() && Face->family_name) OutFace.Family = UTF8_TO_TCHAR(Face->family_name);
	if (const char* PostScript = FT_Get_Postscript_Name(Face)) OutFace.PostScriptName = UTF8_TO_TCHAR(PostScript);
	const TT_OS2* OS2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(Face, ft_sfnt_os2));
	if (OS2 && OS2->usWidthClass != 5)
	{
		OutError = TEXT("Font width variant has no matching source width declaration: ") + Path;
		return false;
	}
	OutFace.Weight = OS2 ? OS2->usWeightClass : ((Face->style_flags & FT_STYLE_FLAG_BOLD) ? 700 : 400);
	OutFace.bItalic = (Face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
	FString StyleName = ReadName(Face, 17);
	if (StyleName.IsEmpty()) StyleName = ReadName(Face, 2);
	StyleName = StyleName.ToLower().Replace(TEXT("italic"), TEXT("")).Replace(TEXT("oblique"), TEXT(""))
		.Replace(TEXT(" "), TEXT("")).Replace(TEXT("-"), TEXT(""));
	const TMap<FString, int32> NamedWeights = {
		{TEXT("thin"), 100}, {TEXT("extralight"), 200}, {TEXT("ultralight"), 200},
		{TEXT("light"), 300}, {TEXT("regular"), 400}, {TEXT("normal"), 400}, {TEXT("book"), 400},
		{TEXT("medium"), 500}, {TEXT("semibold"), 600}, {TEXT("demibold"), 600},
		{TEXT("bold"), 700}, {TEXT("extrabold"), 800}, {TEXT("ultrabold"), 800},
		{TEXT("black"), 900}, {TEXT("heavy"), 900}};
	if (const int32* NamedWeight = NamedWeights.Find(StyleName); NamedWeight && *NamedWeight != OutFace.Weight)
	{
		OutError = TEXT("Font weight metadata contradicts its named style: ") + Path;
		return false;
	}
	if (OutFace.Family.IsEmpty() || OutFace.Weight < 100 || OutFace.Weight > 900 || OutFace.Weight % 100 != 0)
	{
		OutError = TEXT("Font has no usable family or standard static weight: ") + Path;
		return false;
	}
	return true;
}

bool ImportFace(const FFace& Face, const FString& PackageName, FString& OutError, const FString& TypefaceName)
{
	check(IsInGameThread());
	TArray<uint8> Bytes;
	if (!PackageName.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackageName)
		|| !FFileHelper::LoadFileToArray(Bytes, *Face.Path))
	{
		OutError = TEXT("Invalid font destination or unreadable font: ") + PackageName + TEXT(" <- ") + Face.Path;
		return false;
	}
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	UFont* Font = LoadObject<UFont>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn);
	const bool bNew = !Font;
	if (bNew && (FPackageName::DoesPackageExist(PackageName)
		|| FindObject<UObject>(nullptr, *(PackageName + TEXT(".") + AssetName))))
	{
		OutError = TEXT("Font destination already exists but is not a loadable UFont: ") + PackageName;
		return false;
	}
	UPackage* Package = Font ? Font->GetOutermost() : CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = TEXT("Could not create font package: ") + PackageName;
		return false;
	}
	if (!Font) Font = NewObject<UFont>(Package, *AssetName, RF_Public | RF_Standalone);
	const EFontCacheType OldCacheType = Font->FontCacheType;
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
	const ERuntimeFontSource OldSource = Font->RuntimeFontSource;
#endif
	auto& Composite = Design2UMGFontAccess::GetMutableCompositeFont(*Font);
	const FCompositeFont Previous = Composite;
	const bool bWasDirty = Package->IsDirty();
	const FString ResolvedTypeface = TypefaceName.IsEmpty() ? Typeface(Face) : TypefaceName;
	const FName Name(*ResolvedTypeface);
	if (const auto* Existing = Composite.DefaultTypeface.Fonts.FindByPredicate([&](const FTypefaceEntry& Item) { return Item.Name == Name; }))
	{
		const UFontFace* ExistingFace = Cast<UFontFace>(Existing->Font.GetFontFaceAsset());
		if (OldCacheType == EFontCacheType::Runtime
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
			&& OldSource == ERuntimeFontSource::Asset
#endif
			&& ExistingFace && ExistingFace->FontFaceData->GetData() == Bytes)
		{
			if (!SaveEmbeddedFont(*Font, false, OutError)) return false;
			FFontResolver::RegisterFontAssetPath(Face.Family, Font->GetPathName());
			return true;
		}
	}
	const FName FaceName = MakeUniqueObjectName(Font, UFontFace::StaticClass(), FName(*(ResolvedTypeface.Replace(TEXT(" "), TEXT("")) + TEXT("Face"))));
	UFontFace* Embedded = NewObject<UFontFace>(Font, FaceName, RF_Public);
	// Embedded faces stay in their owning font package instead of sharing streamed sidecar names.
	Embedded->LoadingPolicy = EFontLoadingPolicy::Inline;
	Embedded->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(Bytes));
	FTypefaceEntry Entry(Name);
	Entry.Font = FFontData(Embedded);
	if (auto* Existing = Composite.DefaultTypeface.Fonts.FindByPredicate([&](const FTypefaceEntry& Item) { return Item.Name == Name; }))
	{
		*Existing = MoveTemp(Entry);
	}
	else Composite.DefaultTypeface.Fonts.Add(MoveTemp(Entry));
	Font->FontCacheType = EFontCacheType::Runtime;
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
	Font->RuntimeFontSource = ERuntimeFontSource::Asset;
#endif
	Composite.MakeDirty();
	if (!SaveEmbeddedFont(*Font, true, OutError))
	{
		Composite = Previous;
		Font->FontCacheType = OldCacheType;
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
		Font->RuntimeFontSource = OldSource;
#endif
		Embedded->ClearFlags(RF_Public | RF_Standalone);
		Embedded->MarkAsGarbage();
		if (bNew)
		{
			Font->ClearFlags(RF_Public | RF_Standalone);
			// Free the asset path before an immediate retry, without forcing garbage collection.
			Font->Rename(*MakeUniqueObjectName(GetTransientPackage(), UFont::StaticClass()).ToString(),
				GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			Font->MarkAsGarbage();
		}
		Package->SetDirtyFlag(bWasDirty);
		return false;
	}
	if (bNew) FAssetRegistryModule::AssetCreated(Font);
	if (!Face.Family.IsEmpty()) FFontResolver::RegisterFontAssetPath(Face.Family, Font->GetPathName());
	UE_LOG(LogDesign2UMG, Log, TEXT("Local font imported: %s / %s <- %s"), *PackageName, *ResolvedTypeface, *Face.Path);
	return true;
}

const TArray<FFace>& FSourceIndex::GetFaces()
{
	check(IsInGameThread());
	if (bLoaded) return Faces;
	// Keep only parsed metadata, never font bytes or imported assets. Each new index
	// still discovers files and checks timestamps/sizes, so additions and edits are visible.
	struct FCachedFace
	{
		FFileStatData Stat;
		FFace Face;
	};
	static TMap<FString, FCachedFace> MetadataCache;
	TSet<FString> Seen;
	for (const FString& Root : SourceRoots())
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*"), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			const FString Extension = FPaths::GetExtension(File).ToLower();
			if (Extension != TEXT("ttf") && Extension != TEXT("otf") && Extension != TEXT("ttc")) continue;
			Seen.Add(File);
			const FFileStatData Stat = IFileManager::Get().GetStatData(*File);
			const FCachedFace* Cached = MetadataCache.Find(File);
			if (Stat.bIsValid && Cached && Cached->Stat.ModificationTime == Stat.ModificationTime
				&& Cached->Stat.FileSize == Stat.FileSize)
			{
				Faces.Add(Cached->Face);
				continue;
			}
			MetadataCache.Remove(File);
			FFace Face;
			FString Error;
			if (ReadFace(File, Face, Error))
			{
				if (Stat.bIsValid) MetadataCache.Add(File, {Stat, Face});
				Faces.Add(MoveTemp(Face));
			}
			else UE_LOG(LogDesign2UMG, Verbose, TEXT("%s"), *Error);
		}
	}
	for (auto It = MetadataCache.CreateIterator(); It; ++It)
		if (!Seen.Contains(It.Key())) It.RemoveCurrent();
	bLoaded = true;
	return Faces;
}

bool Prepare(const FDesignDocument& Document, TArray<FString>& OutWarnings, FString& OutError, FSourceIndex* SourceIndex)
{
	check(IsInGameThread());
	TArray<FRequest> Requests;
	Collect(Document.Children, Requests);
	Collect(Document.SourceDocument().ImportedReusableNodes, Requests);
	for (int32 Index = 0; Index < Requests.Num();)
	{
		const FRequest& Request = Requests[Index];
		const FString Path = FFontResolver::FindFontAssetPath(Request.Family, Request.PostScript);
		const bool bAutomaticFont = Path.StartsWith(TEXT("/Game/Design2UMG/Fonts/"));
		UFont* Font = !Path.StartsWith(TEXT("/Game/")) ? nullptr : LoadObject<UFont>(nullptr, *Path);
		if (bAutomaticFont && Font && !SaveEmbeddedFont(*Font, false, OutError)) return false;
		if (FFontResolver::HasExplicitFontMapping(Request.Family, Request.PostScript))
		{
			Requests.RemoveAt(Index);
			continue;
		}
		const FCompositeFont* Composite = Font ? Font->GetCompositeFont() : nullptr;
		if (!Composite)
		{
			++Index;
			continue;
		}
		const FName Expected(*(FFontResolver::TypefaceNameForWeightValue(Request.Weight) + (Request.bItalic ? TEXT(" Italic") : TEXT(""))));
		const bool bHasStyle = Composite->DefaultTypeface.Fonts.ContainsByPredicate([&](const FTypefaceEntry& Entry)
		{
			return Entry.Name == Expected && Entry.Font.HasFont();
		});
		if (!bHasStyle && bAutomaticFont)
		{
			++Index;
			continue;
		}
		if (!bHasStyle)
		{
			const FString Warning = FString::Printf(
				TEXT("Project font '%s' is outside the automatic font import directory and was left unchanged; requested family '%s', weight=%d, italic=%s is missing. Add that style to the font or explicitly map a complete font asset."),
				*Path, *Request.Family, Request.Weight, Request.bItalic ? TEXT("true") : TEXT("false"));
			OutWarnings.AddUnique(Warning);
			UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Warning);
		}
		Requests.RemoveAt(Index);
	}
	if (Requests.IsEmpty()) return true;
	FSourceIndex LocalIndex;
	const TArray<FFace>& Faces = (SourceIndex ? *SourceIndex : LocalIndex).GetFaces();

	for (const FRequest& Request : Requests)
	{
		const auto Diagnostic = FFontResolver::ResolveFontDiagnostic(Request.Family, Request.PostScript,
			FString::FromInt(Request.Weight), Request.bItalic ? TEXT("italic") : TEXT("normal"), false);
		if (Diagnostic.ResolutionState == TEXT("ambiguous"))
		{
			const FString Warning = TEXT("Local font import skipped: multiple project fonts match family '") + Request.Family
				+ TEXT("'; select an explicit font mapping.");
			OutWarnings.AddUnique(Warning);
			UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Warning);
			continue;
		}
		const FFace* Match = Faces.FindByPredicate([&](const FFace& Face)
		{
			return IdentityKey(Face.Family) == IdentityKey(Request.Family)
				&& Face.Weight == Request.Weight && Face.bItalic == Request.bItalic
				&& (Request.PostScript.IsEmpty() || Face.PostScriptName.Equals(Request.PostScript, ESearchCase::IgnoreCase));
		});
		if (!Match)
		{
			const FString Warning = FString::Printf(TEXT("Local font unavailable: family='%s', weight=%d, italic=%s, PostScript='%s'. No matching static face in local font directories; configured fallback remains in use."),
				*Request.Family, Request.Weight, Request.bItalic ? TEXT("true") : TEXT("false"), *Request.PostScript);
			OutWarnings.AddUnique(Warning);
			UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Warning);
			continue;
		}
		FString PackageName = FFontResolver::FindFontAssetPath(Request.Family, Request.PostScript);
		if (PackageName.StartsWith(TEXT("/Game/"))) PackageName = FPackageName::ObjectPathToPackageName(PackageName);
		else
		{
			PackageName = FFontResolver::MakeImportedFamilyPackageName(Request.Family);
		}
		if (!ImportFace(*Match, PackageName, OutError)) return false;
	}
	return true;
}
}
