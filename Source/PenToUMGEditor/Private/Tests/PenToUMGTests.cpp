// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "PenParser.h"
#include "PenToUMGConverter.h"
#include "LayoutCalculator.h"

#if WITH_AUTOMATION_WORKER

// ============================================================
// Test 1: Parser - 解析硬编码 JSON，验证节点类型/数量/属性
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenParserTest,
	"PenToUMG.Parser.BasicDocument",
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
	"PenToUMG.Converter.BasicConversion",
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

	FUMGWidgetData Data = FPenToUMGConverter::Convert(Doc);

	// Root widget 应该存在
	TestFalse(TEXT("Root type not empty"), Data.RootWidget.Type.IsEmpty());
	TestTrue(TEXT("Root has children"), Data.RootWidget.Children.Num() > 0);

	// 降级警告：Ellipse 应产生一条警告
	TestTrue(TEXT("Has degradation warnings"), Data.Warnings.Num() > 0);

	bool bFoundEllipseWarning = false;
	for (const FString& W : Data.Warnings)
	{
		if (W.Contains(TEXT("Ellipse")) || W.Contains(TEXT("ellipse")))
		{
			bFoundEllipseWarning = true;
			break;
		}
	}
	TestTrue(TEXT("Ellipse degradation warning present"), bFoundEllipseWarning);

	return true;
}

// ============================================================
// Test 3: LayoutCalculator - 9宫格锚点选择逻辑
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutCalculatorTest,
	"PenToUMG.Layout.NineGridAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLayoutCalculatorTest::RunTest(const FString& Parameters)
{
	FLayoutContext Ctx;
	Ctx.DesignSize = FIntPoint(1920, 1080);

	// Case 1: 左上角元素 → 锚点 (0,0)
	{
		FBounds B;
		B.X = 50.0f; B.Y = 30.0f; B.W = 200.0f; B.H = 100.0f;
		FUMGSlotData Slot = FLayoutCalculator::CalculateSlot(B, Ctx);
		TestEqual(TEXT("TopLeft anchor MinX"), Slot.Anchors.Min.X, 0.0f);
		TestEqual(TEXT("TopLeft anchor MinY"), Slot.Anchors.Min.Y, 0.0f);
	}

	// Case 2: 居中元素 → 锚点 (0.5, 0.5)
	{
		FBounds B;
		B.W = 300.0f; B.H = 200.0f;
		B.X = (1920.0f - B.W) / 2.0f;
		B.Y = (1080.0f - B.H) / 2.0f;
		FUMGSlotData Slot = FLayoutCalculator::CalculateSlot(B, Ctx);
		TestEqual(TEXT("Center anchor MinX"), Slot.Anchors.Min.X, 0.5f);
		TestEqual(TEXT("Center anchor MinY"), Slot.Anchors.Min.Y, 0.5f);
	}

	// Case 3: 右下角元素 → 锚点 (1,1)
	{
		FBounds B;
		B.W = 200.0f; B.H = 100.0f;
		B.X = 1920.0f - B.W - 30.0f;
		B.Y = 1080.0f - B.H - 20.0f;
		FUMGSlotData Slot = FLayoutCalculator::CalculateSlot(B, Ctx);
		TestEqual(TEXT("BottomRight anchor MinX"), Slot.Anchors.Min.X, 1.0f);
		TestEqual(TEXT("BottomRight anchor MinY"), Slot.Anchors.Min.Y, 1.0f);
	}

	// Case 4: 宽度超过 60% 画布 → 拉伸锚点 (0,0)-(1,0)
	{
		FBounds B;
		B.X = 50.0f; B.Y = 50.0f;
		B.W = 1800.0f; B.H = 100.0f;  // ~93.75% 宽度
		FUMGSlotData Slot = FLayoutCalculator::CalculateSlot(B, Ctx);
		TestTrue(TEXT("Stretch anchor: MinX < MaxX"),
			Slot.Anchors.Min.X < Slot.Anchors.Max.X);
	}

	return true;
}

#endif // WITH_AUTOMATION_WORKER
