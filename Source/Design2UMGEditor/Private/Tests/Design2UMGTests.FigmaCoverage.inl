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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenEmptySlotSemanticHintTest,
	"Design2UMG.PenParser.EmptySlotSemanticHint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenEmptySlotSemanticHintTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{ "id":"n1", "type":"frame", "name":"Card Header", "slot":[], "width":100, "height":32 },
			{ "id":"n2", "type":"frame", "name":"Page Numbers", "slot":["s1"], "width":100, "height":32 },
			{ "id":"n3", "type":"frame", "name":"Any Node", "placeholder":true, "width":100, "height":32 }
		]
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("Pen parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 3)
	{
		return false;
	}
	TestEqual(TEXT("Empty slot keeps pen.slot.present for preservation"), Doc.Children[0]->Metadata.FindRef(TEXT("pen.slot.present")), TEXT("true"));
	TestEqual(TEXT("Empty slot keeps pen.slot.count for preservation"), Doc.Children[0]->Metadata.FindRef(TEXT("pen.slot.count")), TEXT("0"));
	TestTrue(TEXT("Empty slot array should mark placeholder surface"), Doc.Children[0]->LayoutData.bSemanticPlaceholderSurface);
	TestEqual(TEXT("Non-empty slot keeps pen.slot.present for preservation"), Doc.Children[1]->Metadata.FindRef(TEXT("pen.slot.present")), TEXT("true"));
	TestEqual(TEXT("Non-empty slot keeps pen.slot.count for preservation"), Doc.Children[1]->Metadata.FindRef(TEXT("pen.slot.count")), TEXT("1"));
	TestFalse(TEXT("Non-empty slot should not synthesize pen.slot.empty"), Doc.Children[1]->Metadata.Contains(TEXT("pen.slot.empty")));
	TestTrue(TEXT("Non-empty slot array should mark placeholder surface"), Doc.Children[1]->LayoutData.bSemanticPlaceholderSurface);
	TestFalse(TEXT("Explicit placeholder should not synthesize pen slot preservation keys"), Doc.Children[2]->Metadata.Contains(TEXT("pen.slot.present")) || Doc.Children[2]->Metadata.Contains(TEXT("pen.slot.count")) || Doc.Children[2]->Metadata.Contains(TEXT("pen.slot.empty")));
	TestTrue(TEXT("Explicit placeholder=true should mark placeholder surface"), Doc.Children[2]->LayoutData.bSemanticPlaceholderSurface);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaEmptySlotSemanticHintTest,
	"Design2UMG.FigmaParser.EmptySlotSemanticHint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaEmptySlotSemanticHintTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "root",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":300,"height":200},
					"children": [
						{
							"id":"e1",
							"name":"Card Header",
							"type":"FRAME",
							"visible": true,
							"slot": [],
							"absoluteBoundingBox": {"x":10,"y":10,"width":80,"height":20}
						},
						{
							"id":"e2",
							"name":"Page Numbers",
							"type":"FRAME",
							"visible": true,
							"slot": ["s1"],
							"absoluteBoundingBox": {"x":10,"y":40,"width":80,"height":20}
						},
						{
							"id":"e3",
							"name":"Any",
							"type":"FRAME",
							"visible": true,
							"placeholder": true,
							"absoluteBoundingBox": {"x":10,"y":70,"width":80,"height":20}
						}
					]
				}]
			}]
		}
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() < 3)
	{
		return false;
	}
	TestEqual(TEXT("Empty slot keeps source.slot.present for preservation"), Doc.Children[0]->Children[0]->Metadata.FindRef(TEXT("source.slot.present")), TEXT("true"));
	TestEqual(TEXT("Empty slot keeps source.slot.count for preservation"), Doc.Children[0]->Children[0]->Metadata.FindRef(TEXT("source.slot.count")), TEXT("0"));
	TestTrue(TEXT("Empty slot array should mark placeholder surface"), Doc.Children[0]->Children[0]->LayoutData.bSemanticPlaceholderSurface);
	TestEqual(TEXT("Non-empty slot keeps source.slot.present for preservation"), Doc.Children[0]->Children[1]->Metadata.FindRef(TEXT("source.slot.present")), TEXT("true"));
	TestEqual(TEXT("Non-empty slot keeps source.slot.count for preservation"), Doc.Children[0]->Children[1]->Metadata.FindRef(TEXT("source.slot.count")), TEXT("1"));
	TestFalse(TEXT("Non-empty slot should not synthesize source.slot.empty"), Doc.Children[0]->Children[1]->Metadata.Contains(TEXT("source.slot.empty")));
	TestTrue(TEXT("Non-empty slot array should mark placeholder surface"), Doc.Children[0]->Children[1]->LayoutData.bSemanticPlaceholderSurface);
	TestFalse(TEXT("Explicit placeholder should not synthesize source slot preservation keys"), Doc.Children[0]->Children[2]->Metadata.Contains(TEXT("source.slot.present")) || Doc.Children[0]->Children[2]->Metadata.Contains(TEXT("source.slot.count")) || Doc.Children[0]->Children[2]->Metadata.Contains(TEXT("source.slot.empty")));
	TestTrue(TEXT("Explicit placeholder=true should mark placeholder surface"), Doc.Children[0]->Children[2]->LayoutData.bSemanticPlaceholderSurface);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaceholderSurfaceConversionTest,
	"Design2UMG.Converter.PlaceholderSurfaceConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPlaceholderSurfaceConversionTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!TestNotNull(TEXT("Settings available"), Settings))
	{
		return false;
	}
	FScopedPlaceholderHatchStyleOverride PlaceholderStyleScope(
		false,
		TEXT("#10203033"),
		TEXT("#112233"),
		TEXT("#44556633"),
		TEXT("#778899"),
		7,
		11);
	const FString Json = TEXT(R"({
		"children": [
			{ "id":"slotCell", "type":"frame", "name":"Table Cell", "slot":[], "width":100, "height":40 },
			{ "id":"pageSlot", "type":"frame", "name":"Page Numbers Slot", "width":40, "height":40, "x":0, "y":48 }
		]
	})");
	FDesignDocument Doc;
	const bool bParsed = FPenParser::ParseDocument(Json, Doc);
	TestTrue(TEXT("Pen parse succeeds"), bParsed);
	if (!bParsed || Doc.Children.Num() != 2)
	{
		return false;
	}
	TestEqual(TEXT("Empty slot metadata preserved"), Doc.Children[0]->Metadata.FindRef(TEXT("pen.slot.empty")), TEXT("true"));
	TestTrue(TEXT("Empty slot array keeps semantic placeholder"), Doc.Children[0]->LayoutData.bSemanticPlaceholderSurface);
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(Settings));
	const FUMGWidgetNode* SlotCell = FindWidgetById(Data.RootWidget, TEXT("slotCell"));
	const FUMGWidgetNode* PageSlot = FindWidgetById(Data.RootWidget, TEXT("pageSlot"));
	TestTrue(TEXT("slotCell widget exists"), SlotCell != nullptr);
	TestTrue(TEXT("pageSlot widget exists"), PageSlot != nullptr);
	if (!SlotCell || !PageSlot)
	{
		return false;
	}
	const FString SlotParams = GetWidgetProperty(*SlotCell, TEXT("MaterialParams"));
	const FString PageParams = GetWidgetProperty(*PageSlot, TEXT("MaterialParams"));
	TestEqual(TEXT("slotCell uses Image"), SlotCell->Type, TEXT("Image"));
	TestEqual(TEXT("slotCell placeholder material"), GetWidgetProperty(*SlotCell, TEXT("MaterialRef")), TEXT("placeholdersurface"));
	TestTrue(TEXT("slotCell placeholder keeps canonical fill color when custom style disabled"), SlotParams.Contains(TEXT("#D282FF33")));
	TestTrue(TEXT("slotCell placeholder keeps canonical outline color when custom style disabled"), SlotParams.Contains(TEXT("#D480FF")));
	TestTrue(TEXT("slotCell placeholder keeps canonical stripe width when custom style disabled"), SlotParams.Contains(TEXT("\"StripeWidth\":1")));
	TestTrue(TEXT("slotCell placeholder keeps canonical stripe gap when custom style disabled"), SlotParams.Contains(TEXT("\"StripeGap\":4")));
	TestTrue(TEXT("slotCell placeholder width override exists"), HasWidgetProperty(*SlotCell, TEXT("WidthOverride")));
	TestTrue(TEXT("slotCell placeholder height override exists"), HasWidgetProperty(*SlotCell, TEXT("HeightOverride")));
	TestEqual(TEXT("pageSlot uses Image"), PageSlot->Type, TEXT("Image"));
	TestEqual(TEXT("pageSlot placeholder material"), GetWidgetProperty(*PageSlot, TEXT("MaterialRef")), TEXT("placeholdersurface"));
	TestTrue(TEXT("pageSlot placeholder keeps canonical fill color when custom style disabled"), PageParams.Contains(TEXT("#D282FF33")));
	TestTrue(TEXT("pageSlot placeholder keeps canonical outline color when custom style disabled"), PageParams.Contains(TEXT("#D480FF")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaceholderSurfacePaletteTracksRefContextTest,
	"Design2UMG.Converter.PlaceholderSurfacePaletteTracksRefContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPlaceholderSurfacePaletteTracksRefContextTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!TestNotNull(TEXT("Settings available"), Settings))
	{
		return false;
	}
	FScopedPlaceholderHatchStyleOverride PlaceholderStyleScope(
		true,
		TEXT("#10203033"),
		TEXT("#112233"),
		TEXT("#44556633"),
		TEXT("#778899"),
		3,
		9);

	const FString BaseJson = TEXT(R"JSON({
		"children": [
			{
				"id": "slotBase",
				"type": "frame",
				"name": "Slot Base",
				"width": 100,
				"height": 40,
				"slot": []
			}
		]
	})JSON");
	FDesignDocument BaseDoc;
	TestTrue(TEXT("Base placeholder parse succeeds"), FPenParser::ParseDocument(BaseJson, BaseDoc));
	const FUMGWidgetData BaseData = FDesign2UMGConverter::Convert(BaseDoc, FDesign2UMGConversionSettings::FromSettings(Settings));
	const FUMGWidgetNode* BaseSlot = FindWidgetById(BaseData.RootWidget, TEXT("slotBase"));
	TestTrue(TEXT("slotBase exists"), BaseSlot != nullptr);
	if (BaseSlot != nullptr)
	{
		const FString Params = GetWidgetProperty(*BaseSlot, TEXT("MaterialParams"));
		TestTrue(TEXT("Base placeholder keeps configured palette"), Params.Contains(TEXT("#10203033")));
		TestTrue(TEXT("Base placeholder keeps configured outline"), Params.Contains(TEXT("#112233")));
		TestTrue(TEXT("Base placeholder keeps configured stripe width"), Params.Contains(TEXT("\"StripeWidth\":3")));
		TestTrue(TEXT("Base placeholder keeps configured stripe gap"), Params.Contains(TEXT("\"StripeGap\":9")));
	}

	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "slotBase",
				"type": "frame",
				"reusable": true,
				"name": "Slot Base",
				"width": 100,
				"height": 40,
				"slot": []
			},
			{
				"id": "screen",
				"type": "frame",
				"width": 200,
				"height": 80,
				"children": [
					{
						"id": "slotInstance",
						"type": "ref",
						"ref": "slotBase"
					}
				]
			}
		]
	})JSON");
	FDesignDocument Doc;
	TestTrue(TEXT("Ref-context placeholder parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(Settings));
	const FUMGWidgetNode* SlotInstance = FindWidgetById(Data.RootWidget, TEXT("slotInstance"));
	TestTrue(TEXT("slotInstance exists"), SlotInstance != nullptr);
	if (SlotInstance != nullptr)
	{
		const FString Params = GetWidgetProperty(*SlotInstance, TEXT("MaterialParams"));
		TestTrue(TEXT("Expanded ref placeholder uses configured instance palette"), Params.Contains(TEXT("#44556633")));
		TestTrue(TEXT("Expanded ref placeholder uses configured instance outline"), Params.Contains(TEXT("#778899")));
		TestTrue(TEXT("Expanded ref placeholder keeps configured stripe width"), Params.Contains(TEXT("\"StripeWidth\":3")));
		TestTrue(TEXT("Expanded ref placeholder keeps configured stripe gap"), Params.Contains(TEXT("\"StripeGap\":9")));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConverterTextAlignVerticalDoesNotMoveSlotTest,
	"Design2UMG.Converter.TextAlignVerticalDoesNotMoveSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConverterTextAlignVerticalDoesNotMoveSlotTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"width": 200,
				"height": 60,
				"layout": "horizontal",
				"alignItems": "start",
				"children": [
					{
						"id": "label",
						"type": "text",
						"content": "Button",
						"fill": "#FFFFFF",
						"fontFamily": "Geist",
						"fontSize": 14,
						"textAlignVertical": "middle"
					}
				]
			}
		]
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("ParseDocument succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Label = FindWidgetById(Data.RootWidget, TEXT("label"));
	TestNotNull(TEXT("Converted label exists"), Label);
	if (!Label)
	{
		return false;
	}
	TestEqual(TEXT("Auto-height label uses fused type"), Label->Type, TEXT("Design2UMGConstrainedTextBlock"));
	TestEqual(TEXT("textAlignVertical kept as metadata"), GetWidgetProperty(*Label, TEXT("TextAlignVertical")), TEXT("Center"));
	TestTrue(TEXT("Fused label slot stays top-aligned"), Label->Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Top);
	FMargin Padding = FMargin(0.0f);
	TestFalse(TEXT("Auto-height text does not synthesize internal padding"),
		Design2UMGWidgetNodeAccess::TryGetWidgetMargin(*Label, TEXT("Padding"), Padding));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConverterTextAlignVerticalUsesInternalPaddingWhenHeightFixedTest,
	"Design2UMG.Converter.TextAlignVerticalUsesInternalPaddingWhenHeightFixed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConverterTextAlignVerticalUsesInternalPaddingWhenHeightFixedTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"width": 240,
				"height": 80,
				"layout": "horizontal",
				"alignItems": "start",
				"children": [
					{
						"id": "label",
						"type": "text",
						"content": "Button",
						"width": 140,
						"height": 40,
						"fill": "#FFFFFF",
						"fontFamily": "Geist",
						"fontSize": 14,
						"textAlignVertical": "middle"
					}
				]
			}
		]
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("ParseDocument succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Label = FindWidgetById(Data.RootWidget, TEXT("label"));
	TestNotNull(TEXT("Converted fixed-height label exists"), Label);
	if (!Label)
	{
		return false;
	}
	TestEqual(TEXT("Fixed-height label uses fused type"), Label->Type, TEXT("Design2UMGConstrainedTextBlock"));

	TestEqual(TEXT("Fixed-height textAlignVertical kept as metadata"), GetWidgetProperty(*Label, TEXT("TextAlignVertical")), TEXT("Center"));
	TestTrue(TEXT("Fixed-height fused label slot stays top-aligned"), Label->Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Top);
	FMargin Padding = FMargin(0.0f);
	TestTrue(TEXT("Fixed-height text synthesizes internal padding"),
		Design2UMGWidgetNodeAccess::TryGetWidgetMargin(*Label, TEXT("Padding"), Padding));
	TestTrue(TEXT("Fixed-height text gets positive top padding"), Padding.Top > 0.5f);
	TestTrue(TEXT("Fixed-height text gets positive bottom padding"), Padding.Bottom > 0.5f);
	TestTrue(TEXT("Center alignment splits slack symmetrically"),
		FMath::IsNearlyEqual(Padding.Top, Padding.Bottom, 0.75f));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConverterNonUniformStrokePaddingTest,
	"Design2UMG.Converter.NonUniformStrokePadding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConverterNonUniformStrokePaddingTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "panel",
				"type": "frame",
				"width": 240,
				"height": 60,
				"layout": "horizontal",
				"padding": [10, 12],
				"stroke": "#FFFFFF",
				"strokeWidth": { "right": 1 },
				"strokeAlignment": "outer",
				"children": [
					{
						"id": "text",
						"type": "text",
						"content": "Item",
						"fill": "#FFFFFF",
						"fontFamily": "Geist",
						"fontSize": 16
					}
				]
			}
		]
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("ParseDocument succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Panel = FindWidgetById(Data.RootWidget, TEXT("panel"));
	TestNotNull(TEXT("Converted panel exists"), Panel);
	if (!Panel)
	{
		return false;
	}
	TestEqual(TEXT("Outside right stroke only adds right inset"), GetWidgetProperty(*Panel, TEXT("StrokePadding")), TEXT("0.0,0.0,0.5,0.0"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOfficialFieldParityTest,
	"Design2UMG.Parity.PenOfficialFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenOfficialFieldParityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"variables": {
			"flagDisabled": {"type":"boolean","value":false},
			"alphaVar": {"type":"number","value":0.66},
			"underVar": {"type":"boolean","value":true},
			"segA": {"type":"string","value":"Hi"}
		},
		"children": [{
			"id": "frame1",
			"type": "frame",
			"layout": "horizontal",
			"layoutIncludeStroke": false,
			"stroke": "#112233",
			"strokeWidth": 2,
			"strokeAlignment": "inner",
			"strokeLinejoin": "miter",
			"strokeLinecap": "round",
			"children": [{
				"id": "text1",
				"type": "text",
				"content": "Spec",
				"fontStyle": "normal",
				"underline": false,
				"strikethrough": true,
				"href": "https://docs.pencil.dev",
				"fill": {
					"type": "mesh_gradient",
					"blendMode": "multiply",
					"opacity": 0.42,
					"rows": 2,
					"columns": 2,
					"points": [
						{"x": 0, "y": 0, "color": "#000000", "in": [-0.1, 0.0], "out": [0.1, 0.0]},
						{"position": {"x": 1, "y": 1}, "color": "#FFFFFF", "handleIn": [0.9, 1.0], "handleOut": [1.1, 1.0]}
					],
					"colors": [
						{"color": "#000000", "position": 0},
						{"color": "#FFFFFF", "position": 1}
					]
				}
			},{
				"id": "rect1",
				"type": "rectangle",
				"fill": {
					"type": "gradient",
					"gradientType": "radial",
					"center": {"x": 0.2, "y": 0.8},
					"size": {"width": "2.5", "height": 1.25},
					"colors": [
						{"color": "#000000", "position": 0},
						{"color": "#FFFFFF", "position": 1}
					]
				},
				"effect": [{
					"type": "shadow",
					"blendMode": "screen",
					"shadowType": "outer",
					"offset": {"x": 2, "y": 3},
					"blur": 4,
					"spread": 1,
					"color": "#01020340"
				}]
			},{
				"id": "rich1",
				"type": "text",
				"enabled": "$flagDisabled",
				"opacity": "$alphaVar",
				"underline": "$underVar",
				"content": [
					{"content": "$segA", "fontWeight": "700"},
					{"content": " there", "fontStyle": "italic"}
				]
			}]
		}]
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("Pen official parity parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() < 3)
		return false;
	const TSharedPtr<FDesignNode>& FrameNode = Doc.Children[0];
	const TSharedPtr<FDesignNode>& TextNode = FrameNode->Children[0];
	const TSharedPtr<FDesignNode>& GradientNode = FrameNode->Children[1];
	const TSharedPtr<FDesignNode>& RichTextNode = FrameNode->Children[2];
	TestTrue(TEXT("layoutIncludeStroke false preserved"), FrameNode->Metadata.FindRef(TEXT("layoutIncludeStroke")) == TEXT("false"));
	TestTrue(TEXT("strokeLinejoin preserved"), FrameNode->Metadata.FindRef(TEXT("stroke.join")) == TEXT("miter"));
	TestTrue(TEXT("strokeLinecap preserved"), FrameNode->Metadata.FindRef(TEXT("stroke.cap")) == TEXT("round"));
	TestTrue(TEXT("fill blendMode preserved"), TextNode->Visual.PrimaryFill().BlendMode == TEXT("multiply"));
	TestTrue(TEXT("fill opacity preserved"), FMath::IsNearlyEqual(TextNode->Visual.PrimaryFill().Opacity, 0.42f, 0.001f));
	TestTrue(TEXT("text href preserved"), TextNode->Text.Href == TEXT("https://docs.pencil.dev"));
	TestTrue(TEXT("text strikethrough preserved"), TextNode->Text.bStrikethrough);
	TestTrue(TEXT("gradient center parsed"), GradientNode->Visual.PrimaryFill().Gradient.Center.IsSet()
		&& FMath::IsNearlyEqual(GradientNode->Visual.PrimaryFill().Gradient.Center.GetValue().X, 0.2f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Visual.PrimaryFill().Gradient.Center.GetValue().Y, 0.8f, 0.001f));
	TestTrue(TEXT("gradient size parsed"), GradientNode->Visual.PrimaryFill().Gradient.Size.IsSet()
		&& FMath::IsNearlyEqual(GradientNode->Visual.PrimaryFill().Gradient.Size.GetValue().X, 2.5f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Visual.PrimaryFill().Gradient.Size.GetValue().Y, 1.25f, 0.001f));
	TestTrue(TEXT("shadow blendMode preserved"), GradientNode->Visual.Effects.Num() > 0
		&& GradientNode->Visual.Effects[0].BlendMode == TEXT("screen")
		&& GradientNode->Visual.Effects[0].Shadow.BlendMode == TEXT("screen"));
	TestTrue(TEXT("text content array fallback"), RichTextNode->Text.Content == TEXT("Hi there"));
	TestTrue(TEXT("text content array raw preserved"), RichTextNode->Metadata.Contains(TEXT("text.content.raw")));
	TestTrue(TEXT("text runs parsed"), RichTextNode->Text.TextRuns.Num() == 2
		&& RichTextNode->Text.TextRuns[0].FontWeight == TEXT("700")
		&& RichTextNode->Text.TextRuns[1].FontStyle == TEXT("italic"));
	TestTrue(TEXT("boolean variable parsed"), !RichTextNode->Transform.bEnabled && RichTextNode->Text.bUnderline);
	TestTrue(TEXT("number variable parsed"), FMath::IsNearlyEqual(RichTextNode->Transform.Opacity, 0.66f, 0.001f));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaOfficialFieldParityTest,
	"Design2UMG.Parity.FigmaOfficialFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaOfficialFieldParityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"components":{"12:34":{"key":"component-key-1","name":"Sidebar Item Active?=True","description":"","remote":true,"componentSetId":"56:78","componentPropertyDefinitions":{"title":{"type":"TEXT","defaultValue":"Hello"}},"documentationLinks":[{"uri":"https://example.com/component"}]}},
		"componentSets":{"56:78":{"key":"component-set-key-1","name":"Sidebar Item","description":"","remote":true,"componentPropertyDefinitions":{"Active":{"type":"BOOLEAN","defaultValue":true}},"variantGroupProperties":{"Active":{"values":["True","False"]}}}},
		"document": {"id":"0:0","name":"Doc","type":"DOCUMENT","prototypeStartNodeID":"0:2","prototypeDevice":{"type":"PRESET","size":"DESKTOP"},"flowStartingPoints":[{"nodeId":"0:2","name":"Doc Flow"}],"children":[
			{"id":"0:1","name":"Page","type":"CANVAS","children":[
				{"id":"0:2","name":"Root","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":800,"height":600},"children":[
					{"id":"gridp","name":"GridParity","type":"FRAME","visible":true,"layoutMode":"GRID","layoutWrap":"WRAP","itemSpacing":8,
					 "paddingLeft":1,"paddingRight":2,"paddingTop":3,"paddingBottom":4,
					 "counterAxisAlignContent":"SPACE_BETWEEN","counterAxisSpacing":6,
					 "layoutPositioning":"ABSOLUTE","layoutAlign":"STRETCH",
					 "overflowDirection":"HORIZONTAL_SCROLLING",
					 "transitionNodeID":"1:3",
					 "transitionDuration":420,
					 "transitionEasing":{"type":"EASE_OUT"},
					 "primaryAxisSizingMode":"AUTO","counterAxisSizingMode":"FIXED",
					 "itemReverseZIndex":true,"strokesIncludedInLayout":false,
					 "minWidth":100,"maxWidth":300,"minHeight":50,"maxHeight":200,
					 "gridRowCount":2,"gridColumnCount":3,"gridRowGap":10,"gridColumnGap":12,
					 "gridRowsSizing":"AUTO","gridColumnsSizing":"FIXED",
					 "gridChildHorizontalAlign":"CENTER","gridChildVerticalAlign":"MAX",
					 "gridRowSpan":2,"gridColumnSpan":1,"gridColumnAnchorIndex":0,"gridRowAnchorIndex":1,
					 "layoutGrids":[{"pattern":"GRID","sectionSize":8,"visible":true}],
					 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"NODE","destinationId":"1:2","navigation":"NAVIGATE","transition":{"type":"SMART_ANIMATE","duration":180,"direction":"LEFT","easing":{"type":"EASE_IN_AND_OUT"},"matchLayers":true}}]}],
					 "flowStartingPoints":[{"nodeId":"0:2","name":"Flow 1"}],
					 "componentPropertyReferences":{"width":"123:1"},
					 "explicitVariableModes":{"1":"2:3"},
					 "resolvedVariableModes":{"1":"2:3"},
					 "annotations":[{"label":"todo","properties":{}}],
					 "measurements":[{"label":"w","value":320}],
					 "exportSettings":[{"suffix":"@2x","format":"PNG"}],
					 "devStatus":{"type":"READY_FOR_DEV"},
					 "branchData":{"mainFileKey":"main1","branchKey":"branch1"},
					 "boundVariables":{"itemSpacing":{"type":"VARIABLE_ALIAS","id":"VariableID:1:2"}},
					 "absoluteBoundingBox":{"x":10,"y":10,"width":320,"height":240},"children":[]},
					{"id":"paint1","name":"PaintParity","type":"RECTANGLE","visible":true,
					 "absoluteBoundingBox":{"x":400,"y":10,"width":120,"height":80},
					 "fills":[{"type":"SOLID","visible":true,"opacity":0.5,"blendMode":"MULTIPLY","color":{"r":1,"g":0,"b":0}}],
					 "effects":[{"type":"DROP_SHADOW","visible":true,"blendMode":"SCREEN","offset":{"x":1,"y":2},"radius":3,"spread":0.5,"opacity":0.6,"color":{"r":0,"g":0,"b":0}}]},
					{"id":"inst1","name":"InstanceParity","type":"INSTANCE","componentId":"12:34","componentSetId":"56:78","mainComponentKey":"component-key-1","visible":true,
					 "componentProperties":{"title":{"type":"TEXT","value":"Hello"}},
					 "variantProperties":{"Active":"True","Size":"Small"},
					 "overrides":[{"id":"inst1_txt","path":["characters"],"value":"Override"}],
					 "isExposedInstance":true,
					 "absoluteBoundingBox":{"x":560,"y":10,"width":120,"height":80},
					 "children":[{"id":"inst1_txt","name":"Label","type":"TEXT","visible":true,
						"absoluteBoundingBox":{"x":570,"y":20,"width":100,"height":24},
						"characters":"Instance text","style":{"fontSize":16}}]}
				]}
			]}
		]}
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("Figma official parity parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() < 3)
		return false;
	const TSharedPtr<FDesignNode>& Node = Doc.Children[0]->Children[0];
	const TSharedPtr<FDesignNode>& PaintNode = Doc.Children[0]->Children[1];
	const TSharedPtr<FDesignNode>& InstanceNode = Doc.Children[0]->Children[2];
	TestTrue(TEXT("figma layoutPositioning"), Node->Metadata.FindRef(TEXT("layoutPositioning")) == TEXT("ABSOLUTE"));
	TestTrue(TEXT("figma counterAxisSpacing"), Node->Metadata.Contains(TEXT("counterAxisSpacing")));
	TestTrue(TEXT("figma layoutAlign"), Node->Metadata.FindRef(TEXT("layoutAlign")) == TEXT("STRETCH"));
	TestTrue(TEXT("figma sizing modes"), Node->Metadata.Contains(TEXT("primaryAxisSizingMode")) && Node->Metadata.Contains(TEXT("counterAxisSizingMode")));
	TestTrue(TEXT("figma min/max"), Node->Metadata.Contains(TEXT("minWidth")) && Node->Metadata.Contains(TEXT("maxHeight")));
	TestTrue(TEXT("figma grid keys"), Node->Metadata.Contains(TEXT("gridRowCount")) && Node->Metadata.Contains(TEXT("gridColumnsSizing")));
	TestTrue(TEXT("figma grid child align"), Node->Metadata.Contains(TEXT("gridChildHorizontalAlign")) && Node->Metadata.Contains(TEXT("gridChildVerticalAlign")));
	TestTrue(TEXT("figma grid span/anchor"), Node->Metadata.Contains(TEXT("gridRowSpan")) && Node->Metadata.Contains(TEXT("gridColumnAnchorIndex")));
	TestTrue(TEXT("figma bool keys"), Node->Metadata.FindRef(TEXT("itemReverseZIndex")) == TEXT("true"));
	TestTrue(TEXT("figma boundVariables"), Node->Metadata.Contains(TEXT("boundVariables.itemSpacing")));
	TestTrue(TEXT("figma overflowDirection"), Node->Metadata.FindRef(TEXT("overflowDirection")) == TEXT("HORIZONTAL_SCROLLING"));
	TestTrue(TEXT("figma layoutGrids"), Node->Metadata.Contains(TEXT("layoutGrids")));
	TestTrue(TEXT("figma interactions"), Node->Metadata.Contains(TEXT("interactions")));
	TestTrue(TEXT("figma flowStartingPoints"), Node->Metadata.Contains(TEXT("flowStartingPoints")));
	TestTrue(TEXT("figma componentPropertyReferences"), Node->Metadata.Contains(TEXT("componentPropertyReferences")));
	TestTrue(TEXT("figma explicitVariableModes"), Node->Metadata.Contains(TEXT("explicitVariableModes")));
	TestTrue(TEXT("figma resolvedVariableModes"), Node->Metadata.Contains(TEXT("resolvedVariableModes")));
	TestTrue(TEXT("figma annotations"), Node->Metadata.Contains(TEXT("annotations")));
	TestTrue(TEXT("figma measurements"), Node->Metadata.Contains(TEXT("measurements")));
	TestTrue(TEXT("figma exportSettings"), Node->Metadata.Contains(TEXT("exportSettings")));
	TestTrue(TEXT("figma devStatus"), Node->Metadata.Contains(TEXT("devStatus")));
	TestTrue(TEXT("figma branchData"), Node->Metadata.Contains(TEXT("branchData")));
	TestTrue(TEXT("figma components dictionary"), Doc.Metadata.Contains(TEXT("components")));
	TestTrue(TEXT("figma componentSets dictionary"), Doc.Metadata.Contains(TEXT("componentSets")));
	TestTrue(TEXT("figma components json field"), !Doc.SourceDocument().ComponentsJson.IsEmpty());
	TestTrue(TEXT("figma componentSets json field"), !Doc.SourceDocument().ComponentSetsJson.IsEmpty());
	TestTrue(TEXT("figma components json carries component defs"), Doc.SourceDocument().ComponentsJson.Contains(TEXT("componentPropertyDefinitions")));
	TestTrue(TEXT("figma componentSets json carries variant groups"), Doc.SourceDocument().ComponentSetsJson.Contains(TEXT("variantGroupProperties")));
	TestTrue(TEXT("figma doc prototypeStartNodeID"), Doc.Metadata.FindRef(TEXT("prototypeStartNodeID")) == TEXT("0:2"));
	TestTrue(TEXT("figma doc prototypeDevice"), Doc.Metadata.Contains(TEXT("prototypeDevice")));
	TestTrue(TEXT("figma absolute positioning semantic"), Node->LayoutData.bAbsoluteLayoutPositioning);
	TestTrue(TEXT("figma wrap cross-axis gap semantic"), Node->LayoutData.WrapCrossAxisGap.IsSet() && FMath::IsNearlyEqual(Node->LayoutData.WrapCrossAxisGap.GetValue(), 6.0f, 0.001f));
	TestTrue(TEXT("figma layoutWrap metadata"), Node->Metadata.FindRef(TEXT("layoutWrap")) == TEXT("WRAP"));
	TestTrue(TEXT("figma strokesIncludedInLayout metadata"), Node->Metadata.FindRef(TEXT("strokesIncludedInLayout")) == TEXT("false"));
	TestTrue(TEXT("figma itemReverseZIndex metadata"), Node->Metadata.FindRef(TEXT("itemReverseZIndex")) == TEXT("true"));
	TestTrue(TEXT("figma boundVariables metadata"), Node->Metadata.Contains(TEXT("boundVariables")));
	TestEqual(TEXT("figma overflowDirection semantic"), Node->LayoutData.Overflow, EDesignOverflow::HorizontalScrolling);
	TestTrue(TEXT("figma prototype transitionNodeID typed"), Node->Source().Prototype.TransitionNodeId == TEXT("1:3"));
	TestTrue(TEXT("figma prototype legacy transition typed"), Node->Source().Prototype.Transition.Duration.IsSet()
		&& FMath::IsNearlyEqual(Node->Source().Prototype.Transition.Duration.GetValue(), 420.0f, 0.001f)
		&& Node->Source().Prototype.Transition.EasingType == TEXT("EASE_OUT"));
	TestTrue(TEXT("figma prototype interactions typed"), Node->Source().Prototype.Interactions.Num() == 1
		&& Node->Source().Prototype.Interactions[0].TriggerType == TEXT("ON_CLICK")
		&& Node->Source().Prototype.Interactions[0].Actions.Num() == 1
		&& Node->Source().Prototype.Interactions[0].Actions[0].Type == TEXT("NODE")
		&& Node->Source().Prototype.Interactions[0].Actions[0].DestinationId == TEXT("1:2")
		&& Node->Source().Prototype.Interactions[0].Actions[0].Navigation == TEXT("NAVIGATE")
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.Type == TEXT("SMART_ANIMATE")
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.Direction == TEXT("LEFT")
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.Duration.IsSet()
		&& FMath::IsNearlyEqual(Node->Source().Prototype.Interactions[0].Actions[0].Transition.Duration.GetValue(), 180.0f, 0.001f)
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.EasingType == TEXT("EASE_IN_AND_OUT")
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.MatchLayers.IsSet()
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.MatchLayers.GetValue());
	TestTrue(TEXT("figma prototype flowStartingPoints typed"), Node->Source().Prototype.FlowStartingPoints.Num() == 1
		&& Node->Source().Prototype.FlowStartingPoints[0].NodeId == TEXT("0:2")
		&& Node->Source().Prototype.FlowStartingPoints[0].Name == TEXT("Flow 1"));
	TestTrue(TEXT("figma doc prototypeStartNodeID typed"), Doc.SourceDocument().PrototypeStartNodeId == TEXT("0:2"));
	TestTrue(TEXT("figma doc prototypeDevice typed"), Doc.SourceDocument().PrototypeDeviceJson.Contains(TEXT("DESKTOP")));
	TestTrue(TEXT("figma doc flowStartingPoints typed"), Doc.SourceDocument().FlowStartingPoints.Num() == 1
		&& Doc.SourceDocument().FlowStartingPoints[0].NodeId == TEXT("0:2")
		&& Doc.SourceDocument().FlowStartingPoints[0].Name == TEXT("Doc Flow"));
	TestTrue(TEXT("figma fill blendMode preserved"), PaintNode->Visual.PrimaryFill().BlendMode == TEXT("MULTIPLY"));
	TestTrue(TEXT("figma fill opacity preserved"), FMath::IsNearlyEqual(PaintNode->Visual.PrimaryFill().Opacity, 0.5f, 0.001f));
	TestTrue(TEXT("figma effect blendMode preserved"), PaintNode->Visual.Effects.Num() > 0
		&& PaintNode->Visual.Effects[0].BlendMode == TEXT("SCREEN")
		&& PaintNode->Visual.Effects[0].Shadow.BlendMode == TEXT("SCREEN"));
	TestTrue(TEXT("figma source node type preserved"), InstanceNode->Source().SourceNodeType == TEXT("INSTANCE"));
	TestTrue(TEXT("figma instance componentId preserved"), InstanceNode->Metadata.FindRef(TEXT("instanceComponentId")) == TEXT("12:34"));
	TestTrue(TEXT("figma instance ext flag"), InstanceNode->Source().ComponentInstance.bIsInstance);
	TestTrue(TEXT("figma instance ext componentId"), InstanceNode->Source().ComponentInstance.ComponentId == TEXT("12:34"));
	TestTrue(TEXT("figma instance ext componentSetId"), InstanceNode->Source().ComponentInstance.ComponentSetId == TEXT("56:78"));
	TestTrue(TEXT("figma instance ext mainComponentKey"), InstanceNode->Source().ComponentInstance.MainComponentKey == TEXT("component-key-1"));
	TestTrue(TEXT("figma instance ext componentProperties"), !InstanceNode->Source().ComponentInstance.ComponentPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance ext variantProperties"), !InstanceNode->Source().ComponentInstance.VariantPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance ext overrides"), !InstanceNode->Source().ComponentInstance.OverridesJson.IsEmpty());
	TestTrue(TEXT("figma instance ext isExposedInstance"), InstanceNode->Source().ComponentInstance.IsExposedInstance.Get(false));
	TestTrue(TEXT("figma instance componentSetId preserved"), InstanceNode->Metadata.FindRef(TEXT("componentSetId")) == TEXT("56:78"));
	TestTrue(TEXT("figma instance mainComponentKey preserved"), InstanceNode->Metadata.FindRef(TEXT("mainComponentKey")) == TEXT("component-key-1"));
