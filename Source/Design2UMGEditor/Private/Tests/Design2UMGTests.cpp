// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "Design2UMGSettings.h"
#include "LayoutCalculator.h"
#include "FigmaParser.h"

#if WITH_AUTOMATION_WORKER

namespace
{
	const FUMGWidgetNode* FindWidgetById(const FUMGWidgetNode& Node, const FString& TargetId)
	{
		if (Node.Id == TargetId)
			return &Node;
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Found = FindWidgetById(Child, TargetId))
				return Found;
		}
		return nullptr;
	}

	const FUMGWidgetNode* FindWidgetByProperty(
		const FUMGWidgetNode& Node,
		const FString& Key,
		const FString* OptionalValue = nullptr)
	{
		if (const FString* Current = Node.Properties.Find(Key))
		{
			if (!OptionalValue || *Current == *OptionalValue)
				return &Node;
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Found = FindWidgetByProperty(Child, Key, OptionalValue))
				return Found;
		}
		return nullptr;
	}

	void CollectTextValues(const FUMGWidgetNode& Node, TArray<FString>& OutTexts)
	{
		if (Node.Type == TEXT("TextBlock"))
		{
			if (const FString* Text = Node.Properties.Find(TEXT("Text")))
				OutTexts.Add(*Text);
		}
		for (const FUMGWidgetNode& Child : Node.Children)
			CollectTextValues(Child, OutTexts);
	}
}

// ============================================================
// Test 1: Parser - 解析硬编码 JSON，验证节点类型/数量/属性
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenParserTest,
	"Design2UMG.Parser.BasicDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "screen1",
				"type": "frame",
				"name": "MainScreen",
				"width": 1920,
				"height": 1080,
				"layout": "vertical",
				"gap": 16,
				"fill": "#1A1A2E",
				"children": [
					{
						"id": "title1",
						"type": "text",
						"content": "Hello World",
						"fontSize": 24,
						"fontWeight": "bold",
						"fill": "#FFFFFF"
					},
					{
						"id": "ellipse1",
						"type": "ellipse",
						"width": 100,
						"height": 100,
						"fill": "#E94560"
					},
					{
						"id": "rect1",
						"type": "rectangle",
						"width": 200,
						"height": 50,
						"fill": "#0F3460",
						"cornerRadius": 8
					}
				]
			}
		]
	})");

	FPenDocument Doc;
	const bool bParsed = FPenParser::ParseDocument(Json, Doc);
	TestTrue(TEXT("ParseDocument succeeds"), bParsed);
	if (!bParsed || Doc.Children.Num() == 0)
		return false;
	TestEqual(TEXT("Top-level children count"), Doc.Children.Num(), 1);

	// Frame 节点
	const TSharedPtr<FPenNode>& Screen = Doc.Children[0];
	TestTrue(TEXT("Screen node valid"), Screen.IsValid());
	if (!Screen.IsValid())
		return false;
	TestEqual(TEXT("Screen type"), Screen->Type, EPenNodeType::Frame);
	TestEqual(TEXT("Screen name"), Screen->Name, TEXT("MainScreen"));
	TestTrue(TEXT("Screen width fixed"), Screen->Width.IsFixed());
	TestEqual(TEXT("Screen width value"), Screen->Width.Value, 1920.0f);
	TestEqual(TEXT("Screen layout"), Screen->Layout, EPenLayout::Vertical);
	TestEqual(TEXT("Screen gap"), Screen->Gap, 16.0f);
	TestEqual(TEXT("Screen children count"), Screen->Children.Num(), 3);
	if (Screen->Children.Num() < 3)
		return false;

	// Text 节点
	const TSharedPtr<FPenNode>& Title = Screen->Children[0];
	TestEqual(TEXT("Title type"), Title->Type, EPenNodeType::Text);
	TestEqual(TEXT("Title content"), Title->Content, TEXT("Hello World"));
	TestEqual(TEXT("Title fontSize"), Title->FontSize, 24.0f);
	TestEqual(TEXT("Title fontWeight"), Title->FontWeight, TEXT("bold"));

	// Ellipse 节点
	const TSharedPtr<FPenNode>& Ellipse = Screen->Children[1];
	TestEqual(TEXT("Ellipse type"), Ellipse->Type, EPenNodeType::Ellipse);

	// Rectangle 节点
	const TSharedPtr<FPenNode>& Rect = Screen->Children[2];
	TestEqual(TEXT("Rect type"), Rect->Type, EPenNodeType::Rectangle);
	TestTrue(TEXT("Rect has corner radius"), Rect->CornerRadius.HasRadius());

	return true;
}

