// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "Design2UMGSettings.h"
#include "DesignVariableResolver.h"
#include "Design2UMGTestUtils.h"
#include "FontResolver.h"
#include "LayoutCalculator.h"
#include "LayoutMapper.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetUtils.h"
#include "VisualMapper.h"
#include "FigmaParser.h"
#include "DesignPathRasterizer.h"
#include "ProceduralShapeGenerator.h"
#include "TextMeasurement.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/RetainerBox.h"
#include "Components/EditableTextBox.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "UObject/UObjectGlobals.h"
#if WITH_AUTOMATION_WORKER
using namespace Design2UMGTestUtils;
namespace
{
	constexpr TCHAR RichTextStyleRowsPropertyKey[] = TEXT("Design2UMG.RichTextStyleRows");

	struct FScopedLuminosityFitOpacityCap
	{
		explicit FScopedLuminosityFitOpacityCap(const float NewValue, const bool bEnableOverride = true)
		{
			Settings = UDesign2UMGSettings::Get();
			if (Settings)
			{
				bSavedUseCustomOverride = Settings->bUseCustomLuminosityFitOpacityCap;
				SavedValue = Settings->LuminosityFitOpacityCap;
				Settings->bUseCustomLuminosityFitOpacityCap = bEnableOverride;
				Settings->LuminosityFitOpacityCap = NewValue;
			}
		}

		~FScopedLuminosityFitOpacityCap()
		{
			if (Settings)
			{
				Settings->bUseCustomLuminosityFitOpacityCap = bSavedUseCustomOverride;
				Settings->LuminosityFitOpacityCap = SavedValue;
			}
		}

		UDesign2UMGSettings* Settings = nullptr;
		bool bSavedUseCustomOverride = false;
		float SavedValue = 0.15f;
	};

	struct FScopedPlaceholderHatchStyleOverride
	{
		FScopedPlaceholderHatchStyleOverride(
			const bool bEnableOverride,
			const FString& InBaseBg,
			const FString& InBaseStripe,
			const FString& InRefBg,
			const FString& InRefStripe,
			const int32 InStripeWidth = 1,
			const int32 InStripeGap = 4)
		{
			Settings = UDesign2UMGSettings::Get();
			if (Settings)
			{
				bSavedUseCustomOverride = Settings->bUseCustomPlaceholderHatchStyle;
				SavedBaseBg = Settings->PlaceholderHatchBackgroundColor;
				SavedBaseStripe = Settings->PlaceholderHatchStripeColor;
				SavedRefBg = Settings->PlaceholderRefHatchBackgroundColor;
				SavedRefStripe = Settings->PlaceholderRefHatchStripeColor;
				SavedStripeWidth = Settings->PlaceholderHatchStripeWidth;
				SavedStripeGap = Settings->PlaceholderHatchStripeGap;

				Settings->bUseCustomPlaceholderHatchStyle = bEnableOverride;
				Settings->PlaceholderHatchBackgroundColor = InBaseBg;
				Settings->PlaceholderHatchStripeColor = InBaseStripe;
				Settings->PlaceholderRefHatchBackgroundColor = InRefBg;
				Settings->PlaceholderRefHatchStripeColor = InRefStripe;
				Settings->PlaceholderHatchStripeWidth = InStripeWidth;
				Settings->PlaceholderHatchStripeGap = InStripeGap;
			}
		}

		~FScopedPlaceholderHatchStyleOverride()
		{
			if (Settings)
			{
				Settings->bUseCustomPlaceholderHatchStyle = bSavedUseCustomOverride;
				Settings->PlaceholderHatchBackgroundColor = SavedBaseBg;
				Settings->PlaceholderHatchStripeColor = SavedBaseStripe;
				Settings->PlaceholderRefHatchBackgroundColor = SavedRefBg;
				Settings->PlaceholderRefHatchStripeColor = SavedRefStripe;
				Settings->PlaceholderHatchStripeWidth = SavedStripeWidth;
				Settings->PlaceholderHatchStripeGap = SavedStripeGap;
			}
		}

		UDesign2UMGSettings* Settings = nullptr;
		bool bSavedUseCustomOverride = false;
		FString SavedBaseBg;
		FString SavedBaseStripe;
		FString SavedRefBg;
		FString SavedRefStripe;
		int32 SavedStripeWidth = 1;
		int32 SavedStripeGap = 4;
	};

	int32 CountOpaqueTexturePixels(UTexture2D* Texture)
	{
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() <= 0)
		{
			return 0;
		}

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		const void* LockedData = Mip.BulkData.LockReadOnly();
		if (!LockedData)
		{
			Mip.BulkData.Unlock();
			return 0;
		}

		const FColor* Pixels = static_cast<const FColor*>(LockedData);
		const int32 PixelCount = Mip.SizeX * Mip.SizeY;
		int32 OpaqueCount = 0;
		for (int32 Index = 0; Index < PixelCount; ++Index)
		{
			if (Pixels[Index].A > 0)
			{
				++OpaqueCount;
			}
		}

