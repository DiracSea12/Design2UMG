// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "DesignLocalFontImport.h"
#include "DesignFontImportPolicy.h"
#include "PencilFontProcess.h"
#include "Design2UMGFontAccess.h"
#include "ExternalServices/PencilExtensionResources.h"
#include "Design2UMGConverter.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "Design2UMGLog.h"
#include "FontResolver.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontCache.h"
#include "Misc/SecureHash.h"
#include "Internationalization/TextChar.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IHttpRequest.h"
#include "Misc/EngineVersionComparison.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"

THIRD_PARTY_INCLUDES_START
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_MULTIPLE_MASTERS_H
#include FT_TRUETYPE_TABLES_H
THIRD_PARTY_INCLUDES_END

namespace DesignLocalFontImport
{
namespace
{
	bool DownloadFont(const FString& Url, TArray<uint8>& OutBytes, FString& OutError)
	{
		struct FTransfer
		{
			FCriticalSection Mutex;
			TArray<uint8> Bytes;
			FString Error;
			TAtomic<bool> bComplete{false};
		};
		const auto Transfer = MakeShared<FTransfer, ESPMode::ThreadSafe>();
		auto& Http = FHttpModule::Get();
		const auto Request = Http.CreateRequest();
		Request->SetURL(Url);
		Request->SetVerb(TEXT("GET"));
		Request->SetTimeout(DesignFontImportPolicy::DownloadTimeoutSeconds);
		const auto Receive = [Transfer](void* Data, int64 Length)
		{
			FScopeLock Lock(&Transfer->Mutex);
			if (!DesignFontImportPolicy::FitsByteLimit(Length, Transfer->Bytes.Num()))
			{
				Transfer->Error = FString::Printf(TEXT("Font response exceeds the %d MiB font limit."), DesignFontImportPolicy::MaxFontBytes / (1024 * 1024));
				return false;
			}
			Transfer->Bytes.Append(static_cast<const uint8*>(Data), static_cast<int32>(Length));
			return true;
		};
#if UE_VERSION_OLDER_THAN(5, 5, 0)
		const bool bStreaming = Request->SetResponseBodyReceiveStreamDelegate(FHttpRequestStreamDelegate::CreateLambda(Receive));
#else
		const bool bStreaming = Request->SetResponseBodyReceiveStreamDelegateV2(
			FHttpRequestStreamDelegateV2::CreateLambda([Receive](void* Data, int64& Length)
			{
				if (!Receive(Data, Length)) Length = 0;
			}));
#endif
		Request->OnProcessRequestComplete().BindLambda([Transfer](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
		{
			FScopeLock Lock(&Transfer->Mutex);
			const int32 Status = Response ? Response->GetResponseCode() : 0;
			if (Transfer->Error.IsEmpty() && (!bSuccess || Status < 200 || Status >= 300 || Transfer->Bytes.IsEmpty()))
				Transfer->Error = FString::Printf(TEXT("Icon font transfer failed (HTTP %d)."), Status);
			Transfer->bComplete.Store(true);
		});
		if (!bStreaming || !Request->ProcessRequest())
		{
			OutError = TEXT("Could not start streamed font request: ") + Url;
			return false;
		}
		FScopedSlowTask Progress(1, NSLOCTEXT("Design2UMG", "FontDownload", "Downloading font..."));

		const double Deadline = FPlatformTime::Seconds() + DesignFontImportPolicy::DownloadTimeoutSeconds;
		while (!Transfer->bComplete.Load())
		{
			Progress.EnterProgressFrame(0);
			Http.GetHttpManager().Tick(0.01f);
			if (Transfer->bComplete.Load()) break;
			const bool bCancelled = Progress.ShouldCancel();
			if (bCancelled || FPlatformTime::Seconds() >= Deadline)
			{
				Request->CancelRequest();
				OutError = bCancelled ? TEXT("Icon font transfer cancelled.") : TEXT("Icon font transfer timed out.");
				return false;
			}
			FPlatformProcess::Sleep(0.01f);
		}
		FScopeLock Lock(&Transfer->Mutex);
		OutError = Transfer->Error;
		if (!OutError.IsEmpty()) return false;
		OutBytes = MoveTemp(Transfer->Bytes);
		return true;
	}

	bool MakeTemporaryFontDirectory(FString& Directory, FString& OutError)
	{
		Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir()
			/ TEXT("Design2UMG/FontImport") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (IFileManager::Get().MakeDirectory(*Directory, true)) return true;
		OutError = TEXT("Could not create temporary font directory: ") + Directory;
		return false;
	}

	int32 ResolveSourceWeight(const FFace& Source, int32 RequestedWeight)
	{
		return Source.WeightRange.IsSet()
			? FMath::Clamp(RequestedWeight, FMath::CeilToInt32(Source.WeightRange->X), FMath::FloorToInt32(Source.WeightRange->Y))
			: Source.Weight;
	}

	bool MatchesStaticStyle(const FFace& Face, int32 Weight, bool bItalic)
	{
		return Face.Weight == Weight && Face.bItalic == bItalic;
	}

	bool ReadStaticSourceFace(const FFace& Source, int32 RequestedWeight, FFace& OutFace,
		FString& TemporaryDirectory, FString& OutError)
	{
		if (!MakeTemporaryFontDirectory(TemporaryDirectory, OutError)) return false;
		const FString Output = TemporaryDirectory / TEXT("converted.ttf");
		const int32 ExpectedWeight = ResolveSourceWeight(Source, RequestedWeight);
		if (!PencilFontProcess::ConvertToStaticFont(FPaths::ConvertRelativePathToFull(Source.Path), Output,
			RequestedWeight, ExpectedWeight, OutError) || !ReadFace(Output, OutFace, OutError)) return false;
		if (!MatchesStaticStyle(OutFace, ExpectedWeight, Source.bItalic) || OutFace.Family != Source.Family)
		{
			OutError = TEXT("Converted local font identity does not match its source: ") + Source.Path;
			return false;
		}
		return true;
	}

	bool ReadFontStyle(const PencilExtensionResources::FFontStyle& Style, int32 SourceWeight,
		FFace& OutFace, FString& TemporaryDirectory, FString& OutError)
	{
		const bool NeedsConversion = Style.bVariable || Style.Url.EndsWith(TEXT(".woff2"), ESearchCase::IgnoreCase);
		if (!NeedsConversion && !Style.Url.EndsWith(TEXT(".ttf"), ESearchCase::IgnoreCase)
			&& !Style.Url.EndsWith(TEXT(".otf"), ESearchCase::IgnoreCase))
		{
			OutError = TEXT("Unsupported installed font format: ") + Style.Url;
			return false;
		}
		TArray<uint8> Bytes;
		if (!DownloadFont(Style.Url, Bytes, OutError)) return false;
		if (!MakeTemporaryFontDirectory(TemporaryDirectory, OutError)) return false;
		const FString InputPath = TemporaryDirectory / TEXT("source.font");
		if (!FFileHelper::SaveArrayToFile(Bytes, *InputPath))
		{
			OutError = TEXT("Could not write temporary source font: ") + InputPath;
			return false;
		}
		const int32 ExpectedWeight = Style.ResolveWeight(SourceWeight);
		FString FontPath = InputPath;
		if (NeedsConversion)
		{
			FontPath = TemporaryDirectory / TEXT("converted.ttf");
			if (!PencilFontProcess::ConvertToStaticFont(InputPath, FontPath, SourceWeight, ExpectedWeight, OutError))
				return false;
		}
		if (!ReadFace(FontPath, OutFace, OutError)) return false;
		if (!MatchesStaticStyle(OutFace, ExpectedWeight, Style.bItalic))
		{
			OutError = TEXT("Downloaded font identity does not match the declared icon style: ") + Style.Url;
			return false;
		}
		return true;
	}

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

	bool IsLocalDeclaration(const FDesignFont& Font, const FString& Family)
	{
		return !Font.Url.IsEmpty() && IdentityKey(Font.Name) == IdentityKey(Family)
			&& !Font.Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
			&& !Font.Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
	}

	FString Typeface(const FFace& Face)
	{
		return FFontResolver::TypefaceNameForExactWeight(Face.Weight)
			+ (Face.bItalic ? TEXT(" Italic") : TEXT(""));
	}

	struct FRequest
	{
		FString Family;
		FString PostScript;
		int32 Weight = 400;
		bool bItalic = false;
		bool bIcon = false;
		TArray<FDesignPreparedFont*> PreparedFonts;
	};

	void Collect(const TArray<TSharedPtr<FDesignNode>>& Nodes, TArray<FRequest>& Requests)
	{
		auto Add = [&Requests](const FString& Family, const FString& PostScript, const FString& Weight, const FString& Style,
			FDesignPreparedFont* Prepared = nullptr, bool bIcon = false)
		{
			if (Prepared) *Prepared = FDesignPreparedFont();
			if (Family.IsEmpty()) return;
			FRequest Request{Family, PostScript, bIcon ? FCString::Atoi(*Weight) : FFontResolver::ParseWeightValue(Weight),
				Style.Contains(TEXT("italic"), ESearchCase::IgnoreCase) || Style.Contains(TEXT("oblique"), ESearchCase::IgnoreCase), bIcon};
			FRequest* Existing = Requests.FindByPredicate([&](const FRequest& Candidate)
				{ return Candidate.Family == Request.Family && Candidate.PostScript == Request.PostScript
					&& Candidate.Weight == Request.Weight && Candidate.bItalic == Request.bItalic && Candidate.bIcon == Request.bIcon; });
			if (!Existing) Existing = &Requests.Add_GetRef(MoveTemp(Request));
			if (Prepared) Existing->PreparedFonts.Add(Prepared);
		};
		for (const auto& Node : Nodes)
		{
			if (!Node) continue;
			auto& Text = Node->Text;
			Add(Text.FontFamily, Text.FontPostScriptName, Text.FontWeight, Text.FontStyle, &Text.PreparedFont);
			if (!Text.IconFontFamily.IsEmpty()) Add(Text.IconFontFamily, FString(), FString::FromInt(Text.IconWeight), TEXT("normal"), nullptr, true);
			for (auto& Run : Text.TextRuns)
			{
				Add(Run.FontFamily.IsEmpty() ? Text.FontFamily : Run.FontFamily,
					Run.FontPostScriptName.IsEmpty() && Run.FontFamily.IsEmpty() ? Text.FontPostScriptName : Run.FontPostScriptName,
					Run.FontWeight.IsEmpty() ? Text.FontWeight : Run.FontWeight,
					Run.FontStyle.IsEmpty() ? Text.FontStyle : Run.FontStyle, &Run.PreparedFont);
			}
			Collect(Node->Children, Requests);
		}
	}

	bool ContainsFontStyle(const FCompositeFont& Composite, int32 Weight, bool bItalic)
	{
		const FName Name(*(FFontResolver::TypefaceNameForExactWeight(Weight) + (bItalic ? TEXT(" Italic") : TEXT(""))));
		return Composite.DefaultTypeface.Fonts.ContainsByPredicate([&](const FTypefaceEntry& Entry)
		{
			return Entry.Name == Name && Entry.Font.HasFont();
		});
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

	bool IsConfiguredFontAsset(const FString& ObjectPath, const FDesign2UMGConversionSettings& Settings)
	{
		const FSoftObjectPath Path(ObjectPath);
		if (Path.IsNull()) return false;
		if (FSoftObjectPath(Settings.DefaultFontAssetPath) == Path || FSoftObjectPath(Settings.DefaultIconFontAssetPath) == Path) return true;
		for (const auto& Pair : Settings.ExplicitFontMappings)
			if (Pair.Value.ToSoftObjectPath() == Path) return true;
		for (const auto& Pair : Settings.IconFontAssetPaths)
			if (FSoftObjectPath(Pair.Value) == Path) return true;
		return false;
	}

	bool SaveFontPackage(UFont& Font, FString& OutError)
	{
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
			Package->SetDirtyFlag(bWasDirty);
			if (bCreatedDirectory) IFileManager::Get().DeleteDirectory(*Directory, false, false);
			OutError = TEXT("Failed to save local font: ") + Package->GetName();
			return false;
		}
		return true;
	}

	bool SaveEmbeddedFont(UFont& Font, bool bSaveRequired, FString& OutError)
	{
		TMap<UFontFace*, EFontLoadingPolicy> PreviousPolicies;
		// New faces are already Inline; adding a source face must preserve the other faces' policies.
		if (!bSaveRequired) ForEachObjectWithOuter(&Font, [&](UObject* Object)
		{
			UFontFace* Face = Cast<UFontFace>(Object);
			if (IsValid(Face) && !Face->HasAnyFlags(RF_Transient) && Face->LoadingPolicy != EFontLoadingPolicy::Inline)
			{
				PreviousPolicies.Add(Face, Face->LoadingPolicy);
				Face->LoadingPolicy = EFontLoadingPolicy::Inline;
			}
		}, false);
		if (!bSaveRequired && PreviousPolicies.IsEmpty()) return true;
		if (SaveFontPackage(Font, OutError)) return true;
		for (const auto& Policy : PreviousPolicies) Policy.Key->LoadingPolicy = Policy.Value;
		return false;
	}

	bool RemoveAutomaticFontFallback(UFont& Font, const FDesign2UMGConversionSettings& Settings, FString& OutError)
	{
		const FString Path = Font.GetPathName();
		if (!Path.StartsWith(TEXT("/Game/Design2UMG/Fonts/")) || IsConfiguredFontAsset(Path, Settings)) return true;
		auto& Composite = Design2UMGFontAccess::GetMutableCompositeFont(Font);
		if (Composite.FallbackTypeface.Typeface.Fonts.IsEmpty()) return true;
		const FCompositeFallbackFont Previous = Composite.FallbackTypeface;
		FSlateFontCache& Cache = *FSlateApplication::Get().GetRenderer()->GetFontCache();
		Composite.FallbackTypeface = FCompositeFallbackFont();
		Composite.MakeDirty();
		// Native typeface selection caches pointers separately from revision-aware character and measure caches.
		Cache.FlushCompositeFont(Composite);
		if (SaveFontPackage(Font, OutError)) return true;
		Composite.FallbackTypeface = Previous;
		Composite.MakeDirty();
		Cache.FlushCompositeFont(Composite);
		return false;
	}

	bool HasImportedFace(const UFont& Font, const FName Name, const TArray<uint8>& Bytes)
	{
		if (Font.FontCacheType != EFontCacheType::Runtime) return false;
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
		if (Font.RuntimeFontSource != ERuntimeFontSource::Asset) return false;
#endif
		const auto* Entry = Font.GetCompositeFont()->DefaultTypeface.Fonts.FindByPredicate(
			[Name](const FTypefaceEntry& Candidate) { return Candidate.Name == Name; });
		const UFontFace* Face = Entry ? Cast<UFontFace>(Entry->Font.GetFontFaceAsset()) : nullptr;
		return Face && Face->FontFaceData->GetData() == Bytes;
	}

	bool BuildGlyphFallbackFonts(const FDesignDocument& Targets, const FDesign2UMGFontMappings& FontMappings, TArray<FSlateFontInfo>& DocumentFonts,
		TMap<FFontData, FString>& FontDataHashes, TArray<FString>& OutWarnings, FString& OutError)
	{
		FSlateFontCache& Cache = *FSlateApplication::Get().GetRenderer()->GetFontCache();
		const auto ForEachText = [&Targets](TFunctionRef<bool(const FString&, const FString&, const FString&,
			const FString&, const FString&, FDesignPreparedFont&)> Handle)
		{
			TFunction<bool(const TArray<TSharedPtr<FDesignNode>>&)> Visit;
			Visit = [&](const TArray<TSharedPtr<FDesignNode>>& Nodes)
			{
				for (const auto& Node : Nodes)
				{
					if (!Node) continue;
					auto& Text = Node->Text;
					if (!Handle(Text.Content, Text.FontFamily, Text.FontPostScriptName, Text.FontWeight,
						Text.FontStyle, Text.PreparedFont)) return false;
					for (auto& Run : Text.TextRuns)
					{
						if (!Handle(Run.Content, Run.FontFamily.IsEmpty() ? Text.FontFamily : Run.FontFamily,
							Run.FontPostScriptName.IsEmpty() && Run.FontFamily.IsEmpty() ? Text.FontPostScriptName : Run.FontPostScriptName,
							Run.FontWeight.IsEmpty() ? Text.FontWeight : Run.FontWeight,
							Run.FontStyle.IsEmpty() ? Text.FontStyle : Run.FontStyle, Run.PreparedFont)) return false;
					}
					if (!Visit(Node->Children)) return false;
				}
				return true;
			};
			return Visit(Targets.Children) && Visit(Targets.SourceDocument().ImportedNodes);
		};
		// Only fonts actually resolved for this import supply glyphs through their own native composite selection.
		ForEachText([&](const FString&, const FString& Family, const FString& PostScript,
			const FString& Weight, const FString& Style, FDesignPreparedFont& Prepared)
		{
			const auto Selection = FFontResolver::ResolveFontSelection(Family, PostScript, Weight, Style, &FontMappings, &Prepared);
			const UFont* Font = Selection.FontAssetPath.IsEmpty() ? nullptr : LoadObject<UFont>(nullptr, *Selection.FontAssetPath);
			if (!Font || Font->FontCacheType != EFontCacheType::Runtime) return true;
			DocumentFonts.AddUnique(FSlateFontInfo(Font, 14, FName(*Selection.TypefaceName)));
			return true;
		});
		TMap<const UFont*, FString> FontIdentities;
		TMap<FString, TSet<int32>> MissingByFont;
		const auto PrepareText = [&](const FString& Content, const FString& Family, const FString& PostScript,
			const FString& Weight, const FString& Style, FDesignPreparedFont& Prepared)
		{
			Prepared.AssetPath.Reset();
			if (Content.IsEmpty()) return true;
			const auto Selection = FFontResolver::ResolveFontSelection(Family, PostScript, Weight, Style, &FontMappings, &Prepared);
			UFont* Original = Selection.FontAssetPath.IsEmpty() ? nullptr : LoadObject<UFont>(nullptr, *Selection.FontAssetPath);
			if (!Original || Original->FontCacheType != EFontCacheType::Runtime) return true;
			const FSlateFontInfo Info(Original, 14, FName(*Selection.TypefaceName));
			if (Prepared.DeclarationKey != FFontResolver::MakeFontDeclarationKey(Family, PostScript, Weight, Style)
				|| Prepared.SourceAssetPath != Original->GetPathName() || Prepared.TypefaceName != Info.TypefaceFontName)
				Prepared.DeclarationKey.Reset();
			Prepared.SourceAssetPath = Original->GetPathName();
			Prepared.TypefaceName = Info.TypefaceFontName;
			TMap<int32, TPair<FFontData, float>> Supplements;
			const FTCHARToUTF32 Converted(*Content);
			for (int32 Index = 0; Index < Converted.Length(); ++Index)
			{
				const int32 Codepoint = static_cast<int32>(Converted.Get()[Index]);
				if (!FTextChar::IsPrint(Codepoint) || Supplements.Contains(Codepoint)) continue;
				float Scale = 1.0f;
				const FFontData& Selected = Cache.GetFontDataForCodepoint(Info, Codepoint, Scale);
				if (Cache.CanLoadCodepoint(Selected, Codepoint, EFontFallback::FF_NoFallback)) continue;
				for (const FSlateFontInfo& DonorInfo : DocumentFonts)
				{
					float DonorScale = 1.0f;
					const FFontData& Donor = Cache.GetFontDataForCodepoint(DonorInfo, Codepoint, DonorScale);
					const UFontFace* Face = Cast<UFontFace>(Donor.GetFontFaceAsset());
					if (Face && Face->FontFaceData->HasData() && Cache.CanLoadCodepoint(Donor, Codepoint, EFontFallback::FF_NoFallback))
					{
						Supplements.Add(Codepoint, TPair<FFontData, float>(Donor, DonorScale));
						break;
					}
				}
				if (!Supplements.Contains(Codepoint))
					MissingByFont.FindOrAdd(Original->GetPathName() + TEXT(" [") + Info.TypefaceFontName.ToString() + TEXT("]")).Add(Codepoint);
			}
			if (Supplements.IsEmpty()) return true;
			TArray<int32> Codepoints;
			Supplements.GetKeys(Codepoints);
			Codepoints.Sort();
			FString* OriginalIdentity = FontIdentities.Find(Original);
			if (!OriginalIdentity)
			{
				const FString Filename = FPackageName::LongPackageNameToFilename(
					Original->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
				OriginalIdentity = &FontIdentities.Add(Original, Original->GetPathName() + TEXT("|")
					+ LexToString(FMD5Hash::HashFile(*Filename)));
			}
			FString Identity = *OriginalIdentity + TEXT("|") + Info.TypefaceFontName.ToString();
			for (const int32 Codepoint : Codepoints)
			{
				const auto& Supplement = Supplements[Codepoint];
				const FFontData& Donor = Supplement.Key;
				FString* Hash = FontDataHashes.Find(Donor);
				if (!Hash)
				{
					const auto Data = Donor.GetFontFaceData();
					Hash = &FontDataHashes.Add(Donor, FMD5::HashBytes(Data->GetData().GetData(), Data->GetData().Num()));
				}
				Identity += FString::Printf(TEXT("|%x:%s:%s"), Codepoint, *Donor.GetFontFaceAsset()->GetPathName(), **Hash);
				if (Supplement.Value != 1.0f)
					Identity += FString::Printf(TEXT("|scale:%.9g"), static_cast<double>(Supplement.Value));
			}
			const FTCHARToUTF8 Utf8Identity(*Identity);
			const FString AssetName = TEXT("GlyphFallback_") + FMD5::HashBytes(
				reinterpret_cast<const uint8*>(Utf8Identity.Get()), Utf8Identity.Length());
			const FString PackageName = TEXT("/Game/Design2UMG/Fonts/GlyphFallbacks/") + AssetName;
			const FString ObjectPath = PackageName + TEXT(".") + AssetName;
			if (UFont* Existing = LoadObject<UFont>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn))
			{
				Prepared.AssetPath = Existing->GetPathName();
				return true;
			}
			if (FPackageName::DoesPackageExist(PackageName) || FindObject<UObject>(nullptr, *ObjectPath))
			{
				OutError = TEXT("Glyph fallback destination is occupied: ") + ObjectPath;
				return false;
			}
			UPackage* Package = CreatePackage(*PackageName);
			UFont* Font = NewObject<UFont>(Package, *AssetName, RF_Public | RF_Standalone);
			Font->FontCacheType = EFontCacheType::Runtime;
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
			Font->RuntimeFontSource = ERuntimeFontSource::Asset;
#endif
			auto& Composite = Design2UMGFontAccess::GetMutableCompositeFont(*Font);
			Composite = *Original->GetCompositeFont();
			const int32 OriginalSubfontCount = Composite.SubTypefaces.Num();
			TMap<FFontData, TMap<float, int32>> SubfontByDonor;
			for (const int32 Codepoint : Codepoints)
			{
				const FInt32Range Range = FInt32Range::Inclusive(Codepoint, Codepoint);
				// Ranges must not overlap: a selected original subfont may itself lack this glyph.
				for (int32 Index = 0; Index < OriginalSubfontCount; ++Index)
				{
					FCompositeSubFont& OriginalSubfont = Composite.SubTypefaces[Index];
					TArray<FInt32Range> Remaining;
					for (const FInt32Range& OriginalRange : OriginalSubfont.CharacterRanges)
						Remaining.Append(FInt32Range::Difference(OriginalRange, Range));
					OriginalSubfont.CharacterRanges = MoveTemp(Remaining);
				}
				const auto& Supplement = Supplements[Codepoint];
				TMap<float, int32>& SubfontsByScale = SubfontByDonor.FindOrAdd(Supplement.Key);
				const int32* ExistingIndex = SubfontsByScale.Find(Supplement.Value);
				const int32 SubfontIndex = ExistingIndex ? *ExistingIndex : Composite.SubTypefaces.AddDefaulted();
				FCompositeSubFont& Subfont = Composite.SubTypefaces[SubfontIndex];
				if (!ExistingIndex)
				{
					SubfontsByScale.Add(Supplement.Value, SubfontIndex);
					Subfont.ScalingFactor = Supplement.Value;
					FTypefaceEntry Entry(Info.TypefaceFontName);
					Entry.Font = Supplement.Key;
					Subfont.Typeface.Fonts.Add(MoveTemp(Entry));
				}
				Subfont.CharacterRanges.Add(Range);
			}
			Composite.MakeDirty();
			if (!SaveEmbeddedFont(*Font, true, OutError))
			{
				Font->ClearFlags(RF_Public | RF_Standalone);
				Font->Rename(*MakeUniqueObjectName(GetTransientPackage(), UFont::StaticClass()).ToString(),
					GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
				Font->MarkAsGarbage();
				Package->SetDirtyFlag(false);
				return false;
			}
			FAssetRegistryModule::AssetCreated(Font);
			Prepared.AssetPath = Font->GetPathName();
			return true;
		};
		if (!ForEachText(PrepareText)) return false;
		TArray<FString> MissingFonts;
		MissingByFont.GetKeys(MissingFonts);
		MissingFonts.Sort();
		for (const FString& Font : MissingFonts)
		{
			TArray<int32> Codepoints = MissingByFont[Font].Array();
			Codepoints.Sort();
			FString Glyphs;
			TArray<FString> Codes;
			for (const int32 Codepoint : Codepoints)
			{
				(void)FTextChar::AppendCodepointToString(Codepoint, Glyphs);
				Codes.Add(FString::Printf(TEXT("U+%04X"), Codepoint));
			}
			const FString Warning = FString::Printf(TEXT("字体缺字：%s 缺少字符“%s”（%s），稿内已导入的字体也无法补字。请为设计稿补充支持这些文字的字体；本次未从稿外自动补充字体。"),
				*Font, *Glyphs, *FString::Join(Codes, TEXT(", ")));
			if (!OutWarnings.Contains(Warning))
			{
				OutWarnings.Add(Warning);
				UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Warning);
			}
		}
		return true;
	}

}

namespace
{
	bool ReadLocalFontFaceMetadata(const FString& Path, FFace& OutFace, FString& OutError, bool bDirectoryDiscovery)
	{
		if (IFileManager::Get().FileSize(*Path) > DesignFontImportPolicy::MaxFontBytes)
		{
			OutError = FString::Printf(TEXT("Local font exceeds the %d MiB font limit: %s"),
				DesignFontImportPolicy::MaxFontBytes / (1024 * 1024), *Path);
			return false;
		}
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
		if (Face->num_faces != 1)
		{
			OutError = TEXT("Font collections require a single-face export: ") + Path;
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
		OutFace.bVariable = FT_HAS_MULTIPLE_MASTERS(Face);
		if (OutFace.bVariable)
		{
			FT_MM_Var* Variation = nullptr;
			if (FT_Get_MM_Var(Face, &Variation) || !Variation)
			{
				OutError = TEXT("Could not read variable font axes: ") + Path;
				return false;
			}
			ON_SCOPE_EXIT { FT_Done_MM_Var(Library, Variation); };
			for (FT_UInt Index = 0; Index < Variation->num_axis; ++Index)
			{
				const FT_Var_Axis& Axis = Variation->axis[Index];
				if (Axis.tag == FT_MAKE_TAG('w', 'g', 'h', 't'))
					OutFace.WeightRange = FVector2D(Axis.minimum / 65536.0, Axis.maximum / 65536.0);
			}
		}
		if (bDirectoryDiscovery)
		{
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
		}
		if (OutFace.Family.IsEmpty() || OutFace.Weight < 100 || OutFace.Weight > 900)
		{
			OutError = TEXT("Font has no usable family or static weight: ") + Path;
			return false;
		}
		return true;
	}
}

bool ReadFace(const FString& Path, FFace& OutFace, FString& OutError)
{
	if (!ReadLocalFontFaceMetadata(Path, OutFace, OutError, false)) return false;
	if (OutFace.bVariable)
	{
		OutError = TEXT("Variable fonts require a static single-face export: ") + Path;
		return false;
	}
	return true;
}

bool ImportFace(const FFace& Face, const FString& PackageName, FString& OutError, const FString& TypefaceName, const FString& RegisteredFamily)
{
	check(IsInGameThread());
	const FString& RegistrationFamily = RegisteredFamily.IsEmpty() ? Face.Family : RegisteredFamily;
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
	if (HasImportedFace(*Font, Name, Bytes))
	{
		if (!SaveEmbeddedFont(*Font, false, OutError)) return false;
		FFontResolver::RegisterFontAssetPath(RegistrationFamily, Font->GetPathName());
		return true;
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
	if (!RegistrationFamily.IsEmpty()) FFontResolver::RegisterFontAssetPath(RegistrationFamily, Font->GetPathName());
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
			if (GWarn && GWarn->ReceivedUserCancel()) return Faces;
			const FString Extension = FPaths::GetExtension(File).ToLower();
			if (Extension != TEXT("ttf") && Extension != TEXT("otf") && Extension != TEXT("ttc") && Extension != TEXT("woff2")) continue;
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
			if (Extension == TEXT("woff2")
				? PencilFontProcess::InspectFont(File, Face, Error)
				: ReadLocalFontFaceMetadata(File, Face, Error, true))
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

bool PrepareGlyphFallbacks(const FDesignDocument& Targets, const FDesign2UMGFontMappings& FontMappings,
	TArray<FSlateFontInfo>& DocumentFonts, TMap<FFontData, FString>& FontDataHashes, TArray<FString>& OutWarnings, FString& OutError)
{
	check(IsInGameThread());
	return BuildGlyphFallbackFonts(Targets, FontMappings, DocumentFonts, FontDataHashes, OutWarnings, OutError);
}

bool Prepare(const FDesignDocument& Document, const FDesign2UMGConversionSettings& Settings,
	TArray<FString>& OutWarnings, FString& OutError, FSourceIndex* SourceIndex, bool bDeclaredOnly)
{
	check(IsInGameThread());
	const FDesign2UMGFontMappings* FontMappings = &Settings.ExplicitFontMappings;
	TArray<FRequest> Requests;
	Collect(Document.Children, Requests);
	Collect(Document.SourceDocument().ImportedNodes, Requests);
	const bool bHasIcons = Requests.ContainsByPredicate([](const FRequest& Request) { return Request.bIcon; });
	for (int32 Index = 0; Index < Requests.Num();)
	{
		const FRequest& Request = Requests[Index];
		const bool bHasDeclaration = Document.Fonts.ContainsByPredicate([&](const FDesignFont& Font)
			{ return IsLocalDeclaration(Font, Request.Family); });
		if (Request.bIcon)
		{
			bool bConfigured = !Settings.DefaultIconFontAssetPath.IsEmpty();
			for (const auto& Pair : Settings.IconFontAssetPaths)
				bConfigured |= Pair.Key.Equals(Request.Family, ESearchCase::IgnoreCase) && !Pair.Value.IsEmpty();
			if (bConfigured)
			{
				Requests.RemoveAt(Index);
				continue;
			}
		}
		if (FFontResolver::HasExplicitFontMapping(Request.Family, Request.PostScript, FontMappings))
		{
			Requests.RemoveAt(Index);
			continue;
		}
		const FString Path = FFontResolver::FindFontAssetPath(Request.Family, Request.PostScript, FontMappings);
		if (IsConfiguredFontAsset(Path, Settings))
		{
			Requests.RemoveAt(Index);
			continue;
		}
		const bool bAutomaticFont = Path.StartsWith(TEXT("/Game/Design2UMG/Fonts/"));
		UFont* Font = !Path.StartsWith(TEXT("/Game/")) ? nullptr : LoadObject<UFont>(nullptr, *Path);
		if (Font && !RemoveAutomaticFontFallback(*Font, Settings, OutError)) return false;
		if (bDeclaredOnly && !bHasDeclaration)
		{
			Requests.RemoveAt(Index);
			continue;
		}
		if (bHasDeclaration)
		{
			++Index;
			continue;
		}
		const FCompositeFont* Composite = Font ? Font->GetCompositeFont() : nullptr;
		if (!Composite)
		{
			++Index;
			continue;
		}
		bool bHasStyle = ContainsFontStyle(*Composite, Request.Weight, Request.bItalic);
		if (!bHasStyle && bAutomaticFont)
		{
			PencilExtensionResources::FFontStyle Style;
			if (PencilExtensionResources::FindFontStyle(Request.Family, Request.Weight, Request.bItalic, Style))
				bHasStyle = ContainsFontStyle(*Composite, Style.ResolveWeight(Request.Weight), Request.bItalic);
		}
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

	for (const FRequest& Request : Requests)
	{
		if (GWarn && GWarn->ReceivedUserCancel())
		{
			OutError = TEXT("Font preparation cancelled.");
			return false;
		}
		TArray<FFace> DeclaredFaces;
		for (const FDesignFont& Font : Document.Fonts)
		{
			if (!IsLocalDeclaration(Font, Request.Family)) continue;
			FFace Face;
			FString Error;
			const bool bRead = FPaths::GetExtension(Font.Url).Equals(TEXT("woff2"), ESearchCase::IgnoreCase)
				? PencilFontProcess::InspectFont(Font.Url, Face, Error)
				: ReadLocalFontFaceMetadata(Font.Url, Face, Error, false);
			if (!bRead)
			{
				OutError = FString::Printf(TEXT("Declared font source failed: family='%s', weight=%d, italic=%s, source='%s'. %s"),
					*Request.Family, Request.Weight, Request.bItalic ? TEXT("true") : TEXT("false"), *Font.Url, *Error);
				return false;
			}
			DeclaredFaces.Add(MoveTemp(Face));
		}
		const bool bDeclared = !DeclaredFaces.IsEmpty();
		const TArray<FFace>& Faces = bDeclared ? DeclaredFaces : (SourceIndex ? *SourceIndex : LocalIndex).GetFaces();
		if (!bDeclared && FFontResolver::FindFontAssetPath(Request.Family, Request.PostScript, FontMappings).IsEmpty()
			&& FFontResolver::HasAmbiguousProjectFontAssets(Request.Family))
		{
			const FString Warning = TEXT("Local font import skipped: multiple project fonts match family '") + Request.Family
				+ TEXT("'; select an explicit font mapping.");
			OutWarnings.AddUnique(Warning);
			UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Warning);
			continue;
		}
		const FFace* Match = nullptr;
		if (bDeclared)
		{
			// Prefer italic agreement, then nearest available weight; preserve declaration order for ties.
			for (const FFace& Face : DeclaredFaces)
			{
				if (!Request.PostScript.IsEmpty() && !Face.PostScriptName.Equals(Request.PostScript, ESearchCase::IgnoreCase)) continue;
				if (!Match || (Face.bItalic == Request.bItalic && Match->bItalic != Request.bItalic)
					|| (Face.bItalic == Match->bItalic
						&& FMath::Abs(ResolveSourceWeight(Face, Request.Weight) - Request.Weight)
							< FMath::Abs(ResolveSourceWeight(*Match, Request.Weight) - Request.Weight)))
					Match = &Face;
			}
		}
		else
		{
			Match = Faces.FindByPredicate([&](const FFace& Face)
			{
				return IdentityKey(Face.Family) == IdentityKey(Request.Family)
					&& !Face.bVariable && Face.Weight == Request.Weight && Face.bItalic == Request.bItalic
					&& (Request.PostScript.IsEmpty() || Face.PostScriptName.Equals(Request.PostScript, ESearchCase::IgnoreCase));
			});
			if (!Match)
			{
				Match = Faces.FindByPredicate([&](const FFace& Face)
				{
					return Face.bVariable && IdentityKey(Face.Family) == IdentityKey(Request.Family)
						&& Face.bItalic == Request.bItalic
						&& (Request.PostScript.IsEmpty() || Face.PostScriptName.Equals(Request.PostScript, ESearchCase::IgnoreCase));
				});
			}
		}
		if (bDeclared && !Match)
		{
			OutError = FString::Printf(TEXT("Declared font has no matching face: family='%s', weight=%d, italic=%s, PostScript='%s', source='%s'."),
				*Request.Family, Request.Weight, Request.bItalic ? TEXT("true") : TEXT("false"),
				*Request.PostScript, *DeclaredFaces[0].Path);
			return false;
		}
		FFace DownloadedFace;
		FString TemporaryDirectory;
		FString FontError;
		ON_SCOPE_EXIT
		{
			// This unique directory contains only the downloaded font and this conversion's files.
			if (!TemporaryDirectory.IsEmpty() && IFileManager::Get().DirectoryExists(*TemporaryDirectory)
				&& !IFileManager::Get().DeleteDirectory(*TemporaryDirectory, false, true))
				UE_LOG(LogDesign2UMG, Warning, TEXT("Could not remove temporary font conversion files: %s"), *TemporaryDirectory);
		};
		if (Match && Match->bVariable && !bDeclared)
		{
			// The local source's real axis clamp may differ from the originally requested style.
			const FString ExistingPath = FFontResolver::FindFontAssetPath(Request.Family, Request.PostScript, FontMappings);
			UFont* Existing = ExistingPath.StartsWith(TEXT("/Game/Design2UMG/Fonts/"))
				? LoadObject<UFont>(nullptr, *ExistingPath) : nullptr;
			const FCompositeFont* Composite = Existing ? Existing->GetCompositeFont() : nullptr;
			if (Composite && ContainsFontStyle(*Composite, ResolveSourceWeight(*Match, Request.Weight), Request.bItalic))
				continue;
		}
		const bool bHasLocalSource = Match != nullptr;
		FString SourceDescription = Match ? Match->Path : FString();
		const auto FormatSourceFailure = [&](const FString& Reason)
		{
			return FString::Printf(TEXT("Font source failed: family='%s', weight=%d, italic=%s, PostScript='%s', source='%s'. %s"),
				*Request.Family, Request.Weight, Request.bItalic ? TEXT("true") : TEXT("false"),
				*Request.PostScript, *SourceDescription, *Reason);
		};
		if (Match && (Match->bVariable || FPaths::GetExtension(Match->Path).Equals(TEXT("woff2"), ESearchCase::IgnoreCase)))
		{
			Match = ReadStaticSourceFace(*Match, Request.Weight, DownloadedFace, TemporaryDirectory, FontError)
				? &DownloadedFace : nullptr;
		}
		if (!Match && !bHasLocalSource)
		{
			PencilExtensionResources::FFontStyle Style;
			if (PencilExtensionResources::FindFontStyle(Request.Family, Request.Weight, Request.bItalic, Style))
			{
				SourceDescription = Style.Url;
				if (ReadFontStyle(Style, Request.Weight, DownloadedFace, TemporaryDirectory, FontError))
					Match = &DownloadedFace;
			}
		}
		if (!Match)
		{
			if (GWarn && GWarn->ReceivedUserCancel())
			{
				OutError = FontError;
				return false;
			}
			if (!SourceDescription.IsEmpty())
			{
				OutError = FormatSourceFailure(FontError);
				return false;
			}
			const FString Warning = FString::Printf(TEXT("Local font unavailable: family='%s', weight=%d, italic=%s, PostScript='%s'. %s"),
				*Request.Family, Request.Weight, Request.bItalic ? TEXT("true") : TEXT("false"), *Request.PostScript,
				FontError.IsEmpty() ? TEXT("No matching static face in local font directories; configured fallback remains in use.") : *FontError);
			OutWarnings.AddUnique(Warning);
			UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Warning);
			continue;
		}
		FString PackageName = bDeclared ? FFontResolver::MakeImportedFamilyPackageName(Request.Family)
			: FFontResolver::FindFontAssetPath(Request.Family, Request.PostScript, FontMappings);
		if (PackageName.StartsWith(TEXT("/Game/"))) PackageName = FPackageName::ObjectPathToPackageName(PackageName);
		else
		{
			PackageName = FFontResolver::MakeImportedFamilyPackageName(Request.Family);
		}
		const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
		UFont* ExistingFont = LoadObject<UFont>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn);
		if (ExistingFont && IsConfiguredFontAsset(ObjectPath, Settings))
		{
			OutError = TEXT("Configured font destination was left unchanged: ") + ObjectPath;
			return false;
		}
		if (ExistingFont && !RemoveAutomaticFontFallback(*ExistingFont, Settings, OutError)) return false;
		TArray<uint8> SourceBytes;
		if (ExistingFont && FFileHelper::LoadFileToArray(SourceBytes, *Match->Path)
			&& HasImportedFace(*ExistingFont, FName(*Typeface(*Match)), SourceBytes))
		{
			// Source preparation reuses identical faces without changing their loading policies.
			FFontResolver::RegisterFontAssetPath((Request.bIcon || bDeclared) ? Request.Family : Match->Family, ObjectPath);
		}
		else if (!ImportFace(*Match, PackageName, OutError, FString(), (Request.bIcon || bDeclared) ? Request.Family : FString()))
		{
			OutError = FormatSourceFailure(OutError);
			return false;
		}
		if (bDeclared)
		{
			const FString Key = FFontResolver::MakeFontDeclarationKey(Request.Family, Request.PostScript,
				FString::FromInt(Request.Weight), Request.bItalic ? TEXT("italic") : TEXT("normal"));
			const FString AssetPath = PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
			for (FDesignPreparedFont* Prepared : Request.PreparedFonts)
				*Prepared = {AssetPath, FName(*Typeface(*Match)), FString(), Key};
		}
	}
	if (bHasIcons) PencilExtensionResources::EnsureIconGlyphsRegistered();
	return true;
}
}
