// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGFontAccess.h"
#include "Design2UMGFontMetrics.h"
#include "Editor.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Fonts/SlateFontInfo.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/ImportSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFontFaceMetricReimportTest,
	"Design2UMG.TextMeasurement.FontFaceMetricReimport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFontFaceMetricReimportTest::RunTest(const FString&)
{
	UImportSubsystem* Imports = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("Import subsystem available"), Imports)) return false;

	Design2UMGFontMetrics::ClearCache();
	ON_SCOPE_EXIT { Design2UMGFontMetrics::ClearCache(); };

	// Minimal sfnt table directory for the metric reader; never sent to a renderer.
	const auto MakeMetrics = [](int16 Gap)
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
		Bytes[72] = static_cast<uint8>(Gap >> 8);
		Bytes[73] = static_cast<uint8>(Gap);
		return FFontFaceData::MakeFontFaceData(MoveTemp(Bytes));
	};
	UFont* Font = NewObject<UFont>();
	UFontFace* Face = NewObject<UFontFace>(Font);
	Face->FontFaceData = MakeMetrics(100);
	Font->FontCacheType = EFontCacheType::Runtime;
	FTypefaceEntry Entry(TEXT("Regular"));
	Entry.Font = FFontData(Face);
	Design2UMGFontAccess::GetMutableCompositeFont(*Font).DefaultTypeface.Fonts.Add(Entry);
	FSlateFontInfo Info(Font, 12, TEXT("Regular"));
	float Gap = 0.0f;
	TestTrue(TEXT("Asset-backed metrics readable"), Design2UMGFontMetrics::TryReadLineGapEm(Info, Gap));
	TestEqual(TEXT("Initial line gap"), Gap, 0.1f);

	Face->FontFaceData = MakeMetrics(200);
	TestTrue(TEXT("Cached metrics readable"), Design2UMGFontMetrics::TryReadLineGapEm(Info, Gap));
	TestEqual(TEXT("Backing data changes alone do not invalidate the cache"), Gap, 0.1f);
	Imports->BroadcastAssetReimport(NewObject<UTexture2D>());
	TestTrue(TEXT("Metrics remain cached after unrelated imports"), Design2UMGFontMetrics::TryReadLineGapEm(Info, Gap));
	TestEqual(TEXT("Non-font reimports leave the cache intact"), Gap, 0.1f);

	Imports->BroadcastAssetReimport(Face);
	TestTrue(TEXT("Reimported face metrics readable"), Design2UMGFontMetrics::TryReadLineGapEm(Info, Gap));
	TestEqual(TEXT("Font face reimport invalidates cached metrics"), Gap, 0.2f);

	Face->FontFaceData = MakeMetrics(300);
	Imports->BroadcastAssetReimport(Font);
	TestTrue(TEXT("Reimported composite font metrics readable"), Design2UMGFontMetrics::TryReadLineGapEm(Info, Gap));
	TestEqual(TEXT("Composite font reimport invalidates cached metrics"), Gap, 0.3f);

	Face->FontFaceData = FFontFaceData::MakeFontFaceData(TArray<uint8>{0});
	Imports->BroadcastAssetReimport(Face);
	TestFalse(TEXT("Invalid font metrics are reported"), Design2UMGFontMetrics::TryReadLineGapEm(Info, Gap));
	Face->FontFaceData = MakeMetrics(400);
	TestFalse(TEXT("Unavailable metrics are cached until reimport"), Design2UMGFontMetrics::TryReadLineGapEm(Info, Gap));
	Imports->BroadcastAssetReimport(Face);
	TestTrue(TEXT("Reimport retries previously unavailable metrics"), Design2UMGFontMetrics::TryReadLineGapEm(Info, Gap));
	TestEqual(TEXT("Recovered line gap"), Gap, 0.4f);
	return true;
}
#endif
