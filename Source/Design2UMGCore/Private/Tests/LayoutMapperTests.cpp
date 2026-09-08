// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "LayoutMapper.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

namespace
{
	TSharedPtr<FDesignNode> MakeNode(EDesignSizeMode WidthMode, float WidthVal, EDesignSizeMode HeightMode, float HeightVal, float X = 0.f, float Y = 0.f)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Transform.Width = { WidthMode, WidthVal };
		Node->Transform.Height = { HeightMode, HeightVal };
		Node->Transform.X = X;
		Node->Transform.Y = Y;
		return Node;
	}
}

// ---------------------------------------------------------------------------
// HBox slot: SizeRule / HAlign
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutMapperHBoxSlotTest,
	"Design2UMG.Unit.LayoutMapper.HBoxSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLayoutMapperHBoxSlotTest::RunTest(const FString&)
{
	// Auto-width child in HBox -> SizeRule=Auto
	{
		FUMGWidgetNode Widget;
		TSharedPtr<FDesignNode> Node = MakeNode(EDesignSizeMode::Fixed, 100.f, EDesignSizeMode::Fixed, 50.f);
		LayoutMapper::ApplySlotProperties(Widget, Node, EDesignLayout::Horizontal);
		TestTrue(TEXT("HBox fixed width -> Auto"), Widget.Slot.SizeRule == EUMGSlotSizeRule::Auto);
	}

	// Fill-width child in HBox -> SizeRule=Fill
	{
		FUMGWidgetNode Widget;
		TSharedPtr<FDesignNode> Node = MakeNode(EDesignSizeMode::FillContainer, 0.f, EDesignSizeMode::Fixed, 50.f);
		Node->LayoutData.LayoutGrow = 1.0f;
		LayoutMapper::ApplySlotProperties(Widget, Node, EDesignLayout::Horizontal);
		TestTrue(TEXT("HBox fill width -> Fill"), Widget.Slot.SizeRule == EUMGSlotSizeRule::Fill);
	}

	// Fill-height child in HBox -> VerticalAlignment=Fill
	{
		FUMGWidgetNode Widget;
		TSharedPtr<FDesignNode> Node = MakeNode(EDesignSizeMode::Fixed, 100.f, EDesignSizeMode::FillContainer, 0.f);
		LayoutMapper::ApplySlotProperties(Widget, Node, EDesignLayout::Horizontal);
		TestTrue(TEXT("HBox fill height -> VAlign Fill"), Widget.Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill);
	}

	return true;
}

