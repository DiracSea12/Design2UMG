// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "Design2UMGSettings.h"
#include "Design2UMGTestUtils.h"
#include "FontResolver.h"
#include "UMGLayoutProjection.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGPropertyKeys.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetUtils.h"
#include "FigmaParser.h"
#include "DesignPathRasterizer.h"
#include "ProceduralShapeGenerator.h"
#include "TextMeasurement.h"
#include "TextMeasurementTestTrace.h"
#include "Design2UMGLog.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/EditableText.h"
#include "Components/RetainerBox.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
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
	struct FLuminosityOpacityCapScope
	{
		explicit FLuminosityOpacityCapScope(const float NewValue, const bool bEnableOverride = true)
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

		~FLuminosityOpacityCapScope()
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropertyBagDynamicColorKeyTest,
	"Design2UMG.PropertyBag.DynamicColorKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPropertyBagDynamicColorKeyTest::RunTest(const FString& Parameters)
{
	FUMGPropertyBag Properties;
	const FLinearColor ExpectedColor(0.1f, 0.2f, 0.3f, 0.4f);
	const FString SerializedColor = Design2UMGWidgetNodeAccess::SerializeWidgetColor(ExpectedColor);
	Properties.Add(TEXT("BoxShadow_0_Color"), SerializedColor);

	FLinearColor ActualColor = FLinearColor::Transparent;
	TestTrue(TEXT("Dynamic box shadow color key is readable as color"), Properties.TryGetColor(TEXT("BoxShadow_0_Color"), ActualColor));
	TestTrue(TEXT("Dynamic box shadow color follows serialized color value"), ActualColor.Equals(FUMGValueParser::ParseColor(SerializedColor), 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRenderTransformScaleJsonContractTest,
	"Design2UMG.Json.RenderTransformScaleContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRenderTransformScaleJsonContractTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"RootWidget": {
			"Type": "TextBlock",
			"Name": "ScaledText",
			"Bounds": { "X": 0, "Y": 0, "Width": 100, "Height": 20 },
			"Render": {
				"RenderTransformScale": "1.250000,0.750000"
			}
		}
	})");

	FUMGWidgetData Parsed;
	FString ParseError;
	TestTrue(TEXT("UMGJsonParser parses RenderTransformScale-only payload"), FUMGJsonParser::ParseJson(Json, Parsed, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddError(ParseError);
	}
	TestFalse(TEXT("Root widget type parsed"), Parsed.RootWidget.Type.IsEmpty());
	if (Parsed.RootWidget.Type.IsEmpty())
	{
		return false;
	}

	const FUMGWidgetNode& Root = Parsed.RootWidget;
	TestTrue(TEXT("RenderTransformScale is marked present"), Root.Render.bHasRenderTransformScale);
	TestTrue(TEXT("RenderTransformScale X preserved"), FMath::IsNearlyEqual(Root.Render.RenderTransformScale.X, 1.25, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("RenderTransformScale Y preserved"), FMath::IsNearlyEqual(Root.Render.RenderTransformScale.Y, 0.75, KINDA_SMALL_NUMBER));

	const FString RoundTripJson = FUMGJsonSerializer::Serialize(Parsed);
	TestTrue(TEXT("Serializer keeps RenderTransformScale"), RoundTripJson.Contains(TEXT("\"RenderTransformScale\"")));
	TestFalse(TEXT("Serializer no longer emits RenderScale alias"), RoundTripJson.Contains(TEXT("\"RenderScale\"")));
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
	// Frame node.
	const TSharedPtr<FDesignNode>& Screen = Doc.Children[0];
	TestTrue(TEXT("Screen node valid"), Screen.IsValid());
	if (!Screen.IsValid())
		return false;
	TestEqual(TEXT("Screen type"), Screen->Type, EDesignNodeType::Frame);
	TestEqual(TEXT("Screen name"), Screen->Name, TEXT("MainScreen"));
	TestTrue(TEXT("Screen width fixed"), Screen->Transform.Width.IsFixed());
	TestEqual(TEXT("Screen width value"), Screen->Transform.Width.Value, 1920.0f);
	TestEqual(TEXT("Screen layout"), Screen->LayoutData.Layout, EDesignLayout::Vertical);
	TestEqual(TEXT("Screen gap"), Screen->LayoutData.ExplicitGap.Get(0.0f), 16.0f);
	TestEqual(TEXT("Screen children count"), Screen->Children.Num(), 3);
	if (Screen->Children.Num() < 3)
		return false;
	// Text node.
	const TSharedPtr<FDesignNode>& Title = Screen->Children[0];
	TestEqual(TEXT("Title type"), Title->Type, EDesignNodeType::Text);
	TestEqual(TEXT("Title content"), Title->Text.Content, TEXT("Hello World"));
	TestEqual(TEXT("Title fontSize"), Title->Text.FontSize, 24.0f);
	TestEqual(TEXT("Title fontWeight"), Title->Text.FontWeight, TEXT("bold"));
	// Ellipse node.
	const TSharedPtr<FDesignNode>& Ellipse = Screen->Children[1];
	TestEqual(TEXT("Ellipse type"), Ellipse->Type, EDesignNodeType::Ellipse);
	// Rectangle node.
	const TSharedPtr<FDesignNode>& Rect = Screen->Children[2];
	TestEqual(TEXT("Rect type"), Rect->Type, EDesignNodeType::Rectangle);
	TestTrue(TEXT("Rect has corner radius"), Rect->Visual.CornerRadius.HasRadius());
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
	// The root widget should be present.
	TestFalse(TEXT("Root type not empty"), Data.RootWidget.Type.IsEmpty());
	TestTrue(TEXT("Root has children"), Data.RootWidget.Children.Num() > 0);
	// Ellipses are approximated with corner radii; a perfect circle should not warn.
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
	"Design2UMG.LayoutData.Layout.NineGridAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLayoutCalculatorTest::RunTest(const FString& Parameters)
{
	FLayoutContext Ctx;
	Ctx.DesignSize = FIntPoint(1920, 1080);
	// Case 1: top-left element -> anchor (0,0)
	{
		FDesignPixelBounds B;
		B.X = 50.0f / 1920.0f; B.Y = 30.0f / 1080.0f;
		B.W = 200.0f / 1920.0f; B.H = 100.0f / 1080.0f;
		FUMGSlotData Slot = LayoutCalculator::CalculateSlot(B, Ctx);
		TestEqual(TEXT("TopLeft anchor MinX"), Slot.Anchors.Min.X, 0.0f);
		TestEqual(TEXT("TopLeft anchor MinY"), Slot.Anchors.Min.Y, 0.0f);
	}
	// Case 2: centered element -> anchor (0.5, 0.5)
	{
		FDesignPixelBounds B;
		B.W = 300.0f / 1920.0f; B.H = 200.0f / 1080.0f;
		B.X = (1.0f - B.W) / 2.0f;
		B.Y = (1.0f - B.H) / 2.0f;
		FUMGSlotData Slot = LayoutCalculator::CalculateSlot(B, Ctx);
		TestEqual(TEXT("Center anchor MinX"), Slot.Anchors.Min.X, 0.5f);
		TestEqual(TEXT("Center anchor MinY"), Slot.Anchors.Min.Y, 0.5f);
	}
	// Case 3: bottom-right element -> anchor (1,1)
	{
		FDesignPixelBounds B;
		B.W = 200.0f / 1920.0f; B.H = 100.0f / 1080.0f;
		B.X = 1.0f - B.W - 30.0f / 1920.0f;
		B.Y = 1.0f - B.H - 20.0f / 1080.0f;
		FUMGSlotData Slot = LayoutCalculator::CalculateSlot(B, Ctx);
		TestEqual(TEXT("BottomRight anchor MinX"), Slot.Anchors.Min.X, 1.0f);
		TestEqual(TEXT("BottomRight anchor MinY"), Slot.Anchors.Min.Y, 1.0f);
	}
	// Case 4: width exceeds 60% of the canvas -> stretch anchor (0,0)-(1,0)
	{
		FDesignPixelBounds B;
		B.X = 50.0f / 1920.0f; B.Y = 50.0f / 1080.0f;
		B.W = 1800.0f / 1920.0f; B.H = 100.0f / 1080.0f;  // ~93.75% width
		FUMGSlotData Slot = LayoutCalculator::CalculateSlot(B, Ctx);
		TestTrue(TEXT("Stretch anchor: MinX < MaxX"),
			Slot.Anchors.Min.X < Slot.Anchors.Max.X);
	}
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
	"Design2UMG.LayoutData.Layout.HorizontalParentRefChildCrossFill",
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
	"Design2UMG.LayoutData.Layout.FlowParentChildBoundsLocalized",
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
	"Design2UMG.LayoutData.Layout.CanvasFillContainerFallbackSize",
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
	TestTrue(TEXT("Root has parsed X"), Root->Transform.HasExplicitX());
	TestTrue(TEXT("Root has parsed Y"), Root->Transform.HasExplicitY());
	TestEqual(TEXT("Root gap parsed from string"), Root->LayoutData.ExplicitGap.Get(0.0f), 8.0f);
	TestEqual(TEXT("Root opacity parsed from string"), Root->Transform.Opacity, 0.75f);
	TestEqual(TEXT("Root padding top parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Top, 1.0f);
	TestEqual(TEXT("Root padding right parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Right, 2.0f);
	TestEqual(TEXT("Root padding bottom parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Bottom, 3.0f);
	TestEqual(TEXT("Root padding left parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Left, 4.0f);
	TestTrue(TEXT("Root cornerRadius parsed as per-corner"), !Root->Visual.CornerRadius.bUniform);
	TestEqual(TEXT("Root cornerRadius TL parsed from string"), Root->Visual.CornerRadius.TopLeft, 5.0f);
	TestEqual(TEXT("Root cornerRadius TR parsed from string"), Root->Visual.CornerRadius.TopRight, 6.0f);
	TestEqual(TEXT("Root cornerRadius BR parsed from string"), Root->Visual.CornerRadius.BottomRight, 7.0f);
	TestEqual(TEXT("Root cornerRadius BL parsed from string"), Root->Visual.CornerRadius.BottomLeft, 8.0f);
	TestTrue(TEXT("Root stroke thickness parsed as uniform"), Root->Visual.Stroke.Thickness.bUniform);
	TestEqual(TEXT("Root stroke thickness parsed from string"), Root->Visual.Stroke.Thickness.Uniform, 2.5f);
	TestEqual(TEXT("Root image transform A parsed from string"), Root->Visual.Fill.Image.Transform.A, 1.0f);
	TestEqual(TEXT("Root image transform D parsed from string"), Root->Visual.Fill.Image.Transform.D, 1.0f);
	TestEqual(TEXT("Root child count"), Root->Children.Num(), 2);
	if (Root->Children.Num() < 2)
		return false;
	TestEqual(TEXT("Text fontSize parsed from string"), Root->Children[0]->Text.FontSize, 16.0f);
	TestEqual(TEXT("Text lineHeight parsed from string"), Root->Children[0]->Text.LineHeight, 1.5f);
	TestEqual(TEXT("Text letterSpacing parsed from string"), Root->Children[0]->Text.LetterSpacing, 0.2f);
	TestEqual(TEXT("Icon weight parsed from string"), Root->Children[1]->Text.IconWeight, 500);
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
	TestEqual(TEXT("Two-value padding top"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Top, 6.0f);
	TestEqual(TEXT("Two-value padding bottom"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Bottom, 6.0f);
	TestEqual(TEXT("Two-value padding left"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Left, 12.0f);
	TestEqual(TEXT("Two-value padding right"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Right, 12.0f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignSchemaSourceExtensionsRemainOptInTest,
	"Design2UMG.Schema.SourceExtensionsRemainOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignSchemaSourceExtensionsRemainOptInTest::RunTest(const FString& Parameters)
{
	FDesignNode Node;
	TestFalse(TEXT("Fresh node has no source extension"), Node.HasSourceData());
	const FDesignSourceNodeData& EmptyNodeSource = Node.Source();
	TestFalse(TEXT("Read-only node source access stays opt-in"), Node.HasSourceData());
	TestTrue(TEXT("Read-only node source returns empty source type"), EmptyNodeSource.SourceNodeType.IsEmpty());

	Node.MutableSource().SourceNodeType = TEXT("FRAME");
	TestTrue(TEXT("Explicit node source mutation materializes extension"), Node.HasSourceData());
	TestEqual(TEXT("Explicit node source mutation persists"), Node.Source().SourceNodeType, FString(TEXT("FRAME")));

	FDesignDocument Document;
	TestFalse(TEXT("Fresh document has no source document extension"), Document.HasSourceDocumentData());
	TestFalse(TEXT("Fresh document has no parse diagnostics extension"), Document.HasParseDiagnostics());

	const FDesignSourceDocumentData& EmptyDocumentSource = Document.SourceDocument();
	const FDesignParseDiagnostics& EmptyDiagnostics = Document.ParseDiagnostics();
	TestFalse(TEXT("Read-only document source access stays opt-in"), Document.HasSourceDocumentData());
	TestFalse(TEXT("Read-only diagnostics access stays opt-in"), Document.HasParseDiagnostics());
	TestTrue(TEXT("Read-only document source returns empty prototype start id"), EmptyDocumentSource.PrototypeStartNodeId.IsEmpty());
	TestEqual(TEXT("Read-only diagnostics returns empty skipped-node list"), EmptyDiagnostics.SkippedDesignerNodes.Num(), 0);

	Document.MutableSourceDocument().PrototypeStartNodeId = TEXT("root");
	FDesignSkippedNodeReport& AddedReport = Document.MutableParseDiagnostics().SkippedDesignerNodes.AddDefaulted_GetRef();
	AddedReport.NodeId = TEXT("skip-1");

	TestTrue(TEXT("Explicit document source mutation materializes source document"), Document.HasSourceDocumentData());
	TestTrue(TEXT("Explicit diagnostics mutation materializes diagnostics"), Document.HasParseDiagnostics());
	TestEqual(TEXT("Explicit document source mutation persists"), Document.SourceDocument().PrototypeStartNodeId, FString(TEXT("root")));
	TestEqual(TEXT("Explicit diagnostics mutation persists"), Document.ParseDiagnostics().SkippedDesignerNodes.Num(), 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUMGSchemaSourceExtensionsRemainOptInTest,
	"Design2UMG.Schema.UMGSourceExtensionsRemainOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FUMGSchemaSourceExtensionsRemainOptInTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	TestFalse(TEXT("Fresh UMG node has no source extension"), Node.HasSourceData());
	const FUMGSourceNodeData& EmptyNodeSource = Node.Source();
	TestFalse(TEXT("Read-only UMG node source access stays opt-in"), Node.HasSourceData());
	TestTrue(TEXT("Read-only UMG node source starts empty"), EmptyNodeSource.SourceNodeType.IsEmpty());

	Node.MutableSource().SourceNodeType = TEXT("INSTANCE");
	Node.MutableSource().ComponentId = TEXT("12:34");
	TestTrue(TEXT("Explicit UMG node source mutation materializes extension"), Node.HasSourceData());
	TestEqual(TEXT("Explicit UMG node source mutation persists"), Node.Source().SourceNodeType, FString(TEXT("INSTANCE")));
	TestEqual(TEXT("Explicit UMG node component id persists"), Node.Source().ComponentId, FString(TEXT("12:34")));

	FUMGWidgetData Data;
	TestFalse(TEXT("Fresh UMG widget data has no source document extension"), Data.HasSourceDocumentData());
	TestFalse(TEXT("Fresh UMG widget data has no prototype navigation extension"), Data.HasPrototypeNavigationData());

	const FUMGSourceDocumentData& EmptySourceDocument = Data.SourceDocument();
	const FUMGPrototypeNavigationData& EmptyPrototypeNavigation = Data.PrototypeNavigation();
	TestFalse(TEXT("Read-only UMG source document access stays opt-in"), Data.HasSourceDocumentData());
	TestFalse(TEXT("Read-only UMG prototype access stays opt-in"), Data.HasPrototypeNavigationData());
	TestTrue(TEXT("Read-only UMG source document starts empty"), EmptySourceDocument.ComponentsJson.IsEmpty());
	TestEqual(TEXT("Read-only UMG prototype starts empty"), EmptyPrototypeNavigation.ClickBindings.Num(), 0);

	Data.MutableSourceDocument().ComponentsJson = TEXT("{\"12:34\":{\"key\":\"button\"}}");
	FUMGPrototypeNavigationBinding& Binding = Data.MutablePrototypeNavigation().ClickBindings.AddDefaulted_GetRef();
	Binding.TriggerWidgetId = TEXT("cta_button");
	Binding.TriggerWidgetName = TEXT("cta_button");
	Binding.Action = EUMGPrototypeNavigationAction::Back;

	TestTrue(TEXT("Explicit UMG source document mutation materializes extension"), Data.HasSourceDocumentData());
	TestTrue(TEXT("Explicit UMG prototype mutation materializes extension"), Data.HasPrototypeNavigationData());
	TestTrue(TEXT("Explicit UMG source document mutation persists"), Data.SourceDocument().ComponentsJson.Contains(TEXT("button")));
	TestEqual(TEXT("Explicit UMG prototype mutation persists"), Data.PrototypeNavigation().ClickBindings.Num(), 1);
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
	TestEqual(TEXT("Dashboard layout"), Dashboard->LayoutData.Layout, EDesignLayout::Horizontal);
	TestTrue(TEXT("Dashboard clip (Figma clipsContent mapped)"), Dashboard->LayoutData.ExplicitClip.Get(false));
	TestEqual(TEXT("Dashboard children"), Dashboard->Children.Num(), 2);
	if (Dashboard->Children.Num() < 2)
		return false;
	// Sidebar: VERTICAL + individualStrokeWeights + FILL height
	const auto& Sidebar = Dashboard->Children[0];
	TestEqual(TEXT("Sidebar layout"), Sidebar->LayoutData.Layout, EDesignLayout::Vertical);
	TestTrue(TEXT("Sidebar height FILL"), Sidebar->Transform.Height.IsFill());
	TestTrue(TEXT("Sidebar width FIXED"), Sidebar->Transform.Width.IsFixed());
	TestEqual(TEXT("Sidebar gap"), Sidebar->LayoutData.ExplicitGap.Get(0.0f), 8.0f);
	TestEqual(TEXT("Sidebar padding left"), Sidebar->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Left, 16.0f);
	TestFalse(TEXT("Sidebar stroke non-uniform"), Sidebar->Visual.Stroke.Thickness.bUniform);
	TestEqual(TEXT("Sidebar stroke right"), Sidebar->Visual.Stroke.Thickness.Right, 1.0f);
	TestEqual(TEXT("Sidebar stroke top"), Sidebar->Visual.Stroke.Thickness.Top, 0.0f);
	TestEqual(TEXT("Sidebar children"), Sidebar->Children.Num(), 4);
	if (Sidebar->Children.Num() < 4)
		return false;
	// Logo text: fontWeight 700 preserved, textAutoResize→Auto, textAlignVertical CENTER
	const auto& Logo = Sidebar->Children[0];
	TestEqual(TEXT("Logo type"), Logo->Type, EDesignNodeType::Text);
	TestEqual(TEXT("Logo content"), Logo->Text.Content, TEXT("Acme Dashboard"));
	TestEqual(TEXT("Logo fontWeight"), Logo->Text.FontWeight, TEXT("700"));
	TestEqual(TEXT("Logo fontSize"), Logo->Text.FontSize, 20.0f);
	TestEqual(TEXT("Logo textGrowth"), Logo->Text.TextGrowth, EDesignTextGrowth::Auto);
	TestEqual(TEXT("Logo textAlignV"), Logo->Text.TextAlignVertical, EDesignTextAlignVertical::Middle);
	TestTrue(TEXT("Logo lineHeight > 0"), Logo->Text.LineHeight > 0.0f);
	// Divider: LINE type
	TestEqual(TEXT("Divider type"), Sidebar->Children[1]->Type, EDesignNodeType::Line);
	// NavItem: COMPONENT → bReusable
	const auto& NavItem = Sidebar->Children[2];
	TestTrue(TEXT("NavItem reusable"), NavItem->bReusable);
	TestEqual(TEXT("NavItem layout"), NavItem->LayoutData.Layout, EDesignLayout::Horizontal);
	TestEqual(TEXT("NavItem children"), NavItem->Children.Num(), 2);
	// NavItem child: VECTOR → Path
	if (NavItem->Children.Num() >= 1)
		TestEqual(TEXT("NavIcon type"), NavItem->Children[0]->Type, EDesignNodeType::Path);
	// NavItem Instance: INSTANCE → Ref = componentId
	const auto& NavInst = Sidebar->Children[3];
	TestEqual(TEXT("NavInst ref"), NavInst->Ref, TEXT("10:5"));
	// MainContent
	const auto& Main = Dashboard->Children[1];
	TestTrue(TEXT("Main width FILL"), Main->Transform.Width.IsFill());
	TestEqual(TEXT("Main padding"), Main->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Top, 32.0f);
	TestEqual(TEXT("Main children"), Main->Children.Num(), 4);
	if (Main->Children.Num() < 4)
		return false;
	// Header: SPACE_BETWEEN
	const auto& Header = Main->Children[0];
	TestEqual(TEXT("Header justify"), Header->LayoutData.JustifyContent, EDesignJustifyContent::SpaceBetween);
	TestEqual(TEXT("Header align"), Header->LayoutData.AlignItems, EDesignAlignItems::Center);
	// Avatar: ELLIPSE + multi-fill (SOLID + IMAGE)
	if (Header->Children.Num() >= 2)
	{
		const auto& Avatar = Header->Children[1];
		TestEqual(TEXT("Avatar type"), Avatar->Type, EDesignNodeType::Ellipse);
		TestTrue(TEXT("Avatar has image fill"), Avatar->Visual.Fill.IsImage());
		TestTrue(TEXT("Avatar multi-fills >= 2"), Avatar->Visual.Fills.Num() >= 2);
	}
	// StatCard: GRADIENT_LINEAR + DROP_SHADOW
	const auto& StatsRow = Main->Children[1];
	if (StatsRow->Children.Num() >= 1)
	{
		const auto& Card = StatsRow->Children[0];
		TestEqual(TEXT("Card fill gradient"), Card->Visual.Fill.Type, EDesignFillType::Gradient);
		TestEqual(TEXT("Card gradient type"), Card->Visual.Fill.Gradient.GradientType, EDesignGradientType::Linear);
		TestTrue(TEXT("Card gradient stops >= 2"), Card->Visual.Fill.Gradient.Colors.Num() >= 2);
		TestTrue(TEXT("Card has effects"), Card->Visual.Effects.Num() > 0);
		TestEqual(TEXT("Card shadow type"), Card->Visual.Effects[0].Type, EDesignEffectType::Shadow);
		TestFalse(TEXT("Card shadow not inner"), Card->Visual.Effects[0].Shadow.bInner);
	}
	// ChartArea: INNER_SHADOW + BACKGROUND_BLUR + rectangleCornerRadii child
	const auto& Chart = Main->Children[2];
	TestTrue(TEXT("Chart effects >= 2"), Chart->Visual.Effects.Num() >= 2);
	bool bFoundInner = false, bFoundBgBlur = false;
	for (const auto& E : Chart->Visual.Effects)
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
		TestFalse(TEXT("Placeholder non-uniform radius"), Placeholder->Visual.CornerRadius.bUniform);
		TestEqual(TEXT("Placeholder TL"), Placeholder->Visual.CornerRadius.TopLeft, 12.0f);
		TestEqual(TEXT("Placeholder BL"), Placeholder->Visual.CornerRadius.BottomLeft, 0.0f);
	}
	// GROUP: absolute positioned children
	const auto& Overlay = Main->Children[3];
	TestEqual(TEXT("Overlay type"), Overlay->Type, EDesignNodeType::Group);
	TestTrue(TEXT("Overlay children >= 7"), Overlay->Children.Num() >= 7);
	if (Overlay->Children.Num() >= 7)
	{
		// LAYER_BLUR
		TestTrue(TEXT("Badge has effects"), Overlay->Children[0]->Visual.Effects.Num() > 0);
		TestEqual(TEXT("Badge blur type"), Overlay->Children[0]->Visual.Effects[0].Type, EDesignEffectType::Blur);
		// FlippedIcon: relativeTransform flip detection
		TestTrue(TEXT("FlippedIcon flipX"), Overlay->Children[1]->Transform.bFlipX);
		// STAR → Polygon
		TestEqual(TEXT("Star type"), Overlay->Children[2]->Type, EDesignNodeType::Polygon);
		// REGULAR_POLYGON → Polygon
		TestEqual(TEXT("Polygon type"), Overlay->Children[3]->Type, EDesignNodeType::Polygon);
		// BOOLEAN_OPERATION → Path
		TestEqual(TEXT("BoolOp type"), Overlay->Children[4]->Type, EDesignNodeType::Path);
		// IMAGE fill with imageTransform + opacity
		const auto& ImgCard = Overlay->Children[5];
		TestTrue(TEXT("ImgCard has image fill"), ImgCard->Visual.Fill.IsImage());
		TestTrue(TEXT("ImgCard opacity < 1"), ImgCard->Transform.Opacity < 1.0f);
		// GRADIENT_RADIAL
		const auto& GradBg = Overlay->Children[6];
		TestEqual(TEXT("GradBg gradient type"), GradBg->Visual.Fill.Gradient.GradientType, EDesignGradientType::Radial);
	}
	// Justified text + textAlignVertical BOTTOM + textAutoResize NONE
	if (Overlay->Children.Num() >= 8)
	{
		const auto& JustText = Overlay->Children[7];
		TestEqual(TEXT("JustText align"), JustText->Text.TextAlign, EDesignTextAlign::Justify);
		TestEqual(TEXT("JustText valign"), JustText->Text.TextAlignVertical, EDesignTextAlignVertical::Bottom);
		TestEqual(TEXT("JustText growth"), JustText->Text.TextGrowth, EDesignTextGrowth::FixedWidthHeight);
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
	TestTrue(TEXT("PIXELS line height mapped as ratio"), FMath::IsNearlyEqual(PxText->Text.LineHeight, 1.5f, 0.001f));
	TestTrue(TEXT("FONT_SIZE_% line height mapped as ratio"), FMath::IsNearlyEqual(PercentText->Text.LineHeight, 1.4f, 0.001f));
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
	TestFalse(TEXT("No-visible-stroke node should not have stroke"), NoVisibleStroke->Visual.Stroke.HasStroke());
	TestEqual(TEXT("No-visible-stroke thickness remains 0"), NoVisibleStroke->Visual.Stroke.Thickness.Uniform, 0.0f);
	TestTrue(TEXT("Visible-stroke node keeps stroke"), VisibleStroke->Visual.Stroke.HasStroke());
	TestEqual(TEXT("Visible-stroke align parsed"), VisibleStroke->Visual.Stroke.Align, EDesignStrokeAlign::Outside);
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
	TestTrue(TEXT("Root X present"), Root->Transform.HasExplicitX());
	TestTrue(TEXT("Root Y present"), Root->Transform.HasExplicitY());
	TestEqual(TEXT("Root X stays in layout space"), Root->Transform.X, 0.0f);
	TestEqual(TEXT("Root Y stays in layout space"), Root->Transform.Y, 0.0f);
	TestTrue(TEXT("Root width fixed"), Root->Transform.Width.IsFixed());
	TestTrue(TEXT("Root height fixed"), Root->Transform.Height.IsFixed());
	TestEqual(TEXT("Root width uses layout bounds"), Root->Transform.Width.Value, 100.0f);
	TestEqual(TEXT("Root height uses layout bounds"), Root->Transform.Height.Value, 80.0f);
	TestEqual(TEXT("Root child count"), Root->Children.Num(), 1);
	if (Root->Children.Num() != 1)
		return false;

	const TSharedPtr<FDesignNode>& Child = Root->Children[0];
	TestTrue(TEXT("Child X present"), Child->Transform.HasExplicitX());
	TestTrue(TEXT("Child Y present"), Child->Transform.HasExplicitY());
	TestEqual(TEXT("Child X remains relative to layout bounds"), Child->Transform.X, 10.0f);
	TestEqual(TEXT("Child Y remains relative to layout bounds"), Child->Transform.Y, 12.0f);
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
	TestTrue(TEXT("Leaf blur X present"), Avatar->Transform.HasExplicitX());
	TestTrue(TEXT("Leaf blur Y present"), Avatar->Transform.HasExplicitY());
	TestEqual(TEXT("Leaf blur X uses layout bounds"), Avatar->Transform.X, 24.0f);
	TestEqual(TEXT("Leaf blur Y uses layout bounds"), Avatar->Transform.Y, 20.0f);
	TestTrue(TEXT("Leaf blur width fixed"), Avatar->Transform.Width.IsFixed());
	TestTrue(TEXT("Leaf blur height fixed"), Avatar->Transform.Height.IsFixed());
	TestEqual(TEXT("Leaf blur width uses layout bounds"), Avatar->Transform.Width.Value, 26.0f);
	TestEqual(TEXT("Leaf blur height uses layout bounds"), Avatar->Transform.Height.Value, 26.0f);
	TestTrue(TEXT("Leaf blur keeps render delta X"), Avatar->Metadata.Contains(TEXT("source.renderBoundsDeltaX")));
	TestTrue(TEXT("Leaf blur keeps render delta Y"), Avatar->Metadata.Contains(TEXT("source.renderBoundsDeltaY")));
	TestTrue(TEXT("Leaf blur keeps render delta W"), Avatar->Metadata.Contains(TEXT("source.renderBoundsDeltaW")));
	TestTrue(TEXT("Leaf blur keeps render delta H"), Avatar->Metadata.Contains(TEXT("source.renderBoundsDeltaH")));
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
	TestTrue(TEXT("Filled frame X present"), Card->Transform.HasExplicitX());
	TestTrue(TEXT("Filled frame Y present"), Card->Transform.HasExplicitY());
	TestEqual(TEXT("Filled frame X uses layout bounds"), Card->Transform.X, 24.0f);
	TestEqual(TEXT("Filled frame Y uses layout bounds"), Card->Transform.Y, 16.0f);
	TestTrue(TEXT("Filled frame width fixed"), Card->Transform.Width.IsFixed());
	TestTrue(TEXT("Filled frame height fixed"), Card->Transform.Height.IsFixed());
	TestEqual(TEXT("Filled frame width uses layout bounds"), Card->Transform.Width.Value, 160.0f);
	TestEqual(TEXT("Filled frame height uses layout bounds"), Card->Transform.Height.Value, 160.0f);
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
	TestTrue(TEXT("Filled container X present"), Content->Transform.HasExplicitX());
	TestTrue(TEXT("Filled container Y present"), Content->Transform.HasExplicitY());
	TestEqual(TEXT("Filled container X uses layout bounds"), Content->Transform.X, 20.0f);
	TestEqual(TEXT("Filled container Y uses layout bounds"), Content->Transform.Y, 20.0f);
	TestTrue(TEXT("Filled container width fixed"), Content->Transform.Width.IsFixed());
	TestTrue(TEXT("Filled container height fixed"), Content->Transform.Height.IsFixed());
	TestEqual(TEXT("Filled container width uses layout bounds"), Content->Transform.Width.Value, 300.0f);
	TestEqual(TEXT("Filled container height uses layout bounds"), Content->Transform.Height.Value, 200.0f);
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
	TestTrue(TEXT("Equal radii normalized to uniform"), Uniform->Visual.CornerRadius.bUniform);
	TestEqual(TEXT("Equal radii keeps numeric value"), Uniform->Visual.CornerRadius.Uniform, 100.0f);
	TestFalse(TEXT("Mixed radii remains non-uniform"), NonUniform->Visual.CornerRadius.bUniform);
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
							"name": "ButtonLabelInter",
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
						},
						{
							"id": "0:4",
							"name": "ButtonLabelRoboto",
							"type": "TEXT",
							"visible": true,
							"layoutSizingHorizontal": "HUG",
							"layoutSizingVertical": "HUG",
							"absoluteBoundingBox": {"x":20,"y":56,"width":35,"height":20},
							"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
							"style": {
								"fontFamily": "Roboto",
								"fontSize": 14,
								"fontWeight": 400
							},
							"characters": "Label"
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

	TFunction<TSharedPtr<FDesignNode>(const TArray<TSharedPtr<FDesignNode>>&, const FString&)> FindTextNodeByContent;
	FindTextNodeByContent = [&](const TArray<TSharedPtr<FDesignNode>>& Nodes, const FString& ExpectedContent) -> TSharedPtr<FDesignNode>
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}

			if (Node->Type == EDesignNodeType::Text && Node->Text.Content == ExpectedContent)
			{
				return Node;
			}

			if (TSharedPtr<FDesignNode> FoundChild = FindTextNodeByContent(Node->Children, ExpectedContent))
			{
				return FoundChild;
			}
		}

		return nullptr;
	};

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	const auto VerifySingleLineFitLabel = [&](const FString& ExpectedText, const float ExpectedWidth, const float ExpectedHeight, const float SourceFontSize)
	{
		const FUMGWidgetNode* Label = FindWidgetByProperty(Data.RootWidget, TEXT("Text"), &ExpectedText);
		TestTrue(*FString::Printf(TEXT("Find text widget '%s'"), *ExpectedText), Label != nullptr);
		if (!Label)
		{
			return false;
		}

		TSharedPtr<FDesignNode> SourceNode = FindTextNodeByContent(Doc.Children, ExpectedText);
		TestTrue(*FString::Printf(TEXT("Find source text node '%s'"), *ExpectedText), SourceNode.IsValid());
		if (!SourceNode.IsValid())
		{
			return false;
		}

		TextMeasurement::ResetCache();
		const FTextResolutionTrace Trace = TextMeasurementTestTrace::DumpTextResolutionTrace(SourceNode);

		TestTrue(*FString::Printf(TEXT("Provided fit width should be preserved for '%s'"), *ExpectedText),
			FMath::IsNearlyEqual(Label->Bounds.W, ExpectedWidth, 0.5f));
		TestTrue(*FString::Printf(TEXT("Provided fit height should be preserved for '%s'"), *ExpectedText),
			FMath::IsNearlyEqual(Label->Bounds.H, ExpectedHeight, 0.5f));
		TestTrue(*FString::Printf(TEXT("Source font size metadata should be preserved for '%s'"), *ExpectedText),
			FMath::IsNearlyEqual(Label->TextStyle.SourceFontSize, SourceFontSize, 0.01f));
		TestTrue(*FString::Printf(TEXT("Resolved widget font size should stay positive and never exceed the authored size for '%s'"), *ExpectedText),
			Label->TextStyle.FontSize > 0.0f
			&& Label->TextStyle.FontSize <= SourceFontSize + 0.01f);
		TestTrue(*FString::Printf(TEXT("Single-line fit text '%s' should use wrapped-width-fit runtime measurement"), *ExpectedText),
			Trace.ResolutionPath.Equals(TEXT("wrapped-width-fit"), ESearchCase::CaseSensitive));
		TestTrue(*FString::Printf(TEXT("Single-line fit text '%s' should still shrink below the authored size after runtime fitting"), *ExpectedText),
			Trace.CompiledFontSize > 0.0f
			&& Trace.CompiledFontSize + 0.5f < SourceFontSize);
		TestTrue(*FString::Printf(TEXT("Single-line fit text '%s' should stay within authored width"), *ExpectedText),
			Trace.CompiledBounds.X <= ExpectedWidth + 4.0f);
		return true;
	};

	if (!VerifySingleLineFitLabel(TEXT("Download"), 63.0f, 20.0f, 18.0f))
	{
		return false;
	}

	if (!VerifySingleLineFitLabel(TEXT("Label"), 35.0f, 20.0f, 14.0f))
	{
		return false;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTextKeepsRenderBoundsWhenRenderBoundsShrinkTest,
	"Design2UMG.FigmaParser.TextKeepsRenderBoundsWhenRenderBoundsShrink",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTextKeepsRenderBoundsWhenRenderBoundsShrinkTest::RunTest(const FString& Parameters)
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":320,"height":200},
					"children": [{
						"id": "0:3",
						"name": "Quick Access",
						"type": "TEXT",
						"visible": true,
						"absoluteBoundingBox": {"x":13,"y":29,"width":111,"height":23},
						"absoluteRenderBounds": {"x":13.2,"y":31.8,"width":108.74,"height":17.57},
						"characters": "Quick Access",
						"style": {
							"fontFamily": "Oxygen",
							"fontSize": 18,
							"fontWeight": 700,
							"textAlignHorizontal": "LEFT",
							"textAlignVertical": "TOP"
						},
						"fills": [{
							"type":"SOLID",
							"visible":true,
							"color":{"r":1,"g":1,"b":1,"a":1}
						}]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma text bounds fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& TextNode = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Text node width fixed"), TextNode->Transform.Width.IsFixed());
	TestTrue(TEXT("Text node height fixed"), TextNode->Transform.Height.IsFixed());
	TestEqual(TEXT("Text node width keeps render bounds"), TextNode->Transform.Width.Value, 108.74f);
	TestEqual(TEXT("Text node height keeps render bounds"), TextNode->Transform.Height.Value, 17.57f);
	TestEqual(TEXT("Text node X keeps render bounds"), TextNode->Transform.X, 13.2f);
	TestEqual(TEXT("Text node Y keeps render bounds"), TextNode->Transform.Y, 31.8f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJustifyAutoTextWrapParityTest,
	"Design2UMG.Text.JustifyAutoWrapParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

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
	TestEqual(TEXT("fontStyle parsed"), TextNode->Text.FontStyle, TEXT("italic"));
	TestTrue(TEXT("underline parsed"), TextNode->Text.bUnderline);
	TestTrue(TEXT("strikethrough parsed"), TextNode->Text.bStrikethrough);
	TestEqual(TEXT("href parsed"), TextNode->Text.Href, TEXT("https://example.com"));
	TestEqual(TEXT("mesh_gradient maps to gradient"), TextNode->Visual.Fill.Type, EDesignFillType::Gradient);
	TestTrue(TEXT("mesh_gradient marker set"), TextNode->Visual.Fill.bFromMeshGradient);
	TestTrue(TEXT("mesh_gradient raw payload kept"), !TextNode->Visual.Fill.MeshGradientRaw.IsEmpty());
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

	TestTrue(TEXT("Rich text style rows exported via typed text payload"), RichLabel->Text.bHasRichTextStyleRows);
	if (!RichLabel->Text.bHasRichTextStyleRows)
	{
		return false;
	}

	// Parse JSON → stable snapshot: rowName{key=value;...}|... (rows sorted by name, keys in fixed order)
	TSharedPtr<FJsonObject> StyleRows;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RichLabel->Text.RichTextStyleRowsJson);
		TestTrue(TEXT("Style rows JSON is valid"), FJsonSerializer::Deserialize(Reader, StyleRows) && StyleRows.IsValid());
		if (!StyleRows.IsValid())
		{
			return false;
		}
	}

	// Fixed key order matching the serializer's runtime-property set
	static const TCHAR* const KeyOrder[] = {
		TEXT("Color"), TEXT("DesignFontSize"), TEXT("FontFamily"), TEXT("FontSize"),
		TEXT("Href"), TEXT("LetterSpacing"), TEXT("Strikethrough"), TEXT("TypefaceFontName"), TEXT("Underline")
	};

	TArray<FString> RowNames;
	for (const auto& Pair : StyleRows->Values)
	{
		RowNames.Add(Pair.Key);
	}
	RowNames.Sort();

	FString Snapshot;
	for (const FString& RowName : RowNames)
	{
		if (!Snapshot.IsEmpty()) Snapshot += TEXT("|");
		Snapshot += RowName + TEXT("{");
		const TSharedPtr<FJsonObject>* RowObj = nullptr;
		StyleRows->TryGetObjectField(RowName, RowObj);
		bool bFirstKey = true;
		for (const TCHAR* Key : KeyOrder)
		{
			if (!RowObj || !(*RowObj)->HasField(Key)) continue;
			if (!bFirstKey) Snapshot += TEXT(";");
			bFirstKey = false;
			const TSharedPtr<FJsonValue>& Val = (*RowObj)->Values[Key];
			FString ValStr;
			if (Val->Type == EJson::String)
			{
				ValStr = Val->AsString();
			}
			else if (Val->Type == EJson::Number)
			{
				ValStr = FString::Printf(TEXT("%.1f"), Val->AsNumber());
			}
			else if (Val->Type == EJson::Boolean)
			{
				ValStr = Val->AsBool() ? TEXT("true") : TEXT("false");
			}
			Snapshot += FString(Key) + TEXT("=") + ValStr;
		}
		Snapshot += TEXT("}");
	}

	const FString ExpectedSnapshot =
		TEXT("d2u_run_0{Color=#112233FF;DesignFontSize=18.0;FontFamily=/Game/Design2UMG/Fonts/Geist.Geist;FontSize=27.0;TypefaceFontName=Regular}")
		TEXT("|d2u_run_1{Color=#112233FF;DesignFontSize=18.0;FontFamily=/Game/Design2UMG/Fonts/Geist.Geist;FontSize=27.0;Href=https://docs.example.com;TypefaceFontName=Bold;Underline=true}");

	TestEqual(TEXT("Rich text style rows snapshot matches"), Snapshot, ExpectedSnapshot);
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
#include "Design2UMGTests.EffectsAndShadow.inl"

#include "Design2UMGTests.FigmaCoverage.inl"

#endif // WITH_AUTOMATION_WORKER
