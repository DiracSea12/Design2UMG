// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
//
// QA white-box structural tests (run d2u-cmsr-slice-a, Component Mapping phases 1/2).
// Test design independent of the in-delivery dev tests: covers behaviors locked by implementation but
// not exercised by dev cases - case-insensitive Key comparison and rejection of invalid identities.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DesignComponentMapping.h"

namespace
{
	using namespace DesignComponentMapping;

	FComponentMappingEntry MakeQAEntry(const FString& Key)
	{
		FComponentMappingEntry Entry;
		Entry.Key = Key;
		Entry.Provider = TEXT("figma");
		Entry.DocumentScope = TEXT("qa-case-scope");
		Entry.ComponentKey = TEXT("component-key-1");
		Entry.WidgetClass = TEXT("/Game/UI/WBP_QACase.WBP_QACase_C");
		return Entry;
	}
}

// ---------------------------------------------------------------------------
// Match-table comparison semantics: Key comparison is case-insensitive (the canonical string assembled
// from Provider/DocumentScope/ComponentKey compares with IgnoreCase as a whole); an identity with any
// empty segment is invalid and must be rejected by both matching tiers - no accidental hits or misses from case or empty fields.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQAKeyCaseInsensitiveAndInvalidIdentityTest,
	"Design2UMG.Unit.ComponentMapping.QAKeyCaseInsensitiveAndInvalidIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAKeyCaseInsensitiveAndInvalidIdentityTest::RunTest(const FString&)
{
	FComponentMappingTable Table;
	// The ini stores an all-uppercase scope; the import side resolves the Key in lowercase.
	Table.Entries.Add(MakeQAEntry(TEXT("figma|QA-CASE-SCOPE|component-key-1")));

	FDesignComponentIdentity Identity;
	Identity.Provider = TEXT("figma");
	Identity.DocumentScope = TEXT("qa-case-scope");
	Identity.ComponentKey = TEXT("component-key-1");

	bool bSetLevel = true;
	const FComponentMappingEntry* Hit = Table.Match(Identity, bSetLevel);
	TestNotNull(TEXT("key match is case-insensitive across all three segments"), Hit);
	if (Hit)
	{
		TestFalse(TEXT("exact variant hit is not flagged as set level"), bSetLevel);
		TestEqual(TEXT("case-insensitive hit returns the configured class"),
			Hit->WidgetClass, TEXT("/Game/UI/WBP_QACase.WBP_QACase_C"));
	}

	// Invalid identity (empty ComponentKey): both matching tiers must reject it; no falling through to the set-level fallback.
	FDesignComponentIdentity EmptyComponentKey;
	EmptyComponentKey.Provider = TEXT("figma");
	EmptyComponentKey.DocumentScope = TEXT("qa-case-scope");
	EmptyComponentKey.ComponentKey = TEXT("");
	EmptyComponentKey.ComponentSetKey = TEXT("56:78");
	bool bUnusedSetLevel = false;
	TestNull(TEXT("empty component key identity never matches"),
		Table.Match(EmptyComponentKey, bUnusedSetLevel));

	FDesignComponentIdentity EmptyProvider;
	EmptyProvider.Provider = TEXT("");
	EmptyProvider.DocumentScope = TEXT("qa-case-scope");
	EmptyProvider.ComponentKey = TEXT("component-key-1");
	TestNull(TEXT("empty provider identity never matches"),
		Table.Match(EmptyProvider, bUnusedSetLevel));

	// The generation stage looks entries up by Key (TryCreateMappedWidget / ApplyMappedComponentInputs)
	// via FindEntryByKey, whose comparison semantics must match Match: case-insensitive.
	const FComponentMappingEntry* ByKey = Table.FindEntryByKey(TEXT("FIGMA|qa-case-scope|COMPONENT-KEY-1"));
	TestNotNull(TEXT("find-by-key is case-insensitive"), ByKey);
	if (ByKey)
	{
		TestEqual(TEXT("find-by-key returns the same entry"),
			ByKey->WidgetClass, TEXT("/Game/UI/WBP_QACase.WBP_QACase_C"));
	}
	TestNull(TEXT("find-by-key with unknown key returns null"),
		Table.FindEntryByKey(TEXT("figma|qa-case-scope|component-key-999")));

	// Key parsing also holds for case-insensitive input (a Provider with drifted case in the ini still parses into three segments).
	FString OutProvider;
	FString OutScope;
	FString OutComponentKey;
	TestTrue(TEXT("mixed-case key still parses into three segments"),
		ParseMappingKey(TEXT("Figma|QA-CASE-SCOPE|Component-Key-1"), OutProvider, OutScope, OutComponentKey));
	TestEqual(TEXT("parsed provider preserves original text"), OutProvider, TEXT("Figma"));
	TestEqual(TEXT("parsed scope preserves original text"), OutScope, TEXT("QA-CASE-SCOPE"));
	TestEqual(TEXT("parsed component key preserves original text"), OutComponentKey, TEXT("Component-Key-1"));
	return true;
}

#endif
