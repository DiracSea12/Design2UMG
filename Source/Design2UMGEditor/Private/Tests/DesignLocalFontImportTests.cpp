// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Design2UMGFontAccess.h"
#include "Design2UMGConverter.h"
#include "DesignImportDialogs.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "Misc/SecureHash.h"
#include "DesignImportPlanning.h"
#include "UMGTextStyleApplication.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "Components/RichTextBlock.h"
#include "Engine/DataTable.h"
#include "DesignLocalFontImport.h"
#include "PencilFontTool.h"
#include "Interfaces/IPluginManager.h"
#include "DesignImportPipeline.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "Design2UMGSettings.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "FontResolver.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/AutomationTest.h"
#include "Tests/Design2UMGFileFailureFixture.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/FontCache.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "ObjectTools.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool SaveFontPolicyFixture(FAutomationTestBase& Test, UFont& Font)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Font.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return Test.TestTrue(TEXT("Save the existing-font policy fixture"),
			UPackage::SavePackage(Font.GetOutermost(), &Font, *Filename, Args));
	}

	UFontFace* AddFallbackFixture(FAutomationTestBase& Test, UFont& Font)
	{
		TArray<uint8> Bytes;
		if (!Test.TestTrue(TEXT("Read real supplementary fixture bytes"), FFileHelper::LoadFileToArray(
			Bytes, *(FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf"))))) return nullptr;
		UFontFace* Face = NewObject<UFontFace>(&Font,
			MakeUniqueObjectName(&Font, UFontFace::StaticClass()), RF_Public);
		Face->LoadingPolicy = EFontLoadingPolicy::Inline;
		Face->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(Bytes));
		auto& Composite = Design2UMGFontAccess::GetMutableCompositeFont(Font);
		FTypefaceEntry Entry(FName(TEXT("Regular")));
		Entry.Font = FFontData(Face);
		Composite.FallbackTypeface.Typeface.Fonts = {Entry};
		Composite.FallbackTypeface.ScalingFactor = 0.9f;
		Composite.MakeDirty();
		FSlateApplication::Get().GetRenderer()->GetFontCache()->FlushCompositeFont(Composite);
		return Face;
	}

	void DeleteFontFixture(FAutomationTestBase& Test, const FString& Package, bool bRemoveDirectory = true)
	{
		FFontResolver::ClearCache();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
		const FString ObjectPath = Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
		if (UFont* Font = FindObject<UFont>(nullptr, *ObjectPath))
		{
			Test.TestTrue(TEXT("Delete only the test font and release its package"), ObjectTools::ForceDeleteObjects({Font}, false) > 0);
		}
		Test.TestFalse(TEXT("Test font file is removed"), IFileManager::Get().FileExists(*Filename));
		if (bRemoveDirectory)
		{
			const FString Directory = FPaths::GetPath(Filename);
			if (IFileManager::Get().DirectoryExists(*Directory))
			{
				Test.TestTrue(TEXT("Remove the empty test font directory"), IFileManager::Get().DeleteDirectory(*Directory, false, false));
			}
			Test.TestFalse(TEXT("Test font directory is removed"), IFileManager::Get().DirectoryExists(*Directory));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocalFontPackageIdentityTest,
	"Design2UMG.Fonts.LocalSource.PackageIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalFontPackageIdentityTest::RunTest(const FString&)
{
	const FString First = FFontResolver::MakeImportedFamilyPackageName(TEXT("Family/A"));
	const FString Second = FFontResolver::MakeImportedFamilyPackageName(TEXT("Family:A"));
	TestTrue(TEXT("Distinct families that sanitize identically have different destinations"), First != Second);
	TestTrue(TEXT("Font package is a valid long package name"), FPackageName::IsValidLongPackageName(First));
	TestEqual(TEXT("The same family resolves deterministically"), First, FFontResolver::MakeImportedFamilyPackageName(TEXT("Family/A")));
	TestTrue(TEXT("Literal underscore name remains distinct"), First != FFontResolver::MakeImportedFamilyPackageName(TEXT("Family_A")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocalFontRediscoveryTest,
	"Design2UMG.Fonts.LocalSource.FamilyRediscovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalFontRediscoveryTest::RunTest(const FString&)
{
	DesignLocalFontImport::FFace Face;
	FString Error;
	const FString Source = FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf");
	if (!TestTrue(TEXT("Read font data"), DesignLocalFontImport::ReadFace(Source, Face, Error))) return false;
	TMap<FString, UFont*> Imported;
	ON_SCOPE_EXIT
	{
		for (const auto& Pair : Imported)
		{
			DeleteFontFixture(*this, FFontResolver::MakeImportedFamilyPackageName(Pair.Key), false);
		}
	};
	const FString Prefix = TEXT("D2UIdentity") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	for (const FString Suffix : {FString(TEXT("/Family")), FString(TEXT(":Family"))})
	{
		const FString RequestedFamily = Prefix + Suffix;
		const FString Package = FFontResolver::MakeImportedFamilyPackageName(RequestedFamily);
		if (!TestTrue(TEXT("Import separate family"), DesignLocalFontImport::ImportFace(Face, Package, Error, FString(), RequestedFamily))) return false;
		UFont* Font = LoadObject<UFont>(nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
		if (!TestNotNull(TEXT("Imported family asset exists"), Font)) return false;
		Imported.Add(RequestedFamily, Font);
		const UFontFace* Embedded = Cast<UFontFace>(Font->GetCompositeFont()->DefaultTypeface.Fonts[0].Font.GetFontFaceAsset());
		if (!TestNotNull(TEXT("Imported family has embedded font data"), Embedded)) return false;
		TestEqual(TEXT("Embedded faces are cooked inline without shared sidecar filenames"), Embedded->LoadingPolicy, EFontLoadingPolicy::Inline);
	}
	FFontResolver::ClearCache();
	for (const auto& Pair : Imported)
	{
		TestEqual(TEXT("Family resolves independently after all transient registrations are cleared"),
			FFontResolver::FindFontAssetPath(Pair.Key), Pair.Value->GetPathName());
		TestFalse(TEXT("Recognized imported family is not ambiguous"),
			FFontResolver::ResolveFontDiagnostic(Pair.Key, FString(), TEXT("700"), TEXT("normal"), false).ResolutionState == TEXT("ambiguous"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignLocalFontIndexLifetimeTest,
	"Design2UMG.Fonts.LocalSource.IndexLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesignLocalFontIndexLifetimeTest::RunTest(const FString&)
{
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/ManualFonts");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString AddedPath = Directory / (TEXT("IndexLifetime_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ttf"));
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*AddedPath); };
	DesignLocalFontImport::FSourceIndex CurrentImport;
	const int32 OriginalCount = CurrentImport.GetFaces().Num();
	int32 MetadataReads = 0;
	TSet<FString> ReadPaths;
	Design2UMGEditorTests::FScopedFileFailures Files;
	Files.OnRead = [&](const FString& Path)
	{
		const FString Extension = FPaths::GetExtension(Path).ToLower();
		if (Extension == TEXT("ttf") || Extension == TEXT("otf") || Extension == TEXT("ttc"))
		{
			++MetadataReads;
			ReadPaths.Add(Path);
		}
	};
	DesignLocalFontImport::FSourceIndex UnchangedImport;
	TestEqual(TEXT("Unchanged import sees same font set"), UnchangedImport.GetFaces().Num(), OriginalCount);
	for (const auto& Face : UnchangedImport.GetFaces())
		TestFalse(TEXT("Cached font bytes are not read again"), ReadPaths.Contains(FPaths::ConvertRelativePathToFull(Face.Path)));
	// Rejected/unreadable files are retried rather than negatively cached.
	const int32 RetriedReads = MetadataReads;
	TestEqual(TEXT("Copy font after the first scan"), IFileManager::Get().Copy(*AddedPath,
		*(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"))), COPY_OK);
	TestEqual(TEXT("Same import reuses its index, not a second directory scan"), CurrentImport.GetFaces().Num(), OriginalCount);
	MetadataReads = 0;
	DesignLocalFontImport::FSourceIndex NextImport;
	TestTrue(TEXT("Next import discovers added font"), NextImport.GetFaces().ContainsByPredicate(
		[&](const DesignLocalFontImport::FFace& Face) { return FPaths::IsSamePath(Face.Path, AddedPath); }));
	TestEqual(TEXT("Only the added font adds a metadata read"), MetadataReads, RetriedReads + 1);
	const FString Replacement = FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf");
	if (!TestEqual(TEXT("Replace font at same path"), IFileManager::Get().Copy(*AddedPath, *Replacement), COPY_OK)) return false;
	MetadataReads = 0;
	DesignLocalFontImport::FSourceIndex ReplacedImport;
	TestTrue(TEXT("Changed file refreshes metadata"), ReplacedImport.GetFaces().ContainsByPredicate(
		[&](const DesignLocalFontImport::FFace& Face) { return FPaths::IsSamePath(Face.Path, AddedPath) && Face.Weight == 700; }));
	TestEqual(TEXT("Only changed file adds a metadata read"), MetadataReads, RetriedReads + 1);
	IFileManager::Get().SetTimeStamp(*AddedPath, IFileManager::Get().GetTimeStamp(*AddedPath) + FTimespan::FromSeconds(2));
	MetadataReads = 0;
	DesignLocalFontImport::FSourceIndex RetimedImport;
	RetimedImport.GetFaces();
	TestEqual(TEXT("Timestamp invalidates unchanged-size file"), MetadataReads, RetriedReads + 1);
	IFileManager::Get().Delete(*AddedPath);
	DesignLocalFontImport::FSourceIndex DeletedImport;
	TestFalse(TEXT("Deleted fonts disappear next import"), DeletedImport.GetFaces().ContainsByPredicate(
		[&](const DesignLocalFontImport::FFace& Face) { return FPaths::IsSamePath(Face.Path, AddedPath); }));

	// A missing optional worker must not let an unrelated WOFF2 hide a usable TTF.
	const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
	if (!TestTrue(TEXT("Find compressed font fixture"), Plugin.IsValid())) return false;
	PencilFontTool::FDownload Download;
	FString Error;
	if (!TestTrue(TEXT("Read optional tool contract"), PencilFontTool::ReadDownload(Download, Error))) return false;
	const FString Receipt = Download.InstallDirectory() / TEXT("installed.json");
	Files.OnRead = nullptr;
	Files.RejectRead = [Receipt](const FString& Path) { return FPaths::IsSamePath(Path, Receipt); };
	TestFalse(TEXT("Test boundary presents the tool as uninstalled without modifying it"), PencilFontTool::IsInstalled());
	const FString Compressed = Directory / (TEXT("!UnrelatedCompressed_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".woff2"));
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*Compressed); FFontResolver::ClearCache(); };
	if (!TestEqual(TEXT("Copy native source beside compressed source"), IFileManager::Get().Copy(*AddedPath,
		*(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"))), COPY_OK)
		|| !TestEqual(TEXT("Copy actual compressed source"), IFileManager::Get().Copy(*Compressed,
			*(Plugin->GetBaseDir() / TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures/DeclaredFont.woff2"))), COPY_OK)) return false;
	DesignLocalFontImport::FSourceIndex MixedSources;
	TestTrue(TEXT("Unrelated compressed source does not stop native discovery"), MixedSources.GetFaces().ContainsByPredicate(
		[&](const DesignLocalFontImport::FFace& Face) { return FPaths::IsSamePath(Face.Path, AddedPath); }));
	TestTrue(TEXT("Uninspected compressed source is retained"), MixedSources.GetUninspectedCompressedSources().ContainsByPredicate(
		[&](const FString& Path) { return FPaths::IsSamePath(Path, Compressed); }));
	DesignLocalFontImport::FFace Native, Seed;
	if (!TestTrue(TEXT("Read native source identity"), DesignLocalFontImport::ReadFace(AddedPath, Native, Error))
		|| !TestTrue(TEXT("Read a different initial weight"), DesignLocalFontImport::ReadFace(Replacement, Seed, Error))) return false;
	const FString Package = TEXT("/Game/Design2UMG/Fonts/IndexLifetime_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/NativeFont");
	ON_SCOPE_EXIT { DeleteFontFixture(*this, Package); FFontResolver::ClearCache(); };
	if (!TestTrue(TEXT("Seed a fixture that still needs the native regular source"), DesignLocalFontImport::ImportFace(Seed, Package, Error))) return false;
	const FString Asset = Package + TEXT(".NativeFont");
	FFontResolver::RegisterFontAssetPath(Native.Family, Asset);
	FDesignDocument Document;
	auto Text = MakeShared<FDesignNode>();
	Text->Text.Content = TEXT("A");
	Text->Text.FontFamily = Native.Family;
	Text->Text.FontWeight = FString::FromInt(Native.Weight);
	Text->Text.FontStyle = TEXT("normal");
	Document.Children.Add(Text);
	TArray<FString> Warnings;
	Error.Reset();
	if (!TestTrue(TEXT("Native static request succeeds without the optional tool"),
		DesignLocalFontImport::Prepare(Document, FDesign2UMGConversionSettings::Defaults(), Warnings, Error, &MixedSources)))
	{ AddError(Error); return false; }
	TestFalse(TEXT("Native preparation supplies the exact requested style"),
		FFontResolver::FindExactTypefaceForAssetPath(Asset, Text->Text.FontWeight).IsEmpty());
	FDesignFont& Required = Document.Fonts.AddDefaulted_GetRef();
	Required.Name = Native.Family;
	Required.Url = Compressed;
	Error.Reset();
	TestFalse(TEXT("An explicitly requested compressed source still requires the tool"),
		DesignLocalFontImport::Prepare(Document, FDesign2UMGConversionSettings::Defaults(), Warnings, Error, &MixedSources));
	TestTrue(TEXT("The real compressed request preserves its source and explains the missing tool"),
		Error.Contains(Compressed) && Error.Contains(TEXT("字体转换工具未安装")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignLocalFontIdentityTest,
	"Design2UMG.Fonts.LocalSource.IdentityAndEmbeddedImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignLocalFontIdentityTest::RunTest(const FString&)
{
	const FString Source = FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf");
	const FString Renamed = FPaths::ProjectSavedDir() / TEXT("Design2UMG/FontTests/NotTheFamily_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ttf");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Renamed), true);
	ON_SCOPE_EXIT { TestTrue(TEXT("Remove temporary font source"), IFileManager::Get().Delete(*Renamed)); };
	if (!TestEqual(TEXT("Copy source to misleading filename"), IFileManager::Get().Copy(*Renamed, *Source), COPY_OK)) return false;
	DesignLocalFontImport::FFace Face;
	FString Error;
	if (!TestTrue(TEXT("Read source metadata"), DesignLocalFontImport::ReadFace(Renamed, Face, Error))) return false;
	TestEqual(TEXT("Family is internal, not filename"), Face.Family, FString(TEXT("Roboto")));
	TestEqual(TEXT("Weight comes from font metadata"), Face.Weight, 700);
	DesignLocalFontImport::FFace Inconsistent;
	TestTrue(TEXT("Static font uses actual weight rather than its style label"),
		DesignLocalFontImport::ReadFace(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Black.ttf"), Inconsistent, Error));
	TestEqual(TEXT("OS/2 weight is authoritative over the Black style label"), Inconsistent.Weight, 400);
	DesignLocalFontImport::FSourceIndex DirectorySources;
	const auto& DirectoryFaces = DirectorySources.GetFaces();
	const auto* NormalFace = DirectoryFaces.FindByPredicate([&](const DesignLocalFontImport::FFace& Candidate)
	{
		return Candidate.Family.Equals(Face.Family, ESearchCase::IgnoreCase)
			&& Candidate.Weight == Inconsistent.Weight && !Candidate.bItalic && !Candidate.bVariable;
	});
	if (!TestNotNull(TEXT("Directory discovery finds a normal-weight face"), NormalFace)) return false;
	TestTrue(TEXT("Directory discovery does not select the conflicting Black face as normal"),
		FPaths::ConvertRelativePathToFull(NormalFace->Path) != FPaths::ConvertRelativePathToFull(
			FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Black.ttf")));

	TestFalse(TEXT("Condensed face is not silently substituted for normal-width Bold"),
		DesignLocalFontImport::ReadFace(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-BoldCondensed.ttf"), Inconsistent, Error));
	const FString PackageName = TEXT("/Game/Design2UMG/Automation/LocalFontTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/IdentityFixture");
	ON_SCOPE_EXIT { DeleteFontFixture(*this, PackageName); };
	if (!TestTrue(TEXT("Import embeds source"), DesignLocalFontImport::ImportFace(Face, PackageName, Error))) return false;
	UFont* Font = LoadObject<UFont>(nullptr, *(PackageName + TEXT(".IdentityFixture")));
	if (!TestNotNull(TEXT("Font asset loads"), Font)) return false;
	const auto& Entries = Font->GetCompositeFont()->DefaultTypeface.Fonts;
	TestEqual(TEXT("One weight"), Entries.Num(), 1);
	if (Entries.Num() == 1)
	{
		TestEqual(TEXT("Named typeface"), Entries[0].Name, FName(TEXT("Bold")));
		TestNotNull(TEXT("Embedded UFontFace, not external filename"), Entries[0].Font.GetFontFaceAsset());
	}
	FFontResolver::ClearCache();
	TestTrue(TEXT("Second import succeeds"), DesignLocalFontImport::ImportFace(Face, PackageName, Error));
	TestEqual(TEXT("Unchanged imported bytes still register the family after cache reset"),
		FFontResolver::FindFontAssetPath(Face.Family), Font->GetPathName());
	TestEqual(TEXT("Second import does not duplicate weight"), Font->GetCompositeFont()->DefaultTypeface.Fonts.Num(), 1);
	TestTrue(TEXT("Import and reimport do not embed fonts absent from the source"),
		Font->GetCompositeFont()->FallbackTypeface.Typeface.Fonts.IsEmpty());

	Font->FontCacheType = EFontCacheType::Offline;
	TestTrue(TEXT("Same bytes still repair an offline font"), DesignLocalFontImport::ImportFace(Face, PackageName, Error));
	TestTrue(TEXT("Imported font uses runtime cache"), Font->FontCacheType == EFontCacheType::Runtime);
	TestFalse(TEXT("Engine destinations are rejected"), DesignLocalFontImport::ImportFace(Face, TEXT("/Engine/LocalFontTest"), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignLocalFontSaveFailureTest,
	"Design2UMG.Fonts.LocalSource.SaveFailureRestoresFont",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesignLocalFontSaveFailureTest::RunTest(const FString&)
{
	DesignLocalFontImport::FFace Seed;
	FString Error;
	if (!TestTrue(TEXT("Read seed font"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), Seed, Error))) return false;
	const FString Package = TEXT("/Game/D2UFontSaveFailure_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/Font");
	if (!TestTrue(TEXT("Save seed font"), DesignLocalFontImport::ImportFace(Seed, Package, Error, TEXT("Regular")))) return false;
	UFont* Font = LoadObject<UFont>(nullptr, *(Package + TEXT(".Font")));
	if (!TestNotNull(TEXT("Seed font exists"), Font)) return false;
	const FString Filename = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension()));
	ON_SCOPE_EXIT { DeleteFontFixture(*this, Package); };
	TArray<uint8> Before;
	if (!TestTrue(TEXT("Read saved font"), FFileHelper::LoadFileToArray(Before, *Filename))) return false;
	Font->FontCacheType = EFontCacheType::Offline;
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
	Font->RuntimeFontSource = ERuntimeFontSource::CoreStyleDefault;
#endif
	Font->GetOutermost()->SetDirtyFlag(false);
	const FFontData OriginalFace = Design2UMGFontAccess::GetMutableCompositeFont(*Font).DefaultTypeface.Fonts[0].Font;
	bool bSaveRejected = false;
	Design2UMGEditorTests::FScopedPackageSaveFailure SaveFailure(
		[&](const FString& Path)
		{
			bSaveRejected = Path.Equals(Filename, ESearchCase::IgnoreCase);
			return bSaveRejected;
		});
	TestFalse(TEXT("Rejected font save fails import"), DesignLocalFontImport::ImportFace(Seed, Package, Error, TEXT("Bold")));
	TestTrue(TEXT("The target save was rejected"), bSaveRejected);
	TestTrue(TEXT("Failure identifies font persistence"), Error.StartsWith(TEXT("Failed to save local font:")));
	TestTrue(TEXT("Original cache type restored"), Font->FontCacheType == EFontCacheType::Offline);
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
	TestTrue(TEXT("Original runtime source restored"), Font->RuntimeFontSource == ERuntimeFontSource::CoreStyleDefault);
#endif
	const auto& Entries = Design2UMGFontAccess::GetMutableCompositeFont(*Font).DefaultTypeface.Fonts;
	TestEqual(TEXT("No added face survives failure"), Entries.Num(), 1);
	if (Entries.Num() == 1)
	{
		TestEqual(TEXT("Original face name restored"), Entries[0].Name, FName(TEXT("Regular")));
		TestTrue(TEXT("Original face data restored"), Entries[0].Font.GetFontFaceAsset() == OriginalFace.GetFontFaceAsset());
	}
	TestFalse(TEXT("Original package dirty state restored"), Font->GetOutermost()->IsDirty());
	TArray<uint8> After;
	TestTrue(TEXT("Saved font remains readable"), FFileHelper::LoadFileToArray(After, *Filename));
	TestTrue(TEXT("Failed save leaves persisted font unchanged"), Before == After);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignLocalFontFirstSaveFailureTest,
	"Design2UMG.Fonts.LocalSource.FirstSaveFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesignLocalFontFirstSaveFailureTest::RunTest(const FString&)
{
	DesignLocalFontImport::FFace Face;
	FString Error;
	if (!TestTrue(TEXT("Read font fixture"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), Face, Error))) return false;
	const FString Package = TEXT("/Game/D2UFontFirstSaveFailure_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/Font");
	const FString ObjectPath = Package + TEXT(".Font");
	const FString Filename = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension()));
	ON_SCOPE_EXIT { DeleteFontFixture(*this, Package); };
	bool bSaveRejected = false;
	Design2UMGEditorTests::FScopedPackageSaveFailure SaveFailure(
		[&](const FString& Path) { bSaveRejected = Path.Equals(Filename, ESearchCase::IgnoreCase); return bSaveRejected; });
	TestFalse(TEXT("First import fails when save is rejected"), DesignLocalFontImport::ImportFace(Face, Package, Error));
	TestTrue(TEXT("The first save was rejected"), bSaveRejected);
	TestTrue(TEXT("First failure identifies font persistence"), Error.StartsWith(TEXT("Failed to save local font:")));
	TestFalse(TEXT("Failed first save leaves no package file"), IFileManager::Get().FileExists(*Filename));
	TestFalse(TEXT("Failed first save removes its empty directory"), IFileManager::Get().DirectoryExists(*FPaths::GetPath(Filename)));
	TestFalse(TEXT("Failed first save leaves no live font"), IsValid(FindObject<UFont>(nullptr, *ObjectPath)));
	TArray<FAssetData> Assets;
	FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(FName(*Package), Assets);
	TestEqual(TEXT("Failed first save leaves no registered asset"), Assets.Num(), 0);
	SaveFailure.AllowSaves();
	if (!TestTrue(TEXT("Retry at the same destination succeeds"), DesignLocalFontImport::ImportFace(Face, Package, Error))) return false;
	TestNotNull(TEXT("Retry creates a loadable font"), LoadObject<UFont>(nullptr, *ObjectPath));
	TestTrue(TEXT("Retry creates a saved package"), IFileManager::Get().FileExists(*Filename));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignLocalFontUserAssetProtectionTest,
	"Design2UMG.Fonts.LocalSource.UserAssetProtection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesignLocalFontUserAssetProtectionTest::RunTest(const FString&)
{
	DesignLocalFontImport::FFace Seed;
	FString Error;
	if (!TestTrue(TEXT("Read seed font"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), Seed, Error))) return false;
	const FString Package = TEXT("/Game/D2UUserFontTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/UserFont");
	if (!TestTrue(TEXT("Create user-owned fixture"), DesignLocalFontImport::ImportFace(Seed, Package, Error, TEXT("Regular")))) return false;
	UFont* Font = LoadObject<UFont>(nullptr, *(Package + TEXT(".UserFont")));
	if (!TestNotNull(TEXT("Fixture font exists"), Font)) return false;
	const FString Filename = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
	ON_SCOPE_EXIT { DeleteFontFixture(*this, Package); };
	TArray<uint8> Before;
	if (!TestTrue(TEXT("Read saved fixture"), FFileHelper::LoadFileToArray(Before, *Filename))) return false;
	FFontResolver::RegisterFontAssetPath(Seed.Family, Font->GetPathName());
	FDesignDocument Document;
	auto Node = MakeShared<FDesignNode>();
	Node->Text.Content = TEXT("Local font");
	Node->Text.FontFamily = Seed.Family;
	Node->Text.FontWeight = TEXT("700");
	Document.Children.Add(Node);
	TArray<FString> Warnings;
	TestTrue(TEXT("User font can still be referenced"), DesignLocalFontImport::Prepare(Document, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()), Warnings, Error));
	TestTrue(TEXT("Automatic preparation preserves user typefaces"),
		FFontResolver::FindExactTypefaceForAssetPath(Font->GetPathName(), TEXT("700")).IsEmpty());
	TArray<uint8> After;
	TestTrue(TEXT("Read fixture after preparation"), FFileHelper::LoadFileToArray(After, *Filename));
	TestTrue(TEXT("Automatic preparation leaves saved user font byte-identical"), Before == After);
	TestTrue(TEXT("Missing requested style identifies the preserved user asset"), Warnings.ContainsByPredicate(
		[&](const FString& Warning) { return Warning.Contains(Font->GetPathName()) && Warning.Contains(TEXT("700")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignUnusedEmbeddedFontPersistenceTest,
	"Design2UMG.Fonts.LocalSource.UnusedEmbeddedPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesignUnusedEmbeddedFontPersistenceTest::RunTest(const FString&)
{
	DesignLocalFontImport::FFace Face;
	FString Error;
	if (!TestTrue(TEXT("Read source"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), Face, Error))) return false;
	Face.Family = TEXT("D2UUnusedEmbedded_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Package = FFontResolver::MakeImportedFamilyPackageName(Face.Family);
	if (!TestTrue(TEXT("Import fixture"), DesignLocalFontImport::ImportFace(Face, Package, Error))) return false;
	ON_SCOPE_EXIT { DeleteFontFixture(*this, Package, false); };
	UFont* Font = LoadObject<UFont>(nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
	if (!TestNotNull(TEXT("Font exists"), Font)) return false;
	UFontFace* Embedded = const_cast<UFontFace*>(Cast<UFontFace>(Font->GetCompositeFont()->DefaultTypeface.Fonts[0].Font.GetFontFaceAsset()));
	if (!TestNotNull(TEXT("Embedded face exists"), Embedded)) return false;
	if (!AddFallbackFixture(*this, *Font)) return false;
	Embedded->LoadingPolicy = EFontLoadingPolicy::LazyLoad;
	if (!SaveFontPolicyFixture(*this, *Font)) return false;
	const FString Filename = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
	TArray<uint8> Before, After;
	FFileHelper::LoadFileToArray(Before, *Filename);
	const FDesignDocument Document;
	TArray<FString> Warnings;
	bool bSaveAttempted = false;
	Design2UMGEditorTests::FScopedPackageSaveFailure Failure([&](const FString& Path)
	{
		const bool bMatches = FPaths::IsSamePath(Path, Filename);
		bSaveAttempted |= bMatches;
		return bMatches;
	});
	TestTrue(TEXT("Unused-font save failure does not affect preparation"), DesignLocalFontImport::Prepare(Document, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()), Warnings, Error));
	TestFalse(TEXT("Unused font save is not attempted"), bSaveAttempted);
	TestTrue(TEXT("Unused font policy stays unchanged"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
	FFileHelper::LoadFileToArray(After, *Filename);
	TestTrue(TEXT("Unused font file stays intact"), Before == After);
	Failure.AllowSaves();
	TestTrue(TEXT("Repeated preparation ignores unused generated fonts"), DesignLocalFontImport::Prepare(Document, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()), Warnings, Error));
	TestTrue(TEXT("Unused font policy stays unchanged"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
	TestFalse(TEXT("Unused automatic fallback is not repaired"), Font->GetCompositeFont()->FallbackTypeface.Typeface.Fonts.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignEmbeddedFontPersistenceTest,
	"Design2UMG.Fonts.LocalSource.EmbeddedPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesignEmbeddedFontPersistenceTest::RunTest(const FString&)
{
	for (const bool bPrepare : {false, true})
	for (const bool bWasDirty : {false, true})
	{
		DesignLocalFontImport::FFace Regular, Bold;
		FString Error;
		if (!TestTrue(TEXT("Read regular source"), DesignLocalFontImport::ReadFace(
			FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), Regular, Error))) return false;
		if (!TestTrue(TEXT("Read bold source"), DesignLocalFontImport::ReadFace(
			FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"), Bold, Error))) return false;
		Regular.Family = TEXT("D2UEmbeddedFont_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		Bold.Family = Regular.Family;
		const FString Package = FFontResolver::MakeImportedFamilyPackageName(Regular.Family);
		if (!TestTrue(TEXT("Import regular seed"), DesignLocalFontImport::ImportFace(Regular, Package, Error))) return false;
		UFontFace* SeparateFace = nullptr;
		ON_SCOPE_EXIT
		{
			if (SeparateFace) TestTrue(TEXT("Delete the separate face fixture"), ObjectTools::ForceDeleteObjects({SeparateFace}, false) > 0);
			DeleteFontFixture(*this, Package, false);
		};
		if (!TestTrue(TEXT("Import bold seed"), DesignLocalFontImport::ImportFace(Bold, Package, Error))) return false;
		UFont* Font = LoadObject<UFont>(nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
		if (!TestNotNull(TEXT("Font exists"), Font)) return false;
		auto& Entries = Design2UMGFontAccess::GetMutableCompositeFont(*Font).DefaultTypeface.Fonts;
		UFontFace* RegularFace = const_cast<UFontFace*>(Cast<UFontFace>(Entries[0].Font.GetFontFaceAsset()));
		UFontFace* BoldFace = const_cast<UFontFace*>(Cast<UFontFace>(Entries[1].Font.GetFontFaceAsset()));
		if (!TestNotNull(TEXT("Regular embedded face exists"), RegularFace)
			|| !TestNotNull(TEXT("Bold embedded face exists"), BoldFace)) return false;
		auto& Composite = Design2UMGFontAccess::GetMutableCompositeFont(*Font);
		TestTrue(TEXT("Imported font starts without an external fallback"), Composite.FallbackTypeface.Typeface.Fonts.IsEmpty());
		RegularFace->LoadingPolicy = EFontLoadingPolicy::LazyLoad;
		BoldFace->LoadingPolicy = EFontLoadingPolicy::Stream;
		SeparateFace = NewObject<UFontFace>(Font->GetOutermost(),
			MakeUniqueObjectName(Font->GetOutermost(), UFontFace::StaticClass()), RF_Public | RF_Standalone);
		SeparateFace->LoadingPolicy = EFontLoadingPolicy::LazyLoad;
		SeparateFace->FontFaceData = FFontFaceData::MakeFontFaceData(TArray<uint8>(RegularFace->FontFaceData->GetData()));
		FAssetRegistryModule::AssetCreated(SeparateFace);
		FTypefaceEntry SeparateEntry(FName(TEXT("Separate")));
		SeparateEntry.Font = FFontData(SeparateFace);
		Entries.Add(MoveTemp(SeparateEntry));
		FCompositeSubFont& Subfont = Composite.SubTypefaces.AddDefaulted_GetRef();
		Subfont.CharacterRanges = {FInt32Range::Inclusive('A', 'A')};
		Subfont.ScalingFactor = 0.75f;
		Subfont.Typeface.Fonts.Add(Entries[0]);
		UFontFace* ExtraFace = bPrepare ? AddFallbackFixture(*this, *Font) : nullptr;
		if (bPrepare && !ExtraFace) return false;
		if (!SaveFontPolicyFixture(*this, *Font)) return false;
		const TArray<uint8> RegularBytes = RegularFace->FontFaceData->GetData();
		const TArray<uint8> BoldBytes = BoldFace->FontFaceData->GetData();
		const FString Filename = FPaths::ConvertRelativePathToFull(
			FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension()));
		TArray<uint8> Before, After;
		if (!TestTrue(TEXT("Read original persisted policies"), FFileHelper::LoadFileToArray(Before, *Filename))) return false;
		Font->GetOutermost()->SetDirtyFlag(bWasDirty);
		FSlateFontCache& Cache = *FSlateApplication::Get().GetRenderer()->GetFontCache();
		const FSlateFontInfo Info(Font, 14, TEXT("Regular"));
		const FVector2D LatinSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(TEXT("A"), Info);
		if (bPrepare)
		{
			float Scale = 1.0f;
			TestTrue(TEXT("Warm native selection really uses the extra persisted face"),
				Cache.GetFontDataForCodepoint(Info, 0x4e2d, Scale).GetFontFaceAsset() == ExtraFace);
			FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(TEXT("A中"), Info);
		}
		FDesignDocument Document;
		auto Node = MakeShared<FDesignNode>();
		Node->Type = EDesignNodeType::Text;
		Node->Text.Content = TEXT("A\u4e2d");
		Node->Text.FontFamily = Regular.Family;
		Node->Text.FontWeight = TEXT("400");
		Document.Children.Add(Node);
		auto Rich = MakeShared<FDesignNode>(*Node);
		FDesignTextRun Run;
		Run.Content = Node->Text.Content;
		Rich->Text.TextRuns.Add(Run);
		Document.Children.Add(Rich);
		auto Icon = MakeShared<FDesignNode>();
		Icon->Type = EDesignNodeType::IconFont;
		Icon->Text.IconFontFamily = Regular.Family;
		Icon->Text.IconWeight = 400;
		Document.Children.Add(Icon);
		if (bPrepare && bWasDirty)
		{
			FDesignFont& Declaration = Document.Fonts.AddDefaulted_GetRef();
			Declaration.Name = Regular.Family;
			Declaration.Url = Regular.Path;
		}
		TArray<FString> Warnings;
		const auto Captured = FDesign2UMGConversionSettings::Defaults();
		const auto Persist = [&]()
		{
			return bPrepare ? DesignLocalFontImport::Prepare(Document, Captured, Warnings, Error)
				: DesignLocalFontImport::ImportFace(Regular, Package, Error);
		};
		bool bSaveRejected = false;
		Design2UMGEditorTests::FScopedPackageSaveFailure SaveFailure([&](const FString& Path)
		{
			bSaveRejected = Path.Equals(Filename, ESearchCase::IgnoreCase);
			if (bSaveRejected && bPrepare)
			{
				float Scale = 1.0f;
				const FFontData& DuringSave = Cache.GetFontDataForCodepoint(Info, 0x4e2d, Scale);
				TestFalse(TEXT("Selection during a rejected save does not use stale fallback"),
					Cache.CanLoadCodepoint(DuringSave, 0x4e2d, EFontFallback::FF_NoFallback));
			}
			return bSaveRejected;
		});
		TestFalse(TEXT("A failed policy save fails the requested operation"), Persist());
		TestTrue(TEXT("Existing-font save was attempted"), bSaveRejected);
		TestTrue(TEXT("Policy failure names the font save"), Error.StartsWith(TEXT("Failed to save local font:")));
		TestTrue(TEXT("Regular policy rolls back"), RegularFace->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
		TestTrue(TEXT("Bold policy rolls back"), BoldFace->LoadingPolicy == EFontLoadingPolicy::Stream);
		TestEqual(TEXT("Failed save restores the prior fallback count"), Composite.FallbackTypeface.Typeface.Fonts.Num(), bPrepare ? 1 : 0);
		if (bPrepare)
		{
			float Scale = 1.0f;
			TestTrue(TEXT("Rollback invalidates intermediate selection and restores the old face"),
				Cache.GetFontDataForCodepoint(Info, 0x4e2d, Scale).GetFontFaceAsset() == ExtraFace);
			TestEqual(TEXT("Fallback selection scale rolls back"), Scale, 0.9f);
		}
		TestEqual(TEXT("Dirty state rolls back"), Font->GetOutermost()->IsDirty(), bWasDirty);
		TestTrue(TEXT("Read font after rejected save"), FFileHelper::LoadFileToArray(After, *Filename));
		TestTrue(TEXT("Failed save leaves disk bytes identical"), Before == After);
		SaveFailure.AllowSaves();
		if (!TestTrue(TEXT("Retry persists existing embedded faces"), Persist())) return false;
		TestTrue(TEXT("Explicit reimport does not backfill external fallback fonts"), Composite.FallbackTypeface.Typeface.Fonts.IsEmpty());
		TestTrue(TEXT("Existing Regular policy follows the requested operation"), RegularFace->LoadingPolicy == (bPrepare ? EFontLoadingPolicy::LazyLoad : EFontLoadingPolicy::Inline));
		TestTrue(TEXT("Existing Bold policy follows the requested operation"), BoldFace->LoadingPolicy == (bPrepare ? EFontLoadingPolicy::Stream : EFontLoadingPolicy::Inline));
		TestTrue(TEXT("Separate font-face loading policy is untouched"), SeparateFace->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
		TestEqual(TEXT("All default typefaces are retained"), Entries.Num(), 3);
		TestTrue(TEXT("Regular object identity is unchanged"), Entries[0].Font.GetFontFaceAsset() == RegularFace);
		TestTrue(TEXT("Bold object identity is unchanged"), Entries[1].Font.GetFontFaceAsset() == BoldFace);
		TestEqual(TEXT("Regular typeface name is unchanged"), Entries[0].Name, FName(TEXT("Regular")));
		TestEqual(TEXT("Bold typeface name is unchanged"), Entries[1].Name, FName(TEXT("Bold")));
		TestTrue(TEXT("Regular bytes are unchanged"), RegularFace->FontFaceData->GetData() == RegularBytes);
		TestTrue(TEXT("Bold bytes are unchanged"), BoldFace->FontFaceData->GetData() == BoldBytes);
		TestEqual(TEXT("Native subfont and its scale survive"), Composite.SubTypefaces.Num(), 1);
		TestEqual(TEXT("Native subfont scaling is unchanged"), Composite.SubTypefaces[0].ScalingFactor, 0.75f);
		TestTrue(TEXT("Native subfont still references its original face"), Composite.SubTypefaces[0].Typeface.Fonts[0].Font.GetFontFaceAsset() == RegularFace);
		TestEqual(TEXT("Native measurement for an existing glyph is unchanged"),
			FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(TEXT("A"), Info)), LatinSize);
		if (bPrepare)
		{
			float Scale = 1.0f;
			TestFalse(TEXT("Successful repair invalidates the original extra-face selection"), Cache.CanLoadCodepoint(
				Cache.GetFontDataForCodepoint(Info, 0x4e2d, Scale), 0x4e2d, EFontFallback::FF_NoFallback));
			TArray<FSlateFontInfo> Donors;
			TMap<FFontData, FString> Hashes;
			TestTrue(TEXT("Real glyph preparation runs after the source repair"), DesignLocalFontImport::PrepareGlyphFallbacks(
				Document, Captured.ExplicitFontMappings, Donors, Hashes, Warnings, Error));
			TestTrue(TEXT("A source-only missing glyph produces the actual missing-glyph warning"), Warnings.ContainsByPredicate(
				[](const FString& Warning) { return Warning.Contains(TEXT("U+4E2D")); }));
			TestTrue(TEXT("No replacement asset is introduced without a donor"), Node->Text.PreparedFont.AssetPath.IsEmpty());
			if (!bWasDirty)
			{
				if (!AddFallbackFixture(*this, *Font) || !SaveFontPolicyFixture(*this, *Font)) return false;
				TestTrue(TEXT("Expanded glyph-only preparation also repairs a used undeclared font"),
					DesignLocalFontImport::Prepare(Document, Captured, Warnings, Error, nullptr, true));
				TestTrue(TEXT("Expanded glyph-only repair persists the same source asset"), Composite.FallbackTypeface.Typeface.Fonts.IsEmpty());
			}
		}
		Design2UMGEditorTests::FScopedPackageSaveFailure NoRepeatedSave([&](const FString& Path)
		{
			bSaveRejected = Path.Equals(Filename, ESearchCase::IgnoreCase);
			return bSaveRejected;
		});
		bSaveRejected = false;
		TestTrue(TEXT("Repeated preparation or reimport is a no-op"), Persist());
		TestFalse(TEXT("An unchanged font is not saved again"), bSaveRejected);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignExplicitFontPolicyProtectionTest,
	"Design2UMG.Fonts.LocalSource.ExplicitPolicyProtection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesignExplicitFontPolicyProtectionTest::RunTest(const FString&)
{
	DesignLocalFontImport::FFace Face;
	FString Error;
	if (!TestTrue(TEXT("Read source"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), Face, Error))) return false;
	Face.Family = TEXT("D2UExplicitFont_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Package = TEXT("/Game/Design2UMG/_TestOutput/ExternalFont_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/Font");
	if (!TestTrue(TEXT("Create mapped font"), DesignLocalFontImport::ImportFace(Face, Package, Error))) return false;
	ON_SCOPE_EXIT { DeleteFontFixture(*this, Package, false); };
	UFont* Font = LoadObject<UFont>(nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
	if (!TestNotNull(TEXT("Mapped font exists"), Font)) return false;
	UFontFace* Embedded = const_cast<UFontFace*>(Cast<UFontFace>(Font->GetCompositeFont()->DefaultTypeface.Fonts[0].Font.GetFontFaceAsset()));
	Embedded->LoadingPolicy = EFontLoadingPolicy::LazyLoad;
	if (!SaveFontPolicyFixture(*this, *Font)) return false;
	UDesign2UMGSettings* Settings = GetMutableDefault<UDesign2UMGSettings>();
	const auto PreviousMappings = Settings->ExplicitFontMappings;
	ON_SCOPE_EXIT { Settings->ExplicitFontMappings = PreviousMappings; FFontResolver::ClearCache(); };
	Settings->ExplicitFontMappings.Add(Face.Family, Font);
	FFontResolver::ClearCache();
	FDesignDocument Document;
	auto Node = MakeShared<FDesignNode>();
	Node->Text.Content = TEXT("Mapped font");
	Node->Text.FontFamily = Face.Family;
	Node->Text.FontWeight = TEXT("400");
	Document.Children.Add(Node);
	FDesignFont& Declaration = Document.Fonts.AddDefaulted_GetRef();
	Declaration.Name = Face.Family;
	Declaration.Url = FPaths::ProjectSavedDir() / FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ttf");
	TArray<FString> Warnings;
	bool bSaveAttempted = false;
	Design2UMGEditorTests::FScopedPackageSaveFailure NoSave([&](const FString&) { bSaveAttempted = true; return true; });
	TestTrue(TEXT("Explicit mapping is usable without a save"), DesignLocalFontImport::Prepare(Document, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()), Warnings, Error));
	TestFalse(TEXT("Explicitly mapped font is not saved"), bSaveAttempted);
	TestTrue(TEXT("Explicitly mapped policy is unchanged"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfiguredGeneratedFontProtectionTest,
	"Design2UMG.Fonts.LocalSource.ConfiguredGeneratedFontProtection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConfiguredGeneratedFontProtectionTest::RunTest(const FString&)
{
	DesignLocalFontImport::FFace Face;
	FString Error;
	if (!TestTrue(TEXT("Read source"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), Face, Error))) return false;
	Face.Family = TEXT("ConfiguredGenerated_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Package = FFontResolver::MakeImportedFamilyPackageName(Face.Family);
	if (!TestTrue(TEXT("Create generated font"), DesignLocalFontImport::ImportFace(Face, Package, Error))) return false;
	ON_SCOPE_EXIT { DeleteFontFixture(*this, Package, false); };
	UFont* Font = LoadObject<UFont>(nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
	if (!TestNotNull(TEXT("Generated font exists"), Font)) return false;
	UFontFace* Embedded = const_cast<UFontFace*>(Cast<UFontFace>(Font->GetCompositeFont()->DefaultTypeface.Fonts[0].Font.GetFontFaceAsset()));
	if (!TestNotNull(TEXT("Embedded face exists"), Embedded) || !AddFallbackFixture(*this, *Font)) return false;
	UDesign2UMGSettings* Settings = GetMutableDefault<UDesign2UMGSettings>();
	const auto PreviousMappings = Settings->ExplicitFontMappings;
	const auto PreviousIcons = Settings->IconFontAssetMap;
	const auto PreviousDefault = Settings->DefaultFontAsset;
	const auto PreviousDefaultIcon = Settings->DefaultIconFontAssetPath;
	ON_SCOPE_EXIT
	{
		Settings->ExplicitFontMappings = PreviousMappings;
		Settings->IconFontAssetMap = PreviousIcons;
		Settings->DefaultFontAsset = PreviousDefault;
		Settings->DefaultIconFontAssetPath = PreviousDefaultIcon;
		FFontResolver::ClearCache();
	};
	for (int32 Mapping = 0; Mapping != 4; ++Mapping)
	{
		Settings->ExplicitFontMappings = PreviousMappings;
		Settings->IconFontAssetMap = PreviousIcons;
		Settings->DefaultFontAsset = PreviousDefault;
		Settings->DefaultIconFontAssetPath = PreviousDefaultIcon;
		if (Mapping == 0) Settings->ExplicitFontMappings.Add(Face.Family, Font);
		if (Mapping == 1) Settings->IconFontAssetMap.Add(Face.Family, Font);
		if (Mapping == 2) Settings->DefaultFontAsset = Font;
		if (Mapping == 3) Settings->DefaultIconFontAssetPath = Font->GetPathName();
		const auto Captured = FDesign2UMGConversionSettings::FromSettings(Settings);
		Settings->ExplicitFontMappings.Reset();
		Settings->IconFontAssetMap.Reset();
		Settings->DefaultFontAsset.Reset();
		Settings->DefaultIconFontAssetPath.Reset();
		Embedded->LoadingPolicy = EFontLoadingPolicy::LazyLoad;
		if (!SaveFontPolicyFixture(*this, *Font)) return false;
		const FString Filename = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
		TArray<uint8> Before, After;
		if (!TestTrue(TEXT("Read configured font before preparation"), FFileHelper::LoadFileToArray(Before, *Filename))) return false;
		bool bSaveAttempted = false;
		Design2UMGEditorTests::FScopedPackageSaveFailure NoSave([&](const FString&) { bSaveAttempted = true; return true; });
		FFontResolver::ClearCache();
		FDesignDocument Document;
		FDesignFont& Declaration = Document.Fonts.AddDefaulted_GetRef();
		Declaration.Name = Face.Family;
		Declaration.Url = FPaths::ProjectSavedDir() / TEXT("NoDeclaredFont_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ttf");
		TArray<FString> Warnings;
		TestTrue(TEXT("Unused mapped generated font does not block preparation"), DesignLocalFontImport::Prepare(Document, Captured, Warnings, Error));
		TestTrue(TEXT("Unused mapped generated font stays unchanged"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
		auto Node = MakeShared<FDesignNode>();
		Node->Text.Content = TEXT("Mapped font");
		Node->Text.FontFamily = Face.Family;
		Node->Text.IconFontFamily = Face.Family;
		Document.Children.Add(Node);
		TestTrue(TEXT("Requested mapped text and icon fonts prepare without replacement"), DesignLocalFontImport::Prepare(Document, Captured, Warnings, Error));
		TestFalse(TEXT("Configured font is never automatically saved"), bSaveAttempted);
		TestTrue(TEXT("Configured font retains its chosen loading policy"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
		if (!TestTrue(TEXT("Read configured font after preparation"), FFileHelper::LoadFileToArray(After, *Filename))) return false;
		TestTrue(TEXT("All four configured asset kinds remain byte-identical"), Before == After);
		TestFalse(TEXT("Captured configured fallback is retained"), Font->GetCompositeFont()->FallbackTypeface.Typeface.Fonts.IsEmpty());
	}
	Settings->DefaultIconFontAssetPath = Font->GetPathName();
	FDesignDocument IconDocument;
	auto Icon = MakeShared<FDesignNode>();
	Icon->Text.FontFamily.Reset();
	Icon->Text.IconFontFamily = Face.Family;
	IconDocument.Children.Add(Icon);
	FDesignFont& Declaration = IconDocument.Fonts.AddDefaulted_GetRef();
	Declaration.Name = Face.Family;
	Declaration.Url = FPaths::ProjectSavedDir() / TEXT("NoDeclaredIcon_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ttf");
	TArray<FString> Warnings;
	TestFalse(TEXT("A new global icon mapping cannot skip an unconfigured captured request"),
		DesignLocalFontImport::Prepare(IconDocument, FDesign2UMGConversionSettings::Defaults(), Warnings, Error));
	TestTrue(TEXT("The captured icon request reaches its declared source"), Error.Contains(Declaration.Url));
	return true;
}

namespace
{
	class FPrepareDeclaredWoff2Test final : public IAutomationLatentCommand
	{
		FAutomationTestBase& Test;
		PencilFontTool::FDownload Download;
		TFunction<bool()> VerifyImport;
		TSharedPtr<PencilFontTool::FInstall, ESPMode::ThreadSafe> Operation;
		FHttpRequestPtr CancelledRequest;
		double Deadline = 0;
		bool bTimedOut = false;

	public:
		FPrepareDeclaredWoff2Test(FAutomationTestBase& InTest, const PencilFontTool::FDownload& InDownload, TFunction<bool()> InVerifyImport)
			: Test(InTest), Download(InDownload), VerifyImport(MoveTemp(InVerifyImport)) {}

		virtual bool Update() override
		{
			if (!Operation)
			{
				if (PencilFontTool::IsInstalled())
				{
					if (!VerifyImport()) Test.AddError(TEXT("DeclaredWoff2 positive test failed using the existing installation."));
					return true;
				}
				if (PencilFontTool::HasInstallation() || PencilFontTool::IsBusy())
				{
					Test.AddError(TEXT("DeclaredWoff2 setup refused: preserve an existing installation or active tool operation."));
					return true;
				}
				Operation = PencilFontTool::Install(Download);
				if (!Operation) { Test.AddError(TEXT("DeclaredWoff2 dependency Install was refused.")); return true; }
				Deadline = FPlatformTime::Seconds() + 330;
				return false;
			}
			if (!Operation->Poll() || (CancelledRequest && CancelledRequest->GetStatus() == EHttpRequestStatus::Processing))
			{
				if (!bTimedOut && FPlatformTime::Seconds() > Deadline)
				{
					Test.AddError(TEXT("DeclaredWoff2 dependency setup timed out; cancelling and waiting for cleanup. ") + Operation->Error);
					CancelledRequest = Operation->Request;
					Operation->Cancel();
					bTimedOut = true;
				}
				return false;
			}
			if (!Operation->Error.IsEmpty())
			{
				Test.AddError(TEXT("DeclaredWoff2 dependency setup failed: ") + Operation->Error);
				return true;
			}
			// Only this successful Install grants ownership of the directory to this test.
			ON_SCOPE_EXIT
			{
				FString Error;
				Test.TestTrue(TEXT("Remove only the installation created for DeclaredWoff2"), PencilFontTool::Remove(Error));
				if (!Error.IsEmpty()) Test.AddError(TEXT("DeclaredWoff2 dependency cleanup: ") + Error);
				Test.TestFalse(TEXT("Test-created font tool directory is removed"), PencilFontTool::HasInstallation());
			};
			if (bTimedOut) return true;
			if (!Test.TestTrue(TEXT("DeclaredWoff2 dependency installation is verified"), PencilFontTool::IsInstalled())) return true;
			if (!VerifyImport()) Test.AddError(TEXT("DeclaredWoff2 positive test failed after dependency preparation."));
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeclaredWoff2FontImportTest,
	"Design2UMG.Fonts.LocalSource.DeclaredWoff2",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeclaredWoff2FontImportTest::RunTest(const FString&)
{
	auto VerifyImport = [this]()
	{
		const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		if (!TestTrue(TEXT("Find the plugin containing the required fixture"), Plugin.IsValid())) return false;
		const FString Source = Plugin->GetBaseDir() / TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures/DeclaredFont.woff2");
		TArray<uint8> SourceBytes;
		if (!TestTrue(TEXT("Read compressed source bytes"), FFileHelper::LoadFileToArray(SourceBytes, *Source))) return false;
		if (!TestTrue(TEXT("Fixture is actual WOFF2, not a renamed TTF"), SourceBytes.Num() >= 4
			&& SourceBytes[0] == 'w' && SourceBytes[1] == 'O' && SourceBytes[2] == 'F' && SourceBytes[3] == '2')) return false;
		FString Error;
		const FString Family = TEXT("D2UDeclaredWoff2_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Package = FFontResolver::MakeImportedFamilyPackageName(Family);
		ON_SCOPE_EXIT { DeleteFontFixture(*this, Package, false); };
		FDesignDocument Document;
		FDesignFont& Declaration = Document.Fonts.AddDefaulted_GetRef();
		Declaration.Name = Family;
		Declaration.Url = Source;
		auto Text = MakeShared<FDesignNode>();
		Text->Text.Content = TEXT("A");
		Text->Text.FontFamily = Family;
		Text->Text.FontWeight = TEXT("400");
		Text->Text.FontStyle = TEXT("normal");
		Document.Children.Add(Text);
		TArray<FString> Warnings;
		if (!TestTrue(TEXT("Declared WOFF2 reaches static conversion and font import"),
			DesignLocalFontImport::Prepare(Document, FDesign2UMGConversionSettings::Defaults(), Warnings, Error, nullptr, true))) return false;
		UFont* Font = LoadObject<UFont>(nullptr, *Text->Text.PreparedFont.SourceAssetPath);
		if (!TestNotNull(TEXT("The declared request binds its saved asset"), Font)) return false;
		const auto& Entries = Font->GetCompositeFont()->DefaultTypeface.Fonts;
		if (!TestEqual(TEXT("One requested static face was imported"), Entries.Num(), 1)) return false;
		const UFontFace* Embedded = Cast<UFontFace>(Entries[0].Font.GetFontFaceAsset());
		if (!TestNotNull(TEXT("Converted face is embedded"), Embedded)) return false;
		const auto& Bytes = Embedded->FontFaceData->GetData();
		TestTrue(TEXT("WOFF2 was decoded into a static sfnt"), Bytes.Num() >= 4
			&& !(Bytes[0] == 'w' && Bytes[1] == 'O' && Bytes[2] == 'F' && Bytes[3] == '2'));
		TestEqual(TEXT("Prepared selection keeps the actual source weight and style"), Text->Text.PreparedFont.TypefaceName,
			FName(TEXT("Regular")));
		TestTrue(TEXT("WOFF2 import does not add a source-external fallback"), Font->GetCompositeFont()->FallbackTypeface.Typeface.Fonts.IsEmpty());
		TArray<uint8> After;
		TestTrue(TEXT("Read compressed source after preparation"), FFileHelper::LoadFileToArray(After, *Source));
		TestTrue(TEXT("Import leaves compressed source unchanged"), After == SourceBytes);
		return true;
	};
	if (PencilFontTool::IsInstalled()) return VerifyImport();
	if (!FParse::Param(FCommandLine::Get(), TEXT("D2UInstallFontToolForTests")))
	{
		AddError(TEXT("DeclaredWoff2 requires the optional font tool. Install it explicitly, or select -D2UInstallFontToolForTests for this dependency test."));
		return false;
	}
	if (PencilFontTool::HasInstallation() || PencilFontTool::IsBusy())
	{
		AddError(TEXT("DeclaredWoff2 setup refused: an existing installation or active operation must be preserved."));
		return false;
	}
	PencilFontTool::FDownload Download;
	FString Error;
	if (!PencilFontTool::ReadDownload(Download, Error)) { AddError(Error); return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FPrepareDeclaredWoff2Test(*this, Download, MoveTemp(VerifyImport)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignLocalFontDefaultPipelineTest,
	"Design2UMG.Fonts.LocalSource.DefaultPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignLocalFontDefaultPipelineTest::RunTest(const FString&)
{
	DesignLocalFontImport::FFace Face;
	FString Error;
	const FString Source = FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf");
	if (!TestTrue(TEXT("Read test face"), DesignLocalFontImport::ReadFace(Source, Face, Error))) return false;
	const FString PackageName = TEXT("/Game/Design2UMG/Fonts/PipelineFixture_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/PipelineFixture");
	DesignLocalFontImport::FFace Seed;
	if (!TestTrue(TEXT("Read distinct seed face"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), Seed, Error))) return false;
	if (!TestTrue(TEXT("Seed fixture with a different typeface"), DesignLocalFontImport::ImportFace(Seed, PackageName, Error, TEXT("Regular")))) return false;
	UFont* Font = LoadObject<UFont>(nullptr, *(PackageName + TEXT(".PipelineFixture")));
	if (!Font) return false;
	ON_SCOPE_EXIT { DeleteFontFixture(*this, PackageName); };
	TestTrue(TEXT("Required weight starts missing"), FFontResolver::FindExactTypefaceForAssetPath(Font->GetPathName(), TEXT("700")).IsEmpty());
	const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo HotInfo(Font, 24, TEXT("Bold"));
	const FVector2D Before = Measure->Measure(TEXT("MMMMiiii"), HotInfo);
	FFontResolver::RegisterFontAssetPath(Face.Family, Font->GetPathName());
	FDesignDocument Document;
	auto Node = MakeShared<FDesignNode>();
	Node->Text.Content = TEXT("Local font");
	Node->Text.FontFamily = Face.Family;
	Node->Text.FontWeight = TEXT("700");
	Document.Children.Add(Node);
	FDesignFont& MetadataOnlyFont = Document.Fonts.AddDefaulted_GetRef();
	MetadataOnlyFont.Name = Face.Family;
	const FImportAnalysisResult Result = DesignImportPlanning::AnalyzeImport(Document, FImportAnalysisInput{false});
	TestTrue(TEXT("No preparation failure"), Result.ErrorMessage.IsEmpty());
	const FString Path = FFontResolver::FindFontAssetPath(Face.Family);
	TestFalse(TEXT("Default analysis prepares a project font"), Path.IsEmpty());
	TestEqual(TEXT("Default analysis supplies the requested weight"),
		FFontResolver::FindExactTypefaceForAssetPath(Path, TEXT("700")), FString(TEXT("Bold")));
	TestEqual(TEXT("Existing family asset reused"), Path, Font->GetPathName());
	UFont* ColdFont = NewObject<UFont>();
	ColdFont->FontCacheType = EFontCacheType::Runtime;
	Design2UMGFontAccess::GetMutableCompositeFont(*ColdFont).DefaultTypeface.Fonts =
		Font->GetCompositeFont()->DefaultTypeface.Fonts;
	const FVector2D Expected = Measure->Measure(TEXT("MMMMiiii"), FSlateFontInfo(ColdFont, 24, TEXT("Bold")));
	const FVector2D After = Measure->Measure(TEXT("MMMMiiii"), HotInfo);
	TestFalse(TEXT("Fixture distinguishes old and new faces"), Before.Equals(Expected, 0.01));
	TestTrue(TEXT("An already used Slate font immediately sees the imported weight"), After.Equals(Expected, 0.01));
	const int32 Count = Font->GetCompositeFont()->DefaultTypeface.Fonts.Num();
	DesignImportPlanning::AnalyzeImport(Document, FImportAnalysisInput{false});
	TestEqual(TEXT("Repeat analysis does not append faces"), Font->GetCompositeFont()->DefaultTypeface.Fonts.Num(), Count);
	FDesignDocument ExternalDocument;
	auto Definition = MakeShared<FDesignNode>();
	Definition->Id = TEXT("external-font");
	Definition->Type = EDesignNodeType::Text;
	Definition->bReusable = true;
	Definition->Text = Node->Text;
	ExternalDocument.MutableSourceDocument().ImportedNodes.Add(Definition);
	auto Instance = MakeShared<FDesignNode>();
	Instance->Type = EDesignNodeType::Ref;
	Instance->Ref = Definition->Id;
	ExternalDocument.Children.Add(Instance);
	Design2UMGFontAccess::GetMutableCompositeFont(*Font).DefaultTypeface.Fonts.RemoveAll([](const FTypefaceEntry& Entry) { return Entry.Name == TEXT("Bold"); });
	FFontResolver::ClearCache();
	FFontResolver::RegisterFontAssetPath(Face.Family, Font->GetPathName());
	TArray<FString> ExternalWarnings;
	TestTrue(TEXT("External component fonts prepare through the existing importer"), DesignLocalFontImport::Prepare(ExternalDocument, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()), ExternalWarnings, Error));
	TestEqual(TEXT("External-only font requests import the missing weight"), FFontResolver::FindExactTypefaceForAssetPath(Font->GetPathName(), TEXT("700")), FString(TEXT("Bold")));
	const FString DeclaredFamily = TEXT("D2UDeclared_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString DeclaredPackage = FFontResolver::MakeImportedFamilyPackageName(DeclaredFamily);
	ON_SCOPE_EXIT { DeleteFontFixture(*this, DeclaredPackage, false); };
	if (!TestTrue(TEXT("Seed automatic cache with stale bytes"),
		DesignLocalFontImport::ImportFace(Seed, DeclaredPackage, Error, TEXT("Bold"), DeclaredFamily))) return false;
	FDesignDocument DeclaredDocument;
	FDesignFont& Declaration = DeclaredDocument.Fonts.AddDefaulted_GetRef();
	Declaration.Name = DeclaredFamily;
	Declaration.Url = FPaths::ConvertRelativePathToFull(Source);
	auto DeclaredNode = MakeShared<FDesignNode>();
	DeclaredNode->Text.Content = TEXT("Declared source");
	DeclaredNode->Text.FontFamily = DeclaredFamily;
	DeclaredNode->Text.FontWeight = TEXT("700");
	DeclaredDocument.Children.Add(DeclaredNode);
	TArray<FString> DeclaredWarnings;
	if (!TestTrue(TEXT("Declared name binds the source despite different internal family"),
		DesignLocalFontImport::Prepare(DeclaredDocument, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()), DeclaredWarnings, Error))) return false;
	UFont* DeclaredFont = LoadObject<UFont>(nullptr,
		*(DeclaredPackage + TEXT(".") + FPackageName::GetLongPackageAssetName(DeclaredPackage)));
	if (!TestNotNull(TEXT("Declared family asset loads"), DeclaredFont)) return false;
	const auto& DeclaredEntries = DeclaredFont->GetCompositeFont()->DefaultTypeface.Fonts;
	if (!TestEqual(TEXT("Declared source updates one existing typeface"), DeclaredEntries.Num(), 1)) return false;
	const UFontFace* DeclaredEmbedded = Cast<UFontFace>(DeclaredEntries[0].Font.GetFontFaceAsset());
	if (!TestNotNull(TEXT("Declared source is embedded"), DeclaredEmbedded)) return false;
	TArray<uint8> DeclaredBytes;
	TestTrue(TEXT("Read exact declared bytes"), FFileHelper::LoadFileToArray(DeclaredBytes, *Declaration.Url));
	TestTrue(TEXT("Declaration replaces stale automatic bytes"), DeclaredEmbedded->FontFaceData->GetData() == DeclaredBytes);
	TestEqual(TEXT("Declared name is registered"), FFontResolver::FindFontAssetPath(DeclaredFamily), DeclaredFont->GetPathName());
	TestTrue(TEXT("Unchanged declared source prepares again"),
		DesignLocalFontImport::Prepare(DeclaredDocument, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()), DeclaredWarnings, Error));
	TestTrue(TEXT("Identical source reuses embedded face"),
		DeclaredEntries[0].Font.GetFontFaceAsset() == DeclaredEmbedded);
	Declaration.Url = FPaths::ProjectSavedDir() / FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ttf");
	TestFalse(TEXT("Missing declared source does not fall back to automatic cache"),
		DesignLocalFontImport::Prepare(DeclaredDocument, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()), DeclaredWarnings, Error));
	TestTrue(TEXT("Declared failure identifies family, requested style and source"),
		Error.Contains(DeclaredFamily) && Error.Contains(TEXT("weight=700"))
		&& Error.Contains(TEXT("italic=false")) && Error.Contains(Declaration.Url));
	Error.Reset();
	Declaration.Url = FPaths::ConvertRelativePathToFull(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Light.ttf"));
	DesignLocalFontImport::FFace Light;
	if (!TestTrue(TEXT("Read declared light source"), DesignLocalFontImport::ReadFace(Declaration.Url, Light, Error))) return false;
	TestEqual(TEXT("Available source really has weight 300"), Light.Weight, 300);
	DeclaredNode->Text.FontWeight = TEXT("normal");
	if (!TestTrue(TEXT("Normal text uses the available declared face without aborting preparation"),
		DesignLocalFontImport::Prepare(DeclaredDocument, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()), DeclaredWarnings, Error))) return false;
	const auto Selection = FFontResolver::ResolveFontSelection(DeclaredFamily, FString(), TEXT("normal"));
	TestEqual(TEXT("Normal text keeps the declared family asset"), Selection.FontAssetPath, DeclaredFont->GetPathName());
	TestEqual(TEXT("Normal text selects the actual 300 typeface"), Selection.TypefaceName, FString(TEXT("Light")));
	TestTrue(TEXT("Preparation does not fabricate a regular 400 style"),
		FFontResolver::FindExactTypefaceForAssetPath(DeclaredFont->GetPathName(), TEXT("400")).IsEmpty());
	const auto* LightEntry = DeclaredEntries.FindByPredicate([](const FTypefaceEntry& Entry) { return Entry.Name == TEXT("Light"); });
	const UFontFace* LightEmbedded = LightEntry ? Cast<UFontFace>(LightEntry->Font.GetFontFaceAsset()) : nullptr;
	if (!TestNotNull(TEXT("Available light source is embedded"), LightEmbedded)) return false;
	TArray<uint8> LightBytes;
	TestTrue(TEXT("Read available declared bytes"), FFileHelper::LoadFileToArray(LightBytes, *Declaration.Url));
	TestTrue(TEXT("Selected face retains the actual source bytes"), LightEmbedded->FontFaceData->GetData() == LightBytes);
	Declaration.Url = FPaths::ConvertRelativePathToFull(Seed.Path);
	FDesignFont& ItalicDeclaration = DeclaredDocument.Fonts.AddDefaulted_GetRef();
	ItalicDeclaration.Name = DeclaredFamily;
	ItalicDeclaration.Url = FPaths::ConvertRelativePathToFull(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-BoldItalic.ttf"));
	DesignLocalFontImport::FFace Italic;
	if (!TestTrue(TEXT("Read the real declared italic face"), DesignLocalFontImport::ReadFace(ItalicDeclaration.Url, Italic, Error))) return false;
	TestTrue(TEXT("The fixture supplies real italic 700, not requested italic 500"), Italic.bItalic && Italic.Weight == 700);
	DeclaredNode->Type = EDesignNodeType::Text;
	DeclaredNode->Id = TEXT("declared-italic");
	DeclaredNode->Text.FontSize = 24;
	DeclaredNode->Text.FontWeight = TEXT("500");
	DeclaredNode->Text.FontStyle = TEXT("italic");
	auto DeclaredRich = MakeShared<FDesignNode>(*DeclaredNode);
	DeclaredRich->Id = TEXT("declared-rich");
	FDesignTextRun ItalicRun;
	ItalicRun.Content = TEXT("Declared italic run");
	ItalicRun.Color = TEXT("#ffffff");
	DeclaredRich->Text.TextRuns.Add(ItalicRun);
	DeclaredDocument.Children.Add(DeclaredRich);
	auto RomanTemplate = MakeShared<FDesignNode>(*DeclaredNode);
	RomanTemplate->Id = TEXT("declared-template");
	RomanTemplate->bReusable = true;
	RomanTemplate->Text.FontWeight = TEXT("400");
	RomanTemplate->Text.FontStyle = TEXT("normal");
	DeclaredDocument.Children.Add(RomanTemplate);
	auto DeclaredInstance = MakeShared<FDesignNode>();
	DeclaredInstance->Type = EDesignNodeType::Ref;
	DeclaredInstance->Id = TEXT("declared-instance");
	DeclaredInstance->Ref = RomanTemplate->Id;
	auto DeclaredPatch = MakeShared<FDesignNodePatch>();
	DeclaredPatch->FontWeight = TEXT("500");
	DeclaredPatch->FontStyle = TEXT("italic");
	DeclaredInstance->RefOverrides = DeclaredPatch;
	DeclaredDocument.Children.Add(DeclaredInstance);
	const auto DeclaredResult = DesignImportPlanning::AnalyzeImport(DeclaredDocument, FImportAnalysisInput{false});
	if (!TestTrue(TEXT("Declared styles prepare and convert through the production import entry"), DeclaredResult.ErrorMessage.IsEmpty())) return false;
	const FDesign2UMGFontMappings NoMappings;
	TestEqual(TEXT("Unprepared declarations retain the original roman selection"),
		FFontResolver::ResolveFontSelection(DeclaredFamily, FString(), TEXT("500"), TEXT("italic"), &NoMappings).TypefaceName,
		FString(TEXT("Regular")));
	TestEqual(TEXT("Measurement's shared selection keeps the prepared real italic face"),
		FFontResolver::ResolveFontSelection(DeclaredFamily, FString(), TEXT("500"), TEXT("italic"), &NoMappings,
			&DeclaredNode->Text.PreparedFont).TypefaceName, FString(TEXT("Bold Italic")));
	FDesign2UMGFontMappings ExactMapping;
	ExactMapping.Add(DeclaredFamily, DeclaredFont);
	TestTrue(TEXT("Prepared document selection cannot relax an explicit mapping's exact-style contract"),
		FFontResolver::ResolveFontSelection(DeclaredFamily, FString(), TEXT("500"), TEXT("italic"), &ExactMapping,
			&DeclaredNode->Text.PreparedFont).FontAssetPath.IsEmpty());
	bool bPlainItalic = false, bRichItalic = false, bInstanceItalic = false;
	TFunction<void(const FUMGWidgetNode&)> CheckDeclaredBindings = [&](const FUMGWidgetNode& Widget)
	{
		if (Widget.Type == TEXT("TextBlock"))
		{
			FSlateFontInfo BoundFont;
			bool bChanged = false;
			UMGTextStyleApplication::ApplyTypedFontInfo(BoundFont, Widget, bChanged);
			const bool bCorrectFace = bChanged && BoundFont.FontObject == DeclaredFont && BoundFont.TypefaceFontName == TEXT("Bold Italic");
			bPlainItalic |= Widget.Id == DeclaredNode->Id && bCorrectFace;
			bInstanceItalic |= Widget.Id == DeclaredInstance->Id && bCorrectFace;
		}
		if (Widget.Id == DeclaredRich->Id && Widget.Text.RichTextStyleRows.IsSet())
		{
			URichTextBlock* BoundRich = NewObject<URichTextBlock>();
			UMGTextStyleApplication::ApplyRichTextDefaultStyle(BoundRich, Widget);
			UMGTextStyleApplication::ApplyRichTextStyleRows(BoundRich, Widget.Text.RichTextStyleRows.GetValue());
			const UDataTable* Table = Design2UMGWidgetPropertyAccess::GetTextStyleSet(*BoundRich);
			const FRichTextStyleRow* Row = Table ? Table->FindRow<FRichTextStyleRow>(TEXT("d2u_run_0"), TEXT("Declared face binding"), false) : nullptr;
			bRichItalic = Row && Row->TextStyle.Font.FontObject == DeclaredFont && Row->TextStyle.Font.TypefaceFontName == TEXT("Bold Italic");
		}
		for (const auto& Child : Widget.Children) CheckDeclaredBindings(Child);
	};
	CheckDeclaredBindings(DeclaredResult.Data.RootWidget);
	TestTrue(TEXT("Ordinary text binds the selected real italic face"), bPlainItalic);
	TestTrue(TEXT("Inherited rich text binds the same selected face"), bRichItalic);
	TestTrue(TEXT("A reference style override selects its actual declared face before sizing and binding"), bInstanceItalic);
	FDesignDocument UsageDocument;
	FDesignFont& UnusedDeclaration = UsageDocument.Fonts.AddDefaulted_GetRef();
	UnusedDeclaration.Name = TEXT("D2UUnused_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TestTrue(TEXT("Unused declaration is not a missing used font"), FFontResolver::FindMissingFonts(UsageDocument).IsEmpty());
	auto MissingNode = MakeShared<FDesignNode>();
	MissingNode->Type = EDesignNodeType::Text;
	MissingNode->Text.Content = TEXT("Used missing font");
	MissingNode->Text.FontFamily = UnusedDeclaration.Name;
	UsageDocument.Children.Add(MissingNode);
	TestTrue(TEXT("Actually used missing font is still reported"),
		FFontResolver::FindMissingFonts(UsageDocument).Contains(UnusedDeclaration.Name));

	auto* Settings = GetMutableDefault<UDesign2UMGSettings>();
	const auto SavedMappings = Settings->ExplicitFontMappings;
	ON_SCOPE_EXIT { Settings->ExplicitFontMappings = SavedMappings; FFontResolver::ClearCache(); };
	Settings->ExplicitFontMappings.Add(Face.Family, Font);
	Design2UMGFontAccess::GetMutableCompositeFont(*Font).DefaultTypeface.Fonts.RemoveAll([](const FTypefaceEntry& Entry) { return Entry.Name == TEXT("Bold"); });
	FFontResolver::ClearCache();
	DesignImportPlanning::AnalyzeImport(Document, FImportAnalysisInput{false});
	TestEqual(TEXT("Explicit mapping prevents automatic modification"), Font->GetCompositeFont()->DefaultTypeface.Fonts.Num(), 1);
	Node->Text.FontFamily = TEXT("D2U Missing Local Font Fixture");
	const FImportAnalysisResult Missing = DesignImportPlanning::AnalyzeImport(Document, FImportAnalysisInput{false});
	TestTrue(TEXT("Missing face is recorded in generated-data diagnostics"),
		Missing.Data.Warnings.ContainsByPredicate([](const FString& Warning) { return Warning.Contains(TEXT("D2U Missing Local Font Fixture")); }));
	Settings->ExplicitFontMappings.Reset();
	FFontResolver::ClearCache();
	FFontResolver::RegisterFontAssetPath(Face.Family, Font->GetPathName());
	Node->Text.FontFamily = Face.Family;
	Node->Text.FontPostScriptName = Face.PostScriptName;
	const FString FontDirectory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/ManualFonts");
	IFileManager::Get().MakeDirectory(*FontDirectory, true);
	const FString LocatedSource = FontDirectory / (TEXT("!KnownFontSource_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ttf"));
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*LocatedSource); };
	if (!TestEqual(TEXT("Copy font bytes for metadata inspection"),
		IFileManager::Get().Copy(*LocatedSource, *Source), COPY_OK)) return false;
	DesignLocalFontImport::FSourceIndex SourceIndex;
	const auto* LocatedFace = SourceIndex.GetFaces().FindByPredicate([&](const DesignLocalFontImport::FFace& Candidate)
	{
		return Candidate.Family == Face.Family && Candidate.PostScriptName == Face.PostScriptName
			&& Candidate.Weight == Face.Weight && Candidate.bItalic == Face.bItalic;
	});
	if (!TestTrue(TEXT("The requested face resolves to the temporary source"),
		LocatedFace && FPaths::IsSamePath(LocatedFace->Path, LocatedSource))) return false;
	const FString OriginalSourcePath = LocatedFace->Path;
	if (!TestTrue(TEXT("Remove the located source after metadata inspection"),
		IFileManager::Get().Delete(*LocatedSource))) return false;
	TArray<FString> SourceWarnings;
	Error.Reset();
	if (!TestFalse(TEXT("Failure to import a located source stops font preparation"),
		DesignLocalFontImport::Prepare(Document, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()), SourceWarnings, Error, &SourceIndex))) return false;
	TestTrue(TEXT("Font source failure preserves the requested family, weight and style"),
		Error.Contains(Face.Family) && Error.Contains(TEXT("weight=700")) && Error.Contains(TEXT("italic=false")));
	TestTrue(TEXT("Font source failure preserves its PostScript name and original source"),
		Error.Contains(Face.PostScriptName) && Error.Contains(OriginalSourcePath));
	TestFalse(TEXT("A located source failure is not downgraded to a missing-font warning"),
		SourceWarnings.ContainsByPredicate([](const FString& Warning) { return Warning.Contains(TEXT("Local font unavailable")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocalFontRejectedReplacementTest,
	"Design2UMG.Fonts.Persistence.RejectedReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLocalFontRejectedReplacementTest::RunTest(const FString&)
{
	const FString StatePath = FPaths::ProjectSavedDir() / TEXT("Design2UMG/FontTests/RejectedReplacement.txt");
	if (!TestFalse(TEXT("Preserve a pending rejected-save fixture"), IFileManager::Get().FileExists(*StatePath))) return false;
	DesignLocalFontImport::FFace Face;
	FString Error;
	if (!TestTrue(TEXT("Read source"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"), Face, Error))) return false;
	Face.Family = TEXT("D2UPersistentFont_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Package = FFontResolver::MakeImportedFamilyPackageName(Face.Family);
	if (!TestTrue(TEXT("Save the original font"), DesignLocalFontImport::ImportFace(Face, Package, Error))) return false;
	const FString ObjectPath = Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
	UFont* Font = LoadObject<UFont>(nullptr, *ObjectPath);
	if (!TestNotNull(TEXT("Original font exists"), Font)) return false;
	UFontFace* Embedded = const_cast<UFontFace*>(Cast<UFontFace>(Font->GetCompositeFont()->DefaultTypeface.Fonts[0].Font.GetFontFaceAsset()));
	if (!TestNotNull(TEXT("Original embedded face exists"), Embedded)) return false;
	Embedded->LoadingPolicy = EFontLoadingPolicy::LazyLoad;
	if (!SaveFontPolicyFixture(*this, *Font)) return false;
	const FString Filename = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension()));
	TArray<uint8> Before, After;
	if (!TestTrue(TEXT("Read original disk bytes"), FFileHelper::LoadFileToArray(Before, *Filename))) return false;
	FString BackupFilename, RejectedFilename;
	int32 Rejections = 0;
	AddExpectedError(TEXT("Error moving file"), EAutomationExpectedErrorFlags::Contains, 1);
	{
		Design2UMGEditorTests::FScopedFileFailures FileFailures;
		FileFailures.RejectMove = [&](const FString& To, const FString& From)
		{
			if (From.Equals(Filename, ESearchCase::IgnoreCase)) BackupFilename = To;
			if (To.Equals(Filename, ESearchCase::IgnoreCase) && !From.Equals(BackupFilename, ESearchCase::IgnoreCase))
			{
				RejectedFilename = From;
				++Rejections;
				return true;
			}
			return false;
		};
		TestFalse(TEXT("Final file replacement failure reaches the caller"), DesignLocalFontImport::ImportFace(Face, Package, Error));
	}
	TestTrue(TEXT("The original file reached the backup boundary"), !BackupFilename.IsEmpty());
	TestTrue(TEXT("The final replacement was rejected"), Rejections > 0);
	TestTrue(TEXT("Failure identifies font persistence"), Error.StartsWith(TEXT("Failed to save local font:")));
	TestTrue(TEXT("Memory policy rolls back after late failure"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
	TestFalse(TEXT("Original clean state is restored"), Font->GetOutermost()->IsDirty());
	TestTrue(TEXT("Read restored original file"), FFileHelper::LoadFileToArray(After, *Filename));
	TestTrue(TEXT("Late failure preserves original file bytes"), Before == After);
	if (IFileManager::Get().FileExists(*RejectedFilename))
	{
		// The engine can retain an uncommitted output after a failed replacement.
		// Preserve this exact fault-injection artifact, then clean only the observed test file.
		const FString EvidenceDirectory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/FontTests/RejectedOutputs");
		IFileManager::Get().MakeDirectory(*EvidenceDirectory, true);
		const FString EvidenceFile = EvidenceDirectory / (FPackageName::GetLongPackageAssetName(Package) + TEXT(".uasset"));
		TestEqual(TEXT("Preserve the uncommitted output for inspection"),
			IFileManager::Get().Copy(*EvidenceFile, *RejectedFilename), uint32(COPY_OK));
		TestTrue(TEXT("Clean the exact fault-injection temporary file"), IFileManager::Get().Delete(*RejectedFilename));
		AddInfo(TEXT("The engine retained an uncommitted temporary output; the test preserved it and removed only that observed temporary file."));
	}
	TestFalse(TEXT("Fault-injection temporary file is cleaned"), IFileManager::Get().FileExists(*RejectedFilename));
	TestFalse(TEXT("No backup file remains"), IFileManager::Get().FileExists(*BackupFilename));
	const FString State = FString::Printf(TEXT("%u\n%s\n%s\n"), FPlatformProcess::GetCurrentProcessId(), *Face.Family, *ObjectPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(StatePath), true);
	TestTrue(TEXT("Record the rejected-save fixture for a separate process"), FFileHelper::SaveStringToFile(
		State, *StatePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocalFontRetryReplacementTest,
	"Design2UMG.Fonts.Persistence.RetryReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLocalFontRetryReplacementTest::RunTest(const FString&)
{
	const FString StatePath = FPaths::ProjectSavedDir() / TEXT("Design2UMG/FontTests/RejectedReplacement.txt");
	const FString PersistencePath = FPaths::ProjectSavedDir() / TEXT("Design2UMG/FontTests/Persistence.txt");
	FString State;
	if (!TestTrue(TEXT("Read rejected-save state"), FFileHelper::LoadFileToString(State, *StatePath))) return false;
	TArray<FString> Fields;
	State.ParseIntoArrayLines(Fields);
	if (!TestEqual(TEXT("Rejected state has process, family and asset"), Fields.Num(), 3)) return false;
	const FString Package = FFontResolver::MakeImportedFamilyPackageName(Fields[1]);
	if (!TestTrue(TEXT("Only the owned persistence fixture is retried"), Fields[1].StartsWith(TEXT("D2UPersistentFont_"))
		&& Fields[2] == Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package))) return false;
	if (!TestTrue(TEXT("Rejected file is loaded in a different process"), FCString::Strtoui64(*Fields[0], nullptr, 10) != FPlatformProcess::GetCurrentProcessId())) return false;
	UFont* Font = LoadObject<UFont>(nullptr, *Fields[2]);
	if (!TestNotNull(TEXT("Old font file is still loadable"), Font)) return false;
	const UFontFace* Embedded = Cast<UFontFace>(Font->GetCompositeFont()->DefaultTypeface.Fonts[0].Font.GetFontFaceAsset());
	if (!TestNotNull(TEXT("Old embedded face is still loadable"), Embedded)) return false;
	TestTrue(TEXT("Old disk policy survived the failed replacement"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
	TArray<uint8> Expected;
	TestTrue(TEXT("Read expected bytes"), FFileHelper::LoadFileToArray(Expected, *(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"))));
	TestTrue(TEXT("Old disk bytes survive restart"), Embedded->FontFaceData->GetData() == Expected);
	DesignLocalFontImport::FFace Face;
	FString Error;
	if (!TestTrue(TEXT("Read retry source"), DesignLocalFontImport::ReadFace(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"), Face, Error))) return false;
	Face.Family = Fields[1];
	if (!TestTrue(TEXT("Retry saves at the same asset path"), DesignLocalFontImport::ImportFace(Face, Package, Error))) return false;
	TestTrue(TEXT("Retry preserves the original face object"), Font->GetCompositeFont()->DefaultTypeface.Fonts[0].Font.GetFontFaceAsset() == Embedded);
	TestTrue(TEXT("Retry persists Inline"), Embedded->LoadingPolicy == EFontLoadingPolicy::Inline);
	if (!TestFalse(TEXT("Preserve a pending successful-save fixture"), IFileManager::Get().FileExists(*PersistencePath))) return false;
	const FString Saved = FString::Printf(TEXT("%u\n%s\n%s\n"), FPlatformProcess::GetCurrentProcessId(), *Fields[1], *Fields[2]);
	TestTrue(TEXT("Record successful retry for another restart"), FFileHelper::SaveStringToFile(Saved, *PersistencePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	TestTrue(TEXT("Remove rejected-save state"), IFileManager::Get().Delete(*StatePath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocalFontPersistenceSaveTest,
	"Design2UMG.Fonts.Persistence.Save",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLocalFontPersistenceSaveTest::RunTest(const FString&)
{
	const FString StatePath = FPaths::ProjectSavedDir() / TEXT("Design2UMG/FontTests/Persistence.txt");
	if (!TestFalse(TEXT("Preserve any pending persistence fixture"), IFileManager::Get().FileExists(*StatePath))) return false;
	DesignLocalFontImport::FFace Face;
	FString Error;
	if (!TestTrue(TEXT("Read persistence source"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"), Face, Error))) return false;
	Face.Family = TEXT("D2UPersistentFont_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Package = FFontResolver::MakeImportedFamilyPackageName(Face.Family);
	if (!TestTrue(TEXT("Save persistent font through the importer"), DesignLocalFontImport::ImportFace(Face, Package, Error))) return false;
	const FString ObjectPath = Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
	UFont* Font = LoadObject<UFont>(nullptr, *ObjectPath);
	if (!TestNotNull(TEXT("Persistent font exists"), Font)) return false;
	UFontFace* Embedded = const_cast<UFontFace*>(Cast<UFontFace>(Font->GetCompositeFont()->DefaultTypeface.Fonts[0].Font.GetFontFaceAsset()));
	if (!TestNotNull(TEXT("Persistent embedded face exists"), Embedded) || !AddFallbackFixture(*this, *Font)) return false;
	UDesign2UMGSettings* Settings = GetMutableDefault<UDesign2UMGSettings>();
	const auto PreviousMappings = Settings->ExplicitFontMappings;
	ON_SCOPE_EXIT
	{
		Settings->ExplicitFontMappings = PreviousMappings;
		FFontResolver::ClearCache();
	};
	Settings->ExplicitFontMappings.Add(Face.Family, Font);
	Embedded->LoadingPolicy = EFontLoadingPolicy::LazyLoad;
	FDesignDocument Document;
	auto Node = MakeShared<FDesignNode>();
	Node->Text.Content = TEXT("Mapped persistent font");
	Node->Text.FontFamily = Face.Family;
	Node->Text.FontWeight = TEXT("700");
	Document.Children.Add(Node);
	TArray<FString> Diagnostics;
	if (!SaveFontPolicyFixture(*this, *Font)) return false;
	const FString MappedFilename = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
	const FMD5Hash MappedIdentity = FMD5Hash::HashFile(*MappedFilename);
	if (!TestTrue(TEXT("Prepare mapped generated font without changing its policy"), DesignLocalFontImport::Prepare(
		Document, FDesign2UMGConversionSettings::FromSettings(Settings), Diagnostics, Error))
		|| !TestTrue(TEXT("Mapped generated face retains LazyLoad"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad)
		|| !TestTrue(TEXT("Mapped asset bytes remain unchanged"), FMD5Hash::HashFile(*MappedFilename) == MappedIdentity)
		|| !TestTrue(TEXT("Preparation preserves the explicit mapping"), Settings->ExplicitFontMappings[Face.Family].Get() == Font))
	{
		DeleteFontFixture(*this, Package, false);
		return false;
	}
	Settings->ExplicitFontMappings = PreviousMappings;
	FFontResolver::ClearCache();
	if (!TestTrue(TEXT("Prepare the now-unconfigured automatic font in place"), DesignLocalFontImport::Prepare(
		Document, FDesign2UMGConversionSettings::FromSettings(Settings), Diagnostics, Error))) return false;
	TestTrue(TEXT("In-place cleanup preserves the old face policy"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
	TestTrue(TEXT("In-place cleanup removes the saved fallback"), Font->GetCompositeFont()->FallbackTypeface.Typeface.Fonts.IsEmpty());
	if (!TestTrue(TEXT("Explicit unmapped reimport persists the source before restart"),
		DesignLocalFontImport::ImportFace(Face, Package, Error))
		|| !TestTrue(TEXT("Unmapped imported face is Inline before restart"), Embedded->LoadingPolicy == EFontLoadingPolicy::Inline))
	{
		DeleteFontFixture(*this, Package, false);
		return false;
	}
	const FString State = FString::Printf(TEXT("%u\n%s\n%s\n"), FPlatformProcess::GetCurrentProcessId(), *Face.Family, *ObjectPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(StatePath), true);
	if (!TestTrue(TEXT("Record the saved fixture and originating process"), FFileHelper::SaveStringToFile(
		State, *StatePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
	{
		DeleteFontFixture(*this, Package, false);
		TestTrue(TEXT("Remove incomplete persistence state"), IFileManager::Get().Delete(*StatePath));
		return false;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocalFontPersistenceReloadTest,
	"Design2UMG.Fonts.Persistence.Reload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLocalFontPersistenceReloadTest::RunTest(const FString&)
{
	const FString StatePath = FPaths::ProjectSavedDir() / TEXT("Design2UMG/FontTests/Persistence.txt");
	FString State;
	if (!TestTrue(TEXT("Read the saved persistence fixture"), FFileHelper::LoadFileToString(State, *StatePath))) return false;
	TArray<FString> Fields;
	State.ParseIntoArrayLines(Fields);
	if (!TestEqual(TEXT("Persistence fixture has process, family and asset path"), Fields.Num(), 3)) return false;
	const FString Package = FFontResolver::MakeImportedFamilyPackageName(Fields[1]);
	const FString ExpectedPath = Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
	if (!TestTrue(TEXT("Persistence cleanup is limited to the test family"), Fields[1].StartsWith(TEXT("D2UPersistentFont_")) && Fields[2] == ExpectedPath)) return false;
	if (!TestTrue(TEXT("Reload runs in a different editor process"), FCString::Strtoui64(*Fields[0], nullptr, 10) != FPlatformProcess::GetCurrentProcessId())) return false;
	UFont* Font = LoadObject<UFont>(nullptr, *Fields[2]);
	if (!TestNotNull(TEXT("Font loads from its saved package after restart"), Font)) return false;
	ON_SCOPE_EXIT
	{
		DeleteFontFixture(*this, Package, false);
		TestTrue(TEXT("Remove persistence state"), IFileManager::Get().Delete(*StatePath));
		TestFalse(TEXT("Persistence state is removed"), IFileManager::Get().FileExists(*StatePath));
	};
	TestTrue(TEXT("Removed fallback stays absent after restart"), Font->GetCompositeFont()->FallbackTypeface.Typeface.Fonts.IsEmpty());
	TestTrue(TEXT("Runtime font cache survives restart"), Font->FontCacheType == EFontCacheType::Runtime);
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
	TestTrue(TEXT("Asset font source survives restart"), Font->RuntimeFontSource == ERuntimeFontSource::Asset);
#endif
	const auto& Entries = Font->GetCompositeFont()->DefaultTypeface.Fonts;
	if (TestEqual(TEXT("Saved font retains one typeface"), Entries.Num(), 1))
	{
		TestEqual(TEXT("Saved typeface name survives restart"), Entries[0].Name, FName(TEXT("Bold")));
		const UFontFace* Embedded = Cast<UFontFace>(Entries[0].Font.GetFontFaceAsset());
		if (TestNotNull(TEXT("Embedded face survives restart"), Embedded))
		{
			TestTrue(TEXT("Unmapped explicitly imported policy survives restart"), Embedded->LoadingPolicy == EFontLoadingPolicy::Inline);
			TArray<uint8> Source;
			TestTrue(TEXT("Read expected face bytes"), FFileHelper::LoadFileToArray(Source,
				*(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"))));
			TestTrue(TEXT("Embedded face bytes survive restart"), Embedded->FontFaceData->GetData() == Source);
		}
	}
	FFontResolver::ClearCache();
	TestEqual(TEXT("Family is rediscovered without transient registration"), FFontResolver::FindFontAssetPath(Fields[1]), Fields[2]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocumentGlyphFallbackSaveTest,
	"Design2UMG.Fonts.GlyphFallback.Save",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDocumentGlyphFallbackSaveTest::RunTest(const FString&)
{
	const FString StatePath = FPaths::ProjectSavedDir() / TEXT("Design2UMG/FontTests/GlyphFallback.txt");
	if (!TestFalse(TEXT("Preserve a pending glyph fallback fixture"), IFileManager::Get().FileExists(*StatePath))) return false;
	const FString Directory = TEXT("/Game/Design2UMG/Automation/GlyphFallback_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString PrimaryPackage = Directory / TEXT("Primary");
	const FString DonorPackage = Directory / TEXT("Donor");
	FString DerivedPackage;
	bool bKeepForReload = false;
	ON_SCOPE_EXIT
	{
		if (!bKeepForReload)
		{
			IFileManager::Get().Delete(*StatePath);
			IFileManager::Get().Delete(*FPaths::ChangeExtension(StatePath, TEXT("json")));
			if (!DerivedPackage.IsEmpty()) DeleteFontFixture(*this, DerivedPackage, false);
			DeleteFontFixture(*this, DonorPackage, false);
			DeleteFontFixture(*this, PrimaryPackage, false);
		}
	};
	FString Error;
	DesignLocalFontImport::FFace PrimarySource, DonorSource;
	if (!TestTrue(TEXT("Read primary fixture"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), PrimarySource, Error))
		|| !TestTrue(TEXT("Read donor fixture"), DesignLocalFontImport::ReadFace(
			FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf"), DonorSource, Error))) return false;
	if (!TestTrue(TEXT("Import primary fixture"), DesignLocalFontImport::ImportFace(PrimarySource, PrimaryPackage, Error, TEXT("Regular")))
		|| !TestTrue(TEXT("Import donor fixture"), DesignLocalFontImport::ImportFace(DonorSource, DonorPackage, Error, TEXT("Regular")))) return false;
	UFont* Primary = LoadObject<UFont>(nullptr, *(PrimaryPackage + TEXT(".Primary")));
	UFont* Donor = LoadObject<UFont>(nullptr, *(DonorPackage + TEXT(".Donor")));
	if (!TestNotNull(TEXT("Primary fixture exists"), Primary) || !TestNotNull(TEXT("Donor fixture exists"), Donor)) return false;
	// Source imports must preserve the document font boundary before donor preparation.
	auto& PrimaryComposite = Design2UMGFontAccess::GetMutableCompositeFont(*Primary);
	TestTrue(TEXT("Primary import has no external fallback"), PrimaryComposite.FallbackTypeface.Typeface.Fonts.IsEmpty());
	// The donor's default face also lacks these glyphs; its native subfont is the only supplier.
	auto& DonorComposite = Design2UMGFontAccess::GetMutableCompositeFont(*Donor);
	const FFontData DonorData = DonorComposite.DefaultTypeface.Fonts[0].Font;
	DonorComposite.DefaultTypeface.Fonts[0].Font = PrimaryComposite.DefaultTypeface.Fonts[0].Font;
	TestTrue(TEXT("Donor import has no external fallback"), DonorComposite.FallbackTypeface.Typeface.Fonts.IsEmpty());
	FCompositeSubFont& DonorSubfont = DonorComposite.SubTypefaces.AddDefaulted_GetRef();
	DonorSubfont.ScalingFactor = 0.75f;
	DonorSubfont.CharacterRanges = {FInt32Range::Inclusive(0x4e2d, 0x4e2d), FInt32Range::Inclusive(0x65e5, 0x65e5)};
	FTypefaceEntry DonorEntry(FName(TEXT("Regular")));
	DonorEntry.Font = DonorData;
	DonorSubfont.Typeface.Fonts.Add(MoveTemp(DonorEntry));
	DonorComposite.MakeDirty();
	if (!SaveFontPolicyFixture(*this, *Donor)) return false;
	UDesign2UMGSettings* Settings = GetMutableDefault<UDesign2UMGSettings>();
	const auto PreviousMappings = Settings->ExplicitFontMappings;
	const auto PreviousDefault = Settings->DefaultFontAsset;
	const auto PreviousIconMap = Settings->IconFontAssetMap;
	const auto PreviousDefaultIcon = Settings->DefaultIconFontAssetPath;
	ON_SCOPE_EXIT
	{
		Settings->ExplicitFontMappings = PreviousMappings;
		Settings->DefaultFontAsset = PreviousDefault;
		Settings->IconFontAssetMap = PreviousIconMap;
		Settings->DefaultIconFontAssetPath = PreviousDefaultIcon;
		FFontResolver::ClearCache();
	};
	Settings->ExplicitFontMappings.Add(TEXT("GlyphPrimary"), Primary);
	Settings->ExplicitFontMappings.Add(TEXT("GlyphDonor"), Donor);
	Settings->DefaultFontAsset = Primary;
	Settings->IconFontAssetMap.Add(TEXT("GlyphIcon"), Donor);
	Settings->DefaultIconFontAssetPath = Primary->GetPathName();
	const auto CapturedSettings = FDesign2UMGConversionSettings::FromSettings(Settings);
	const auto& FontMappings = CapturedSettings.ExplicitFontMappings;
	Settings->ExplicitFontMappings[TEXT("GlyphPrimary")] = Donor;
	Settings->ExplicitFontMappings[TEXT("GlyphDonor")] = Primary;
	FFontResolver::ClearCache();
	const auto FileIdentity = [](const FString& Package)
	{
		return LexToString(FMD5Hash::HashFile(*FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension())));
	};
	const FString PrimaryIdentity = FileIdentity(PrimaryPackage);
	const FString DonorIdentity = FileIdentity(DonorPackage);
	FDesignDocument Document;
	auto Text = MakeShared<FDesignNode>();
	Text->Type = EDesignNodeType::Text;
	Text->Name = TEXT("Mixed text");
	Text->Id = TEXT("mixed-text");
	Text->Text.FontFamily = TEXT("GlyphPrimary");
	Text->Text.FontWeight = TEXT("400");
	Text->Text.Content = TEXT("A\u4e2d\u65e5");
	Document.Children.Add(Text);
	auto Rich = MakeShared<FDesignNode>(*Text);
	Rich->Id = TEXT("rich-text");
	FDesignTextRun Run;
	Run.Content = Text->Text.Content;
	Run.Color = TEXT("#ffffff");
	Rich->Text.TextRuns.Add(Run);
	Document.Children.Add(Rich);
	auto DonorText = MakeShared<FDesignNode>(*Text);
	DonorText->Id = TEXT("donor-text");
	DonorText->Text.FontFamily = TEXT("GlyphDonor");
	Document.Children.Add(DonorText);
	FSlateFontCache& Cache = *FSlateApplication::Get().GetRenderer()->GetFontCache();
	TestFalse(TEXT("Primary really lacks the fixture glyph"), Cache.CanLoadCodepoint(
		Cache.GetDefaultFontData(FSlateFontInfo(Primary, 14, TEXT("Regular"))), 0x4e2d, EFontFallback::FF_NoFallback));
	TestFalse(TEXT("Donor default face also lacks the fixture glyph"), Cache.CanLoadCodepoint(
		Cache.GetDefaultFontData(FSlateFontInfo(Donor, 14, TEXT("Regular"))), 0x4e2d, EFontFallback::FF_NoFallback));
	float DonorScale = 1.0f;
	const FFontData& NativeDonor = Cache.GetFontDataForCodepoint(FSlateFontInfo(Donor, 14, TEXT("Regular")), 0x4e2d, DonorScale);
	TestTrue(TEXT("Document donor's own native subfont supports the glyph"), Cache.CanLoadCodepoint(NativeDonor, 0x4e2d, EFontFallback::FF_NoFallback));
	TestEqual(TEXT("Native subfont has an effective scale distinct from the default"), DonorScale, 0.75f);
	TestFalse(TEXT("Primary lacks the adjacent distinct fixture glyph"), Cache.CanLoadCodepoint(
		Cache.GetDefaultFontData(FSlateFontInfo(Primary, 14, TEXT("Regular"))), 0x65e5, EFontFallback::FF_NoFallback));
	float NativeAdjacentScale = 1.0f;
	const FFontData& NativeAdjacent = Cache.GetFontDataForCodepoint(FSlateFontInfo(Donor, 14, TEXT("Regular")), 0x65e5, NativeAdjacentScale);
	TestTrue(TEXT("Document donor supports the adjacent distinct fixture glyph"), Cache.CanLoadCodepoint(NativeAdjacent, 0x65e5, EFontFallback::FF_NoFallback));
	TArray<FString> Warnings;
	TArray<FSlateFontInfo> DocumentDonors;
	TArray<FSlateFontInfo> UnsupportedDonors;
	TMap<FFontData, FString> FontDataHashes;
	FDesignDocument Unsupported;
	Unsupported.Children = {Text, Rich};
	TestTrue(TEXT("Missing document donor remains a diagnostic, not a failed import"),
		DesignLocalFontImport::PrepareGlyphFallbacks(Unsupported, FontMappings, UnsupportedDonors, FontDataHashes, Warnings, Error));
	TestEqual(TEXT("Repeated missing characters in two nodes are aggregated by font"), Warnings.Num(), 1);
	if (!Warnings.IsEmpty())
	{
		TestTrue(TEXT("Diagnostic identifies the missing character and source asset"),
			Warnings[0].Contains(TEXT("U+4E2D")) && Warnings[0].Contains(Primary->GetPathName()));
		FUMGWidgetData WarningData;
		WarningData.Warnings = Warnings;
		const FString Message = DesignImportDialogs::FormatImportResult(WarningData, FString(), true);
		TestTrue(TEXT("Existing completion message carries the aggregated glyph diagnostic"), Message.Contains(Warnings[0]));
	}
	TestTrue(TEXT("No external font is substituted when the document has no donor"), Text->Text.PreparedFont.AssetPath.IsEmpty());
	Warnings.Reset();
	if (!TestTrue(TEXT("Prepare source fonts through the existing import entry"), DesignLocalFontImport::Prepare(Document, CapturedSettings, Warnings, Error))) return false;
	{
		Design2UMGEditorTests::FScopedPackageSaveFailure RejectDerived([](const FString& Filename)
		{
			return Filename.Contains(TEXT("/GlyphFallbacks/")) || Filename.Contains(TEXT("\\GlyphFallbacks\\"));
		});
		TestFalse(TEXT("A rejected composite save fails preparation"),
			DesignLocalFontImport::PrepareGlyphFallbacks(Document, FontMappings, DocumentDonors, FontDataHashes, Warnings, Error));
		TestTrue(TEXT("A failed save does not bind an unsaved composite"), Text->Text.PreparedFont.AssetPath.IsEmpty());
	}
	if (!TestTrue(TEXT("Prepare document-local glyph fallback with captured mappings"),
		DesignLocalFontImport::PrepareGlyphFallbacks(Document, FontMappings, DocumentDonors, FontDataHashes, Warnings, Error))) return false;
	TestTrue(TEXT("Empty fonts header does not warn when document fonts cover the text"), Warnings.IsEmpty());
	const FString DerivedPath = Text->Text.PreparedFont.AssetPath;
	if (!TestFalse(TEXT("Missing glyph receives a derived asset"), DerivedPath.IsEmpty())) return false;
	DerivedPackage = FPackageName::ObjectPathToPackageName(DerivedPath);
	TestTrue(TEXT("Complete donor text keeps its original asset"), DonorText->Text.PreparedFont.AssetPath.IsEmpty());
	TestEqual(TEXT("Source family is preserved"), Text->Text.FontFamily, FString(TEXT("GlyphPrimary")));
	TestEqual(TEXT("Rich run receives the same prepared selection"), Rich->Text.TextRuns[0].PreparedFont.AssetPath, DerivedPath);
	TestTrue(TEXT("Repeated preparation succeeds"),
		DesignLocalFontImport::PrepareGlyphFallbacks(Document, FontMappings, DocumentDonors, FontDataHashes, Warnings, Error));
	TestEqual(TEXT("Repeated preparation reuses the derived asset"), Text->Text.PreparedFont.AssetPath, DerivedPath);
	TestEqual(TEXT("Primary asset is never rewritten"), FileIdentity(PrimaryPackage), PrimaryIdentity);
	TestEqual(TEXT("Donor asset is never rewritten"), FileIdentity(DonorPackage), DonorIdentity);
	TestTrue(TEXT("Primary native fallback is never changed"), Primary->GetCompositeFont()->FallbackTypeface.Typeface.Fonts.IsEmpty());
	TestEqual(TEXT("Prepared primary comes from the captured mapping"), Text->Text.PreparedFont.SourceAssetPath, Primary->GetPathName());
	TestEqual(TEXT("Current global policy is different, proving the snapshot fixture discriminates"),
		FFontResolver::ResolveFontSelection(TEXT("GlyphPrimary"), FString(), TEXT("400")).FontAssetPath, Donor->GetPathName());
	TestTrue(TEXT("Changed global mappings are not rewritten by preparation"), Settings->ExplicitFontMappings[TEXT("GlyphPrimary")].Get() == Donor
		&& Settings->ExplicitFontMappings[TEXT("GlyphDonor")].Get() == Primary);
	TestTrue(TEXT("Default and icon configurations remain intact"), Settings->DefaultFontAsset.Get() == Primary
		&& Settings->IconFontAssetMap[TEXT("GlyphIcon")].Get() == Donor && Settings->DefaultIconFontAssetPath == Primary->GetPathName());
	auto Template = MakeShared<FDesignNode>(*Text);
	Template->Id = TEXT("glyph-template");
	Template->bReusable = true;
	Template->Text.Content = TEXT("A");
	Template->Text.PreparedFont = FDesignPreparedFont();
	Document.Children.Add(Template);
	auto Instance = MakeShared<FDesignNode>();
	Instance->Type = EDesignNodeType::Ref;
	Instance->Id = TEXT("glyph-instance");
	Instance->Ref = Template->Id;
	auto InstancePatch = MakeShared<FDesignNodePatch>();
	InstancePatch->Content = Text->Text.Content;
	Instance->RefOverrides = InstancePatch;
	Document.Children.Add(Instance);
	int32 PreparedInstances = 0;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, CapturedSettings,
		[&](const FDesignDocument& Expanded, TArray<FString>& InstanceWarnings, FString& InstanceError,
			FDesign2UMGConverter::EFontPreparation Preparation)
		{
			++PreparedInstances;
			if (!TestEqual(TEXT("Reference preparation only visits the actual expanded subtree"), Expanded.Children.Num(), 1)) return false;
			TestEqual(TEXT("Instance content is final before preparing its font"), Expanded.Children.Last()->Text.Content, Text->Text.Content);
			TestTrue(TEXT("Reference preparation only reuses existing fonts"), Preparation == FDesign2UMGConverter::EFontPreparation::GlyphCoverage);
			const bool bPrepared = DesignLocalFontImport::PrepareGlyphFallbacks(Expanded, FontMappings, DocumentDonors, FontDataHashes, InstanceWarnings, InstanceError);
			TestEqual(TEXT("Expanded instance binds the composite before sizing"), Expanded.Children.Last()->Text.PreparedFont.AssetPath, DerivedPath);
			return bPrepared;
		});
	TestTrue(TEXT("Reference expansion runs font preparation"), PreparedInstances > 0);
	TestTrue(TEXT("Conversion succeeds"), Data.ConversionError.IsEmpty());
	const FString Json = FUMGJsonSerializer::Serialize(Data);
	const FString JsonPath = FPaths::ChangeExtension(StatePath, TEXT("json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(StatePath), true);
	TestTrue(TEXT("Persist generated bindings for a fresh process"), FFileHelper::SaveStringToFile(
		Json, *JsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	const FString State = FString::Printf(TEXT("%u\n%s\n%s\n%s\n%s\n%s\n"), FPlatformProcess::GetCurrentProcessId(),
		*Primary->GetPathName(), *Donor->GetPathName(), *DerivedPath, *PrimaryIdentity, *DonorIdentity);
	TestTrue(TEXT("Record only owned fixture paths"), FFileHelper::SaveStringToFile(
		State, *StatePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	bKeepForReload = !HasAnyErrors();
	return bKeepForReload;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocumentGlyphFallbackReloadTest,
	"Design2UMG.Fonts.GlyphFallback.Reload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDocumentGlyphFallbackReloadTest::RunTest(const FString&)
{
	const FString StatePath = FPaths::ProjectSavedDir() / TEXT("Design2UMG/FontTests/GlyphFallback.txt");
	FString State, Json;
	if (!TestTrue(TEXT("Read saved fixture state"), FFileHelper::LoadFileToString(State, *StatePath))) return false;
	TArray<FString> Fields;
	State.ParseIntoArrayLines(Fields);
	if (!TestEqual(TEXT("Persistence state contains process, assets and original identities"), Fields.Num(), 6)) return false;
	if (!TestTrue(TEXT("Reload uses a different editor process"), FCString::Strtoui64(*Fields[0], nullptr, 10) != FPlatformProcess::GetCurrentProcessId())) return false;
	if (!TestTrue(TEXT("Cleanup is restricted to owned fixtures"), Fields[1].StartsWith(TEXT("/Game/Design2UMG/Automation/GlyphFallback_"))
		&& Fields[2].StartsWith(TEXT("/Game/Design2UMG/Automation/GlyphFallback_"))
		&& Fields[3].StartsWith(TEXT("/Game/Design2UMG/Fonts/GlyphFallbacks/GlyphFallback_")))) return false;
	const FString JsonPath = FPaths::ChangeExtension(StatePath, TEXT("json"));
	ON_SCOPE_EXIT
	{
		const TArray<FString> OwnedPackages = {
			FPackageName::ObjectPathToPackageName(Fields[3]),
			FPackageName::ObjectPathToPackageName(Fields[2]),
			FPackageName::ObjectPathToPackageName(Fields[1])};
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand(
			[this, OwnedPackages, StatePath, JsonPath]()
			{
				if (IsAsyncLoading()) return false;
				// Delete dependents after the loader releases their public exports.
				for (const FString& Package : OwnedPackages) DeleteFontFixture(*this, Package, false);
				TestTrue(TEXT("Remove fixture state"), IFileManager::Get().Delete(*StatePath));
				TestTrue(TEXT("Remove generated binding fixture"), IFileManager::Get().Delete(*JsonPath));
				return true;
			},
			[this, StatePath]()
			{
				AddError(FString::Printf(TEXT("Timed out waiting for font fixture loading to finish; owned assets and state are retained: %s"), *StatePath));
				return true;
			}));
	};
	UFont* Primary = LoadObject<UFont>(nullptr, *Fields[1]);
	UFont* Donor = LoadObject<UFont>(nullptr, *Fields[2]);
	UFont* Font = LoadObject<UFont>(nullptr, *Fields[3]);
	if (!TestNotNull(TEXT("Primary reloads"), Primary) || !TestNotNull(TEXT("Donor reloads"), Donor)
		|| !TestNotNull(TEXT("Derived native UFont reloads"), Font)) return false;
	for (int32 Index = 1; Index <= 2; ++Index)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(
			FPackageName::ObjectPathToPackageName(Fields[Index]), FPackageName::GetAssetPackageExtension());
		TestEqual(TEXT("Original asset identity survives the separate process"), LexToString(FMD5Hash::HashFile(*Filename)), Fields[Index + 3]);
	}
	FSlateFontCache& Cache = *FSlateApplication::Get().GetRenderer()->GetFontCache();
	const FSlateFontInfo Info(Font, 14, TEXT("Regular"));
	float Scale = 1.0f;
	const FFontData& Supplement = Cache.GetFontDataForCodepoint(Info, 0x4e2d, Scale);
	float AdjacentScale = 1.0f;
	const FFontData& Adjacent = Cache.GetFontDataForCodepoint(Info, 0x65e5, AdjacentScale);
	TestTrue(TEXT("Adjacent different missing characters share one native FontData entry"), &Supplement == &Adjacent);
	float NativeScale = 1.0f;
	const FFontData& NativeDonor = Cache.GetFontDataForCodepoint(FSlateFontInfo(Donor, 14, TEXT("Regular")), 0x4e2d, NativeScale);
	TestEqual(TEXT("The original donor subfont scale survives reload"), NativeScale, 0.75f);
	TestEqual(TEXT("The derived glyph preserves the native donor's effective scale"), Scale, NativeScale);
	TestEqual(TEXT("Adjacent supplied glyphs keep the same native scale"), AdjacentScale, NativeScale);
	TestTrue(TEXT("Adjacent glyph loads without LastResort"), Cache.CanLoadCodepoint(Adjacent, 0x65e5, EFontFallback::FF_NoFallback));
	const UFontFace* SupplementFace = Cast<UFontFace>(Supplement.GetFontFaceAsset());
	if (!TestNotNull(TEXT("Native subfont asset reference reloads"), SupplementFace)) return false;
	TestTrue(TEXT("Subfont reuses the original donor face without copying or rewriting it"),
		SupplementFace == NativeDonor.GetFontFaceAsset());
	TestTrue(TEXT("Original donor loading policy remains inline"), SupplementFace->LoadingPolicy == EFontLoadingPolicy::Inline);
	TestTrue(TEXT("Native font supplies the actual glyph without LastResort"), Cache.CanLoadCodepoint(Supplement, 0x4e2d, EFontFallback::FF_NoFallback));
	const FFontData& Latin = Cache.GetFontDataForCodepoint(Info, 'A', Scale);
	TestTrue(TEXT("Existing glyph keeps the primary face"), Latin == Cache.GetDefaultFontData(Info));
	TestTrue(TEXT("Primary typeface bytes are preserved"), Latin.GetFontFaceData()->GetData()
		== Cache.GetDefaultFontData(FSlateFontInfo(Primary, 14, TEXT("Regular"))).GetFontFaceData()->GetData());
	TestTrue(TEXT("Supplement contains the imported donor bytes"), Supplement.GetFontFaceData()->GetData()
		== NativeDonor.GetFontFaceData()->GetData());
	if (!TestTrue(TEXT("Read generated UMG data"), FFileHelper::LoadFileToString(Json, *JsonPath))) return false;
	FUMGWidgetData Parsed;
	if (!TestTrue(TEXT("Resolved font bindings survive JSON parsing"), FUMGJsonParser::ParseJson(Json, Parsed, &State))) return false;
	bool bPlainBound = false, bRichBound = false;
	TFunction<void(const FUMGWidgetNode&)> Visit = [&](const FUMGWidgetNode& Node)
	{
		bPlainBound |= Node.Type == TEXT("TextBlock") && Node.Text.FontFamily.Get(FString()) == Fields[3];
		if (Node.Text.RichTextStyleRows.IsSet())
			for (const auto& Row : Node.Text.RichTextStyleRows.GetValue()) bRichBound |= Row.Value.FontFamily == Fields[3];
		for (const auto& Child : Node.Children) Visit(Child);
	};
	Visit(Parsed.RootWidget);
	TestTrue(TEXT("Plain text binds the saved composite"), bPlainBound);
	TestTrue(TEXT("Rich text binds the saved composite"), bRichBound);
	return true;
}
#endif
