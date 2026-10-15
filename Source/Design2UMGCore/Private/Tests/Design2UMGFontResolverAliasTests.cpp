// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "DesignTypeMapper.h"
#include "Design2UMGFontAccess.h"
#include "FontResolver.h"
#include "Engine/FontFace.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFontAssetRegistryAccessTest,
	"Design2UMG.FontResolver.AssetRegistryAccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFontAssetRegistryAccessTest::RunTest(const FString& Parameters)
{
	UFont* Font = NewObject<UFont>(GetTransientPackage());
	const FAssetData FontAsset(Font);
	const FAssetData FaceAsset(NewObject<UFontFace>(GetTransientPackage()));
	TestTrue(TEXT("Font assets match the native class key"), Design2UMGFontAccess::IsFontAsset(FontAsset));
	TestFalse(TEXT("Font faces are not font assets"), Design2UMGFontAccess::IsFontAsset(FaceAsset));
	TestEqual(TEXT("Asset paths preserve the native object identity"),
		FontAsset.ToSoftObjectPath().ToString(), Font->GetPathName());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFontResolverIconAliasTableTest,
	"Design2UMG.FontResolver.IconAliasTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFontResolverIconAliasTableTest::RunTest(const FString& Parameters)
{
	// Static alias table: design-source names map onto bundled icon glyphs.
	TestEqual(TEXT("more-horizontal aliases to ellipsis"),
		FFontResolver::NormalizeIconNameKey(TEXT("more-horizontal")), FString(TEXT("ellipsis")));
	TestEqual(TEXT("more-vertical aliases to ellipsis-vertical"),
		FFontResolver::NormalizeIconNameKey(TEXT("more-vertical")), FString(TEXT("ellipsis-vertical")));

	// Normalization: case-insensitive and separator-tolerant, then alias applied.
	TestEqual(TEXT("More_Horizontal (mixed case/underscore) aliases too"),
		FFontResolver::NormalizeIconNameKey(TEXT(" More_Horizontal ")), FString(TEXT("ellipsis")));
	TestEqual(TEXT("MORE-VERTICAL upper-case aliases too"),
		FFontResolver::NormalizeIconNameKey(TEXT("MORE-VERTICAL")), FString(TEXT("ellipsis-vertical")));

	// Non-aliased names pass through normalized.
	TestEqual(TEXT("Unknown name passes through"),
		FFontResolver::NormalizeIconNameKey(TEXT("Chevron_Right")), FString(TEXT("chevron-right")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFontWeightAliasSingleSourceTest,
	"Design2UMG.FontResolver.FontWeightAliasSingleSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFontWeightAliasSingleSourceTest::RunTest(const FString& Parameters)
{
	// Both weight consumers share one table: full alias coverage on the numeric and typeface side.
	TestEqual(TEXT("ultralight parses to 200"), FFontResolver::ParseWeightValue(TEXT("ultralight")), 200);
	TestEqual(TEXT("ultralight maps to ExtraLight"), DesignTypeMapper::MapFontWeight(TEXT("ultralight")), FName(TEXT("ExtraLight")));
	TestEqual(TEXT("demibold parses to 600"), FFontResolver::ParseWeightValue(TEXT("demibold")), 600);
	TestEqual(TEXT("demibold maps to SemiBold"), DesignTypeMapper::MapFontWeight(TEXT("demibold")), FName(TEXT("SemiBold")));
	TestEqual(TEXT("ultrabold parses to 800"), FFontResolver::ParseWeightValue(TEXT("ultrabold")), 800);
	TestEqual(TEXT("ultrabold maps to ExtraBold"), DesignTypeMapper::MapFontWeight(TEXT("ultrabold")), FName(TEXT("ExtraBold")));
	TestEqual(TEXT("heavy parses to 900"), FFontResolver::ParseWeightValue(TEXT("heavy")), 900);
	TestEqual(TEXT("heavy maps to Black"), DesignTypeMapper::MapFontWeight(TEXT("heavy")), FName(TEXT("Black")));

	// Out-of-range numerics fall back to 400/Regular on both sides (fallback, not clamp).
	TestEqual(TEXT("950 falls back to 400"), FFontResolver::ParseWeightValue(TEXT("950")), 400);
	TestEqual(TEXT("950 maps to Regular"), DesignTypeMapper::MapFontWeight(TEXT("950")), FName(TEXT("Regular")));
	TestEqual(TEXT("50 falls back to 400"), FFontResolver::ParseWeightValue(TEXT("50")), 400);
	TestEqual(TEXT("50 maps to Regular"), DesignTypeMapper::MapFontWeight(TEXT("50")), FName(TEXT("Regular")));

	// Non-hundred in-range numerics round to the nearest hundred consistently.
	TestEqual(TEXT("550 rounds to 600"), FFontResolver::ParseWeightValue(TEXT("550")), 600);
	TestEqual(TEXT("550 maps to SemiBold"), DesignTypeMapper::MapFontWeight(TEXT("550")), FName(TEXT("SemiBold")));

	// Trim behavior on padded input and the shared numeric-to-typeface mapping stay locked here.
	TestEqual(TEXT("padded bold input trims to 700"), FFontResolver::ParseWeightValue(TEXT(" Bold ")), 700);
	TestEqual(TEXT("weight 700 maps to Bold typeface"),
		FFontResolver::TypefaceNameForWeightValue(700), FString(TEXT("Bold")));
	return true;
}

#endif
