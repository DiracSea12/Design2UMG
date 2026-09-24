// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetRenderer.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesign2UMGUnpremultiplyPixelsTest,
	"Design2UMG.Render.UnpremultiplyPixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGUnpremultiplyPixelsTest::RunTest(const FString& Parameters)
{
	// Opaque pixels (A=255) must be byte-identical: cases without translucency
	// keep producing the exact same PNG bytes.
	{
		TArray<FColor> Pixels;
		Pixels.Add(FColor(1, 127, 254, 255));
		Pixels.Add(FColor(255, 255, 255, 255));
		FUMGWidgetRenderer::UnpremultiplyPixels(Pixels);

		TestEqual(TEXT("opaque pixel 1 R unchanged"), Pixels[0].R, 1);
		TestEqual(TEXT("opaque pixel 1 G unchanged"), Pixels[0].G, 127);
		TestEqual(TEXT("opaque pixel 1 B unchanged"), Pixels[0].B, 254);
		TestEqual(TEXT("opaque pixel 1 A unchanged"), Pixels[0].A, 255);
		TestEqual(TEXT("opaque white R unchanged"), Pixels[1].R, 255);
		TestEqual(TEXT("opaque white G unchanged"), Pixels[1].G, 255);
		TestEqual(TEXT("opaque white B unchanged"), Pixels[1].B, 255);
	}

	// #fffffff2 serialization: a fully saturated source color at A=242 premultiplies
	// to (242,242,242,242); un-premultiply must restore RGB=255.
	{
		TArray<FColor> Pixels;
		Pixels.Add(FColor(242, 242, 242, 242));
		FUMGWidgetRenderer::UnpremultiplyPixels(Pixels);

		TestEqual(TEXT("#fffffff2 R restored to 255"), Pixels[0].R, 255);
		TestEqual(TEXT("#fffffff2 G restored to 255"), Pixels[0].G, 255);
		TestEqual(TEXT("#fffffff2 B restored to 255"), Pixels[0].B, 255);
		TestEqual(TEXT("#fffffff2 A preserved"), Pixels[0].A, 242);
	}

	// Fully transparent pixels get RGB zeroed (no division by zero).
	{
		TArray<FColor> Pixels;
		Pixels.Add(FColor(100, 150, 200, 0));
		FUMGWidgetRenderer::UnpremultiplyPixels(Pixels);

		TestEqual(TEXT("transparent R zeroed"), Pixels[0].R, 0);
		TestEqual(TEXT("transparent G zeroed"), Pixels[0].G, 0);
		TestEqual(TEXT("transparent B zeroed"), Pixels[0].B, 0);
		TestEqual(TEXT("transparent A preserved"), Pixels[0].A, 0);
	}

	// Clamp boundary: an out-of-range premultiplied state (RGB > A scaled past
	// 255) must clamp to 255 instead of wrapping.
	{
		TArray<FColor> Pixels;
		Pixels.Add(FColor(200, 200, 200, 128)); // 200 * 255 / 128 = 398 -> clamp 255
		FUMGWidgetRenderer::UnpremultiplyPixels(Pixels);

		TestEqual(TEXT("clamped R at 255"), Pixels[0].R, 255);
		TestEqual(TEXT("clamped G at 255"), Pixels[0].G, 255);
		TestEqual(TEXT("clamped B at 255"), Pixels[0].B, 255);
	}

	// Mid-alpha exact values: (128,64,32,128) -> round(x * 255 / 128)
	// = (255, 128, 64).
	{
		TArray<FColor> Pixels;
		Pixels.Add(FColor(128, 64, 32, 128));
		FUMGWidgetRenderer::UnpremultiplyPixels(Pixels);

		TestEqual(TEXT("mid-alpha R exact"), Pixels[0].R, 255);
		TestEqual(TEXT("mid-alpha G exact"), Pixels[0].G, 128);
		TestEqual(TEXT("mid-alpha B exact"), Pixels[0].B, 64);
		TestEqual(TEXT("mid-alpha A preserved"), Pixels[0].A, 128);
	}

	return true;
}

#endif