TestTrue(TEXT("figma instance componentProperties preserved"), InstanceNode->Metadata.Contains(TEXT("componentProperties")));
TestTrue(TEXT("figma instance variantProperties preserved"), InstanceNode->Metadata.Contains(TEXT("variantProperties")));
TestTrue(TEXT("figma instance overrides preserved"), InstanceNode->Metadata.Contains(TEXT("overrides")));
TestTrue(TEXT("figma instance isExposedInstance preserved"), InstanceNode->Metadata.FindRef(TEXT("isExposedInstance")) == TEXT("true"));

	const FUMGWidgetData ConvertedData = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("figma document components catalog exported"), ConvertedData.SourceDocument().ComponentsJson.Contains(TEXT("component-key-1")));
	TestTrue(TEXT("figma document componentSets catalog exported"), ConvertedData.SourceDocument().ComponentSetsJson.Contains(TEXT("component-set-key-1")));

	const FUMGWidgetNode* ConvertedInstanceWidget = FindWidgetById(ConvertedData.RootWidget, TEXT("inst1"));
	TestNotNull(TEXT("figma instance widget exported to UMG tree"), ConvertedInstanceWidget);
	if (!ConvertedInstanceWidget)
	{
		return false;
	}

	TestTrue(TEXT("figma instance typed node data exported"), ConvertedInstanceWidget->Source().IsInstance());
	TestEqual(TEXT("figma instance source node type exported"), ConvertedInstanceWidget->Source().SourceNodeType, TEXT("INSTANCE"));
	TestEqual(TEXT("figma instance componentId exported"), ConvertedInstanceWidget->Source().ComponentId, TEXT("12:34"));
	TestEqual(TEXT("figma instance componentSetId exported"), ConvertedInstanceWidget->Source().ComponentSetId, TEXT("56:78"));
	TestEqual(TEXT("figma instance mainComponentKey exported"), ConvertedInstanceWidget->Source().MainComponentKey, TEXT("component-key-1"));
	TestTrue(TEXT("figma instance componentProperties exported"), !ConvertedInstanceWidget->Source().ComponentPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance variantProperties exported"), !ConvertedInstanceWidget->Source().VariantPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance overrides exported"), !ConvertedInstanceWidget->Source().OverridesJson.IsEmpty());
	TestTrue(TEXT("figma instance exposed flag exported"),
		ConvertedInstanceWidget->Source().HasIsExposedInstance.IsSet()
			&& ConvertedInstanceWidget->Source().HasIsExposedInstance.GetValue());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTransformGroupPreservedTest,
	"Design2UMG.FigmaParser.TransformGroupPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTransformGroupPreservedTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "root",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 300, "height": 200},
					"children": [{
						"id": "tg1",
						"name": "Transform Group",
						"type": "TRANSFORM_GROUP",
						"visible": true,
						"absoluteBoundingBox": {"x": 24, "y": 16, "width": 120, "height": 48},
						"children": [{
							"id": "rect1",
							"name": "Card",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x": 24, "y": 16, "width": 120, "height": 48},
							"fills": [{
								"type": "SOLID",
								"visible": true,
								"color": {"r": 0.2, "g": 0.3, "b": 0.8, "a": 1.0}
							}]
						}]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Transform group parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& TransformGroup = Doc.Children[0]->Children[0];
	TestEqual(TEXT("TRANSFORM_GROUP maps to Group"), TransformGroup->Type, EDesignNodeType::Group);
	TestEqual(TEXT("TRANSFORM_GROUP source type preserved"), TransformGroup->Source().SourceNodeType, TEXT("TRANSFORM_GROUP"));
	TestEqual(TEXT("TRANSFORM_GROUP keeps child hierarchy"), TransformGroup->Children.Num(), 1);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* GroupWidget = FindWidgetById(Data.RootWidget, TEXT("tg1"));
	const FUMGWidgetNode* RectWidget = FindWidgetById(Data.RootWidget, TEXT("rect1"));
	TestTrue(TEXT("Transform group widget exists"), GroupWidget != nullptr);
	TestTrue(TEXT("Transform group child widget exists"), RectWidget != nullptr);
	if (!GroupWidget || !RectWidget)
	{
		return false;
	}

	TestEqual(TEXT("Transform group converts to container widget"), GroupWidget->Type, TEXT("CanvasPanel"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTextPathFallbackTest,
	"Design2UMG.FigmaParser.TextPathFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTextPathFallbackTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "root",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 320, "height": 200},
					"children": [{
						"id": "text_path_1",
						"name": "Curved Label",
						"type": "TEXT_PATH",
						"visible": true,
						"absoluteBoundingBox": {"x": 32, "y": 24, "width": 180, "height": 40},
						"characters": "Curved Title",
						"fills": [{
							"type": "SOLID",
							"visible": true,
							"color": {"r": 0.9, "g": 0.9, "b": 0.95, "a": 1.0}
						}],
						"style": {
							"fontFamily": "Inter",
							"fontSize": 18,
							"fontWeight": 400,
							"textAlignHorizontal": "CENTER",
							"textAlignVertical": "TOP",
							"lineHeightPx": 20
						}
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("TEXT_PATH parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& TextPathNode = Doc.Children[0]->Children[0];
	TestEqual(TEXT("TEXT_PATH maps to Text"), TextPathNode->Type, EDesignNodeType::Text);
	TestEqual(TEXT("TEXT_PATH source type preserved"), TextPathNode->Source().SourceNodeType, TEXT("TEXT_PATH"));
	TestEqual(TEXT("TEXT_PATH keeps text content"), TextPathNode->Text.Content, TEXT("Curved Title"));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* TextWidget = FindWidgetById(Data.RootWidget, TEXT("text_path_1"));
	TestTrue(TEXT("TEXT_PATH widget exists"), TextWidget != nullptr);
	if (!TextWidget)
	{
		return false;
	}

	TestEqual(TEXT("TEXT_PATH falls back to TextBlock"), TextWidget->Type, TEXT("TextBlock"));
	TestEqual(TEXT("TEXT_PATH fallback keeps content"), GetWidgetProperty(*TextWidget, TEXT("Text")), TEXT("Curved Title"));

	bool bFoundWarning = false;
	for (const FString& Warning : Data.Warnings)
	{
		if (Warning.Contains(TEXT("TEXT_PATH")) && Warning.Contains(TEXT("Curved Label")))
		{
			bFoundWarning = true;
			break;
		}
	}
	TestTrue(TEXT("TEXT_PATH fallback emits warning"), bFoundWarning);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSkipDesignOnlyNodesTest,
	"Design2UMG.FigmaParser.SkipDesignOnlyNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaSkipDesignOnlyNodesTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "root",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 320, "height": 200},
					"children": [
						{
							"id": "slice1",
							"name": "Export Slice",
							"type": "SLICE",
							"visible": true,
							"absoluteBoundingBox": {"x": 10, "y": 10, "width": 120, "height": 80}
						},
						{
							"id": "sticky1",
							"name": "FigJam Sticky",
							"type": "STICKY",
							"visible": true,
							"absoluteBoundingBox": {"x": 20, "y": 20, "width": 100, "height": 60}
						},
						{
							"id": "keep_text",
							"name": "Keep Text",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x": 32, "y": 24, "width": 90, "height": 24},
							"characters": "Keep me",
							"style": {
								"fontFamily": "Inter",
								"fontSize": 16,
								"fontWeight": 400
							}
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Design-only node fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& KeptNode = Doc.Children[0]->Children[0];
	TestEqual(TEXT("Only runtime text child remains"), KeptNode->Id, TEXT("keep_text"));
	TestEqual(TEXT("Skipped designer node count recorded"), Doc.ParseDiagnostics().SkippedDesignerNodes.Num(), 2);
	TestEqual(TEXT("First skipped node id recorded"), Doc.ParseDiagnostics().SkippedDesignerNodes[0].NodeId, TEXT("slice1"));
	TestEqual(TEXT("Second skipped node reason recorded"), Doc.ParseDiagnostics().SkippedDesignerNodes[1].Reason, TEXT("figjam_only"));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestNull(TEXT("SLICE node should be skipped"), FindWidgetById(Data.RootWidget, TEXT("slice1")));
	TestNull(TEXT("FigJam STICKY node should be skipped"), FindWidgetById(Data.RootWidget, TEXT("sticky1")));
	TestTrue(TEXT("Normal text node should remain"), FindWidgetById(Data.RootWidget, TEXT("keep_text")) != nullptr);
	bool bFoundSliceWarning = false;
	bool bFoundStickyWarning = false;
	for (const FString& Warning : Data.Warnings)
	{
		bFoundSliceWarning |= Warning.Contains(TEXT("SLICE")) && Warning.Contains(TEXT("slice1"));
		bFoundStickyWarning |= Warning.Contains(TEXT("STICKY")) && Warning.Contains(TEXT("sticky1"));
	}
	TestTrue(TEXT("SLICE skip emits warning"), bFoundSliceWarning);
	TestTrue(TEXT("STICKY skip emits warning"), bFoundStickyWarning);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLuminosityImageOverlayTest,
	"Design2UMG.FigmaParser.LuminosityImageOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLuminosityImageOverlayTest::RunTest(const FString& Parameters)
{
	FLuminosityOpacityCapScope LuminosityOpacityCapScope(0.15f);
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "node_lum",
					"name": "LumCard",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 80},
					"absoluteRenderBounds": {"x": 0, "y": 0, "width": 120, "height": 80},
					"fills": [
						{"type": "SOLID", "visible": true, "blendMode": "NORMAL", "color": {"r": 0.9254902, "g": 0.9019608, "b": 0.94117647, "a": 1}},
						{"type": "IMAGE", "visible": true, "blendMode": "LUMINOSITY", "imageRef": "img_lum", "scaleMode": "FIT", "scalingFactor": 0.5}
					],
					"children": []
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Doc.Children[0];
	TestEqual(TEXT("Luminosity placeholder keeps both fills"), Node->Visual.Fills.Num(), 2);
	TestEqual(TEXT("Base fill remains color"), Node->Visual.Fills[0].Type, EDesignFillType::Color);
	TestEqual(TEXT("Overlay fill remains image"), Node->Visual.Fills[1].Type, EDesignFillType::Image);
	TestEqual(TEXT("Overlay image mode preserved"), Node->Visual.Fills[1].Image.Mode, EDesignImageMode::Fit);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()));
	const FUMGWidgetNode* Overlay = FindWidgetById(Data.RootWidget, TEXT("node_lum"));
	TestTrue(TEXT("Converted widget found"), Overlay != nullptr);
	if (!Overlay)
	{
		return false;
	}

	TestEqual(TEXT("Luminosity image converts to multi-fill overlay"), Overlay->Type, TEXT("Overlay"));
	TestEqual(TEXT("Leaf multi-fill overlay keeps only base + image layers"), Overlay->Children.Num(), 2);
	if (Overlay->Children.Num() != 2)
	{
		return false;
	}

	const FUMGWidgetNode& BaseLayer = Overlay->Children[0];
	const FUMGWidgetNode& FitLayer = Overlay->Children[1];
	TestEqual(TEXT("Base layer is border"), BaseLayer.Type, TEXT("Border"));
	TestEqual(TEXT("Fit layer is scale box"), FitLayer.Type, TEXT("ScaleBox"));
	TestTrue(TEXT("Fit layer has image child"), FitLayer.Children.Num() == 1);
	if (FitLayer.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& ImageChild = FitLayer.Children[0];
	TestEqual(TEXT("Image child type"), ImageChild.Type, TEXT("Image"));
	TestEqual(TEXT("Image child keeps texture"), GetWidgetProperty(ImageChild, TEXT("TexturePath")), TEXT("img_lum"));
	TestEqual(TEXT("Fit layer stretch"), GetWidgetProperty(FitLayer, TEXT("Stretch")), TEXT("ScaleToFit"));
	TestFalse(TEXT("Luminosity fit layer should not pin synthetic pivot"), HasWidgetProperty(FitLayer, TEXT("RenderTransformPivot")));
	TestFalse(TEXT("Luminosity fit layer should not inject synthetic scale"), HasWidgetProperty(FitLayer, TEXT("RenderTransformScale")));
	TestFalse(TEXT("Luminosity fit layer should not inject synthetic translation"), HasWidgetProperty(FitLayer, TEXT("RenderTranslation")));
	TestTrue(TEXT("Luminosity fit image applies opacity fallback"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(ImageChild, TEXT("RenderOpacity"))), 0.15f, 0.0001f));
	TestFalse(TEXT("Non-square fit image child should not pin width override"), HasWidgetProperty(ImageChild, TEXT("WidthOverride")));
	TestFalse(TEXT("Non-square fit image child should not pin height override"), HasWidgetProperty(ImageChild, TEXT("HeightOverride")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaWideLuminosityImageOverlayTest,
	"Design2UMG.FigmaParser.WideLuminosityImageOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaWideLuminosityImageOverlayTest::RunTest(const FString& Parameters)
{
	FLuminosityOpacityCapScope LuminosityOpacityCapScope(0.15f);
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "overlay_wide",
					"name": "OverlayWide",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 220, "height": 120},
					"absoluteRenderBounds": {"x": 0, "y": 0, "width": 220, "height": 120},
					"fills": [
						{"type": "SOLID", "visible": true, "blendMode": "NORMAL", "color": {"r": 0.9254902, "g": 0.9019608, "b": 0.94117647, "a": 1}},
						{"type": "IMAGE", "visible": true, "blendMode": "LUMINOSITY", "imageRef": "img_lum_wide", "scaleMode": "FIT", "scalingFactor": 0.5}
					],
					"children": []
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Wide luminosity parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0)
	{
		return false;
	}

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()));
	const FUMGWidgetNode* Overlay = FindWidgetById(Data.RootWidget, TEXT("overlay_wide"));
	TestTrue(TEXT("Wide luminosity overlay widget exists"), Overlay != nullptr);
	if (!Overlay || Overlay->Children.Num() < 2)
	{
		return false;
	}

	const FUMGWidgetNode& FitLayer = Overlay->Children[1];
	TestEqual(TEXT("Wide fit layer is scale box"), FitLayer.Type, TEXT("ScaleBox"));
	TestTrue(TEXT("Wide fit layer has image child"), FitLayer.Children.Num() == 1);
	if (FitLayer.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& ImageChild = FitLayer.Children[0];
	TestFalse(TEXT("Wide luminosity fit layer should not inject synthetic scale"), HasWidgetProperty(FitLayer, TEXT("RenderTransformScale")));
	TestFalse(TEXT("Wide luminosity fit layer should not inject synthetic translation"), HasWidgetProperty(FitLayer, TEXT("RenderTranslation")));
	TestTrue(TEXT("Wide luminosity image applies opacity fallback"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(ImageChild, TEXT("RenderOpacity"))), 0.15f, 0.0001f));
	TestFalse(TEXT("Wide luminosity image should not pin width override"), HasWidgetProperty(ImageChild, TEXT("WidthOverride")));
	TestFalse(TEXT("Wide luminosity image should not pin height override"), HasWidgetProperty(ImageChild, TEXT("HeightOverride")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSquareLuminosityImageOverlayTest,
	"Design2UMG.FigmaParser.SquareLuminosityImageOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaSquareLuminosityImageOverlayTest::RunTest(const FString& Parameters)
{
	FLuminosityOpacityCapScope LuminosityOpacityCapScope(0.15f);
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "overlay_square",
					"name": "OverlaySquare",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
					"absoluteRenderBounds": {"x": 0, "y": 0, "width": 120, "height": 120},
					"fills": [
						{"type": "SOLID", "visible": true, "blendMode": "NORMAL", "color": {"r": 0.9254902, "g": 0.9019608, "b": 0.94117647, "a": 1}},
						{"type": "IMAGE", "visible": true, "blendMode": "LUMINOSITY", "imageRef": "img_lum_square", "scaleMode": "FIT", "imageTransform": [[1,0,0],[0,1,0]]}
					],
					"children": []
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Square luminosity parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0)
	{
		return false;
	}

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()));
	const FUMGWidgetNode* Overlay = FindWidgetById(Data.RootWidget, TEXT("overlay_square"));
	TestTrue(TEXT("Square luminosity overlay widget exists"), Overlay != nullptr);
	if (!Overlay)
	{
		return false;
	}

	TestEqual(TEXT("Square luminosity image converts to overlay"), Overlay->Type, TEXT("Overlay"));
	TestTrue(TEXT("Square luminosity overlay keeps image layer"), Overlay->Children.Num() >= 2);
	if (Overlay->Children.Num() < 2)
	{
		return false;
	}

	const FUMGWidgetNode& FitLayer = Overlay->Children[1];
	TestEqual(TEXT("Square fit layer is scale box"), FitLayer.Type, TEXT("ScaleBox"));
	TestTrue(TEXT("Square fit layer has image child"), FitLayer.Children.Num() == 1);
	if (FitLayer.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& ImageChild = FitLayer.Children[0];
	TestEqual(TEXT("Square image child keeps texture"), GetWidgetProperty(ImageChild, TEXT("TexturePath")), TEXT("img_lum_square"));
	TestEqual(TEXT("Square image child pins width"), GetWidgetProperty(ImageChild, TEXT("WidthOverride")), TEXT("120.0"));
	TestEqual(TEXT("Square image child pins height"), GetWidgetProperty(ImageChild, TEXT("HeightOverride")), TEXT("120.0"));
	TestTrue(TEXT("Square image child applies opacity fallback"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(ImageChild, TEXT("RenderOpacity"))), 0.15f, 0.0001f));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLuminosityIdentityTransformOverlayTest,
	"Design2UMG.FigmaParser.LuminosityIdentityTransformOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLuminosityIdentityTransformOverlayTest::RunTest(const FString& Parameters)
{
	FLuminosityOpacityCapScope LuminosityOpacityCapScope(0.15f);
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "overlay_id",
					"name": "OverlayIdentity",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 80},
					"absoluteRenderBounds": {"x": 0, "y": 0, "width": 120, "height": 80},
					"fills": [
						{"type": "SOLID", "visible": true, "blendMode": "NORMAL", "color": {"r": 0.9254902, "g": 0.9019608, "b": 0.94117647, "a": 1}},
						{"type": "IMAGE", "visible": true, "blendMode": "LUMINOSITY", "imageRef": "img_lum_id", "scaleMode": "FIT", "imageTransform": [[1,0,0],[0,1,0]]}
					],
					"children": []
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0)
	{
		return false;
	}

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()));
	const FUMGWidgetNode* Overlay = FindWidgetById(Data.RootWidget, TEXT("overlay_id"));
	TestTrue(TEXT("Identity transform overlay widget exists"), Overlay != nullptr);
	if (!Overlay)
	{
		return false;
	}

	TestEqual(TEXT("Identity transform luminosity image converts to overlay"), Overlay->Type, TEXT("Overlay"));
	TestTrue(TEXT("Identity transform overlay keeps image layer"), Overlay->Children.Num() >= 2);
	if (Overlay->Children.Num() < 2 || Overlay->Children[1].Children.Num() != 1)
	{
		return false;
	}
	const FUMGWidgetNode& FitLayer = Overlay->Children[1];
	const FUMGWidgetNode& ImageChild = FitLayer.Children[0];
	TestFalse(TEXT("Identity transform overlay should not inject synthetic scale"), HasWidgetProperty(FitLayer, TEXT("RenderTransformScale")));
	TestFalse(TEXT("Identity transform overlay should not inject synthetic translation"), HasWidgetProperty(FitLayer, TEXT("RenderTranslation")));
	TestTrue(TEXT("Identity transform overlay applies opacity fallback"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(ImageChild, TEXT("RenderOpacity"))), 0.15f, 0.0001f));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaVectorRasterFallbackTest,
	"Design2UMG.FigmaParser.VectorRasterFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaVectorRasterFallbackTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 400, "height": 400},
					"children": [{
						"id": "ellipse_grad",
						"name": "Ellipse Gradient",
						"type": "ELLIPSE",
						"visible": true,
						"absoluteBoundingBox": {"x": 20, "y": 20, "width": 320, "height": 320},
						"fillGeometry": [{"path":"M0 160C0 71.6 71.6 0 160 0C248.4 0 320 71.6 320 160C320 248.4 248.4 320 160 320C71.6 320 0 248.4 0 160Z","windingRule":"NONZERO"}],
						"fills": [
							{"type":"GRADIENT_LINEAR","visible":true,"gradientStops":[
								{"position":0.0,"color":{"r":1,"g":0,"b":0,"a":1}},
								{"position":1.0,"color":{"r":0,"g":0,"b":1,"a":1}}
							],"gradientHandlePositions":[{"x":0,"y":0.5},{"x":1,"y":0.5}]},
							{"type":"IMAGE","visible":true,"imageRef":"vec_grad_1","scaleMode":"FILL"}
						],
						"children": []
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma vector fallback parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& EllipseNode = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Vector raster fallback captured"), EllipseNode->Visual.VectorRasterFallbackImageRef == TEXT("vec_grad_1"));

	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Doc, ImageRefs);
	TestTrue(TEXT("CollectImageRefs includes vector fallback"), ImageRefs.Contains(TEXT("vec_grad_1")));

	TMap<FString, FString> ImageMap;
	ImageMap.Add(TEXT("vec_grad_1"), TEXT("/Game/Transient/T_vec_grad_1.T_vec_grad_1"));
	FFigmaParser::ApplyImageUrls(Doc, ImageMap);
	TestTrue(TEXT("ApplyImageUrls remaps vector fallback"), EllipseNode->Visual.VectorRasterFallbackImageRef == TEXT("/Game/Transient/T_vec_grad_1.T_vec_grad_1"));

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("ellipse_grad"));
	TestTrue(TEXT("Gradient ellipse widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	TestEqual(TEXT("Gradient ellipse uses image fallback"), Widget->Type, TEXT("Image"));
	TestEqual(TEXT("Gradient ellipse texture path preserved"), GetWidgetProperty(*Widget, TEXT("TexturePath")), TEXT("/Game/Transient/T_vec_grad_1.T_vec_grad_1"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaRenderFallbackMetadataRoundTripTest,
	"Design2UMG.FigmaParser.RenderFallbackMetadataRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaRenderFallbackMetadataRoundTripTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 300, "height": 200},
					"children": [{
						"id": "render_leaf",
						"name": "Image Tile",
						"type": "FRAME",
						"visible": true,
						"absoluteBoundingBox": {"x": 24, "y": 16, "width": 160, "height": 96},
						"absoluteRenderBounds": {"x": 24, "y": 16, "width": 160, "height": 96},
						"fills": [
							{"type":"SOLID","visible":true,"blendMode":"NORMAL","color":{"r":0.2,"g":0.2,"b":0.2,"a":1}},
							{"type":"IMAGE","visible":true,"blendMode":"LUMINOSITY","imageRef":"img_orig","scaleMode":"FIT","filters":{"contrast":0.25}}
						],
						"design2umgRenderFallbackImageRef": "render_202_5356",
						"design2umgRenderFallbackScope": "full",
						"design2umgRenderFallbackSource": "figma_render_png",
						"design2umgRenderFallbackReason": "image_filters+image_blend_luminosity",
						"interactions": {"unexpected":"object"},
						"children": []
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Render fallback parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& RenderNode = Doc.Children[0]->Children[0];
	TestEqual(TEXT("Render fallback image ref normalized"), RenderNode->Visual.RenderFallback.ImageRef, TEXT("render_202_5356"));
	TestEqual(TEXT("Render fallback scope normalized"), RenderNode->Visual.RenderFallback.Scope, EDesignRenderFallbackScope::Leaf);
	TestEqual(TEXT("Render fallback source preserved"), RenderNode->Metadata.FindRef(TEXT("design2umgRenderFallbackSource")), TEXT("figma_render_png"));
	TestEqual(TEXT("Render fallback reason preserved"), RenderNode->Metadata.FindRef(TEXT("design2umgRenderFallbackReason")), TEXT("image_filters+image_blend_luminosity"));
	TestTrue(TEXT("Non-array interactions remain metadata"), RenderNode->Metadata.Contains(TEXT("interactions")));
	TestFalse(TEXT("Non-array interactions do not suppress render fallback"), RenderNode->Visual.RenderFallback.bSuppressedBySourceSemantics);

	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Doc, ImageRefs);
	TestTrue(TEXT("CollectImageRefs includes original image"), ImageRefs.Contains(TEXT("img_orig")));
	TestTrue(TEXT("CollectImageRefs includes render fallback image"), ImageRefs.Contains(TEXT("render_202_5356")));

	TMap<FString, FString> ImageMap;
	ImageMap.Add(TEXT("img_orig"), TEXT("/Game/Transient/T_img_orig.T_img_orig"));
	ImageMap.Add(TEXT("render_202_5356"), TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356"));
	FFigmaParser::ApplyImageUrls(Doc, ImageMap);
	TestEqual(TEXT("ApplyImageUrls remaps render fallback image"), RenderNode->Visual.RenderFallback.ImageRef, TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaStructuredLeafRenderFallbackWidgetTest,
	"Design2UMG.FigmaParser.StructuredLeafRenderFallbackWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaStructuredLeafRenderFallbackWidgetTest::RunTest(const FString& Parameters)
{
	FDesignDocument Doc;
	TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
	Root->Id = TEXT("root");
	Root->Name = TEXT("Root");
	Root->Type = EDesignNodeType::Frame;
	Root->LayoutData.Layout = EDesignLayout::None;
	Root->Transform.ExplicitX = 0.0f;
	Root->Transform.ExplicitY = 0.0f;
	Root->Transform.X = 0.0f;
	Root->Transform.Y = 0.0f;
	Root->Transform.Width = FDesignSize::Fixed(100.0f);
	Root->Transform.Height = FDesignSize::Fixed(80.0f);

	TSharedPtr<FDesignNode> Leaf = MakeShared<FDesignNode>();
	Leaf->Id = TEXT("render_leaf_structured");
	Leaf->Name = TEXT("Image Tile Structured");
	Leaf->Type = EDesignNodeType::Frame;
	Leaf->LayoutData.Layout = EDesignLayout::None;
	Leaf->Transform.ExplicitX = 10.0f;
	Leaf->Transform.ExplicitY = 12.0f;
	Leaf->Transform.X = 10.0f;
	Leaf->Transform.Y = 12.0f;
	Leaf->Transform.Width = FDesignSize::Fixed(26.0f);
	Leaf->Transform.Height = FDesignSize::Fixed(26.0f);
	Leaf->MutableSource().SourceNodeType = TEXT("FRAME");
	FDesignFill& LeafFill = Leaf->Visual.Fills.AddDefaulted_GetRef();
	LeafFill.Type = EDesignFillType::Image;
	LeafFill.bEnabled = true;
	LeafFill.Image.Url = TEXT("img_avatar");
	LeafFill.Image.Mode = EDesignImageMode::Fit;
	Leaf->Visual.RenderFallback.ImageRef = TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356");
	Root->Children.Add(Leaf);
	Doc.Children.Add(Root);

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("render_leaf_structured"));
	TestTrue(TEXT("Structured render fallback widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	TestEqual(TEXT("Structured render fallback converts directly to image"), Widget->Type, TEXT("Image"));
	TestEqual(TEXT("Structured render fallback keeps texture path"), GetWidgetProperty(*Widget, TEXT("TexturePath")), TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356"));
	TestTrue(TEXT("Structured render fallback keeps design X"), FMath::IsNearlyEqual(Widget->Bounds.X, 10.0f, 0.001f));
	TestTrue(TEXT("Structured render fallback keeps design Y"), FMath::IsNearlyEqual(Widget->Bounds.Y, 12.0f, 0.001f));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLeafRenderFallbackWidgetTest,
	"Design2UMG.FigmaParser.LeafRenderFallbackWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLeafRenderFallbackWidgetTest::RunTest(const FString& Parameters)
{
	FDesignDocument Doc;
	TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
	Root->Id = TEXT("root");
	Root->Name = TEXT("Root");
	Root->Type = EDesignNodeType::Frame;
	Root->LayoutData.Layout = EDesignLayout::None;
	Root->Transform.ExplicitX = 0.0f;
	Root->Transform.ExplicitY = 0.0f;
	Root->Transform.X = 0.0f;
	Root->Transform.Y = 0.0f;
	Root->Transform.Width = FDesignSize::Fixed(100.0f);
	Root->Transform.Height = FDesignSize::Fixed(80.0f);

	TSharedPtr<FDesignNode> Leaf = MakeShared<FDesignNode>();
	Leaf->Id = TEXT("render_leaf");
	Leaf->Name = TEXT("Image Tile");
	Leaf->Type = EDesignNodeType::Frame;
	Leaf->LayoutData.Layout = EDesignLayout::None;
	Leaf->Transform.ExplicitX = 10.0f;
	Leaf->Transform.ExplicitY = 12.0f;
	Leaf->Transform.X = 10.0f;
	Leaf->Transform.Y = 12.0f;
	Leaf->Transform.Width = FDesignSize::Fixed(26.0f);
	Leaf->Transform.Height = FDesignSize::Fixed(26.0f);
	FDesignFill& LeafFill = Leaf->Visual.Fills.AddDefaulted_GetRef();
	LeafFill.Type = EDesignFillType::Image;
	LeafFill.bEnabled = true;
	LeafFill.Image.Url = TEXT("img_avatar");
	LeafFill.Image.Mode = EDesignImageMode::Fit;
	Leaf->Visual.RenderFallback.ImageRef = TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356");
	Root->Children.Add(Leaf);
	Doc.Children.Add(Root);

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("render_leaf"));
	TestTrue(TEXT("Leaf render fallback widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	TestEqual(TEXT("Leaf render fallback converts directly to image"), Widget->Type, TEXT("Image"));
	TestEqual(TEXT("Leaf render fallback keeps texture path"), GetWidgetProperty(*Widget, TEXT("TexturePath")), TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356"));
	const FString WidthOverrideValue = GetWidgetProperty(*Widget, TEXT("WidthOverride"));
	const FString HeightOverrideValue = GetWidgetProperty(*Widget, TEXT("HeightOverride"));
	const float WidthOverride = FCString::Atof(*WidthOverrideValue);
	const float HeightOverride = FCString::Atof(*HeightOverrideValue);
	TestFalse(TEXT("Leaf render fallback exposes typed width override"), WidthOverrideValue.IsEmpty());
	TestFalse(TEXT("Leaf render fallback exposes typed height override"), HeightOverrideValue.IsEmpty());
	TestTrue(TEXT("Leaf render fallback keeps design width override"), FMath::IsNearlyEqual(WidthOverride, 26.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback keeps design height override"), FMath::IsNearlyEqual(HeightOverride, 26.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback keeps design X"), FMath::IsNearlyEqual(Widget->Bounds.X, 10.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback keeps design Y"), FMath::IsNearlyEqual(Widget->Bounds.Y, 12.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback widget width uses design bounds"), FMath::IsNearlyEqual(Widget->Bounds.W, 26.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback widget height uses design bounds"), FMath::IsNearlyEqual(Widget->Bounds.H, 26.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback keeps no structural children"), Widget->Children.Num() == 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaRenderFallbackKeepsChildTreeTest,
	"Design2UMG.FigmaParser.RenderFallbackKeepsChildTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaRenderFallbackKeepsChildTreeTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "render_parent",
					"name": "Card",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 260, "height": 160},
					"absoluteRenderBounds": {"x": 0, "y": 0, "width": 260, "height": 160},
					"fills": [
						{"type":"SOLID","visible":true,"blendMode":"NORMAL","color":{"r":0.2,"g":0.2,"b":0.2,"a":1}},
						{"type":"IMAGE","visible":true,"blendMode":"LUMINOSITY","imageRef":"img_orig","scaleMode":"FIT","filters":{"contrast":0.25}}
					],
					"design2umgRenderFallbackImageRef": "/Game/Transient/T_render_parent.T_render_parent",
					"design2umgRenderFallbackScope": "full",
					"design2umgRenderFallbackSource": "figma_render_png",
					"design2umgRenderFallbackReason": "image_filters+image_blend_luminosity",
					"children": [{
						"id": "render_child",
						"name": "Title",
						"type": "TEXT",
						"visible": true,
						"absoluteBoundingBox": {"x": 20, "y": 18, "width": 80, "height": 24},
						"characters": "Hello",
						"style": {
							"fontFamily": "Inter",
							"fontSize": 16,
							"fontWeight": 400,
							"textAlignHorizontal": "LEFT",
							"textAlignVertical": "TOP",
							"textCase": "ORIGINAL",
							"textDecoration": "NONE",
							"letterSpacing": 0,
							"lineHeightUnit": "AUTO",
							"lineHeightPx": 0
						}
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Render fallback parent parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* ParentWidget = FindWidgetById(Data.RootWidget, TEXT("render_parent"));
	const FUMGWidgetNode* ChildWidget = FindWidgetById(Data.RootWidget, TEXT("render_child"));
	TestTrue(TEXT("Render fallback parent widget exists"), ParentWidget != nullptr);
	TestTrue(TEXT("Render fallback child widget exists"), ChildWidget != nullptr);
	if (!ParentWidget || !ChildWidget)
	{
		return false;
	}

	TestFalse(TEXT("Render fallback parent with children must not flatten to image"), ParentWidget->Type == TEXT("Image"));
	TestEqual(TEXT("Child text preserved"), GetWidgetProperty(*ChildWidget, TEXT("Text")), TEXT("Hello"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExportedImageClearsExplicitFillTest,
	"Design2UMG.FigmaParser.ExportedImageClearsExplicitFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaExportedImageClearsExplicitFillTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("remote_vector");
	Node->Type = EDesignNodeType::Path;
	Node->Visual.bFillExplicitClear = true;

	TMap<FString, FString> IdToUrl;
	IdToUrl.Add(Node->Id, TEXT("https://cdn.example.com/remote-vector.png"));
	TArray<TSharedPtr<FDesignNode>> Nodes;
	Nodes.Add(Node);

	FFigmaImageExporter::ApplyExportedImages(Nodes, IdToUrl);

	TestEqual(TEXT("Exported vector becomes a rectangle"), Node->Type, EDesignNodeType::Rectangle);
	TestFalse(TEXT("Exported image clears the stale explicit fill flag"), Node->Visual.bFillExplicitClear);
	TestEqual(TEXT("Exported vector has one image fill"), Node->Visual.Fills.Num(), 1);
	if (Node->Visual.Fills.Num() == 1)
	{
		TestTrue(TEXT("Exported fill keeps the resolved image URL"), Node->Visual.Fills[0].IsImage());
		TestEqual(TEXT("Exported fill URL is preserved"), Node->Visual.Fills[0].Image.Url, TEXT("https://cdn.example.com/remote-vector.png"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExtractKeyTest,
	"Design2UMG.FigmaParser.ExtractFileKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaExtractKeyTest::RunTest(const FString& Parameters)
{
	FString Key;
	TestTrue(TEXT("Design URL"), FFigmaParser::ExtractFileKey(TEXT("https://www.figma.com/design/abc123XYZ/MyFile"), Key));
	TestEqual(TEXT("Design URL key"), Key, TEXT("abc123XYZ"));
	TestTrue(TEXT("File URL"), FFigmaParser::ExtractFileKey(TEXT("https://www.figma.com/file/def456/OtherFile?node-id=1:2"), Key));
	TestEqual(TEXT("File URL key"), Key, TEXT("def456"));
	TestTrue(TEXT("Raw key"), FFigmaParser::ExtractFileKey(TEXT("abc123XYZ"), Key));
	TestEqual(TEXT("Raw key value"), Key, TEXT("abc123XYZ"));
	TestFalse(TEXT("Invalid URL"), FFigmaParser::ExtractFileKey(TEXT("https://example.com/not/figma"), Key));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaComplexConverterTest,
	"Design2UMG.FigmaParser.ComplexEndToEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaComplexConverterTest::RunTest(const FString& Parameters)
{
	FString Json;
	TestTrue(TEXT("Load FigmaComplex fixture"), LoadFixtureJson(TEXT("FigmaComplex.json"), Json));
	if (Json.IsEmpty())
		return false;
	FDesignDocument Doc;
	TestTrue(TEXT("E2E parse"), FFigmaParser::ParseDocument(Json, Doc));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestFalse(TEXT("E2E root type"), Data.RootWidget.Type.IsEmpty());
	TestTrue(TEXT("E2E has children"), Data.RootWidget.Children.Num() > 0);
	// Verify that key text widgets survive end-to-end conversion.
	TArray<FString> Texts;
	CollectTextValues(Data.RootWidget, Texts);
	TestTrue(TEXT("Contains Dashboard Overview"), Texts.Contains(TEXT("Dashboard Overview")));
	TestTrue(TEXT("Contains $48,290"), Texts.Contains(TEXT("$48,290")));
	TestTrue(TEXT("Contains Overview"), Texts.Contains(TEXT("Overview")));
	// CollectImageRefs should capture image references used by the fixture.
	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Doc, ImageRefs);
	TestTrue(TEXT("Has avatar imageRef"), ImageRefs.Contains(TEXT("img_avatar_001")));
	TestTrue(TEXT("Has card bg imageRef"), ImageRefs.Contains(TEXT("img_card_bg_002")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaRealDashboardTest,
	"Design2UMG.FigmaParser.RealDashboard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaRealDashboardTest::RunTest(const FString& Parameters)
{
	FString Json;
	TestTrue(TEXT("Load FigmaRealDashboard fixture"), LoadFixtureJson(TEXT("FigmaRealDashboard.json"), Json));
	if (Json.IsEmpty())
		return false;
	FDesignDocument Doc;
	TestTrue(TEXT("Real dashboard parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestTrue(TEXT("Has top-level children"), Doc.Children.Num() > 0);
	if (Doc.Children.Num() == 0)
		return false;
	const auto& Root = Doc.Children[0];
	TestEqual(TEXT("Root type is Frame"), Root->Type, EDesignNodeType::Frame);
	TestTrue(TEXT("Root has children"), Root->Children.Num() > 0);
	// Count node types to validate parser coverage on a real dashboard fixture.
	TMap<EDesignNodeType, int32> TypeCounts;
	TFunction<void(const TSharedPtr<FDesignNode>&)> CountNodes = [&](const TSharedPtr<FDesignNode>& Node)
	{
		TypeCounts.FindOrAdd(Node->Type)++;
		for (const auto& Child : Node->Children)
			CountNodes(Child);
	};
	CountNodes(Root);
	int32 TotalNodes = 0;
	for (const auto& Pair : TypeCounts)
		TotalNodes += Pair.Value;
	TestTrue(TEXT("Total nodes > 50"), TotalNodes > 50);
	TestTrue(TEXT("Has Frame nodes"), TypeCounts.Contains(EDesignNodeType::Frame));
	TestTrue(TEXT("Has Text nodes"), TypeCounts.Contains(EDesignNodeType::Text));
	TestTrue(TEXT("Has Rectangle nodes"), TypeCounts.Contains(EDesignNodeType::Rectangle));
	// Run the full end-to-end conversion on the parsed dashboard.
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestFalse(TEXT("Widget root type not empty"), Data.RootWidget.Type.IsEmpty());
	TestTrue(TEXT("Widget has children"), Data.RootWidget.Children.Num() > 0);
	// Verify that text content still exists after conversion.
	TArray<FString> Texts;
	CollectTextValues(Data.RootWidget, Texts);
	TestTrue(TEXT("Has text content"), Texts.Num() > 0);
	return true;
}