// ============================================================
// Test 2: Converter - AST 转 Widget 树，验证结构和降级警告
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenConverterTest,
	"Design2UMG.Converter.BasicConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"name": "TestFrame",
				"width": 800,
				"height": 600,
				"layout": "vertical",
				"children": [
					{
						"id": "txt",
						"type": "text",
						"content": "Label",
						"fontSize": 16
					},
					{
						"id": "ell",
						"type": "ellipse",
						"width": 64,
						"height": 64,
						"fill": "#FF0000"
					}
				]
			}
		]
	})");

	FPenDocument Doc;
	TestTrue(TEXT("Parse succeeds"), FPenParser::ParseDocument(Json, Doc));

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	// Root widget 应该存在
	TestFalse(TEXT("Root type not empty"), Data.RootWidget.Type.IsEmpty());
	TestTrue(TEXT("Root has children"), Data.RootWidget.Children.Num() > 0);

	// Ellipse 用圆角近似，正圆完美匹配，不产生降级警告
	bool bFoundEllipseWarning = false;
	for (const FString& W : Data.Warnings)
	{
		if (W.Contains(TEXT("Ellipse")) || W.Contains(TEXT("ellipse")))
		{
			bFoundEllipseWarning = true;
			break;
		}
	}
	TestFalse(TEXT("Ellipse should not produce warning"), bFoundEllipseWarning);

	return true;
}

// ============================================================
// Test 3: LayoutCalculator - 9宫格锚点选择逻辑
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutCalculatorTest,
	"Design2UMG.Layout.NineGridAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLayoutCalculatorTest::RunTest(const FString& Parameters)
{
	FLayoutContext Ctx;
	Ctx.DesignSize = FIntPoint(1920, 1080);

	// Case 1: 左上角元素 → 锚点 (0,0)
	{
		FBounds B;
		B.X = 50.0f / 1920.0f; B.Y = 30.0f / 1080.0f;
		B.W = 200.0f / 1920.0f; B.H = 100.0f / 1080.0f;
		FUMGSlotData Slot = FLayoutCalculator::CalculateSlot(B, Ctx);
		TestEqual(TEXT("TopLeft anchor MinX"), Slot.Anchors.Min.X, 0.0f);
		TestEqual(TEXT("TopLeft anchor MinY"), Slot.Anchors.Min.Y, 0.0f);
	}

	// Case 2: 居中元素 → 锚点 (0.5, 0.5)
	{
		FBounds B;
		B.W = 300.0f / 1920.0f; B.H = 200.0f / 1080.0f;
		B.X = (1.0f - B.W) / 2.0f;
		B.Y = (1.0f - B.H) / 2.0f;
		FUMGSlotData Slot = FLayoutCalculator::CalculateSlot(B, Ctx);
		TestEqual(TEXT("Center anchor MinX"), Slot.Anchors.Min.X, 0.5f);
		TestEqual(TEXT("Center anchor MinY"), Slot.Anchors.Min.Y, 0.5f);
	}

	// Case 3: 右下角元素 → 锚点 (1,1)
	{
		FBounds B;
		B.W = 200.0f / 1920.0f; B.H = 100.0f / 1080.0f;
		B.X = 1.0f - B.W - 30.0f / 1920.0f;
		B.Y = 1.0f - B.H - 20.0f / 1080.0f;
		FUMGSlotData Slot = FLayoutCalculator::CalculateSlot(B, Ctx);
		TestEqual(TEXT("BottomRight anchor MinX"), Slot.Anchors.Min.X, 1.0f);
		TestEqual(TEXT("BottomRight anchor MinY"), Slot.Anchors.Min.Y, 1.0f);
	}

	// Case 4: 宽度超过 60% 画布 → 拉伸锚点 (0,0)-(1,0)
	{
		FBounds B;
		B.X = 50.0f / 1920.0f; B.Y = 50.0f / 1080.0f;
		B.W = 1800.0f / 1920.0f; B.H = 100.0f / 1080.0f;  // ~93.75% 宽度
		FUMGSlotData Slot = FLayoutCalculator::CalculateSlot(B, Ctx);
		TestTrue(TEXT("Stretch anchor: MinX < MaxX"),
			Slot.Anchors.Min.X < Slot.Anchors.Max.X);
	}

	return true;
}

