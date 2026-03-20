// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "Design2UMGSettings.h"
#include "DesignVariableResolver.h"
#include "Design2UMGTestUtils.h"
#include "LayoutCalculator.h"
#include "FigmaParser.h"
#include "Framework/Application/SlateApplication.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;

// ============================================================
// Test 1: Parser - 解析硬编码 JSON，验证节点类型/数量/属性

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

	FDesignDocument Doc;
	const bool bParsed = FPenParser::ParseDocument(Json, Doc);
	TestTrue(TEXT("ParseDocument succeeds"), bParsed);
	if (!bParsed || Doc.Children.Num() == 0)
		return false;
	TestEqual(TEXT("Top-level children count"), Doc.Children.Num(), 1);

	// Frame 节点
	const TSharedPtr<FDesignNode>& Screen = Doc.Children[0];
	TestTrue(TEXT("Screen node valid"), Screen.IsValid());
	if (!Screen.IsValid())
		return false;
	TestEqual(TEXT("Screen type"), Screen->Type, EDesignNodeType::Frame);
	TestEqual(TEXT("Screen name"), Screen->Name, TEXT("MainScreen"));
	TestTrue(TEXT("Screen width fixed"), Screen->Width.IsFixed());
	TestEqual(TEXT("Screen width value"), Screen->Width.Value, 1920.0f);
	TestEqual(TEXT("Screen layout"), Screen->Layout, EDesignLayout::Vertical);
	TestEqual(TEXT("Screen gap"), Screen->Gap, 16.0f);
	TestEqual(TEXT("Screen children count"), Screen->Children.Num(), 3);
	if (Screen->Children.Num() < 3)
		return false;

	// Text 节点
	const TSharedPtr<FDesignNode>& Title = Screen->Children[0];
	TestEqual(TEXT("Title type"), Title->Type, EDesignNodeType::Text);
	TestEqual(TEXT("Title content"), Title->Content, TEXT("Hello World"));
	TestEqual(TEXT("Title fontSize"), Title->FontSize, 24.0f);
	TestEqual(TEXT("Title fontWeight"), Title->FontWeight, TEXT("bold"));

	// Ellipse 节点
	const TSharedPtr<FDesignNode>& Ellipse = Screen->Children[1];
	TestEqual(TEXT("Ellipse type"), Ellipse->Type, EDesignNodeType::Ellipse);

	// Rectangle 节点
	const TSharedPtr<FDesignNode>& Rect = Screen->Children[2];
	TestEqual(TEXT("Rect type"), Rect->Type, EDesignNodeType::Rectangle);
	TestTrue(TEXT("Rect has corner radius"), Rect->CornerRadius.HasRadius());

	return true;
}

// ============================================================
// Test 2: Converter - AST 转 Widget 树，验证结构和降级警告

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

	FDesignDocument Doc;
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
// Test 4: Ref-of-Ref descendants 覆盖
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRefOfRefDescendantsOverrideTest,
	"Design2UMG.Ref.RefOfRefDescendantsContentOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRefOfRefDescendantsOverrideTest::RunTest(const FString& Parameters)
{
	FString Json;
	TestTrue(TEXT("Load fixture RefOfRefDescendantsOverride"), LoadFixtureJson(TEXT("RefOfRefDescendantsOverride.json"), Json));
	if (Json.IsEmpty())
		return false;

	FDesignDocument Doc;
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
// Test 4.1: descendants bare-id 深层覆盖（无 slash path）
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRefDeepDescendantsBareIdOverrideTest,
	"Design2UMG.Ref.DeepDescendantsBareIdOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRefDeepDescendantsBareIdOverrideTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "BaseComp",
				"type": "frame",
				"reusable": true,
				"layout": "vertical",
				"children": [
					{
						"id": "container",
						"type": "frame",
						"children": [
							{ "id": "deepLabel", "type": "text", "content": "Default Label" }
						]
					}
				]
			},
			{
				"id": "VariantComp",
				"type": "ref",
				"reusable": true,
				"ref": "BaseComp",
				"descendants": {
					"deepLabel": { "content": "Reusable Override" }
				}
			},
			{
				"id": "screen",
				"type": "frame",
				"width": 640,
				"height": 200,
				"layout": "vertical",
				"children": [
					{
						"id": "instance",
						"type": "ref",
						"ref": "VariantComp",
						"descendants": {
							"deepLabel": { "content": "Final Override" }
						}
					}
				]
			}
		]
	})JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Deep descendants parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	TArray<FString> TextValues;
	CollectTextValues(Data.RootWidget, TextValues);
	TestTrue(TEXT("Text contains Final Override"), TextValues.Contains(TEXT("Final Override")));
	TestFalse(TEXT("Text should not keep Default Label"), TextValues.Contains(TEXT("Default Label")));
	TestFalse(TEXT("Text should not keep Reusable Override"), TextValues.Contains(TEXT("Reusable Override")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRefExplicitEffectClearTest,
	"Design2UMG.Ref.ExplicitEffectClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRefExplicitEffectClearTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "BaseTab",
				"type": "frame",
				"reusable": true,
				"fill": "#FFFFFF",
				"effect": {
					"type": "shadow",
					"shadowType": "outer",
					"color": "#00000033",
					"offset": { "x": 0, "y": 1 },
					"blur": 2
				},
				"children": [
					{ "id": "label", "type": "text", "content": "Tab", "fontSize": 14 }
				]
			},
			{
				"id": "InactiveTab",
				"type": "ref",
				"reusable": true,
				"ref": "BaseTab",
				"effect": []
			},
			{
				"id": "screen",
				"type": "frame",
				"width": 320,
				"height": 120,
				"layout": "vertical",
				"children": [
					{
						"id": "inactiveInstance",
						"type": "ref",
						"ref": "InactiveTab"
					}
				]
			}
		]
	})JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Effect-clear fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestNull(TEXT("Explicit effect clear should remove inherited box shadow widget"),
		FindWidgetById(Data.RootWidget, TEXT("inactiveInstance_shadow0")));
	return true;
}