// ---------------------------------------------------------------------------
// VBox slot: SizeRule / VAlign
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutMapperVBoxSlotTest,
	"Design2UMG.Unit.LayoutMapper.VBoxSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLayoutMapperVBoxSlotTest::RunTest(const FString&)
{
	// Fixed-height child in VBox -> SizeRule=Auto
	{
		FUMGWidgetNode Widget;
		TSharedPtr<FDesignNode> Node = MakeNode(EDesignSizeMode::Fixed, 100.f, EDesignSizeMode::Fixed, 50.f);
		LayoutMapper::ApplySlotProperties(Widget, Node, EDesignLayout::Vertical);
		TestTrue(TEXT("VBox fixed height -> Auto"), Widget.Slot.SizeRule == EUMGSlotSizeRule::Auto);
	}

	// Fill-height child in VBox -> SizeRule=Fill
	{
		FUMGWidgetNode Widget;
		TSharedPtr<FDesignNode> Node = MakeNode(EDesignSizeMode::Fixed, 100.f, EDesignSizeMode::FillContainer, 0.f);
		Node->LayoutData.LayoutGrow = 1.0f;
		LayoutMapper::ApplySlotProperties(Widget, Node, EDesignLayout::Vertical);
		TestTrue(TEXT("VBox fill height -> Fill"), Widget.Slot.SizeRule == EUMGSlotSizeRule::Fill);
	}

	// Fill-width child in VBox -> HAlign=Fill
	{
		FUMGWidgetNode Widget;
		TSharedPtr<FDesignNode> Node = MakeNode(EDesignSizeMode::FillContainer, 0.f, EDesignSizeMode::Fixed, 50.f);
		LayoutMapper::ApplySlotProperties(Widget, Node, EDesignLayout::Vertical);
		TestTrue(TEXT("VBox fill width -> HAlign Fill"), Widget.Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Canvas slot: Anchors / Offsets
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutMapperCanvasSlotTest,
	"Design2UMG.Unit.LayoutMapper.CanvasSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLayoutMapperCanvasSlotTest::RunTest(const FString&)
{
	// Fixed child in Canvas -> offsets match x/y/w/h
	{
		FUMGWidgetNode Widget;
		TSharedPtr<FDesignNode> Node = MakeNode(EDesignSizeMode::Fixed, 120.f, EDesignSizeMode::Fixed, 80.f, 30.f, 20.f);
		LayoutMapper::ApplySlotProperties(Widget, Node, EDesignLayout::None);
		TestEqual(TEXT("Canvas Offsets.Left"), Widget.Slot.Offsets.Left, 30.f);
		TestEqual(TEXT("Canvas Offsets.Top"), Widget.Slot.Offsets.Top, 20.f);
		TestEqual(TEXT("Canvas Offsets.Right"), Widget.Slot.Offsets.Right, 120.f);
		TestEqual(TEXT("Canvas Offsets.Bottom"), Widget.Slot.Offsets.Bottom, 80.f);
	}

	// Stretch-width child -> Anchors.Max.X = 1
	{
		FUMGWidgetNode Widget;
		TSharedPtr<FDesignNode> Node = MakeNode(EDesignSizeMode::FillContainer, 0.f, EDesignSizeMode::Fixed, 80.f, 0.f, 0.f);
		LayoutMapper::ApplySlotProperties(Widget, Node, EDesignLayout::None);
		TestEqual(TEXT("Stretch width Anchors.Max.X"), Widget.Slot.Anchors.Max.X, 1.f);
		TestEqual(TEXT("Stretch width Anchors.Max.Y"), Widget.Slot.Anchors.Max.Y, 0.f);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Padding handling
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutMapperPaddingTest,
	"Design2UMG.Unit.LayoutMapper.Padding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLayoutMapperPaddingTest::RunTest(const FString&)
{
	// ApplyPadding sets WidgetStyle.Padding
	{
		FUMGWidgetNode Widget;
		FDesignPadding Pad;
		Pad.Top = 4.f; Pad.Right = 8.f; Pad.Bottom = 4.f; Pad.Left = 8.f;
		LayoutMapper::ApplyPadding(Widget, Pad);
		TestEqual(TEXT("Padding.Left"), Widget.WidgetStyle.Padding.GetValue().Left, 8.f);
		TestEqual(TEXT("Padding.Top"), Widget.WidgetStyle.Padding.GetValue().Top, 4.f);
		TestEqual(TEXT("Padding.Right"), Widget.WidgetStyle.Padding.GetValue().Right, 8.f);
		TestEqual(TEXT("Padding.Bottom"), Widget.WidgetStyle.Padding.GetValue().Bottom, 4.f);
	}

	// HasPadding: node with explicit padding returns true
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		FDesignPadding Pad;
		Pad.Top = 10.f;
		Node->LayoutData.SetPadding(Pad);
		TestTrue(TEXT("HasPadding true"), LayoutMapper::HasPadding(Node));
	}

	// HasPadding: node with zero padding returns false
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		TestFalse(TEXT("HasPadding false when no padding"), LayoutMapper::HasPadding(Node));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutMapperSourceTextBoxUsesStrictSizeBoxTest,
	"Design2UMG.Unit.LayoutMapper.SourceTextBoxUsesStrictSizeBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLayoutMapperSourceTextBoxUsesStrictSizeBoxTest::RunTest(const FString&)
{
	FUMGWidgetNode TextWidget;
	TextWidget.Id = TEXT("text");
	TextWidget.Name = TEXT("Text");
	TextWidget.Type = TEXT("TextBlock");
	TextWidget.Bounds = { 0.0f, 0.0f, 117.0f, 40.0f };

	TSharedPtr<FDesignNode> Node = MakeNode(EDesignSizeMode::Fixed, 188.16f, EDesignSizeMode::Fixed, 42.0f);
	Node->Type = EDesignNodeType::Text;
	Node->Text.Content = TEXT("Score 0000");
	Node->Text.TextGrowth = EDesignTextGrowth::Auto;
	Node->Transform.bSourceHasWidth = true;
	Node->Transform.bSourceHasHeight = true;

	const FUMGWidgetNode Wrapper = LayoutMapper::WrapWithSizeBox(MoveTemp(TextWidget), Node, EDesignLayout::Horizontal);

	TestEqual(TEXT("Source textbox is wrapped in a SizeBox"), Wrapper.Type, FString(TEXT("SizeBox")));
	TestTrue(TEXT("Wrapper width uses source textbox width"),
		FMath::IsNearlyEqual(Wrapper.Bounds.W, 188.16f, 0.001f));
	TestTrue(TEXT("Wrapper height uses source textbox height"),
		FMath::IsNearlyEqual(Wrapper.Bounds.H, 42.0f, 0.001f));
	TestTrue(TEXT("Wrapper WidthOverride uses source textbox width"),
		FMath::IsNearlyEqual(Wrapper.WidgetStyle.WidthOverride.Get(0.0f), 188.16f, 0.001f));
	TestTrue(TEXT("Wrapper HeightOverride uses source textbox height"),
		FMath::IsNearlyEqual(Wrapper.WidgetStyle.HeightOverride.Get(0.0f), 42.0f, 0.001f));
	TestEqual(TEXT("Wrapped child keeps standard TextBlock type"), Wrapper.Children[0].Type, FString(TEXT("TextBlock")));
	TestEqual(TEXT("Wrapped child keeps id"), Wrapper.Children[0].Id, FString(TEXT("text")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
