// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGAutomationTestAdapter.h"

#if WITH_DEV_AUTOMATION_TESTS

DEFINE_LOG_CATEGORY_STATIC(LogDesign2UMGAutomationTestAdapter, Log, All);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGAutomationTestAdapterLiteralTest,
	"Design2UMG.Automation.ExpectedLiteralAdapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGAutomationTestAdapterLiteralTest::RunTest(const FString&)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	const FString ContainsLiteral = TEXT("adapter.contains[1]+(literal)?^$|{2}\\tail");
	ExpectedLogMessages.Expect(ContainsLiteral.ToLower(), ELogVerbosity::Warning,
		Design2UMGAutomationTestAdapter::EMessageMatch::Contains, 2);
	UE_LOG(LogDesign2UMGAutomationTestAdapter, Warning, TEXT("prefix %s suffix"), *ContainsLiteral.ToUpper());
	UE_LOG(LogDesign2UMGAutomationTestAdapter, Warning, TEXT("prefix %s suffix"), *ContainsLiteral);

	const FString ExactLiteral = TEXT("adapter.exact[2]+(literal)?^$|{3}\\tail");
	ExpectedLogMessages.Expect(ExactLiteral.ToLower(), ELogVerbosity::Error,
		Design2UMGAutomationTestAdapter::EMessageMatch::Exact, 1);
	UE_LOG(LogDesign2UMGAutomationTestAdapter, Error, TEXT("%s"), *ExactLiteral.ToUpper());

	return true;
}

#endif
