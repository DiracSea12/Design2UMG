// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "FontResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

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

#endif
