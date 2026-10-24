// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ExternalServices/PencilExtensionLocator.h"
#include "ExternalServices/PencilExtensionResources.h"
#include "FontResolver.h"
#include "DesignLocalFontImport.h"
#include "Design2UMGConverter.h"
#include "Design2UMGSettings.h"
#include "DesignSchema.h"
#include "Engine/Font.h"
#include "Misc/ScopeExit.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "UObject/UObjectGlobals.h"
#include "PenBundledLibraries.h"
#include "PenParser.h"
#include "PencilFontTool.h"
#include "PencilFontToolText.h"
#include "HAL/PlatformMisc.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
#include "HttpModule.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace
{
	FString LoadIconIndexFixtureJson(FAutomationTestBase& Test)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		if (!Plugin.IsValid())
		{
			Test.AddError(TEXT("Design2UMG plugin not found"));
			return FString();
		}
		const FString FixturePath = FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures/icon_index_sample.json"));
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *FixturePath))
		{
			Test.AddError(FString::Printf(TEXT("Failed to load fixture: %s"), *FixturePath));
			return FString();
		}
		return Json;
	}

	FString LoadSampleLibPenFixtureJson(FAutomationTestBase& Test)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		if (!Plugin.IsValid())
		{
			Test.AddError(TEXT("Design2UMG plugin not found"));
			return FString();
		}
		const FString FixturePath = FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures/sample.lib.pen"));
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *FixturePath))
		{
			Test.AddError(FString::Printf(TEXT("Failed to load fixture: %s"), *FixturePath));
			return FString();
		}
		return Json;
	}

	bool IsHighSurrogate(const TCHAR Char)
	{
		return Char >= 0xD800 && Char <= 0xDBFF;
	}

	bool IsLowSurrogate(const TCHAR Char)
	{
		return Char >= 0xDC00 && Char <= 0xDFFF;
	}
}