// ============================================================
// Test 4: FigmaParser - Figma JSON → FPenDocument
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaParserTest,
	"Design2UMG.FigmaParser.BasicDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserTest::RunTest(const FString& Parameters)
{
	// 模拟 Figma GET /v1/files/:key 返回的 JSON（精简版）
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Document",
			"type": "DOCUMENT",
			"children": [
				{
					"id": "0:1",
					"name": "Page 1",
					"type": "CANVAS",
					"children": [
						{
							"id": "1:2",
							"name": "LoginScreen",
							"type": "FRAME",
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 1920, "height": 1080},
							"layoutMode": "VERTICAL",
							"itemSpacing": 24,
							"paddingLeft": 32, "paddingRight": 32,
							"paddingTop": 48, "paddingBottom": 48,
							"primaryAxisAlignItems": "CENTER",
							"counterAxisAlignItems": "CENTER",
							"clipsContent": true,
							"fills": [{"type": "SOLID", "color": {"r": 0.1, "g": 0.1, "b": 0.18, "a": 1}}],
							"cornerRadius": 0,
							"children": [
								{
									"id": "1:3",
									"name": "Title",
									"type": "TEXT",
									"characters": "Welcome Back",
									"style": {
										"fontFamily": "Inter",
										"fontSize": 32,
										"fontWeight": 700,
										"lineHeightPx": 40,
										"lineHeightUnit": "PIXELS",
										"letterSpacing": -0.5,
										"textAlignHorizontal": "CENTER",
										"textAlignVertical": "TOP"
									},
									"absoluteBoundingBox": {"x": 100, "y": 48, "width": 300, "height": 40},
									"layoutSizingHorizontal": "HUG",
									"layoutSizingVertical": "HUG",
									"fills": [{"type": "SOLID", "color": {"r": 1, "g": 1, "b": 1, "a": 1}}]
								},
								{
									"id": "1:4",
									"name": "InputBox",
									"type": "RECTANGLE",
									"absoluteBoundingBox": {"x": 32, "y": 112, "width": 400, "height": 48},
									"layoutSizingHorizontal": "FILL",
									"layoutSizingVertical": "FIXED",
									"fills": [{"type": "SOLID", "color": {"r": 0.2, "g": 0.2, "b": 0.25, "a": 1}}],
									"cornerRadius": 8,
									"strokes": [{"type": "SOLID", "color": {"r": 0.4, "g": 0.4, "b": 0.5, "a": 1}, "visible": true}],
									"strokeWeight": 1,
									"strokeAlign": "INSIDE"
								},
								{
									"id": "1:5",
									"name": "Avatar",
									"type": "ELLIPSE",
									"absoluteBoundingBox": {"x": 200, "y": 180, "width": 80, "height": 80},
									"layoutSizingHorizontal": "FIXED",
									"layoutSizingVertical": "FIXED",
									"fills": [{"type": "SOLID", "color": {"r": 0.91, "g": 0.27, "b": 0.38, "a": 1}}]
								},
								{
									"id": "1:6",
									"name": "Shadow Box",
									"type": "RECTANGLE",
									"absoluteBoundingBox": {"x": 32, "y": 280, "width": 400, "height": 100},
									"layoutSizingHorizontal": "FILL",
									"layoutSizingVertical": "FIXED",
									"fills": [{"type": "SOLID", "color": {"r": 0.15, "g": 0.15, "b": 0.2, "a": 1}}],
									"effects": [
										{
											"type": "DROP_SHADOW",
											"visible": true,
											"color": {"r": 0, "g": 0, "b": 0, "a": 0.25},
											"offset": {"x": 0, "y": 4},
											"radius": 12,
											"spread": 0
										}
									],
									"rectangleCornerRadii": [12, 12, 0, 0]
								}
							]
						}
					]
				}
			]
		},
		"components": {
		},
		"componentSets": {
		}
	})");

	FPenDocument Doc;
	const bool bParsed = FFigmaParser::ParseDocument(Json, Doc);
	TestTrue(TEXT("Figma ParseDocument succeeds"), bParsed);
	if (!bParsed || Doc.Children.Num() == 0)
		return false;
	TestEqual(TEXT("Figma top-level children"), Doc.Children.Num(), 1);

	// Frame
	const TSharedPtr<FPenNode>& Screen = Doc.Children[0];
	TestTrue(TEXT("Figma screen valid"), Screen.IsValid());
	if (!Screen.IsValid())
		return false;
	TestEqual(TEXT("Figma screen type"), Screen->Type, EPenNodeType::Frame);
	TestEqual(TEXT("Figma screen name"), Screen->Name, TEXT("LoginScreen"));
	TestEqual(TEXT("Figma layout"), Screen->Layout, EPenLayout::Vertical);
	TestEqual(TEXT("Figma gap"), Screen->Gap, 24.0f);
	TestEqual(TEXT("Figma padding left"), Screen->Padding.Left, 32.0f);
	TestEqual(TEXT("Figma padding top"), Screen->Padding.Top, 48.0f);
	TestEqual(TEXT("Figma justify"), Screen->JustifyContent, EPenJustifyContent::Center);
	TestEqual(TEXT("Figma align"), Screen->AlignItems, EPenAlignItems::Center);
	TestTrue(TEXT("Figma clip"), Screen->bClip);
	TestEqual(TEXT("Figma children count"), Screen->Children.Num(), 4);
	if (Screen->Children.Num() < 4)
		return false;

	// Text
	const TSharedPtr<FPenNode>& Title = Screen->Children[0];
	TestEqual(TEXT("Figma title type"), Title->Type, EPenNodeType::Text);
	TestEqual(TEXT("Figma title content"), Title->Content, TEXT("Welcome Back"));
	TestEqual(TEXT("Figma title font"), Title->FontFamily, TEXT("Inter"));
	TestEqual(TEXT("Figma title fontSize"), Title->FontSize, 32.0f);
	TestEqual(TEXT("Figma title fontWeight"), Title->FontWeight, TEXT("bold"));
	TestTrue(TEXT("Figma title lineHeight > 0"), Title->LineHeight > 0.0f);
	TestEqual(TEXT("Figma title textAlign"), Title->TextAlign, EPenTextAlign::Center);
	TestTrue(TEXT("Figma title width HUG"), Title->Width.IsFit());

	// Rectangle with stroke
	const TSharedPtr<FPenNode>& Input = Screen->Children[1];
	TestEqual(TEXT("Figma input type"), Input->Type, EPenNodeType::Rectangle);
	TestTrue(TEXT("Figma input width FILL"), Input->Width.IsFill());
	TestTrue(TEXT("Figma input height FIXED"), Input->Height.IsFixed());
	TestTrue(TEXT("Figma input has corner radius"), Input->CornerRadius.HasRadius());
	TestTrue(TEXT("Figma input has stroke"), Input->Stroke.HasStroke());
	TestEqual(TEXT("Figma input stroke align"), Input->Stroke.Align, EPenStrokeAlign::Inside);

	// Ellipse
	const TSharedPtr<FPenNode>& Avatar = Screen->Children[2];
	TestEqual(TEXT("Figma avatar type"), Avatar->Type, EPenNodeType::Ellipse);

	// Rectangle with effects and individual corner radii
	const TSharedPtr<FPenNode>& ShadowBox = Screen->Children[3];
	TestEqual(TEXT("Figma shadow box type"), ShadowBox->Type, EPenNodeType::Rectangle);
	TestTrue(TEXT("Figma shadow box has effects"), ShadowBox->Effects.Num() > 0);
	TestEqual(TEXT("Figma shadow effect type"), ShadowBox->Effects[0].Type, EPenEffectType::Shadow);
	TestFalse(TEXT("Figma shadow not inner"), ShadowBox->Effects[0].Shadow.bInner);
	TestFalse(TEXT("Figma shadow box non-uniform radius"), ShadowBox->CornerRadius.bUniform);
	TestEqual(TEXT("Figma shadow box TopLeft"), ShadowBox->CornerRadius.TopLeft, 12.0f);
	TestEqual(TEXT("Figma shadow box BottomLeft"), ShadowBox->CornerRadius.BottomLeft, 0.0f);

	return true;
}