// ============================================================
// Test 5: Horizontal 父容器 + Ref 子节点交叉轴 Fill
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHorizontalParentRefChildCrossFillTest,
	"Design2UMG.Layout.HorizontalParentRefChildCrossFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHorizontalParentRefChildCrossFillTest::RunTest(const FString& Parameters)
{
	FString Json;
	TestTrue(TEXT("Load fixture HorizontalParentRefChildCrossFill"), LoadFixtureJson(TEXT("HorizontalParentRefChildCrossFill.json"), Json));
	if (Json.IsEmpty())
		return false;

	FDesignDocument Doc;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRefFillClearSuppressesInheritedStrokeFillTest,
	"Design2UMG.Ref.FillClearSuppressesInheritedStrokeFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRefFillClearSuppressesInheritedStrokeFillTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "screen",
				"type": "frame",
				"width": 400,
				"height": 200,
				"layout": "vertical",
				"gap": 12,
				"children": [
					{
						"id": "ghostFrame",
						"type": "frame",
						"width": 120,
						"height": 40,
						"cornerRadius": 999,
						"stroke": {
							"align": "inside",
							"thickness": 1
						}
					},
					{
						"id": "defaultItem",
						"type": "ref",
						"ref": "pill",
						"fill": []
					},
					{
						"id": "outlinedItem",
						"type": "ref",
						"ref": "pill",
						"fill": [],
						"stroke": {
							"align": "inside",
							"thickness": 1,
							"fill": "#888888"
						}
					}
				]
			},
			{
				"id": "pill",
				"type": "frame",
				"reusable": true,
				"width": 248,
				"fill": "#222222",
				"cornerRadius": 100,
				"stroke": {
					"align": "inside",
					"thickness": 1
				},
				"padding": [12, 16],
				"alignItems": "center",
				"children": [
					{
						"id": "label",
						"type": "text",
						"content": "Item",
						"fill": "#FFFFFF"
					}
				]
			}
		]
	})JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Fill clear ref parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	const FUMGWidgetNode* DefaultItem = FindWidgetById(Data.RootWidget, TEXT("defaultItem"));
	TestTrue(TEXT("Default item exists"), DefaultItem != nullptr);
	if (DefaultItem != nullptr)
	{
		TestFalse(TEXT("Fill clear ref should clear inherited outline color"),
			DefaultItem->Properties.Contains(TEXT("OutlineColor")));
	}

	const FUMGWidgetNode* GhostFrame = FindWidgetById(Data.RootWidget, TEXT("ghostFrame"));
	TestTrue(TEXT("Ghost frame exists"), GhostFrame != nullptr);
	if (GhostFrame != nullptr)
	{
		TestFalse(TEXT("Stroke without explicit fill should stay invisible"),
			GhostFrame->Properties.Contains(TEXT("OutlineColor")));
	}

	const FUMGWidgetNode* OutlinedItem = FindWidgetById(Data.RootWidget, TEXT("outlinedItem"));
	TestTrue(TEXT("Outlined item exists"), OutlinedItem != nullptr);
	if (OutlinedItem != nullptr)
	{
		const FString* OutlineColor = OutlinedItem->Properties.Find(TEXT("OutlineColor"));
		TestTrue(TEXT("Explicit stroke fill should be preserved"),
			OutlineColor != nullptr && *OutlineColor == TEXT("#888888"));
	}

	return true;
}

// ============================================================
// Test 5.0b: Flow 父容器(H/V)下，子节点 Bounds.X/Y 应归零
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlowParentChildBoundsLocalizedTest,
	"Design2UMG.Layout.FlowParentChildBoundsLocalized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFlowParentChildBoundsLocalizedTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"layout": "horizontal",
				"width": 600,
				"height": 120,
				"children": [
					{ "id": "childA", "type": "text", "x": 123, "y": 45, "content": "A", "fontSize": 16 },
					{ "id": "childB", "type": "text", "x": 350, "y": 60, "content": "B", "fontSize": 16 }
				]
			}
		]
	})JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Flow bounds-localized parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	const FUMGWidgetNode* ChildA = FindWidgetById(Data.RootWidget, TEXT("childA"));
	TestTrue(TEXT("childA exists"), ChildA != nullptr);
	if (ChildA)
	{
		TestTrue(TEXT("childA bounds x should be 0 in flow layout"), FMath::IsNearlyEqual(ChildA->Bounds.X, 0.0f, 0.01f));
		TestTrue(TEXT("childA bounds y should be 0 in flow layout"), FMath::IsNearlyEqual(ChildA->Bounds.Y, 0.0f, 0.01f));
	}

	const FUMGWidgetNode* ChildB = FindWidgetById(Data.RootWidget, TEXT("childB"));
	TestTrue(TEXT("childB exists"), ChildB != nullptr);
	if (ChildB)
	{
		TestTrue(TEXT("childB bounds x should be 0 in flow layout"), FMath::IsNearlyEqual(ChildB->Bounds.X, 0.0f, 0.01f));
		TestTrue(TEXT("childB bounds y should be 0 in flow layout"), FMath::IsNearlyEqual(ChildB->Bounds.Y, 0.0f, 0.01f));
	}

	return true;
}

// ============================================================
// Test 5.0: Ref 实例未显式提供 x/y 时，应使用本地原点(0,0)
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRefInstanceNoPositionUsesLocalOriginTest,
	"Design2UMG.Ref.InstanceNoPositionUsesLocalOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRefInstanceNoPositionUsesLocalOriginTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "Comp",
				"type": "frame",
				"reusable": true,
				"x": 300,
				"y": 400,
				"width": 120,
				"height": 60,
				"layout": "none",
				"children": [
					{ "id": "label", "type": "text", "x": 8, "y": 6, "content": "Demo", "fontSize": 14 }
				]
			},
			{
				"id": "root",
				"type": "frame",
				"width": 800,
				"height": 600,
				"layout": "none",
				"children": [
					{ "id": "instance", "type": "ref", "ref": "Comp" }
				]
			}
		]
	})JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Ref instance local-origin parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	const FUMGWidgetNode* Instance = FindWidgetById(Data.RootWidget, TEXT("instance"));
	TestTrue(TEXT("instance widget exists"), Instance != nullptr);
	if (Instance)
	{
		TestTrue(TEXT("instance bounds x should be local origin"), FMath::IsNearlyEqual(Instance->Bounds.X, 0.0f, 0.01f));
		TestTrue(TEXT("instance bounds y should be local origin"), FMath::IsNearlyEqual(Instance->Bounds.Y, 0.0f, 0.01f));
		TestTrue(TEXT("instance slot left should be local origin"), FMath::IsNearlyEqual(Instance->Slot.Offsets.Left, 0.0f, 0.01f));
		TestTrue(TEXT("instance slot top should be local origin"), FMath::IsNearlyEqual(Instance->Slot.Offsets.Top, 0.0f, 0.01f));
	}

	return true;
}

// ============================================================
// Test 5.1: Canvas(layout=none) 下 fill_container(N) 使用回退尺寸，不做全屏拉伸
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCanvasFillContainerFallbackSizeTest,
	"Design2UMG.Layout.CanvasFillContainerFallbackSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCanvasFillContainerFallbackSizeTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"name": "CanvasRoot",
				"width": 1000,
				"height": 800,
				"layout": "none",
				"children": [
					{
						"id": "withFallback",
						"type": "rectangle",
						"x": 100,
						"y": 120,
						"width": "fill_container(300)",
						"height": 80,
						"fill": "#222222"
					},
					{
						"id": "withoutFallback",
						"type": "rectangle",
						"x": 20,
						"y": 240,
						"width": "fill_container",
						"height": 60,
						"fill": "#333333"
					}
				]
			}
		]
	})JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Canvas fallback parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	const FUMGWidgetNode* WithFallback = FindWidgetById(Data.RootWidget, TEXT("withFallback"));
	TestTrue(TEXT("withFallback widget exists"), WithFallback != nullptr);
	if (WithFallback)
	{
		TestEqual(TEXT("withFallback anchor max X should stay fixed"), WithFallback->Slot.Anchors.Max.X, 0.0f);
		TestEqual(TEXT("withFallback width should use fallback size"), WithFallback->Slot.Offsets.Right, 300.0f);
	}

	const FUMGWidgetNode* WithoutFallback = FindWidgetById(Data.RootWidget, TEXT("withoutFallback"));
	TestTrue(TEXT("withoutFallback widget exists"), WithoutFallback != nullptr);
	if (WithoutFallback)
	{
		TestEqual(TEXT("withoutFallback should stretch on X"), WithoutFallback->Slot.Anchors.Max.X, 1.0f);
		TestEqual(TEXT("withoutFallback stretch right offset should be zero"), WithoutFallback->Slot.Offsets.Right, 0.0f);
	}

	return true;
}

