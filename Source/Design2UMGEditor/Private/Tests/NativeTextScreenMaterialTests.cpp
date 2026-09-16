// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"

namespace
{
// Test-only host mirrors SObjectWidget: descendants paint before the WBP paint callback.
class SNativeTextCoordinateProbe : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SNativeTextCoordinateProbe) {}
    SLATE_END_ARGS()
    TArray<TSharedRef<STextBlock>> Texts;
    TArray<UMaterialInstanceDynamic*> Materials;

    void Construct(const FArguments&) { ChildSlot[SNew(SOverlay)]; }
    void SetTexts(const TSharedRef<SWidget>& Content) { ChildSlot[Content]; }

    int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override
    {
        const int32 Result = SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, Layer, Style, bEnabled);
        for (int32 Index = 0; Index < Texts.Num(); ++Index)
        {
            const FGeometry& TextGeometry = Texts[Index]->GetPaintSpaceGeometry();
            const FVector2D Origin = TextGeometry.LocalToAbsolute(FVector2D::ZeroVector);
            const FVector2D X = TextGeometry.LocalToAbsolute(FVector2D(1, 0)) - Origin;
            const FVector2D Y = TextGeometry.LocalToAbsolute(FVector2D(0, 1)) - Origin;
            const double Det = X.X * Y.Y - Y.X * X.Y;
            const double Width = TextGeometry.GetLocalSize().X;
            const FVector2D Row(Y.Y / (Det * Width), -Y.X / (Det * Width));
            Materials[Index]->SetVectorParameterValue(TEXT("PixelToTextX"),
                FLinearColor(Row.X, Row.Y, -FVector2D::DotProduct(Row, Origin), 0));
        }
        return Result;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTextScreenMaterialTest,
    "Design2UMG.Text.NativeFontScreenPosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeTextScreenMaterialTest::RunTest(const FString&)
{
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>());
    Material->MaterialDomain = MD_UI;
    Material->BlendMode = BLEND_Translucent;
    auto* Position = NewObject<UMaterialExpressionScreenPosition>(Material.Get());
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
        Material->GetExpressionCollection().AddExpression(Expression);
    Material->GetEditorOnlyData()->EmissiveColor.Expression = Divide;
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Material->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
    Material->EnsureIsComplete();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTextTransformedMaterialTest,
    "Design2UMG.Text.NativeFontMaterialCurrentPaintGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeTextTransformedMaterialTest::RunTest(const FString&)
{
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>());
    Material->MaterialDomain = MD_UI;
    Material->BlendMode = BLEND_Translucent;
    auto* Position = NewObject<UMaterialExpressionScreenPosition>(Material.Get());
    auto* Mapping = NewObject<UMaterialExpressionVectorParameter>(Material.Get());
    Mapping->ParameterName = TEXT("PixelToTextX");
    auto* Expression = NewObject<UMaterialExpressionCustom>(Material.Get());
    Expression->OutputType = CMOT_Float1;
    Expression->Code = TEXT("return dot(P.xy, M.xy) + M.z;");
    FCustomInput PositionInput;
    PositionInput.InputName = TEXT("P");
    PositionInput.Input.Expression = Position;
    PositionInput.Input.OutputIndex = 1;
    Expression->Inputs.Reset();
    Expression->Inputs.Add(PositionInput);
    FCustomInput MappingInput;
    MappingInput.InputName = TEXT("M");
    MappingInput.Input.Expression = Mapping;
    Expression->Inputs.Add(MappingInput);
    UMaterialExpression* Expressions[] = {Position, Mapping, Expression};
    for (UMaterialExpression* Item : Expressions) Material->GetExpressionCollection().AddExpression(Item);
    Material->GetEditorOnlyData()->EmissiveColor.Expression = Expression;
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Material->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
    Material->EnsureIsComplete();
    const auto Host = SNew(SNativeTextCoordinateProbe);
    const auto Overlay = SNew(SOverlay);
    TArray<TStrongObjectPtr<UMaterialInstanceDynamic>> Instances;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        Instances.Emplace(UMaterialInstanceDynamic::Create(Material.Get(), GetTransientPackage()));
        FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 36);
        Font.FontMaterial = Instances.Last().Get();
        const auto Text = SNew(STextBlock).Text(FText::FromString(TEXT("WiWi"))).Font(Font).ColorAndOpacity(FLinearColor::White);
        Text->SetRenderTransformPivot(FVector2D::ZeroVector);
        Overlay->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top)[Text];
        Host->Texts.Add(Text);
        Host->Materials.Add(Instances.Last().Get());
    }
    Host->SetTexts(Overlay);
    const FVector2D Size(512, 256);
    TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size, TF_Nearest, false));
    FWidgetRenderer Renderer(true);
    for (int32 Frame = 0; Frame < 3; ++Frame)
    {
        for (int32 Index = 0; Index < 2; ++Index)
        {
            const float Scale = 0.8f + 0.2f * (Frame + Index);
            const float Angle = 0.1f * (Frame - Index);
            const FVector2f Offset(40.f + Index * 210.f + Frame * 15.f, 30.f + Index * 110.f);
            Host->Texts[Index]->SetRenderTransform(FSlateRenderTransform(Concatenate(FScale2D(Scale), FQuat2D(Angle), Offset)));
        }
        Renderer.DrawWidget(Target.Get(), Host, 1.f, Size, 0.f);
        FlushRenderingCommands();
        TArray<FColor> Pixels;
        if (!TestTrue(TEXT("Read transformed native text"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return false;
        for (int32 Instance = 0; Instance < 2; ++Instance)
        {
            const FGeometry& Geometry = Host->Texts[Instance]->GetPaintSpaceGeometry();
            int32 Count = 0;
            float MaxError = 0.f;
            for (int32 Index = 0; Index < Pixels.Num(); ++Index)
            {
                if (Pixels[Index].A != 255) continue;
                const FVector2D Local = Geometry.AbsoluteToLocal(FVector2D(Index % 512 + 0.5, Index / 512 + 0.5));
                if (Local.X < 0 || Local.Y < 0 || Local.X >= Geometry.GetLocalSize().X || Local.Y >= Geometry.GetLocalSize().Y) continue;
                const float Expected = Local.X / Geometry.GetLocalSize().X;
                MaxError = FMath::Max(MaxError, FMath::Abs(FLinearColor::FromSRGBColor(Pixels[Index]).R - Expected));
                ++Count;
            }
            TestTrue(TEXT("Each transformed instance is visible"), Count > 100);
            TestTrue(TEXT("Paint-time mapping uses this frame and this instance geometry"), MaxError < 0.01f);
            AddInfo(FString::Printf(TEXT("frame=%d instance=%d pixels=%d max_error=%.6f"), Frame, Instance, Count, MaxError));
        }
    }
    return true;
}
#endif
