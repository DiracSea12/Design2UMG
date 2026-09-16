// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "TextMeasurementRuntimeProbe.h"
#include "Editor.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Subsystems/ImportSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFontFaceMetricReimportTest,
 "Design2UMG.TextMeasurement.FontFaceMetricReimport",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFontFaceMetricReimportTest::RunTest(const FString&)
{
 UImportSubsystem* Imports = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr;
 if (!TestNotNull(TEXT("Import subsystem available"), Imports)) return false;
 // Minimal sfnt table directory for the metric reader; never sent to a renderer.
 auto MakeMetrics = [](uint8 Gap)
 {
  TArray<uint8> Bytes;
  Bytes.SetNumZeroed(80);
  Bytes[1] = 1;
  Bytes[5] = 2;
  Bytes[12] = 'h'; Bytes[13] = 'e'; Bytes[14] = 'a'; Bytes[15] = 'd';
  Bytes[23] = 44;
  Bytes[28] = 'h'; Bytes[29] = 'h'; Bytes[30] = 'e'; Bytes[31] = 'a';
  Bytes[39] = 64;
  Bytes[62] = 3; Bytes[63] = 232; // 1000 units per em.
  Bytes[73] = Gap;
  return FFontFaceData::MakeFontFaceData(MoveTemp(Bytes));
 };
 UFontFace* Face = NewObject<UFontFace>();
 Face->FontFaceData = MakeMetrics(100);
 UFont* Font = NewObject<UFont>();
 Font->FontCacheType = EFontCacheType::Runtime;
 FTypefaceEntry Entry(TEXT("Regular"));
 Entry.Font = FFontData(Face);
 Font->CompositeFont.DefaultTypeface.Fonts.Add(Entry);
 FSlateFontInfo Info(Font, 12, TEXT("Regular"));
 TextMeasurementRuntimeProbe::ClearLineGapEmCache();
 float Gap = 0.0f;
 TestTrue(TEXT("Asset-backed metrics readable"), TextMeasurementRuntimeProbe::TryReadLineGapEm(Info, Gap));
 TestEqual(TEXT("Initial line gap"), Gap, 0.1f);
 Face->FontFaceData = MakeMetrics(200);
 Imports->BroadcastAssetReimport(Face);
 TestTrue(TEXT("Reimported face metrics readable"), TextMeasurementRuntimeProbe::TryReadLineGapEm(Info, Gap));
 TestEqual(TEXT("Font face reimport invalidates cached metrics"), Gap, 0.2f);
 TextMeasurementRuntimeProbe::ClearLineGapEmCache();
 return true;
}
#endif