// ============================================================
// Test 5: FigmaParser - ExtractFileKey URL 解析
// ============================================================

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

// ============================================================
// Test 6: FigmaParser → Converter 端到端
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaConverterTest,
	"Design2UMG.FigmaParser.EndToEndConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0", "name": "Doc", "type": "DOCUMENT",
			"children": [{
				"id": "0:1", "name": "Page", "type": "CANVAS",
				"children": [{
					"id": "2:1", "name": "Card", "type": "FRAME",
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 400, "height": 300},
					"layoutMode": "VERTICAL",
					"itemSpacing": 8,
					"fills": [{"type": "SOLID", "color": {"r": 1, "g": 1, "b": 1, "a": 1}}],
					"children": [
						{
							"id": "2:2", "name": "Label", "type": "TEXT",
							"characters": "Hello Figma",
							"style": {"fontFamily": "Roboto", "fontSize": 18, "fontWeight": 400,
								"textAlignHorizontal": "LEFT", "textAlignVertical": "TOP",
								"lineHeightUnit": "AUTO"},
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 200, "height": 24},
							"layoutSizingHorizontal": "HUG", "layoutSizingVertical": "HUG",
							"fills": [{"type": "SOLID", "color": {"r": 0, "g": 0, "b": 0, "a": 1}}]
						}
					]
				}]
			}]
		},
		"components": {}, "componentSets": {}
	})");

	FPenDocument Doc;
	TestTrue(TEXT("E2E parse"), FFigmaParser::ParseDocument(Json, Doc));

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestFalse(TEXT("E2E root type"), Data.RootWidget.Type.IsEmpty());
	TestTrue(TEXT("E2E has children"), Data.RootWidget.Children.Num() > 0);

	return true;
}

