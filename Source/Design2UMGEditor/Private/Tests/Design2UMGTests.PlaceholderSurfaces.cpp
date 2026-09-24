// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Placeholder surface coverage: empty-slot semantic hints, placeholder conversion, and hatch palette ref-context tracking.

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "Design2UMGSettings.h"
#include "PenParser.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGCoreTests;
using namespace Design2UMGTestUtils;

namespace
{
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenEmptySlotSemanticHintTest,
	"Design2UMG.PenParser.EmptySlotSemanticHint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenEmptySlotSemanticHintTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{ "id":"n1", "type":"frame", "name":"Card Header", "slot":[], "width":100, "height":32 },
			{ "id":"n2", "type":"frame", "name":"Page Numbers", "slot":["content_slot"], "width":100, "height":32 },
			{ "id":"n3", "type":"frame", "name":"Any Node", "placeholder":true, "width":100, "height":32 }
		]
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("Pen parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 3)
	{
		return false;
	}
	TestEqual(TEXT("Empty slot keeps source.slot.present for preservation"), Doc.Children[0]->Metadata.FindRef(TEXT("source.slot.present")), TEXT("true"));
	TestEqual(TEXT("Empty slot keeps source.slot.count for preservation"), Doc.Children[0]->Metadata.FindRef(TEXT("source.slot.count")), TEXT("0"));
	TestTrue(TEXT("Empty slot array should mark placeholder surface"), Doc.Children[0]->LayoutData.bSemanticPlaceholderSurface);
	TestEqual(TEXT("Non-empty slot keeps source.slot.present for preservation"), Doc.Children[1]->Metadata.FindRef(TEXT("source.slot.present")), TEXT("true"));
	TestEqual(TEXT("Non-empty slot keeps source.slot.count for preservation"), Doc.Children[1]->Metadata.FindRef(TEXT("source.slot.count")), TEXT("1"));
	TestFalse(TEXT("Non-empty slot should not synthesize source.slot.empty"), Doc.Children[1]->Metadata.Contains(TEXT("source.slot.empty")));
	TestTrue(TEXT("Non-empty slot array should mark placeholder surface"), Doc.Children[1]->LayoutData.bSemanticPlaceholderSurface);
	TestFalse(TEXT("Explicit placeholder should not synthesize source slot preservation keys"), Doc.Children[2]->Metadata.Contains(TEXT("source.slot.present")) || Doc.Children[2]->Metadata.Contains(TEXT("source.slot.count")) || Doc.Children[2]->Metadata.Contains(TEXT("source.slot.empty")));
	TestTrue(TEXT("Explicit placeholder=true should mark placeholder surface"), Doc.Children[2]->LayoutData.bSemanticPlaceholderSurface);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaEmptySlotSemanticHintTest,
	"Design2UMG.FigmaParser.EmptySlotSemanticHint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaEmptySlotSemanticHintTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("root"), TEXT("Root"), TEXT("FRAME"), 0, 0, 300, 200,
			FString::Printf(TEXT("%s,%s,%s"),
				*MakeLeafNode(TEXT("e1"), TEXT("Card Header"), TEXT("FRAME"), 10, 10, 80, 20,
					TEXT(",\"slot\":[]")),
				*MakeLeafNode(TEXT("e2"), TEXT("Page Numbers"), TEXT("FRAME"), 10, 40, 80, 20,
					TEXT(",\"slot\":[\"content_slot\"]")),
				*MakeLeafNode(TEXT("e3"), TEXT("Any"), TEXT("FRAME"), 10, 70, 80, 20,
					TEXT(",\"placeholder\":true")))));
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
			{ "id":"pageSlot", "type":"frame", "name":"Page Numbers Slot", "slot":[], "width":40, "height":40, "x":0, "y":48 }
		]
	})");
	FDesignDocument Doc;
	const bool bParsed = FPenParser::ParseDocument(Json, Doc);
	TestTrue(TEXT("Pen parse succeeds"), bParsed);
	if (!bParsed || Doc.Children.Num() != 2)
	{
		return false;
	}
	TestEqual(TEXT("Empty slot metadata preserved"), Doc.Children[0]->Metadata.FindRef(TEXT("source.slot.empty")), TEXT("true"));
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

	TestEqual(TEXT("slotCell uses Image"), SlotCell->Type, TEXT("Image"));
	if (!TestTrue(TEXT("Typed shape payload is present"), PageSlot->Brush.Shape.IsSet())) return false;
	const auto& PageShape = PageSlot->Brush.Shape.GetValue();
	if (!TestTrue(TEXT("Typed shape payload is present"), SlotCell->Brush.Shape.IsSet())) return false;
	const auto& SlotShape = SlotCell->Brush.Shape.GetValue();
	TestFalse(TEXT("Generated placeholder carries no JSON parameters"), SlotCell->Brush.MaterialParams.IsSet());
	TestEqual(TEXT("slotCell placeholder material"), GetWidgetProperty(*SlotCell, TEXT("MaterialRef")), TEXT("placeholdersurface"));
	TestTrue(TEXT("slotCell placeholder keeps canonical fill color when custom style disabled"), SlotShape.Color.IsSet() && SlotShape.Color->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#D282FF33")))));
	TestTrue(TEXT("slotCell placeholder keeps canonical outline color when custom style disabled"), SlotShape.StripeColor.IsSet() && SlotShape.StripeColor->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#D480FF")))));
	TestTrue(TEXT("slotCell placeholder keeps canonical stripe width when custom style disabled"), SlotShape.StripeWidth.IsSet() && SlotShape.StripeWidth.GetValue() == 1);
	TestTrue(TEXT("slotCell placeholder keeps canonical stripe gap when custom style disabled"), SlotShape.StripeGap.IsSet() && SlotShape.StripeGap.GetValue() == 4);
	TestTrue(TEXT("slotCell placeholder width override exists"), HasWidgetProperty(*SlotCell, TEXT("WidthOverride")));
	TestTrue(TEXT("slotCell placeholder height override exists"), HasWidgetProperty(*SlotCell, TEXT("HeightOverride")));
	TestEqual(TEXT("pageSlot uses Image"), PageSlot->Type, TEXT("Image"));
	TestEqual(TEXT("pageSlot placeholder material"), GetWidgetProperty(*PageSlot, TEXT("MaterialRef")), TEXT("placeholdersurface"));
	TestTrue(TEXT("pageSlot placeholder keeps canonical fill color when custom style disabled"), PageShape.Color.IsSet() && PageShape.Color->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#D282FF33")))));
	TestTrue(TEXT("pageSlot placeholder keeps canonical outline color when custom style disabled"), PageShape.StripeColor.IsSet() && PageShape.StripeColor->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#D480FF")))));
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
		if (!TestTrue(TEXT("Typed shape payload is present"), BaseSlot->Brush.Shape.IsSet())) return false;
		const auto& Shape = BaseSlot->Brush.Shape.GetValue();

		TestTrue(TEXT("Base placeholder keeps configured palette"), Shape.Color.IsSet() && Shape.Color->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#10203033")))));
		TestTrue(TEXT("Base placeholder keeps configured outline"), Shape.StripeColor.IsSet() && Shape.StripeColor->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#112233")))));
		TestTrue(TEXT("Base placeholder keeps configured stripe width"), Shape.StripeWidth.IsSet() && Shape.StripeWidth.GetValue() == 3);
		TestTrue(TEXT("Base placeholder keeps configured stripe gap"), Shape.StripeGap.IsSet() && Shape.StripeGap.GetValue() == 9);
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
		if (!TestTrue(TEXT("Typed shape payload is present"), SlotInstance->Brush.Shape.IsSet())) return false;
		const auto& Shape = SlotInstance->Brush.Shape.GetValue();

		TestTrue(TEXT("Expanded ref placeholder uses configured instance palette"), Shape.Color.IsSet() && Shape.Color->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#44556633")))));
		TestTrue(TEXT("Expanded ref placeholder uses configured instance outline"), Shape.StripeColor.IsSet() && Shape.StripeColor->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#778899")))));
		TestTrue(TEXT("Expanded ref placeholder keeps configured stripe width"), Shape.StripeWidth.IsSet() && Shape.StripeWidth.GetValue() == 3);
		TestTrue(TEXT("Expanded ref placeholder keeps configured stripe gap"), Shape.StripeGap.IsSet() && Shape.StripeGap.GetValue() == 9);
	}
	return true;
}

#endif
