// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/SVirtualWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/WidgetRenderer.h"
#include "Input/HittestGrid.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Editor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Minimal replica of the automation screenshot chain (SVirtualWindow +
	// FWidgetRenderer + InitCustomFormat(..., /*bForceLinearGamma*/ true)):
	// renders one widget and reports the center pixel bytes.
	bool RenderWidgetCenterPixel(const TSharedRef<SWidget>& Content, const FIntPoint& Size, FColor& OutPixel)
	{
		TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow);
		Window->SetContent(Content);
		Window->Resize(FVector2D(Size.X, Size.Y));
		Window->SlatePrepass(1.0f);

		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
		RT->Filter = TF_Bilinear;
		RT->ClearColor = FLinearColor::Transparent;
		RT->SRGB = true;
		RT->RenderTargetFormat = RTF_RGBA8;
		RT->InitCustomFormat(Size.X, Size.Y, PF_B8G8R8A8, true);

		FWidgetRenderer Renderer(true);
		Renderer.SetIsPrepassNeeded(false);
		TUniquePtr<FHittestGrid> HitTestGrid = MakeUnique<FHittestGrid>();
		Renderer.DrawWindow(RT, *HitTestGrid, Window, 1.0f, FVector2D(Size.X, Size.Y), 0.1f);
		FlushRenderingCommands();

		FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			RT->ReleaseResource();
			RT->MarkAsGarbage();
			return false;
		}
		TArray<FColor> Pixels;
		if (!Resource->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
		{
			RT->ReleaseResource();
			RT->MarkAsGarbage();
			return false;
		}
		OutPixel = Pixels[(Size.Y / 2) * Size.X + Size.X / 2];

		RT->ReleaseResource();
		RT->MarkAsGarbage();
		return true;
	}

	UTexture2D* MakeSolidTexture(const FColor& Color)
	{
		constexpr int32 TexSize = 8;
		UTexture2D* Texture = UTexture2D::CreateTransient(TexSize, TexSize, PF_B8G8R8A8, NAME_None);
		if (!Texture)
		{
			return nullptr;
		}
		Texture->AddToRoot();
		Texture->SRGB = true;
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->Filter = TF_Nearest;
		Texture->NeverStream = true;

		void* Data = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		TArray<FColor, TInlineAllocator<64>> Solid;
		Solid.Init(Color, TexSize * TexSize);
		FMemory::Memcpy(Data, Solid.GetData(), Solid.Num() * sizeof(FColor));
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
		FlushRenderingCommands();
		return Texture;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRenderGammaProbeSolidBorderTest,
	"Design2UMG.Render.GammaProbeSolidBorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRenderGammaProbeSolidBorderTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("Gamma probe requires a real RHI; skipped under -nullrhi"));
		return true;
	}
	// Known failure guard: this probe documents the screenshot chain's color
	// behavior. A pure #5741BE vertex-color border must come back as the same
	// bytes when the chain is byte-faithful.
	const FColor Source(0x57, 0x41, 0xBE, 0xFF);
	TSharedRef<SBorder> Border = SNew(SBorder)
		.BorderBackgroundColor(FLinearColor(Source))
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill);

	FColor Pixel;
	if (!RenderWidgetCenterPixel(Border, FIntPoint(32, 32), Pixel))
	{
		AddError(TEXT("Gamma probe: solid border render failed"));
		return false;
	}
	UE_LOG(LogTemp, Warning, TEXT("GammaProbe solid border #5741BE -> #%02X%02X%02X"), Pixel.R, Pixel.G, Pixel.B);
	TestEqual(TEXT("Solid border red byte round-trips"), Pixel.R, Source.R);
	TestEqual(TEXT("Solid border green byte round-trips"), Pixel.G, Source.G);
	TestEqual(TEXT("Solid border blue byte round-trips"), Pixel.B, Source.B);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRenderGammaProbeTextureImageTest,
	"Design2UMG.Render.GammaProbeTextureImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRenderGammaProbeTextureImageTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("Gamma probe requires a real RHI; skipped under -nullrhi"));
		return true;
	}
	// A transient sRGB-flagged texture holding #5741BE bytes, drawn through an
	// SImage on the same screenshot chain. Bytes must round-trip for the path
	// raster pipeline (which writes sRGB bytes) to be faithful.
	const FColor Source(0x57, 0x41, 0xBE, 0xFF);
	UTexture2D* Texture = MakeSolidTexture(Source);
	if (!Texture)
	{
		AddError(TEXT("Gamma probe: texture creation failed"));
		return false;
	}

	FSlateImageBrush Brush(Texture, FVector2D(32.0f, 32.0f));
	TSharedRef<SImage> Image = SNew(SImage).Image(&Brush);

	FColor Pixel;
	const bool bRendered = RenderWidgetCenterPixel(Image, FIntPoint(32, 32), Pixel);
	Texture->RemoveFromRoot();
	if (!bRendered)
	{
		AddError(TEXT("Gamma probe: texture image render failed"));
		return false;
	}
	UE_LOG(LogTemp, Warning, TEXT("GammaProbe texture image #5741BE -> #%02X%02X%02X"), Pixel.R, Pixel.G, Pixel.B);
	TestEqual(TEXT("Texture image red byte round-trips"), Pixel.R, Source.R);
	TestEqual(TEXT("Texture image green byte round-trips"), Pixel.G, Source.G);
	TestEqual(TEXT("Texture image blue byte round-trips"), Pixel.B, Source.B);
	return true;
}

#endif