// ============================================================
// Test 7: Ref-of-Ref descendants 覆盖
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRefOfRefDescendantsOverrideTest,
	"Design2UMG.Ref.RefOfRefDescendantsContentOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRefOfRefDescendantsOverrideTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "KbyBJ",
				"type": "frame",
				"reusable": true,
				"layout": "horizontal",
				"children": [
					{ "id": "248ys", "type": "text", "content": "Tab Item" }
				]
			},
			{
				"id": "BdBJJ",
				"type": "ref",
				"reusable": true,
				"ref": "KbyBJ",
				"descendants": {
					"248ys": { "fill": "#666666" }
				}
			},
			{
				"id": "screen",
				"type": "frame",
				"width": 640,
				"height": 120,
				"layout": "horizontal",
				"children": [
					{
						"id": "tab1",
						"type": "ref",
						"ref": "KbyBJ",
						"descendants": { "248ys": { "content": "Integrations" } }
					},
					{
						"id": "tab2",
						"type": "ref",
						"ref": "BdBJJ",
						"descendants": { "248ys": { "content": "Billing" } }
					},
					{
						"id": "tab3",
						"type": "ref",
						"ref": "BdBJJ",
						"descendants": { "248ys": { "content": "Profile" } }
					},
					{
						"id": "tab4",
						"type": "ref",
						"ref": "BdBJJ",
						"descendants": { "248ys": { "content": "Advanced" } }
					}
				]
			}
		]
	})JSON");

	FPenDocument Doc;
	TestTrue(TEXT("RefOfRef parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	TArray<FString> TextValues;
	CollectTextValues(Data.RootWidget, TextValues);
	TestTrue(TEXT("Text contains Integrations"), TextValues.Contains(TEXT("Integrations")));
	TestTrue(TEXT("Text contains Billing"), TextValues.Contains(TEXT("Billing")));
	TestTrue(TEXT("Text contains Profile"), TextValues.Contains(TEXT("Profile")));
	TestTrue(TEXT("Text contains Advanced"), TextValues.Contains(TEXT("Advanced")));
	TestFalse(TEXT("Text should not keep default Tab Item"), TextValues.Contains(TEXT("Tab Item")));
	return true;
}

// ============================================================
// Test 8: Horizontal 父容器 + Ref 子节点交叉轴 Fill
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHorizontalParentRefChildCrossFillTest,
	"Design2UMG.Layout.HorizontalParentRefChildCrossFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHorizontalParentRefChildCrossFillTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "d5ZTS",
				"type": "frame",
				"name": "Sidebar",
				"reusable": true,
				"layout": "vertical",
				"width": 280,
				"height": "fill_container(640)",
				"children": [
					{ "id": "header", "type": "frame", "width": "fill_container", "height": 80 }
				]
			},
			{
				"id": "root",
				"type": "frame",
				"layout": "horizontal",
				"width": 1440,
				"height": 900,
				"children": [
					{ "id": "sidebar", "type": "ref", "ref": "d5ZTS" },
					{ "id": "content", "type": "frame", "layout": "vertical", "width": "fill_container", "height": 900 }
				]
			}
		]
	})JSON");

	FPenDocument Doc;
	TestTrue(TEXT("CrossFill parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	const FUMGWidgetNode* Sidebar = FindWidgetById(Data.RootWidget, TEXT("sidebar"));
	TestTrue(TEXT("Sidebar widget exists"), Sidebar != nullptr);
	if (Sidebar)
	{
		TestTrue(TEXT("Sidebar resolved height should use parent inner height (900)"),
			FMath::IsNearlyEqual(Sidebar->Bounds.H, 900.0f, 0.01f));
	}
	return true;
}

// ============================================================
// Test 9: IconFont 无映射降级
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIconFontNoMappingFallbackTest,
	"Design2UMG.IconFont.NoMappingFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FIconFontNoMappingFallbackTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	const TMap<FString, FString> SavedMap = Settings->IconFontAssetMap;
	const FString SavedDefaultPath = Settings->DefaultIconFontAssetPath;
	const FString SavedPlaceholder = Settings->MissingIconFontPlaceholder;

	Settings->IconFontAssetMap.Empty();
	Settings->DefaultIconFontAssetPath.Empty();
	Settings->MissingIconFontPlaceholder = TEXT("[missing-icon]");

	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"width": 320,
				"height": 64,
				"layout": "horizontal",
				"children": [
					{
						"id": "icon1",
						"type": "icon_font",
						"iconFontFamily": "Material Symbols Sharp",
						"iconFontName": "dashboard",
						"fontSize": 20
					}
				]
			}
		]
	})JSON");

	FPenDocument Doc;
	TestTrue(TEXT("Icon fallback parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	const FUMGWidgetNode* IconNode = FindWidgetByProperty(Data.RootWidget, TEXT("IconName"));
	TestTrue(TEXT("Icon widget exists"), IconNode != nullptr);
	if (IconNode)
	{
		TestFalse(TEXT("Missing mapping should not inject invalid FontFamily string as Typeface"),
			IconNode->Properties.Contains(TEXT("FontFamily")));
		const FString* TextValue = IconNode->Properties.Find(TEXT("Text"));
		TestTrue(TEXT("Missing mapping uses configured placeholder text"),
			TextValue && *TextValue == TEXT("[missing-icon]"));
	}

	Settings->IconFontAssetMap = SavedMap;
	Settings->DefaultIconFontAssetPath = SavedDefaultPath;
	Settings->MissingIconFontPlaceholder = SavedPlaceholder;
	return true;
}

// ============================================================
// Test 10: Parser number/string 宽容解析
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParserNumberStringFlexibleTest,
	"Design2UMG.Parser.NumberStringFlexible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FParserNumberStringFlexibleTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"x": "12",
				"y": "34.5",
				"rotation": "3",
				"width": 800,
				"height": 600,
				"layout": "vertical",
				"gap": "8",
				"opacity": "0.75",
				"padding": ["1", "2", "3", "4"],
				"cornerRadius": ["5", "6", "7", "8"],
				"stroke": {
					"align": "inside",
					"thickness": "2.5"
				},
				"fill": {
					"type": "image",
					"url": "https://example.com/icon.png",
					"imageTransform": [["1", "0", "0"], ["0", "1", "0"]]
				},
				"children": [
					{
						"id": "txt",
						"type": "text",
						"content": "Label",
						"fontSize": "16",
						"lineHeight": "1.5",
						"letterSpacing": "0.2"
					},
					{
						"id": "icon",
						"type": "icon_font",
						"iconFontName": "dashboard",
						"weight": "500"
					}
				]
			}
		]
	})JSON");

	FPenDocument Doc;
	TestTrue(TEXT("String number parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level child count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() == 0)
		return false;

	const TSharedPtr<FPenNode>& Root = Doc.Children[0];
	TestTrue(TEXT("Root has parsed X"), Root->bHasX);
	TestTrue(TEXT("Root has parsed Y"), Root->bHasY);
	TestEqual(TEXT("Root gap parsed from string"), Root->Gap, 8.0f);
	TestEqual(TEXT("Root opacity parsed from string"), Root->Opacity, 0.75f);
	TestEqual(TEXT("Root padding top parsed from string"), Root->Padding.Top, 1.0f);
	TestEqual(TEXT("Root padding right parsed from string"), Root->Padding.Right, 2.0f);
	TestEqual(TEXT("Root padding bottom parsed from string"), Root->Padding.Bottom, 3.0f);
	TestEqual(TEXT("Root padding left parsed from string"), Root->Padding.Left, 4.0f);
	TestTrue(TEXT("Root cornerRadius parsed as per-corner"), !Root->CornerRadius.bUniform);
	TestEqual(TEXT("Root cornerRadius TL parsed from string"), Root->CornerRadius.TopLeft, 5.0f);
	TestEqual(TEXT("Root cornerRadius TR parsed from string"), Root->CornerRadius.TopRight, 6.0f);
	TestEqual(TEXT("Root cornerRadius BR parsed from string"), Root->CornerRadius.BottomRight, 7.0f);
	TestEqual(TEXT("Root cornerRadius BL parsed from string"), Root->CornerRadius.BottomLeft, 8.0f);
	TestTrue(TEXT("Root stroke thickness parsed as uniform"), Root->Stroke.Thickness.bUniform);
	TestEqual(TEXT("Root stroke thickness parsed from string"), Root->Stroke.Thickness.Uniform, 2.5f);
	TestEqual(TEXT("Root image transform A parsed from string"), Root->Fill.Image.Transform.A, 1.0f);
	TestEqual(TEXT("Root image transform D parsed from string"), Root->Fill.Image.Transform.D, 1.0f);
	TestEqual(TEXT("Root child count"), Root->Children.Num(), 2);
	if (Root->Children.Num() < 2)
		return false;
	TestEqual(TEXT("Text fontSize parsed from string"), Root->Children[0]->FontSize, 16.0f);
	TestEqual(TEXT("Text lineHeight parsed from string"), Root->Children[0]->LineHeight, 1.5f);
	TestEqual(TEXT("Text letterSpacing parsed from string"), Root->Children[0]->LetterSpacing, 0.2f);
	TestEqual(TEXT("Icon weight parsed from string"), Root->Children[1]->IconWeight, 500);
	return true;
}

#endif // WITH_AUTOMATION_WORKER
