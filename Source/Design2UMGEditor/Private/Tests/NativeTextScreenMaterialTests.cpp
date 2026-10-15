// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Design2UMGMaterialAccess.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionDivide.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Text/STextBlock.h"
#if UE_VERSION_OLDER_THAN(5, 2, 0)
#include "MaterialShared.h"
#else
#include "MaterialDomain.h"
#endif
#include "TextureResource.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTextScreenMaterialTest,
    "Design2UMG.Text.NativeFontScreenPosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeTextScreenMaterialTest::RunTest(const FString&)
{
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>());
    Material->MaterialDomain = MD_UI;
    Material->BlendMode = BLEND_Translucent;
    UClass* ScreenPositionClass = LoadClass<UMaterialExpression>(nullptr, TEXT("/Script/Engine.MaterialExpressionScreenPosition"));
    if (!TestNotNull(TEXT("Engine ScreenPosition expression class exists"), ScreenPositionClass)) return false;
    auto* Position = NewObject<UMaterialExpression>(Material.Get(), ScreenPositionClass);
    auto* Mask = NewObject<UMaterialExpressionComponentMask>(Material.Get());
    Mask->Input.Expression = Position;
    Mask->Input.OutputIndex = 1; // Native pixel position, not viewport UV.
    Mask->R = true;
    Mask->G = Mask->B = Mask->A = false;
    auto* Divide = NewObject<UMaterialExpressionDivide>(Material.Get());
    Divide->A.Expression = Mask;
    Divide->ConstB = 320.f;
    UMaterialExpression* Expressions[] = {Position, Mask, Divide};
    for (UMaterialExpression* Expression : Expressions)
        Design2UMGMaterialAccess::AddExpression(*Material, *Expression);
    Material->GetExpressionInputForProperty(MP_EmissiveColor)[0].Expression = Divide;
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Design2UMGMaterialAccess::CacheShadersForRendering(*Material);
    Design2UMGMaterialAccess::WaitForCompilation(*Material);
    FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 48);
    Font.FontMaterial = Material.Get();
    for (const FString& Text : {FString(TEXT("WiWi")), FString(TEXT("Wi\nWi"))})
    {
        const auto Widget = SNew(STextBlock).Text(FText::FromString(Text)).Font(Font).ColorAndOpacity(FLinearColor::White);
        const FVector2D Size(320, 180);
        // The Slate shader performs gamma encoding. Use a linear target to avoid
        // applying a second hardware sRGB conversion to the same material output.
        TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size, TF_Nearest, false));
        FWidgetRenderer Renderer(true);
        Renderer.DrawWidget(Target.Get(), Widget, 1.f, Size, 0.f);
        FlushRenderingCommands();
        TArray<FColor> Pixels;
        if (!TestTrue(TEXT("Read native screen-position text"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return false;
        double SumError = 0.0;
        int32 Count = 0;
        float MaxError = 0.f;
        TSet<int32> SampledColumns;
        for (int32 Index = 0; Index < Pixels.Num(); ++Index)
        {
            if (Pixels[Index].A != 255) continue;
            const float Expected = ((Index % 320) + 0.5f) / 320.f;
            const float Error = FMath::Abs(FLinearColor::FromSRGBColor(Pixels[Index]).R - Expected);
            const int32 X = Index % 320;
            if ((X == 16 || X == 32 || X == 64) && !SampledColumns.Contains(X))
            {
                SampledColumns.Add(X);
                AddInfo(FString::Printf(TEXT("sample x=%d raw=%d decoded=%.6f expected=%.6f"),
                    X, Pixels[Index].R, FLinearColor::FromSRGBColor(Pixels[Index]).R, Expected));
            }
            MaxError = FMath::Max(MaxError, Error);
            SumError += Error;
            ++Count;
        }
        TestTrue(TEXT("Opaque glyph pixels exist"), Count > 100);
        TestTrue(TEXT("Native material follows physical horizontal pixels within 8-bit readback precision"), MaxError < 0.01f);
        AddInfo(FString::Printf(TEXT("multiline=%d pixels=%d mean_error=%.6f max_error=%.6f"), Text.Contains(TEXT("\n")), Count, Count ? SumError / Count : 0.0, MaxError));
    }
    return true;
}

#endif
