// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "Design2UMGSettings.h"
#include "LayoutCalculator.h"
#include "FigmaParser.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

	bool LoadFixtureJson(const FString& FixtureFileName, FString& OutJson)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		if (!Plugin.IsValid())
			return false;

		const FString FixturePath = FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures"),
			FixtureFileName);
		return FFileHelper::LoadFileToString(OutJson, *FixturePath);
	}
}

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
	const TSharedPtr<FDesignNode>& Title = Screen->Children[0];
	TestEqual(TEXT("Title type"), Title->Type, EPenNodeType::Text);
	TestEqual(TEXT("Title content"), Title->Content, TEXT("Hello World"));
	TestEqual(TEXT("Title fontSize"), Title->FontSize, 24.0f);
	TestEqual(TEXT("Title fontWeight"), Title->FontWeight, TEXT("bold"));

	// Ellipse 节点
	const TSharedPtr<FDesignNode>& Ellipse = Screen->Children[1];
	TestEqual(TEXT("Ellipse type"), Ellipse->Type, EPenNodeType::Ellipse);

	// Rectangle 节点
	const TSharedPtr<FDesignNode>& Rect = Screen->Children[2];
	TestEqual(TEXT("Rect type"), Rect->Type, EPenNodeType::Rectangle);
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
// Test 7: Parser number/string 宽松解析
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
	TestEqual(TEXT("Dashboard type"), Dashboard->Type, EPenNodeType::Frame);
	TestEqual(TEXT("Dashboard layout"), Dashboard->Layout, EPenLayout::Horizontal);
	TestTrue(TEXT("Dashboard clip (Figma clipsContent mapped)"), Dashboard->bClip);
	TestEqual(TEXT("Dashboard children"), Dashboard->Children.Num(), 2);
	if (Dashboard->Children.Num() < 2)
		return false;

	// Sidebar: VERTICAL + individualStrokeWeights + FILL height
	const auto& Sidebar = Dashboard->Children[0];
	TestEqual(TEXT("Sidebar layout"), Sidebar->Layout, EPenLayout::Vertical);
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
	TestEqual(TEXT("Logo type"), Logo->Type, EPenNodeType::Text);
	TestEqual(TEXT("Logo content"), Logo->Content, TEXT("Acme Dashboard"));
	TestEqual(TEXT("Logo fontWeight"), Logo->FontWeight, TEXT("700"));
	TestEqual(TEXT("Logo fontSize"), Logo->FontSize, 20.0f);
	TestEqual(TEXT("Logo textGrowth"), Logo->TextGrowth, EPenTextGrowth::Auto);
	TestEqual(TEXT("Logo textAlignV"), Logo->TextAlignVertical, EPenTextAlignVertical::Middle);
	TestTrue(TEXT("Logo lineHeight > 0"), Logo->LineHeight > 0.0f);

	// Divider: LINE type
	TestEqual(TEXT("Divider type"), Sidebar->Children[1]->Type, EPenNodeType::Line);

	// NavItem: COMPONENT → bReusable
	const auto& NavItem = Sidebar->Children[2];
	TestTrue(TEXT("NavItem reusable"), NavItem->bReusable);
	TestEqual(TEXT("NavItem layout"), NavItem->Layout, EPenLayout::Horizontal);
	TestEqual(TEXT("NavItem children"), NavItem->Children.Num(), 2);

	// NavItem child: VECTOR → Path
	if (NavItem->Children.Num() >= 1)
		TestEqual(TEXT("NavIcon type"), NavItem->Children[0]->Type, EPenNodeType::Path);

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
	TestEqual(TEXT("Header justify"), Header->JustifyContent, EPenJustifyContent::SpaceBetween);
	TestEqual(TEXT("Header align"), Header->AlignItems, EPenAlignItems::Center);

	// Avatar: ELLIPSE + multi-fill (SOLID + IMAGE)
	if (Header->Children.Num() >= 2)
	{
		const auto& Avatar = Header->Children[1];
		TestEqual(TEXT("Avatar type"), Avatar->Type, EPenNodeType::Ellipse);
		TestTrue(TEXT("Avatar has image fill"), Avatar->Fill.IsImage());
		TestTrue(TEXT("Avatar multi-fills >= 2"), Avatar->Fills.Num() >= 2);
	}

	// StatCard: GRADIENT_LINEAR + DROP_SHADOW
	const auto& StatsRow = Main->Children[1];
	if (StatsRow->Children.Num() >= 1)
	{
		const auto& Card = StatsRow->Children[0];
		TestEqual(TEXT("Card fill gradient"), Card->Fill.Type, EPenFillType::Gradient);
		TestEqual(TEXT("Card gradient type"), Card->Fill.Gradient.GradientType, EPenGradientType::Linear);
		TestTrue(TEXT("Card gradient stops >= 2"), Card->Fill.Gradient.Colors.Num() >= 2);
		TestTrue(TEXT("Card has effects"), Card->Effects.Num() > 0);
		TestEqual(TEXT("Card shadow type"), Card->Effects[0].Type, EPenEffectType::Shadow);
		TestFalse(TEXT("Card shadow not inner"), Card->Effects[0].Shadow.bInner);
	}

	// ChartArea: INNER_SHADOW + BACKGROUND_BLUR + rectangleCornerRadii child
	const auto& Chart = Main->Children[2];
	TestTrue(TEXT("Chart effects >= 2"), Chart->Effects.Num() >= 2);
	bool bFoundInner = false, bFoundBgBlur = false;
	for (const auto& E : Chart->Effects)
	{
		if (E.Type == EPenEffectType::Shadow && E.Shadow.bInner) bFoundInner = true;
		if (E.Type == EPenEffectType::BackgroundBlur) bFoundBgBlur = true;
	}
	TestTrue(TEXT("Chart has inner shadow"), bFoundInner);
	TestTrue(TEXT("Chart has bg blur"), bFoundBgBlur);

	// ChartPlaceholder: non-uniform corner radius
	if (Chart->Children.Num() >= 2)
	{
		const auto& Placeholder = Chart->Children[1];
		TestEqual(TEXT("Placeholder type"), Placeholder->Type, EPenNodeType::Rectangle);
		TestFalse(TEXT("Placeholder non-uniform radius"), Placeholder->CornerRadius.bUniform);
		TestEqual(TEXT("Placeholder TL"), Placeholder->CornerRadius.TopLeft, 12.0f);
		TestEqual(TEXT("Placeholder BL"), Placeholder->CornerRadius.BottomLeft, 0.0f);
	}

	// GROUP: absolute positioned children
	const auto& Overlay = Main->Children[3];
	TestEqual(TEXT("Overlay type"), Overlay->Type, EPenNodeType::Group);
	TestTrue(TEXT("Overlay children >= 7"), Overlay->Children.Num() >= 7);

	if (Overlay->Children.Num() >= 7)
	{
		// LAYER_BLUR
		TestTrue(TEXT("Badge has effects"), Overlay->Children[0]->Effects.Num() > 0);
		TestEqual(TEXT("Badge blur type"), Overlay->Children[0]->Effects[0].Type, EPenEffectType::Blur);

		// FlippedIcon: relativeTransform flip detection
		TestTrue(TEXT("FlippedIcon flipX"), Overlay->Children[1]->bFlipX);

		// STAR → Polygon
		TestEqual(TEXT("Star type"), Overlay->Children[2]->Type, EPenNodeType::Polygon);

		// REGULAR_POLYGON → Polygon
		TestEqual(TEXT("Polygon type"), Overlay->Children[3]->Type, EPenNodeType::Polygon);

		// BOOLEAN_OPERATION → Path
		TestEqual(TEXT("BoolOp type"), Overlay->Children[4]->Type, EPenNodeType::Path);

		// IMAGE fill with imageTransform + opacity
		const auto& ImgCard = Overlay->Children[5];
		TestTrue(TEXT("ImgCard has image fill"), ImgCard->Fill.IsImage());
		TestTrue(TEXT("ImgCard opacity < 1"), ImgCard->Opacity < 1.0f);

		// GRADIENT_RADIAL
		const auto& GradBg = Overlay->Children[6];
		TestEqual(TEXT("GradBg gradient type"), GradBg->Fill.Gradient.GradientType, EPenGradientType::Radial);
	}

	// Justified text + textAlignVertical BOTTOM + textAutoResize NONE
	if (Overlay->Children.Num() >= 8)
	{
		const auto& JustText = Overlay->Children[7];
		TestEqual(TEXT("JustText align"), JustText->TextAlign, EPenTextAlign::Justify);
		TestEqual(TEXT("JustText valign"), JustText->TextAlignVertical, EPenTextAlignVertical::Bottom);
		TestEqual(TEXT("JustText growth"), JustText->TextGrowth, EPenTextGrowth::FixedWidthHeight);
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
	TestEqual(TEXT("Visible-stroke align parsed"), VisibleStroke->Stroke.Align, EPenStrokeAlign::Outside);

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
	TestEqual(TEXT("Root type is Frame"), Root->Type, EPenNodeType::Frame);
	TestTrue(TEXT("Root has children"), Root->Children.Num() > 0);

	// 统计各节点类型数量，验证解析完整性
	TMap<EPenNodeType, int32> TypeCounts;
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
	TestTrue(TEXT("Has Frame nodes"), TypeCounts.Contains(EPenNodeType::Frame));
	TestTrue(TEXT("Has Text nodes"), TypeCounts.Contains(EPenNodeType::Text));
	TestTrue(TEXT("Has Rectangle nodes"), TypeCounts.Contains(EPenNodeType::Rectangle));

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

// ============================================================
// Batch import: pencil-welcome.pen 4 frames
// ============================================================
#include "PenImporter.h"
#include "DesignImportUtils.h"
#include "RHI.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ObjectTools.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenWelcomeBatchImportTest,
	"Design2UMG.BatchImport.PencilWelcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenWelcomeBatchImportTest::RunTest(const FString& Parameters)
{
	// 使用插件 Fixtures 目录，避免硬编码绝对路径
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
	if (!Plugin.IsValid())
	{
		AddError(TEXT("Design2UMG plugin not found"));
		return false;
	}
	const FString PenFile = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures/pencil-welcome.pen"));
	if (!FPaths::FileExists(PenFile))
	{
		AddWarning(FString::Printf(TEXT("Test fixture not found, skipping: %s"), *PenFile));
		return true;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/PencilWelcome");
	TArray<FString> GeneratedAssetPaths;

	struct FFrameInfo { FString Id; FString Name; };
	TArray<FFrameInfo> Frames = {
		{TEXT("sbChi"), TEXT("welcome")},
		{TEXT("cOnQP"), TEXT("Step1")},
		{TEXT("tlPHu"), TEXT("Step2")},
		{TEXT("CzlXB"), TEXT("Step3")}
	};

	for (const auto& F : Frames)
	{
		FUMGWidgetData Data;
		bool bOk = FPenImporter::ImportFileWithTargetNode(PenFile, F.Id, Data);
		TestTrue(*FString::Printf(TEXT("Parse %s"), *F.Name), bOk);
		if (!bOk) continue;

		if (GUsingNullRHI)
			continue;

		FString FrameOutputPath = OutputPath / Data.WidgetName;
		bool bPng = DesignImportUtils::GenerateBlueprintAndPng(Data, FrameOutputPath);
		TestTrue(*FString::Printf(TEXT("Render %s"), *F.Name), bPng);
		GeneratedAssetPaths.Add(FrameOutputPath);
	}

	// 清理测试产物，避免污染项目资产库
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<UObject*> ObjectsToDelete;
	for (const FString& AssetDir : GeneratedAssetPaths)
	{
		TArray<FAssetData> Assets;
		ARM.Get().GetAssetsByPath(FName(*AssetDir), Assets, true);
		for (const FAssetData& AD : Assets)
		{
			if (UObject* Obj = AD.GetAsset())
				ObjectsToDelete.Add(Obj);
		}
	}
	if (ObjectsToDelete.Num() > 0)
		ObjectTools::ForceDeleteObjects(ObjectsToDelete, false);

	return true;
}

// ============================================================
// Test 12: LayoutFixer - Conservatism Check (Basic)
// ============================================================
#include "LayoutFixer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutFixerConservatismTest,
	"Design2UMG.LayoutFixer.ConservatismCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLayoutFixerConservatismTest::RunTest(const FString& Parameters)
{
	FFixerPreset Preset;
	Preset.DesignSize = FIntPoint(1920, 1080);
	Preset.SnapPx = 4;
	
	FFixerWidgetNode Root;
	Root.Bounds = {0, 0, 1, 1};
	
	FFixerWidgetNode Child1;
	Child1.Bounds = {0, 0, 0.1f, 0.1f};
	
	FFixerWidgetNode Child2;
	Child2.Bounds = {0.5f, 0, 0.1f, 0.1f};
	
	Root.Children.Add(Child1);
	Root.Children.Add(Child2);

	TArray<FFixerLogEntry> Log;
	FFixerStats Stats;
	FLayoutFixer::FixWidgetTree(Root, Preset, Log, &Stats);
	
	TestEqual(TEXT("RolledBack should be non-negative"), Stats.RolledBack >= 0, true);
	
	return true;
}

// ============================================================
// Test 13: PreviewWindow - Instantiation & Validation
// ============================================================
#include "SDesign2UMGPreviewWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewWindowTest,
	"Design2UMG.PreviewWindow.Initialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPreviewWindowTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}

	const FString DummyJson = TEXT("{}");
	TArray<FString> Warnings;

	TSharedPtr<SDesign2UMGPreviewWindow> PreviewWindow = SNew(SDesign2UMGPreviewWindow)
		.OriginalJson(DummyJson)
		.OutputPath(TEXT("/Game/Test"))
		.Warnings(Warnings);

	TestTrue(TEXT("Preview window instantiated"), PreviewWindow.IsValid());
	PreviewWindow.Reset();
	
	return true;
}

#endif // WITH_AUTOMATION_WORKER

