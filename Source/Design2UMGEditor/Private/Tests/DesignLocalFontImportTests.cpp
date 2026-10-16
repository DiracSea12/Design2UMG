// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Design2UMGFontAccess.h"
#include "DesignImportPlanning.h"
#include "DesignLocalFontImport.h"
#include "DesignImportPipeline.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "Design2UMGSettings.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "FontResolver.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Tests/Design2UMGFileFailureFixture.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
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
		Face.Family = Prefix + Suffix;
		const FString Package = FFontResolver::MakeImportedFamilyPackageName(Face.Family);
		if (!TestTrue(TEXT("Import separate family"), DesignLocalFontImport::ImportFace(Face, Package, Error))) return false;
		UFont* Font = LoadObject<UFont>(nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
		if (!TestNotNull(TEXT("Imported family asset exists"), Font)) return false;
		Imported.Add(Face.Family, Font);
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
	TestFalse(TEXT("Contradictory static weight metadata is not imported as Regular"),
		DesignLocalFontImport::ReadFace(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Black.ttf"), Inconsistent, Error));
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
	TestTrue(TEXT("User font can still be referenced"), DesignLocalFontImport::Prepare(Document, Warnings, Error));
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
	TestTrue(TEXT("Unused-font save failure does not affect preparation"), DesignLocalFontImport::Prepare(Document, Warnings, Error));
	TestFalse(TEXT("Unused font save is not attempted"), bSaveAttempted);
	TestTrue(TEXT("Unused font policy stays unchanged"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
	FFileHelper::LoadFileToArray(After, *Filename);
	TestTrue(TEXT("Unused font file stays intact"), Before == After);
	Failure.AllowSaves();
	TestTrue(TEXT("Repeated preparation ignores unused generated fonts"), DesignLocalFontImport::Prepare(Document, Warnings, Error));
	TestTrue(TEXT("Unused font policy stays unchanged"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
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
		if (!SaveFontPolicyFixture(*this, *Font)) return false;
		const TArray<uint8> RegularBytes = RegularFace->FontFaceData->GetData();
		const TArray<uint8> BoldBytes = BoldFace->FontFaceData->GetData();
		const FString Filename = FPaths::ConvertRelativePathToFull(
			FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension()));
		TArray<uint8> Before, After;
		if (!TestTrue(TEXT("Read original persisted policies"), FFileHelper::LoadFileToArray(Before, *Filename))) return false;
		Font->GetOutermost()->SetDirtyFlag(bWasDirty);
		FDesignDocument Document;
		auto Node = MakeShared<FDesignNode>();
		Node->Text.Content = TEXT("Existing font");
		Node->Text.FontFamily = Regular.Family;
		Node->Text.FontWeight = TEXT("400");
		Document.Children.Add(Node);
		TArray<FString> Warnings;
		auto Persist = [&]()
		{
			return bPrepare ? DesignLocalFontImport::Prepare(Document, Warnings, Error)
				: DesignLocalFontImport::ImportFace(Regular, Package, Error);
		};
		bool bSaveRejected = false;
		Design2UMGEditorTests::FScopedPackageSaveFailure SaveFailure([&](const FString& Path)
		{
			bSaveRejected = Path.Equals(Filename, ESearchCase::IgnoreCase);
			return bSaveRejected;
		});
		TestFalse(TEXT("A failed policy save fails the requested operation"), Persist());
		TestTrue(TEXT("Existing-font save was attempted"), bSaveRejected);
		TestTrue(TEXT("Policy failure names the font save"), Error.StartsWith(TEXT("Failed to save local font:")));
		TestTrue(TEXT("Regular policy rolls back"), RegularFace->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
		TestTrue(TEXT("Bold policy rolls back"), BoldFace->LoadingPolicy == EFontLoadingPolicy::Stream);
		TestEqual(TEXT("Dirty state rolls back"), Font->GetOutermost()->IsDirty(), bWasDirty);
		TestTrue(TEXT("Read font after rejected save"), FFileHelper::LoadFileToArray(After, *Filename));
		TestTrue(TEXT("Failed save leaves disk bytes identical"), Before == After);
		SaveFailure.AllowSaves();
		if (!TestTrue(TEXT("Retry persists existing embedded faces"), Persist())) return false;
		TestTrue(TEXT("Regular data is stored with the owning font"), RegularFace->LoadingPolicy == EFontLoadingPolicy::Inline);
		TestTrue(TEXT("Other embedded styles use the same rule"), BoldFace->LoadingPolicy == EFontLoadingPolicy::Inline);
		TestTrue(TEXT("Separate font-face loading policy is untouched"), SeparateFace->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
		TestTrue(TEXT("Regular object identity is unchanged"), Entries[0].Font.GetFontFaceAsset() == RegularFace);
		TestTrue(TEXT("Bold object identity is unchanged"), Entries[1].Font.GetFontFaceAsset() == BoldFace);
		TestTrue(TEXT("Regular bytes are unchanged"), RegularFace->FontFaceData->GetData() == RegularBytes);
		TestTrue(TEXT("Bold bytes are unchanged"), BoldFace->FontFaceData->GetData() == BoldBytes);
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
	TArray<FString> Warnings;
	bool bSaveAttempted = false;
	Design2UMGEditorTests::FScopedPackageSaveFailure NoSave([&](const FString&) { bSaveAttempted = true; return true; });
	TestTrue(TEXT("Explicit mapping is usable without a save"), DesignLocalFontImport::Prepare(Document, Warnings, Error));
	TestFalse(TEXT("Explicitly mapped font is not saved"), bSaveAttempted);
	TestTrue(TEXT("Explicitly mapped policy is unchanged"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignMappedGeneratedFontPolicyTest,
	"Design2UMG.Fonts.LocalSource.MappedGeneratedPolicyMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDesignMappedGeneratedFontPolicyTest::RunTest(const FString&)
{
	DesignLocalFontImport::FFace Face;
	FString Error;
	if (!TestTrue(TEXT("Read source"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), Face, Error))) return false;
	Face.Family = TEXT("MappedGenerated_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Package = FFontResolver::MakeImportedFamilyPackageName(Face.Family);
	if (!TestTrue(TEXT("Create generated font"), DesignLocalFontImport::ImportFace(Face, Package, Error))) return false;
	ON_SCOPE_EXIT { DeleteFontFixture(*this, Package, false); };
	UFont* Font = LoadObject<UFont>(nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
	if (!TestNotNull(TEXT("Generated font exists"), Font)) return false;
	UFontFace* Embedded = const_cast<UFontFace*>(Cast<UFontFace>(Font->GetCompositeFont()->DefaultTypeface.Fonts[0].Font.GetFontFaceAsset()));
	if (!TestNotNull(TEXT("Embedded face exists"), Embedded)) return false;
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
		Embedded->LoadingPolicy = EFontLoadingPolicy::LazyLoad;
		if (!SaveFontPolicyFixture(*this, *Font)) return false;
		FFontResolver::ClearCache();
		FDesignDocument Document;
		TArray<FString> Warnings;
		TestTrue(TEXT("Unused mapped generated font does not block preparation"), DesignLocalFontImport::Prepare(Document, Warnings, Error));
		TestTrue(TEXT("Unused mapped generated font stays unchanged"), Embedded->LoadingPolicy == EFontLoadingPolicy::LazyLoad);
		auto Node = MakeShared<FDesignNode>();
		Node->Text.Content = TEXT("Mapped font");
		Node->Text.FontFamily = Face.Family;
		Document.Children.Add(Node);
		TestTrue(TEXT("Requested mapped generated font prepares"), DesignLocalFontImport::Prepare(Document, Warnings, Error));
		TestTrue(TEXT("Requested mapped generated font becomes inline"), Embedded->LoadingPolicy == EFontLoadingPolicy::Inline);
	}
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
	ExternalDocument.MutableSourceDocument().ImportedReusableNodes.Add(Definition);
	auto Instance = MakeShared<FDesignNode>();
	Instance->Type = EDesignNodeType::Ref;
	Instance->Ref = Definition->Id;
	ExternalDocument.Children.Add(Instance);
	Design2UMGFontAccess::GetMutableCompositeFont(*Font).DefaultTypeface.Fonts.RemoveAll([](const FTypefaceEntry& Entry) { return Entry.Name == TEXT("Bold"); });
	FFontResolver::ClearCache();
	FFontResolver::RegisterFontAssetPath(Face.Family, Font->GetPathName());
	TArray<FString> ExternalWarnings;
	TestTrue(TEXT("External component fonts prepare through the existing importer"), DesignLocalFontImport::Prepare(ExternalDocument, ExternalWarnings, Error));
	TestEqual(TEXT("External-only font requests import the missing weight"), FFontResolver::FindExactTypefaceForAssetPath(Font->GetPathName(), TEXT("700")), FString(TEXT("Bold")));
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
	if (!TestNotNull(TEXT("Persistent embedded face exists"), Embedded)) return false;
	Embedded->LoadingPolicy = EFontLoadingPolicy::LazyLoad;
	if (!SaveFontPolicyFixture(*this, *Font)) return false;
	if (!TestTrue(TEXT("Persist an existing-font policy through normal reimport"), DesignLocalFontImport::ImportFace(Face, Package, Error))) return false;
	TestTrue(TEXT("Existing face policy is Inline before restart"), Embedded->LoadingPolicy == EFontLoadingPolicy::Inline);
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
	Document.Children.Add(Node);
	TArray<FString> Diagnostics;
	if (!SaveFontPolicyFixture(*this, *Font)
		|| !TestTrue(TEXT("Prepare mapped generated font for restart"), DesignLocalFontImport::Prepare(Document, Diagnostics, Error))
		|| !TestTrue(TEXT("Mapped generated face is Inline before restart"), Embedded->LoadingPolicy == EFontLoadingPolicy::Inline))
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
			TestTrue(TEXT("Existing-font policy survives restart"), Embedded->LoadingPolicy == EFontLoadingPolicy::Inline);
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
#endif
