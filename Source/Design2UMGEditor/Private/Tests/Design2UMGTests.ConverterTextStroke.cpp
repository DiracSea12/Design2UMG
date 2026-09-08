// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Converter detail coverage: vertical text alignment slot behavior and non-uniform stroke padding.

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "PenParser.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;


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
	const FUMGWidgetNode* LabelWrapper = FindWidgetById(Data.RootWidget, TEXT("label_sz"));
	TestNotNull(TEXT("Converted label exists"), Label);
	TestNotNull(TEXT("Converted label SizeBox wrapper exists"), LabelWrapper);
	if (!Label || !LabelWrapper)
	{
		return false;
	}
	TestEqual(TEXT("Auto-height label keeps standard type"), Label->Type, TEXT("TextBlock"));
	TestEqual(TEXT("Auto-height label wrapper is a SizeBox"), LabelWrapper->Type, TEXT("SizeBox"));
	TestEqual(TEXT("textAlignVertical kept as metadata"), GetWidgetProperty(*Label, TEXT("TextAlignVertical")), TEXT("Center"));
	TestTrue(TEXT("Auto-height label slot stays top-aligned on the wrapper"), LabelWrapper->Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Top);
	TestFalse(TEXT("Auto-height text does not synthesize internal padding"),
		Label->WidgetStyle.Padding.IsSet());
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
	const FUMGWidgetNode* LabelWrapper = FindWidgetById(Data.RootWidget, TEXT("label_sz"));
	TestNotNull(TEXT("Converted fixed-height label exists"), Label);
	TestNotNull(TEXT("Converted fixed-height label wrapper exists"), LabelWrapper);
	if (!Label || !LabelWrapper)
	{
		return false;
	}
	TestEqual(TEXT("Fixed-height label keeps standard type"), Label->Type, TEXT("TextBlock"));
	TestEqual(TEXT("Fixed-height label wrapper is a SizeBox"), LabelWrapper->Type, TEXT("SizeBox"));

	TestEqual(TEXT("Fixed-height textAlignVertical kept as metadata"), GetWidgetProperty(*Label, TEXT("TextAlignVertical")), TEXT("Center"));
	TestTrue(TEXT("Fixed-height label slot stays top-aligned on the wrapper"), LabelWrapper->Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Top);
	TestTrue(TEXT("Fixed-height text synthesizes internal padding"),
		Label->WidgetStyle.Padding.IsSet());
	const FMargin Padding = Label->WidgetStyle.Padding.GetValue();
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

#endif

