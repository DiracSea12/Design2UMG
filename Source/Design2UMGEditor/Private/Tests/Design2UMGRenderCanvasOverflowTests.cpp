// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportGeneration.h"
#include "UMGGenerator.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#include "Blueprint/UserWidget.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

namespace
{
	const FLinearColor RootFillColor(1.0f, 0.0f, 0.0f, 1.0f);

	bool ReadPngPixels(const FString& PngPath, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *PngPath))
		{
			return false;
		}

		IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()))
		{
			return false;
		}

		TArray64<uint8> RawData;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
		{
			return false;
		}

		OutWidth = Wrapper->GetWidth();
		OutHeight = Wrapper->GetHeight();
		OutPixels.SetNumUninitialized(OutWidth * OutHeight);
		FMemory::Memcpy(OutPixels.GetData(), RawData.GetData(), OutPixels.Num() * sizeof(FColor));
		return true;
	}

	FColor PixelAt(const TArray<FColor>& Pixels, int32 Width, int32 X, int32 Y)
	{
		return Pixels[Y * Width + X];
	}

	bool IsFillColor(const FColor& Pixel)
	{
		return Pixel.A == 255 && Pixel.R == 255 && Pixel.G == 0 && Pixel.B == 0;
	}

	bool IsTransparent(const FColor& Pixel)
	{
		return Pixel.A == 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGRenderCanvasOverflowBackgroundTest,
	"Design2UMG.Generation.RenderCanvasOverflowBackgroundBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGRenderCanvasOverflowBackgroundTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_RenderCanvasOverflow");
	Data.RootWidget.Type = TEXT("Border");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");
	Data.RootWidget.Bounds.W = 96.0f;
	Data.RootWidget.Bounds.H = 64.0f;
	Data.RootWidget.Brush.BrushColor = RootFillColor;

	const FString OutputPath = TEXT("/Game/Design2UMG/Automation/RenderCanvasOverflow");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* WidgetBP = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("Blueprint is built for the canvas overflow contract"), WidgetBP))
	{
		return false;
	}

	// BuildWidgetBlueprint only assembles the template tree; the compiled render
	// path instantiates the generated class, so compile first like the real save flow.
	FKismetEditorUtilities::CompileBlueprint(WidgetBP, EBlueprintCompileOptions::None);
	if (!TestTrue(TEXT("Compiled WBP has a generated class"), WidgetBP->GeneratedClass != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const auto RenderAndRead = [this, &WidgetBP](const TCHAR* Label, const TOptional<FVector2D>& Override, TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
	{
		const FString PngPath = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("D2UCanvasOverflow"), TEXT(".png"));
		FIntPoint RenderedSize = FIntPoint::ZeroValue;
		FString Error;
		const bool bRendered = DesignImportGeneration::RenderCompiledBlueprintToPngFile(WidgetBP, PngPath, &RenderedSize, &Error, Override);
		const bool bRead = bRendered && ReadPngPixels(PngPath, OutPixels, OutW, OutH);
		if (!bRead)
		{
			AddError(FString::Printf(TEXT("%s: render/read failed (%s)"), Label, *Error));
		}
		else
		{
			const FColor& Corner = OutPixels[0];
			const FColor& Center = OutPixels[OutH / 2 * OutW + OutW / 2];
			const FColor& Last = OutPixels.Last();
			AddInfo(FString::Printf(
				TEXT("%s: %dx%d corner=(%d,%d,%d,%d) center=(%d,%d,%d,%d) last=(%d,%d,%d,%d)"),
				Label, OutW, OutH,
				Corner.R, Corner.G, Corner.B, Corner.A,
				Center.R, Center.G, Center.B, Center.A,
				Last.R, Last.G, Last.B, Last.A));
		}
		IFileManager::Get().Delete(*PngPath);
		return bRead;
	};

	// Pencil reference canvases cover the rendered content union box, which can
	// be larger than the root node's declared bounds. The root fill must stay
	// inside the declared bounds; the rest of the calibrated canvas stays transparent.
	{
		TArray<FColor> Pixels;
		int32 W = 0;
		int32 H = 0;
		if (RenderAndRead(TEXT("both-axis overflow"), FVector2D(200, 100), Pixels, W, H))
		{
			TestTrue(TEXT("both-axis overflow: canvas is the calibrated extent"), W == 200 && H == 100);
			// Centered placement of the 96x64 root on a 200x100 canvas: x[52,147] y[18,81].
			TestTrue(TEXT("both-axis overflow: center pixel keeps the root fill"), IsFillColor(PixelAt(Pixels, W, 100, 50)));
			TestTrue(TEXT("both-axis overflow: root edge pixel keeps the fill"), IsFillColor(PixelAt(Pixels, W, 60, 25)));
			TestTrue(TEXT("both-axis overflow: top-left corner outside root bounds is transparent"), IsTransparent(PixelAt(Pixels, W, 5, 5)));
			TestTrue(TEXT("both-axis overflow: bottom-right corner outside root bounds is transparent"), IsTransparent(PixelAt(Pixels, W, 195, 95)));
			TestTrue(TEXT("both-axis overflow: right margin outside root bounds is transparent"), IsTransparent(PixelAt(Pixels, W, 170, 50)));
		}
	}
	{
		TArray<FColor> Pixels;
		int32 W = 0;
		int32 H = 0;
		if (RenderAndRead(TEXT("single-axis overflow"), FVector2D(200, 64), Pixels, W, H))
		{
			TestTrue(TEXT("single-axis overflow: canvas is the calibrated extent"), W == 200 && H == 64);
			// One-axis overflow keeps the top-left anchoring: root covers x[0,95] y[0,63].
			TestTrue(TEXT("single-axis overflow: top-left pixel keeps the root fill"), IsFillColor(PixelAt(Pixels, W, 5, 5)));
			TestTrue(TEXT("single-axis overflow: pixel inside root bounds keeps the fill"), IsFillColor(PixelAt(Pixels, W, 90, 60)));
			TestTrue(TEXT("single-axis overflow: area right of root bounds is transparent"), IsTransparent(PixelAt(Pixels, W, 150, 30)));
			TestTrue(TEXT("single-axis overflow: far-right margin is transparent"), IsTransparent(PixelAt(Pixels, W, 195, 5)));
		}
	}
	{
		// Without calibration, the root fills its design-time canvas.
		TArray<FColor> Pixels;
		int32 W = 0;
		int32 H = 0;
		if (RenderAndRead(TEXT("no override"), TOptional<FVector2D>(), Pixels, W, H))
		{
			TestTrue(TEXT("no override: render size is the design-time extent"), W == 96 && H == 64);
			TestTrue(TEXT("no override: root fill covers the canvas"), IsFillColor(PixelAt(Pixels, W, 5, 5)) && IsFillColor(PixelAt(Pixels, W, 90, 60)));
		}
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

#endif
