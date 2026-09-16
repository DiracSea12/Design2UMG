// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved.
// SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "FigmaParser.h"
#include "Design2UMGConverter.h"
#include "UMGGenerator.h"
#include "UMGWidgetUtils.h"
#include "WidgetBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "Automation/Design2UMGAutomationStructureSnapshot.h"
#include "Dom/JsonObject.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "UMGTextStyleApplication.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaCharacterColorRuntimeTest,
	"Design2UMG.Converter.CharacterColorRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaCharacterColorRuntimeTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"document":{"id":"root","type":"FRAME",
		"absoluteBoundingBox":{"x":0,"y":0,"width":240,"height":160},
		"children":[{"id":"text","type":"TEXT","name":"Mixed color","characters":"MM\nMM",
		"absoluteBoundingBox":{"x":10,"y":10,"width":220,"height":140},
		"style":{"fontFamily":"Roboto","fontSize":48,"fontWeight":400},
		"fills":[{"type":"SOLID","color":{"r":0,"g":0,"b":1}}],
		"characterStyleOverrides":[1],
		"styleOverrideTable":{"1":{"fills":[{"type":"SOLID","opacity":0.5,
		"color":{"r":1,"g":0,"b":0}}]}}}]}})JSON");
	FDesignDocument Document;
	if (!TestTrue(TEXT("Source parses"), FFigmaParser::ParseDocument(Json, Document))) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data,
		TEXT("/Temp/Design2UMGCharacterColor_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (!TestNotNull(TEXT("Native WBP generated"), Blueprint)) return false;
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	const TSharedPtr<FJsonObject> Structure = Design2UMGAutomationStructureSnapshot::BuildWidgetTreeStructureSnapshot(Blueprint->WidgetTree);
	const TArray<TSharedPtr<FJsonValue>>& VisibleTexts = Structure->GetArrayField(TEXT("visible_texts"));
	if (!TestEqual(TEXT("One compiled multiline label"), VisibleTexts.Num(), 1)) return false;
	TestEqual(TEXT("No internal style tags become visible across lines"), VisibleTexts[0]->AsString(), FString(TEXT("MM MM")));
	TArray<uint8> Png;
	FIntPoint Size;
	FString Error;
	if (!TestTrue(TEXT("Compiled widget renders"), FUMGWidgetUtils::RenderWidgetToPng(Blueprint, Png, Size, &Error)))
	{
		AddError(Error);
		return false;
	}
	FImage Image;
	if (!TestTrue(TEXT("PNG decodes"), FImageUtils::DecompressImage(Png.GetData(), Png.Num(), Image))) return false;
	if (!TestTrue(TEXT("Rendered pixel format"), Image.Format == ERawImageFormat::BGRA8)) return false;
	int32 RedCount = 0;
	int32 BlueCount = 0;
	int32 MaxRedAlpha = 0;
	const FColor* Pixels = reinterpret_cast<const FColor*>(Image.RawData.GetData());
	for (int64 Index = 0; Index < static_cast<int64>(Image.SizeX) * Image.SizeY; ++Index)
	{
		const FColor Pixel = Pixels[Index];
		if (Pixel.A < 16) continue;
		if (Pixel.R > 200 && Pixel.G < 10 && Pixel.B < 10)
		{
			++RedCount;
			MaxRedAlpha = FMath::Max(MaxRedAlpha, static_cast<int32>(Pixel.A));
		}
		if (Pixel.B > 200 && Pixel.R < 10 && Pixel.G < 10) ++BlueCount;
	}
	AddInfo(FString::Printf(TEXT("Rendered red=%d, blue=%d, maximum red alpha=%d"), RedCount, BlueCount, MaxRedAlpha));
	TestTrue(TEXT("Overridden glyph renders red"), RedCount > 20);
	TestTrue(TEXT("Omitted trailing index retains blue base glyph"), BlueCount > 20);
	TestTrue(TEXT("Red glyph retains source half opacity"), FMath::Abs(MaxRedAlpha - 128) <= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRichTextDefaultFontTest,
	"Design2UMG.Converter.RichTextDefaultFont",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRichTextDefaultFontTest::RunTest(const FString&)
{
	URichTextBlock* RichText = NewObject<URichTextBlock>();
	FUMGWidgetNode Node;
	Node.TextStyle.FontSize = 16.0f;
	Node.Text.TypefaceFontName = TEXT("Regular");
	UMGTextStyleApplication::ApplyRichTextDefaultStyle(RichText, Node);
	TestEqual(TEXT("An unresolved source font uses the same native font as plain text"),
		RichText->GetCurrentDefaultTextStyle().Font.FontObject,
		GetDefault<UTextBlock>()->GetFont().FontObject);
	TestEqual(TEXT("Requested typeface survives default font initialization"),
		RichText->GetCurrentDefaultTextStyle().Font.TypefaceFontName, FName(TEXT("Regular")));
	return true;
}

#endif