// ============================================================
// Test 6: IconFont 无映射降级

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIconFontNoMappingFallbackTest,
	"Design2UMG.IconFont.NoMappingFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FIconFontNoMappingFallbackTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	const TMap<FString, TSoftObjectPtr<UFont>> SavedMap = Settings->IconFontAssetMap;
	const FString SavedDefaultPath = Settings->DefaultIconFontAssetPath;
	const FString SavedPlaceholder = Settings->MissingIconFontPlaceholder;

	Settings->IconFontAssetMap.Empty();
	Settings->DefaultIconFontAssetPath.Empty();
	Settings->MissingIconFontPlaceholder = TEXT("[missing-icon]");

	FString Json;
	TestTrue(TEXT("Load fixture IconFontNoMappingFallback"), LoadFixtureJson(TEXT("IconFontNoMappingFallback.json"), Json));
	if (Json.IsEmpty())
	{
		Settings->IconFontAssetMap = SavedMap;
		Settings->DefaultIconFontAssetPath = SavedDefaultPath;
		Settings->MissingIconFontPlaceholder = SavedPlaceholder;
		return false;
	}

	FDesignDocument Doc;
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
// Test 7: IconFont 无映射且空占位时保留原始 IconName
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIconFontNoMappingKeepIconNameTest,
	"Design2UMG.IconFont.NoMappingKeepsIconName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FIconFontNoMappingKeepIconNameTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	const TMap<FString, TSoftObjectPtr<UFont>> SavedMap = Settings->IconFontAssetMap;
	const FString SavedDefaultPath = Settings->DefaultIconFontAssetPath;
	const FString SavedPlaceholder = Settings->MissingIconFontPlaceholder;

	Settings->IconFontAssetMap.Empty();
	Settings->DefaultIconFontAssetPath.Empty();
	Settings->MissingIconFontPlaceholder.Empty();

	FString Json;
	TestTrue(TEXT("Load fixture IconFontNoMappingFallback"), LoadFixtureJson(TEXT("IconFontNoMappingFallback.json"), Json));
	if (Json.IsEmpty())
	{
		Settings->IconFontAssetMap = SavedMap;
		Settings->DefaultIconFontAssetPath = SavedDefaultPath;
		Settings->MissingIconFontPlaceholder = SavedPlaceholder;
		return false;
	}

	FDesignDocument Doc;
	TestTrue(TEXT("Icon fallback parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	const FString ExpectedIconName = TEXT("dashboard");
	const FUMGWidgetNode* IconNode = FindWidgetByProperty(Data.RootWidget, TEXT("IconName"), &ExpectedIconName);
	TestTrue(TEXT("Icon widget exists"), IconNode != nullptr);
	if (IconNode)
	{
		TestFalse(TEXT("Missing mapping should not inject invalid FontFamily string as Typeface"),
			IconNode->Properties.Contains(TEXT("FontFamily")));
		const FString* TextValue = IconNode->Properties.Find(TEXT("Text"));
		TestTrue(TEXT("Empty placeholder should keep original IconName text"),
			TextValue && *TextValue == ExpectedIconName);
	}

	Settings->IconFontAssetMap = SavedMap;
	Settings->DefaultIconFontAssetPath = SavedDefaultPath;
	Settings->MissingIconFontPlaceholder = SavedPlaceholder;
	return true;
}

// ============================================================
// Test 8: Parser number/string 宽松解析
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParserNumberStringFlexibleTest,
	"Design2UMG.Parser.NumberStringFlexible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FParserNumberStringFlexibleTest::RunTest(const FString& Parameters)
{
	FString Json;
	TestTrue(TEXT("Load fixture ParserNumberStringFlexible"), LoadFixtureJson(TEXT("ParserNumberStringFlexible.json"), Json));
	if (Json.IsEmpty())
		return false;

	FDesignDocument Doc;
	TestTrue(TEXT("String number parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level child count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() == 0)
		return false;

	const TSharedPtr<FDesignNode>& Root = Doc.Children[0];
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenPaddingTwoValueAxisOrderTest,
	"Design2UMG.Parser.PenPaddingTwoValueAxisOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenPaddingTwoValueAxisOrderTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [{
			"id": "panel",
			"type": "frame",
			"padding": [6, 12]
		}]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("ParseDocument succeeds"), FPenParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Child count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Panel = Doc.Children[0];
	TestEqual(TEXT("Two-value padding top"), Panel->Padding.Top, 6.0f);
	TestEqual(TEXT("Two-value padding bottom"), Panel->Padding.Bottom, 6.0f);
	TestEqual(TEXT("Two-value padding left"), Panel->Padding.Left, 12.0f);
	TestEqual(TEXT("Two-value padding right"), Panel->Padding.Right, 12.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVariableResolverLiteralDollarTextTest,
	"Design2UMG.Parser.LiteralDollarTextIsNotVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FVariableResolverLiteralDollarTextTest::RunTest(const FString& Parameters)
{
	TMap<FString, FDesignVariable> Variables;
	FDesignVariableResolver Resolver;
	Resolver.SetVariables(Variables);

	TestFalse(TEXT("$45.00 should not be treated as variable ref"), Resolver.IsVariableRef(TEXT("$45.00")));
	TestEqual(TEXT("Literal dollar text should round-trip unchanged"),
		Resolver.ResolveString(TEXT("$45.00")),
		FString(TEXT("$45.00")));
	TestTrue(TEXT("$--font-primary remains a variable ref"), Resolver.IsVariableRef(TEXT("$--font-primary")));

	return true;
}

// ============================================================
// Test 8: FigmaParser - 复杂 Dashboard 全路径覆盖

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaComplexParserTest,
	"Design2UMG.FigmaParser.ComplexDashboard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaComplexParserTest::RunTest(const FString& Parameters)
{
	FString Json;
	TestTrue(TEXT("Load FigmaComplex fixture"), LoadFixtureJson(TEXT("FigmaComplex.json"), Json));
	if (Json.IsEmpty())
		return false;

	FDesignDocument Doc;
	TestTrue(TEXT("Figma complex parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level frame count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() == 0)
		return false;

	// Dashboard root: HORIZONTAL auto layout
	const auto& Dashboard = Doc.Children[0];
	TestEqual(TEXT("Dashboard type"), Dashboard->Type, EDesignNodeType::Frame);
	TestEqual(TEXT("Dashboard layout"), Dashboard->Layout, EDesignLayout::Horizontal);
	TestTrue(TEXT("Dashboard clip (Figma clipsContent mapped)"), Dashboard->bClip);
	TestEqual(TEXT("Dashboard children"), Dashboard->Children.Num(), 2);
	if (Dashboard->Children.Num() < 2)
		return false;

	// Sidebar: VERTICAL + individualStrokeWeights + FILL height
	const auto& Sidebar = Dashboard->Children[0];
	TestEqual(TEXT("Sidebar layout"), Sidebar->Layout, EDesignLayout::Vertical);
	TestTrue(TEXT("Sidebar height FILL"), Sidebar->Height.IsFill());
	TestTrue(TEXT("Sidebar width FIXED"), Sidebar->Width.IsFixed());
	TestEqual(TEXT("Sidebar gap"), Sidebar->Gap, 8.0f);
	TestEqual(TEXT("Sidebar padding left"), Sidebar->Padding.Left, 16.0f);
	TestFalse(TEXT("Sidebar stroke non-uniform"), Sidebar->Stroke.Thickness.bUniform);
	TestEqual(TEXT("Sidebar stroke right"), Sidebar->Stroke.Thickness.Right, 1.0f);
	TestEqual(TEXT("Sidebar stroke top"), Sidebar->Stroke.Thickness.Top, 0.0f);
	TestEqual(TEXT("Sidebar children"), Sidebar->Children.Num(), 4);
	if (Sidebar->Children.Num() < 4)
		return false;

	// Logo text: fontWeight 700 preserved, textAutoResize→Auto, textAlignVertical CENTER
	const auto& Logo = Sidebar->Children[0];
	TestEqual(TEXT("Logo type"), Logo->Type, EDesignNodeType::Text);
	TestEqual(TEXT("Logo content"), Logo->Content, TEXT("Acme Dashboard"));
	TestEqual(TEXT("Logo fontWeight"), Logo->FontWeight, TEXT("700"));
	TestEqual(TEXT("Logo fontSize"), Logo->FontSize, 20.0f);
	TestEqual(TEXT("Logo textGrowth"), Logo->TextGrowth, EDesignTextGrowth::Auto);
	TestEqual(TEXT("Logo textAlignV"), Logo->TextAlignVertical, EDesignTextAlignVertical::Middle);
	TestTrue(TEXT("Logo lineHeight > 0"), Logo->LineHeight > 0.0f);

	// Divider: LINE type
	TestEqual(TEXT("Divider type"), Sidebar->Children[1]->Type, EDesignNodeType::Line);

	// NavItem: COMPONENT → bReusable
	const auto& NavItem = Sidebar->Children[2];
	TestTrue(TEXT("NavItem reusable"), NavItem->bReusable);
	TestEqual(TEXT("NavItem layout"), NavItem->Layout, EDesignLayout::Horizontal);
	TestEqual(TEXT("NavItem children"), NavItem->Children.Num(), 2);

	// NavItem child: VECTOR → Path
	if (NavItem->Children.Num() >= 1)
		TestEqual(TEXT("NavIcon type"), NavItem->Children[0]->Type, EDesignNodeType::Path);

	// NavItem Instance: INSTANCE → Ref = componentId
	const auto& NavInst = Sidebar->Children[3];
	TestEqual(TEXT("NavInst ref"), NavInst->Ref, TEXT("10:5"));

	// MainContent
	const auto& Main = Dashboard->Children[1];
	TestTrue(TEXT("Main width FILL"), Main->Width.IsFill());
	TestEqual(TEXT("Main padding"), Main->Padding.Top, 32.0f);
	TestEqual(TEXT("Main children"), Main->Children.Num(), 4);
	if (Main->Children.Num() < 4)
		return false;

	// Header: SPACE_BETWEEN
	const auto& Header = Main->Children[0];
	TestEqual(TEXT("Header justify"), Header->JustifyContent, EDesignJustifyContent::SpaceBetween);
	TestEqual(TEXT("Header align"), Header->AlignItems, EDesignAlignItems::Center);

	// Avatar: ELLIPSE + multi-fill (SOLID + IMAGE)
	if (Header->Children.Num() >= 2)
	{
		const auto& Avatar = Header->Children[1];
		TestEqual(TEXT("Avatar type"), Avatar->Type, EDesignNodeType::Ellipse);
		TestTrue(TEXT("Avatar has image fill"), Avatar->Fill.IsImage());
		TestTrue(TEXT("Avatar multi-fills >= 2"), Avatar->Fills.Num() >= 2);
	}

	// StatCard: GRADIENT_LINEAR + DROP_SHADOW
	const auto& StatsRow = Main->Children[1];
	if (StatsRow->Children.Num() >= 1)
	{
		const auto& Card = StatsRow->Children[0];
		TestEqual(TEXT("Card fill gradient"), Card->Fill.Type, EDesignFillType::Gradient);
		TestEqual(TEXT("Card gradient type"), Card->Fill.Gradient.GradientType, EDesignGradientType::Linear);
		TestTrue(TEXT("Card gradient stops >= 2"), Card->Fill.Gradient.Colors.Num() >= 2);
		TestTrue(TEXT("Card has effects"), Card->Effects.Num() > 0);
		TestEqual(TEXT("Card shadow type"), Card->Effects[0].Type, EDesignEffectType::Shadow);
		TestFalse(TEXT("Card shadow not inner"), Card->Effects[0].Shadow.bInner);
	}

	// ChartArea: INNER_SHADOW + BACKGROUND_BLUR + rectangleCornerRadii child
	const auto& Chart = Main->Children[2];
	TestTrue(TEXT("Chart effects >= 2"), Chart->Effects.Num() >= 2);
	bool bFoundInner = false, bFoundBgBlur = false;
	for (const auto& E : Chart->Effects)
	{
		if (E.Type == EDesignEffectType::Shadow && E.Shadow.bInner) bFoundInner = true;
		if (E.Type == EDesignEffectType::BackgroundBlur) bFoundBgBlur = true;
	}
	TestTrue(TEXT("Chart has inner shadow"), bFoundInner);
	TestTrue(TEXT("Chart has bg blur"), bFoundBgBlur);

	// ChartPlaceholder: non-uniform corner radius
	if (Chart->Children.Num() >= 2)
	{
		const auto& Placeholder = Chart->Children[1];
		TestEqual(TEXT("Placeholder type"), Placeholder->Type, EDesignNodeType::Rectangle);
		TestFalse(TEXT("Placeholder non-uniform radius"), Placeholder->CornerRadius.bUniform);
		TestEqual(TEXT("Placeholder TL"), Placeholder->CornerRadius.TopLeft, 12.0f);
		TestEqual(TEXT("Placeholder BL"), Placeholder->CornerRadius.BottomLeft, 0.0f);
	}

	// GROUP: absolute positioned children
	const auto& Overlay = Main->Children[3];
	TestEqual(TEXT("Overlay type"), Overlay->Type, EDesignNodeType::Group);
	TestTrue(TEXT("Overlay children >= 7"), Overlay->Children.Num() >= 7);

	if (Overlay->Children.Num() >= 7)
	{
		// LAYER_BLUR
		TestTrue(TEXT("Badge has effects"), Overlay->Children[0]->Effects.Num() > 0);
		TestEqual(TEXT("Badge blur type"), Overlay->Children[0]->Effects[0].Type, EDesignEffectType::Blur);

		// FlippedIcon: relativeTransform flip detection
		TestTrue(TEXT("FlippedIcon flipX"), Overlay->Children[1]->bFlipX);

		// STAR → Polygon
		TestEqual(TEXT("Star type"), Overlay->Children[2]->Type, EDesignNodeType::Polygon);

		// REGULAR_POLYGON → Polygon
		TestEqual(TEXT("Polygon type"), Overlay->Children[3]->Type, EDesignNodeType::Polygon);

		// BOOLEAN_OPERATION → Path
		TestEqual(TEXT("BoolOp type"), Overlay->Children[4]->Type, EDesignNodeType::Path);

		// IMAGE fill with imageTransform + opacity
		const auto& ImgCard = Overlay->Children[5];
		TestTrue(TEXT("ImgCard has image fill"), ImgCard->Fill.IsImage());
		TestTrue(TEXT("ImgCard opacity < 1"), ImgCard->Opacity < 1.0f);

		// GRADIENT_RADIAL
		const auto& GradBg = Overlay->Children[6];
		TestEqual(TEXT("GradBg gradient type"), GradBg->Fill.Gradient.GradientType, EDesignGradientType::Radial);
	}

	// Justified text + textAlignVertical BOTTOM + textAutoResize NONE
	if (Overlay->Children.Num() >= 8)
	{
		const auto& JustText = Overlay->Children[7];
		TestEqual(TEXT("JustText align"), JustText->TextAlign, EDesignTextAlign::Justify);
		TestEqual(TEXT("JustText valign"), JustText->TextAlignVertical, EDesignTextAlignVertical::Bottom);
		TestEqual(TEXT("JustText growth"), JustText->TextGrowth, EDesignTextGrowth::FixedWidthHeight);
	}

	return true;
}

// ============================================================
// Test 8.5: FigmaParser - lineHeight unit mapping
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLineHeightMappingTest,
	"Design2UMG.FigmaParser.LineHeightMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaLineHeightMappingTest::RunTest(const FString& Parameters)
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":100,"height":100},
					"children": [
						{
							"id": "0:3",
							"name": "TextPx",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x":1,"y":1,"width":30,"height":12},
							"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
							"style": {
								"fontFamily": "Inter",
								"fontSize": 10,
								"fontWeight": 400,
								"lineHeightUnit": "PIXELS",
								"lineHeightPx": 15
							},
							"characters": "A",
							"textAutoResize": "HEIGHT"
						},
						{
							"id": "0:4",
							"name": "TextPercent",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x":1,"y":20,"width":30,"height":12},
							"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
							"style": {
								"fontFamily": "Inter",
								"fontSize": 10,
								"fontWeight": 400,
								"lineHeightUnit": "FONT_SIZE_%",
								"lineHeightPercentFontSize": 140
							},
							"characters": "B",
							"textAutoResize": "HEIGHT"
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

	const TSharedPtr<FDesignNode>& PxText = Doc.Children[0]->Children[0];
	const TSharedPtr<FDesignNode>& PercentText = Doc.Children[0]->Children[1];

	TestTrue(TEXT("PIXELS line height mapped as ratio"), FMath::IsNearlyEqual(PxText->LineHeight, 1.5f, 0.001f));
	TestTrue(TEXT("FONT_SIZE_% line height mapped as ratio"), FMath::IsNearlyEqual(PercentText->LineHeight, 1.4f, 0.001f));
	return true;
}

// ============================================================
// Test 8.6: FigmaParser - stroke visibility gate
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaStrokeVisibilityGateTest,
	"Design2UMG.FigmaParser.StrokeVisibilityGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaStrokeVisibilityGateTest::RunTest(const FString& Parameters)
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
					"children": [
						{
							"id": "0:3",
							"name": "NoVisibleStroke",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x":10,"y":10,"width":120,"height":40},
							"strokeWeight": 3,
							"strokeAlign": "CENTER",
							"strokes": [{"type":"SOLID","visible":false,"color":{"r":1,"g":0,"b":0,"a":1}}]
						},
						{
							"id": "0:4",
							"name": "VisibleStroke",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x":10,"y":70,"width":120,"height":40},
							"strokeWeight": 2,
							"strokeAlign": "OUTSIDE",
							"strokes": [{"type":"SOLID","visible":true,"color":{"r":0,"g":1,"b":0,"a":1}}]
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

	const TSharedPtr<FDesignNode>& NoVisibleStroke = Doc.Children[0]->Children[0];
	const TSharedPtr<FDesignNode>& VisibleStroke = Doc.Children[0]->Children[1];

	TestFalse(TEXT("No-visible-stroke node should not have stroke"), NoVisibleStroke->Stroke.HasStroke());
	TestEqual(TEXT("No-visible-stroke thickness remains 0"), NoVisibleStroke->Stroke.Thickness.Uniform, 0.0f);
	TestTrue(TEXT("Visible-stroke node keeps stroke"), VisibleStroke->Stroke.HasStroke());
	TestEqual(TEXT("Visible-stroke align parsed"), VisibleStroke->Stroke.Align, EDesignStrokeAlign::Outside);

	return true;
}

// ============================================================
// Test 8.7: FigmaParser - uniform corner radii normalization
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaCornerRadiusNormalizationTest,
	"Design2UMG.FigmaParser.CornerRadiusNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaCornerRadiusNormalizationTest::RunTest(const FString& Parameters)
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
					"children": [
						{
							"id": "0:3",
							"name": "UniformRadius",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x":10,"y":10,"width":120,"height":40},
							"rectangleCornerRadii": [100,100,100,100]
						},
						{
							"id": "0:4",
							"name": "NonUniformRadius",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x":10,"y":70,"width":120,"height":40},
							"rectangleCornerRadii": [8,12,8,12]
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

	const TSharedPtr<FDesignNode>& Uniform = Doc.Children[0]->Children[0];
	const TSharedPtr<FDesignNode>& NonUniform = Doc.Children[0]->Children[1];

	TestTrue(TEXT("Equal radii normalized to uniform"), Uniform->CornerRadius.bUniform);
	TestEqual(TEXT("Equal radii keeps numeric value"), Uniform->CornerRadius.Uniform, 100.0f);
	TestFalse(TEXT("Mixed radii remains non-uniform"), NonUniform->CornerRadius.bUniform);

	return true;
}

// ============================================================
// Test 8.8: Converter - keep provided fit-size for text
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTextFitPreserveSizeTest,
	"Design2UMG.Converter.TextFitPreserveSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaTextFitPreserveSizeTest::RunTest(const FString& Parameters)
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
					"layoutMode": "VERTICAL",
					"absoluteBoundingBox": {"x":0,"y":0,"width":500,"height":200},
					"children": [
						{
							"id": "0:3",
							"name": "ButtonLabel",
							"type": "TEXT",
							"visible": true,
							"layoutSizingHorizontal": "HUG",
							"layoutSizingVertical": "HUG",
							"absoluteBoundingBox": {"x":20,"y":20,"width":63,"height":20},
							"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
							"style": {
								"fontFamily": "Inter",
								"fontSize": 18,
								"fontWeight": 600
							},
							"characters": "Download"
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0)
		return false;

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FString ExpectedText = TEXT("Download");
	const FUMGWidgetNode* Label = FindWidgetByProperty(Data.RootWidget, TEXT("Text"), &ExpectedText);
	TestTrue(TEXT("Find text widget"), Label != nullptr);
	if (!Label)
		return false;

	TestTrue(TEXT("Provided fit width should be preserved"), FMath::IsNearlyEqual(Label->Bounds.W, 63.0f, 0.5f));
	TestTrue(TEXT("Provided fit height should be preserved"), FMath::IsNearlyEqual(Label->Bounds.H, 20.0f, 0.5f));
	return true;
}

// ============================================================
// Test 8.9: PenParser - mesh_gradient + extended text fields
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenExtendedFieldsAndMeshGradientTest,
	"Design2UMG.PenParser.ExtendedFieldsAndMeshGradient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenExtendedFieldsAndMeshGradientTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "txt1",
				"type": "text",
				"content": "Hello",
				"fontStyle": "italic",
				"underline": true,
				"strikethrough": true,
				"href": "https://example.com",
				"fill": {
					"type": "mesh_gradient",
					"colors": [
						{"color": "#FF0000", "position": 0.0},
						{"color": "#00FF00", "position": 1.0}
					]
				}
			},
			{
				"id": "comp1",
				"type": "frame",
				"reusable": true,
				"layoutIncludeStroke": true,
				"stroke": {
					"align": "inside",
					"thickness": 1,
					"fill": "#000000",
					"join": "round",
					"miterAngle": 45,
					"cap": "square",
					"dashPattern": [2, 4]
				},
				"children": [
					{
						"id": "label",
						"type": "text",
						"content": "Default"
					}
				]
			},
			{
				"id": "inst1",
				"type": "ref",
				"ref": "comp1",
				"descendants": {
					"label": {
						"content": "Go",
						"fontStyle": "italic",
						"underline": true,
						"href": "https://override.example"
					}
				}
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Pen parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() < 2)
		return false;

	const TSharedPtr<FDesignNode>& TextNode = Doc.Children[0];
	TestEqual(TEXT("Text node type"), TextNode->Type, EDesignNodeType::Text);
	TestEqual(TEXT("fontStyle parsed"), TextNode->FontStyle, TEXT("italic"));
	TestTrue(TEXT("underline parsed"), TextNode->bUnderline);
	TestTrue(TEXT("strikethrough parsed"), TextNode->bStrikethrough);
	TestEqual(TEXT("href parsed"), TextNode->Href, TEXT("https://example.com"));
	TestEqual(TEXT("mesh_gradient maps to gradient"), TextNode->Fill.Type, EDesignFillType::Gradient);
	TestTrue(TEXT("mesh_gradient marker set"), TextNode->Fill.bFromMeshGradient);
	TestTrue(TEXT("mesh_gradient raw payload kept"), !TextNode->Fill.MeshGradientRaw.IsEmpty());
	const TSharedPtr<FDesignNode>& ComponentNode = Doc.Children[1];
	const FString* LayoutIncludeStroke = ComponentNode->Metadata.Find(TEXT("layoutIncludeStroke"));
	TestTrue(TEXT("layoutIncludeStroke preserved"), LayoutIncludeStroke && *LayoutIncludeStroke == TEXT("true"));
	const FString* StrokeJoin = ComponentNode->Metadata.Find(TEXT("stroke.join"));
	TestTrue(TEXT("stroke join preserved"), StrokeJoin && *StrokeJoin == TEXT("round"));
	const FString* StrokeCap = ComponentNode->Metadata.Find(TEXT("stroke.cap"));
	TestTrue(TEXT("stroke cap preserved"), StrokeCap && *StrokeCap == TEXT("square"));
	const FString* StrokeMiterAngle = ComponentNode->Metadata.Find(TEXT("stroke.miterAngle"));
	TestTrue(TEXT("stroke miterAngle preserved"), StrokeMiterAngle && !StrokeMiterAngle->IsEmpty());
	const FString* StrokeDashPattern = ComponentNode->Metadata.Find(TEXT("stroke.dashPattern"));
	TestTrue(TEXT("stroke dashPattern preserved"), StrokeDashPattern && StrokeDashPattern->Contains(TEXT("2")) && StrokeDashPattern->Contains(TEXT("4")));

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FString OverrideText = TEXT("Go");
	const FUMGWidgetNode* OverrideLabel = FindWidgetByProperty(Data.RootWidget, TEXT("Text"), &OverrideText);
	TestTrue(TEXT("Find override text widget"), OverrideLabel != nullptr);
	if (!OverrideLabel)
		return false;

	const FString* FontStyle = OverrideLabel->Properties.Find(TEXT("FontStyle"));
	TestTrue(TEXT("Override fontStyle propagated"), FontStyle && *FontStyle == TEXT("italic"));
	const FString* Underline = OverrideLabel->Properties.Find(TEXT("Underline"));
	TestTrue(TEXT("Override underline propagated"), Underline && *Underline == TEXT("true"));
	const FString* Href = OverrideLabel->Properties.Find(TEXT("Href"));
	TestTrue(TEXT("Override href propagated"), Href && *Href == TEXT("https://override.example"));

	return true;
}

// ============================================================
// Test 8.10: FigmaParser - GRID/WRAP and metadata preservation
// ============================================================

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

	TestEqual(TEXT("GRID layout parsed"), GridNode->Layout, EDesignLayout::Grid);
	TestEqual(TEXT("WRAP layout parsed"), WrapNode->Layout, EDesignLayout::Wrap);
	TestTrue(TEXT("GRID gap preserved"), FMath::IsNearlyEqual(GridNode->Gap, 12.0f, 0.001f));
	TestTrue(TEXT("WRAP gap preserved"), FMath::IsNearlyEqual(WrapNode->Gap, 8.0f, 0.001f));
	TestTrue(TEXT("GRID padding left preserved"), FMath::IsNearlyEqual(GridNode->Padding.Left, 10.0f, 0.001f));
	TestTrue(TEXT("WRAP padding top preserved"), FMath::IsNearlyEqual(WrapNode->Padding.Top, 7.0f, 0.001f));
	const FString* CounterAxisAlignContent = GridNode->Metadata.Find(TEXT("counterAxisAlignContent"));
	TestTrue(TEXT("counterAxisAlignContent preserved"), CounterAxisAlignContent && *CounterAxisAlignContent == TEXT("SPACE_BETWEEN"));
	const FString* LayoutPositioning = GridNode->Metadata.Find(TEXT("layoutPositioning"));
	TestTrue(TEXT("layoutPositioning preserved"), LayoutPositioning && *LayoutPositioning == TEXT("ABSOLUTE"));
	TestTrue(TEXT("layoutPositioning semantic flag"), GridNode->bAbsoluteLayoutPositioning);
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

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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

// ============================================================
// Test 8.10b: Empty slot arrays should preserve placeholder semantics
// ============================================================

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

	TestTrue(TEXT("Empty slot array should mark placeholder surface"), Doc.Children[0]->bSemanticPlaceholderSurface);
	TestTrue(TEXT("Non-empty slot array should mark placeholder surface"), Doc.Children[1]->bSemanticPlaceholderSurface);
	TestTrue(TEXT("Explicit placeholder=true should mark placeholder surface"), Doc.Children[2]->bSemanticPlaceholderSurface);
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

	TestTrue(TEXT("Empty slot array should mark placeholder surface"), Doc.Children[0]->Children[0]->bSemanticPlaceholderSurface);
	TestTrue(TEXT("Non-empty slot array should mark placeholder surface"), Doc.Children[0]->Children[1]->bSemanticPlaceholderSurface);
	TestTrue(TEXT("Explicit placeholder=true should mark placeholder surface"), Doc.Children[0]->Children[2]->bSemanticPlaceholderSurface);
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

	const FString SavedBg = Settings->PlaceholderHatchBackgroundColor;
	const FString SavedStripe = Settings->PlaceholderHatchStripeColor;
	Settings->PlaceholderHatchBackgroundColor = TEXT("#D282FF33");
	Settings->PlaceholderHatchStripeColor = TEXT("#D480FF");

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
		Settings->PlaceholderHatchBackgroundColor = SavedBg;
		Settings->PlaceholderHatchStripeColor = SavedStripe;
		return false;
	}

	TestEqual(TEXT("Empty slot metadata preserved"), Doc.Children[0]->Metadata.FindRef(TEXT("pen.slot.empty")), TEXT("true"));
	TestTrue(TEXT("Empty slot array keeps semantic placeholder"), Doc.Children[0]->bSemanticPlaceholderSurface);

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* SlotCell = FindWidgetById(Data.RootWidget, TEXT("slotCell"));
	const FUMGWidgetNode* PageSlot = FindWidgetById(Data.RootWidget, TEXT("pageSlot"));

	TestTrue(TEXT("slotCell widget exists"), SlotCell != nullptr);
	TestTrue(TEXT("pageSlot widget exists"), PageSlot != nullptr);
	if (!SlotCell || !PageSlot)
	{
		Settings->PlaceholderHatchBackgroundColor = SavedBg;
		Settings->PlaceholderHatchStripeColor = SavedStripe;
		return false;
	}

	TestEqual(TEXT("slotCell uses Image"), SlotCell->Type, TEXT("Image"));
	TestEqual(TEXT("slotCell placeholder material"), SlotCell->Properties.FindRef(TEXT("MaterialRef")), TEXT("placeholdersurface"));
	TestTrue(TEXT("slotCell placeholder params carry fill color"), SlotCell->Properties.FindRef(TEXT("MaterialParams")).Contains(TEXT("#D282FF33")));
	TestTrue(TEXT("slotCell placeholder params carry outline color"), SlotCell->Properties.FindRef(TEXT("MaterialParams")).Contains(TEXT("#D480FF")));
	TestTrue(TEXT("slotCell placeholder width override exists"), SlotCell->Properties.Contains(TEXT("WidthOverride")));
	TestTrue(TEXT("slotCell placeholder height override exists"), SlotCell->Properties.Contains(TEXT("HeightOverride")));

	TestEqual(TEXT("pageSlot uses Image"), PageSlot->Type, TEXT("Image"));
	TestEqual(TEXT("pageSlot placeholder material"), PageSlot->Properties.FindRef(TEXT("MaterialRef")), TEXT("placeholdersurface"));
	TestTrue(TEXT("pageSlot placeholder params carry fill color"), PageSlot->Properties.FindRef(TEXT("MaterialParams")).Contains(TEXT("#D282FF33")));
	TestTrue(TEXT("pageSlot placeholder params carry outline color"), PageSlot->Properties.FindRef(TEXT("MaterialParams")).Contains(TEXT("#D480FF")));

	Settings->PlaceholderHatchBackgroundColor = SavedBg;
	Settings->PlaceholderHatchStripeColor = SavedStripe;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaceholderSurfacePaletteTracksRefContextTest,
	"Design2UMG.Converter.PlaceholderSurfacePaletteTracksRefContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlaceholderSurfacePaletteTracksRefContextTest::RunTest(const FString& Parameters)
{
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
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	const FUMGWidgetNode* SlotBase = FindWidgetById(Data.RootWidget, TEXT("slotBase"));
	TestTrue(TEXT("slotBase exists"), SlotBase != nullptr);
	if (SlotBase != nullptr)
	{
		const FString Params = SlotBase->Properties.FindRef(TEXT("MaterialParams"));
		TestTrue(TEXT("Base placeholder keeps definition palette"), Params.Contains(TEXT("#D282FF33")));
		TestTrue(TEXT("Base placeholder keeps definition outline"), Params.Contains(TEXT("#D480FF")));
	}

	const FUMGWidgetNode* SlotInstance = FindWidgetById(Data.RootWidget, TEXT("slotInstance"));
	TestTrue(TEXT("slotInstance exists"), SlotInstance != nullptr);
	if (SlotInstance != nullptr)
	{
		const FString Params = SlotInstance->Properties.FindRef(TEXT("MaterialParams"));
		TestTrue(TEXT("Expanded ref placeholder uses instance palette"), Params.Contains(TEXT("#9682FF33")));
		TestTrue(TEXT("Expanded ref placeholder uses instance outline"), Params.Contains(TEXT("#9580FF")));
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

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Label = FindWidgetById(Data.RootWidget, TEXT("label"));
	TestNotNull(TEXT("Converted label exists"), Label);
	if (!Label)
	{
		return false;
	}

	TestEqual(TEXT("textAlignVertical kept as metadata"), Label->Properties.FindRef(TEXT("TextAlignVertical")), TEXT("Center"));
	TestEqual(TEXT("Parent slot stays top-aligned"), Label->Slot.VerticalAlignment, TEXT("Top"));
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
				"stroke": {
					"align": "outside",
					"thickness": { "right": 1 },
					"fill": "#FFFFFF"
				},
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

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Panel = FindWidgetById(Data.RootWidget, TEXT("panel"));
	TestNotNull(TEXT("Converted panel exists"), Panel);
	if (!Panel)
	{
		return false;
	}

	TestEqual(TEXT("Outside right stroke only adds right inset"), Panel->Properties.FindRef(TEXT("StrokePadding")), TEXT("0.0,0.0,0.5,0.0"));
	return true;
}

// ============================================================
// Test 8.11: Official-field parity smoke tests (.pen / Figma)
// ============================================================

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
			"stroke": {
				"align": "inside",
				"thickness": 2,
				"fill": "#112233",
				"join": "miter",
				"miterAngle": 30,
				"cap": "round",
				"dashPattern": [1, 2]
			},
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
	TestTrue(TEXT("stroke.join preserved"), FrameNode->Metadata.FindRef(TEXT("stroke.join")) == TEXT("miter"));
	TestTrue(TEXT("stroke.cap preserved"), FrameNode->Metadata.FindRef(TEXT("stroke.cap")) == TEXT("round"));
	TestTrue(TEXT("stroke.dashPattern preserved"), FrameNode->Metadata.FindRef(TEXT("stroke.dashPattern")).Contains(TEXT("1")));
	TestTrue(TEXT("mesh gradient preserved"), TextNode->Fill.bFromMeshGradient && !TextNode->Fill.MeshGradientRaw.IsEmpty());
	TestTrue(TEXT("mesh gradient rows/columns parsed"), TextNode->Fill.MeshGradient.bHasRows
		&& TextNode->Fill.MeshGradient.bHasColumns
		&& TextNode->Fill.MeshGradient.Rows == 2
		&& TextNode->Fill.MeshGradient.Columns == 2);
	TestTrue(TEXT("mesh gradient points parsed"), TextNode->Fill.MeshGradient.Points.Num() == 2
		&& TextNode->Fill.MeshGradient.Points[0].bHasPosition
		&& TextNode->Fill.MeshGradient.Points[0].InHandle.bHasValue
		&& TextNode->Fill.MeshGradient.Points[1].OutHandle.bHasValue);
	TestTrue(TEXT("fill blendMode preserved"), TextNode->Fill.BlendMode == TEXT("multiply"));
	TestTrue(TEXT("fill opacity preserved"), FMath::IsNearlyEqual(TextNode->Fill.Opacity, 0.42f, 0.001f));
	TestTrue(TEXT("text href preserved"), TextNode->Href == TEXT("https://docs.pencil.dev"));
	TestTrue(TEXT("text strikethrough preserved"), TextNode->bStrikethrough);
	TestTrue(TEXT("gradient center parsed"), GradientNode->Fill.Gradient.bHasCenter
		&& FMath::IsNearlyEqual(GradientNode->Fill.Gradient.CenterX, 0.2f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Fill.Gradient.CenterY, 0.8f, 0.001f));
	TestTrue(TEXT("gradient size parsed"), GradientNode->Fill.Gradient.bHasSize
		&& FMath::IsNearlyEqual(GradientNode->Fill.Gradient.SizeWidth, 2.5f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Fill.Gradient.SizeHeight, 1.25f, 0.001f));
	TestTrue(TEXT("shadow blendMode preserved"), GradientNode->Effects.Num() > 0
		&& GradientNode->Effects[0].BlendMode == TEXT("screen")
		&& GradientNode->Effects[0].Shadow.BlendMode == TEXT("screen"));
	TestTrue(TEXT("text content array fallback"), RichTextNode->Content == TEXT("Hi there"));
	TestTrue(TEXT("text content array raw preserved"), RichTextNode->Metadata.Contains(TEXT("text.content.raw")));
	TestTrue(TEXT("text runs parsed"), RichTextNode->TextRuns.Num() == 2
		&& RichTextNode->TextRuns[0].FontWeight == TEXT("700")
		&& RichTextNode->TextRuns[1].FontStyle == TEXT("italic"));
	TestTrue(TEXT("boolean variable parsed"), !RichTextNode->bEnabled && RichTextNode->bUnderline);
	TestTrue(TEXT("number variable parsed"), FMath::IsNearlyEqual(RichTextNode->Opacity, 0.66f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaOfficialFieldParityTest,
	"Design2UMG.Parity.FigmaOfficialFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaOfficialFieldParityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {"id":"0:0","name":"Doc","type":"DOCUMENT","prototypeStartNodeID":"0:2","prototypeDevice":{"type":"PRESET","size":"DESKTOP"},"children":[
			{"id":"0:1","name":"Page","type":"CANVAS","children":[
				{"id":"0:2","name":"Root","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":800,"height":600},"children":[
					{"id":"gridp","name":"GridParity","type":"FRAME","visible":true,"layoutMode":"GRID","layoutWrap":"WRAP","itemSpacing":8,
					 "paddingLeft":1,"paddingRight":2,"paddingTop":3,"paddingBottom":4,
					 "counterAxisAlignContent":"SPACE_BETWEEN","counterAxisSpacing":6,
					 "layoutPositioning":"ABSOLUTE","layoutAlign":"STRETCH",
					 "overflowDirection":"HORIZONTAL_SCROLLING",
					 "primaryAxisSizingMode":"AUTO","counterAxisSizingMode":"FIXED",
					 "itemReverseZIndex":true,"strokesIncludedInLayout":false,
					 "minWidth":100,"maxWidth":300,"minHeight":50,"maxHeight":200,
					 "gridRowCount":2,"gridColumnCount":3,"gridRowGap":10,"gridColumnGap":12,
					 "gridRowsSizing":"AUTO","gridColumnsSizing":"FIXED",
					 "gridChildHorizontalAlign":"CENTER","gridChildVerticalAlign":"MAX",
					 "gridRowSpan":2,"gridColumnSpan":1,"gridColumnAnchorIndex":0,"gridRowAnchorIndex":1,
					 "layoutGrids":[{"pattern":"GRID","sectionSize":8,"visible":true}],
					 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"NODE","destinationId":"1:2","navigation":"NAVIGATE"}]}],
					 "flowStartingPoints":[{"nodeId":"0:2","name":"Flow 1"}],
					 "componentPropertyReferences":{"width":"123:1"},
					 "explicitVariableModes":{"1":"2:3"},
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
					{"id":"inst1","name":"InstanceParity","type":"INSTANCE","componentId":"12:34","visible":true,
					 "componentProperties":{"title":{"type":"TEXT","value":"Hello"}},
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
	TestTrue(TEXT("figma annotations"), Node->Metadata.Contains(TEXT("annotations")));
	TestTrue(TEXT("figma measurements"), Node->Metadata.Contains(TEXT("measurements")));
	TestTrue(TEXT("figma exportSettings"), Node->Metadata.Contains(TEXT("exportSettings")));
	TestTrue(TEXT("figma devStatus"), Node->Metadata.Contains(TEXT("devStatus")));
	TestTrue(TEXT("figma branchData"), Node->Metadata.Contains(TEXT("branchData")));
	TestTrue(TEXT("figma doc prototypeStartNodeID"), Doc.Metadata.FindRef(TEXT("prototypeStartNodeID")) == TEXT("0:2"));
	TestTrue(TEXT("figma doc prototypeDevice"), Doc.Metadata.Contains(TEXT("prototypeDevice")));
	TestTrue(TEXT("figma absolute positioning semantic"), Node->bAbsoluteLayoutPositioning);
	TestTrue(TEXT("figma fill blendMode preserved"), PaintNode->Fill.BlendMode == TEXT("MULTIPLY"));
	TestTrue(TEXT("figma fill opacity preserved"), FMath::IsNearlyEqual(PaintNode->Fill.Opacity, 0.5f, 0.001f));
	TestTrue(TEXT("figma effect blendMode preserved"), PaintNode->Effects.Num() > 0
		&& PaintNode->Effects[0].BlendMode == TEXT("SCREEN")
		&& PaintNode->Effects[0].Shadow.BlendMode == TEXT("SCREEN"));
	TestTrue(TEXT("figma source node type preserved"), InstanceNode->Metadata.FindRef(TEXT("sourceNodeType")) == TEXT("INSTANCE"));
	TestTrue(TEXT("figma instance componentId preserved"), InstanceNode->Metadata.FindRef(TEXT("instanceComponentId")) == TEXT("12:34"));
	TestTrue(TEXT("figma instance componentProperties preserved"), InstanceNode->Metadata.Contains(TEXT("componentProperties")));
	TestTrue(TEXT("figma instance overrides preserved"), InstanceNode->Metadata.Contains(TEXT("overrides")));
	TestTrue(TEXT("figma instance isExposedInstance preserved"), InstanceNode->Metadata.FindRef(TEXT("isExposedInstance")) == TEXT("true"));
	return true;
}

// ============================================================
// Test 9: FigmaParser - ExtractFileKey URL 解析
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
// Test 10: FigmaParser → Converter 端到端

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

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestFalse(TEXT("E2E root type"), Data.RootWidget.Type.IsEmpty());
	TestTrue(TEXT("E2E has children"), Data.RootWidget.Children.Num() > 0);

	// 验证关键文本 Widget 存在
	TArray<FString> Texts;
	CollectTextValues(Data.RootWidget, Texts);
	TestTrue(TEXT("Contains Dashboard Overview"), Texts.Contains(TEXT("Dashboard Overview")));
	TestTrue(TEXT("Contains $48,290"), Texts.Contains(TEXT("$48,290")));
	TestTrue(TEXT("Contains Overview"), Texts.Contains(TEXT("Overview")));

	// CollectImageRefs 应该能收集到 imageRef
	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Doc, ImageRefs);
	TestTrue(TEXT("Has avatar imageRef"), ImageRefs.Contains(TEXT("img_avatar_001")));
	TestTrue(TEXT("Has card bg imageRef"), ImageRefs.Contains(TEXT("img_card_bg_002")));

	return true;
}

// ============================================================
// Test 11: FigmaParser - 真实 Dashboard 设计稿解析

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

	// 统计各节点类型数量，验证解析完整性
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

	// 端到端转换
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestFalse(TEXT("Widget root type not empty"), Data.RootWidget.Type.IsEmpty());
	TestTrue(TEXT("Widget has children"), Data.RootWidget.Children.Num() > 0);

	// 验证关键文本 Widget 存在
	TArray<FString> Texts;
	CollectTextValues(Data.RootWidget, Texts);
	TestTrue(TEXT("Has text content"), Texts.Num() > 0);

	return true;
}

#endif // WITH_AUTOMATION_WORKER



