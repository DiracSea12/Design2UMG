// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGMaterialAccess.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "Design2UMGEffectMaterialBuilder.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Input/HittestGrid.h"
#if UE_VERSION_OLDER_THAN(5, 2, 0)
#include "MaterialShared.h"
#else
#include "MaterialDomain.h"
#endif
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SVirtualWindow.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	constexpr int32 TextureSize = 32;

	UTexture2D* CreateHalfFilledTexture(uint8 Alpha)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(TextureSize, TextureSize, PF_B8G8R8A8);
		if (!Texture) return nullptr;
		Texture->SRGB = true;
		Texture->Filter = TF_Nearest;
		Texture->NeverStream = true;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		FColor* Pixels = static_cast<FColor*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < TextureSize; ++Y)
		{
			for (int32 X = 0; X < TextureSize; ++X)
			{
				Pixels[Y * TextureSize + X] = FColor(0, 0, 0, X >= TextureSize / 2 ? Alpha : 0);
			}
		}
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UMaterial* CreateConstantMaterial(const FLinearColor& PremultipliedColor)
	{
		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		Material->MaterialDomain = MD_UI;
		Material->BlendMode = BLEND_AlphaComposite;
		Material->SetShadingModel(MSM_Unlit);
		auto* Color = NewObject<UMaterialExpressionConstant3Vector>(Material);
		Color->Constant = PremultipliedColor;
		Design2UMGMaterialAccess::AddExpression(*Material, *Color);
		auto* Opacity = NewObject<UMaterialExpressionConstant>(Material);
		Opacity->R = PremultipliedColor.A;
		Design2UMGMaterialAccess::AddExpression(*Material, *Opacity);
		Material->GetExpressionInputForProperty(MP_EmissiveColor)[0].Expression = Color;
		Material->GetExpressionInputForProperty(MP_Opacity)[0].Expression = Opacity;
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Design2UMGMaterialAccess::CacheShadersForRendering(*Material);
		Design2UMGMaterialAccess::WaitForCompilation(*Material);
		return Material;
	}

	bool RenderMaterialCenter(UMaterialInterface* Material, FColor& OutPixel)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.ImageSize = FVector2D(TextureSize, TextureSize);
		Brush.SetResourceObject(Material);
		TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow);
		Window->SetContent(SNew(SImage).Image(&Brush));
		Window->Resize(FVector2D(TextureSize, TextureSize));
		Window->SlatePrepass(1.0f);
		TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
		Target->ClearColor = FLinearColor::Transparent;
		Target->InitCustomFormat(TextureSize, TextureSize, PF_B8G8R8A8, true);
		FWidgetRenderer Renderer(true);
		Renderer.SetIsPrepassNeeded(false);
		FHittestGrid HitTestGrid;
		Renderer.DrawWindow(Target.Get(), HitTestGrid, Window, 1.0f, FVector2D(TextureSize, TextureSize), 0.1f);
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		const bool bRead = Resource && Resource->ReadPixels(Pixels) && Pixels.Num() == TextureSize * TextureSize;
		if (bRead) OutPixel = Pixels[(TextureSize / 2) * TextureSize + TextureSize / 2];
		Target->ReleaseResource();
		FlushRenderingCommands();
		return bRead;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerShadowOpacityRenderingTest,
	"Design2UMG.Shadow.InnerShadowOpacityRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInnerShadowOpacityRenderingTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddError(TEXT("Inner shadow rendering requires a real graphics backend"));
		return false;
	}
	UMaterialInterface* Parent = Design2UMGEffectMaterialBuilder::GetOrCreateInnerShadowRetainerMaterial();
	if (!TestNotNull(TEXT("Inner shadow material"), Parent)) return false;
	constexpr float Strength = 0.25f;
	for (uint8 Alpha : {uint8(255), uint8(128)})
	{
		TStrongObjectPtr<UTexture2D> Texture(CreateHalfFilledTexture(Alpha));
		if (!TestNotNull(TEXT("Half-filled source texture"), Texture.Get())) return false;
		TStrongObjectPtr<UMaterialInstanceDynamic> Effect(UMaterialInstanceDynamic::Create(Parent, GetTransientPackage()));
		Effect->SetTextureParameterValue(TEXT("Texture"), Texture.Get());
		Effect->SetVectorParameterValue(TEXT("BlurUVStep"), FLinearColor(0.00001f, 0.00001f, 0.0f, 0.0f));
		Effect->SetVectorParameterValue(TEXT("InnerShadowOffset"), FLinearColor(0.5f, 0.0f, 0.0f, 0.0f));
		Effect->SetVectorParameterValue(TEXT("InnerShadowColor"), FLinearColor::Red);
		Effect->SetScalarParameterValue(TEXT("InnerShadowStrength"), Strength);
		const float Opacity = Alpha / 255.0f;
		// The displaced sample is transparent; normal blending clips the shadow to the source alpha.
		TStrongObjectPtr<UMaterial> Expected(CreateConstantMaterial(FLinearColor(Opacity * Opacity * Strength, 0.0f, 0.0f, Opacity)));
		FColor ActualPixel, ExpectedPixel;
		if (!TestTrue(TEXT("Render inner shadow"), RenderMaterialCenter(Effect.Get(), ActualPixel))
			|| !TestTrue(TEXT("Render independently calculated color"), RenderMaterialCenter(Expected.Get(), ExpectedPixel))) return false;
		AddInfo(FString::Printf(TEXT("alpha=%u actual=%s expected=%s"), Alpha, *ActualPixel.ToString(), *ExpectedPixel.ToString()));
		TestTrue(TEXT("Inner shadow respects source opacity and premultiplied color"),
			FMath::Abs(int32(ActualPixel.R) - int32(ExpectedPixel.R)) <= 2
			&& FMath::Abs(int32(ActualPixel.G) - int32(ExpectedPixel.G)) <= 2
			&& FMath::Abs(int32(ActualPixel.B) - int32(ExpectedPixel.B)) <= 2
			&& FMath::Abs(int32(ActualPixel.A) - int32(ExpectedPixel.A)) <= 2);
	}
	FlushRenderingCommands();
	return true;
}
#endif