// ---------------------------------------------------------------------------
// icon-index.json parser: families, codepoints (including surrogate pairs)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPencilExtensionIconIndexParserTest,
	"Design2UMG.Unit.PencilExtensionResources.IconIndexParser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilExtensionIconIndexParserTest::RunTest(const FString&)
{
	const FString Json = LoadIconIndexFixtureJson(*this);
	if (Json.IsEmpty())
	{
		return false;
	}

	TSharedPtr<FJsonObject> Fixture;
	if (!TestTrue(TEXT("Index fixture is structured JSON"),
	              FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Fixture)))
		return false;
	FString SetsJson;
	if (!TestTrue(
	        TEXT("Fixture sets serialize"),
	        FJsonSerializer::Serialize(Fixture->GetArrayField(TEXT("sets")),
	                                   TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SetsJson))))
		return false;
	const FString Bundle = TEXT("const other=JSON.parse('[]');const arbitraryName=JSON.parse('") + SetsJson +
	                       TEXT("');throw new Error('this code is not executed');");
	FString ExtractedJson;
	FString Error;
	if (!TestTrue(TEXT("Static index is discovered independently of bundle variable names"),
	              PencilExtensionResources::ExtractIconIndexJson(Bundle, ExtractedJson, Error)))
		return false;
	TMap<FString, TMap<FString, FString>> GlyphsByFamily;
	TMap<FString, TMap<FString, FString>> NamesByFamily;
	TestTrue(TEXT("Extracted index uses the existing parser"),
	         PencilExtensionResources::ParseIconIndexJson(ExtractedJson, GlyphsByFamily, NamesByFamily, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	// Every family of every set gets its own table; Material Symbols shares one codepoint
	// table across its three variants.
	TestEqual(TEXT("Five families parsed"), GlyphsByFamily.Num(), 5);
	TestTrue(TEXT("lucide family present"), GlyphsByFamily.Contains(TEXT("lucide")));
	TestTrue(TEXT("phosphor family present"), GlyphsByFamily.Contains(TEXT("phosphor")));
	TestTrue(TEXT("Material Symbols Rounded family present"), GlyphsByFamily.Contains(TEXT("Material Symbols Rounded")));

	// BMP codepoints become single-unit glyph strings.
	const TMap<FString, FString>* LucideGlyphs = GlyphsByFamily.Find(TEXT("lucide"));
	TestTrue(TEXT("lucide glyph table found"), LucideGlyphs != nullptr);
	if (LucideGlyphs)
	{
		const FString* ActivityGlyph = LucideGlyphs->Find(TEXT("activity"));
		TestTrue(TEXT("lucide activity glyph found"), ActivityGlyph != nullptr);
		if (ActivityGlyph)
		{
			TestEqual(TEXT("lucide activity glyph is single unit"), ActivityGlyph->Len(), 1);
			TestEqual(TEXT("lucide activity glyph codepoint"), (*ActivityGlyph)[0], static_cast<TCHAR>(57400));
		}
	}

	// Icon names normalize to the resolver key space (underscore -> dash).
	const TMap<FString, FString>* RoundedGlyphs = GlyphsByFamily.Find(TEXT("Material Symbols Rounded"));
	TestTrue(TEXT("Material Symbols Rounded glyph table found"), RoundedGlyphs != nullptr);
	if (RoundedGlyphs)
	{
		TestTrue(TEXT("query_stats is keyed as query-stats"), RoundedGlyphs->Contains(TEXT("query-stats")));
		TestFalse(TEXT("raw underscore key is not stored"), RoundedGlyphs->Contains(TEXT("query_stats")));

		// Codepoints above 0xFFFF encode as UTF-16 surrogate pairs.
		const FString* MoodHeartGlyph = RoundedGlyphs->Find(TEXT("mood-heart"));
		TestTrue(TEXT("mood_heart glyph found"), MoodHeartGlyph != nullptr);
		if (MoodHeartGlyph)
		{
			TestEqual(TEXT("mood_heart glyph is a surrogate pair"), MoodHeartGlyph->Len(), 2);
			if (MoodHeartGlyph->Len() == 2)
			{
				TestTrue(TEXT("mood_heart starts with high surrogate"), IsHighSurrogate((*MoodHeartGlyph)[0]));
				TestTrue(TEXT("mood_heart ends with low surrogate"), IsLowSurrogate((*MoodHeartGlyph)[1]));
			}
		}

		// Shared codepoint table: the same icon resolves in every Material Symbols variant.
		const TMap<FString, FString>* SharpGlyphs = GlyphsByFamily.Find(TEXT("Material Symbols Sharp"));
		TestTrue(TEXT("Material Symbols Sharp glyph table found"), SharpGlyphs != nullptr);
		if (SharpGlyphs)
		{
			TestEqual(TEXT("Material Symbols variants share glyphs"),
				SharpGlyphs->FindRef(TEXT("search")), RoundedGlyphs->FindRef(TEXT("search")));
		}
	}

	// Reverse map keeps the raw icon name from the index.
	const TMap<FString, FString>* RoundedNames = NamesByFamily.Find(TEXT("Material Symbols Rounded"));
	TestTrue(TEXT("Material Symbols Rounded name table found"), RoundedNames != nullptr);
	if (RoundedNames)
	{
		TestEqual(TEXT("Reverse map keeps raw underscore name"),
			RoundedNames->FindRef(RoundedGlyphs ? RoundedGlyphs->FindRef(TEXT("query-stats")) : FString()),
			FString(TEXT("query_stats")));
	}

	// Structural validation: empty/absent sets and malformed payloads fail with an error.
	TMap<FString, TMap<FString, FString>> ThrowawayGlyphs;
	TMap<FString, TMap<FString, FString>> ThrowawayNames;
	TestFalse(TEXT("Empty sets rejected"),
		PencilExtensionResources::ParseIconIndexJson(TEXT("{\"sets\":[]}"), ThrowawayGlyphs, ThrowawayNames, Error));
	TestTrue(TEXT("Empty sets error reported"), !Error.IsEmpty());
	TestFalse(TEXT("Missing sets rejected"),
		PencilExtensionResources::ParseIconIndexJson(TEXT("{\"icons\":[]}"), ThrowawayGlyphs, ThrowawayNames, Error));
	TestFalse(TEXT("Invalid JSON rejected"),
		PencilExtensionResources::ParseIconIndexJson(TEXT("not json"), ThrowawayGlyphs, ThrowawayNames, Error));
	TestFalse(TEXT("Unrelated JSON data is not an icon registry"),
		PencilExtensionResources::ExtractIconIndexJson(TEXT("JSON.parse('[]')"), ExtractedJson, Error));
	TestTrue(TEXT("Missing bundle index has a useful error"), !Error.IsEmpty());

	TArray<PencilExtensionResources::FFontStyle> Styles;
	const TSet<FString> Families{TEXT("test-icons"), TEXT("variable-icons")};
	const FString FontBundle = TEXT(R"({name:"unrelated",styles:[{url:"https://example.test/other.ttf",weight:400}]},
{name:"test-icons",styles:[{url:"https://example.test/icons.ttf",weight:400}]},
{name:"variable-icons",styles:[{url:"https://example.test/variable.ttf",weight:400,axes:[{tag:"wght",start:100,end:700}]}]},
{name:"normal-text",styles:[{url:"https://example.test/roman.ttf",weight:400,italic:!1},
{url:"https://example.test/italic.ttf",weight:400,italic:!0,axes:[{tag:"wght",start:100,end:900}]}]},
{name:"",styles:[{url:""}]})");
	TestTrue(TEXT("Read only registered families' static font descriptions as data"),
		PencilExtensionResources::ExtractFontStyles(FontBundle, Families, Styles, Error));
	if (TestEqual(TEXT("Only registered descriptions are retained"), Styles.Num(), 2))
	{
		TestEqual(TEXT("Font URL belongs to its declared family"), Styles[0].Family, FString(TEXT("test-icons")));
		TestEqual(TEXT("Static style keeps its actual weight"), Styles[0].Weight, 400);
		TestEqual(TEXT("Static style keeps its URL"), Styles[0].Url, FString(TEXT("https://example.test/icons.ttf")));
		TestFalse(TEXT("Static style has no variable axes"), Styles[0].bVariable);
		TestTrue(TEXT("Variable TTF stays distinct from static TTF"), Styles[1].bVariable);
		TestEqual(TEXT("Static icon style keeps the nearest actual face weight"), Styles[0].ResolveWeight(200), 400);
		TestEqual(TEXT("Variable icon style keeps a non-hundred source weight"), Styles[1].ResolveWeight(350), 350);
		TestEqual(TEXT("Variable icon style clamps to its declared axis range"), Styles[1].ResolveWeight(900), 700);
	}
	TestTrue(TEXT("Read current text font descriptions without evaluating their boolean literals"),
		PencilExtensionResources::ExtractFontStyles(FontBundle, TSet<FString>{TEXT("normal-text")}, Styles, Error));
	if (TestEqual(TEXT("The actual roman and italic source descriptions remain distinct"), Styles.Num(), 2))
	{
		TestFalse(TEXT("False italic literal selects the roman source"), Styles[0].bItalic);
		TestTrue(TEXT("True italic literal selects the italic variable source"), Styles[1].bItalic && Styles[1].bVariable);
	}
	TestTrue(TEXT("Unfiltered source descriptions skip the editor's empty-family placeholder"),
		PencilExtensionResources::ExtractFontStyles(FontBundle, TSet<FString>(), Styles, Error));
	TestEqual(TEXT("Only the five actual source styles are indexed"), Styles.Num(), 5);

	return true;
}

// Requires the installed Pencil library metadata; does not alter its real font assets.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilStaticIconStyleReuseTest,
	"Design2UMG.Integration.PencilResources.StaticIconStyleReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPencilStaticIconStyleReuseTest::RunTest(const FString&)
{
	PencilExtensionResources::FFontStyle Style;
	if (!TestTrue(TEXT("Installed Pencil library describes the static icon style"),
		PencilExtensionResources::FindFontStyle(TEXT("feather"), 200, false, Style))) return false;
	if (!TestFalse(TEXT("The selected library style is static"), Style.bVariable)) return false;
	if (!TestEqual(TEXT("The nearest available static weight is 400, not requested 200"), Style.Weight, 400)) return false;

	UDesign2UMGSettings* Settings = GetMutableDefault<UDesign2UMGSettings>();
	const auto PreviousMappings = Settings->ExplicitFontMappings;
	const auto PreviousIcons = Settings->IconFontAssetMap;
	const auto PreviousDefault = Settings->DefaultFontAsset;
	const FString PreviousDefaultIcon = Settings->DefaultIconFontAssetPath;
	const FString Package = TEXT("/Game/Design2UMG/Fonts/D2U_IconStyleReuse_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString ObjectPath = Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
	const FString Filename = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
	ON_SCOPE_EXIT
	{
		Settings->ExplicitFontMappings = PreviousMappings;
		Settings->IconFontAssetMap = PreviousIcons;
		Settings->DefaultFontAsset = PreviousDefault;
		Settings->DefaultIconFontAssetPath = PreviousDefaultIcon;
		FFontResolver::ClearCache();
		if (UFont* Font = FindObject<UFont>(nullptr, *ObjectPath))
			TestTrue(TEXT("Delete only this test's font asset"), ObjectTools::ForceDeleteObjects({Font}, false) > 0);
		TestFalse(TEXT("Temporary font package was removed"), IFileManager::Get().FileExists(*Filename));
	};
	Settings->ExplicitFontMappings.Reset();
	Settings->IconFontAssetMap.Reset();
	Settings->DefaultFontAsset.Reset();
	Settings->DefaultIconFontAssetPath.Reset();

	DesignLocalFontImport::FFace Face;
	FString Error;
	if (!TestTrue(TEXT("Read the static 400 face"), DesignLocalFontImport::ReadFace(
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), Face, Error))) return false;
	if (!TestEqual(TEXT("The fixture has the declared static weight"), Face.Weight, Style.Weight)) return false;
	if (!TestTrue(TEXT("Create an isolated automatic font asset"),
		DesignLocalFontImport::ImportFace(Face, Package, Error, FString(), Style.Family))) return false;
	UFont* Font = LoadObject<UFont>(nullptr, *ObjectPath);
	if (!TestNotNull(TEXT("The isolated font exists"), Font)) return false;
	const auto* OriginalFace = Font->GetCompositeFont()->DefaultTypeface.Fonts[0].Font.GetFontFaceAsset();
	TArray<uint8> Before, After;
	if (!TestTrue(TEXT("Read the prepared temporary font package"), FFileHelper::LoadFileToArray(Before, *Filename))) return false;
	FDesignDocument Document;
	auto Icon = MakeShared<FDesignNode>();
	Icon->Type = EDesignNodeType::IconFont;
	Icon->Text.FontFamily.Reset();
	Icon->Text.IconFontFamily = Style.Family;
	Icon->Text.IconWeight = 200;
	Document.Children.Add(Icon);
	TArray<FString> Warnings;
	TestTrue(TEXT("Requested 200 reuses the available static 400 style"),
		DesignLocalFontImport::Prepare(Document, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()), Warnings, Error));
	TestTrue(TEXT("Reimport preserves the exact embedded face instead of downloading another"),
		Font->GetCompositeFont()->DefaultTypeface.Fonts[0].Font.GetFontFaceAsset() == OriginalFace);
	TestTrue(TEXT("The selected existing style produces no missing-font warning"), Warnings.IsEmpty());
	if (!TestTrue(TEXT("Read the font package after reuse"), FFileHelper::LoadFileToArray(After, *Filename))) return false;
	TestTrue(TEXT("Existing static font package stays byte-identical"), Before == After);
	return true;
}

