// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Design2UMGFontAccess.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_VERSION_OLDER_THAN(5, 3, 0)
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/SlateTextLayout.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "TextureResource.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTextMetricsTest,
    "Design2UMG.Text.NativeFontFaceRunMetrics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeTextMetricsTest::RunTest(const FString&)
{
    UFont* Source = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    if (!TestNotNull(TEXT("Native font asset"), Source)) return false;
    const FString PointerPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG/NativeTextProbe/metric-font-path.txt");
    const bool bReload = FPlatformMisc::GetEnvironmentVariable(TEXT("D2U_NATIVE_METRIC_RELOAD")) == TEXT("1");
    TStrongObjectPtr<UFont> Adjusted;
    if (bReload)
    {
        FString AssetPath;
        if (!TestTrue(TEXT("Read persisted font path"), FFileHelper::LoadFileToString(AssetPath, *PointerPath))) return false;
        Adjusted.Reset(LoadObject<UFont>(nullptr, *AssetPath));
        if (!TestNotNull(TEXT("Reload native font asset"), Adjusted.Get())) return false;
    }
    else
    {
        const FString PackageName = TEXT("/Game/Design2UMG/NativeTextProbe/Metrics_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Adjusted.Reset(DuplicateObject<UFont>(Source, CreatePackage(*PackageName), TEXT("MetricFont")));
        Adjusted->SetFlags(RF_Public | RF_Standalone);
    }
    int32 FaceCount = 0;
    for (FTypefaceEntry& Entry : Design2UMGFontAccess::GetMutableCompositeFont(*Adjusted).DefaultTypeface.Fonts)
    {
        const UFontFace* SourceFace = Cast<UFontFace>(Entry.Font.GetFontFaceAsset());
        if (!TestNotNull(TEXT("Persistent face asset"), SourceFace)) return false;
        if (bReload)
        {
            TestTrue(TEXT("Native metric override survives loading"), SourceFace->bIsAscendOverridden && SourceFace->bIsDescendOverridden);
            ++FaceCount;
            continue;
        }
        UFontFace* Face = DuplicateObject<UFontFace>(SourceFace, Adjusted.Get());
        Face->LayoutMethod = EFontLayoutMethod::Metrics;
        Face->bIsAscendOverridden = true;
        Face->AscendOverriddenValue = 48;
        Face->bIsDescendOverridden = true;
        Face->DescendOverriddenValue = -16;
        Entry.Font = FFontData(Face);
        ++FaceCount;
    }
    TestTrue(TEXT("Separate native font contains metric faces"), FaceCount > 0);
    const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const TSharedRef<const FString> Text = MakeShared<FString>(TEXT("WiWi"));
    for (const int32 Size : {24, 48})
    {
        const FSlateFontInfo OriginalFont(Source, Size, TEXT("Regular"));
        const FSlateFontInfo AdjustedFont(Adjusted.Get(), Size, TEXT("Regular"));
        const auto OriginalRun = FSlateTextRun::Create(FRunInfo(), Text,
            FTextBlockStyle().SetFont(OriginalFont));
        const auto AdjustedRun = FSlateTextRun::Create(FRunInfo(), Text,
            FTextBlockStyle().SetFont(AdjustedFont));
        const int16 OriginalHeight = OriginalRun->GetMaxHeight(1.f);
        const int16 AdjustedHeight = AdjustedRun->GetMaxHeight(1.f);
        TestTrue(TEXT("Per-run font metrics affect native layout height"), AdjustedHeight > OriginalHeight);
        TestEqual(TEXT("Glyph advance stays unchanged"),
            Measure->Measure(*Text, AdjustedFont).X, Measure->Measure(*Text, OriginalFont).X);
        AddInfo(FString::Printf(TEXT("size=%d original_height=%d adjusted_height=%d original_baseline=%d adjusted_baseline=%d"),
            Size, OriginalHeight, AdjustedHeight, OriginalRun->GetBaseLine(1.f), AdjustedRun->GetBaseLine(1.f)));

        TArray<TArray<uint8>> GlyphMasks;
        TArray<FIntPoint> GlyphBounds;
        for (const FSlateFontInfo& Font : {OriginalFont, AdjustedFont})
        {
            const FVector2D TargetSize(512, 256);
            const auto Widget = SNew(STextBlock).Text(FText::FromString(*Text)).Font(Font).ColorAndOpacity(FLinearColor::White);
            TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(TargetSize, TF_Nearest, true));
            FWidgetRenderer Renderer(true);
            Renderer.DrawWidget(Target.Get(), Widget, 1.f, TargetSize, 0.f);
            FlushRenderingCommands();
            TArray<FColor> Pixels;
            if (!TestTrue(TEXT("Read native font render"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return false;
            int32 MinX = 512, MinY = 256, MaxX = -1, MaxY = -1;
            for (int32 Y = 0; Y < 256; ++Y)
            {
                for (int32 X = 0; X < 512; ++X)
                {
                    if (Pixels[Y * 512 + X].A > 0)
                    {
                        MinX = FMath::Min(MinX, X); MaxX = FMath::Max(MaxX, X);
                        MinY = FMath::Min(MinY, Y); MaxY = FMath::Max(MaxY, Y);
                    }
                }
            }
            if (!TestTrue(TEXT("Rendered glyph is visible and not clipped"), MaxX >= MinX && MinY > 0 && MaxX < 511 && MaxY < 255)) return false;
            TArray<uint8>& Mask = GlyphMasks.AddDefaulted_GetRef();
            GlyphBounds.Emplace(MaxX - MinX + 1, MaxY - MinY + 1);
            // Compare only generated test renders, normalized for their changed baseline.
            for (int32 Y = MinY; Y <= MaxY; ++Y)
                for (int32 X = MinX; X <= MaxX; ++X)
                    Mask.Add(Pixels[Y * 512 + X].A);
            AddInfo(FString::Printf(TEXT("glyph size=%d bounds=%d,%d..%d,%d"), Size, MinX, MinY, MaxX, MaxY));
        }
        TestTrue(TEXT("Changing native metrics preserves glyph alpha exactly after translation"),
            GlyphBounds[0] == GlyphBounds[1] && GlyphMasks[0] == GlyphMasks[1]);

        for (const float WrapWidth : {90.f, 180.f, 360.f})
        {
            const TSharedRef<STextBlock> Owner = SNew(STextBlock);
            TArray<TSharedRef<FSlateTextLayout>> Layouts;
            for (const bool bAdjustMiddle : {false, true})
            {
                const FTextBlockStyle Base = FTextBlockStyle().SetFont(OriginalFont);
                const FTextBlockStyle Middle = FTextBlockStyle().SetFont(bAdjustMiddle ? AdjustedFont : OriginalFont);
                const auto Layout = FSlateTextLayout::Create(&Owner.Get(), Base);
                Layout->SetWrappingWidth(WrapWidth);
                const TSharedRef<FString> Mixed = MakeShared<FString>(TEXT("Wi Wi Wi Wi Wi Wi"));
                TArray<TSharedRef<IRun>> Runs;
                Runs.Add(FSlateTextRun::Create(FRunInfo(), Mixed, Base, FTextRange(0, 6)));
                Runs.Add(FSlateTextRun::Create(FRunInfo(), Mixed, Middle, FTextRange(6, 11)));
                Runs.Add(FSlateTextRun::Create(FRunInfo(), Mixed, Base, FTextRange(11, Mixed->Len())));
                Layout->AddLine(FTextLayout::FNewLineData(Mixed, MoveTemp(Runs)));
                const TSharedRef<FString> Plain = MakeShared<FString>(TEXT("Wi"));
                TArray<TSharedRef<IRun>> PlainRuns;
                PlainRuns.Add(FSlateTextRun::Create(FRunInfo(), Plain, Base));
                Layout->AddLine(FTextLayout::FNewLineData(Plain, MoveTemp(PlainRuns)));
                Layout->UpdateIfNeeded();
                Layouts.Add(Layout);
            }
            const auto& OriginalLines = Layouts[0]->GetLineViews();
            const auto& AdjustedLines = Layouts[1]->GetLineViews();
            if (!TestEqual(TEXT("Changing metrics preserves wrapped line count"), AdjustedLines.Num(), OriginalLines.Num())) return false;
            int32 TallerLines = 0;
            for (int32 Index = 0; Index < OriginalLines.Num(); ++Index)
            {
                const auto& Before = OriginalLines[Index];
                const auto& After = AdjustedLines[Index];
                TestEqual(TEXT("Wrapped range start unchanged"), After.Range.BeginIndex, Before.Range.BeginIndex);
                TestEqual(TEXT("Wrapped range end unchanged"), After.Range.EndIndex, Before.Range.EndIndex);
                TestEqual(TEXT("Wrapped line width unchanged"), After.Size.X, Before.Size.X);
                const bool bContainsAdjustedRun = After.ModelIndex == 0 && After.Range.BeginIndex < 11 && After.Range.EndIndex > 6;
                if (bContainsAdjustedRun)
                {
                    TestTrue(TEXT("Only mixed lines acquire increased height"), After.Size.Y > Before.Size.Y);
                    ++TallerLines;
                }
                else
                {
                    TestEqual(TEXT("Unchanged runs retain original line height"), After.Size.Y, Before.Size.Y);
                }
            }
            TestTrue(TEXT("Adjusted run appears in the mixed layout"), TallerLines > 0);
            AddInfo(FString::Printf(TEXT("mixed size=%d wrap=%.0f lines=%d taller_lines=%d"), Size, WrapWidth, AdjustedLines.Num(), TallerLines));
        }
    }
    if (!bReload)
    {
        const FString Filename = FPackageName::LongPackageNameToFilename(Adjusted->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        if (!TestTrue(TEXT("Save native metric font"), UPackage::SavePackage(Adjusted->GetOutermost(), Adjusted.Get(), *Filename, Args))) return false;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(PointerPath), true);
        TestTrue(TEXT("Record native font path for fresh process"), FFileHelper::SaveStringToFile(Adjusted->GetPathName(), *PointerPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
    return true;
}
#endif
