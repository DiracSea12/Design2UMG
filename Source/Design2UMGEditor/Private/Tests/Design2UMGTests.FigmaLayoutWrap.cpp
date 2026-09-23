// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Figma layout coverage: GRID/WRAP metadata, wrap gap/fill semantics, and flow/instance padding border-wrapper rules.

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;
using namespace Design2UMGCoreTests;


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaGridWrapMetadataTest,
	"Design2UMG.FigmaParser.GridWrapMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaGridWrapMetadataTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 900, 600, FString::Printf(
		TEXT("%s,%s"),
		*MakeContainerNode(TEXT("grid1"), TEXT("GridContainer"), TEXT("FRAME"), 20, 20, 300, 200, FString(),
			TEXT(",\"layoutMode\":\"GRID\",\"itemSpacing\":12,\"paddingLeft\":10,\"paddingRight\":20,\"paddingTop\":30,"
				 "\"paddingBottom\":40,\"counterAxisSpacing\":6,\"counterAxisAlignContent\":\"SPACE_BETWEEN\","
				 "\"layoutPositioning\":\"ABSOLUTE\",\"layoutAlign\":\"STRETCH\",\"primaryAxisSizingMode\":\"AUTO\","
				 "\"counterAxisSizingMode\":\"FIXED\",\"minWidth\":100,\"maxWidth\":500,\"minHeight\":80,\"maxHeight\":400,"
				 "\"gridRowCount\":2,\"gridColumnCount\":3,\"gridRowGap\":14,\"gridColumnGap\":16,"
				 "\"gridRowsSizing\":\"AUTO\",\"gridColumnsSizing\":\"FIXED\",\"gridChildHorizontalAlign\":\"CENTER\","
				 "\"gridChildVerticalAlign\":\"MIN\",\"gridRowSpan\":2,\"gridColumnSpan\":3,"
				 "\"gridColumnAnchorIndex\":1,\"gridRowAnchorIndex\":0,\"itemReverseZIndex\":true,"
				 "\"strokesIncludedInLayout\":true,"
				 "\"boundVariables\":{\"itemSpacing\":{\"type\":\"VARIABLE_ALIAS\",\"id\":\"VariableID:1:2\"}}")),
		*MakeContainerNode(TEXT("wrap1"), TEXT("WrapContainer"), TEXT("FRAME"), 20, 260, 400, 200, FString(),
				TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutWrap\":\"WRAP\",\"itemSpacing\":8,"
					 "\"paddingLeft\":5,\"paddingRight\":6,\"paddingTop\":7,\"paddingBottom\":9")))));
	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() < 2)
		return false;
	const TSharedPtr<FDesignNode>& GridNode = Doc.Children[0]->Children[0];
	const TSharedPtr<FDesignNode>& WrapNode = Doc.Children[0]->Children[1];
	TestEqual(TEXT("GRID layout parsed"), GridNode->LayoutData.Layout, EDesignLayout::Grid);
	TestEqual(TEXT("WRAP layout parsed"), WrapNode->LayoutData.Layout, EDesignLayout::Wrap);
	TestTrue(TEXT("GRID gap preserved"), FMath::IsNearlyEqual(GridNode->LayoutData.ExplicitGap.Get(0.0f), 12.0f, 0.001f));
	TestTrue(TEXT("WRAP gap preserved"), FMath::IsNearlyEqual(WrapNode->LayoutData.ExplicitGap.Get(0.0f), 8.0f, 0.001f));
	TestTrue(TEXT("GRID padding left preserved"), FMath::IsNearlyEqual(GridNode->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Left, 10.0f, 0.001f));
	TestTrue(TEXT("WRAP padding top preserved"), FMath::IsNearlyEqual(WrapNode->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Top, 7.0f, 0.001f));
	const FString* CounterAxisAlignContent = GridNode->Metadata.Find(TEXT("counterAxisAlignContent"));
	TestTrue(TEXT("counterAxisAlignContent preserved"), CounterAxisAlignContent && *CounterAxisAlignContent == TEXT("SPACE_BETWEEN"));
	const FString* LayoutPositioning = GridNode->Metadata.Find(TEXT("layoutPositioning"));
	TestTrue(TEXT("layoutPositioning preserved"), LayoutPositioning && *LayoutPositioning == TEXT("ABSOLUTE"));
	TestTrue(TEXT("layoutPositioning semantic flag"), GridNode->LayoutData.bAbsoluteLayoutPositioning);
	const FString* ItemReverseZIndex = GridNode->Metadata.Find(TEXT("itemReverseZIndex"));
	TestTrue(TEXT("itemReverseZIndex preserved"), ItemReverseZIndex && *ItemReverseZIndex == TEXT("true"));
	const FString* StrokesIncludedInLayout = GridNode->Metadata.Find(TEXT("strokesIncludedInLayout"));
	TestTrue(TEXT("strokesIncludedInLayout preserved"), StrokesIncludedInLayout && *StrokesIncludedInLayout == TEXT("true"));
	TestTrue(TEXT("boundVariables preserved"), GridNode->Metadata.Contains(TEXT("boundVariables.itemSpacing")));
	TestTrue(TEXT("counterAxisSpacing preserved"), GridNode->Metadata.Contains(TEXT("counterAxisSpacing")));
	TestTrue(TEXT("layoutAlign preserved"), GridNode->Metadata.Contains(TEXT("layoutAlign")));
	TestTrue(TEXT("primaryAxisSizingMode preserved"), GridNode->Metadata.Contains(TEXT("primaryAxisSizingMode")));
	TestTrue(TEXT("counterAxisSizingMode preserved"), GridNode->Metadata.Contains(TEXT("counterAxisSizingMode")));
	TestTrue(TEXT("min/max size preserved"), GridNode->Metadata.Contains(TEXT("minWidth")) && GridNode->Metadata.Contains(TEXT("maxHeight")));
	TestTrue(TEXT("grid count/gap preserved"), GridNode->Metadata.Contains(TEXT("gridRowCount")) && GridNode->Metadata.Contains(TEXT("gridColumnGap")));
	TestTrue(TEXT("grid sizing preserved"), GridNode->Metadata.Contains(TEXT("gridRowsSizing")) && GridNode->Metadata.Contains(TEXT("gridColumnsSizing")));
	TestTrue(TEXT("grid child align preserved"), GridNode->Metadata.Contains(TEXT("gridChildHorizontalAlign")) && GridNode->Metadata.Contains(TEXT("gridChildVerticalAlign")));
	TestTrue(TEXT("grid child span/anchor preserved"), GridNode->Metadata.Contains(TEXT("gridRowSpan")) && GridNode->Metadata.Contains(TEXT("gridColumnAnchorIndex")));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* GridWidget = FindWidgetById(Data.RootWidget, TEXT("grid1"));
	const FUMGWidgetNode* WrapWidget = FindWidgetById(Data.RootWidget, TEXT("wrap1"));
	TestTrue(TEXT("Grid widget exists"), GridWidget != nullptr);
	TestTrue(TEXT("Wrap widget exists"), WrapWidget != nullptr);
	if (!GridWidget || !WrapWidget)
		return false;
	TestEqual(TEXT("Grid widget type"), GridWidget->Type, TEXT("UniformGridPanel"));
	TestEqual(TEXT("Wrap widget type"), WrapWidget->Type, TEXT("WrapBox"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWrapGapUsesInnerSlotPaddingOnlyTest,
	"Design2UMG.LayoutData.Layout.WrapGapUsesInnerSlotPaddingOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWrapGapUsesInnerSlotPaddingOnlyTest::RunTest(const FString& Parameters)
{
	// Zero main-axis spacing allows positive cross-axis spacing; negative main-axis spacing
	// suppresses both. Compare numeric padding: unset and explicit zero have the same behavior.
	struct FWrapGapCase
	{
		const TCHAR* Label;
		float MainGap;
		float CrossGap;
		bool bCrossSet;    // false => counterAxisSpacing omitted (falls back to the main axis)
		float ExpectedX;
		float ExpectedY;
	};
	const TArray<FWrapGapCase> Cases = {
		{TEXT("(0,16)"),      0.0f, 16.0f, true,   0.0f, 16.0f},  // zero main axis + positive cross axis: the reopened boundary
		{TEXT("(16,0)"),     16.0f,  0.0f, true,  16.0f,  0.0f},  // non-zero main axis, zero cross axis
		{TEXT("(0,0)"),       0.0f,  0.0f, true,   0.0f,  0.0f},  // both zero -> numerically zero, presence not forced
		{TEXT("(24,16)"),    24.0f, 16.0f, true,  24.0f, 16.0f},  // original retained case
		{TEXT("(16,unset)"), 16.0f,  0.0f, false, 16.0f, 16.0f},  // cross axis unset -> falls back to the main axis
		{TEXT("(-8,0)"),     -8.0f,  0.0f, true,   0.0f,  0.0f},  // Negative main-axis spacing suppresses padding.
		{TEXT("(-8,16)"),    -8.0f, 16.0f, true,   0.0f,  0.0f},  // negative main axis + positive cross axis: still no write
		{TEXT("(-5e-9,16)"), -5.0e-9f, 16.0f, true, 0.0f, 0.0f},  // TINY negative main axis: still no write. Guards against a toleranced near-zero test silently reclassifying negative gaps as zero; only an EXACT 0 reopens the case.
	};

	bool bAllCasesPass = true;
	for (const FWrapGapCase& C : Cases)
	{
		// %.9g, not %.0f: the matrix includes a tiny negative main-axis gap (-5e-9) that
		// %.0f would round to "0" and thereby turn into the very case under test.
		const FString GapJson = C.bCrossSet
			? FString::Printf(TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutWrap\":\"WRAP\",\"itemSpacing\":%.9g,\"counterAxisSpacing\":%.9g"), C.MainGap, C.CrossGap)
			: FString::Printf(TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutWrap\":\"WRAP\",\"itemSpacing\":%.9g"), C.MainGap);

		const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 400, 300,
			MakeContainerNode(TEXT("wrap1"), TEXT("Wrap"), TEXT("FRAME"), 0, 0, 300, 200, FString::Printf(
				TEXT("%s,%s"),
				*MakeContainerNode(TEXT("c1"), TEXT("Card1"), TEXT("FRAME"), 0, 0, 100, 50, FString()),
				*MakeContainerNode(TEXT("c2"), TEXT("Card2"), TEXT("FRAME"), 100, 0, 100, 50, FString())),
				*GapJson)));

		FDesignDocument Doc;
		if (!TestTrue(FString::Printf(TEXT("Wrap gap parse succeeds %s"), C.Label), FFigmaParser::ParseDocument(Json, Doc)))
		{
			bAllCasesPass = false;
			continue;
		}
		FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
		const FUMGWidgetNode* WrapWidget = FindWidgetById(Data.RootWidget, TEXT("wrap1"));
		if (!TestTrue(FString::Printf(TEXT("Wrap widget exists %s"), C.Label), WrapWidget != nullptr) || (WrapWidget && WrapWidget->Children.Num() < 2))
		{
			bAllCasesPass = false;
			continue;
		}

		const FVector2D Actual = WrapWidget->WidgetStyle.InnerSlotPadding.Get(FVector2D::ZeroVector);
		if (!TestTrue(FString::Printf(TEXT("Wrap inner slot padding %s expected (%.9g,%.9g) actual (%.9g,%.9g)"),
				C.Label, C.ExpectedX, C.ExpectedY, Actual.X, Actual.Y),
				FMath::IsNearlyEqual(Actual.X, C.ExpectedX, 0.001f) && FMath::IsNearlyEqual(Actual.Y, C.ExpectedY, 0.001f)))
		{
			bAllCasesPass = false;
		}
		if (!TestTrue(FString::Printf(TEXT("Second child has no duplicated left gap %s"), C.Label),
				FMath::IsNearlyZero(WrapWidget->Children[1].Slot.Padding.Left)))
		{
			bAllCasesPass = false;
		}
		if (!TestTrue(FString::Printf(TEXT("Second child has no duplicated top gap %s"), C.Label),
				FMath::IsNearlyZero(WrapWidget->Children[1].Slot.Padding.Top)))
		{
			bAllCasesPass = false;
		}
	}
	return bAllCasesPass;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaWrapFillChildPreservesMeasuredSizeTest,
	"Design2UMG.FigmaParser.WrapFillChildPreservesMeasuredSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaWrapFillChildPreservesMeasuredSizeTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 400, 300,
		MakeContainerNode(TEXT("wrap1"), TEXT("Wrap"), TEXT("FRAME"), 0, 0, 300, 200, FString::Printf(
			TEXT("%s,%s"),
			*MakeContainerNode(TEXT("card1"), TEXT("Card1"), TEXT("FRAME"), 0, 0, 100, 60,
				MakeLeafNode(TEXT("img1"), TEXT("Image1"), TEXT("RECTANGLE"), 0, 0, 100, 40,
					FString::Printf(TEXT(",\"layoutSizingHorizontal\":\"FILL\",\"layoutSizingVertical\":\"FIXED\",\"fills\":[%s]"),
						*MakeSolidFillJson(1, 1, 1))),
				TEXT(",\"layoutMode\":\"VERTICAL\",\"layoutSizingHorizontal\":\"FILL\",\"layoutSizingVertical\":\"FIXED\","
					 "\"layoutGrow\":1")),
			*MakeContainerNode(TEXT("card2"), TEXT("Card2"), TEXT("FRAME"), 124, 0, 100, 60,
				MakeLeafNode(TEXT("img2"), TEXT("Image2"), TEXT("RECTANGLE"), 124, 0, 100, 40,
					FString::Printf(TEXT(",\"layoutSizingHorizontal\":\"FILL\",\"layoutSizingVertical\":\"FIXED\",\"fills\":[%s]"),
						*MakeSolidFillJson(1, 1, 1))),
				TEXT(",\"layoutMode\":\"VERTICAL\",\"layoutSizingHorizontal\":\"FILL\",\"layoutSizingVertical\":\"FIXED\","
					 "\"layoutGrow\":1"))),
		TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutWrap\":\"WRAP\",\"itemSpacing\":24,\"counterAxisSpacing\":16"))));

	FDesignDocument Doc;
	TestTrue(TEXT("Wrap fill parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* WrapWidget = FindWidgetById(Data.RootWidget, TEXT("wrap1"));
	const FUMGWidgetNode* Card1 = FindWidgetById(Data.RootWidget, TEXT("card1"));
	const FUMGWidgetNode* Card1Wrapper = FindWidgetById(Data.RootWidget, TEXT("card1_sz"));
	TestTrue(TEXT("Wrap widget exists"), WrapWidget != nullptr);
	TestTrue(TEXT("Wrap fill child exists"), Card1 != nullptr);
	TestTrue(TEXT("Wrap fill child is wrapped in a SizeBox"), Card1Wrapper != nullptr);
	if (!WrapWidget || !Card1 || !Card1Wrapper)
	{
		return false;
	}

	TestEqual(TEXT("Wrap child keeps standard type"), Card1->Type, TEXT("VerticalBox"));
	TestEqual(TEXT("Wrap child wrapper is a SizeBox"), Card1Wrapper->Type, TEXT("SizeBox"));
	TestEqual(TEXT("Wrap fill child preserves measured width"), GetWidgetProperty(*Card1Wrapper, TEXT("WidthOverride")), TEXT("100.0"));
	TestEqual(TEXT("Wrap fill child preserves measured height"), GetWidgetProperty(*Card1Wrapper, TEXT("HeightOverride")), TEXT("60.0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaCenteredFlowFillTextKeepsMeasuredWidthTest,
	"Design2UMG.FigmaParser.CenteredFlowFillTextKeepsMeasuredWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaCenteredFlowFillTextKeepsMeasuredWidthTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 200, 120,
		MakeContainerNode(TEXT("nav"), TEXT("Nav"), TEXT("FRAME"), 0, 0, 72, 40,
			MakeTextNode(TEXT("nav_label"), TEXT("Label"), 0, 0, 72, 16, TEXT("Label"),
				TEXT("\"fontFamily\":\"Roboto\",\"fontWeight\":500,\"fontSize\":12,\"textAlignHorizontal\":\"CENTER\","
					 "\"textAlignVertical\":\"TOP\",\"textCase\":\"ORIGINAL\",\"textDecoration\":\"NONE\","
					 "\"letterSpacing\":0.5,\"lineHeightUnit\":\"PIXELS\",\"lineHeightPx\":16"),
				TEXT(",\"layoutSizingHorizontal\":\"FILL\",\"layoutSizingVertical\":\"HUG\","
					 "\"absoluteRenderBounds\":{\"x\":21.2,\"y\":3,\"width\":29.5,\"height\":9.1}")),
			TEXT(",\"layoutMode\":\"VERTICAL\",\"counterAxisSizingMode\":\"FIXED\",\"primaryAxisSizingMode\":\"FIXED\""))));

	FDesignDocument Doc;
	TestTrue(TEXT("Centered flow fill text parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Label = FindWidgetById(Data.RootWidget, TEXT("nav_label"));
	const FUMGWidgetNode* LabelWrapper = FindWidgetById(Data.RootWidget, TEXT("nav_label_sz"));
	TestTrue(TEXT("Centered flow fill text exists"), Label != nullptr);
	TestTrue(TEXT("Centered flow fill text is wrapped in a SizeBox"), LabelWrapper != nullptr);
	if (!Label || !LabelWrapper)
	{
		return false;
	}

	TestEqual(TEXT("Centered flow fill text keeps standard type"), Label->Type, TEXT("TextBlock"));
	TestEqual(TEXT("Centered flow fill text wrapper is a SizeBox"), LabelWrapper->Type, TEXT("SizeBox"));
	TestEqual(TEXT("Centered flow fill text preserves measured width"), GetWidgetProperty(*LabelWrapper, TEXT("WidthOverride")), TEXT("72.0"));
	const FString HeightOverride = GetWidgetProperty(*LabelWrapper, TEXT("HeightOverride"));
	const FString MinDesiredHeight = GetWidgetProperty(*LabelWrapper, TEXT("MinDesiredHeight"));
	TestTrue(
		TEXT("Centered flow fill text preserves measured height"),
		HeightOverride == TEXT("16.0") || MinDesiredHeight == TEXT("16.0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaWrapPaddingUsesBorderWrapperTest,
	"Design2UMG.FigmaParser.WrapPaddingUsesBorderWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaWrapPaddingUsesBorderWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 400, 300,
		MakeContainerNode(TEXT("wrap_pad"), TEXT("WrapPad"), TEXT("FRAME"), 0, 0, 300, 200, FString::Printf(
			TEXT("%s,%s"),
			*MakeContainerNode(TEXT("c1"), TEXT("Card1"), TEXT("FRAME"), 0, 0, 100, 50, FString()),
			*MakeContainerNode(TEXT("c2"), TEXT("Card2"), TEXT("FRAME"), 100, 0, 100, 50, FString())),
		TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutWrap\":\"WRAP\",\"itemSpacing\":24,\"counterAxisSpacing\":16,"
			 "\"paddingLeft\":24,\"paddingRight\":24,\"paddingTop\":8,\"paddingBottom\":32"))));

	FDesignDocument Doc;
	TestTrue(TEXT("Wrap padding parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* WrapWidget = FindWidgetById(Data.RootWidget, TEXT("wrap_pad"));
	TestTrue(TEXT("Wrap padding widget exists"), WrapWidget != nullptr);
	if (!WrapWidget)
	{
		return false;
	}

	TestEqual(TEXT("Wrap padding container is promoted to border"), WrapWidget->Type, TEXT("Border"));
	TestEqual(TEXT("Wrap padding serialized on border wrapper"), GetWidgetProperty(*WrapWidget, TEXT("Padding")), TEXT("24.0,8.0,24.0,32.0"));
	TestTrue(TEXT("Border wrapper keeps inner wrap child"), WrapWidget->Children.Num() == 1 && WrapWidget->Children[0].Type == TEXT("WrapBox"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaFlowPaddingUsesBorderWrapperTest,
	"Design2UMG.FigmaParser.FlowPaddingUsesBorderWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaFlowPaddingUsesBorderWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 400, 300,
		MakeContainerNode(TEXT("flow_pad"), TEXT("FlowPad"), TEXT("FRAME"), 0, 0, 300, 120, FString::Printf(
			TEXT("%s,%s"),
			*MakeContainerNode(TEXT("c1"), TEXT("Card1"), TEXT("FRAME"), 24, 8, 100, 50, FString()),
			*MakeContainerNode(TEXT("c2"), TEXT("Card2"), TEXT("FRAME"), 148, 8, 100, 50, FString())),
		TEXT(",\"layoutMode\":\"HORIZONTAL\",\"itemSpacing\":24,"
			 "\"paddingLeft\":24,\"paddingRight\":12,\"paddingTop\":8,\"paddingBottom\":16"))));

	FDesignDocument Doc;
	TestTrue(TEXT("Flow padding parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* FlowWidget = FindWidgetById(Data.RootWidget, TEXT("flow_pad"));
	TestTrue(TEXT("Flow padding widget exists"), FlowWidget != nullptr);
	if (!FlowWidget)
	{
		return false;
	}

	TestEqual(TEXT("Flow padding container is promoted to border"), FlowWidget->Type, TEXT("Border"));
	TestEqual(TEXT("Flow padding serialized on border wrapper"), GetWidgetProperty(*FlowWidget, TEXT("Padding")), TEXT("24.0,8.0,12.0,16.0"));
	TestTrue(TEXT("Border wrapper keeps inner horizontal child"), FlowWidget->Children.Num() == 1 && FlowWidget->Children[0].Type == TEXT("HorizontalBox"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaInstanceSubtreePaddingUsesBorderWrapperTest,
	"Design2UMG.FigmaParser.InstanceSubtreePaddingUsesBorderWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaInstanceSubtreePaddingUsesBorderWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 400, 300,
		MakeContainerNode(TEXT("inst_root"), TEXT("Card Instance"), TEXT("INSTANCE"), 0, 0, 300, 120,
			MakeContainerNode(TEXT("flow_in_instance"), TEXT("Flow In Instance"), TEXT("FRAME"), 0, 0, 300, 120, FString::Printf(
				TEXT("%s,%s"),
				*MakeContainerNode(TEXT("c1"), TEXT("Card1"), TEXT("FRAME"), 24, 8, 100, 50, FString()),
				*MakeContainerNode(TEXT("c2"), TEXT("Card2"), TEXT("FRAME"), 148, 8, 100, 50, FString())),
			TEXT(",\"layoutMode\":\"HORIZONTAL\",\"itemSpacing\":24,"
				 "\"paddingLeft\":24,\"paddingRight\":12,\"paddingTop\":8,\"paddingBottom\":16")),
			TEXT(",\"componentId\":\"card_component\""))));

	FDesignDocument Doc;
	TestTrue(TEXT("Instance subtree parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* FlowWidget = FindWidgetById(Data.RootWidget, TEXT("flow_in_instance"));
	TestTrue(TEXT("Flow widget inside instance subtree exists"), FlowWidget != nullptr);
	if (!FlowWidget)
	{
		return false;
	}

	TestEqual(TEXT("Instance subtree flow uses a real padding surface"), FlowWidget->Type, TEXT("Border"));
	TestTrue(TEXT("Instance padding keeps horizontal flow inside"), FlowWidget->Children.Num() == 1 && FlowWidget->Children[0].Type == TEXT("HorizontalBox"));
	TestEqual(TEXT("Instance subtree flow padding reaches its border"), GetWidgetProperty(*FlowWidget, TEXT("Padding")), TEXT("24.0,8.0,12.0,16.0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTopLevelInstanceMixedStretchPaddingUsesBorderWrapperTest,
	"Design2UMG.FigmaParser.TopLevelInstanceMixedStretchPaddingUsesBorderWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTopLevelInstanceMixedStretchPaddingUsesBorderWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 400, 320,
		MakeContainerNode(TEXT("inst_nav_root"), TEXT("Navigation Rail"), TEXT("INSTANCE"), 0, 0, 72, 300, FString::Printf(
			TEXT("%s,%s"),
			*MakeContainerNode(TEXT("menu_block"), TEXT("Menu & Fab"), TEXT("FRAME"), 8, 44, 56, 116, FString::Printf(
				TEXT("%s,%s"),
				*MakeContainerNode(TEXT("fab"), TEXT("Fab"), TEXT("FRAME"), 8, 44, 56, 56, FString()),
				*MakeContainerNode(TEXT("menu"), TEXT("Menu"), TEXT("FRAME"), 8, 104, 56, 56, FString())),
				TEXT(",\"layoutMode\":\"VERTICAL\",\"layoutAlign\":\"INHERIT\","
					 "\"layoutSizingHorizontal\":\"HUG\",\"layoutSizingVertical\":\"HUG\"")),
			*MakeContainerNode(TEXT("segments_block"), TEXT("Segments"), TEXT("FRAME"), 0, 200, 72, 100, FString::Printf(
				TEXT("%s,%s,%s"),
				*MakeContainerNode(TEXT("seg1"), TEXT("Seg1"), TEXT("FRAME"), 0, 200, 72, 24, FString()),
				*MakeContainerNode(TEXT("seg2"), TEXT("Seg2"), TEXT("FRAME"), 0, 228, 72, 24, FString()),
				*MakeContainerNode(TEXT("seg3"), TEXT("Seg3"), TEXT("FRAME"), 0, 256, 72, 24, FString())),
				TEXT(",\"layoutMode\":\"VERTICAL\",\"layoutAlign\":\"STRETCH\","
					 "\"layoutSizingHorizontal\":\"FILL\",\"layoutSizingVertical\":\"HUG\""))),
		TEXT(",\"componentId\":\"nav_component\",\"layoutMode\":\"VERTICAL\",\"itemSpacing\":40,"
			 "\"paddingLeft\":0,\"paddingTop\":44,\"paddingRight\":0,\"paddingBottom\":56"))));

	FDesignDocument Doc;
	TestTrue(TEXT("Top-level instance mixed stretch parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* RootInstance = FindWidgetById(Data.RootWidget, TEXT("inst_nav_root"));
	TestTrue(TEXT("Top-level mixed-stretch instance exists"), RootInstance != nullptr);
	if (!RootInstance)
	{
		return false;
	}

	TestEqual(TEXT("Mixed-stretch top-level instance promotes to border"), RootInstance->Type, TEXT("Border"));
	TestEqual(TEXT("Mixed-stretch top-level instance keeps runtime padding on border"), GetWidgetProperty(*RootInstance, TEXT("Padding")), TEXT("0.0,44.0,0.0,56.0"));
	TestTrue(TEXT("Mixed-stretch top-level instance keeps inner vertical child"),
		RootInstance->Children.Num() == 1 && RootInstance->Children[0].Type == TEXT("VerticalBox"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTopLevelInstanceUniformStretchPaddingUsesBorderWrapperTest,
	"Design2UMG.FigmaParser.TopLevelInstanceUniformStretchPaddingUsesBorderWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTopLevelInstanceUniformStretchPaddingUsesBorderWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 400, 320,
		MakeContainerNode(TEXT("inst_app_root"), TEXT("App bar"), TEXT("INSTANCE"), 0, 0, 320, 112, FString::Printf(
			TEXT("%s,%s"),
			*MakeContainerNode(TEXT("icons_row"), TEXT("Leading & Trailing icons"), TEXT("FRAME"), 0, 0, 320, 56, FString::Printf(
				TEXT("%s,%s"),
				*MakeContainerNode(TEXT("left_icon"), TEXT("LeftIcon"), TEXT("FRAME"), 0, 0, 24, 24, FString()),
				*MakeContainerNode(TEXT("right_icon"), TEXT("RightIcon"), TEXT("FRAME"), 296, 0, 24, 24, FString())),
				TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutAlign\":\"STRETCH\","
					 "\"layoutSizingHorizontal\":\"FILL\",\"layoutSizingVertical\":\"FIXED\"")),
			*MakeContainerNode(TEXT("title_block"), TEXT("Text content"), TEXT("INSTANCE"), 0, 64, 320, 36,
				MakeTextNode(TEXT("title_line"), TEXT("TitleLine"), 0, 64, 64, 20, TEXT("Title"),
					TEXT("\"fontSize\":16")),
				TEXT(",\"componentId\":\"title_component\",\"layoutMode\":\"VERTICAL\",\"layoutAlign\":\"STRETCH\","
					 "\"layoutSizingHorizontal\":\"FILL\",\"layoutSizingVertical\":\"HUG\""))),
			TEXT(",\"componentId\":\"app_component\",\"layoutMode\":\"VERTICAL\",\"itemSpacing\":8,"
				 "\"primaryAxisSizingMode\":\"AUTO\",\"counterAxisSizingMode\":\"FIXED\","
				 "\"paddingLeft\":0,\"paddingTop\":0,\"paddingRight\":0,\"paddingBottom\":12"))));

	FDesignDocument Doc;
	TestTrue(TEXT("Top-level instance uniform stretch parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* RootInstance = FindWidgetById(Data.RootWidget, TEXT("inst_app_root"));
	TestTrue(TEXT("Top-level uniform-stretch instance exists"), RootInstance != nullptr);
	if (!RootInstance)
	{
		return false;
	}

	TestEqual(TEXT("Uniform-stretch top-level instance uses a real padding surface"), RootInstance->Type, TEXT("Border"));
	TestEqual(TEXT("Uniform-stretch top-level instance padding reaches its border"), GetWidgetProperty(*RootInstance, TEXT("Padding")), TEXT("0.0,0.0,0.0,12.0"));
	return true;
}

#endif

