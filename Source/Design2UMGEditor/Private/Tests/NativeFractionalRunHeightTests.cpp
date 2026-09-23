// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Framework/Text/SlateTextRun.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/SlateStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/UObjectGlobals.h"
#include "TextureResource.h"
#include "UObject/Package.h"

// Capability probe only. Inflated native descent plus a shared line-height
// factor might represent fractional advances without changing glyph geometry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeFractionalRunHeightTest,
    "Design2UMG.Text.NativeFractionalRunHeight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeFractionalRunHeightTest::RunTest(const FString&)
{
    UFont* Source = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    if (!TestNotNull(TEXT("Source font"), Source)) return false;
    const FSlateFontInfo OriginalFont(Source, 24, TEXT("Regular"));
    const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const int32 NaturalHeight = Measure->GetMaxCharacterHeight(OriginalFont);
    const int32 NaturalAscent = NaturalHeight + Measure->GetBaseline(OriginalFont);
    constexpr float Factor = 0.125f;
    TArray<TStrongObjectPtr<UFont>> Fonts;
    const auto MakeMetricFont = [&](int32 Height) -> UFont*
    {
        UFont* Font = DuplicateObject<UFont>(Source, GetTransientPackage());
        Fonts.Emplace(Font);
        for (FTypefaceEntry& Entry : Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts)
        {
            const UFontFace* SourceFace = Cast<UFontFace>(Entry.Font.GetFontFaceAsset());
            if (!TestNotNull(TEXT("Native face"), SourceFace)) return nullptr;
            UFontFace* Face = DuplicateObject<UFontFace>(SourceFace, Font);
            Face->LayoutMethod = EFontLayoutMethod::Metrics;
            // Keep the original ascent, including its native DPI rounding.
            Face->bIsDescendOverridden = true;
            Face->DescendOverriddenValue = NaturalAscent - Height;
            Entry.Font = FFontData(Face);
        }
        return Font;
    };
    UFont* EqualFont = MakeMetricFont(NaturalHeight * 8);
    UFont* FractionalFont = MakeMetricFont(NaturalHeight * 8 + 3);
    if (!EqualFont || !FractionalFont) return false;
    const FSlateFontInfo EqualInfo(EqualFont, 24, TEXT("Regular"));
    const FSlateFontInfo FractionalInfo(FractionalFont, 24, TEXT("Regular"));
    TestEqual(TEXT("Inflated metrics have requested height"), int32(Measure->GetMaxCharacterHeight(EqualInfo)), NaturalHeight * 8);
    TestEqual(TEXT("Glyph advance is unchanged"), Measure->Measure(TEXT("AV WiWi"), EqualInfo).X, Measure->Measure(TEXT("AV WiWi"), OriginalFont).X);
    const auto Owner = SNew(STextBlock);
    const FTextBlockStyle EqualStyle = FTextBlockStyle().SetFont(EqualInfo).SetColorAndOpacity(FLinearColor::White);
    const FTextBlockStyle FractionalStyle = FTextBlockStyle().SetFont(FractionalInfo).SetColorAndOpacity(FLinearColor::White);
    const auto Layout = FSlateTextLayout::Create(&Owner.Get(), EqualStyle);
    Layout->SetLineHeightPercentage(Factor);
    Layout->SetApplyLineHeightToBottomLine(true);
    TArray<FTextLayout::FNewLineData> Lines;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const auto Text = MakeShared<FString>(TEXT("AV WiWi"));
        TArray<TSharedRef<IRun>> Runs;
        Runs.Add(FSlateTextRun::Create(FRunInfo(), Text, Index == 1 ? FractionalStyle : EqualStyle));
        Lines.Emplace(Text, Runs);
    }
    Layout->AddLines(Lines);
    Layout->UpdateIfNeeded();
    const auto& Views = Layout->GetLineViews();
    if (!TestEqual(TEXT("Three independent lines"), Views.Num(), 3)) return false;
    TestEqual(TEXT("First line height"), Views[0].Size.Y, double(NaturalHeight));
    TestEqual(TEXT("Fractional second line height"), Views[1].Size.Y, double(NaturalHeight) + 0.375);
    TestEqual(TEXT("Fractional cumulative line position"), Views[2].Offset.Y, double(NaturalHeight * 2) + 0.375);
    FWidgetRenderer Renderer(false, true);
    TArray<TArray<FColor>> Images;
    for (int32 Mode = 0; Mode < 2; ++Mode)
    {
        FTextBlockStyle Style = FTextBlockStyle().SetFont(Mode == 0 ? OriginalFont : EqualInfo).SetColorAndOpacity(FLinearColor::White);
        const auto Widget = SNew(SRichTextBlock).Text(FText::FromString(TEXT("AV WiWi\nAV WiWi")))
            .TextStyle(&Style).LineHeightPercentage(Mode == 0 ? 1.f : Factor).ApplyLineHeightToBottomLine(true);
        const FVector2D Size(300.f, 140.f);
        TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size, TF_Nearest, false));
        if (!TestNotNull(TEXT("Render target"), Target.Get())) return false;
        Renderer.DrawWidget(Target.Get(), Widget, 1.f, Size, 0.f);
        FlushRenderingCommands();
        if (!TestTrue(TEXT("Read rendered text"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Images.AddDefaulted_GetRef()))) return false;
    }
    int32 Differences = 0;
    int32 Visible = 0;
    for (int32 Index = 0; Index < Images[0].Num(); ++Index)
    {
        Differences += Images[0][Index] != Images[1][Index];
        Visible += Images[0][Index].A > 0;
    }
    AddInfo(FString::Printf(TEXT("natural_height=%d ascent=%d pixel_differences=%d"), NaturalHeight, NaturalAscent, Differences));
    TestTrue(TEXT("Original contains visible glyphs"), Visible > 50);
    TestEqual(TEXT("Equivalent descent-scaled layout preserves exact pixels"), Differences, 0);
    return true;
}
#endif