// ---------------------------------------------------------------------------
// Registration round trip: parser output -> FFontResolver -> query hits
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPencilExtensionIconGlyphLookupSerializationCycleTest,
	"Design2UMG.Unit.PencilExtensionResources.IconGlyphLookupSerializationCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilExtensionIconGlyphLookupSerializationCycleTest::RunTest(const FString&)
{
	const FString Json = LoadIconIndexFixtureJson(*this);
	if (Json.IsEmpty())
	{
		return false;
	}

	TMap<FString, TMap<FString, FString>> GlyphsByFamily;
	TMap<FString, TMap<FString, FString>> NamesByFamily;
	FString Error;
	if (!PencilExtensionResources::ParseIconIndexJson(Json, GlyphsByFamily, NamesByFamily, Error))
	{
		AddError(FString::Printf(TEXT("Fixture parse failed: %s"), *Error));
		return false;
	}

	for (const TPair<FString, TMap<FString, FString>>& Pair : GlyphsByFamily)
	{
		const TMap<FString, FString>* Names = NamesByFamily.Find(Pair.Key);
		FFontResolver::RegisterKnownIconGlyphs(Pair.Key, Pair.Value, Names ? *Names : TMap<FString, FString>());
	}

	// Pen library values ("Material Symbols Rounded") hit their shared codepoint table.
	const FString QueryStatsGlyph = FFontResolver::FindKnownIconGlyph(TEXT("Material Symbols Rounded"), TEXT("query_stats"));
	TestEqual(TEXT("query_stats resolves via pen library name"), QueryStatsGlyph, GlyphsByFamily.FindRef(TEXT("Material Symbols Rounded")).FindRef(TEXT("query-stats")));

	// Family/icon queries are separator- and case-insensitive.
	TestEqual(TEXT("Mixed-case icon query hits"),
		FFontResolver::FindKnownIconGlyph(TEXT("lucide"), TEXT("Ellipsis")),
		GlyphsByFamily.FindRef(TEXT("lucide")).FindRef(TEXT("ellipsis")));
	TestEqual(TEXT("Spaced family query hits"),
		FFontResolver::FindKnownIconGlyph(TEXT("MaterialSymbolsRounded"), TEXT("query-stats")),
		QueryStatsGlyph);

	// Surrogate-pair glyphs flow through unchanged.
	TestEqual(TEXT("Surrogate-pair glyph resolves"),
		FFontResolver::FindKnownIconGlyph(TEXT("Material Symbols Sharp"), TEXT("mood_heart")),
		GlyphsByFamily.FindRef(TEXT("Material Symbols Sharp")).FindRef(TEXT("mood-heart")));

	// The static alias table keeps mapping design names onto existing glyphs.
	TestEqual(TEXT("Alias more-horizontal -> lucide ellipsis"),
		FFontResolver::FindKnownIconGlyph(TEXT("lucide"), TEXT("more-horizontal")),
		GlyphsByFamily.FindRef(TEXT("lucide")).FindRef(TEXT("ellipsis")));

	// Reverse lookup recovers the raw icon name for structure snapshots.
	TestEqual(TEXT("Reverse lookup recovers raw name"),
		FFontResolver::FindKnownIconNameByGlyph(TEXT("Material Symbols Rounded"), QueryStatsGlyph),
		FString(TEXT("query_stats")));

	// Unregistered combinations stay empty so the icon-name fallback is preserved.
	TestTrue(TEXT("Unknown family misses"),
		FFontResolver::FindKnownIconGlyph(TEXT("SomeOtherLibrary"), TEXT("query_stats")).IsEmpty());
	TestTrue(TEXT("Unknown icon misses"),
		FFontResolver::FindKnownIconGlyph(TEXT("lucide"), TEXT("does-not-exist")).IsEmpty());

	FFontResolver::ClearCache();
	return true;
}

// ---------------------------------------------------------------------------
// Extension root discovery: newest-first per search directory, directory priority
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPencilExtensionRootCandidatesTest,
	"Design2UMG.Unit.PencilExtensionResources.ExtensionRootCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilExtensionRootCandidatesTest::RunTest(const FString&)
{
	const FString ScratchDir = FPaths::Combine(
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()),
		TEXT("Automation/PencilExtensionLocatorTest"));
	IFileManager::Get().DeleteDirectory(*ScratchDir, false, true);

	const FString PrimaryDir = ScratchDir / TEXT("primary");
	const FString SecondaryDir = ScratchDir / TEXT("secondary");
	for (const FString& Version : { TEXT("0.6.9"), TEXT("0.6.68"), TEXT("0.6.100") })
	{
		IFileManager::Get().MakeDirectory(*(PrimaryDir / FString(TEXT("highagency.pencildev-")) + Version), true);
	}
	IFileManager::Get().MakeDirectory(*(SecondaryDir / TEXT("highagency.pencildev-0.99.0")), true);

	TArray<FString> SearchDirs = { PrimaryDir, SecondaryDir };
	TArray<PencilExtensionLocator::FExtensionRoot> Roots;
	PencilExtensionLocator::CollectExtensionRoots(SearchDirs, Roots);

	// Newest-first by numeric version (not lexicographic), earlier search directory wins overall.
	TestEqual(TEXT("Four roots discovered"), Roots.Num(), 4);
	if (Roots.Num() == 4)
	{
		TestEqual(TEXT("Newest version first"), Roots[0].Version, FString(TEXT("0.6.100")));
		TestEqual(TEXT("Second newest second"), Roots[1].Version, FString(TEXT("0.6.68")));
		TestEqual(TEXT("Numeric order beats lexicographic (0.6.9 last in dir)"), Roots[2].Version, FString(TEXT("0.6.9")));
		TestEqual(TEXT("Search directory priority preserved"), Roots[3].Version, FString(TEXT("0.99.0")));
		TestEqual(TEXT("Root directory path"),
			Roots[0].Directory, PrimaryDir / TEXT("highagency.pencildev-0.6.100"));
	}

	// Resource candidates are built from the extension root without touching a real install.
	TArray<FString> RelativeCandidates;
	PencilExtensionResources::GetIconIndexRelativeCandidates(RelativeCandidates);
	TestEqual(TEXT("Only the current icon registry source is advertised"), RelativeCandidates.Num(), 1);
	TestTrue(TEXT("Current editor bundle path advertised"),
		RelativeCandidates.Contains(TEXT("out/editor/assets/index.js")));
	if (Roots.Num() > 0 && RelativeCandidates.Num() > 0)
	{
		TestEqual(TEXT("Icon index candidate path construction"),
			Roots[0].Directory / RelativeCandidates[0],
			PrimaryDir / TEXT("highagency.pencildev-0.6.100") / TEXT("out/editor/assets/index.js"));
	}

	IFileManager::Get().DeleteDirectory(*ScratchDir, false, true);
	return true;
}

