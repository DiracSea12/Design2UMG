// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "FigmaParser.h"
#include "Design2UMGConverter.h"
#include "UMGGenerator.h"
#include "UMGWidgetUtils.h"
#include "ImageUtils.h"
#include "ImageCore.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaNestedStretchPaddingRuntimeTest,
 "Design2UMG.FigmaParser.NestedStretchPaddingRuntime",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaNestedStretchPaddingRuntimeTest::RunTest(const FString&)
{
 const FString Json = TEXT(R"JSON({"document":{"id":"root","type":"FRAME","name":"PaddingProbe",
 "absoluteBoundingBox":{"x":0,"y":0,"width":128,"height":80},"children":[
 {"id":"outer","type":"INSTANCE","name":"Outer","componentId":"outer-component",
 "absoluteBoundingBox":{"x":10,"y":10,"width":100,"height":40},"children":[
 {"id":"inner","type":"INSTANCE","name":"Inner","componentId":"inner-component",
 "absoluteBoundingBox":{"x":10,"y":10,"width":100,"height":40},"layoutMode":"VERTICAL",
 "paddingLeft":12,"paddingRight":12,"paddingTop":8,"paddingBottom":8,"children":[
 {"id":"content","type":"RECTANGLE","name":"Content","layoutSizingHorizontal":"FILL",
 "layoutSizingVertical":"FIXED","absoluteBoundingBox":{"x":22,"y":18,"width":76,"height":24},
 "fills":[{"type":"SOLID","color":{"r":1,"g":1,"b":1},"opacity":1}]}]}]}]}})JSON");
 auto CheckRuntime = [this](const FString& CaseJson, int32 OutsideStrokeInset)
 {
 FDesignDocument Document;
 if (!TestTrue(TEXT("Source fixture parses"), FFigmaParser::ParseDocument(CaseJson, Document))) return false;
 const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
 UWidgetBlueprint* Blueprint = FUMGGenerator::GenerateWidgetBlueprint(Data,
  TEXT("/Game/Design2UMG/Automation/PaddingProbe_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
 if (!TestNotNull(TEXT("Native widget blueprint generated"), Blueprint)) return false;
 TArray<uint8> Png;
 FIntPoint Size;
 FString Error;
 if (!TestTrue(TEXT("Generated widget renders"), FUMGWidgetUtils::RenderWidgetToPng(Blueprint, Png, Size, &Error))) return false;
 FImage Image;
 if (!TestTrue(TEXT("Decode runtime render"), FImageUtils::DecompressImage(Png.GetData(), Png.Num(), Image))) return false;
 if (!TestTrue(TEXT("BGRA8 runtime pixels"), Image.Format == ERawImageFormat::BGRA8)) return false;
 const FColor* Pixels = reinterpret_cast<const FColor*>(Image.RawData.GetData());
 int32 MinX = Image.SizeX, MinY = Image.SizeY, MaxX = -1, MaxY = -1;
 for (int32 Y = 0; Y < Image.SizeY; ++Y)
 {
  for (int32 X = 0; X < Image.SizeX; ++X)
  {
   const FColor& Pixel = Pixels[Y * Image.SizeX + X];
   if (Pixel.R > 240 && Pixel.G > 240 && Pixel.B > 240 && Pixel.A > 240)
   {
    MinX = FMath::Min(MinX, X); MinY = FMath::Min(MinY, Y);
    MaxX = FMath::Max(MaxX, X); MaxY = FMath::Max(MaxY, Y);
   }
  }
 }
 TestEqual(TEXT("Rendered content left matches source padding"), MinX, 22 + OutsideStrokeInset);
 TestEqual(TEXT("Rendered content top matches source padding"), MinY, 18 + OutsideStrokeInset);
 TestEqual(TEXT("Rendered content width matches source inner width"), MaxX - MinX + 1, 76);
 TestEqual(TEXT("Rendered content height matches source"), MaxY - MinY + 1, 24);
  return true;
 };
 for (int32 Variant = 0; Variant < 6; ++Variant)
 {
  FString CaseJson = Json;
  if (Variant != 0 && Variant < 4)
  {
   CaseJson.ReplaceInline(TEXT("\"layoutSizingHorizontal\":\"FILL\""), TEXT("\"layoutSizingHorizontal\":\"FIXED\""));
  }
  if (Variant >= 2 && Variant < 4)
  {
   CaseJson.ReplaceInline(TEXT("\"layoutMode\":\"VERTICAL\""), TEXT("\"layoutMode\":\"HORIZONTAL\""));
  }
  if (Variant == 2)
  {
   CaseJson.ReplaceInline(TEXT("\"layoutSizingVertical\":\"FIXED\""), TEXT("\"layoutSizingVertical\":\"FILL\""));
  }
  if (Variant >= 4)
  {
   CaseJson.ReplaceInline(TEXT("\"name\":\"PaddingProbe\","),
    TEXT("\"name\":\"PaddingProbe\",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":0,\"g\":0,\"b\":0}}],"));
  }
  if (Variant == 5)
   CaseJson.ReplaceInline(TEXT("\"name\":\"PaddingProbe\","),
    TEXT("\"name\":\"PaddingProbe\",\"strokeWeight\":8,\"strokeAlign\":\"OUTSIDE\",\"strokes\":[{\"type\":\"SOLID\",\"color\":{\"r\":0.5,\"g\":0.5,\"b\":0.5}}],"));
  AddInfo(FString::Printf(TEXT("Runtime padding variant %d (vertical/horizontal, fill/fixed)"), Variant));
  TestTrue(TEXT("Runtime padding fixture completes"), CheckRuntime(CaseJson, Variant == 5 ? 8 : 0));
 }

 return true;
}
#endif
