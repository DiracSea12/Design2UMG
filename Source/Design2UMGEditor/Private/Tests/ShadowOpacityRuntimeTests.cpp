// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGImageTestUtils.h"
#include "FigmaParser.h"
#include "Design2UMGConverter.h"
#include "UMGGenerator.h"
#include "UMGWidgetRenderer.h"
#include "WidgetBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "Misc/ScopeExit.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowOpacityRuntimeTest,
	"Design2UMG.Converter.ShadowOpacityRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FShadowOpacityRuntimeTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"document":{"id":"root","type":"FRAME",
		"absoluteBoundingBox":{"x":0,"y":0,"width":220,"height":160},
		"children":[{"id":"panel","type":"RECTANGLE",
			"absoluteBoundingBox":{"x":20,"y":20,"width":80,"height":80},
			"fills":[{"type":"SOLID","color":{"r":1,"g":1,"b":1,"a":1}}],
			"effects":[{"type":"DROP_SHADOW","visible":true,"radius":0,
				"offset":{"x":100,"y":0},"color":{"r":0.8,"g":0.4,"b":0.2,"a":0.5}}]}]}})JSON");
	for (const int32 Blur : {0, 12})
	{
		for (const bool Ellipse : {false, true})
		{
		FString SourceJson = Json.Replace(TEXT("\"radius\":0"), *FString::Printf(TEXT("\"radius\":%d"), Blur));
		if (Ellipse)
		{
			SourceJson.ReplaceInline(TEXT("RECTANGLE"), TEXT("ELLIPSE"));
			SourceJson.ReplaceInline(TEXT("\"width\":80,\"height\":80"), TEXT("\"width\":80,\"height\":40"));
		}
		FDesignDocument Document;
		if (!TestTrue(TEXT("Parse source shadow"), FFigmaParser::ParseDocument(SourceJson, Document))) return false;
		FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		const FString Folder = TEXT("/Game/Design2UMG/Automation/ShadowOpacity_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
		UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder);
		if (!TestNotNull(TEXT("Build native widget"), Blueprint)) return false;
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		TArray<uint8> Png;
		FIntPoint Size;
		FString Error;
		if (!TestTrue(TEXT("Render compiled widget"), FUMGWidgetRenderer::RenderWidgetToPng(Blueprint, Png, Size, &Error))) return false;
		FImage Image;
		if (!TestTrue(TEXT("Decode rendered PNG"), Design2UMGImageTestUtils::DecodeToBGRA8(Png.GetData(), Png.Num(), Image))) return false;
		if (!TestTrue(TEXT("Expected pixel surface"), Image.Format == ERawImageFormat::BGRA8 && Image.SizeX >= 220 && Image.SizeY >= 160)) return false;
		const FColor* Pixels = reinterpret_cast<const FColor*>(Image.RawData.GetData());
		const FColor Pixel = Pixels[(Ellipse ? 40 : 60) * Image.SizeX + 160];
		AddInfo(FString::Printf(TEXT("Ellipse %d blur %d unoccluded shadow RGBA: %d,%d,%d,%d"), Ellipse, Blur, Pixel.R, Pixel.G, Pixel.B, Pixel.A));
		TestTrue(TEXT("Source half-opacity is applied once, not squared"), FMath::Abs(static_cast<int32>(Pixel.A) - 128) <= 2);
		TestTrue(TEXT("Source shadow colour is applied once"),
			FMath::Abs(static_cast<int32>(Pixel.R) - 204) <= 3
			&& FMath::Abs(static_cast<int32>(Pixel.G) - 102) <= 3
			&& FMath::Abs(static_cast<int32>(Pixel.B) - 51) <= 3);
		if (Ellipse)
		{
			const FColor Corner = Pixels[21 * Image.SizeX + 121];
			AddInfo(FString::Printf(TEXT("Ellipse bounding-box corner alpha: %d"), Corner.A));
			TestTrue(TEXT("Ellipse shadow does not fill its bounding-box corners"), Corner.A <= 12);
		}
		}
	}
	return true;
}
#endif
