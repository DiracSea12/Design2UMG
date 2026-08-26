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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTreeChildBuilderAutoGridLocalRowGroupingTest,
	"Design2UMG.WidgetTreeChildBuilder.AutoGridLocalRowGrouping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetTreeChildBuilderAutoGridLocalRowGroupingTest::RunTest(const FString& Parameters)
{
	// Case 1: homogeneous rows retain the existing clean slot assignment.
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

	// Case 2: heterogeneous heights with aligned tops remain in their authored rows.
	{
		const TArray<FAutoGridChildSpec> Specs = {
			{   0.0f,   0.0f, 80.0f, 20.0f },
			{ 100.0f,   0.0f, 80.0f, 80.0f },
			{   0.0f, 120.0f, 80.0f, 20.0f },
			{ 100.0f, 120.0f, 80.0f, 80.0f },
		};
		const TArray<FIntPoint> Slots =
			BuildBlueprintAndReadGridSlots(*this, MakeGridNodeWithChildren(Specs));
		TestEqual(TEXT("Heterogeneous rows produce four slot assignments"), Slots.Num(), 4);
		if (Slots.Num() == 4)
		{
			TestTrue(TEXT("Aligned heterogeneous children share their local row"),
				Slots[0].X == 0 && Slots[1].X == 0 && Slots[2].X == 1 && Slots[3].X == 1);
		}
	}

	// Case 3: compare each item with a stable row anchor so a three-item center
	// chain does not collapse its distant third item into the first row.
	{
		const TArray<FAutoGridChildSpec> Specs = {
			{   0.0f, 0.0f, 80.0f, 100.0f },
			{ 100.0f, 40.0f, 80.0f, 100.0f },
			{ 200.0f, 80.0f, 80.0f, 100.0f },
		};
		const TArray<FIntPoint> Slots =
			BuildBlueprintAndReadGridSlots(*this, MakeGridNodeWithChildren(Specs));
		TestEqual(TEXT("Chained centers produce three slot assignments"), Slots.Num(), 3);
		if (Slots.Num() == 3)
		{
			TestTrue(TEXT("Chained centers are split against the row anchor"),
				Slots[0].X == 0 && Slots[1].X == 0 && Slots[2].X == 1);
		}
	}

	// Case 4: a distant tall outlier must not widen the local threshold enough
	// to merge two short rows.
	{
		const TArray<FAutoGridChildSpec> Specs = {
			{   0.0f,    0.0f, 80.0f, 40.0f },
			{   0.0f,   60.0f, 80.0f, 40.0f },
			{ 200.0f, 1000.0f, 80.0f, 400.0f },
		};
		const TArray<FIntPoint> Slots =
			BuildBlueprintAndReadGridSlots(*this, MakeGridNodeWithChildren(Specs));
		TestEqual(TEXT("Outlier case produces three assignments"), Slots.Num(), 3);
		if (Slots.Num() == 3)
		{
			TestTrue(TEXT("Short rows remain separate despite the tall outlier"),
				Slots[0].X != Slots[1].X);
		}
	}

	// Case 5: extreme vertical gap far exceeding any local threshold stays two
	// rows (guards against single-row collapse).
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