// ---------------------------------------------------------------------------
// Bundled library documents (*.lib.pen): variables extraction and registry round trip
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPencilExtensionLibPenVariablesParserTest,
	"Design2UMG.Unit.PencilExtensionResources.LibPenVariablesParser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilExtensionLibPenVariablesParserTest::RunTest(const FString&)
{
	const FString LibPenJson = LoadSampleLibPenFixtureJson(*this);
	if (LibPenJson.IsEmpty())
	{
		return false;
	}

	FDesignDocument Document;
	if (!TestTrue(TEXT("Fixture lib.pen parses through the production parser"), FPenParser::ParseDocument(LibPenJson, Document)))
	{
		return false;
	}
	TestTrue(TEXT("variables keys carried over"), Document.Variables.Contains(TEXT("--primary")));
	TestTrue(TEXT("themed entries carried over"), Document.Variables.Contains(TEXT("--muted-foreground")));
	TestFalse(TEXT("document-level fields are not variables"), Document.Variables.Contains(TEXT("version")));
	TestFalse(TEXT("children are not variables"), Document.Variables.Contains(TEXT("children")));

	// Exercise the loader's existing rejection of malformed library payloads.
	const FString ScratchDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Automation/PencilLibraryValidation");
	IFileManager::Get().DeleteDirectory(*ScratchDir, false, true);
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*ScratchDir, false, true); };
	if (!TestTrue(TEXT("Create library validation directory"), IFileManager::Get().MakeDirectory(*ScratchDir, true))) return false;
	const FString InvalidLibraryPath = ScratchDir / TEXT("invalid.lib.pen");
	if (!TestTrue(TEXT("Write invalid JSON"), FFileHelper::SaveStringToFile(TEXT("not json"), *InvalidLibraryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
	TestEqual(TEXT("Invalid JSON rejected by loader"), PencilExtensionResources::RegisterLibrariesFromDirectory(ScratchDir), 0);
	if (!TestTrue(TEXT("Write library missing variables"), FFileHelper::SaveStringToFile(TEXT(R"({"version":"2.8"})"), *InvalidLibraryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
	TestEqual(TEXT("Missing variables rejected by loader"), PencilExtensionResources::RegisterLibrariesFromDirectory(ScratchDir), 0);

	// Only the two supported package layouts are candidates; the official layout wins.
	TArray<FString> RelativeDirs;
	PencilExtensionResources::GetLibraryPenRelativeCandidates(RelativeDirs);
	if (TestEqual(TEXT("Exactly two library data layouts"), RelativeDirs.Num(), 2))
	{
		TestEqual(TEXT("Official library data layout has priority"), RelativeDirs[0], FString(TEXT("out/data")));
		TestEqual(TEXT("Original library data layout remains supported"),
			RelativeDirs[1], FString(TEXT("node_modules/@ha/pencil-editor/data")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPencilExtensionLibrariesRegistrySerializationCycleTest,
	"Design2UMG.Unit.PencilExtensionResources.LibrariesRegistrySerializationCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilExtensionLibrariesRegistrySerializationCycleTest::RunTest(const FString&)
{
	const FString LibPenJson = LoadSampleLibPenFixtureJson(*this);
	if (LibPenJson.IsEmpty())
	{
		return false;
	}

	const FString SourcePath = FPaths::ProjectSavedDir() / TEXT("Automation/registry-fixture.lib.pen");
	PenBundledLibraries::RegisterLibraryDocument(TEXT("registry-fixture"), LibPenJson, SourcePath);
	FString Registered;
	FString RegisteredPath;
	TestTrue(TEXT("Registered library resolves"),
		PenBundledLibraries::TryGetDocumentJson(TEXT("registry-fixture"), Registered, &RegisteredPath));
	TestEqual(TEXT("Complete registered payload matches"), Registered, LibPenJson);
	TestEqual(TEXT("Registered source path matches"), RegisteredPath, SourcePath);

	const FString ImportJson = TEXT(R"({"version":"2.8","imports":{"Y":"pencil:registry-fixture.lib.pen"},"children":[]})");
	FDesignDocument Document;
	if (!TestTrue(TEXT("Registered library imports through the parser"), FPenParser::ParseDocument(ImportJson, Document))) return false;
	if (TestTrue(TEXT("Imported variables use the document alias"), Document.Variables.Contains(TEXT("Y:--primary"))))
	{
		TestEqual(TEXT("Variable records the import alias"), Document.Variables[TEXT("Y:--primary")].SourceAlias, FString(TEXT("Y")));
		TestEqual(TEXT("Variable retains its default value"), Document.Variables[TEXT("Y:--primary")].Value, FString(TEXT("#171717")));
	}

	// Replacing the complete document also replaces its variables and source path.
	const FString Replacement = TEXT(R"({"version":"2.8","variables":{"replacement":{"type":"number","value":7}},"children":[]})");
	PenBundledLibraries::RegisterLibraryDocument(TEXT("registry-fixture"), Replacement);
	TestTrue(TEXT("Replaced library resolves"),
		PenBundledLibraries::TryGetDocumentJson(TEXT("registry-fixture"), Registered, &RegisteredPath));
	TestEqual(TEXT("Replaced document wins"), Registered, Replacement);
	TestTrue(TEXT("Replacement clears the old source path"), RegisteredPath.IsEmpty());
	if (!TestTrue(TEXT("Replacement imports through the parser"), FPenParser::ParseDocument(ImportJson, Document))) return false;
	TestEqual(TEXT("Only replacement variables remain"), Document.Variables.Num(), 1);
	TestTrue(TEXT("Replacement variable is imported"), Document.Variables.Contains(TEXT("Y:replacement")));
	TestFalse(TEXT("Old variables do not survive document replacement"), Document.Variables.Contains(TEXT("Y:--primary")));

	FString Unregistered;
	TestFalse(TEXT("Unregistered library misses"),
		PenBundledLibraries::TryGetDocumentJson(TEXT("no-such-library"), Unregistered));
	TestFalse(TEXT("Empty name has no registered document"),
		PenBundledLibraries::TryGetDocumentJson(FString(), Unregistered));
	return true;
}

// ---------------------------------------------------------------------------
// Bundled library documents (*.lib.pen): directory scan derives PenParser-compatible keys
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPencilExtensionLibrariesDirectoryScanTest,
	"Design2UMG.Unit.PencilExtensionResources.LibrariesDirectoryScan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilExtensionLibrariesDirectoryScanTest::RunTest(const FString&)
{
	// The derivation must match the PenParser query key: "pencil:sample.lib.pen" imports
	// query "sample", so the file name must strip the whole ".lib.pen" suffix.
	FString Name;
	TestTrue(TEXT("shadcn.lib.pen derives shadcn"),
		PencilExtensionResources::TryDeriveLibraryNameFromFileName(TEXT("shadcn.lib.pen"), Name));
	TestEqual(TEXT("Derived name"), Name, FString(TEXT("shadcn")));
	TestTrue(TEXT("Bare suffix rejected"),
		!PencilExtensionResources::TryDeriveLibraryNameFromFileName(TEXT(".lib.pen"), Name));
	TestTrue(TEXT("Plain .pen rejected"),
		!PencilExtensionResources::TryDeriveLibraryNameFromFileName(TEXT("design.pen"), Name));

	// Production scan path: enumerate a directory -> derive library names -> register.
	// The fixture dir holds exactly one document (sample.lib.pen); after the scan the
	// query must hit under "sample" (a GetBaseFilename-style key like "sample.lib" is a bug).
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
	if (!Plugin.IsValid())
	{
		AddError(TEXT("Design2UMG plugin not found"));
		return false;
	}
	const FString FixtureDir = FPaths::Combine(
		Plugin->GetBaseDir(), TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures"));

	TestEqual(TEXT("Fixture directory registers one library"),
		PencilExtensionResources::RegisterLibrariesFromDirectory(FixtureDir), 1);

	FString DocumentJson;
	TestTrue(TEXT("Scan retains the full component library"), PenBundledLibraries::TryGetDocumentJson(TEXT("sample"), DocumentJson));
	TestFalse(TEXT("No leftover 'sample.lib' key"),
		PenBundledLibraries::TryGetDocumentJson(TEXT("sample.lib"), DocumentJson));
	TestEqual(TEXT("Registered payload equals the complete fixture"), DocumentJson, LoadSampleLibPenFixtureJson(*this));

	FDesignDocument Document;
	if (!TestTrue(TEXT("Scanned library variables import through the parser"), FPenParser::ParseDocument(
		TEXT(R"({"version":"2.8","imports":{"Y":"pencil:sample.lib.pen"},"children":[]})"), Document))) return false;
	TestTrue(TEXT("Scan registers under the PenParser query key"), Document.Variables.Contains(TEXT("Y:--primary")));

	const FString ScratchDir = FPaths::Combine(
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()),
		TEXT("Automation/PencilExtensionLibraryLayoutsTest"));
	IFileManager::Get().DeleteDirectory(*ScratchDir, false, true);
	ON_SCOPE_EXIT
	{
		PencilExtensionResources::RegisterLibrariesFromDirectory(FixtureDir);
		IFileManager::Get().DeleteDirectory(*ScratchDir, false, true);
	};
	TArray<FString> RelativeDirs;
	PencilExtensionResources::GetLibraryPenRelativeCandidates(RelativeDirs);
	for (const FString& RelativeDir : RelativeDirs)
	{
		const FString DataDir = ScratchDir / RelativeDir;
		if (!TestTrue(TEXT("Create library layout directory"), IFileManager::Get().MakeDirectory(*DataDir, true)))
		{
			return false;
		}
		TestEqual(TEXT("An empty library directory registers nothing"),
			PencilExtensionResources::RegisterLibrariesFromDirectory(DataDir), 0);
		const FString LibraryPath = DataDir / TEXT("sample.lib.pen");
		if (!TestTrue(TEXT("Place the existing fixture in the library layout"),
			FFileHelper::SaveStringToFile(DocumentJson, *LibraryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
		{
			return false;
		}
		TestEqual(TEXT("The existing loader registers the library layout"),
			PencilExtensionResources::RegisterLibrariesFromDirectory(DataDir), 1);
		FString RegisteredDocument;
		FString RegisteredPath;
		TestTrue(TEXT("Library layout retains the full document and its source path"),
			PenBundledLibraries::TryGetDocumentJson(TEXT("sample"), RegisteredDocument, &RegisteredPath));
		TestEqual(TEXT("Library layout preserves document contents"), RegisteredDocument, DocumentJson);
		TestEqual(TEXT("Library layout source path is retained"), RegisteredPath, LibraryPath);
	}
	return true;
}

namespace
{
    class FFontToolTestLocation
    {
        FString Original;
        bool bHadValue = false, bDirty = false;
    public:
        FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
            / (TEXT("FontTool-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        FFontToolTestLocation()
        {
            bHadValue = GConfig->GetString(TEXT("Design2UMG.FontTool"), TEXT("StorageDirectory"), Original, GEditorPerProjectIni);
            if (const FConfigFile* Config = GConfig->FindConfigFile(GEditorPerProjectIni)) bDirty = Config->Dirty;
            // Select only this fixture without inspecting/removing an existing user installation.
            GConfig->SetString(TEXT("Design2UMG.FontTool"), TEXT("StorageDirectory"), *Root, GEditorPerProjectIni);
            IFileManager::Get().MakeDirectory(*Root, true);
        }
        ~FFontToolTestLocation()
        {
            if (bHadValue) GConfig->SetString(TEXT("Design2UMG.FontTool"), TEXT("StorageDirectory"), *Original, GEditorPerProjectIni);
            else GConfig->RemoveKey(TEXT("Design2UMG.FontTool"), TEXT("StorageDirectory"), GEditorPerProjectIni);
            GConfig->Flush(false, GEditorPerProjectIni);
            if (FConfigFile* Config = GConfig->FindConfigFile(GEditorPerProjectIni)) Config->Dirty = bDirty;
            // The fixture owns this unique directory; junction tests remove their link before cleanup.
            IFileManager::Get().DeleteDirectory(*Root, false, true);
        }
    };
    FString FontToolFixtureHash(const FString& Text)
    {
        FTCHARToUTF8 Bytes(*Text);
        uint8 Digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Digest);
        return BytesToHex(Digest, sizeof(Digest));
    }
    bool WriteFontToolFixture(const PencilFontTool::FDownload& Download)
    {
        const FString Directory = Download.InstallDirectory();
        IFileManager::Get().MakeDirectory(*Directory, true);
        const FString Payload = TEXT("fixture, never executed");
        const FString Identity = FString::Printf(TEXT("{\"protocol\":1,\"binary_sha256\":\"%s\",\"files_sha256\":{\"PencilFontWorker.exe\":\"%s\"}}"), *FontToolFixtureHash(Payload), *FontToolFixtureHash(Payload));
        const FString Receipt = FString::Printf(TEXT("{\"archive_sha256\":\"%s\",\"identity_sha256\":\"%s\"}"),
            *Download.ArchiveSha256, *FontToolFixtureHash(Identity));
        auto Save = [&Directory](const FString& Name, const FString& Value)
        { return FFileHelper::SaveStringToFile(Value, *(Directory / Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); };
        return Save(TEXT("PencilFontWorker.exe"), Payload) && Save(TEXT("identity.json"), Identity) && Save(TEXT("installed.json"), Receipt);
    }
	// Explicitly selected online validation; engine ticks keep real HTTP and Slate responsive.
	class FValidateFontToolLifecycle final : public IAutomationLatentCommand
	{
		FAutomationTestBase& Test;
		PencilFontTool::FDownload Download;
        TSharedPtr<FFontToolTestLocation> Location;
		enum class EStep { CancelDownload, WrongDigest, InstallAndRemove, Cleanup };
		EStep Step = EStep::CancelDownload;
		TSharedPtr<PencilFontTool::FInstall, ESPMode::ThreadSafe> Operation;
		FHttpRequestPtr CancelledRequest;
		TArray<FString> OriginalStaging;
		double Deadline = 0;
		bool bOwnInstallation = false;

		bool Finish()
		{
			if (bOwnInstallation && PencilFontTool::HasInstallation())
			{
				FString Error;
				if (!PencilFontTool::Remove(Error)) Test.AddError(TEXT("Lifecycle cleanup failed: ") + Error);
			}
			return true;
		}

	public:
		FValidateFontToolLifecycle(FAutomationTestBase& InTest, const PencilFontTool::FDownload& InDownload, const TSharedPtr<FFontToolTestLocation>& InLocation)
			: Test(InTest), Download(InDownload), Location(InLocation)
		{
			IFileManager::Get().FindFiles(OriginalStaging, *(FPaths::GetPath(Download.InstallDirectory()) / TEXT("staging-*")), false, true);
		}

		virtual bool Update() override
		{
			if (!Operation)
			{
				if (PencilFontTool::HasInstallation() || PencilFontTool::IsBusy())
				{
					Test.AddError(TEXT("Lifecycle step refused: an installation or another tool operation appeared before Install."));
					return Finish();
				}
				PencilFontTool::FDownload Request = Download;
				if (Step == EStep::WrongDigest)
					Request.ArchiveSha256[0] = Request.ArchiveSha256[0] == TEXT('0') ? TEXT('1') : TEXT('0');
				Operation = PencilFontTool::Install(Request);
				if (!Operation) { Test.AddError(TEXT("Lifecycle Install was refused because the service is busy.")); return Finish(); }
                FString LocationError;
                Test.TestFalse(TEXT("Active install prevents location changes"), PencilFontTool::SetStorageDirectory(Location->Root / TEXT("blocked"), LocationError));
                Test.TestEqual(TEXT("Async install captures the chosen destination"), Operation->Directory, Request.InstallDirectory());
				if (Step == EStep::CancelDownload)
				{
					CancelledRequest = Operation->Request;
					Operation->Cancel();
				}
				Deadline = FPlatformTime::Seconds() + 330;
				return false;
			}
			if (!Operation->Poll() || (CancelledRequest && CancelledRequest->GetStatus() == EHttpRequestStatus::Processing))
			{
				if (Step != EStep::Cleanup && FPlatformTime::Seconds() > Deadline)
				{
					Test.AddError(FString::Printf(TEXT("Lifecycle step %d timed out; cancelling and waiting for cleanup. Last error: %s"),
						static_cast<int32>(Step), *Operation->Error));
					if (Operation->Request) CancelledRequest = Operation->Request;
					Operation->Cancel();
					Step = EStep::Cleanup;
				}
				return false;
			}
			Test.AddInfo(FString::Printf(TEXT("Lifecycle step %d completed: %s"), static_cast<int32>(Step),
				Operation->Error.IsEmpty() ? TEXT("success") : *Operation->Error));
			CancelledRequest.Reset();
			bOwnInstallation |= Operation->Error.IsEmpty() && PencilFontTool::HasInstallation();
			TArray<FString> RemainingStaging;
			IFileManager::Get().FindFiles(RemainingStaging, *(FPaths::GetPath(Download.InstallDirectory()) / TEXT("staging-*")), false, true);
			for (const FString& Directory : OriginalStaging) RemainingStaging.Remove(Directory);
			if (!Test.TestTrue(TEXT("Completed work has cleaned its staging directory"), RemainingStaging.IsEmpty()))
			{
				Test.AddError(TEXT("Staging left after completion: ") + FString::Join(RemainingStaging, TEXT("\n")));
				return Finish();
			}
			if (Step == EStep::Cleanup) return Finish();
			if (Step == EStep::CancelDownload)
			{
				if (!Test.TestFalse(TEXT("Immediately cancelled download leaves no installation"), PencilFontTool::HasInstallation())
					|| !Test.TestFalse(TEXT("Cancelled download is not usable"), PencilFontTool::IsInstalled())) return Finish();
				Step = EStep::WrongDigest;
				Operation.Reset();
				return false;
			}
			if (Step == EStep::WrongDigest)
			{
				if (!Test.TestTrue(TEXT("Real download with the wrong digest fails archive verification"), Operation->Error == FPencilFontToolText().Get(TEXT("ArchiveVerificationFailed")))
					|| !Test.TestFalse(TEXT("Wrong digest leaves no installation"), PencilFontTool::HasInstallation())
					|| !Test.TestFalse(TEXT("Wrong digest is not usable"), PencilFontTool::IsInstalled())) return Finish();
				Step = EStep::InstallAndRemove;
				Operation.Reset();
				return false;
			}
			if (!Test.TestTrue(TEXT("Real HTTP and background installation completed successfully"), Operation->Error.IsEmpty())
				|| !Test.TestTrue(TEXT("Verified installation is usable"), PencilFontTool::IsInstalled())) return Finish();
			FString Worker, Error;
			const bool bAcquired = PencilFontTool::AcquireWorker(Worker, Error);
			if (!Test.TestTrue(TEXT("Acquire verifies the installed worker and dependencies"), bAcquired))
			{
				Test.AddError(TEXT("AcquireWorker: ") + Error);
				return Finish();
			}
			Error.Reset();
			Test.TestFalse(TEXT("Remove is refused while the worker is acquired"), PencilFontTool::Remove(Error));
            FString LocationError;
            Test.TestFalse(TEXT("Acquired worker prevents location changes"), PencilFontTool::SetStorageDirectory(Location->Root / TEXT("blocked"), LocationError));
			Test.AddInfo(TEXT("Remove while working: ") + Error);
			Test.TestFalse(TEXT("Busy removal supplies its full reason"), Error.IsEmpty());
			PencilFontTool::ReleaseWorker();
			Error.Reset();
			if (!Test.TestTrue(TEXT("Remove succeeds after worker release"), PencilFontTool::Remove(Error)))
			{
				Test.AddError(TEXT("Remove after release: ") + Error);
				return Finish();
			}
			bOwnInstallation = false;
			Test.TestFalse(TEXT("Removal deletes the tool directory"), PencilFontTool::HasInstallation());
			Test.TestFalse(TEXT("Removed tool is not installed"), PencilFontTool::IsInstalled());
			Error.Reset();
			const bool bAcquiredAfterRemoval = PencilFontTool::AcquireWorker(Worker, Error);
			Test.TestFalse(TEXT("Acquire fails again after removal"), bAcquiredAfterRemoval);
			if (bAcquiredAfterRemoval) PencilFontTool::ReleaseWorker();
			Test.AddInfo(TEXT("Acquire after removal: ") + Error);
			Test.TestTrue(TEXT("Missing tool gives the installation error"), Error == FPencilFontToolText().Get(TEXT("NotInstalled")));
			return Finish();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilFontToolDownloadTest,
	"Design2UMG.Pencil.FontTool.DownloadManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPencilFontToolDownloadTest::RunTest(const FString& Parameters)
{
	if (!TestFalse(TEXT("Path tests require an idle tool service"), PencilFontTool::IsBusy())) return false;
    auto Location = MakeShared<FFontToolTestLocation>();
	const FString Manifest = TEXT(R"({"version":"1.0.0","url":"https://github.com/DiracSea12/Design2UMG/releases/download/pencil-font-worker-1.0.0/PencilFontWorker-1.0.0-Win64.zip","archive_sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","archive_bytes":100,"installed_bytes":200})");
	PencilFontTool::FDownload Download;
	FString Error;
	if (!TestTrue(TEXT("Fixed release contract parses"), PencilFontTool::ParseDownload(Manifest, Download, Error))) return false;
	TestEqual(TEXT("Download byte count retained"), Download.ArchiveBytes, int64(100));
	TestEqual(TEXT("Installed byte count retained"), Download.InstalledBytes, int64(200));
	TestEqual(TEXT("Tool uses its own versioned directory under the selected storage location"), Download.InstallDirectory(),
        Location->Root / TEXT("PencilFontWorker/1.0.0"));
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
    if (!TestTrue(TEXT("Plugin is discoverable"), Plugin.IsValid())) return false;
    TestEqual(TEXT("Default storage is inside the active plugin, not AppData"), PencilFontTool::DefaultStorageDirectory(),
        FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Saved/Tools")));
    TestTrue(TEXT("Chosen location is saved"), PencilFontTool::SetStorageDirectory(Location->Root / TEXT("chosen{root}"), Error));
    FConfigFile SavedConfig;
    SavedConfig.Read(GEditorPerProjectIni);
    FString SavedLocation;
    TestTrue(TEXT("Location is written to editor user config"), SavedConfig.GetString(TEXT("Design2UMG.FontTool"), TEXT("StorageDirectory"), SavedLocation));
    TestEqual(TEXT("Persisted location equals the selected location"), SavedLocation, PencilFontTool::StorageDirectory());
    TestEqual(TEXT("Template arguments may contain braces in user paths"),
        FPencilFontToolText().Get(TEXT("ActualLocation"), {{TEXT("InstallDirectory"), TEXT("X:/{user}/PencilFontWorker/1.0.0")}}).Contains(TEXT("X:/{user}/PencilFontWorker/1.0.0")), true);
    TestTrue(TEXT("Missing text keys identify the editable resource and key"),
        FPencilFontToolText().Get(TEXT("TestMissingKey")).Contains(TEXT("Resources/FontToolText.json [TestMissingKey:")));
#if PLATFORM_WINDOWS
    const FString Directory = Download.InstallDirectory();
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString UserFile = Directory / TEXT("keep.txt");
    TestTrue(TEXT("Create foreign destination content"), FFileHelper::SaveStringToFile(TEXT("user file"), *UserFile));
    auto Occupied = PencilFontTool::Install(Download);
    TestTrue(TEXT("Foreign destination is refused before HTTP"), Occupied && Occupied->bComplete && !Occupied->Request);
    TestFalse(TEXT("Missing receipt never permits removal"), PencilFontTool::Remove(Error));
    TestTrue(TEXT("Unregistered user content remains"), FPaths::FileExists(UserFile));
    PencilFontTool::FDownload RealDownload;
    if (!TestTrue(TEXT("Use the real archive identity for ownership fixture"), PencilFontTool::ReadDownload(RealDownload, Error))) return false;
    if (!TestTrue(TEXT("Create an owned tool fixture"), WriteFontToolFixture(RealDownload))) return false;
    TestFalse(TEXT("Existing installation prevents location changes"), PencilFontTool::SetStorageDirectory(Location->Root / TEXT("other"), Error));
    TestTrue(TEXT("Owned files can be removed"), PencilFontTool::Remove(Error));
    TestTrue(TEXT("Extra user file in the installation folder is preserved"), FPaths::FileExists(UserFile));
    TestFalse(TEXT("Owned executable is removed"), FPaths::FileExists(Directory / TEXT("PencilFontWorker.exe")));
    TestFalse(TEXT("Preserved user files do not count as an installed tool"), PencilFontTool::HasInstallation());
    if (!TestTrue(TEXT("Create fixture for record cleanup retry"), WriteFontToolFixture(RealDownload))) return false;
    const FString ReceiptFile = Directory / TEXT("installed.json"), IdentityFile = Directory / TEXT("identity.json");
    TArray<uint8> ReceiptBefore, ReceiptAfter;
    FString UserContents;
    if (!TestTrue(TEXT("Read original receipt bytes"), FFileHelper::LoadFileToArray(ReceiptBefore, *ReceiptFile))) return false;
    {
        ON_SCOPE_EXIT { TestTrue(TEXT("Restore writable fixture identity"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*IdentityFile, false)); };
        if (!TestTrue(TEXT("Make fixture identity read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*IdentityFile, true))) return false;
        TestFalse(TEXT("Read-only identity prevents record cleanup"), PencilFontTool::Remove(Error));
        TestEqual(TEXT("Record cleanup reports the read-only identity"), Error, FPencilFontToolText().Get(TEXT("RemoveFailed"), {{TEXT("File"), IdentityFile}}));
        TestTrue(TEXT("Receipt remains readable after identity deletion fails"), FFileHelper::LoadFileToArray(ReceiptAfter, *ReceiptFile));
        TestTrue(TEXT("Receipt is restored with identical bytes"), ReceiptBefore == ReceiptAfter);
        TestTrue(TEXT("Foreign file is unchanged after record cleanup failure"), FFileHelper::LoadFileToString(UserContents, *UserFile) && UserContents == TEXT("user file"));
    }
    TestTrue(TEXT("Removal retries successfully after identity becomes writable"), PencilFontTool::Remove(Error));
    TestTrue(TEXT("Foreign file is unchanged after record cleanup retry"), FFileHelper::LoadFileToString(UserContents, *UserFile) && UserContents == TEXT("user file"));
    TestTrue(TEXT("Create fixture for damaged record check"), WriteFontToolFixture(RealDownload));
    FFileHelper::SaveStringToFile(TEXT("{}"), *(Directory / TEXT("installed.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TestFalse(TEXT("Damaged receipt refuses deletion"), PencilFontTool::Remove(Error));
    TestTrue(TEXT("Damaged receipt leaves tool payload"), FPaths::FileExists(Directory / TEXT("PencilFontWorker.exe")));
    // Only fixture-owned contents are cleared; user configuration and existing tools are untouched.
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    TestTrue(TEXT("Return to empty fixture location"), PencilFontTool::SetStorageDirectory(Location->Root, Error));
    const FString Outside = Location->Root / TEXT("outside"), Link = Location->Root / TEXT("PencilFontWorker");
    IFileManager::Get().MakeDirectory(*Outside, true);
    const FString Sentinel = Outside / TEXT("keep.txt");
    FFileHelper::SaveStringToFile(TEXT("outside content"), *Sentinel);
    int32 LinkExit = -1;
    FString Cmd = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")) / TEXT("System32/cmd.exe");
    FPaths::MakePlatformFilename(Cmd);
    const FString LinkCommand = FString::Printf(TEXT("/d /c mklink /J \"%s\" \"%s\""), *Link, *Outside);
    const bool bLinkCreated = FPlatformProcess::ExecProcess(*Cmd, *LinkCommand, &LinkExit, nullptr, nullptr) && LinkExit == 0;
    if (!TestTrue(TEXT("Create real Windows junction inside the managed subtree"), bLinkCreated)) return false;
    ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Link, false, false); };
    auto Linked = PencilFontTool::Install(Download);
    TestTrue(TEXT("Managed junction is rejected before HTTP"), Linked && Linked->bComplete && !Linked->Request);
    TestFalse(TEXT("Removal rejects a managed junction"), PencilFontTool::Remove(Error));
    TestFalse(TEXT("Unsafe managed junction is not an installation"), PencilFontTool::HasInstallation());
    TestTrue(TEXT("An uninstalled managed junction permits choosing a safe location"), PencilFontTool::SetStorageDirectory(Location->Root / TEXT("safe"), Error));
    TestTrue(TEXT("Real junction target content is untouched"), FPaths::FileExists(Sentinel));
    if (!TestTrue(TEXT("Remove only the fixture junction before further tests"), IFileManager::Get().DeleteDirectory(*Link, false, false))) return false;
#endif
	TestFalse(TEXT("An alternate download origin is rejected"), PencilFontTool::ParseDownload(
		Manifest.Replace(TEXT("https://github.com/"), TEXT("https://example.com/")), Download, Error));
	TestFalse(TEXT("A missing complete archive digest is rejected"), PencilFontTool::ParseDownload(
		Manifest.Replace(TEXT("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"), TEXT("bad")), Download, Error));
	TestFalse(TEXT("An unknown version cannot choose a different installation directory"), PencilFontTool::ParseDownload(
		Manifest.Replace(TEXT("\"version\":\"1.0.0\""), TEXT("\"version\":\"../other\"")), Download, Error));
	FHttpModule& Http = FHttpModule::Get();
	if (!TestFalse(TEXT("Synchronous rejection requires an idle tool service"), PencilFontTool::IsBusy())
		|| !TestFalse(TEXT("Synchronous rejection uses the real HTTP backend"), Http.IsNullHttpEnabled())) return false;
	FConfigFile* EngineConfig = GConfig->FindConfigFile(GEngineIni);
	if (!TestNotNull(TEXT("HTTP configuration is loaded"), EngineConfig)) return false;
	FString OriginalHttpValue;
	const bool bHadHttpValue = GConfig->GetString(TEXT("HTTP"), TEXT("bEnableHttp"), OriginalHttpValue, GEngineIni);
	const bool bWasHttpEnabled = Http.IsHttpEnabled();
	const bool bWasConfigDirty = EngineConfig->Dirty;
	{
		ON_SCOPE_EXIT
		{
			// UpdateConfigs leaves a missing key unchanged, so restore runtime state before key presence.
			GConfig->SetBool(TEXT("HTTP"), TEXT("bEnableHttp"), bWasHttpEnabled, GEngineIni);
			Http.UpdateConfigs();
			if (bHadHttpValue) GConfig->SetString(TEXT("HTTP"), TEXT("bEnableHttp"), *OriginalHttpValue, GEngineIni);
			else GConfig->RemoveKey(TEXT("HTTP"), TEXT("bEnableHttp"), GEngineIni);
			EngineConfig->Dirty = bWasConfigDirty;
		};
		GConfig->SetBool(TEXT("HTTP"), TEXT("bEnableHttp"), false, GEngineIni);
		Http.UpdateConfigs();
		if (!TestFalse(TEXT("Engine HTTP is disabled before attempting Install"), Http.IsHttpEnabled())) return false;
		const auto Rejected = PencilFontTool::Install(Download);
		if (!TestTrue(TEXT("Disabled HTTP returns a completed installation operation"), Rejected.IsValid())) return false;
		ON_SCOPE_EXIT
		{
			Rejected->Cancel();
			if (Rejected->Work.IsValid()) Rejected->Work.Wait();
		};
		TestTrue(TEXT("Startup rejection completed synchronously before Install returned"), Rejected->bComplete);
        const FString FrozenDirectory = Rejected->Directory;
        TestTrue(TEXT("Idle completed request permits a new location"), PencilFontTool::SetStorageDirectory(Location->Root / TEXT("after-request"), Error));
        TestEqual(TEXT("Install request retains its captured destination after config changes"), Rejected->Directory, FrozenDirectory);
		TestEqual(TEXT("Startup failure preserves the synchronous completion callback error"), Rejected->Error,
			FPencilFontToolText().Get(TEXT("DownloadFailed")));
		TestFalse(TEXT("Completed rejection releases its request member"), Rejected->Request.IsValid());
		TestFalse(TEXT("Disabled HTTP starts no background installation work"), Rejected->Work.IsValid());
		TestFalse(TEXT("Completed rejection leaves the tool service idle"), PencilFontTool::IsBusy());
		AddInfo(TEXT("Synchronous HTTP rejection: ") + Rejected->Error);
	}
	FString RestoredHttpValue;
	TestEqual(TEXT("Original HTTP enabled state is restored"), Http.IsHttpEnabled(), bWasHttpEnabled);
	TestEqual(TEXT("Original HTTP configuration key presence is restored"),
		GConfig->GetString(TEXT("HTTP"), TEXT("bEnableHttp"), RestoredHttpValue, GEngineIni), bHadHttpValue);
	TestEqual(TEXT("Original HTTP configuration value is restored"), RestoredHttpValue, OriginalHttpValue);
	TestEqual(TEXT("Original HTTP configuration dirty state is restored"), bool(EngineConfig->Dirty), bWasConfigDirty);
	if (!FParse::Param(FCommandLine::Get(), TEXT("D2UValidateFontToolLifecycle"))) return true;
	if (!TestTrue(TEXT("Read real release contract for explicitly selected online validation"), PencilFontTool::ReadDownload(Download, Error)))
	{ AddError(Error); return false; }
	if (PencilFontTool::HasInstallation() || PencilFontTool::IsBusy())
	{
		AddError(TEXT("Online lifecycle validation refused: an existing user installation or active tool operation must be left untouched."));
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FValidateFontToolLifecycle(*this, Download, Location));
	return true;
}

#endif
