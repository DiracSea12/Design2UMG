// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Layout/ArrangedChildren.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SConstraintCanvas.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSizeBoxNestedFillEquivalenceTest,
    "Design2UMG.Structure.NestedFixedSizeBoxFillEquivalence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSizeBoxNestedFillEquivalenceTest::RunTest(const FString&)
{
    // A fully constrained outer size box masks inner desired-size constraints.
    // This says nothing about source identity, clipping, transforms or bindings.
    for (const FVector2D Allocation : {FVector2D(40, 30), FVector2D(100, 80), FVector2D(240, 180)})
    {
        for (const FVector2D InnerSize : {FVector2D(20, 10), FVector2D(300, 200)})
        {
            const auto Leaf = SNew(SSpacer).Size(FVector2D(35, 25));
            const auto Inner = SNew(SBox).WidthOverride(InnerSize.X).HeightOverride(InnerSize.Y)[Leaf];
            const auto Outer = SNew(SBox).WidthOverride(100).HeightOverride(80)[Inner];
            const auto MergedLeaf = SNew(SSpacer).Size(FVector2D(35, 25));
            const auto Merged = SNew(SBox).WidthOverride(100).HeightOverride(80)[MergedLeaf];
            Outer->SlatePrepass();
            Merged->SlatePrepass();
            TestEqual(TEXT("Outer desired size ignores inner override"), Outer->GetDesiredSize(), Merged->GetDesiredSize());
            const FGeometry Root = FGeometry::MakeRoot(Allocation, FSlateLayoutTransform());
            FArrangedChildren OuterChildren(EVisibility::All);
            Outer->ArrangeChildren(Root, OuterChildren);
            if (!TestEqual(TEXT("Original outer has one child"), OuterChildren.Num(), 1)) return false;
            FArrangedChildren InnerChildren(EVisibility::All);
            Inner->ArrangeChildren(OuterChildren[0].Geometry, InnerChildren);
            FArrangedChildren MergedChildren(EVisibility::All);
            Merged->ArrangeChildren(Root, MergedChildren);
            if (!TestEqual(TEXT("Original inner has one child"), InnerChildren.Num(), 1)
                || !TestEqual(TEXT("Merged has one child"), MergedChildren.Num(), 1)) return false;
            TestEqual(TEXT("Leaf allotted size unchanged even below desired size"),
                InnerChildren[0].Geometry.GetLocalSize(), MergedChildren[0].Geometry.GetLocalSize());
            TestEqual(TEXT("Leaf position unchanged"),
                InnerChildren[0].Geometry.GetAbsolutePosition(), MergedChildren[0].Geometry.GetAbsolutePosition());
        }
    }
    const auto CollapsedLeaf = SNew(SSpacer).Size(FVector2D(35, 25)).Visibility(EVisibility::Collapsed);
    const auto Nested = SNew(SBox).WidthOverride(100).HeightOverride(80)
        [SNew(SBox).WidthOverride(300).HeightOverride(200)[CollapsedLeaf]];
    const auto Direct = SNew(SBox).WidthOverride(100).HeightOverride(80)
        [SNew(SSpacer).Size(FVector2D(35, 25)).Visibility(EVisibility::Collapsed)];
    Nested->SlatePrepass();
    Direct->SlatePrepass();
    TestEqual(TEXT("Nested box retains fixed desired size around collapsed descendant"), FVector2D(Nested->GetDesiredSize()), FVector2D(100, 80));
    TestEqual(TEXT("Direct collapsed child zeros outer desired size despite overrides"), FVector2D(Direct->GetDesiredSize()), FVector2D::ZeroVector);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSizeBoxCanvasBoundaryEquivalenceTest,
    "Design2UMG.Structure.FixedCanvasSizeCarrierEquivalence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSizeBoxCanvasBoundaryEquivalenceTest::RunTest(const FString&)
{
    int32 Comparisons = 0;
    for (const bool bCanvasRoot : {false, true})
    for (const bool bStretch : {false, true})
    for (const FVector2D Allocation : {FVector2D(40, 30), FVector2D(100, 80), FVector2D(240, 180)})
    {
        TArray<TSharedRef<SWidget>> Roots;
        TArray<TSharedRef<SSpacer>> Leaves;
        for (const bool bKeepCarrier : {true, false})
        {
            const auto Leaf = SNew(SSpacer).Size(FVector2D(35, 25));
            Leaves.Add(Leaf);
            TSharedRef<SWidget> Content = Leaf;
            if (bKeepCarrier) Content = SNew(SBox).WidthOverride(70).HeightOverride(50)[Leaf];
            const auto Canvas = SNew(SConstraintCanvas)
                + SConstraintCanvas::Slot().Anchors(bStretch ? FAnchors(0, 0, 1, 1) : FAnchors(0, 0))
                .Alignment(FVector2D::ZeroVector).AutoSize(false)
                .Offset(bStretch ? FMargin(8, 9, 10, 11) : FMargin(8, 9, 70, 50))[Content];
            if (bCanvasRoot)
            {
                Roots.Add(SNew(SConstraintCanvas) + SConstraintCanvas::Slot()
                    .Alignment(FVector2D::ZeroVector).AutoSize(false).Offset(FMargin(3, 4, 100, 80))[Canvas]);
            }
            else Roots.Add(SNew(SBox).WidthOverride(100).HeightOverride(80)[Canvas]);
        }
        // Exercise changes on the same objects, not just initial visibility.
        for (const EVisibility Visibility : {EVisibility::Visible, EVisibility::Hidden, EVisibility::Collapsed, EVisibility::Visible})
        {
            TArray<FGeometry> LeafGeometry;
            for (int32 Index = 0; Index < Roots.Num(); ++Index)
            {
                Leaves[Index]->SetVisibility(Visibility);
                Roots[Index]->SlatePrepass();
                TSharedRef<SWidget> Current = Roots[Index];
                FGeometry Geometry = FGeometry::MakeRoot(Allocation, FSlateLayoutTransform());
                while (&Current.Get() != &Leaves[Index].Get())
                {
                    FArrangedChildren Children(EVisibility::All);
                    Current->ArrangeChildren(Geometry, Children);
                    if (!TestEqual(TEXT("Probe branch retains one arranged child"), Children.Num(), 1)) return false;
                    Current = Children[0].Widget;
                    Geometry = Children[0].Geometry;
                }
                LeafGeometry.Add(Geometry);
            }
            TestEqual(TEXT("Parent desired size unchanged across visibility transitions"), Roots[0]->GetDesiredSize(), Roots[1]->GetDesiredSize());
            TestEqual(TEXT("Leaf allocation unchanged under fixed parent boundary"), LeafGeometry[0].GetLocalSize(), LeafGeometry[1].GetLocalSize());
            TestEqual(TEXT("Leaf location unchanged under fixed parent boundary"), LeafGeometry[0].GetAbsolutePosition(), LeafGeometry[1].GetAbsolutePosition());
            ++Comparisons;
        }
    }
    AddInfo(FString::Printf(TEXT("Parent-boundary geometry comparisons=%d"), Comparisons));
    return true;
}
#endif
