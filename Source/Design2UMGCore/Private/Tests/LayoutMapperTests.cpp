// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "LayoutMapper.h"
#include "DesignSchema.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"
#include "Design2UMGConverter.h"
#include "PenParser.h"
#include "UMGWidgetTypes.h"
#include "Design2UMGConverterFlowChildPreResolution.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutCarrierComponentIdentityTest,
	"Design2UMG.Unit.LayoutMapper.ComponentIdentityOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLayoutCarrierComponentIdentityTest::RunTest(const FString&)
{
	for (const bool bFillChild : {false, true})
	{
		FUMGWidgetNode Instance;
		Instance.Id = Instance.Name = TEXT("instance");
		Instance.Type = TEXT("HorizontalBox");
		Instance.Bounds.W = 100;
		Instance.Bounds.H = 40;
		Instance.MutableSource().SourceNodeId = TEXT("instance");
		Instance.MutableSource().SourceNodeType = TEXT("INSTANCE");
		Instance.MutableSource().MainComponentKey = TEXT("component-key");
		FUMGWidgetNode Child;
		Child.Type = TEXT("TextBlock");
		Child.Slot.SizeRule = bFillChild ? EUMGSlotSizeRule::Fill : EUMGSlotSizeRule::Auto;
		Child.Slot.FillWeight = 1.f;
		Instance.Children.Add(MoveTemp(Child));
		const auto Source = MakeNode(EDesignSizeMode::Fixed, 100, EDesignSizeMode::Fixed, 40);
		const FUMGWidgetNode Carrier = LayoutMapper::WrapWithSizeBox(MoveTemp(Instance), Source, EDesignLayout::Horizontal);
		TestFalse(TEXT("Added layout carrier does not duplicate component source identity"), Carrier.HasSourceData());
		if (!TestEqual(TEXT("Carrier contains the component boundary"), Carrier.Children.Num(), 1)) continue;
		TestEqual(TEXT("Component boundary is not folded into its child"), Carrier.Children[0].Id, FString(TEXT("instance")));
		TestTrue(TEXT("Component boundary retains source metadata"), Carrier.Children[0].HasSourceData());
		if (Carrier.Children[0].HasSourceData())
		{
			TestEqual(TEXT("Component boundary retains its reusable identity"), Carrier.Children[0].Source().MainComponentKey, FString(TEXT("component-key")));
		}
	}
	return true;
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
	for (const bool bFitWidth : {false, true})
	for (const float FitValue : {0.f, 32.f})
	{
		FUMGWidgetNode Widget;
		const auto Node = MakeNode(
			bFitWidth ? EDesignSizeMode::FitContent : EDesignSizeMode::Fixed, bFitWidth ? FitValue : 120.f,
			bFitWidth ? EDesignSizeMode::Fixed : EDesignSizeMode::FitContent, bFitWidth ? 80.f : FitValue);
		LayoutMapper::ApplySlotProperties(Widget, Node, EDesignLayout::None);
		TestEqual(TEXT("Only an unresolved fit axis uses desired size"), Widget.Slot.bAutoSize, FitValue == 0.f);
		TestEqual(TEXT("Mixed-size canvas preserves width"), Widget.Slot.Offsets.Right, Node->Transform.Width.Value);
		TestEqual(TEXT("Mixed-size canvas preserves height"), Widget.Slot.Offsets.Bottom, Node->Transform.Height.Value);
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutMapperNativeLinearPaddingTest,
	"Design2UMG.Unit.LayoutMapper.NativeLinearPadding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutMapperNativeLinearPaddingTest::RunTest(const FString&)
{
	FDesignPadding Padding;
	Padding.Left = 11; Padding.Top = 7; Padding.Right = 13; Padding.Bottom = 17;
	for (const bool bVertical : {false, true})
	{
		FUMGWidgetNode Box;
		Box.Type = bVertical ? UMGWidgetTypes::VerticalBox : UMGWidgetTypes::HorizontalBox;
		Box.Children.SetNum(2);
		LayoutMapper::ApplyGap(Box.Children, 9, bVertical ? EDesignLayout::Vertical : EDesignLayout::Horizontal);
		LayoutMapper::ApplyPadding(Box, Padding);
		TestEqual(TEXT("First slot carries leading and cross-axis insets"), Box.Children[0].Slot.Padding,
			bVertical ? FMargin(11, 7, 13, 0) : FMargin(11, 7, 0, 17));
		TestEqual(TEXT("Last slot retains gap and carries trailing inset"), Box.Children[1].Slot.Padding,
			bVertical ? FMargin(11, 9, 13, 17) : FMargin(9, 7, 13, 17));
		FUMGWidgetNode Single;
		Single.Type = Box.Type;
		Single.Children.SetNum(1);
		LayoutMapper::ApplyPadding(Single, Padding);
		TestEqual(TEXT("Single slot gets all four insets exactly once"), Single.Children[0].Slot.Padding, FMargin(11, 7, 13, 17));
	}
	FUMGWidgetNode Border;
	Border.Type = UMGWidgetTypes::Border;
	LayoutMapper::ApplyPadding(Border, Padding);
	TestEqual(TEXT("Border retains native padding property"), Border.WidgetStyle.Padding.Get(FMargin()), FMargin(11, 7, 13, 17));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutMapperPositionedFlowPaddingTest,
	"Design2UMG.Unit.LayoutMapper.PositionedFlowPadding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutMapperPositionedFlowPaddingTest::RunTest(const FString&)
{
	FDesignDocument Document;
	if (!TestTrue(TEXT("Positioned flow parses"), FPenParser::ParseDocument(TEXT(R"JSON(
		{"version":"2.14","children":[{"id":"container","type":"frame","width":200,"height":150,
		"layout":"vertical","padding":12,"gap":9,"children":[
		{"id":"first","type":"rectangle","x":12,"y":12,"width":30,"height":20,"fill":"#FFFFFF"},
		{"id":"second","type":"rectangle","x":12,"y":41,"width":30,"height":20,"fill":"#FFFFFF"}]}]}
	)JSON"), Document))) return false;
	TestFalse(TEXT("Parser preserves baked-coordinate ownership, without a second wrapper"),
		Document.Children[0]->LayoutData.PaddingRequiresRuntimeWrap.Get(true));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode& Box = Data.RootWidget;
	TestEqual(TEXT("Flow remains native without an added Border"), Box.Type, UMGWidgetTypes::VerticalBox);
	if (!TestEqual(TEXT("Both flow slots survive conversion"), Box.Children.Num(), 2)) return false;
	TestEqual(TEXT("Discarded flow coordinates are replaced by the declared inset"), Box.Children[0].Slot.Padding, FMargin(12, 12, 12, 0));
	TestEqual(TEXT("Gap and trailing inset are each applied once"), Box.Children[1].Slot.Padding, FMargin(12, 9, 12, 12));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutMapperInheritedFillTest,
	"Design2UMG.Unit.LayoutMapper.InheritedFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutMapperInheritedFillTest::RunTest(const FString&)
{
	for (const bool bVertical : {false, true})
	{
		FDesignDocument Document;
		const FString Json = FString::Printf(TEXT(R"JSON({"version":"2.14","children":[
			{"id":"cell","type":"frame","reusable":true,"width":"fill_container(90)","height":"fill_container(80)","fill":"#FFFFFF"},
			{"id":"parent","type":"frame","layout":"%s","width":200,"height":120,"children":[
				{"id":"a","type":"ref","ref":"cell"},{"id":"b","type":"ref","ref":"cell"}]}]})JSON"),
			bVertical ? TEXT("vertical") : TEXT("horizontal"));
		if (!TestTrue(TEXT("Inherited fill fixture parses"), FPenParser::ParseDocument(Json, Document))) return false;
		const auto Parent = Document.Children[1];
		FUMGWidgetNode Panel;
		Panel.Bounds.W = 200;
		Panel.Bounds.H = 120;
		const TMap<FString, FString> Theme;
		Design2UMGConverterInternal::FFlowChildPreResolutionResolver Resolver(Document, Theme);
		Resolver.PreResolveFlowChildren(Parent, Panel, Parent->Children);
		for (const auto& Child : Parent->Children)
		{
			const auto Expanded = Resolver.ResolveExpandedChild(Child, true);
			if (!TestTrue(TEXT("Measured instance expands"), Expanded.IsValid())) return false;
			TestTrue(TEXT("Inherited main and cross fill modes survive measurement"),
				Expanded->Transform.Width.IsFill() && Expanded->Transform.Height.IsFill());
			TestEqual(TEXT("Resolved width uses available parent space"), Expanded->Transform.Width.Value, bVertical ? 200.f : 100.f);
			TestEqual(TEXT("Resolved height uses available parent space"), Expanded->Transform.Height.Value, bVertical ? 60.f : 120.f);
			FUMGWidgetNode Widget;
			LayoutMapper::ApplySlotProperties(Widget, Expanded, Parent->LayoutData.Layout);
			TestEqual(TEXT("Runtime slot still fills instead of taking its fallback width"), Widget.Slot.SizeRule, EUMGSlotSizeRule::Fill);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
