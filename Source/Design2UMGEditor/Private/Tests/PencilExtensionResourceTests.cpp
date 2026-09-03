// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ExternalServices/PencilExtensionLocator.h"
#include "ExternalServices/PencilExtensionResources.h"
#include "FontResolver.h"

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

	TMap<FString, TMap<FString, FString>> GlyphsByFamily;
	TMap<FString, TMap<FString, FString>> NamesByFamily;
	FString Error;
	TestTrue(TEXT("Fixture parses"), PencilExtensionResources::ParseIconIndexJson(Json, GlyphsByFamily, NamesByFamily, Error));
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

	return true;
}

// ---------------------------------------------------------------------------
// Registration round trip: parser output -> FFontResolver -> query hits
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPencilExtensionIconGlyphLookupRoundTripTest,
	"Design2UMG.Unit.PencilExtensionResources.IconGlyphLookupRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilExtensionIconGlyphLookupRoundTripTest::RunTest(const FString&)
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
	TestTrue(TEXT("Icon index relative candidate known"),
		RelativeCandidates.Contains(TEXT("node_modules/@ha/pencil-editor/src/managers/data/icon-index.json")));
	if (Roots.Num() > 0 && RelativeCandidates.Num() > 0)
	{
		TestEqual(TEXT("Icon index candidate path construction"),
			Roots[0].Directory / RelativeCandidates[0],
			PrimaryDir / TEXT("highagency.pencildev-0.6.100") / TEXT("node_modules/@ha/pencil-editor/src/managers/data/icon-index.json"));
	}

	IFileManager::Get().DeleteDirectory(*ScratchDir, false, true);
	return true;
}

#endif
