// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "GeneratedAssetLifecycle.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UMGGenerator.h"
#include "UMGSchema.h"
#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/PanelWidget.h"

#if WITH_AUTOMATION_WORKER

namespace
{
	struct FAutoGridChildSpec
	{
		float X = 0.0f;
		float Y = 0.0f;
		float W = 0.0f;
		float H = 0.0f;
	};

	FUMGWidgetNode MakeGridNodeWithChildren(const TArray<FAutoGridChildSpec>& ChildSpecs)
	{
		FUMGWidgetNode Grid;
		Grid.Type = TEXT("UniformGridPanel");
		Grid.Name = TEXT("AutoGridRoot");
		Grid.Bounds = { 0.0f, 0.0f, 800.0f, 600.0f };

		int32 Index = 0;
		for (const FAutoGridChildSpec& Spec : ChildSpecs)
		{
			FUMGWidgetNode Child;
			Child.Type = TEXT("TextBlock");
			Child.Name = FString::Printf(TEXT("AutoGridCell_%d"), Index);
			Child.Bounds = { Spec.X, Spec.Y, Spec.W, Spec.H };
			Child.Text.Text = FString::Printf(TEXT("%d"), Index);
			Grid.Children.Add(MoveTemp(Child));
			++Index;
		}
		return Grid;
	}

	// Drive the real widget-tree build path and collect the assigned (Row, Column)
	// per child in child order. BuildWidgetBlueprint creates a named (unsaved)
	// /Game package; each blueprint is discarded right after use.
	TArray<FIntPoint> BuildBlueprintAndReadGridSlots(FAutomationTestBase& Test, FUMGWidgetNode GridNode)
	{
		FUMGWidgetData Data;
		Data.RootWidget = MoveTemp(GridNode);

		UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(
			Data, TEXT("/Game/Design2UMG/_TestScratch/AutoGridThreshold"));
		if (!Test.TestNotNull(TEXT("Auto-grid blueprint builds"), Blueprint)
			|| !Blueprint || !Blueprint->WidgetTree)
		{
			return {};
		}

		TArray<UWidget*> Widgets;
		Blueprint->WidgetTree->GetAllWidgets(Widgets);

		ON_SCOPE_EXIT
		{
			// BuildWidgetBlueprint creates a named /Game package marked dirty;
			// discard it so repeated runs do not accumulate memory assets.
			GeneratedAssetLifecycle::DiscardUnsavedAsset(Blueprint);
		};

		// Children of the grid are the only non-panel widgets in this tree;
		// collect their grid slots directly (widget names get sanitized, so
		// name-keyed lookups are unreliable here).
		TArray<FIntPoint> Slots;
		for (UWidget* Widget : Widgets)
		{
			if (Widget->IsA<UPanelWidget>())
			{
				continue;
			}
			if (const UUniformGridSlot* GridSlot = Cast<UUniformGridSlot>(Widget->Slot))
			{
				Slots.Add(FIntPoint(GridSlot->GetRow(), GridSlot->GetColumn()));
			}
			else
			{
				Test.AddError(FString::Printf(
					TEXT("Child widget '%s' did not receive a UniformGridSlot"),
					*Widget->GetFName().GetPlainNameString()));
			}
		}
		return Slots;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTreeChildBuilderAutoGridRowThresholdBoundaryTest,
	"Design2UMG.WidgetTreeChildBuilder.AutoGridRowThresholdBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetTreeChildBuilderAutoGridRowThresholdBoundaryTest::RunTest(const FString& Parameters)
{
	// Row threshold = max(AvgHeight * 0.5, 0.01). These cases pin the documented
	// boundary behavior of that heuristic without changing it.

	// Case 1: homogeneous 40px-high items, second row starts exactly one item
	// height below the first (40 > 20 threshold) => two rows.
	{
		const TArray<FAutoGridChildSpec> Specs = {
			{   0.0f,  0.0f, 80.0f, 40.0f },
			{ 100.0f,  0.0f, 80.0f, 40.0f },
			{   0.0f, 40.0f, 80.0f, 40.0f },
			{ 100.0f, 40.0f, 80.0f, 40.0f },
		};
		const TArray<FIntPoint> Slots =
			BuildBlueprintAndReadGridSlots(*this, MakeGridNodeWithChildren(Specs));
		TestEqual(TEXT("Two clean rows produce four slot assignments"), Slots.Num(), 4);
		if (Slots.Num() == 4)
		{
			TestTrue(TEXT("Same-baseline items share a row"),
				Slots[0].X == 0 && Slots[1].X == 0 && Slots[2].X == 1 && Slots[3].X == 1);
			TestTrue(TEXT("Column order follows center X within a row"),
				Slots[0].Y == 0 && Slots[1].Y == 1 && Slots[2].Y == 0 && Slots[3].Y == 1);
		}
	}

	// Case 2: heterogeneous heights (10px and 90px). AvgHeight = 50, threshold 25:
	// centers 5 and 85 are 80 apart -> separate rows despite overlapping bands.
	{
		const TArray<FAutoGridChildSpec> Specs = {
			{   0.0f, 0.0f, 80.0f, 10.0f },
			{ 100.0f, 40.0f, 80.0f, 90.0f },
		};
		const TArray<FIntPoint> Slots =
			BuildBlueprintAndReadGridSlots(*this, MakeGridNodeWithChildren(Specs));
		TestEqual(TEXT("Heterogeneous heights produce two slot assignments"), Slots.Num(), 2);
		if (Slots.Num() == 2)
		{
			TestTrue(TEXT("Centers beyond half-average-height land on separate rows"),
				Slots[0].X != Slots[1].X);
		}
	}

	// Case 3: extreme vertical gap far exceeding any threshold stays two rows
	// (guards against regressions toward single-row collapse).
	{
		const TArray<FAutoGridChildSpec> Specs = {
			{ 0.0f,   0.0f, 80.0f, 40.0f },
			{ 0.0f, 500.0f, 80.0f, 40.0f },
		};
		const TArray<FIntPoint> Slots =
			BuildBlueprintAndReadGridSlots(*this, MakeGridNodeWithChildren(Specs));
		TestEqual(TEXT("Sparse grid produces two assignments"), Slots.Num(), 2);
		if (Slots.Num() == 2)
		{
			TestTrue(TEXT("Extreme gap separates rows"), Slots[0].X != Slots[1].X);
		}
	}

	return true;
}

#endif
