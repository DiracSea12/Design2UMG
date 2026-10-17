// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Spacer.h"
#include "Misc/AutomationTest.h"
#include "UMGWidgetTreeBuilder.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTreeUniqueNamesTest,
	"Design2UMG.Generation.UniqueWidgetNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetTreeUniqueNamesTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UWidgetTree> Tree(NewObject<UWidgetTree>());
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		const FString Name = Index == 0 ? TEXT("Repeated") : FString::Printf(TEXT("Repeated_%d"), Index);
		if (!TestNotNull(TEXT("Occupied widget name created"),
			Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), FName(*Name))))
		{
			return false;
		}
	}
	for (int32 Index = 1; Index < 1000; ++Index)
	{
		if (!TestNotNull(TEXT("Occupied property-collision name created"),
			Tree->ConstructWidget<USpacer>(USpacer::StaticClass(),
				FName(*FString::Printf(TEXT("ToolTipText_w%d"), Index)))))
		{
			return false;
		}
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("Spacer");
	Node.Name = TEXT("Repeated");
	UWidget* RepeatedWidget = FUMGWidgetTreeBuilder::BuildWidgetTree(Tree.Get(), Node, nullptr, nullptr);
	if (TestNotNull(TEXT("Repeated-name widget created"), RepeatedWidget))
	{
		TestEqual(TEXT("Duplicate names remain unique beyond 999 suffixes"),
			RepeatedWidget->GetFName(), FName(TEXT("Repeated_1000")));
		TestEqual(TEXT("Reimport preserves the duplicate-name suffix rank"),
			FUMGWidgetTreeBuilder::GetWidgetNameSuffixRank(RepeatedWidget->GetName(), TEXT("Repeated")), 1999);
	}
	Node.Name = TEXT("ToolTipText");
	UWidget* PropertyCollisionWidget = FUMGWidgetTreeBuilder::BuildWidgetTree(Tree.Get(), Node, nullptr, nullptr);
	if (TestNotNull(TEXT("Property-collision widget created"), PropertyCollisionWidget))
	{
		TestEqual(TEXT("Property-collision names remain unique beyond 999 suffixes"),
			PropertyCollisionWidget->GetFName(), FName(TEXT("ToolTipText_w1000")));
		TestEqual(TEXT("Reimport preserves the property-collision suffix rank"),
			FUMGWidgetTreeBuilder::GetWidgetNameSuffixRank(PropertyCollisionWidget->GetName(), TEXT("ToolTipText")), 2000);
	}
	return true;
}

#endif