		Mip.BulkData.Unlock();
		return OpaqueCount;
	}

	bool ReadTexturePixel(UTexture2D* Texture, int32 X, int32 Y, FColor& OutPixel)
	{
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() <= 0)
		{
			return false;
		}

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		if (X < 0 || Y < 0 || X >= Mip.SizeX || Y >= Mip.SizeY)
		{
			return false;
		}

		const void* LockedData = Mip.BulkData.LockReadOnly();
		if (!LockedData)
		{
			Mip.BulkData.Unlock();
			return false;
		}

		const FColor* Pixels = static_cast<const FColor*>(LockedData);
		OutPixel = Pixels[Y * Mip.SizeX + X];
		Mip.BulkData.Unlock();
		return true;
	}

	TSharedPtr<FDesignNode> MakeSharedCalibrationTextNode(const FString& Id, const FString& Content)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Id;
		Node->Type = EDesignNodeType::Text;
		Node->Content = Content;
		Node->FontFamily = TEXT("Geist");
		Node->FontWeight = TEXT("Regular");
		Node->FontSize = 36.0f;
		Node->LineHeight = 1.35f;
		Node->LetterSpacing = 0.0f;
		Node->TextAlign = EDesignTextAlign::Left;
		Node->TextAlignVertical = EDesignTextAlignVertical::Middle;
		Node->TextGrowth = EDesignTextGrowth::FixedWidth;
		Node->Width = FDesignSize::Fixed(80.0f);
		Node->Height = FDesignSize::Fit(0.0f);
		Node->bHasWidth = true;
		Node->bHasHeight = true;
		return Node;
	}

	TSharedPtr<FDesignNode> MakeSharedCalibrationContainer(const FString& Id)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Id;
		Node->Type = EDesignNodeType::Frame;
		Node->Layout = EDesignLayout::Vertical;
		Node->Width = FDesignSize::Fixed(80.0f);
		Node->Height = FDesignSize::Fit(0.0f);
		Node->bHasWidth = true;
		Node->bHasHeight = true;
		return Node;
	}

	FDesignDocument BuildSharedCalibrationFitContentDocument(const bool bExplicitFirst, const bool bUseRef)
	{
		const FString AutoContent = TEXT("Design2UMG text calibration should stay stable across different wrap widths and never depend on evaluation order.");
		const FString ExplicitContent = TEXT("Design2UMG text calibration should stay stable across\ndifferent wrap widths and never depend on evaluation order.");
		TSharedPtr<FDesignNode> AutoNode = MakeSharedCalibrationTextNode(TEXT("shared_auto"), AutoContent);
		TSharedPtr<FDesignNode> ExplicitNode = MakeSharedCalibrationTextNode(TEXT("shared_explicit"), ExplicitContent);

		auto AppendChildren = [&](const TSharedPtr<FDesignNode>& Parent)
		{
			if (bExplicitFirst)
			{
				Parent->Children.Add(ExplicitNode);
				Parent->Children.Add(AutoNode);
			}
			else
			{
				Parent->Children.Add(AutoNode);
				Parent->Children.Add(ExplicitNode);
			}
		};

		FDesignDocument Doc;
		if (bUseRef)
		{
			TSharedPtr<FDesignNode> Component = MakeSharedCalibrationContainer(TEXT("shared_component"));
			Component->bReusable = true;
			AppendChildren(Component);
			Doc.Children.Add(Component);

			TSharedPtr<FDesignNode> Screen = MakeSharedCalibrationContainer(TEXT("screen"));
			TSharedPtr<FDesignNode> Instance = MakeShared<FDesignNode>();
			Instance->Id = TEXT("shared_component_instance");
			Instance->Name = TEXT("shared_component_instance");
			Instance->Type = EDesignNodeType::Ref;
			Instance->Ref = TEXT("shared_component");
			Screen->Children.Add(Instance);
			Doc.Children.Add(Screen);
			return Doc;
		}

		TSharedPtr<FDesignNode> Screen = MakeSharedCalibrationContainer(TEXT("screen"));
		AppendChildren(Screen);
		Doc.Children.Add(Screen);
		return Doc;
	}

	bool ExtractSharedCalibrationHeights(
		const FUMGWidgetData& Data,
		FAutomationTestBase& Test,
		float& OutRootHeight,
		float& OutExplicitHeight,
		float& OutAutoHeight)
	{
		const FUMGWidgetNode* RootWidget = FindWidgetById(Data.RootWidget, TEXT("screen"));
		Test.TestTrue(TEXT("Find fit-content root"), RootWidget != nullptr);
		if (!RootWidget)
		{
			return false;
		}

		const FUMGWidgetNode* ExplicitWidget = FindWidgetById(Data.RootWidget, TEXT("shared_explicit"));
		Test.TestTrue(TEXT("Find explicit multiline text"), ExplicitWidget != nullptr);
		if (!ExplicitWidget)
		{
			return false;
		}

		const FUMGWidgetNode* AutoWidget = FindWidgetById(Data.RootWidget, TEXT("shared_auto"));
		Test.TestTrue(TEXT("Find auto-wrap text"), AutoWidget != nullptr);
		if (!AutoWidget)
		{
			return false;
		}

		OutRootHeight = RootWidget->Bounds.H;
		OutExplicitHeight = ExplicitWidget->Bounds.H;
		OutAutoHeight = AutoWidget->Bounds.H;
		return true;
	}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropertyBagEmptyStringStillAppliesTest,
	"Design2UMG.PropertyBag.EmptyStringApplies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPropertyBagEmptyStringStillAppliesTest::RunTest(const FString& Parameters)
{
	UEditableTextBox* Widget = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("EditableTextBox created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	Widget->SetText(FText::FromString(TEXT("Existing Text")));
	Widget->SetHintText(FText::FromString(TEXT("Existing Hint")));

	FUMGPropertyBag Properties;
	Properties.Add(TEXT("Text"), TEXT(""));
	Properties.Add(TEXT("HintText"), TEXT(""));
	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Properties);

	TestTrue(TEXT("Explicit empty Text still clears widget text"), Widget->GetText().ToString().IsEmpty());
	TestTrue(TEXT("Explicit empty HintText still clears widget hint"), Widget->GetHintText().ToString().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropertyBagDynamicFloatKeyTest,
	"Design2UMG.PropertyBag.DynamicFloatKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPropertyBagDynamicFloatKeyTest::RunTest(const FString& Parameters)
{
	FUMGPropertyBag Properties;
	Properties.Add(TEXT("BoxShadow_0_OffsetX"), TEXT("12.5"));
	Properties.Add(TEXT("BoxShadow_0_OffsetY"), TEXT("-4.25"));
	Properties.Add(TEXT("BoxShadow_0_Blur"), TEXT("16"));

	float OffsetX = 0.0f;
	float OffsetY = 0.0f;
	float Blur = 0.0f;
	TestTrue(TEXT("Dynamic OffsetX key is readable as float"), Properties.TryGetFloat(TEXT("BoxShadow_0_OffsetX"), OffsetX));
	TestTrue(TEXT("Dynamic OffsetY key is readable as float"), Properties.TryGetFloat(TEXT("BoxShadow_0_OffsetY"), OffsetY));
	TestTrue(TEXT("Dynamic Blur key is readable as float"), Properties.TryGetFloat(TEXT("BoxShadow_0_Blur"), Blur));
	TestEqual(TEXT("OffsetX value preserved"), OffsetX, 12.5f);
	TestEqual(TEXT("OffsetY value preserved"), OffsetY, -4.25f);
	TestEqual(TEXT("Blur value preserved"), Blur, 16.0f);
	return true;
}

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderDirectTest,
	"Design2UMG.VisualMapper.WrapWithBorderDirect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperWrapWithBorderDirectTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Type = EDesignNodeType::Frame;
	Node->Opacity = 1.0f;

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Border");
	Widget.Id = TEXT("panel");
	Widget.Name = TEXT("Panel");
	Widget.Bounds.W = 240.0f;
	Widget.Bounds.H = 60.0f;
	Widget.Properties.Add(TEXT("BrushColor"), TEXT("#112233FF"));
	Widget.Properties.Add(TEXT("RenderOpacity"), TEXT("0.6"));
	Widget.Properties.Add(TEXT("RenderAngle"), TEXT("15.0"));
	Widget.Properties.Add(TEXT("Clipping"), TEXT("ClipToBounds"));

	FUMGWidgetNode Wrapped = FVisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("Wrapped node remains Border"), Wrapped.Type, TEXT("Border"));
	TestEqual(TEXT("BrushColor moved to outer border"), GetWidgetProperty(Wrapped, TEXT("BrushColor")), TEXT("#112233FF"));
	TestEqual(TEXT("RenderOpacity moved to outer border"), GetWidgetProperty(Wrapped, TEXT("RenderOpacity")), TEXT("0.6"));
	TestEqual(TEXT("RenderAngle moved to outer border"), GetWidgetProperty(Wrapped, TEXT("RenderAngle")), TEXT("15.0"));
	TestEqual(TEXT("Clipping moved to outer border"), GetWidgetProperty(Wrapped, TEXT("Clipping")), TEXT("ClipToBounds"));
	TestFalse(TEXT("Outer border should not keep legacy BrushColor"), HasLegacyWidgetProperty(Wrapped, TEXT("BrushColor")));
	TestFalse(TEXT("Outer border should not keep legacy RenderOpacity"), HasLegacyWidgetProperty(Wrapped, TEXT("RenderOpacity")));
	TestFalse(TEXT("Outer border should not keep legacy RenderAngle"), HasLegacyWidgetProperty(Wrapped, TEXT("RenderAngle")));
	TestFalse(TEXT("Outer border should not keep legacy Clipping"), HasLegacyWidgetProperty(Wrapped, TEXT("Clipping")));
	TestEqual(TEXT("Wrapped child count"), Wrapped.Children.Num(), 1);
	if (Wrapped.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& Inner = Wrapped.Children[0];
	TestEqual(TEXT("Inner widget id suffix"), Inner.Id, TEXT("panel_inner"));
	TestEqual(TEXT("Inner widget name suffix"), Inner.Name, TEXT("Panel_inner"));
	TestFalse(TEXT("Inner widget should not keep BrushColor"), HasWidgetProperty(Inner, TEXT("BrushColor")));
	TestFalse(TEXT("Inner widget should not keep RenderOpacity"), HasWidgetProperty(Inner, TEXT("RenderOpacity")));
	TestFalse(TEXT("Inner widget should not keep RenderAngle"), HasWidgetProperty(Inner, TEXT("RenderAngle")));
	TestFalse(TEXT("Inner widget should not keep Clipping"), HasWidgetProperty(Inner, TEXT("Clipping")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualMapperWrapWithBorderTypedTest,
	"Design2UMG.VisualMapper.WrapWithBorderTyped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVisualMapperWrapWithBorderTypedTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Type = EDesignNodeType::Frame;
	Node->Opacity = 1.0f;

	FUMGWidgetNode Widget;
	Widget.Type = TEXT("Border");
	Widget.Id = TEXT("typed_panel");
	Widget.Name = TEXT("TypedPanel");
	Widget.Bounds.W = 320.0f;
	Widget.Bounds.H = 72.0f;
	Widget.Brush.BrushColor = FLinearColor(0.25f, 0.5f, 0.75f, 1.0f);
	Widget.Brush.bHasBrushColor = true;
	Widget.Render.RenderOpacity = 0.55f;
	Widget.Render.bHasRenderOpacity = true;
	Widget.Render.RenderAngle = 12.0f;
	Widget.Render.bHasRenderAngle = true;
	Widget.Render.Clipping = TEXT("ClipToBounds");
	Widget.Render.bHasClipping = true;

	FUMGWidgetNode Wrapped = FVisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	TestEqual(TEXT("Typed wrapped node remains Border"), Wrapped.Type, TEXT("Border"));
	TestEqual(TEXT("Typed brush color moved to outer border"), GetWidgetProperty(Wrapped, TEXT("BrushColor")), TEXT("#89BCE1FF"));
	TestTrue(TEXT("Typed render opacity moved to outer border"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(Wrapped, TEXT("RenderOpacity"))), 0.55f, 0.0001f));
	TestTrue(TEXT("Typed render angle moved to outer border"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(Wrapped, TEXT("RenderAngle"))), 12.0f, 0.0001f));
	TestEqual(TEXT("Typed clipping moved to outer border"), GetWidgetProperty(Wrapped, TEXT("Clipping")), TEXT("ClipToBounds"));
	TestFalse(TEXT("Typed outer border should not synthesize legacy BrushColor"), HasLegacyWidgetProperty(Wrapped, TEXT("BrushColor")));
	TestFalse(TEXT("Typed outer border should not synthesize legacy RenderOpacity"), HasLegacyWidgetProperty(Wrapped, TEXT("RenderOpacity")));
	TestFalse(TEXT("Typed outer border should not synthesize legacy RenderAngle"), HasLegacyWidgetProperty(Wrapped, TEXT("RenderAngle")));
	TestFalse(TEXT("Typed outer border should not synthesize legacy Clipping"), HasLegacyWidgetProperty(Wrapped, TEXT("Clipping")));
	TestEqual(TEXT("Typed wrapped child count"), Wrapped.Children.Num(), 1);
	if (Wrapped.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& Inner = Wrapped.Children[0];
	TestFalse(TEXT("Typed inner widget should not keep BrushColor"), HasWidgetProperty(Inner, TEXT("BrushColor")));
	TestFalse(TEXT("Typed inner widget should not keep RenderOpacity"), HasWidgetProperty(Inner, TEXT("RenderOpacity")));
	TestFalse(TEXT("Typed inner widget should not keep RenderAngle"), HasWidgetProperty(Inner, TEXT("RenderAngle")));
	TestFalse(TEXT("Typed inner widget should not keep Clipping"), HasWidgetProperty(Inner, TEXT("Clipping")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutMapperDirectHelpersTest,
	"Design2UMG.LayoutMapper.DirectHelpers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutMapperDirectHelpersTest::RunTest(const FString& Parameters)
{
	TArray<FUMGWidgetNode> Children;
	Children.SetNum(3);
	FLayoutMapper::ApplyGap(Children, 12.0f, EDesignLayout::Horizontal);
	TestEqual(TEXT("First child horizontal gap stays zero"), Children[0].Slot.Padding.Left, 0.0f);
	TestEqual(TEXT("Second child horizontal gap"), Children[1].Slot.Padding.Left, 12.0f);
	TestEqual(TEXT("Third child horizontal gap"), Children[2].Slot.Padding.Left, 12.0f);

	FUMGWidgetNode Widget;
	FDesignPadding Padding;
	Padding.Left = 1.0f;
	Padding.Top = 2.0f;
	Padding.Right = 3.0f;
	Padding.Bottom = 4.0f;
	FLayoutMapper::ApplyPadding(Widget, Padding);
	TestEqual(TEXT("Padding serialized directly"), GetWidgetProperty(Widget, TEXT("Padding")), TEXT("1.0,2.0,3.0,4.0"));
	return true;
}
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
			HasWidgetProperty(*DefaultItem, TEXT("OutlineColor")));
	}
	const FUMGWidgetNode* GhostFrame = FindWidgetById(Data.RootWidget, TEXT("ghostFrame"));
	TestTrue(TEXT("Ghost frame exists"), GhostFrame != nullptr);
	if (GhostFrame != nullptr)
	{
		TestFalse(TEXT("Stroke without explicit fill should stay invisible"),
			HasWidgetProperty(*GhostFrame, TEXT("OutlineColor")));
	}
	const FUMGWidgetNode* OutlinedItem = FindWidgetById(Data.RootWidget, TEXT("outlinedItem"));
	TestTrue(TEXT("Outlined item exists"), OutlinedItem != nullptr);
	if (OutlinedItem != nullptr)
	{
		const FLinearColor OutlineColor = FUMGValueParser::ParseColor(GetWidgetProperty(*OutlinedItem, TEXT("OutlineColor")));
		TestTrue(TEXT("Explicit stroke fill should be preserved"),
			OutlineColor.Equals(FUMGValueParser::ParseColor(TEXT("#888888")), 0.001f));
	}
	return true;
}
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
			HasWidgetProperty(*IconNode, TEXT("FontFamily")));
		TestEqual(TEXT("Missing mapping uses configured placeholder text"),
			GetWidgetProperty(*IconNode, TEXT("Text")),
			TEXT("[missing-icon]"));
	}
	Settings->IconFontAssetMap = SavedMap;
	Settings->DefaultIconFontAssetPath = SavedDefaultPath;
	Settings->MissingIconFontPlaceholder = SavedPlaceholder;
	return true;
}
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
			HasWidgetProperty(*IconNode, TEXT("FontFamily")));
		TestEqual(TEXT("Empty placeholder should keep original IconName text"),
			GetWidgetProperty(*IconNode, TEXT("Text")),
			ExpectedIconName);
	}
	Settings->IconFontAssetMap = SavedMap;
	Settings->DefaultIconFontAssetPath = SavedDefaultPath;
	Settings->MissingIconFontPlaceholder = SavedPlaceholder;
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIconFontLigatureFallbackUsesRegularWeightTest,
	"Design2UMG.IconFont.LigatureFallbackUsesRegularWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIconFontLigatureFallbackUsesRegularWeightTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	const TMap<FString, TSoftObjectPtr<UFont>> SavedMap = Settings->IconFontAssetMap;
	const FString SavedDefaultPath = Settings->DefaultIconFontAssetPath;
	const FString SavedPlaceholder = Settings->MissingIconFontPlaceholder;
	Settings->IconFontAssetMap.Empty();
	Settings->DefaultIconFontAssetPath = TEXT("/Engine/EngineFonts/Roboto.Roboto");
	Settings->MissingIconFontPlaceholder.Empty();

	const FString Json = TEXT(R"({
		"children": [{
			"id": "root",
			"type": "frame",
			"children": [{
				"id": "icon1",
				"type": "icon_font",
				"iconFontFamily": "MissingIconFamily_UnitTest",
				"iconFontName": "dashboard",
				"fontSize": 20,
				"weight": 100
			}]
		}]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Icon weight fixture parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FString ExpectedIconName = TEXT("dashboard");
	const FUMGWidgetNode* IconNode = FindWidgetByProperty(Data.RootWidget, TEXT("IconName"), &ExpectedIconName);
	TestTrue(TEXT("Icon widget exists"), IconNode != nullptr);
	if (IconNode)
	{
		TestEqual(TEXT("Icon widget uses configured default font asset"),
			GetWidgetProperty(*IconNode, TEXT("FontFamily")),
			TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		TestEqual(TEXT("Ligature fallback icon keeps regular typeface without explicit fontWeight"),
			GetWidgetProperty(*IconNode, TEXT("TypefaceFontName")),
			TEXT("Regular"));
	}

	Settings->IconFontAssetMap = SavedMap;
	Settings->DefaultIconFontAssetPath = SavedDefaultPath;
	Settings->MissingIconFontPlaceholder = SavedPlaceholder;
	return true;
}
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExpandedContainerRenderBoundsUseLayoutSpaceTest,
	"Design2UMG.FigmaParser.ExpandedContainerRenderBoundsUseLayoutSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaExpandedContainerRenderBoundsUseLayoutSpaceTest::RunTest(const FString& Parameters)
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
					"clipsContent": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":100,"height":80},
					"absoluteRenderBounds": {"x":-8,"y":-8,"width":116,"height":96},
					"effects": [{
						"type": "DROP_SHADOW",
						"visible": true,
						"blendMode": "NORMAL",
						"offset": {"x":0,"y":0},
						"radius": 8,
						"color": {"r":0,"g":0,"b":0,"a":0.25}
					}],
					"children": [{
						"id": "0:3",
						"name": "Child",
						"type": "RECTANGLE",
						"visible": true,
						"absoluteBoundingBox": {"x":10,"y":12,"width":40,"height":20},
						"absoluteRenderBounds": {"x":10,"y":12,"width":40,"height":20},
						"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level frame count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1)
		return false;

	const TSharedPtr<FDesignNode>& Root = Doc.Children[0];
	TestTrue(TEXT("Root X present"), Root->bHasX);
	TestTrue(TEXT("Root Y present"), Root->bHasY);
	TestEqual(TEXT("Root X stays in layout space"), Root->X, 0.0f);
	TestEqual(TEXT("Root Y stays in layout space"), Root->Y, 0.0f);
	TestTrue(TEXT("Root width fixed"), Root->Width.IsFixed());
	TestTrue(TEXT("Root height fixed"), Root->Height.IsFixed());
	TestEqual(TEXT("Root width uses layout bounds"), Root->Width.Value, 100.0f);
	TestEqual(TEXT("Root height uses layout bounds"), Root->Height.Value, 80.0f);
	TestEqual(TEXT("Root child count"), Root->Children.Num(), 1);
	if (Root->Children.Num() != 1)
		return false;

	const TSharedPtr<FDesignNode>& Child = Root->Children[0];
	TestTrue(TEXT("Child X present"), Child->bHasX);
	TestTrue(TEXT("Child Y present"), Child->bHasY);
	TestEqual(TEXT("Child X remains relative to layout bounds"), Child->X, 10.0f);
	TestEqual(TEXT("Child Y remains relative to layout bounds"), Child->Y, 12.0f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLeafForegroundBlurUsesLayoutBoundsTest,
	"Design2UMG.FigmaParser.LeafForegroundBlurUsesLayoutBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLeafForegroundBlurUsesLayoutBoundsTest::RunTest(const FString& Parameters)
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":160,"height":120},
					"absoluteRenderBounds": {"x":0,"y":0,"width":160,"height":120},
					"children": [{
						"id": "blur_avatar",
						"name": "BlurAvatar",
						"type": "ELLIPSE",
						"visible": true,
						"absoluteBoundingBox": {"x":24,"y":20,"width":26,"height":26},
						"absoluteRenderBounds": {"x":2.25,"y":-1.75,"width":69.5,"height":69.5},
						"fills": [
							{"type":"IMAGE","visible":true,"blendMode":"NORMAL","imageRef":"avatar","scaleMode":"FIT"},
							{"type":"IMAGE","visible":true,"blendMode":"NORMAL","imageRef":"avatar_mask","scaleMode":"FILL"}
						],
						"effects": [{
							"type":"LAYER_BLUR",
							"visible":true,
							"blendMode":"NORMAL",
							"radius":21.75
						}],
						"children": []
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Leaf blur parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level frame count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Avatar = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Leaf blur X present"), Avatar->bHasX);
	TestTrue(TEXT("Leaf blur Y present"), Avatar->bHasY);
	TestEqual(TEXT("Leaf blur X uses layout bounds"), Avatar->X, 24.0f);
	TestEqual(TEXT("Leaf blur Y uses layout bounds"), Avatar->Y, 20.0f);
	TestTrue(TEXT("Leaf blur width fixed"), Avatar->Width.IsFixed());
	TestTrue(TEXT("Leaf blur height fixed"), Avatar->Height.IsFixed());
	TestEqual(TEXT("Leaf blur width uses layout bounds"), Avatar->Width.Value, 26.0f);
	TestEqual(TEXT("Leaf blur height uses layout bounds"), Avatar->Height.Value, 26.0f);
	TestTrue(TEXT("Leaf blur keeps render delta X"), Avatar->Metadata.Contains(TEXT("figma.renderBoundsDeltaX")));
	TestTrue(TEXT("Leaf blur keeps render delta Y"), Avatar->Metadata.Contains(TEXT("figma.renderBoundsDeltaY")));
	TestTrue(TEXT("Leaf blur keeps render delta W"), Avatar->Metadata.Contains(TEXT("figma.renderBoundsDeltaW")));
	TestTrue(TEXT("Leaf blur keeps render delta H"), Avatar->Metadata.Contains(TEXT("figma.renderBoundsDeltaH")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaShrunkenFilledFrameUsesLayoutBoundsTest,
	"Design2UMG.FigmaParser.ShrunkenFilledFrameUsesLayoutBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaShrunkenFilledFrameUsesLayoutBoundsTest::RunTest(const FString& Parameters)
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":320,"height":240},
					"absoluteRenderBounds": {"x":0,"y":0,"width":320,"height":240},
					"children": [{
						"id": "0:3",
						"name": "ImageCard",
						"type": "FRAME",
						"visible": true,
						"absoluteBoundingBox": {"x":24,"y":16,"width":160,"height":160},
						"absoluteRenderBounds": {"x":24,"y":16,"width":160,"height":100},
						"fills": [
							{"type":"SOLID","visible":true,"blendMode":"NORMAL","color":{"r":0.92,"g":0.90,"b":0.94,"a":1}},
							{"type":"IMAGE","visible":true,"blendMode":"LUMINOSITY","imageRef":"img_card","scaleMode":"FIT"}
						],
						"children": []
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Shrunken filled frame parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level frame count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Card = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Filled frame X present"), Card->bHasX);
	TestTrue(TEXT("Filled frame Y present"), Card->bHasY);
	TestEqual(TEXT("Filled frame X uses layout bounds"), Card->X, 24.0f);
	TestEqual(TEXT("Filled frame Y uses layout bounds"), Card->Y, 16.0f);
	TestTrue(TEXT("Filled frame width fixed"), Card->Width.IsFixed());
	TestTrue(TEXT("Filled frame height fixed"), Card->Height.IsFixed());
	TestEqual(TEXT("Filled frame width uses layout bounds"), Card->Width.Value, 160.0f);
	TestEqual(TEXT("Filled frame height uses layout bounds"), Card->Height.Value, 160.0f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaFilledAutoLayoutContainerUsesLayoutBoundsTest,
	"Design2UMG.FigmaParser.FilledAutoLayoutContainerUsesLayoutBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaFilledAutoLayoutContainerUsesLayoutBoundsTest::RunTest(const FString& Parameters)
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
					"absoluteRenderBounds": {"x":0,"y":0,"width":400,"height":300},
					"children": [{
						"id":"content",
						"name":"Content",
						"type":"FRAME",
						"visible":true,
						"layoutMode":"VERTICAL",
						"absoluteBoundingBox":{"x":20,"y":20,"width":300,"height":200},
						"absoluteRenderBounds":{"x":20,"y":20,"width":300,"height":136},
						"fills":[{"type":"SOLID","visible":true,"blendMode":"NORMAL","color":{"r":1,"g":1,"b":1,"a":1}}],
						"children":[
							{
								"id":"headline",
								"name":"Headline",
								"type":"TEXT",
								"visible":true,
								"layoutSizingHorizontal":"FILL",
								"layoutSizingVertical":"HUG",
								"absoluteBoundingBox":{"x":36,"y":36,"width":268,"height":48},
								"absoluteRenderBounds":{"x":38,"y":41,"width":240,"height":33.27},
								"characters":"Label",
								"style":{
									"fontFamily":"Roboto",
									"fontWeight":700,
									"fontSize":32,
									"textAlignHorizontal":"LEFT",
									"textAlignVertical":"TOP",
									"textCase":"ORIGINAL",
									"textDecoration":"NONE",
									"letterSpacing":0,
									"lineHeightUnit":"PIXELS",
									"lineHeightPx":48
								}
							}
						]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Filled auto-layout container parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level frame count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Content = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Filled container X present"), Content->bHasX);
	TestTrue(TEXT("Filled container Y present"), Content->bHasY);
	TestEqual(TEXT("Filled container X uses layout bounds"), Content->X, 20.0f);
	TestEqual(TEXT("Filled container Y uses layout bounds"), Content->Y, 20.0f);
	TestTrue(TEXT("Filled container width fixed"), Content->Width.IsFixed());
	TestTrue(TEXT("Filled container height fixed"), Content->Height.IsFixed());
	TestEqual(TEXT("Filled container width uses layout bounds"), Content->Width.Value, 300.0f);
	TestEqual(TEXT("Filled container height uses layout bounds"), Content->Height.Value, 200.0f);
	TestEqual(TEXT("Filled container child count"), Content->Children.Num(), 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaImplicitGroupMaskLeafSkippedTest,
	"Design2UMG.FigmaParser.ImplicitGroupMaskLeafSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaImplicitGroupMaskLeafSkippedTest::RunTest(const FString& Parameters)
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
					"id": "masked_group",
					"name": "MaskedGroup",
					"type": "GROUP",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
					"children": [
						{
							"id": "mask_rect",
							"name": "Mask Shape",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
							"fills": [{
								"type": "SOLID",
								"visible": true,
								"color": {"r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0}
							}]
						},
						{
							"id": "label",
							"name": "Label",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x": 10, "y": 10, "width": 80, "height": 20},
							"characters": "Visible",
							"style": {
								"fontFamily": "Inter",
								"fontSize": 16,
								"fontWeight": 400,
								"textAlignHorizontal": "LEFT",
								"textAlignVertical": "TOP"
							}
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Implicit group mask fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Group = Doc.Children[0];
	TestEqual(TEXT("Masked group keeps one runtime child"), Group->Children.Num(), 1);
	if (Group->Children.Num() != 1)
	{
		return false;
	}

	TestEqual(TEXT("Implicit mask leaf is skipped"), Group->Children[0]->Id, TEXT("label"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaImplicitMaskLeafKeepsMultiFillGeometryTest,
	"Design2UMG.FigmaParser.ImplicitMaskLeafKeepsMultiFillGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaImplicitMaskLeafKeepsMultiFillGeometryTest::RunTest(const FString& Parameters)
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
					"id": "logo_group",
					"name": "LogoGroup",
					"type": "GROUP",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 48, "height": 48},
					"children": [
						{
							"id": "logo_backplate",
							"name": "Logo Backplate",
							"type": "ELLIPSE",
							"visible": true,
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 48, "height": 48},
							"fills": [
								{
									"type": "SOLID",
									"visible": true,
									"color": {"r": 0.15, "g": 0.2, "b": 0.7, "a": 1.0}
								},
								{
									"type": "IMAGE",
									"visible": true,
									"imageRef": "img_logo",
									"scaleMode": "FILL"
								}
							]
						},
						{
							"id": "logo_foreground",
							"name": "Foreground",
							"type": "VECTOR",
							"visible": true,
							"absoluteBoundingBox": {"x": 12, "y": 12, "width": 24, "height": 24},
							"fillGeometry": [{
								"path": "M 0 0 L 24 0 L 12 24 Z",
								"windingRule": "NONZERO"
							}],
							"fills": [{
								"type": "SOLID",
								"visible": true,
								"color": {"r": 1.0, "g": 1.0, "b": 1.0, "a": 1.0}
							}]
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Multi-fill logo fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Group = Doc.Children[0];
	TestEqual(TEXT("Group keeps both runtime children"), Group->Children.Num(), 2);
	if (Group->Children.Num() != 2)
	{
		return false;
	}

	TestEqual(TEXT("Multi-fill backplate is preserved"), Group->Children[0]->Id, TEXT("logo_backplate"));
	TestEqual(TEXT("Foreground stays after preserved backplate"), Group->Children[1]->Id, TEXT("logo_foreground"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaFullBoundsFrameRectangleKeptWithoutMaskFlagTest,
	"Design2UMG.FigmaParser.FullBoundsFrameRectangleKeptWithoutMaskFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaFullBoundsFrameRectangleKeptWithoutMaskFlagTest::RunTest(const FString& Parameters)
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
					"id": "frame_root",
					"name": "FrameRoot",
					"type": "FRAME",
					"visible": true,
					"clipsContent": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
					"children": [
						{
							"id": "bg_rect",
							"name": "Rectangle",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
							"fills": [{
								"type": "SOLID",
								"visible": true,
								"color": {"r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0}
							}]
						},
						{
							"id": "fg_label",
							"name": "Foreground",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x": 12, "y": 12, "width": 60, "height": 20},
							"characters": "Keep",
							"style": {
								"fontFamily": "Inter",
								"fontSize": 16,
								"fontWeight": 400,
								"textAlignHorizontal": "LEFT",
								"textAlignVertical": "TOP"
							}
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Full-bounds frame rectangle fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Frame = Doc.Children[0];
	TestEqual(TEXT("Full-bounds frame keeps both children"), Frame->Children.Num(), 2);
	if (Frame->Children.Num() != 2)
	{
		return false;
	}

	TestEqual(TEXT("Background rectangle is preserved"), Frame->Children[0]->Id, TEXT("bg_rect"));
	TestEqual(TEXT("Foreground label is preserved"), Frame->Children[1]->Id, TEXT("fg_label"));
	return true;
}
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJustifyAutoTextWrapParityTest,
	"Design2UMG.Text.JustifyAutoWrapParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementCalibrationCacheUsesNodeContextTest,
	"Design2UMG.Text.CalibrationCacheUsesNodeContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementCalibrationCacheUsesNodeContextTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping text calibration cache test."));
		return true;
	}

	auto MakeNode = [](const float InWidth, const float InLetterSpacing, const FString& InContent)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Type = EDesignNodeType::Text;
		Node->Content = InContent;
		Node->FontFamily = TEXT("Geist");
		Node->FontWeight = TEXT("Regular");
		Node->FontSize = 36.0f;
		Node->LineHeight = 1.35f;
		Node->LetterSpacing = InLetterSpacing;
		Node->TextAlign = EDesignTextAlign::Left;
		Node->TextAlignVertical = EDesignTextAlignVertical::Middle;
		Node->TextGrowth = EDesignTextGrowth::FixedWidth;
		Node->Width = FDesignSize::Fixed(InWidth);
		Node->bHasWidth = true;
		return Node;
	};

	const FString LongContent = TEXT("Design2UMG text calibration should stay stable across different wrap widths and never depend on evaluation order.");
	const TSharedPtr<FDesignNode> NarrowNode = MakeNode(80.0f, 0.0f, LongContent);
	const TSharedPtr<FDesignNode> WideNode = MakeNode(320.0f, 0.0f, LongContent);

	TextMeasurement::ResetCache();
	const float NarrowFirst = TextMeasurement::ResolveTextFontSizeForUMG(NarrowNode);
	const float WideSecond = TextMeasurement::ResolveTextFontSizeForUMG(WideNode);

	TextMeasurement::ResetCache();
	const float WideFirst = TextMeasurement::ResolveTextFontSizeForUMG(WideNode);
	const float NarrowSecond = TextMeasurement::ResolveTextFontSizeForUMG(NarrowNode);

	TestTrue(TEXT("Narrow calibration resolved"), NarrowFirst > 0.0f && NarrowSecond > 0.0f);
	TestTrue(TEXT("Wide calibration resolved"), WideFirst > 0.0f && WideSecond > 0.0f);
	TestFalse(TEXT("Different wrap widths should not collapse to one cached size"),
		FMath::IsNearlyEqual(NarrowFirst, WideFirst, 0.01f));
	TestTrue(TEXT("Narrow calibration is order independent"),
		FMath::IsNearlyEqual(NarrowFirst, NarrowSecond, 0.01f));
	TestTrue(TEXT("Wide calibration is order independent"),
		FMath::IsNearlyEqual(WideFirst, WideSecond, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFontResolverDefaultFontAliasLookupTest,
	"Design2UMG.Text.FontResolverDefaultFontAliasLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFontResolverDefaultFontAliasLookupTest::RunTest(const FString& Parameters)
{
	const UObject* DefaultFontObject = FTextBlockStyle::GetDefault().Font.FontObject;
	if (!DefaultFontObject)
	{
		AddWarning(TEXT("Default Slate text font is unavailable, skipping font alias lookup test."));
		return true;
	}

	const FString DefaultFontPath = DefaultFontObject->GetPathName();
	const FString DefaultFontName = DefaultFontObject->GetName();
	const FString StyleQualifiedAlias = DefaultFontName + TEXT(" Regular");

	TestEqual(TEXT("Default font family resolves through FontResolver"),
		FFontResolver::FindFontAssetPath(DefaultFontName),
		DefaultFontPath);
	TestEqual(TEXT("Style-qualified alias resolves to the same font asset"),
		FFontResolver::FindFontAssetPath(StyleQualifiedAlias),
		DefaultFontPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedCalibrationFitContentOrderIndependenceTest,
	"Design2UMG.Text.SharedCalibrationFitContentOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedCalibrationFitContentOrderIndependenceTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping shared calibration fit-content test."));
		return true;
	}

	float ExplicitFirstRootHeight = 0.0f;
	float ExplicitFirstExplicitHeight = 0.0f;
	float ExplicitFirstAutoHeight = 0.0f;
	{
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(BuildSharedCalibrationFitContentDocument(true, false));
		if (!ExtractSharedCalibrationHeights(
			Data,
			*this,
			ExplicitFirstRootHeight,
			ExplicitFirstExplicitHeight,
			ExplicitFirstAutoHeight))
		{
			return false;
		}
	}

	float AutoFirstRootHeight = 0.0f;
	float AutoFirstExplicitHeight = 0.0f;
	float AutoFirstAutoHeight = 0.0f;
	{
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(BuildSharedCalibrationFitContentDocument(false, false));
		if (!ExtractSharedCalibrationHeights(
			Data,
			*this,
			AutoFirstRootHeight,
			AutoFirstExplicitHeight,
			AutoFirstAutoHeight))
		{
			return false;
		}
	}

	TestTrue(TEXT("Fit-content parent height should be order independent"),
		FMath::IsNearlyEqual(ExplicitFirstRootHeight, AutoFirstRootHeight, 0.5f));
	TestTrue(TEXT("Fit-content parent height should collapse to child sum"),
		FMath::IsNearlyEqual(ExplicitFirstRootHeight, ExplicitFirstExplicitHeight + ExplicitFirstAutoHeight, 0.5f));
	TestTrue(TEXT("Auto-first fit-content parent height should collapse to child sum"),
		FMath::IsNearlyEqual(AutoFirstRootHeight, AutoFirstExplicitHeight + AutoFirstAutoHeight, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedCalibrationRefFitContentOrderIndependenceTest,
	"Design2UMG.Text.SharedCalibrationRefFitContentOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedCalibrationRefFitContentOrderIndependenceTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping shared calibration ref fit-content test."));
		return true;
	}

	float ExplicitFirstRootHeight = 0.0f;
	float ExplicitFirstExplicitHeight = 0.0f;
	float ExplicitFirstAutoHeight = 0.0f;
	{
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(BuildSharedCalibrationFitContentDocument(true, true));
		if (!ExtractSharedCalibrationHeights(
			Data,
			*this,
			ExplicitFirstRootHeight,
			ExplicitFirstExplicitHeight,
			ExplicitFirstAutoHeight))
		{
			return false;
		}
	}

	float AutoFirstRootHeight = 0.0f;
	float AutoFirstExplicitHeight = 0.0f;
	float AutoFirstAutoHeight = 0.0f;
	{
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(BuildSharedCalibrationFitContentDocument(false, true));
		if (!ExtractSharedCalibrationHeights(
			Data,
			*this,
			AutoFirstRootHeight,
			AutoFirstExplicitHeight,
			AutoFirstAutoHeight))
		{
			return false;
		}
	}

	TestTrue(TEXT("Ref fit-content parent height should be order independent"),
		FMath::IsNearlyEqual(ExplicitFirstRootHeight, AutoFirstRootHeight, 0.5f));
	TestTrue(TEXT("Ref fit-content parent height should collapse to child sum"),
		FMath::IsNearlyEqual(ExplicitFirstRootHeight, ExplicitFirstExplicitHeight + ExplicitFirstAutoHeight, 0.5f));
	TestTrue(TEXT("Auto-first ref fit-content parent height should collapse to child sum"),
		FMath::IsNearlyEqual(AutoFirstRootHeight, AutoFirstExplicitHeight + AutoFirstAutoHeight, 0.5f));
	return true;
}

bool FJustifyAutoTextWrapParityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "justify_auto",
				"type": "text",
				"name": "Justify Auto",
				"content": "This sentence should wrap against the source width.",
				"width": 120,
				"textAlign": "justify",
				"textGrowth": "auto",
				"fontFamily": "Roboto",
				"fontSize": 16
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Justify auto text fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* TextWidget = FindWidgetById(Data.RootWidget, TEXT("justify_auto"));
	TestTrue(TEXT("Justify auto text widget exists"), TextWidget != nullptr);
	if (!TextWidget)
	{
		return false;
	}

	TestEqual(TEXT("Justify auto text converts to TextBlock"), TextWidget->Type, TEXT("TextBlock"));
	TestEqual(TEXT("Justify auto text enables runtime wrapping"), GetWidgetProperty(*TextWidget, TEXT("AutoWrapText")), TEXT("true"));
	TestEqual(TEXT("Justify auto text keeps source wrap width"), GetWidgetProperty(*TextWidget, TEXT("WrapTextAt")), TEXT("120.0"));
	TestTrue(TEXT("Justify auto text should not clip like fixed-height text"), GetWidgetProperty(*TextWidget, TEXT("Clipping")).IsEmpty());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKnownWidthFillTextWrapParityTest,
	"Design2UMG.Text.KnownWidthFillWrapParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FKnownWidthFillTextWrapParityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "fill_known_wrap",
				"type": "text",
				"name": "Fill Known Wrap",
				"content": "This fill-width text should keep the known fallback width at runtime.",
				"width": "fill_container(300)",
				"textGrowth": "auto",
				"fontFamily": "Roboto",
				"fontSize": 16
			}
		]
	})JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Known-width fill text fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* TextWidget = FindWidgetById(Data.RootWidget, TEXT("fill_known_wrap"));
	TestTrue(TEXT("Known-width fill text widget exists"), TextWidget != nullptr);
	if (!TextWidget)
	{
		return false;
	}

	TestEqual(TEXT("Known-width fill text enables runtime wrapping"), GetWidgetProperty(*TextWidget, TEXT("AutoWrapText")), TEXT("true"));
	TestEqual(TEXT("Known-width fill text keeps fallback wrap width"), GetWidgetProperty(*TextWidget, TEXT("WrapTextAt")), TEXT("300.0"));
	TestTrue(TEXT("Known-width fill text should not clip"), GetWidgetProperty(*TextWidget, TEXT("Clipping")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTextContentPreservesParagraphBreaksTest,
	"Design2UMG.Text.RuntimeContentPreservesParagraphBreaks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRuntimeTextContentPreservesParagraphBreaksTest::RunTest(const FString& Parameters)
{
	const FString SourceText = TEXT("Line one\r\n\r\nLine two");
	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(SourceText);
	TestEqual(TEXT("CRLF normalized to LF"), RuntimeText, FString(TEXT("Line one\n\nLine two")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiParagraphFixedWidthTextKeepsDeclaredFontSizeTest,
	"Design2UMG.Text.MultiParagraphFixedWidthKeepsDeclaredFontSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMultiParagraphFixedWidthTextKeepsDeclaredFontSizeTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":1400,"height":200},
					"children": [{
						"id": "body",
						"name": "Supporting text",
						"type": "TEXT",
						"visible": true,
						"absoluteBoundingBox": {"x":0,"y":0,"width":1325,"height":80},
						"style": {
							"fontFamily": "Roboto",
							"fontWeight": 400,
							"fontSize": 14,
							"textAlignHorizontal": "LEFT",
							"textAlignVertical": "TOP",
							"letterSpacing": 0.25,
							"lineHeightUnit": "PIXELS",
							"lineHeightPx": 20
						},
						"layoutSizingHorizontal": "FILL",
						"layoutSizingVertical": "HUG",
						"layoutPositioning": "AUTO",
						"textAutoResize": "HEIGHT",
						"characters": "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.\n\nDuis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum."
					}]
				}]
			}]
		}
	})JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma multi-paragraph fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* TextWidget = FindWidgetById(Data.RootWidget, TEXT("body"));
	TestNotNull(TEXT("Converted body text exists"), TextWidget);
	if (!TextWidget)
	{
		return false;
	}

	TestEqual(TEXT("Body text keeps declared runtime content"), TextWidget->Text.Text, FString(TEXT("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.\n\nDuis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.")));
	TestEqual(TEXT("Body text enables runtime wrap"), GetWidgetProperty(*TextWidget, TEXT("AutoWrapText")), TEXT("true"));
	TestEqual(TEXT("Body text keeps authored wrap width"), GetWidgetProperty(*TextWidget, TEXT("WrapTextAt")), TEXT("1325.0"));
	TestTrue(TEXT("Body text should not clip after conversion"), GetWidgetProperty(*TextWidget, TEXT("Clipping")).IsEmpty());
	return true;
}
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
	const FUMGWidgetNode* OverrideLabel = FindWidgetById(Data.RootWidget, TEXT("label"));
	TestTrue(TEXT("Find override text widget"), OverrideLabel != nullptr);
	if (!OverrideLabel)
		return false;
	TestEqual(TEXT("Override text with href upgrades to rich text"), OverrideLabel->Type, TEXT("RichTextBlock"));
	const FString OverrideMarkup = GetWidgetProperty(*OverrideLabel, TEXT("Text"));
	TestTrue(TEXT("Override text content propagated"), OverrideMarkup.Contains(TEXT("Go")));
	TestTrue(TEXT("Override href serialized into markup"), OverrideMarkup.Contains(TEXT("href=\"https://override.example\"")));
	TestEqual(TEXT("Override fontStyle propagated"), GetWidgetProperty(*OverrideLabel, TEXT("FontStyle")), TEXT("italic"));
	TestEqual(TEXT("Override underline propagated"), GetWidgetProperty(*OverrideLabel, TEXT("Underline")), TEXT("true"));
	TestEqual(TEXT("Override href propagated"), GetWidgetProperty(*OverrideLabel, TEXT("Href")), TEXT("https://override.example"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenRichTextConverterUsesRichTextBlockTest,
	"Design2UMG.Converter.PenRichTextUsesRichTextBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenRichTextConverterUsesRichTextBlockTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"width": 320,
				"height": 120,
				"children": [
					{
						"id": "rich_label",
						"type": "text",
						"width": 220,
						"height": "fit_content",
						"fontFamily": "Geist",
						"fontSize": 18,
						"fill": "#112233",
						"content": [
							{ "content": "Open " },
							{ "content": "Docs", "fontWeight": "700", "underline": true, "href": "https://docs.example.com" }
						]
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Pen rich text fixture parses"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* RichLabel = FindWidgetById(Data.RootWidget, TEXT("rich_label"));
	TestNotNull(TEXT("Rich text widget exists"), RichLabel);
	if (!RichLabel)
	{
		return false;
	}

	TestEqual(TEXT("Rich text converts to RichTextBlock"), RichLabel->Type, TEXT("RichTextBlock"));

	const FString Markup = GetWidgetProperty(*RichLabel, TEXT("Text"));
	TestTrue(TEXT("Rich text markup contains first run tag"), Markup.Contains(TEXT("<d2u_run_0>")));
	TestTrue(TEXT("Rich text markup contains href attribute"), Markup.Contains(TEXT("href=\"https://docs.example.com\"")));
	TestTrue(TEXT("Rich text markup keeps link text"), Markup.Contains(TEXT("Docs")));
	TestEqual(TEXT("Single rich hyperlink becomes tooltip"), GetWidgetProperty(*RichLabel, TEXT("ToolTipText")), TEXT("https://docs.example.com"));

	FString StyleRowsJson;
	TestTrue(TEXT("Rich text style rows metadata exported"), RichLabel->Properties.TryGetString(RichTextStyleRowsPropertyKey, StyleRowsJson));
	TestTrue(TEXT("Rich text style rows include second run"), StyleRowsJson.Contains(TEXT("d2u_run_1")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathStrokeOnlyIconRasterizationTest,
	"Design2UMG.Path.StrokeOnlyIconRasterization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPathStrokeOnlyIconRasterizationTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"name": "Root",
				"width": 24,
				"height": 24,
				"children": [
					{
						"id": "icon_plus",
						"type": "path",
						"name": "plus",
						"x": 7,
						"y": 7,
						"width": 10,
						"height": 10,
						"geometry": "M7.07104 3.53555l-3.53552 0-3.53552 0m3.53552-3.53555l0 3.53555 0 3.53549",
						"stroke": {
							"align": "center",
							"thickness": 1.5,
							"fill": "#111111",
							"join": "round",
							"cap": "round"
						}
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Stroke-only path parses"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("icon_plus"));
	TestTrue(TEXT("Converted widget keeps stroke-only icon"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	TestEqual(TEXT("Stroke-only path converts to image widget"), Widget->Type, TEXT("Image"));
	TestTrue(
		TEXT("Stroke color propagated"),
		FUMGValueParser::ParseColor(GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeColor))
			.Equals(FUMGValueParser::ParseColor(TEXT("#111111")), 0.0001f));
	TestEqual(TEXT("Stroke thickness propagated"), GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeThickness), TEXT("1.5"));
	TestEqual(TEXT("Stroke cap propagated"), GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeCap), TEXT("round"));
	TestEqual(TEXT("Stroke join propagated"), GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeJoin), TEXT("round"));

	const FString Geometry = GetWidgetProperty(*Widget, UMGPropertyKeys::PathGeometry);
	const FString FillRule = GetWidgetProperty(*Widget, UMGPropertyKeys::PathFillRule);
	const FString StrokeColorValue = GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeColor);
	const float StrokeThickness = FCString::Atof(*GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeThickness));
	UTexture2D* Texture = FDesignPathRasterizer::Rasterize(
		Geometry,
		20,
		20,
		TOptional<FLinearColor>(),
		FillRule,
		TOptional<FLinearColor>(FUMGValueParser::ParseColor(StrokeColorValue)),
		StrokeThickness,
		GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeCap),
		GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeJoin));
	TestTrue(TEXT("Stroke-only path rasterizes to texture"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	TestTrue(TEXT("Stroke-only path produces visible pixels"), CountOpaqueTexturePixels(Texture) >= 24);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForegroundBlurRetainerWrapperTest,
	"Design2UMG.Converter.ForegroundBlurRetainerWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FForegroundBlurRetainerWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "blur_card",
				"type": "frame",
				"name": "Blur Card",
				"width": 240,
				"height": 80,
				"effect": {
					"type": "blur",
					"radius": 12
				},
				"children": [
					{
						"id": "title",
						"type": "text",
						"content": "Blurred",
						"fontSize": 24
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Foreground blur fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	TestTrue(TEXT("Foreground blur effect parsed into IR"),
		Doc.Children[0]->Effects.Num() == 1 && Doc.Children[0]->Effects[0].Type == EDesignEffectType::Blur);
	TestTrue(TEXT("Foreground blur radius preserved"),
		Doc.Children[0]->Effects.Num() == 1 && FMath::IsNearlyEqual(Doc.Children[0]->Effects[0].Radius, 12.0f));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Foreground blur uses RetainerBox wrapper"), Data.RootWidget.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Foreground blur retain rendering typed"), Data.RootWidget.Effect.bHasRetainRendering && Data.RootWidget.Effect.bRetainRendering);
	TestEqual(TEXT("Foreground blur effect material ref exported"), Data.RootWidget.Effect.EffectMaterial, TEXT("d2u.retainer.foreground_blur"));
	TestEqual(TEXT("Foreground blur texture parameter exported"), Data.RootWidget.Effect.TextureParameter, TEXT("Texture"));

	const FVector2D UVStep = Data.RootWidget.Effect.EffectBlurUVStep;
	TestTrue(TEXT("Foreground blur UV step is typed"), Data.RootWidget.Effect.bHasEffectBlurUVStep);
	TestTrue(TEXT("Foreground blur UV step X positive"), UVStep.X > 0.0f);
	TestTrue(TEXT("Foreground blur UV step Y positive"), UVStep.Y > 0.0f);
	FString LegacyEffectMaterial;
	TestFalse(TEXT("Foreground blur no longer writes effect material to legacy properties"),
		Data.RootWidget.Properties.TryGetString(UMGPropertyKeys::EffectMaterial, LegacyEffectMaterial));
	TestTrue(TEXT("Foreground blur wrapper keeps child"), Data.RootWidget.Children.Num() == 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerShadowRetainerWrapperTest,
	"Design2UMG.Shadow.InnerShadowRetainerWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInnerShadowRetainerWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "inner_shadow_card",
				"type": "frame",
				"name": "Inner Shadow Card",
				"width": 200,
				"height": 80,
				"fill": "#FFFFFFFF",
				"effect": {
					"type": "shadow",
					"shadowType": "inner",
					"color": "#00000066",
					"offset": { "x": 4, "y": 3 },
					"blur": 8
				},
				"children": [
					{
						"id": "title",
						"type": "text",
						"content": "Inset",
						"fontSize": 24
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Inner shadow fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	TestTrue(TEXT("Inner shadow parsed into IR"),
		Doc.Children[0]->Effects.Num() == 1
		&& Doc.Children[0]->Effects[0].Type == EDesignEffectType::Shadow
		&& Doc.Children[0]->Effects[0].Shadow.bInner);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Inner shadow uses RetainerBox wrapper"), Data.RootWidget.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Inner shadow retain rendering typed"), Data.RootWidget.Effect.bHasRetainRendering && Data.RootWidget.Effect.bRetainRendering);
	TestEqual(TEXT("Inner shadow effect material ref exported"), Data.RootWidget.Effect.EffectMaterial, TEXT("d2u.retainer.inner_shadow"));
	TestEqual(TEXT("Inner shadow texture parameter exported"), Data.RootWidget.Effect.TextureParameter, TEXT("Texture"));
	TestTrue(TEXT("Inner shadow color exported via typed effect"), Data.RootWidget.Effect.bHasEffectColor);
	TestTrue(TEXT("Inner shadow offset exported via typed effect"), Data.RootWidget.Effect.bHasEffectOffset);
	TestTrue(TEXT("Inner shadow blur step exported via typed effect"), Data.RootWidget.Effect.bHasEffectBlurUVStep);
	TestTrue(TEXT("Inner shadow normalized offset X preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectOffset.X, 0.02f, 0.0001f));
	TestTrue(TEXT("Inner shadow normalized offset Y preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectOffset.Y, 0.0375f, 0.0001f));
	TestTrue(TEXT("Inner shadow color alpha preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectColor.A, 0.4f, 0.01f));
	FString LegacyEffectMaterial;
	TestFalse(TEXT("Inner shadow no longer writes effect material to legacy properties"),
		Data.RootWidget.Properties.TryGetString(UMGPropertyKeys::EffectMaterial, LegacyEffectMaterial));
	TestTrue(TEXT("Inner shadow wrapper keeps child"), Data.RootWidget.Children.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackgroundBlurUsesTypedEffectDataTest,
	"Design2UMG.Converter.BackgroundBlurUsesTypedEffectData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBackgroundBlurUsesTypedEffectDataTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "bg_blur_card",
				"type": "frame",
				"name": "Blur Layer",
				"width": 240,
				"height": 80,
				"effect": {
					"type": "background_blur",
					"radius": 18
				},
				"children": [
					{
						"id": "title",
						"type": "text",
						"content": "Backdrop",
						"fontSize": 24
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Background blur fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Background blur uses BackgroundBlur wrapper"), Data.RootWidget.Type, TEXT("BackgroundBlur"));
	TestTrue(TEXT("Background blur strength exported via typed effect"), Data.RootWidget.Effect.bHasBlurStrength);
	TestTrue(TEXT("Background blur strength preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.BlurStrength, 18.0f));
	float LegacyBlurStrength = 0.0f;
	TestFalse(TEXT("Background blur no longer writes blur strength to legacy properties"),
		Data.RootWidget.Properties.TryGetFloat(UMGPropertyKeys::BlurStrength, LegacyBlurStrength));
	TestTrue(TEXT("Background blur wrapper keeps child"), Data.RootWidget.Children.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaEffectsConvertToTypedWrappersTest,
	"Design2UMG.FigmaParser.EffectsConvertToTypedWrappers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaEffectsConvertToTypedWrappersTest::RunTest(const FString& Parameters)
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
					"id": "1:1",
					"name": "EffectCard",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":180,"height":96},
					"absoluteRenderBounds": {"x":0,"y":0,"width":180,"height":96},
					"effects": [
						{
							"type": "INNER_SHADOW",
							"visible": true,
							"blendMode": "NORMAL",
							"offset": {"x":4,"y":-2},
							"radius": 12,
							"color": {"r":0,"g":0,"b":0,"a":0.3}
						},
						{
							"type": "BACKGROUND_BLUR",
							"visible": true,
							"radius": 16
						}
					],
					"children": [{
						"id": "1:2",
						"name": "Title",
						"type": "TEXT",
						"visible": true,
						"absoluteBoundingBox": {"x":16,"y":20,"width":80,"height":24},
						"characters": "Fx",
						"style": {"fontFamily":"Inter","fontSize":20,"fontWeight":500,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma effect wrapper fixture parses"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Figma background blur becomes outer wrapper"), Data.RootWidget.Type, TEXT("BackgroundBlur"));
	TestTrue(TEXT("Figma background blur keeps typed strength"), Data.RootWidget.Effect.bHasBlurStrength && FMath::IsNearlyEqual(Data.RootWidget.Effect.BlurStrength, 16.0f, 0.001f));
	TestTrue(TEXT("Figma background blur keeps one child"), Data.RootWidget.Children.Num() == 1);
	if (Data.RootWidget.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& InnerShadowWrapper = Data.RootWidget.Children[0];
	TestEqual(TEXT("Figma inner shadow becomes retainer child"), InnerShadowWrapper.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Figma inner shadow retain rendering typed"), InnerShadowWrapper.Effect.bHasRetainRendering && InnerShadowWrapper.Effect.bRetainRendering);
	TestEqual(TEXT("Figma inner shadow effect material typed"), InnerShadowWrapper.Effect.EffectMaterial, TEXT("d2u.retainer.inner_shadow"));
	TestTrue(TEXT("Figma inner shadow offset typed"), InnerShadowWrapper.Effect.bHasEffectOffset);
	TestTrue(TEXT("Figma inner shadow blur typed"), InnerShadowWrapper.Effect.bHasEffectBlurUVStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLayerBlurConvertsToForegroundBlurWrapperTest,
	"Design2UMG.FigmaParser.LayerBlurConvertsToForegroundBlurWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLayerBlurConvertsToForegroundBlurWrapperTest::RunTest(const FString& Parameters)
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
					"id": "1:1",
					"name": "BlurCard",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":160,"height":80},
					"absoluteRenderBounds": {"x":0,"y":0,"width":160,"height":80},
					"effects": [{
						"type": "LAYER_BLUR",
						"visible": true,
						"radius": 14
					}],
					"children": []
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma layer blur fixture parses"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Figma layer blur becomes foreground retainer wrapper"), Data.RootWidget.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Figma layer blur retain rendering typed"), Data.RootWidget.Effect.bHasRetainRendering && Data.RootWidget.Effect.bRetainRendering);
	TestEqual(TEXT("Figma layer blur effect material typed"), Data.RootWidget.Effect.EffectMaterial, TEXT("d2u.retainer.foreground_blur"));
	TestTrue(TEXT("Figma layer blur UV step typed"), Data.RootWidget.Effect.bHasEffectBlurUVStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaDropShadowConvertsToBoxShadowWrapperTest,
	"Design2UMG.FigmaParser.DropShadowConvertsToBoxShadowWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaDropShadowConvertsToBoxShadowWrapperTest::RunTest(const FString& Parameters)
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
					"id": "1:1",
					"name": "ShadowCard",
					"type": "RECTANGLE",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":160,"height":80},
					"absoluteRenderBounds": {"x":0,"y":0,"width":160,"height":80},
					"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
					"effects": [{
						"type": "DROP_SHADOW",
						"visible": true,
						"blendMode": "NORMAL",
						"offset": {"x":6,"y":4},
						"radius": 10,
						"spread": 2,
						"color": {"r":0,"g":0,"b":0,"a":0.2}
					}],
					"children": []
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma drop shadow fixture parses"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Figma outer shadow becomes overlay wrapper"), Data.RootWidget.Type, TEXT("Overlay"));
	TestTrue(TEXT("Figma outer shadow wrapper keeps shadow and content children"), Data.RootWidget.Children.Num() == 2);
	if (Data.RootWidget.Children.Num() != 2)
	{
		return false;
	}

	TestEqual(TEXT("Figma outer shadow synthetic shadow node is image"), Data.RootWidget.Children[0].Type, TEXT("Image"));
	TestTrue(TEXT("Figma outer shadow synthetic blur typed"), Data.RootWidget.Children[0].Shadow.bHasShadowBlur && FMath::IsNearlyEqual(Data.RootWidget.Children[0].Shadow.ShadowBlur, 10.0f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetainerEffectMaterialApplicationTest,
	"Design2UMG.PropertyApplier.RetainerEffectMaterialForegroundBlur",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRetainerEffectMaterialApplicationTest::RunTest(const FString& Parameters)
{
	URetainerBox* Widget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("RetainerBox created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("RetainerBox");
	Node.Effect.bRetainRendering = true;
	Node.Effect.bHasRetainRendering = true;
	Node.Effect.EffectMaterial = TEXT("d2u.retainer.foreground_blur");
	Node.Effect.TextureParameter = TEXT("Texture");
	Node.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);
	Node.Effect.bHasEffectBlurUVStep = true;
	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	UMaterialInstanceDynamic* DynamicEffect = Widget->GetEffectMaterial();
	TestTrue(TEXT("RetainerBox dynamic effect material created"), DynamicEffect != nullptr);
	if (!DynamicEffect)
	{
		return false;
	}

	TestEqual(TEXT("RetainerBox texture parameter kept"), Widget->GetTextureParameter(), FName(TEXT("Texture")));
	const FLinearColor BlurUVStep = DynamicEffect->K2_GetVectorParameterValue(TEXT("BlurUVStep"));
	TestTrue(TEXT("RetainerBox blur UV step X applied"), FMath::IsNearlyEqual(BlurUVStep.R, 0.01f, 0.0001f));
	TestTrue(TEXT("RetainerBox blur UV step Y applied"), FMath::IsNearlyEqual(BlurUVStep.G, 0.02f, 0.0001f));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerShadowRetainerMaterialApplicationTest,
	"Design2UMG.Shadow.InnerShadowRetainerMaterialApplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInnerShadowRetainerMaterialApplicationTest::RunTest(const FString& Parameters)
{
	URetainerBox* Widget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("Inner shadow RetainerBox created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("RetainerBox");
	Node.Effect.bRetainRendering = true;
	Node.Effect.bHasRetainRendering = true;
	Node.Effect.EffectMaterial = TEXT("d2u.retainer.inner_shadow");
	Node.Effect.TextureParameter = TEXT("Texture");
	Node.Effect.EffectColor = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
	Node.Effect.bHasEffectColor = true;
	Node.Effect.EffectOffset = FVector2D(0.05f, -0.03f);
	Node.Effect.bHasEffectOffset = true;
	Node.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);
	Node.Effect.bHasEffectBlurUVStep = true;
	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	UMaterialInstanceDynamic* DynamicEffect = Widget->GetEffectMaterial();
	TestTrue(TEXT("Inner shadow dynamic effect material created"), DynamicEffect != nullptr);
	if (!DynamicEffect)
	{
		return false;
	}

	const FLinearColor BlurUVStep = DynamicEffect->K2_GetVectorParameterValue(TEXT("BlurUVStep"));
	const FLinearColor InnerShadowColor = DynamicEffect->K2_GetVectorParameterValue(TEXT("InnerShadowColor"));
	const FLinearColor InnerShadowOffset = DynamicEffect->K2_GetVectorParameterValue(TEXT("InnerShadowOffset"));
	const float InnerShadowStrength = DynamicEffect->K2_GetScalarParameterValue(TEXT("InnerShadowStrength"));
	TestTrue(TEXT("Inner shadow blur UV step X applied"), FMath::IsNearlyEqual(BlurUVStep.R, 0.01f, 0.0001f));
	TestTrue(TEXT("Inner shadow blur UV step Y applied"), FMath::IsNearlyEqual(BlurUVStep.G, 0.02f, 0.0001f));
	TestTrue(TEXT("Inner shadow color R applied"), FMath::IsNearlyEqual(InnerShadowColor.R, 0.1f, 0.0001f));
	TestTrue(TEXT("Inner shadow color A applied"), FMath::IsNearlyEqual(InnerShadowColor.A, 0.4f, 0.0001f));
	TestTrue(TEXT("Inner shadow strength applied"), FMath::IsNearlyEqual(InnerShadowStrength, 0.4f, 0.0001f));
	TestTrue(TEXT("Inner shadow offset X applied"), FMath::IsNearlyEqual(InnerShadowOffset.R, 0.05f, 0.0001f));
	TestTrue(TEXT("Inner shadow offset Y applied"), FMath::IsNearlyEqual(InnerShadowOffset.G, -0.03f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRichTextBlockStyleRowsApplicationTest,
	"Design2UMG.PropertyApplier.RichTextBlockStyleRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRichTextBlockStyleRowsApplicationTest::RunTest(const FString& Parameters)
{
	URichTextBlock* Widget = NewObject<URichTextBlock>(GetTransientPackageAsObject());
	TestTrue(TEXT("RichTextBlock created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	FTextBlockStyle PreconfiguredStyle = FTextBlockStyle::GetDefault();
	PreconfiguredStyle.ShadowColorAndOpacity = FLinearColor(0.8f, 0.1f, 0.2f, 0.5f);
	PreconfiguredStyle.ShadowOffset = FVector2D(2.0f, 3.0f);
	Widget->SetDefaultTextStyle(PreconfiguredStyle);

	FUMGWidgetNode Node;
	Node.Type = TEXT("RichTextBlock");
	Node.Text.Text = TEXT("<d2u_run_0>Hello </><d2u_run_1 href=\"https://docs.example.com\">Docs</>");
	Node.Text.bHasText = true;
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.bHasFontFamily = true;
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.bHasTypefaceFontName = true;
	Node.Text.Justification = TEXT("Center");
	Node.Text.bHasJustification = true;
	Node.Text.bAutoWrapText = true;
	Node.Text.bHasAutoWrapText = true;
	Node.Text.WrapTextAt = 180.0f;
	Node.Text.bHasWrapTextAt = true;
	Node.Text.LineHeightPercentage = 1.25f;
	Node.Text.bHasLineHeightPercentage = true;
	Node.Text.Color = FLinearColor(0.1f, 0.2f, 0.3f, 1.0f);
	Node.Text.bHasColor = true;
	Node.TextStyle.FontSize = 18.0f;
	Node.TextStyle.SourceFontSize = 18.0f;
	Node.TextStyle.LetterSpacing = 1.5f;
	Node.TextStyle.bHasLetterSpacing = true;
	Node.WidgetStyle.Padding = FMargin(3.0f, 4.0f, 5.0f, 6.0f);
	Node.WidgetStyle.bHasPadding = true;
	Node.WidgetStyle.ToolTipText = TEXT("https://docs.example.com");
	Node.WidgetStyle.bHasToolTipText = true;
	Node.Properties.Add(
		RichTextStyleRowsPropertyKey,
		TEXT("{\"d2u_run_0\":{\"Color\":\"#112233FF\"},\"d2u_run_1\":{\"FontSize\":22.0,\"DesignFontSize\":22.0,\"Color\":\"#224466FF\",\"Underline\":true,\"Href\":\"https://docs.example.com\"}}"));

	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	TestEqual(TEXT("Rich text source text applied"), Widget->GetText().ToString(), TEXT("<d2u_run_0>Hello </><d2u_run_1 href=\"https://docs.example.com\">Docs</>"));
	TestEqual(TEXT("Rich text tooltip applied"), Widget->GetToolTipText().ToString(), TEXT("https://docs.example.com"));
	TestTrue(TEXT("Rich text auto wrap applied"), Widget->GetAutoWrapText());
	TestTrue(TEXT("Rich text wrap width applied"), FMath::IsNearlyEqual(Widget->GetWrapTextAt(), 180.0f, 0.001f));
	const FStructProperty* MarginProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	TestTrue(TEXT("Rich text margin property found"), MarginProperty != nullptr);
	if (MarginProperty)
	{
		const FMargin* MarginValue = MarginProperty->ContainerPtrToValuePtr<FMargin>(Widget);
		TestTrue(TEXT("Rich text padding applied as internal margin"),
			MarginValue
			&& FMath::IsNearlyEqual(MarginValue->Left, 3.0f, 0.001f)
			&& FMath::IsNearlyEqual(MarginValue->Top, 4.0f, 0.001f)
			&& FMath::IsNearlyEqual(MarginValue->Right, 5.0f, 0.001f)
			&& FMath::IsNearlyEqual(MarginValue->Bottom, 6.0f, 0.001f));
	}

	const FTextBlockStyle& DefaultStyle = Widget->GetCurrentDefaultTextStyle();
	TestEqual(TEXT("Rich text default font size applied"), DefaultStyle.Font.Size, 18.0f);
	const FLinearColor ExpectedDefaultColor = FUMGValueParser::ParseColor(
		Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Text.Color));
	TestTrue(TEXT("Rich text default color applied"),
		DefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedDefaultColor, 0.0001f));
	TestTrue(TEXT("Rich text default style preserves preconfigured shadow color"),
		DefaultStyle.ShadowColorAndOpacity.Equals(PreconfiguredStyle.ShadowColorAndOpacity, 0.0001f));
	TestTrue(TEXT("Rich text default style preserves preconfigured shadow offset"),
		DefaultStyle.ShadowOffset == PreconfiguredStyle.ShadowOffset);

	UDataTable* StyleSet = Widget->GetTextStyleSet();
	TestTrue(TEXT("Rich text style set created"), StyleSet != nullptr);
	if (!StyleSet)
	{
		return false;
	}
	TestTrue(TEXT("Rich text generated style set is widget-owned"), StyleSet->GetOuter() == Widget);
	TestFalse(TEXT("Rich text generated style set is not transient"), StyleSet->HasAnyFlags(RF_Transient));

	const FRichTextStyleRow* BaseRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_0"), TEXT("RichTextBlockStyleRowsApplication"));
	TestNotNull(TEXT("Base row exists"), BaseRow);
	if (!BaseRow)
	{
		return false;
	}
	TestEqual(TEXT("Sparse base row inherits default font size"), BaseRow->TextStyle.Font.Size, 18.0f);
	TestTrue(TEXT("Sparse base row preserves base shadow color"),
		BaseRow->TextStyle.ShadowColorAndOpacity.Equals(PreconfiguredStyle.ShadowColorAndOpacity, 0.0001f));

	const FRichTextStyleRow* LinkRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_1"), TEXT("RichTextBlockStyleRowsApplication"));
	TestNotNull(TEXT("Link row exists"), LinkRow);
	if (!LinkRow)
	{
		return false;
	}

	TestEqual(TEXT("Link row font size applied"), LinkRow->TextStyle.Font.Size, 22.0f);
	TestTrue(TEXT("Link row underline brush created"), LinkRow->TextStyle.UnderlineBrush.DrawAs != ESlateBrushDrawType::NoDrawType);
	TestTrue(TEXT("Link row color applied"),
		LinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(
			FUMGValueParser::ParseColor(TEXT("#224466FF")),
			0.0001f));
	TestTrue(TEXT("Link row preserves base shadow color"),
		LinkRow->TextStyle.ShadowColorAndOpacity.Equals(PreconfiguredStyle.ShadowColorAndOpacity, 0.0001f));
	TestTrue(TEXT("Link row preserves base shadow offset"),
		LinkRow->TextStyle.ShadowOffset == PreconfiguredStyle.ShadowOffset);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetSwitcherUsesTypedStateTest,
	"Design2UMG.PropertyApplier.WidgetSwitcherUsesTypedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetSwitcherUsesTypedStateTest::RunTest(const FString& Parameters)
{
	UWidgetSwitcher* Widget = NewObject<UWidgetSwitcher>(GetTransientPackageAsObject());
	TestTrue(TEXT("WidgetSwitcher created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	Widget->AddChild(NewObject<UTextBlock>(Widget));
	Widget->AddChild(NewObject<UTextBlock>(Widget));
	Widget->AddChild(NewObject<UTextBlock>(Widget));

	FUMGWidgetNode Node;
	Node.Type = TEXT("WidgetSwitcher");
	Node.WidgetState.ActiveWidgetIndex = 2;
	Node.WidgetState.bHasActiveWidgetIndex = true;

	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);
	TestEqual(TEXT("Typed widget state applies active index"), Widget->GetActiveWidgetIndex(), 2);

	int32 LegacyActiveWidgetIndex = 0;
	TestFalse(TEXT("Typed widget state test keeps legacy properties empty"),
		Node.Properties.TryGetInt(UMGPropertyKeys::ActiveWidgetIndex, LegacyActiveWidgetIndex));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetStateJsonRoundTripTest,
	"Design2UMG.UMGJson.WidgetStateRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetStateJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_WidgetStateRoundTrip");
	SourceData.RootWidget.Type = TEXT("WidgetSwitcher");
	SourceData.RootWidget.Id = TEXT("switcher_root");
	SourceData.RootWidget.Name = TEXT("SwitcherRoot");
	SourceData.RootWidget.WidgetState.ActiveWidgetIndex = 1;
	SourceData.RootWidget.WidgetState.bHasActiveWidgetIndex = true;

	FUMGWidgetNode FirstPage;
	FirstPage.Type = TEXT("TextBlock");
	FirstPage.Id = TEXT("page_a");
	FirstPage.Name = TEXT("PageA");
	FirstPage.Properties.Add(TEXT("Text"), TEXT("A"));

	FUMGWidgetNode SecondPage;
	SecondPage.Type = TEXT("TextBlock");
	SecondPage.Id = TEXT("page_b");
	SecondPage.Name = TEXT("PageB");
	SecondPage.Properties.Add(TEXT("Text"), TEXT("B"));

	SourceData.RootWidget.Children.Add(MoveTemp(FirstPage));
	SourceData.RootWidget.Children.Add(MoveTemp(SecondPage));

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestFalse(TEXT("Serialized widget state emits WidgetState payload"), Json.IsEmpty() || !Json.Contains(TEXT("\"WidgetState\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Widget state JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	TestTrue(TEXT("Parsed widget state keeps typed active index"), ParsedData.RootWidget.WidgetState.bHasActiveWidgetIndex);
	TestEqual(TEXT("Parsed widget state active index preserved"), ParsedData.RootWidget.WidgetState.ActiveWidgetIndex, 1);

	int32 LegacyActiveWidgetIndex = 0;
	TestFalse(TEXT("Widget state round-trip does not rehydrate legacy ActiveWidgetIndex"),
		ParsedData.RootWidget.Properties.TryGetInt(UMGPropertyKeys::ActiveWidgetIndex, LegacyActiveWidgetIndex));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedBrushImageGradientJsonRoundTripTest,
	"Design2UMG.UMGJson.TypedBrushImageGradientRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedBrushImageGradientJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_TypedBrushRoundTrip");
	SourceData.RootWidget.Type = TEXT("CanvasPanel");
	SourceData.RootWidget.Id = TEXT("root");
	SourceData.RootWidget.Name = TEXT("Root");

	FUMGWidgetNode ImageNode;
	ImageNode.Type = TEXT("Image");
	ImageNode.Id = TEXT("typed_image");
	ImageNode.Name = TEXT("TypedImage");
	ImageNode.Brush.Image.TexturePath = TEXT("/Game/UI/T_TypedImage.T_TypedImage");
	ImageNode.Brush.Image.bHasTexturePath = true;
	ImageNode.Brush.Image.Mode = TEXT("Crop");
	ImageNode.Brush.Image.bHasMode = true;
	ImageNode.Brush.Image.Transform.A = 1.0f;
	ImageNode.Brush.Image.Transform.B = 0.0f;
	ImageNode.Brush.Image.Transform.TX = 0.125f;
	ImageNode.Brush.Image.Transform.C = 0.0f;
	ImageNode.Brush.Image.Transform.D = 1.0f;
	ImageNode.Brush.Image.Transform.TY = -0.25f;
	ImageNode.Brush.Image.bHasTransform = true;

	FUMGWidgetNode GradientNode;
	GradientNode.Type = TEXT("Image");
	GradientNode.Id = TEXT("typed_gradient");
	GradientNode.Name = TEXT("TypedGradient");
	GradientNode.Brush.Gradient.Type = TEXT("Angular");
	GradientNode.Brush.Gradient.bHasType = true;
	GradientNode.Brush.Gradient.Rotation = 135.0f;
	GradientNode.Brush.Gradient.bHasRotation = true;
	GradientNode.Brush.Gradient.Center = FVector2D(0.45f, 0.55f);
	GradientNode.Brush.Gradient.bHasCenter = true;
	GradientNode.Brush.Gradient.Size = FVector2D(0.8f, 1.1f);
	GradientNode.Brush.Gradient.bHasSize = true;
	{
		FUMGVisualBrushData::FGradientStopData& Stop0 = GradientNode.Brush.Gradient.Stops.AddDefaulted_GetRef();
		Stop0.Color = FLinearColor::Red;
		Stop0.Position = 0.0f;
		FUMGVisualBrushData::FGradientStopData& Stop1 = GradientNode.Brush.Gradient.Stops.AddDefaulted_GetRef();
		Stop1.Color = FLinearColor::Green;
		Stop1.Position = 0.5f;
		FUMGVisualBrushData::FGradientStopData& Stop2 = GradientNode.Brush.Gradient.Stops.AddDefaulted_GetRef();
		Stop2.Color = FLinearColor::Blue;
		Stop2.Position = 1.0f;
	}

	SourceData.RootWidget.Children.Add(MoveTemp(ImageNode));
	SourceData.RootWidget.Children.Add(MoveTemp(GradientNode));

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestTrue(TEXT("Typed brush round-trip emits nested image payload"), Json.Contains(TEXT("\"Image\"")));
	TestTrue(TEXT("Typed brush round-trip emits nested gradient payload"), Json.Contains(TEXT("\"Gradient\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Typed brush round-trip parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	const FUMGWidgetNode* ParsedImageNode = FindWidgetById(ParsedData.RootWidget, TEXT("typed_image"));
	const FUMGWidgetNode* ParsedGradientNode = FindWidgetById(ParsedData.RootWidget, TEXT("typed_gradient"));
	TestTrue(TEXT("Parsed typed image node found"), ParsedImageNode != nullptr);
	TestTrue(TEXT("Parsed typed gradient node found"), ParsedGradientNode != nullptr);
	if (!ParsedImageNode || !ParsedGradientNode)
	{
		return false;
	}

	TestTrue(TEXT("Typed image texture path preserved"), ParsedImageNode->Brush.Image.bHasTexturePath && ParsedImageNode->Brush.Image.TexturePath == TEXT("/Game/UI/T_TypedImage.T_TypedImage"));
	TestTrue(TEXT("Typed image mode preserved"), ParsedImageNode->Brush.Image.bHasMode && ParsedImageNode->Brush.Image.Mode == TEXT("Crop"));
	TestTrue(TEXT("Typed image transform preserved"), ParsedImageNode->Brush.Image.bHasTransform && FMath::IsNearlyEqual(ParsedImageNode->Brush.Image.Transform.TX, 0.125f, 0.0001f));
	TestEqual(TEXT("Typed image helper still exposes texture path"), GetWidgetProperty(*ParsedImageNode, TEXT("TexturePath")), TEXT("/Game/UI/T_TypedImage.T_TypedImage"));

	TestTrue(TEXT("Typed gradient type preserved"), ParsedGradientNode->Brush.Gradient.bHasType && ParsedGradientNode->Brush.Gradient.Type == TEXT("Angular"));
	TestTrue(TEXT("Typed gradient rotation preserved"), ParsedGradientNode->Brush.Gradient.bHasRotation && FMath::IsNearlyEqual(ParsedGradientNode->Brush.Gradient.Rotation, 135.0f, 0.001f));
	TestTrue(TEXT("Typed gradient stop count preserved"), ParsedGradientNode->Brush.Gradient.Stops.Num() == 3);
	TestEqual(TEXT("Typed gradient helper exposes material ref"), GetWidgetProperty(*ParsedGradientNode, TEXT("MaterialRef")), TEXT("angulargradient"));
	TestTrue(TEXT("Typed gradient helper exposes material params"), GetWidgetProperty(*ParsedGradientNode, TEXT("MaterialParams")).Contains(TEXT("\"Stops\"")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerShadowEffectJsonRoundTripTest,
	"Design2UMG.UMGJson.InnerShadowEffectRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInnerShadowEffectJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_InnerShadowEffectRoundTrip");
	SourceData.RootWidget.Type = TEXT("RetainerBox");
	SourceData.RootWidget.Id = TEXT("inner_shadow_root");
	SourceData.RootWidget.Name = TEXT("InnerShadowRoot");
	SourceData.RootWidget.Effect.bRetainRendering = true;
	SourceData.RootWidget.Effect.bHasRetainRendering = true;
	SourceData.RootWidget.Effect.EffectMaterial = TEXT("d2u.retainer.inner_shadow");
	SourceData.RootWidget.Effect.TextureParameter = TEXT("Texture");
	SourceData.RootWidget.Effect.EffectColor = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
	SourceData.RootWidget.Effect.bHasEffectColor = true;
	SourceData.RootWidget.Effect.EffectOffset = FVector2D(0.05f, -0.03f);
	SourceData.RootWidget.Effect.bHasEffectOffset = true;
	SourceData.RootWidget.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);
	SourceData.RootWidget.Effect.bHasEffectBlurUVStep = true;

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestFalse(TEXT("Serialized inner shadow emits Effect payload"), Json.IsEmpty() || !Json.Contains(TEXT("\"Effect\"")));
	TestTrue(TEXT("Serialized inner shadow emits effect color"), Json.Contains(TEXT("\"EffectColor\"")));
	TestTrue(TEXT("Serialized inner shadow emits effect offset"), Json.Contains(TEXT("\"EffectOffset\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Inner shadow effect JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	TestEqual(TEXT("Parsed inner shadow effect material preserved"), ParsedData.RootWidget.Effect.EffectMaterial, TEXT("d2u.retainer.inner_shadow"));
	TestTrue(TEXT("Parsed inner shadow color preserved"), ParsedData.RootWidget.Effect.bHasEffectColor);
	TestTrue(TEXT("Parsed inner shadow offset preserved"), ParsedData.RootWidget.Effect.bHasEffectOffset);
	TestTrue(TEXT("Parsed inner shadow blur step preserved"), ParsedData.RootWidget.Effect.bHasEffectBlurUVStep);
	TestTrue(TEXT("Parsed inner shadow color alpha preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectColor.A, 0.4f, 0.0001f));
	TestTrue(TEXT("Parsed inner shadow offset X preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectOffset.X, 0.05f, 0.0001f));
	TestTrue(TEXT("Parsed inner shadow offset Y preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectOffset.Y, -0.03f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadialGradientMultiStopFidelityTest,
	"Design2UMG.Gradient.RadialMultiStopFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRadialGradientMultiStopFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"name": "Root",
				"width": 64,
				"height": 64,
				"children": [
					{
						"id": "grad_card",
						"type": "rectangle",
						"name": "Gradient Card",
						"width": 64,
						"height": 64,
						"fill": {
							"type": "gradient",
							"gradientType": "radial",
							"colors": [
								{ "color": "#FF0000FF", "position": 0.0 },
								{ "color": "#00FF00FF", "position": 0.5 },
								{ "color": "#0000FFFF", "position": 1.0 }
							]
						}
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Radial multi-stop gradient fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("grad_card"));
	TestTrue(TEXT("Radial gradient widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	TestEqual(TEXT("Radial gradient material ref preserved"), GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialRef), TEXT("radialgradient"));
	const FString MaterialParams = GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialParams);
	TestTrue(TEXT("Radial gradient exports Stops array"), MaterialParams.Contains(TEXT("\"Stops\"")));
	TestFalse(TEXT("Radial gradient no longer falls back to center/edge pair"), MaterialParams.Contains(TEXT("ColorCenter")));
	TestFalse(TEXT("Radial gradient no longer writes material ref to legacy properties"), HasLegacyWidgetProperty(*Widget, UMGPropertyKeys::MaterialRef));
	TestFalse(TEXT("Radial gradient no longer writes material params to legacy properties"), HasLegacyWidgetProperty(*Widget, UMGPropertyKeys::MaterialParams));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("radialgradient"),
		Design2UMGWidgetNodeAccess::BuildEffectivePropertyBag(*Widget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Radial gradient texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor CenterPixel;
	FColor MidPixel;
	FColor EdgePixel;
	TestTrue(TEXT("Radial gradient center pixel readable"), ReadTexturePixel(Texture, 32, 32, CenterPixel));
	TestTrue(TEXT("Radial gradient mid pixel readable"), ReadTexturePixel(Texture, 32, 16, MidPixel));
	TestTrue(TEXT("Radial gradient edge pixel readable"), ReadTexturePixel(Texture, 0, 32, EdgePixel));

	TestTrue(TEXT("Radial gradient center keeps first stop red"), CenterPixel.R > 200 && CenterPixel.G < 80 && CenterPixel.B < 80);
	TestTrue(TEXT("Radial gradient midpoint reaches middle stop green"), MidPixel.G > MidPixel.R && MidPixel.G > MidPixel.B);
	TestTrue(TEXT("Radial gradient edge reaches final stop blue"), EdgePixel.B > 200 && EdgePixel.R < 80 && EdgePixel.G < 80);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadialGradientTransformFidelityTest,
	"Design2UMG.Gradient.RadialTransformFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRadialGradientTransformFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"name": "Root",
				"width": 64,
				"height": 64,
				"children": [
					{
						"id": "grad_transform",
						"type": "rectangle",
						"name": "Gradient Transform",
						"width": 64,
						"height": 64,
						"fill": {
							"type": "gradient",
							"gradientType": "radial",
							"rotation": 90,
							"center": {"x": 0.25, "y": 0.75},
							"size": {"width": 0.25, "height": 1.0},
							"colors": [
								{ "color": "#FF0000FF", "position": 0.0 },
								{ "color": "#0000FFFF", "position": 1.0 }
							]
						}
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Radial transform fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("grad_transform"));
	TestTrue(TEXT("Radial transform widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	const FString MaterialParams = GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialParams);
	TestEqual(TEXT("Radial transform material ref preserved"), GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialRef), TEXT("radialgradient"));
	TestTrue(TEXT("Radial transform exports center X"), MaterialParams.Contains(TEXT("\"CenterX\":0.2500")));
	TestTrue(TEXT("Radial transform exports center Y"), MaterialParams.Contains(TEXT("\"CenterY\":0.7500")));
	TestTrue(TEXT("Radial transform exports size width"), MaterialParams.Contains(TEXT("\"SizeWidth\":0.2500")));
	TestTrue(TEXT("Radial transform exports size height"), MaterialParams.Contains(TEXT("\"SizeHeight\":1.0000")));
	TestTrue(TEXT("Radial transform exports rotation"), MaterialParams.Contains(TEXT("\"Rotation\":90.0000")));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("radialgradient"),
		Design2UMGWidgetNodeAccess::BuildEffectivePropertyBag(*Widget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Radial transform texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor ShiftedCenterPixel;
	FColor RotatedMajorAxisPixel;
	FColor RotatedMinorAxisPixel;
	FColor TextureCenterPixel;
	TestTrue(TEXT("Radial transform shifted center pixel readable"), ReadTexturePixel(Texture, 16, 47, ShiftedCenterPixel));
	TestTrue(TEXT("Radial transform major-axis pixel readable"), ReadTexturePixel(Texture, 27, 47, RotatedMajorAxisPixel));
	TestTrue(TEXT("Radial transform minor-axis pixel readable"), ReadTexturePixel(Texture, 16, 36, RotatedMinorAxisPixel));
	TestTrue(TEXT("Radial transform texture-center pixel readable"), ReadTexturePixel(Texture, 32, 32, TextureCenterPixel));

	TestTrue(TEXT("Shifted center keeps red core"), ShiftedCenterPixel.R > 200 && ShiftedCenterPixel.B < 80);
	TestTrue(TEXT("Rotated major axis stays inside elongated ellipse"), RotatedMajorAxisPixel.R > RotatedMajorAxisPixel.B);
	TestTrue(TEXT("Rotated minor axis reaches edge sooner"), RotatedMinorAxisPixel.B > 200 && RotatedMinorAxisPixel.R < 80);
	TestTrue(TEXT("Default center no longer acts as gradient center"), TextureCenterPixel.B > 180 && TextureCenterPixel.R < 120);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaRadialGradientHandleFidelityTest,
	"Design2UMG.FigmaParser.RadialGradientHandleFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaRadialGradientHandleFidelityTest::RunTest(const FString& Parameters)
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":64,"height":64},
					"children": [{
						"id": "grad_figma",
						"name": "Figma Radial",
						"type": "RECTANGLE",
						"visible": true,
						"absoluteBoundingBox": {"x":0,"y":0,"width":64,"height":64},
						"fills": [{
							"type": "GRADIENT_RADIAL",
							"visible": true,
							"gradientHandlePositions": [
								{"x":0.25,"y":0.75},
								{"x":0.25,"y":0.875},
								{"x":0.75,"y":0.75}
							],
							"gradientStops": [
								{"position":0.0,"color":{"r":1.0,"g":0.0,"b":0.0,"a":1.0}},
								{"position":1.0,"color":{"r":0.0,"g":0.0,"b":1.0,"a":1.0}}
							]
						}]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma radial handle fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& GradientNode = Doc.Children[0]->Children[0];
	TestEqual(TEXT("Figma radial gradient type preserved"), GradientNode->Fill.Gradient.GradientType, EDesignGradientType::Radial);
	TestTrue(TEXT("Figma radial center parsed"), GradientNode->Fill.Gradient.bHasCenter
		&& FMath::IsNearlyEqual(GradientNode->Fill.Gradient.CenterX, 0.25f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Fill.Gradient.CenterY, 0.75f, 0.001f));
	TestTrue(TEXT("Figma radial size parsed"), GradientNode->Fill.Gradient.bHasSize
		&& FMath::IsNearlyEqual(GradientNode->Fill.Gradient.SizeWidth, 0.25f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Fill.Gradient.SizeHeight, 1.0f, 0.001f));
	TestTrue(TEXT("Figma radial rotation parsed"), FMath::IsNearlyEqual(GradientNode->Fill.Gradient.Rotation, 90.0f, 0.001f));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("grad_figma"));
	TestTrue(TEXT("Figma radial widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	const FString MaterialParams = GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialParams);
	TestEqual(TEXT("Figma radial material ref preserved"), GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialRef), TEXT("radialgradient"));
	TestTrue(TEXT("Figma radial exports center X"), MaterialParams.Contains(TEXT("\"CenterX\":0.2500")));
	TestTrue(TEXT("Figma radial exports center Y"), MaterialParams.Contains(TEXT("\"CenterY\":0.7500")));
	TestTrue(TEXT("Figma radial exports size width"), MaterialParams.Contains(TEXT("\"SizeWidth\":0.2500")));
	TestTrue(TEXT("Figma radial exports size height"), MaterialParams.Contains(TEXT("\"SizeHeight\":1.0000")));
	TestTrue(TEXT("Figma radial exports rotation"), MaterialParams.Contains(TEXT("\"Rotation\":90.0000")));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("radialgradient"),
		Design2UMGWidgetNodeAccess::BuildEffectivePropertyBag(*Widget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Figma radial texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor ShiftedCenterPixel;
	FColor RotatedMajorAxisPixel;
	FColor RotatedMinorAxisPixel;
	TestTrue(TEXT("Figma radial shifted center pixel readable"), ReadTexturePixel(Texture, 16, 47, ShiftedCenterPixel));
	TestTrue(TEXT("Figma radial major-axis pixel readable"), ReadTexturePixel(Texture, 27, 47, RotatedMajorAxisPixel));
	TestTrue(TEXT("Figma radial minor-axis pixel readable"), ReadTexturePixel(Texture, 16, 36, RotatedMinorAxisPixel));

	TestTrue(TEXT("Figma radial shifted center keeps red core"), ShiftedCenterPixel.R > 200 && ShiftedCenterPixel.B < 80);
	TestTrue(TEXT("Figma radial rotated major axis stays inside ellipse"), RotatedMajorAxisPixel.R > RotatedMajorAxisPixel.B);
	TestTrue(TEXT("Figma radial rotated minor axis reaches edge sooner"), RotatedMinorAxisPixel.B > 200 && RotatedMinorAxisPixel.R < 80);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxShadowBlurPaddingFidelityTest,
	"Design2UMG.Shadow.BoxShadowBlurPaddingFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBoxShadowBlurPaddingFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "shadow_card",
				"type": "rectangle",
				"name": "Shadow Card",
				"width": 40,
				"height": 24,
				"fill": "#FFFFFFFF",
				"effect": {
					"type": "shadow",
					"shadowType": "outer",
					"color": "#000000FF",
					"offset": { "x": 0, "y": 0 },
					"blur": 8,
					"spread": 0
				}
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Box shadow blur-padding fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Wrapper = FindWidgetById(Data.RootWidget, TEXT("shadow_card"));
	const FUMGWidgetNode* ShadowWidget = FindWidgetById(Data.RootWidget, TEXT("shadow_card_shadow0"));
	TestTrue(TEXT("Shadow wrapper exists"), Wrapper != nullptr);
	TestTrue(TEXT("Shadow image child exists"), ShadowWidget != nullptr);
	if (!Wrapper || !ShadowWidget)
	{
		return false;
	}

	TestEqual(TEXT("Box shadow wraps widget with overlay"), Wrapper->Type, TEXT("Overlay"));
	TestEqual(TEXT("Box shadow material ref preserved"), GetWidgetProperty(*ShadowWidget, UMGPropertyKeys::MaterialRef), TEXT("boxshadow"));
	TestTrue(TEXT("Box shadow width includes blur padding"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("WidthOverride"))), 56.0f, 0.001f));
	TestTrue(TEXT("Box shadow height includes blur padding"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("HeightOverride"))), 40.0f, 0.001f));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("boxshadow"),
		Design2UMGWidgetNodeAccess::BuildEffectivePropertyBag(*ShadowWidget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Box shadow texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor CornerPixel;
	FColor CenterPixel;
	TestTrue(TEXT("Box shadow corner pixel readable"), ReadTexturePixel(Texture, 0, 0, CornerPixel));
	TestTrue(TEXT("Box shadow center pixel readable"), ReadTexturePixel(Texture, 28, 20, CenterPixel));

	TestTrue(TEXT("Blur padding corner stays substantially softer than the core"), CornerPixel.A * 4 < CenterPixel.A);
	TestTrue(TEXT("Shadow core stays opaque"), CenterPixel.A > 200);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxShadowExplicitBlurPaddingMaterialParamTest,
	"Design2UMG.Shadow.ExplicitBlurPaddingMaterialParam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBoxShadowExplicitBlurPaddingMaterialParamTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode LegacyShadow;
	LegacyShadow.Properties.Add(TEXT("WidthOverride"), TEXT("56"));
	LegacyShadow.Properties.Add(TEXT("HeightOverride"), TEXT("40"));
	LegacyShadow.Properties.Add(UMGPropertyKeys::ShadowBlur, TEXT("8"));
	LegacyShadow.Properties.Add(TEXT("MaterialParams"), TEXT("{\"Color\":\"#000000FF\",\"CornerRadius\":0.0}"));

	FUMGWidgetNode ExplicitPaddingShadow;
	ExplicitPaddingShadow.Properties.Add(TEXT("WidthOverride"), TEXT("56"));
	ExplicitPaddingShadow.Properties.Add(TEXT("HeightOverride"), TEXT("40"));
	ExplicitPaddingShadow.Properties.Add(UMGPropertyKeys::ShadowBlur, TEXT("8"));
	ExplicitPaddingShadow.Properties.Add(TEXT("MaterialParams"), TEXT("{\"Color\":\"#000000FF\",\"CornerRadius\":0.0,\"BlurPadding\":8.0}"));

	UTexture2D* LegacyTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("boxshadow"),
		LegacyShadow.Properties,
		GetTransientPackageAsObject());
	UTexture2D* ExplicitPaddingTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("boxshadow"),
		ExplicitPaddingShadow.Properties,
		GetTransientPackageAsObject());
	TestTrue(TEXT("Legacy box shadow texture generated"), LegacyTexture != nullptr);
	TestTrue(TEXT("Explicit blur-padding texture generated"), ExplicitPaddingTexture != nullptr);
	if (!LegacyTexture || !ExplicitPaddingTexture)
	{
		return false;
	}

	FColor LegacyCornerPixel;
	FColor ExplicitCornerPixel;
	FColor ExplicitCenterPixel;
	TestTrue(TEXT("Legacy corner pixel readable"), ReadTexturePixel(LegacyTexture, 0, 0, LegacyCornerPixel));
	TestTrue(TEXT("Explicit blur-padding corner pixel readable"), ReadTexturePixel(ExplicitPaddingTexture, 0, 0, ExplicitCornerPixel));
	TestTrue(TEXT("Explicit blur-padding center pixel readable"), ReadTexturePixel(ExplicitPaddingTexture, 28, 20, ExplicitCenterPixel));

	TestTrue(TEXT("Explicit blur-padding softens the padded corner"), ExplicitCornerPixel.A < LegacyCornerPixel.A);
	TestTrue(TEXT("Explicit blur-padding keeps the shadow core visible"), ExplicitCenterPixel.A > 150);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxShadowNegativeSpreadFidelityTest,
	"Design2UMG.Shadow.NegativeSpreadFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBoxShadowNegativeSpreadFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "shadow_chip",
				"type": "rectangle",
				"name": "Shadow Chip",
				"width": 40,
				"height": 40,
				"fill": "#FFFFFFFF",
				"effect": {
					"type": "shadow",
					"shadowType": "outer",
					"color": "#000000FF",
					"offset": { "x": 0, "y": 0 },
					"blur": 0,
					"spread": -6
				}
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Negative spread shadow fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* ShadowWidget = FindWidgetById(Data.RootWidget, TEXT("shadow_chip_shadow0"));
	TestTrue(TEXT("Negative spread shadow image child exists"), ShadowWidget != nullptr);
	if (!ShadowWidget)
	{
		return false;
	}

	TestEqual(TEXT("Negative spread keeps rounded rect path when blur is zero"), GetWidgetProperty(*ShadowWidget, UMGPropertyKeys::MaterialRef), TEXT("roundedrect"));
	TestTrue(TEXT("Negative spread shrinks width"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("WidthOverride"))), 28.0f, 0.001f));
	TestTrue(TEXT("Negative spread shrinks height"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("HeightOverride"))), 28.0f, 0.001f));
	FVector2D NegativeSpreadTranslation = FVector2D::ZeroVector;
	TestTrue(TEXT("Negative spread exposes render translation"),
		Design2UMGWidgetNodeAccess::BuildEffectivePropertyBag(*ShadowWidget).TryGetVector2D(TEXT("RenderTranslation"), NegativeSpreadTranslation));
	TestTrue(TEXT("Negative spread translates back inward"), NegativeSpreadTranslation.Equals(FVector2D(6.0f, 6.0f), 0.001f));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("roundedrect"),
		Design2UMGWidgetNodeAccess::BuildEffectivePropertyBag(*ShadowWidget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Negative spread texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	TestEqual(TEXT("Negative spread opaque pixel area matches shrunken rect"), CountOpaqueTexturePixels(Texture), 28 * 28);
	return true;
}
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWrapGapUsesInnerSlotPaddingOnlyTest,
	"Design2UMG.Layout.WrapGapUsesInnerSlotPaddingOnly",
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
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* WrapWidget = FindWidgetById(Data.RootWidget, TEXT("wrap1"));
	const FUMGWidgetNode* Card1SizeBox = FindWidgetById(Data.RootWidget, TEXT("card1_sz"));
	TestTrue(TEXT("Wrap widget exists"), WrapWidget != nullptr);
	TestTrue(TEXT("Wrap fill child gets SizeBox"), Card1SizeBox != nullptr);
	if (!WrapWidget || !Card1SizeBox)
	{
		return false;
	}

	TestEqual(TEXT("Wrap child SizeBox type"), Card1SizeBox->Type, TEXT("SizeBox"));
	TestEqual(TEXT("Wrap fill child preserves measured width"), GetWidgetProperty(*Card1SizeBox, TEXT("WidthOverride")), TEXT("100.0"));
	TestEqual(TEXT("Wrap fill child preserves measured height"), GetWidgetProperty(*Card1SizeBox, TEXT("HeightOverride")), TEXT("60.0"));
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
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* LabelSizeBox = FindWidgetById(Data.RootWidget, TEXT("nav_label_sz"));
	TestTrue(TEXT("Centered flow fill text gets SizeBox"), LabelSizeBox != nullptr);
	if (!LabelSizeBox)
	{
		return false;
	}

	TestEqual(TEXT("Centered flow fill text preserves measured width"), GetWidgetProperty(*LabelSizeBox, TEXT("WidthOverride")), TEXT("72.0"));
	const FString HeightOverride = GetWidgetProperty(*LabelSizeBox, TEXT("HeightOverride"));
	const FString MinDesiredHeight = GetWidgetProperty(*LabelSizeBox, TEXT("MinDesiredHeight"));
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
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	TestTrue(TEXT("Empty slot array keeps semantic placeholder"), Doc.Children[0]->bSemanticPlaceholderSurface);
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	const FUMGWidgetData BaseData = FDesign2UMGConverter::Convert(BaseDoc);
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
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Label = FindWidgetById(Data.RootWidget, TEXT("label"));
	const FUMGWidgetNode* LabelSizeBox = FindWidgetById(Data.RootWidget, TEXT("label_sz"));
	TestNotNull(TEXT("Converted label exists"), Label);
	TestNotNull(TEXT("Wrapped label SizeBox exists"), LabelSizeBox);
	if (!Label || !LabelSizeBox)
	{
		return false;
	}
	TestEqual(TEXT("textAlignVertical kept as metadata"), GetWidgetProperty(*Label, TEXT("TextAlignVertical")), TEXT("Center"));
	TestEqual(TEXT("Outer SizeBox slot stays top-aligned"), LabelSizeBox->Slot.VerticalAlignment, TEXT("Top"));
	FMargin Padding = FMargin(0.0f);
	TestFalse(TEXT("Auto-height text does not synthesize internal padding"),
		Design2UMGWidgetNodeAccess::BuildEffectivePropertyBag(*Label).TryGetMargin(TEXT("Padding"), Padding));
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
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Label = FindWidgetById(Data.RootWidget, TEXT("label"));
	const FUMGWidgetNode* LabelSizeBox = FindWidgetById(Data.RootWidget, TEXT("label_sz"));
	TestNotNull(TEXT("Converted fixed-height label exists"), Label);
	TestNotNull(TEXT("Wrapped fixed-height label SizeBox exists"), LabelSizeBox);
	if (!Label || !LabelSizeBox)
	{
		return false;
	}

	TestEqual(TEXT("Fixed-height textAlignVertical kept as metadata"), GetWidgetProperty(*Label, TEXT("TextAlignVertical")), TEXT("Center"));
	TestEqual(TEXT("Fixed-height label outer slot stays top-aligned"), LabelSizeBox->Slot.VerticalAlignment, TEXT("Top"));
	FMargin Padding = FMargin(0.0f);
	TestTrue(TEXT("Fixed-height text synthesizes internal padding"),
		Design2UMGWidgetNodeAccess::BuildEffectivePropertyBag(*Label).TryGetMargin(TEXT("Padding"), Padding));
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
	TestTrue(TEXT("figma components json field"), !Doc.FigmaDocument.ComponentsJson.IsEmpty());
	TestTrue(TEXT("figma componentSets json field"), !Doc.FigmaDocument.ComponentSetsJson.IsEmpty());
	TestTrue(TEXT("figma components json carries component defs"), Doc.FigmaDocument.ComponentsJson.Contains(TEXT("componentPropertyDefinitions")));
	TestTrue(TEXT("figma componentSets json carries variant groups"), Doc.FigmaDocument.ComponentSetsJson.Contains(TEXT("variantGroupProperties")));
	TestTrue(TEXT("figma doc prototypeStartNodeID"), Doc.Metadata.FindRef(TEXT("prototypeStartNodeID")) == TEXT("0:2"));
	TestTrue(TEXT("figma doc prototypeDevice"), Doc.Metadata.Contains(TEXT("prototypeDevice")));
	TestTrue(TEXT("figma absolute positioning semantic"), Node->bAbsoluteLayoutPositioning);
	TestTrue(TEXT("figma layout ext layoutPositioning"), Node->FigmaLayout.LayoutPositioning == TEXT("ABSOLUTE"));
	TestTrue(TEXT("figma layout ext counterAxisSpacing"), Node->FigmaLayout.bHasCounterAxisSpacing && FMath::IsNearlyEqual(Node->FigmaLayout.CounterAxisSpacing, 6.0f, 0.001f));
	TestTrue(TEXT("figma layout ext layoutWrap"), Node->FigmaLayout.LayoutWrap == TEXT("WRAP"));
	TestTrue(TEXT("figma layout ext strokesIncludedInLayout"), Node->FigmaLayout.bHasStrokesIncludedInLayout && Node->FigmaLayout.bStrokesIncludedInLayout == false);
	TestTrue(TEXT("figma layout ext itemReverseZIndex"), Node->FigmaLayout.bHasItemReverseZIndex && Node->FigmaLayout.bItemReverseZIndex);
	TestTrue(TEXT("figma layout ext boundVariables"), !Node->FigmaLayout.BoundVariablesJson.IsEmpty());
	TestTrue(TEXT("figma prototype overflowDirection typed"), Node->FigmaPrototype.OverflowDirection == TEXT("HORIZONTAL_SCROLLING"));
	TestTrue(TEXT("figma prototype transitionNodeID typed"), Node->FigmaPrototype.TransitionNodeId == TEXT("1:3"));
	TestTrue(TEXT("figma prototype legacy transition typed"), Node->FigmaPrototype.Transition.bHasDuration
		&& FMath::IsNearlyEqual(Node->FigmaPrototype.Transition.Duration, 420.0f, 0.001f)
		&& Node->FigmaPrototype.Transition.EasingType == TEXT("EASE_OUT"));
	TestTrue(TEXT("figma prototype interactions typed"), Node->FigmaPrototype.Interactions.Num() == 1
		&& Node->FigmaPrototype.Interactions[0].TriggerType == TEXT("ON_CLICK")
		&& Node->FigmaPrototype.Interactions[0].Actions.Num() == 1
		&& Node->FigmaPrototype.Interactions[0].Actions[0].Type == TEXT("NODE")
		&& Node->FigmaPrototype.Interactions[0].Actions[0].DestinationId == TEXT("1:2")
		&& Node->FigmaPrototype.Interactions[0].Actions[0].Navigation == TEXT("NAVIGATE")
		&& Node->FigmaPrototype.Interactions[0].Actions[0].Transition.Type == TEXT("SMART_ANIMATE")
		&& Node->FigmaPrototype.Interactions[0].Actions[0].Transition.Direction == TEXT("LEFT")
		&& Node->FigmaPrototype.Interactions[0].Actions[0].Transition.bHasDuration
		&& FMath::IsNearlyEqual(Node->FigmaPrototype.Interactions[0].Actions[0].Transition.Duration, 180.0f, 0.001f)
		&& Node->FigmaPrototype.Interactions[0].Actions[0].Transition.EasingType == TEXT("EASE_IN_AND_OUT")
		&& Node->FigmaPrototype.Interactions[0].Actions[0].Transition.bHasMatchLayers
		&& Node->FigmaPrototype.Interactions[0].Actions[0].Transition.bMatchLayers);
	TestTrue(TEXT("figma prototype flowStartingPoints typed"), Node->FigmaPrototype.FlowStartingPoints.Num() == 1
		&& Node->FigmaPrototype.FlowStartingPoints[0].NodeId == TEXT("0:2")
		&& Node->FigmaPrototype.FlowStartingPoints[0].Name == TEXT("Flow 1"));
	TestTrue(TEXT("figma doc prototypeStartNodeID typed"), Doc.FigmaDocument.PrototypeStartNodeId == TEXT("0:2"));
	TestTrue(TEXT("figma doc prototypeDevice typed"), Doc.FigmaDocument.PrototypeDeviceJson.Contains(TEXT("DESKTOP")));
	TestTrue(TEXT("figma doc flowStartingPoints typed"), Doc.FigmaDocument.FlowStartingPoints.Num() == 1
		&& Doc.FigmaDocument.FlowStartingPoints[0].NodeId == TEXT("0:2")
		&& Doc.FigmaDocument.FlowStartingPoints[0].Name == TEXT("Doc Flow"));
	TestTrue(TEXT("figma fill blendMode preserved"), PaintNode->Fill.BlendMode == TEXT("MULTIPLY"));
	TestTrue(TEXT("figma fill opacity preserved"), FMath::IsNearlyEqual(PaintNode->Fill.Opacity, 0.5f, 0.001f));
	TestTrue(TEXT("figma effect blendMode preserved"), PaintNode->Effects.Num() > 0
		&& PaintNode->Effects[0].BlendMode == TEXT("SCREEN")
		&& PaintNode->Effects[0].Shadow.BlendMode == TEXT("SCREEN"));
	TestTrue(TEXT("figma source node type preserved"), InstanceNode->Metadata.FindRef(TEXT("sourceNodeType")) == TEXT("INSTANCE"));
	TestTrue(TEXT("figma instance componentId preserved"), InstanceNode->Metadata.FindRef(TEXT("instanceComponentId")) == TEXT("12:34"));
	TestTrue(TEXT("figma instance ext flag"), InstanceNode->FigmaInstance.bIsInstance);
	TestTrue(TEXT("figma instance ext componentId"), InstanceNode->FigmaInstance.ComponentId == TEXT("12:34"));
	TestTrue(TEXT("figma instance ext componentSetId"), InstanceNode->FigmaInstance.ComponentSetId == TEXT("56:78"));
	TestTrue(TEXT("figma instance ext mainComponentKey"), InstanceNode->FigmaInstance.MainComponentKey == TEXT("component-key-1"));
	TestTrue(TEXT("figma instance ext componentProperties"), !InstanceNode->FigmaInstance.ComponentPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance ext variantProperties"), !InstanceNode->FigmaInstance.VariantPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance ext overrides"), !InstanceNode->FigmaInstance.OverridesJson.IsEmpty());
	TestTrue(TEXT("figma instance ext isExposedInstance"), InstanceNode->FigmaInstance.bHasIsExposedInstance && InstanceNode->FigmaInstance.bIsExposedInstance);
	TestTrue(TEXT("figma instance componentSetId preserved"), InstanceNode->Metadata.FindRef(TEXT("componentSetId")) == TEXT("56:78"));
	TestTrue(TEXT("figma instance mainComponentKey preserved"), InstanceNode->Metadata.FindRef(TEXT("mainComponentKey")) == TEXT("component-key-1"));
TestTrue(TEXT("figma instance componentProperties preserved"), InstanceNode->Metadata.Contains(TEXT("componentProperties")));
TestTrue(TEXT("figma instance variantProperties preserved"), InstanceNode->Metadata.Contains(TEXT("variantProperties")));
TestTrue(TEXT("figma instance overrides preserved"), InstanceNode->Metadata.Contains(TEXT("overrides")));
TestTrue(TEXT("figma instance isExposedInstance preserved"), InstanceNode->Metadata.FindRef(TEXT("isExposedInstance")) == TEXT("true"));

	const FUMGWidgetData ConvertedData = FDesign2UMGConverter::Convert(Doc);
	TestTrue(TEXT("figma document components catalog exported"), ConvertedData.FigmaDocument.ComponentsJson.Contains(TEXT("component-key-1")));
	TestTrue(TEXT("figma document componentSets catalog exported"), ConvertedData.FigmaDocument.ComponentSetsJson.Contains(TEXT("component-set-key-1")));

	const FUMGWidgetNode* ConvertedInstanceWidget = FindWidgetById(ConvertedData.RootWidget, TEXT("inst1"));
	TestNotNull(TEXT("figma instance widget exported to UMG tree"), ConvertedInstanceWidget);
	if (!ConvertedInstanceWidget)
	{
		return false;
	}

	TestTrue(TEXT("figma instance typed node data exported"), ConvertedInstanceWidget->Figma.IsInstance());
	TestEqual(TEXT("figma instance source node type exported"), ConvertedInstanceWidget->Figma.SourceNodeType, TEXT("INSTANCE"));
	TestEqual(TEXT("figma instance componentId exported"), ConvertedInstanceWidget->Figma.ComponentId, TEXT("12:34"));
	TestEqual(TEXT("figma instance componentSetId exported"), ConvertedInstanceWidget->Figma.ComponentSetId, TEXT("56:78"));
	TestEqual(TEXT("figma instance mainComponentKey exported"), ConvertedInstanceWidget->Figma.MainComponentKey, TEXT("component-key-1"));
	TestTrue(TEXT("figma instance componentProperties exported"), !ConvertedInstanceWidget->Figma.ComponentPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance variantProperties exported"), !ConvertedInstanceWidget->Figma.VariantPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance overrides exported"), !ConvertedInstanceWidget->Figma.OverridesJson.IsEmpty());
	TestTrue(TEXT("figma instance exposed flag exported"),
		ConvertedInstanceWidget->Figma.bHasIsExposedInstance && ConvertedInstanceWidget->Figma.bIsExposedInstance);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeNavigationConverterTest,
	"Design2UMG.Converter.FigmaPrototypeNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeNavigationConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"2:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"cta_button","name":"CTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE","transition":{"type":"DISSOLVE","duration":180,"easing":{"type":"EASE_IN_AND_OUT"}}}]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"PageTwo","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"page2_title","name":"Done","type":"TEXT","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":48,"width":120,"height":32},
						 "characters":"Done",
						 "style":{"fontFamily":"Inter","fontSize":24,"fontWeight":600,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}}
					]}
				]}
			]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma prototype navigation parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestTrue(TEXT("Prototype navigation plan exists"), Data.PrototypeNavigation.IsValid());
	TestEqual(TEXT("Prototype root uses WidgetSwitcher"), Data.RootWidget.Type, TEXT("WidgetSwitcher"));
	TestEqual(TEXT("Prototype start page resolves to second page"), Data.PrototypeNavigation.InitialPageIndex, 1);
	TestEqual(TEXT("Prototype page count"), Data.PrototypeNavigation.PageIds.Num(), 2);
	TestEqual(TEXT("Prototype click binding count"), Data.PrototypeNavigation.ClickBindings.Num(), 1);
	if (!Data.PrototypeNavigation.IsValid() || Data.PrototypeNavigation.ClickBindings.Num() != 1)
	{
		return false;
	}

	const FUMGPrototypeNavigationBinding& Binding = Data.PrototypeNavigation.ClickBindings[0];
	TestEqual(TEXT("Prototype trigger widget id"), Binding.TriggerWidgetId, TEXT("cta_button"));
	TestEqual(TEXT("Prototype trigger widget runtime name"), Binding.TriggerWidgetName, TEXT("cta_button"));
	TestTrue(TEXT("Prototype switcher widget name is no longer fixed hardcode"),
		Data.PrototypeNavigation.SwitcherWidgetName != TEXT("__d2u_prototype_switcher"));
	TestEqual(TEXT("Prototype trigger type recorded"), Binding.TriggerType, TEXT("ON_CLICK"));
	TestEqual(TEXT("Prototype target page id"), Binding.TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Prototype target page index"), Binding.TargetPageIndex, 1);
	TestEqual(TEXT("Prototype transition type preserved"), Binding.Transition.Type, TEXT("DISSOLVE"));
	TestTrue(TEXT("Prototype transition duration preserved"), FMath::IsNearlyEqual(Binding.Transition.DurationSeconds, 0.18f, 0.0001f));
	TestEqual(TEXT("Prototype transition easing preserved"), Binding.Transition.EasingType, TEXT("EASE_IN_AND_OUT"));
	TestTrue(TEXT("Prototype switcher active index exported via typed state"), Data.RootWidget.WidgetState.bHasActiveWidgetIndex);
	TestEqual(TEXT("Prototype switcher typed active index preserved"), Data.RootWidget.WidgetState.ActiveWidgetIndex, 1);

	const FUMGWidgetNode* TriggerWidget = FindWidgetById(Data.RootWidget, TEXT("cta_button"));
	TestTrue(TEXT("Prototype trigger widget exists"), TriggerWidget != nullptr);
	if (!TriggerWidget)
	{
		return false;
	}

	TestEqual(TEXT("Prototype trigger converts to transparent button wrapper"), TriggerWidget->Type, TEXT("Button"));
	TestTrue(TEXT("Prototype trigger keeps one visual child"), TriggerWidget->Children.Num() == 1);
	int32 LegacyActiveWidgetIndex = 0;
	TestFalse(TEXT("Prototype switcher no longer writes active index to legacy properties"),
		Data.RootWidget.Properties.TryGetInt(UMGPropertyKeys::ActiveWidgetIndex, LegacyActiveWidgetIndex));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeSpecialActionsConverterTest,
	"Design2UMG.Converter.FigmaPrototypeSpecialActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeSpecialActionsConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"1:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"overlay_button","name":"Overlay","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"OPEN_OVERLAY","transition":{"type":"DISSOLVE","duration":150}}]}],
						 "children":[]},
						{"id":"link_button","name":"Link","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":120,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.7,"b":0.5,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"OPEN_URL","url":"https://example.com/help"}]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"OverlayPage","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"back_button","name":"Back","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.8,"g":0.3,"b":0.2,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"BACK","transition":{"type":"DISSOLVE","duration":90}}]}],
						 "children":[]}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Figma prototype special actions parse succeeds"), FFigmaParser::ParseDocument(Json, Document));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Prototype special action plan exists"), Data.PrototypeNavigation.IsValid());
	TestEqual(TEXT("Prototype special action binding count"), Data.PrototypeNavigation.ClickBindings.Num(), 3);
	if (!Data.PrototypeNavigation.IsValid() || Data.PrototypeNavigation.ClickBindings.Num() != 3)
	{
		return false;
	}

	auto FindBindingByTrigger = [&](const TCHAR* TriggerId) -> const FUMGPrototypeNavigationBinding*
	{
		return Data.PrototypeNavigation.ClickBindings.FindByPredicate(
			[&](const FUMGPrototypeNavigationBinding& Binding)
			{
				return Binding.TriggerWidgetId == TriggerId;
			});
	};

	const FUMGPrototypeNavigationBinding* OverlayBinding = FindBindingByTrigger(TEXT("overlay_button"));
	const FUMGPrototypeNavigationBinding* BackBinding = FindBindingByTrigger(TEXT("back_button"));
	const FUMGPrototypeNavigationBinding* OpenLinkBinding = FindBindingByTrigger(TEXT("link_button"));
	TestNotNull(TEXT("Overlay binding exported"), OverlayBinding);
	TestNotNull(TEXT("Back binding exported"), BackBinding);
	TestNotNull(TEXT("Open link binding exported"), OpenLinkBinding);
	if (!OverlayBinding || !BackBinding || !OpenLinkBinding)
	{
		return false;
	}

	TestEqual(TEXT("Overlay action typed"), OverlayBinding->Action, EUMGPrototypeNavigationAction::Overlay);
	TestEqual(TEXT("Overlay trigger type preserved"), OverlayBinding->TriggerType, TEXT("ON_CLICK"));
	TestEqual(TEXT("Overlay target page id preserved"), OverlayBinding->TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Overlay target page index preserved"), OverlayBinding->TargetPageIndex, 1);
	TestEqual(TEXT("Overlay transition preserved"), OverlayBinding->Transition.Type, TEXT("DISSOLVE"));
	TestTrue(TEXT("Overlay duration preserved"), FMath::IsNearlyEqual(OverlayBinding->Transition.DurationSeconds, 0.15f, 0.0001f));
	TestTrue(TEXT("Prototype overlay host widget name is no longer fixed hardcode"),
		Data.PrototypeNavigation.OverlayHostWidgetName != TEXT("__d2u_prototype_overlay_host"));

	TestEqual(TEXT("Back action typed"), BackBinding->Action, EUMGPrototypeNavigationAction::Back);
	TestEqual(TEXT("Back trigger type preserved"), BackBinding->TriggerType, TEXT("ON_CLICK"));
	TestTrue(TEXT("Back no longer abuses target page id"), BackBinding->TargetPageId.IsEmpty());
	TestEqual(TEXT("Back no longer abuses placeholder target page index"), BackBinding->TargetPageIndex, INDEX_NONE);
	TestEqual(TEXT("Back transition preserved"), BackBinding->Transition.Type, TEXT("DISSOLVE"));

	TestEqual(TEXT("Open link action typed"), OpenLinkBinding->Action, EUMGPrototypeNavigationAction::OpenLink);
	TestEqual(TEXT("Open link trigger type preserved"), OpenLinkBinding->TriggerType, TEXT("ON_CLICK"));
	TestTrue(TEXT("Open link target page id stays empty"), OpenLinkBinding->TargetPageId.IsEmpty());
	TestEqual(TEXT("Open link target page index stays empty"), OpenLinkBinding->TargetPageIndex, INDEX_NONE);
	TestEqual(TEXT("Open link URL preserved"), OpenLinkBinding->ExternalUrl, TEXT("https://example.com/help"));

	const FUMGWidgetNode* OverlayTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("overlay_button"));
	const FUMGWidgetNode* BackTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("back_button"));
	const FUMGWidgetNode* OpenLinkTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("link_button"));
	TestEqual(TEXT("Overlay trigger wraps into button"), OverlayTriggerWidget ? OverlayTriggerWidget->Type : FString(), TEXT("Button"));
	TestEqual(TEXT("Back trigger wraps into button"), BackTriggerWidget ? BackTriggerWidget->Type : FString(), TEXT("Button"));
	TestEqual(TEXT("Open link trigger wraps into button"), OpenLinkTriggerWidget ? OpenLinkTriggerWidget->Type : FString(), TEXT("Button"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeHoverAndPressConverterTest,
	"Design2UMG.Converter.FigmaPrototypeHoverAndPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeHoverAndPressConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"1:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"hover_button","name":"HoverCTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_HOVER"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]}],
						 "children":[]},
						{"id":"drag_button","name":"DragCTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":120,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.7,"b":0.5,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_DRAG"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"PageTwo","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"press_button","name":"PressBack","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.8,"g":0.3,"b":0.2,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_PRESS"},"actions":[{"type":"BACK"}]}],
						 "children":[]}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Figma prototype hover/press parse succeeds"), FFigmaParser::ParseDocument(Json, Document));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Prototype hover/press plan exists"), Data.PrototypeNavigation.IsValid());
	TestEqual(TEXT("Prototype hover/press binding count"), Data.PrototypeNavigation.ClickBindings.Num(), 3);
	if (!Data.PrototypeNavigation.IsValid() || Data.PrototypeNavigation.ClickBindings.Num() != 3)
	{
		return false;
	}

	auto FindBindingByTrigger = [&](const TCHAR* TriggerId) -> const FUMGPrototypeNavigationBinding*
	{
		return Data.PrototypeNavigation.ClickBindings.FindByPredicate(
			[&](const FUMGPrototypeNavigationBinding& Binding)
			{
				return Binding.TriggerWidgetId == TriggerId;
			});
	};

	const FUMGPrototypeNavigationBinding* HoverBinding = FindBindingByTrigger(TEXT("hover_button"));
	const FUMGPrototypeNavigationBinding* DragBinding = FindBindingByTrigger(TEXT("drag_button"));
	const FUMGPrototypeNavigationBinding* PressBinding = FindBindingByTrigger(TEXT("press_button"));
	TestNotNull(TEXT("Hover binding exported"), HoverBinding);
	TestNotNull(TEXT("Drag binding exported"), DragBinding);
	TestNotNull(TEXT("Press binding exported"), PressBinding);
	if (!HoverBinding || !DragBinding || !PressBinding)
	{
		return false;
	}

	TestEqual(TEXT("Hover trigger type preserved"), HoverBinding->TriggerType, TEXT("ON_HOVER"));
	TestEqual(TEXT("Hover action typed"), HoverBinding->Action, EUMGPrototypeNavigationAction::Navigate);
	TestEqual(TEXT("Hover target page id preserved"), HoverBinding->TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Hover target page index preserved"), HoverBinding->TargetPageIndex, 1);

	TestEqual(TEXT("Drag trigger type preserved"), DragBinding->TriggerType, TEXT("ON_DRAG"));
	TestEqual(TEXT("Drag action typed"), DragBinding->Action, EUMGPrototypeNavigationAction::Navigate);
	TestEqual(TEXT("Drag target page id preserved"), DragBinding->TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Drag target page index preserved"), DragBinding->TargetPageIndex, 1);
	TestTrue(TEXT("Drag binding exports executable steps"), DragBinding->Steps.Num() > 0);

	TestEqual(TEXT("Press trigger type preserved"), PressBinding->TriggerType, TEXT("ON_PRESS"));
	TestEqual(TEXT("Press action typed"), PressBinding->Action, EUMGPrototypeNavigationAction::Back);
	TestTrue(TEXT("Press back keeps target page empty"), PressBinding->TargetPageId.IsEmpty());
	TestEqual(TEXT("Press back keeps target page index empty"), PressBinding->TargetPageIndex, INDEX_NONE);

	TestFalse(TEXT("Drag trigger no longer warns as unsupported"),
		Data.Warnings.ContainsByPredicate(
			[](const FString& Warning)
			{
				return Warning.Contains(TEXT("ON_DRAG")) && Warning.Contains(TEXT("drag_button"));
			}));

	const FUMGWidgetNode* HoverTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("hover_button"));
	const FUMGWidgetNode* PressTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("press_button"));
	const FUMGWidgetNode* DragTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("drag_button"));
	TestEqual(TEXT("Hover trigger wraps into button"), HoverTriggerWidget ? HoverTriggerWidget->Type : FString(), TEXT("Button"));
	TestEqual(TEXT("Press trigger wraps into button"), PressTriggerWidget ? PressTriggerWidget->Type : FString(), TEXT("Button"));
	TestEqual(TEXT("Drag trigger now wraps into button"), DragTriggerWidget ? DragTriggerWidget->Type : FString(), TEXT("Button"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeKeyboardAndConditionalConverterTest,
	"Design2UMG.Converter.FigmaPrototypeKeyboardAndConditional",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeKeyboardAndConditionalConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"1:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"key_down_hotspot","name":"KeyDownHotspot","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":40,"width":100,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.3,"g":0.3,"b":0.8,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_KEY_DOWN","keyCodes":["ENTER"]},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]}],
						 "children":[]},
						{"id":"conditional_button","name":"ConditionalCTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":104,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.8,"g":0.4,"b":0.1,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[
							{"type":"SET_VARIABLE","variableId":"var:isPrimary","variableValue":{"resolvedType":"BOOLEAN","value":true}},
							{"type":"CONDITIONAL","conditionalBlocks":[
								{"condition":{"type":"VARIABLE_ALIAS","id":"var:isPrimary"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]},
								{"condition":{"resolvedType":"BOOLEAN","value":true},"actions":[{"type":"NODE","destinationId":"3:1","navigation":"NAVIGATE"}]}
							]}
						 ]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"PageTwo","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"key_up_hotspot","name":"KeyUpHotspot","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":40,"width":100,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.7,"b":0.4,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_KEY_UP","keyCodes":["ESCAPE"]},"actions":[{"type":"BACK"}]}],
						 "children":[]}
					]},
					{"id":"3:1","name":"PageThree","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"page_three_title","name":"Fallback","type":"TEXT","visible":true,
						 "absoluteBoundingBox":{"x":24,"y":32,"width":120,"height":28},
						 "characters":"Fallback","style":{"fontSize":20}}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Figma prototype keyboard/conditional parse succeeds"), FFigmaParser::ParseDocument(Json, Document));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Prototype keyboard/conditional plan exists"), Data.PrototypeNavigation.IsValid());
	TestEqual(TEXT("Prototype keyboard/conditional binding count"), Data.PrototypeNavigation.ClickBindings.Num(), 3);
	if (!Data.PrototypeNavigation.IsValid() || Data.PrototypeNavigation.ClickBindings.Num() != 3)
	{
		return false;
	}

	auto FindBindingByTrigger = [&](const TCHAR* TriggerId) -> const FUMGPrototypeNavigationBinding*
	{
		return Data.PrototypeNavigation.ClickBindings.FindByPredicate(
			[&](const FUMGPrototypeNavigationBinding& Binding)
			{
				return Binding.TriggerWidgetId == TriggerId;
			});
	};

	const FUMGPrototypeNavigationBinding* KeyDownBinding = FindBindingByTrigger(TEXT("key_down_hotspot"));
	const FUMGPrototypeNavigationBinding* ConditionalBinding = FindBindingByTrigger(TEXT("conditional_button"));
	const FUMGPrototypeNavigationBinding* KeyUpBinding = FindBindingByTrigger(TEXT("key_up_hotspot"));
	TestNotNull(TEXT("Key-down binding exported"), KeyDownBinding);
	TestNotNull(TEXT("Conditional binding exported"), ConditionalBinding);
	TestNotNull(TEXT("Key-up binding exported"), KeyUpBinding);
	if (!KeyDownBinding || !ConditionalBinding || !KeyUpBinding)
	{
		return false;
	}

	TestEqual(TEXT("Key-down trigger type preserved"), KeyDownBinding->TriggerType, TEXT("ON_KEY_DOWN"));
	TestTrue(TEXT("Key-down trigger exports key code"), KeyDownBinding->TriggerKeyCodes.Num() == 1 && KeyDownBinding->TriggerKeyCodes[0] == TEXT("ENTER"));
	TestEqual(TEXT("Key-down action summary preserved"), KeyDownBinding->Action, EUMGPrototypeNavigationAction::Navigate);
	TestEqual(TEXT("Key-down target page id preserved"), KeyDownBinding->TargetPageId, TEXT("2:1"));

	TestEqual(TEXT("Key-up trigger type preserved"), KeyUpBinding->TriggerType, TEXT("ON_KEY_UP"));
	TestTrue(TEXT("Key-up trigger exports key code"), KeyUpBinding->TriggerKeyCodes.Num() == 1 && KeyUpBinding->TriggerKeyCodes[0] == TEXT("ESCAPE"));
	TestEqual(TEXT("Key-up action summary preserved"), KeyUpBinding->Action, EUMGPrototypeNavigationAction::Back);

	TestEqual(TEXT("Conditional trigger keeps click type"), ConditionalBinding->TriggerType, TEXT("ON_CLICK"));
	TestEqual(TEXT("Conditional summary resolves to first terminal navigation"), ConditionalBinding->Action, EUMGPrototypeNavigationAction::Navigate);
	TestEqual(TEXT("Conditional summary target page id resolves first true branch"), ConditionalBinding->TargetPageId, TEXT("2:1"));
	TestTrue(TEXT("Conditional binding exports step program"), ConditionalBinding->Steps.Num() >= 6);
	if (ConditionalBinding->Steps.Num() < 6)
	{
		return false;
	}

	TestEqual(TEXT("Conditional first step is set-variable"), ConditionalBinding->Steps[0].Kind, EUMGPrototypeNavigationStepKind::SetVariable);
	TestEqual(TEXT("Conditional second step is condition jump"), ConditionalBinding->Steps[1].Kind, EUMGPrototypeNavigationStepKind::ConditionJump);
	TestEqual(TEXT("Conditional first branch navigates to page two"), ConditionalBinding->Steps[2].TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Conditional final branch navigates to page three"), ConditionalBinding->Steps[5].TargetPageId, TEXT("3:1"));
	TestTrue(TEXT("Conditional step graph has no unsupported warnings"),
		!Data.Warnings.ContainsByPredicate(
			[](const FString& Warning)
			{
				return Warning.Contains(TEXT("conditional_button"))
					&& (Warning.Contains(TEXT("CONDITIONAL")) || Warning.Contains(TEXT("SET_VARIABLE")));
			}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaOverflowDirectionMapsToScrollBoxTest,
	"Design2UMG.Converter.FigmaOverflowDirectionMapsToScrollBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaOverflowDirectionMapsToScrollBoxTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"scroll_frame","name":"ScrollFrame","type":"FRAME","visible":true,
					 "layoutMode":"VERTICAL","itemSpacing":12,"overflowDirection":"VERTICAL_SCROLLING",
					 "absoluteBoundingBox":{"x":0,"y":0,"width":320,"height":240},
					 "children":[
						{"id":"row1","name":"Row1","type":"RECTANGLE","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":320,"height":80},"fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.3,"b":0.4,"a":1}}]},
						{"id":"row2","name":"Row2","type":"RECTANGLE","visible":true,"absoluteBoundingBox":{"x":0,"y":92,"width":320,"height":80},"fills":[{"type":"SOLID","visible":true,"color":{"r":0.4,"g":0.3,"b":0.2,"a":1}}]}
					 ]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse scrollable figma fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	const FUMGWidgetNode* ScrollFrame = FindWidgetById(Data.RootWidget, TEXT("scroll_frame"));
	TestNotNull(TEXT("Scrollable frame converts"), ScrollFrame);
	if (!ScrollFrame)
	{
		return false;
	}

	TestEqual(TEXT("overflowDirection frame maps to ScrollBox"), ScrollFrame->Type, TEXT("ScrollBox"));
	TestEqual(TEXT("ScrollBox orientation exported"), GetWidgetProperty(*ScrollFrame, TEXT("Orientation")), TEXT("Vertical"));
	TestEqual(TEXT("ScrollBox scrollbar visibility exported"), GetWidgetProperty(*ScrollFrame, TEXT("ScrollBarVisibility")), TEXT("Hidden"));
	TestTrue(TEXT("ScrollBox keeps both children"), ScrollFrame->Children.Num() == 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaGridSlotSemanticsConverterTest,
	"Design2UMG.Converter.FigmaGridSlotSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaGridSlotSemanticsConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"grid_frame","name":"grid_frame","type":"FRAME","visible":true,
					 "layoutMode":"GRID","gridRowCount":2,"gridColumnCount":2,"gridRowGap":24,"gridColumnGap":12,
					 "gridChildHorizontalAlign":"CENTER","gridChildVerticalAlign":"MAX",
					 "absoluteBoundingBox":{"x":0,"y":0,"width":320,"height":240},
					 "children":[
						{"id":"head_card","name":"head_card","type":"RECTANGLE","visible":true,
						 "gridRowAnchorIndex":0,"gridColumnAnchorIndex":0,
						 "absoluteBoundingBox":{"x":0,"y":0,"width":100,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.3,"b":0.4,"a":1}}]},
						{"id":"tail_card","name":"tail_card","type":"RECTANGLE","visible":true,
						 "gridRowAnchorIndex":0,"gridColumnAnchorIndex":1,
						 "absoluteBoundingBox":{"x":116,"y":0,"width":100,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.4,"g":0.3,"b":0.2,"a":1}}]},
						{"id":"hero_card","name":"hero_card","type":"RECTANGLE","visible":true,
						 "gridRowAnchorIndex":1,"gridColumnAnchorIndex":0,"gridColumnSpan":2,
						 "absoluteBoundingBox":{"x":0,"y":64,"width":216,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.6,"b":0.5,"a":1}}]}
					 ]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse figma grid slot semantics fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);

	const FUMGWidgetNode* GridFrame = FindWidgetById(Data.RootWidget, TEXT("grid_frame"));
	const FUMGWidgetNode* TailCard = FindWidgetById(Data.RootWidget, TEXT("tail_card_sz"));
	const FUMGWidgetNode* HeroCard = FindWidgetById(Data.RootWidget, TEXT("hero_card_sz"));
	TestNotNull(TEXT("Grid frame converts"), GridFrame);
	TestNotNull(TEXT("Tail card wrapper converts"), TailCard);
	TestNotNull(TEXT("Hero card wrapper converts"), HeroCard);
	if (!GridFrame || !TailCard || !HeroCard)
	{
		return false;
	}

	TestEqual(TEXT("Grid frame upgrades to GridPanel when span is present"), GridFrame->Type, TEXT("GridPanel"));

	TestEqual(TEXT("Tail card row exported"), TailCard->Slot.Row, 0);
	TestEqual(TEXT("Tail card column exported"), TailCard->Slot.Column, 1);
	TestEqual(TEXT("Tail card horizontal align exported"), TailCard->Slot.HorizontalAlignment, TEXT("Center"));
	TestEqual(TEXT("Tail card vertical align exported"), TailCard->Slot.VerticalAlignment, TEXT("Bottom"));
	TestTrue(TEXT("Tail card gets half horizontal gap on the inner edge"), FMath::IsNearlyEqual(TailCard->Slot.Padding.Left, 6.0f, 0.01f));
	TestTrue(TEXT("Tail card keeps zero right padding on the outer edge"), FMath::IsNearlyEqual(TailCard->Slot.Padding.Right, 0.0f, 0.01f));
	TestTrue(TEXT("Tail card gets half row gap before the next row"), FMath::IsNearlyEqual(TailCard->Slot.Padding.Bottom, 12.0f, 0.01f));

	TestEqual(TEXT("Hero card row exported"), HeroCard->Slot.Row, 1);
	TestEqual(TEXT("Hero card column exported"), HeroCard->Slot.Column, 0);
	TestEqual(TEXT("Hero card column span exported"), HeroCard->Slot.ColumnSpan, 2);
	TestEqual(TEXT("Hero card horizontal align exported"), HeroCard->Slot.HorizontalAlignment, TEXT("Center"));
	TestEqual(TEXT("Hero card vertical align exported"), HeroCard->Slot.VerticalAlignment, TEXT("Bottom"));
	TestTrue(TEXT("Hero card gets zero left padding on the outer edge"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Left, 0.0f, 0.01f));
	TestTrue(TEXT("Hero card gets half row gap above the second row"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Top, 12.0f, 0.01f));
	TestTrue(TEXT("Hero card keeps zero right padding when spanning to the edge"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Right, 0.0f, 0.01f));
	TestTrue(TEXT("Hero card keeps zero bottom padding on the outer edge"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Bottom, 0.0f, 0.01f));
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
	TestEqual(TEXT("TRANSFORM_GROUP source type preserved"), TransformGroup->Metadata.FindRef(TEXT("sourceNodeType")), TEXT("TRANSFORM_GROUP"));
	TestEqual(TEXT("TRANSFORM_GROUP keeps child hierarchy"), TransformGroup->Children.Num(), 1);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	TestEqual(TEXT("TEXT_PATH source type preserved"), TextPathNode->Metadata.FindRef(TEXT("sourceNodeType")), TEXT("TEXT_PATH"));
	TestEqual(TEXT("TEXT_PATH keeps text content"), TextPathNode->Content, TEXT("Curved Title"));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	TestEqual(TEXT("Skipped designer node count recorded"), Doc.FigmaDiagnostics.SkippedDesignerNodes.Num(), 2);
	TestEqual(TEXT("Skipped designer node count metadata recorded"), Doc.Metadata.FindRef(TEXT("figma.skippedDesignerNodeCount")), TEXT("2"));
	TestTrue(TEXT("Skipped designer node report metadata recorded"), Doc.Metadata.FindRef(TEXT("figma.skippedDesignerNodes")).Contains(TEXT("slice1")));
	TestTrue(TEXT("Skipped designer node report includes skip reason"), Doc.Metadata.FindRef(TEXT("figma.skippedDesignerNodes")).Contains(TEXT("figjam_only")));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	FScopedLuminosityFitOpacityCap LuminosityOpacityCapScope(0.15f);
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
	TestEqual(TEXT("Luminosity placeholder keeps both fills"), Node->Fills.Num(), 2);
	TestEqual(TEXT("Base fill remains color"), Node->Fills[0].Type, EDesignFillType::Color);
	TestEqual(TEXT("Overlay fill remains image"), Node->Fills[1].Type, EDesignFillType::Image);
	TestEqual(TEXT("Overlay image mode preserved"), Node->Fills[1].Image.Mode, EDesignImageMode::Fit);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Overlay = FindWidgetById(Data.RootWidget, TEXT("node_lum"));
	TestTrue(TEXT("Converted widget found"), Overlay != nullptr);
	if (!Overlay)
	{
		return false;
	}

	TestEqual(TEXT("Luminosity image converts to multi-fill overlay"), Overlay->Type, TEXT("Overlay"));
	TestTrue(TEXT("Overlay has at least base + image layers"), Overlay->Children.Num() >= 2);
	if (Overlay->Children.Num() < 2)
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
	FScopedLuminosityFitOpacityCap LuminosityOpacityCapScope(0.15f);
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

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	FScopedLuminosityFitOpacityCap LuminosityOpacityCapScope(0.15f);
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

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	FScopedLuminosityFitOpacityCap LuminosityOpacityCapScope(0.15f);
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

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	TestTrue(TEXT("Vector raster fallback metadata captured"), EllipseNode->Metadata.FindRef(TEXT("figma.vectorRasterFallback")) == TEXT("vec_grad_1"));

	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Doc, ImageRefs);
	TestTrue(TEXT("CollectImageRefs includes vector fallback"), ImageRefs.Contains(TEXT("vec_grad_1")));

	TMap<FString, FString> ImageMap;
	ImageMap.Add(TEXT("vec_grad_1"), TEXT("/Game/Transient/T_vec_grad_1.T_vec_grad_1"));
	FFigmaParser::ApplyImageUrls(Doc, ImageMap);
	TestTrue(TEXT("ApplyImageUrls remaps vector fallback"), EllipseNode->Metadata.FindRef(TEXT("figma.vectorRasterFallback")) == TEXT("/Game/Transient/T_vec_grad_1.T_vec_grad_1"));

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
						"design2umgRenderFallbackOffsetX": "-8.5",
						"design2umgRenderFallbackOffsetY": "3.25",
						"design2umgRenderFallbackWidth": "176",
						"design2umgRenderFallbackHeight": "104",
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
	TestEqual(TEXT("Render fallback image ref preserved"), RenderNode->Metadata.FindRef(TEXT("figma.renderFallbackImageRef")), TEXT("render_202_5356"));
	TestEqual(TEXT("Render fallback scope preserved"), RenderNode->Metadata.FindRef(TEXT("figma.renderFallbackScope")), TEXT("full"));
	TestEqual(TEXT("Render fallback source preserved"), RenderNode->Metadata.FindRef(TEXT("figma.renderFallbackSource")), TEXT("figma_render_png"));
	TestEqual(TEXT("Render fallback reason preserved"), RenderNode->Metadata.FindRef(TEXT("figma.renderFallbackReason")), TEXT("image_filters+image_blend_luminosity"));
	TestEqual(TEXT("Render fallback offsetX preserved"), RenderNode->Metadata.FindRef(TEXT("figma.renderFallbackOffsetX")), TEXT("-8.5"));
	TestEqual(TEXT("Render fallback offsetY preserved"), RenderNode->Metadata.FindRef(TEXT("figma.renderFallbackOffsetY")), TEXT("3.25"));
	TestEqual(TEXT("Render fallback width preserved"), RenderNode->Metadata.FindRef(TEXT("figma.renderFallbackWidth")), TEXT("176"));
	TestEqual(TEXT("Render fallback height preserved"), RenderNode->Metadata.FindRef(TEXT("figma.renderFallbackHeight")), TEXT("104"));

	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Doc, ImageRefs);
	TestTrue(TEXT("CollectImageRefs includes original image"), ImageRefs.Contains(TEXT("img_orig")));
	TestTrue(TEXT("CollectImageRefs includes render fallback image"), ImageRefs.Contains(TEXT("render_202_5356")));

	TMap<FString, FString> ImageMap;
	ImageMap.Add(TEXT("img_orig"), TEXT("/Game/Transient/T_img_orig.T_img_orig"));
	ImageMap.Add(TEXT("render_202_5356"), TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356"));
	FFigmaParser::ApplyImageUrls(Doc, ImageMap);
	TestEqual(TEXT("ApplyImageUrls remaps render fallback image"), RenderNode->Metadata.FindRef(TEXT("figma.renderFallbackImageRef")), TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356"));
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
	Root->Layout = EDesignLayout::None;
	Root->bHasX = true;
	Root->bHasY = true;
	Root->X = 0.0f;
	Root->Y = 0.0f;
	Root->Width = FDesignSize::Fixed(100.0f);
	Root->Height = FDesignSize::Fixed(80.0f);

	TSharedPtr<FDesignNode> Leaf = MakeShared<FDesignNode>();
	Leaf->Id = TEXT("render_leaf");
	Leaf->Name = TEXT("Image Tile");
	Leaf->Type = EDesignNodeType::Frame;
	Leaf->Layout = EDesignLayout::None;
	Leaf->bHasX = true;
	Leaf->bHasY = true;
	Leaf->X = 10.0f;
	Leaf->Y = 12.0f;
	Leaf->Width = FDesignSize::Fixed(26.0f);
	Leaf->Height = FDesignSize::Fixed(26.0f);
	Leaf->Fill.Type = EDesignFillType::Image;
	Leaf->Fill.bEnabled = true;
	Leaf->Fill.Image.Url = TEXT("img_avatar");
	Leaf->Fill.Image.Mode = EDesignImageMode::Fit;
	Leaf->Metadata.Add(TEXT("figma.renderFallbackImageRef"), TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356"));
	Leaf->Metadata.Add(TEXT("figma.renderFallbackScope"), TEXT("full"));
	Leaf->Metadata.Add(TEXT("figma.renderFallbackSource"), TEXT("figma_render_png"));
	Leaf->Metadata.Add(TEXT("figma.renderFallbackReason"), TEXT("foreground_blur"));
	Leaf->Metadata.Add(TEXT("figma.renderFallbackOffsetX"), TEXT("-21.7463"));
	Leaf->Metadata.Add(TEXT("figma.renderFallbackOffsetY"), TEXT("-21.7463"));
	Leaf->Metadata.Add(TEXT("figma.renderFallbackWidth"), TEXT("69.4925"));
	Leaf->Metadata.Add(TEXT("figma.renderFallbackHeight"), TEXT("69.4925"));
	Root->Children.Add(Leaf);
	Doc.Children.Add(Root);

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	TestFalse(TEXT("Leaf render fallback width override should not stay in legacy properties"), HasLegacyWidgetProperty(*Widget, TEXT("WidthOverride")));
	TestFalse(TEXT("Leaf render fallback height override should not stay in legacy properties"), HasLegacyWidgetProperty(*Widget, TEXT("HeightOverride")));
	TestFalse(TEXT("Leaf render fallback exposes typed width override"), WidthOverrideValue.IsEmpty());
	TestFalse(TEXT("Leaf render fallback exposes typed height override"), HeightOverrideValue.IsEmpty());
	TestTrue(TEXT("Leaf render fallback keeps render-box width"), FMath::IsNearlyEqual(WidthOverride, 69.4925f, 0.001f));
	TestTrue(TEXT("Leaf render fallback keeps render-box height"), FMath::IsNearlyEqual(HeightOverride, 69.4925f, 0.001f));
	TestTrue(TEXT("Leaf render fallback shifts X by render offset"), FMath::IsNearlyEqual(Widget->Bounds.X, -11.7463f, 0.001f));
	TestTrue(TEXT("Leaf render fallback shifts Y by render offset"), FMath::IsNearlyEqual(Widget->Bounds.Y, -9.7463f, 0.001f));
	TestTrue(TEXT("Leaf render fallback widget width uses render box"), FMath::IsNearlyEqual(Widget->Bounds.W, 69.4925f, 0.001f));
	TestTrue(TEXT("Leaf render fallback widget height uses render box"), FMath::IsNearlyEqual(Widget->Bounds.H, 69.4925f, 0.001f));
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
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
