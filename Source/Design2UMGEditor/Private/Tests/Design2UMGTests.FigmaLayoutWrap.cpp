// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Figma layout coverage: GRID/WRAP metadata, wrap gap/fill semantics, and flow/instance padding border-wrapper rules.

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "FigmaParser.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaGridWrapMetadataTest,
	"Design2UMG.FigmaParser.GridWrapMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaGridWrapMetadataTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page 1",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":900,"height":600},
					"children": [
						{
							"id": "grid1",
							"name": "GridContainer",
							"type": "FRAME",
							"visible": true,
							"layoutMode": "GRID",
							"itemSpacing": 12,
							"paddingLeft": 10,
							"paddingRight": 20,
							"paddingTop": 30,
							"paddingBottom": 40,
							"counterAxisSpacing": 6,
							"counterAxisAlignContent": "SPACE_BETWEEN",
							"layoutPositioning": "ABSOLUTE",
							"layoutAlign": "STRETCH",
							"primaryAxisSizingMode": "AUTO",
							"counterAxisSizingMode": "FIXED",
							"minWidth": 100,
							"maxWidth": 500,
							"minHeight": 80,
							"maxHeight": 400,
							"gridRowCount": 2,
							"gridColumnCount": 3,
							"gridRowGap": 14,
							"gridColumnGap": 16,
							"gridRowsSizing": "AUTO",
							"gridColumnsSizing": "FIXED",
							"gridChildHorizontalAlign": "CENTER",
							"gridChildVerticalAlign": "MIN",
							"gridRowSpan": 2,
							"gridColumnSpan": 3,
							"gridColumnAnchorIndex": 1,
							"gridRowAnchorIndex": 0,
							"itemReverseZIndex": true,
							"strokesIncludedInLayout": true,
							"boundVariables": {
								"itemSpacing": {"type": "VARIABLE_ALIAS", "id": "VariableID:1:2"}
							},
							"absoluteBoundingBox": {"x":20,"y":20,"width":300,"height":200},
							"children": []
						},
						{
							"id": "wrap1",
							"name": "WrapContainer",
							"type": "FRAME",
							"visible": true,
							"layoutMode": "HORIZONTAL",
							"layoutWrap": "WRAP",
							"itemSpacing": 8,
							"paddingLeft": 5,
							"paddingRight": 6,
							"paddingTop": 7,
							"paddingBottom": 9,
							"absoluteBoundingBox": {"x":20,"y":260,"width":400,"height":200},
							"children": []
						}
					]
				}]
			}]
		}
	})");
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
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page 1",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":400,"height":300},
					"children": [{
						"id": "wrap1",
						"name": "Wrap",
						"type": "FRAME",
						"visible": true,
						"layoutMode": "HORIZONTAL",
						"layoutWrap": "WRAP",
						"itemSpacing": 24,
						"counterAxisSpacing": 16,
						"absoluteBoundingBox": {"x":0,"y":0,"width":300,"height":200},
						"children": [
							{"id":"c1","name":"Card1","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":100,"height":50}},
							{"id":"c2","name":"Card2","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":100,"y":0,"width":100,"height":50}}
						]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Wrap gap parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* WrapWidget = FindWidgetById(Data.RootWidget, TEXT("wrap1"));
	TestTrue(TEXT("Wrap widget exists"), WrapWidget != nullptr);
	if (!WrapWidget || WrapWidget->Children.Num() < 2)
	{
		return false;
	}

	TestEqual(TEXT("Wrap uses dual-axis inner slot padding"), GetWidgetProperty(*WrapWidget, TEXT("InnerSlotPadding")), TEXT("24.0,16.0"));
	TestTrue(TEXT("Second child has no duplicated left gap"), FMath::IsNearlyZero(WrapWidget->Children[1].Slot.Padding.Left));
	TestTrue(TEXT("Second child has no duplicated top gap"), FMath::IsNearlyZero(WrapWidget->Children[1].Slot.Padding.Top));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaWrapFillChildPreservesMeasuredSizeTest,
	"Design2UMG.FigmaParser.WrapFillChildPreservesMeasuredSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaWrapFillChildPreservesMeasuredSizeTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page 1",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":400,"height":300},
					"children": [{
						"id": "wrap1",
						"name": "Wrap",
						"type": "FRAME",
						"visible": true,
						"layoutMode": "HORIZONTAL",
						"layoutWrap": "WRAP",
						"itemSpacing": 24,
						"counterAxisSpacing": 16,
						"absoluteBoundingBox": {"x":0,"y":0,"width":300,"height":200},
						"children": [
							{
								"id":"card1",
								"name":"Card1",
								"type":"FRAME",
								"visible":true,
								"layoutMode":"VERTICAL",
								"layoutSizingHorizontal":"FILL",
								"layoutSizingVertical":"FIXED",
								"layoutGrow":1,
								"absoluteBoundingBox":{"x":0,"y":0,"width":100,"height":60},
								"children":[
									{
										"id":"img1",
										"name":"Image1",
										"type":"RECTANGLE",
										"visible":true,
										"layoutSizingHorizontal":"FILL",
										"layoutSizingVertical":"FIXED",
										"absoluteBoundingBox":{"x":0,"y":0,"width":100,"height":40},
										"fills":[{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}]
									}
								]
							},
							{
								"id":"card2",
								"name":"Card2",
								"type":"FRAME",
								"visible":true,
								"layoutMode":"VERTICAL",
								"layoutSizingHorizontal":"FILL",
								"layoutSizingVertical":"FIXED",
								"layoutGrow":1,
								"absoluteBoundingBox":{"x":124,"y":0,"width":100,"height":60},
								"children":[
									{
										"id":"img2",
										"name":"Image2",
										"type":"RECTANGLE",
										"visible":true,
										"layoutSizingHorizontal":"FILL",
										"layoutSizingVertical":"FIXED",
										"absoluteBoundingBox":{"x":124,"y":0,"width":100,"height":40},
										"fills":[{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}]
									}
								]
							}
						]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Wrap fill parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* WrapWidget = FindWidgetById(Data.RootWidget, TEXT("wrap1"));
	const FUMGWidgetNode* Card1 = FindWidgetById(Data.RootWidget, TEXT("card1"));
	TestTrue(TEXT("Wrap widget exists"), WrapWidget != nullptr);
	TestTrue(TEXT("Wrap fill child gets fused size constraints"), Card1 != nullptr);
	if (!WrapWidget || !Card1)
	{
		return false;
	}

	TestEqual(TEXT("Wrap child fused type"), Card1->Type, TEXT("Design2UMGConstrainedVerticalBox"));
	TestEqual(TEXT("Wrap fill child preserves measured width"), GetWidgetProperty(*Card1, TEXT("WidthOverride")), TEXT("100.0"));
	TestEqual(TEXT("Wrap fill child preserves measured height"), GetWidgetProperty(*Card1, TEXT("HeightOverride")), TEXT("60.0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaCenteredFlowFillTextKeepsMeasuredWidthTest,
	"Design2UMG.FigmaParser.CenteredFlowFillTextKeepsMeasuredWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaCenteredFlowFillTextKeepsMeasuredWidthTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page 1",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":200,"height":120},
					"children": [{
						"id":"nav",
						"name":"Nav",
						"type":"FRAME",
						"visible":true,
						"layoutMode":"VERTICAL",
						"counterAxisSizingMode":"FIXED",
						"primaryAxisSizingMode":"FIXED",
						"absoluteBoundingBox":{"x":0,"y":0,"width":72,"height":40},
						"children":[
							{
								"id":"nav_label",
								"name":"Label",
								"type":"TEXT",
								"visible":true,
								"layoutSizingHorizontal":"FILL",
								"layoutSizingVertical":"HUG",
								"absoluteBoundingBox":{"x":0,"y":0,"width":72,"height":16},
								"absoluteRenderBounds":{"x":21.2,"y":3,"width":29.5,"height":9.1},
								"characters":"Label",
								"style":{
									"fontFamily":"Roboto",
									"fontWeight":500,
									"fontSize":12,
									"textAlignHorizontal":"CENTER",
									"textAlignVertical":"TOP",
									"textCase":"ORIGINAL",
									"textDecoration":"NONE",
									"letterSpacing":0.5,
									"lineHeightUnit":"PIXELS",
									"lineHeightPx":16
								}
							}
						]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Centered flow fill text parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Label = FindWidgetById(Data.RootWidget, TEXT("nav_label"));
	TestTrue(TEXT("Centered flow fill text gets fused size constraints"), Label != nullptr);
	if (!Label)
	{
		return false;
	}

	TestEqual(TEXT("Centered flow fill text uses fused type"), Label->Type, TEXT("Design2UMGConstrainedTextBlock"));
	TestEqual(TEXT("Centered flow fill text preserves measured width"), GetWidgetProperty(*Label, TEXT("WidthOverride")), TEXT("72.0"));
	const FString HeightOverride = GetWidgetProperty(*Label, TEXT("HeightOverride"));
	const FString MinDesiredHeight = GetWidgetProperty(*Label, TEXT("MinDesiredHeight"));
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
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page 1",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":400,"height":300},
					"children": [{
						"id": "wrap_pad",
						"name": "WrapPad",
						"type": "FRAME",
						"visible": true,
						"layoutMode": "HORIZONTAL",
						"layoutWrap": "WRAP",
						"itemSpacing": 24,
						"counterAxisSpacing": 16,
						"paddingLeft": 24,
						"paddingRight": 24,
						"paddingTop": 8,
						"paddingBottom": 32,
						"absoluteBoundingBox": {"x":0,"y":0,"width":300,"height":200},
						"children": [
							{"id":"c1","name":"Card1","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":100,"height":50}},
							{"id":"c2","name":"Card2","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":100,"y":0,"width":100,"height":50}}
						]
					}]
				}]
			}]
		}
	})");

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
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page 1",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":400,"height":300},
					"children": [{
						"id": "flow_pad",
						"name": "FlowPad",
						"type": "FRAME",
						"visible": true,
						"layoutMode": "HORIZONTAL",
						"itemSpacing": 24,
						"paddingLeft": 24,
						"paddingRight": 12,
						"paddingTop": 8,
						"paddingBottom": 16,
						"absoluteBoundingBox": {"x":0,"y":0,"width":300,"height":120},
						"children": [
							{"id":"c1","name":"Card1","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":24,"y":8,"width":100,"height":50}},
							{"id":"c2","name":"Card2","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":148,"y":8,"width":100,"height":50}}
						]
					}]
				}]
			}]
		}
	})");

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaInstanceSubtreePaddingSkipsBorderWrapperTest,
	"Design2UMG.FigmaParser.InstanceSubtreePaddingSkipsBorderWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaInstanceSubtreePaddingSkipsBorderWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page 1",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":400,"height":300},
					"children": [{
						"id": "inst_root",
						"name": "Card Instance",
						"type": "INSTANCE",
						"visible": true,
						"componentId": "card_component",
						"absoluteBoundingBox": {"x":0,"y":0,"width":300,"height":120},
						"children": [{
							"id": "flow_in_instance",
							"name": "Flow In Instance",
							"type": "FRAME",
							"visible": true,
							"layoutMode": "HORIZONTAL",
							"itemSpacing": 24,
							"paddingLeft": 24,
							"paddingRight": 12,
							"paddingTop": 8,
							"paddingBottom": 16,
							"absoluteBoundingBox": {"x":0,"y":0,"width":300,"height":120},
							"children": [
								{"id":"c1","name":"Card1","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":24,"y":8,"width":100,"height":50}},
								{"id":"c2","name":"Card2","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":148,"y":8,"width":100,"height":50}}
							]
						}]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Instance subtree parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* FlowWidget = FindWidgetById(Data.RootWidget, TEXT("flow_in_instance"));
	TestTrue(TEXT("Flow widget inside instance subtree exists"), FlowWidget != nullptr);
	if (!FlowWidget)
	{
		return false;
	}

	TestEqual(TEXT("Instance subtree flow keeps direct horizontal container"), FlowWidget->Type, TEXT("HorizontalBox"));
	TestEqual(TEXT("Instance subtree flow keeps direct padding on the container"), GetWidgetProperty(*FlowWidget, TEXT("Padding")), TEXT("24.0,8.0,12.0,16.0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTopLevelInstanceMixedStretchPaddingUsesBorderWrapperTest,
	"Design2UMG.FigmaParser.TopLevelInstanceMixedStretchPaddingUsesBorderWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTopLevelInstanceMixedStretchPaddingUsesBorderWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page 1",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":400,"height":320},
					"children": [{
						"id": "inst_nav_root",
						"name": "Navigation Rail",
						"type": "INSTANCE",
						"visible": true,
						"componentId": "nav_component",
						"layoutMode": "VERTICAL",
						"itemSpacing": 40,
						"paddingLeft": 0,
						"paddingTop": 44,
						"paddingRight": 0,
						"paddingBottom": 56,
						"absoluteBoundingBox": {"x":0,"y":0,"width":72,"height":300},
						"children": [
							{
								"id": "menu_block",
								"name": "Menu & Fab",
								"type": "FRAME",
								"visible": true,
								"layoutMode": "VERTICAL",
								"layoutAlign": "INHERIT",
								"layoutSizingHorizontal": "HUG",
								"layoutSizingVertical": "HUG",
								"absoluteBoundingBox": {"x":8,"y":44,"width":56,"height":116},
								"children": [
									{"id":"fab","name":"Fab","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":8,"y":44,"width":56,"height":56}},
									{"id":"menu","name":"Menu","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":8,"y":104,"width":56,"height":56}}
								]
							},
							{
								"id": "segments_block",
								"name": "Segments",
								"type": "FRAME",
								"visible": true,
								"layoutMode": "VERTICAL",
								"layoutAlign": "STRETCH",
								"layoutSizingHorizontal": "FILL",
								"layoutSizingVertical": "HUG",
								"absoluteBoundingBox": {"x":0,"y":200,"width":72,"height":100},
								"children": [
									{"id":"seg1","name":"Seg1","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":200,"width":72,"height":24}},
									{"id":"seg2","name":"Seg2","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":228,"width":72,"height":24}},
									{"id":"seg3","name":"Seg3","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":256,"width":72,"height":24}}
								]
							}
						]
					}]
				}]
			}]
		}
	})");

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTopLevelInstanceUniformStretchPaddingSkipsBorderWrapperTest,
	"Design2UMG.FigmaParser.TopLevelInstanceUniformStretchPaddingSkipsBorderWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTopLevelInstanceUniformStretchPaddingSkipsBorderWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page 1",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":400,"height":320},
					"children": [{
						"id": "inst_app_root",
						"name": "App bar",
						"type": "INSTANCE",
						"visible": true,
						"componentId": "app_component",
						"layoutMode": "VERTICAL",
						"itemSpacing": 8,
						"primaryAxisSizingMode": "AUTO",
						"counterAxisSizingMode": "FIXED",
						"paddingLeft": 0,
						"paddingTop": 0,
						"paddingRight": 0,
						"paddingBottom": 12,
						"absoluteBoundingBox": {"x":0,"y":0,"width":320,"height":112},
						"children": [
							{
								"id": "icons_row",
								"name": "Leading & Trailing icons",
								"type": "FRAME",
								"visible": true,
								"layoutMode": "HORIZONTAL",
								"layoutAlign": "STRETCH",
								"layoutSizingHorizontal": "FILL",
								"layoutSizingVertical": "FIXED",
								"absoluteBoundingBox": {"x":0,"y":0,"width":320,"height":56},
								"children": [
									{"id":"left_icon","name":"LeftIcon","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":24,"height":24}},
									{"id":"right_icon","name":"RightIcon","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":296,"y":0,"width":24,"height":24}}
								]
							},
							{
								"id": "title_block",
								"name": "Text content",
								"type": "INSTANCE",
								"visible": true,
								"componentId": "title_component",
								"layoutMode": "VERTICAL",
								"layoutAlign": "STRETCH",
								"layoutSizingHorizontal": "FILL",
								"layoutSizingVertical": "HUG",
								"absoluteBoundingBox": {"x":0,"y":64,"width":320,"height":36},
								"children": [
									{"id":"title_line","name":"TitleLine","type":"TEXT","visible":true,"characters":"Title","style":{"fontSize":16},"absoluteBoundingBox":{"x":0,"y":64,"width":64,"height":20}}
								]
							}
						]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Top-level instance uniform stretch parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* RootInstance = FindWidgetById(Data.RootWidget, TEXT("inst_app_root"));
	TestTrue(TEXT("Top-level uniform-stretch instance exists"), RootInstance != nullptr);
	if (!RootInstance)
	{
		return false;
	}

	TestEqual(TEXT("Uniform-stretch top-level instance keeps direct vertical container"), RootInstance->Type, TEXT("VerticalBox"));
	TestEqual(TEXT("Uniform-stretch top-level instance keeps direct padding"), GetWidgetProperty(*RootInstance, TEXT("Padding")), TEXT("0.0,0.0,0.0,12.0"));
	return true;
}

#endif

