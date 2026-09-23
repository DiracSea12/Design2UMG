// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "DesignLocalFontImport.h"
#include "DesignImportPipeline.h"
#include "DesignSchema.h"
#include "Design2UMGSettings.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "FontResolver.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
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
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

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
	const FString Source = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"))->GetBaseDir() / TEXT("Fonts/Biryani-Bold.ttf");
	if (!TestTrue(TEXT("Read font data"), DesignLocalFontImport::ReadFace(Source, Face, Error))) return false;
	TMap<FString, UFont*> Imported;
	ON_SCOPE_EXIT
	{
		for (const auto& Pair : Imported)
		{
			UFont* Font = Pair.Value;
			const FString Filename = FPackageName::LongPackageNameToFilename(Font->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
			FAssetRegistryModule::AssetDeleted(Font);
			IFileManager::Get().Delete(*Filename);
			Font->ClearFlags(RF_Public | RF_Standalone);
			Font->MarkAsGarbage();
		}
		FFontResolver::ClearCache();
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
	const FString Replacement = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"))->GetBaseDir() / TEXT("Fonts/Biryani-Bold.ttf");
	if (!TestEqual(TEXT("Replace font at same path"), IFileManager::Get().Copy(*AddedPath, *Replacement), COPY_OK)) return false;
	MetadataReads = 0;
	DesignLocalFontImport::FSourceIndex ReplacedImport;
	TestTrue(TEXT("Changed file refreshes metadata"), ReplacedImport.GetFaces().ContainsByPredicate(
		[&](const DesignLocalFontImport::FFace& Face) { return FPaths::IsSamePath(Face.Path, AddedPath) && Face.Family == TEXT("Biryani"); }));
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
	const FString Source = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"))->GetBaseDir() / TEXT("Fonts/Biryani-Bold.ttf");
	const FString Renamed = FPaths::ProjectSavedDir() / TEXT("Design2UMG/FontTests/NotTheFamily.ttf");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Renamed), true);
	if (!TestEqual(TEXT("Copy source to misleading filename"), IFileManager::Get().Copy(*Renamed, *Source), COPY_OK)) return false;
	DesignLocalFontImport::FFace Face;
	FString Error;
	if (!TestTrue(TEXT("Read source metadata"), DesignLocalFontImport::ReadFace(Renamed, Face, Error))) return false;
	TestEqual(TEXT("Family is internal, not filename"), Face.Family, FString(TEXT("Biryani")));
	TestEqual(TEXT("Weight comes from font metadata"), Face.Weight, 700);
	DesignLocalFontImport::FFace Inconsistent;
	TestFalse(TEXT("Contradictory static weight metadata is not imported as Regular"),
		DesignLocalFontImport::ReadFace(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Black.ttf"), Inconsistent, Error));
	TestFalse(TEXT("Condensed face is not silently substituted for normal-width Bold"),
		DesignLocalFontImport::ReadFace(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-BoldCondensed.ttf"), Inconsistent, Error));
	const FString PackageName = TEXT("/Game/Design2UMG/Automation/LocalFontTest/IdentityFixture");
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
	FAssetRegistryModule::AssetDeleted(Font);
	IFileManager::Get().Delete(*FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()));
	Font->ClearFlags(RF_Public | RF_Standalone);
	Font->MarkAsGarbage();
	FFontResolver::ClearCache();
	IFileManager::Get().Delete(*Renamed);
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
	ON_SCOPE_EXIT
	{
		FAssetRegistryModule::AssetDeleted(Font);
		IFileManager::Get().Delete(*Filename);
		Font->ClearFlags(RF_Public | RF_Standalone);
		Font->MarkAsGarbage();
		FFontResolver::ClearCache();
	};
	TArray<uint8> Before;
	if (!TestTrue(TEXT("Read saved fixture"), FFileHelper::LoadFileToArray(Before, *Filename))) return false;
	FFontResolver::RegisterFontAssetPath(TEXT("Biryani"), Font->GetPathName());
	FDesignDocument Document;
	auto Node = MakeShared<FDesignNode>();
	Node->Text.Content = TEXT("Local font");
	Node->Text.FontFamily = TEXT("Biryani");
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignLocalFontDefaultPipelineTest,
	"Design2UMG.Fonts.LocalSource.DefaultPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignLocalFontDefaultPipelineTest::RunTest(const FString&)
{
	DesignLocalFontImport::FFace Face;
	FString Error;
	const FString Source = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"))->GetBaseDir() / TEXT("Fonts/Biryani-Bold.ttf");
	if (!TestTrue(TEXT("Read test face"), DesignLocalFontImport::ReadFace(Source, Face, Error))) return false;
	const FString PackageName = TEXT("/Game/Design2UMG/Fonts/PipelineFixture_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/PipelineFixture");
	DesignLocalFontImport::FFace Seed;
	if (!TestTrue(TEXT("Read distinct seed face"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), Seed, Error))) return false;
	if (!TestTrue(TEXT("Seed fixture with a different typeface"), DesignLocalFontImport::ImportFace(Seed, PackageName, Error, TEXT("Regular")))) return false;
	UFont* Font = LoadObject<UFont>(nullptr, *(PackageName + TEXT(".PipelineFixture")));
	if (!Font) return false;
	ON_SCOPE_EXIT
	{
		FAssetRegistryModule::AssetDeleted(Font);
		IFileManager::Get().Delete(*FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()));
		Font->ClearFlags(RF_Public | RF_Standalone);
		Font->MarkAsGarbage();
		FFontResolver::ClearCache();
	};
	TestTrue(TEXT("Required weight starts missing"), FFontResolver::FindExactTypefaceForAssetPath(Font->GetPathName(), TEXT("700")).IsEmpty());
	const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo HotInfo(Font, 24, TEXT("Bold"));
	const FVector2D Before = Measure->Measure(TEXT("MMMMiiii"), HotInfo);
	FFontResolver::RegisterFontAssetPath(TEXT("Biryani"), Font->GetPathName());
	FDesignDocument Document;
	auto Node = MakeShared<FDesignNode>();
	Node->Text.Content = TEXT("Local font");
	Node->Text.FontFamily = TEXT("Biryani");
	Node->Text.FontWeight = TEXT("700");
	Document.Children.Add(Node);
	const FImportAnalysisResult Result = DesignImportPipeline::AnalyzeImport(Document, FImportAnalysisInput{false});
	TestTrue(TEXT("No preparation failure"), Result.ErrorMessage.IsEmpty());
	const FString Path = FFontResolver::FindFontAssetPath(TEXT("Biryani"));
	TestFalse(TEXT("Default analysis prepares a project font"), Path.IsEmpty());
	TestEqual(TEXT("Default analysis supplies the requested weight"),
		FFontResolver::FindExactTypefaceForAssetPath(Path, TEXT("700")), FString(TEXT("Bold")));
	TestEqual(TEXT("Existing family asset reused"), Path, Font->GetPathName());
	UFont* ColdFont = NewObject<UFont>();
	ColdFont->FontCacheType = EFontCacheType::Runtime;
	ColdFont->GetMutableInternalCompositeFont().DefaultTypeface.Fonts =
		Font->GetCompositeFont()->DefaultTypeface.Fonts;
	const FVector2D Expected = Measure->Measure(TEXT("MMMMiiii"), FSlateFontInfo(ColdFont, 24, TEXT("Bold")));
	const FVector2D After = Measure->Measure(TEXT("MMMMiiii"), HotInfo);
	TestFalse(TEXT("Fixture distinguishes old and new faces"), Before.Equals(Expected, 0.01));
	TestTrue(TEXT("An already used Slate font immediately sees the imported weight"), After.Equals(Expected, 0.01));
	const int32 Count = Font->GetCompositeFont()->DefaultTypeface.Fonts.Num();
	DesignImportPipeline::AnalyzeImport(Document, FImportAnalysisInput{false});
	TestEqual(TEXT("Repeat analysis does not append faces"), Font->GetCompositeFont()->DefaultTypeface.Fonts.Num(), Count);
	auto* Settings = GetMutableDefault<UDesign2UMGSettings>();
	const auto SavedMappings = Settings->ExplicitFontMappings;
	ON_SCOPE_EXIT { Settings->ExplicitFontMappings = SavedMappings; FFontResolver::ClearCache(); };
	Settings->ExplicitFontMappings.Add(TEXT("Biryani"), Font);
	Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.RemoveAll([](const FTypefaceEntry& Entry) { return Entry.Name == TEXT("Bold"); });
	FFontResolver::ClearCache();
	DesignImportPipeline::AnalyzeImport(Document, FImportAnalysisInput{false});
	TestEqual(TEXT("Explicit mapping prevents automatic modification"), Font->GetCompositeFont()->DefaultTypeface.Fonts.Num(), 1);
	Node->Text.FontFamily = TEXT("D2U Missing Local Font Fixture");
	const FImportAnalysisResult Missing = DesignImportPipeline::AnalyzeImport(Document, FImportAnalysisInput{false});
	TestTrue(TEXT("Missing face is recorded in generated-data diagnostics"),
		Missing.Data.Warnings.ContainsByPredicate([](const FString& Warning) { return Warning.Contains(TEXT("D2U Missing Local Font Fixture")); }));
	return true;
}
#endif
