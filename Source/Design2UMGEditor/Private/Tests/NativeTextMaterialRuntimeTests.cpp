// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGMaterialAccess.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#if UE_VERSION_OLDER_THAN(5, 2, 0)
#include "MaterialShared.h"
#else
#include "MaterialDomain.h"
#endif
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Slate/SRetainerWidget.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Misc/ScopeExit.h"
#include "Styling/SlateStyle.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/RetainerBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/RichTextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/Font.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "TextureResource.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "NativeTextAnimation.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/UMGSequenceTickManager.h"
#include "Components/TextBlock.h"
#include "MovieScene.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Animation/MovieSceneMarginTrack.h"
#include "Animation/MovieSceneMarginSection.h"
#include "Tracks/MovieSceneIntegerTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneIntegerSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"
#include "Engine/LatentActionManager.h"
#include "UObject/UnrealType.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Sections/MovieSceneEventRepeaterSection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTextCanvasOffsetRuntimeTest,
    "Design2UMG.Text.NativeCanvasOffsetRuntime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeTextCanvasOffsetRuntimeTest::RunTest(const FString&)
{
    UPackage* Package = GetTransientPackage();
    const TStrongObjectPtr<UWidgetBlueprint> Blueprint(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), Package, MakeUniqueObjectName(Package, UWidgetBlueprint::StaticClass(), TEXT("TextOffset")),
        BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
    if (!TestNotNull(TEXT("Native offset blueprint exists"), Blueprint.Get())) return false;
    auto* Canvas = Blueprint->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
    auto* Text = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label"));
    Text->SetText(FText::FromString(TEXT("Editable")));
    Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 24));
    Text->bIsVariable = true;
    Blueprint->WidgetTree->RootWidget = Canvas;
    auto* Slot = Canvas->AddChildToCanvas(Text);
    Slot->SetAutoSize(true);
    Slot->SetOffsets(FMargin(10, 12, 0, 0));
    auto* Animation = UMGPrototypeAnimationBuilder::CreateAnimation(Blueprint.Get(), TEXT("OffsetMotion"), 1.0f);
    const FGuid Guid = Animation->MovieScene->AddPossessable(TEXT("Label Slot"), UCanvasPanelSlot::StaticClass());
    FWidgetAnimationBinding Binding;
    Binding.AnimationGuid = Guid;
    Binding.WidgetName = Text->GetFName();
    Binding.SlotWidgetName = Canvas->GetFName();
    Animation->AnimationBindings.Add(Binding);
    auto* Track = Animation->MovieScene->AddTrack<UMovieSceneMarginTrack>(Guid);
    Track->SetPropertyNameAndPath(TEXT("Offsets"), TEXT("LayoutData.Offsets"));
    auto* Section = CastChecked<UMovieSceneMarginSection>(Track->CreateNewSection());
    Section->SetRange(Animation->MovieScene->GetPlaybackRange());
    const FFrameNumber End(static_cast<int32>(Animation->MovieScene->GetTickResolution().AsDecimal()));
    Section->LeftCurve.AddLinearKey(0,10); Section->LeftCurve.AddLinearKey(End,60);
    Section->TopCurve.AddLinearKey(0,12); Section->TopCurve.AddLinearKey(End,12);
    Section->RightCurve.AddLinearKey(0,0); Section->RightCurve.AddLinearKey(End,0);
    Section->BottomCurve.AddLinearKey(0,0); Section->BottomCurve.AddLinearKey(End,0);
    Track->AddSection(*Section);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint.Get());
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    if (!TestTrue(TEXT("Native offset blueprint compiles"), Blueprint->Status != BS_Error)) return false;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("Editor world exists"), World) || !FSlateApplication::IsInitialized()) return false;
    const TStrongObjectPtr<UUserWidget> Instance(CreateWidget<UUserWidget>(World, Blueprint->GeneratedClass.Get()));
    if (!TestNotNull(TEXT("Native offset instance exists"), Instance.Get())) return false;
    const auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Native text slot animation")))
        .ClientSize(FVector2D(300,100)).FocusWhenFirstShown(false).SupportsMaximize(false).SupportsMinimize(false)
        [SNew(SBox)[Instance->TakeWidget()]];
    FSlateApplication::Get().AddWindow(Window, true);
    ON_SCOPE_EXIT { FSlateApplication::Get().RequestDestroyWindow(Window); };
    Window->SlatePrepass(); FSlateApplication::Get().Tick(); FSlateApplication::Get().Tick();
    auto* Label = Cast<UTextBlock>(Instance->WidgetTree->FindWidget(TEXT("Label")));
    auto* ActualSlot = Label ? Cast<UCanvasPanelSlot>(Label->Slot) : nullptr;
    const auto* Property = FindFProperty<FObjectProperty>(Instance->GetClass(), TEXT("OffsetMotion"));
    auto* Runtime = Property ? Cast<UWidgetAnimation>(Property->GetObjectPropertyValue_InContainer(Instance.Get())) : nullptr;
    if (!TestNotNull(TEXT("Native text label exists"), Label) || !TestNotNull(TEXT("Native Canvas slot exists"), ActualSlot)
        || !TestNotNull(TEXT("Native offset animation exists"), Runtime)) return false;
    const FVector2D Before = Label->GetCachedGeometry().GetAbsolutePosition();
    const FVector2D BeforeSize = Label->GetCachedGeometry().GetLocalSize();
    Instance->PlayAnimation(Runtime,0,1,EUMGSequencePlayMode::Forward,1,true);
    for (const float Delta : {0.0f, 0.5f, 0.25f})
    {
        ++GFrameCounter;
        UUMGSequenceTickManager::Get(Instance.Get())->OnWidgetTicked(Instance.Get());
        FSlateApplication::Get().OnPreTick().Broadcast(Delta);
        World->GetLatentActionManager().BeginFrame();
        World->GetLatentActionManager().ProcessLatentActions(Instance.Get(),Delta);
        FSlateApplication::Get().OnPostTick().Broadcast(Delta);
    }
    Window->SlatePrepass(); FSlateApplication::Get().Tick();
    const FVector2D After = Label->GetCachedGeometry().GetAbsolutePosition();
    TestTrue(TEXT("Native margin track changes the Canvas slot"), ActualSlot->GetOffsets().Left > 30.0f);
    TestTrue(TEXT("Canvas setter moves the live Slate text"), After.X > Before.X + 20.0f);
    TestTrue(TEXT("Auto-sized native text keeps its desired size during movement"),
        Label->GetCachedGeometry().GetLocalSize().Equals(BeforeSize,0.1));
    return !HasAnyErrors();
}

// Capability probe only: an ordinary engine text widget with a native font
// material. A successful render does not establish Figma gradient fidelity.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTextFontAnimationRuntimeTest,
    "Design2UMG.Text.NativeFontAnimationRuntime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool RunNativeTextFontAnimationProbe(FAutomationTestBase& Test, bool bRefresh)
{
    const bool bReload = FPlatformMisc::GetEnvironmentVariable(TEXT("D2U_NATIVE_FONT_PROBE_RELOAD")) == TEXT("1");
    const FString EvidenceDirectory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/NativeFontAnimationProbe");
    const FString PackageDirectory = TEXT("/Game/Design2UMGTests/NativeFontAnimation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    for (const bool bLetterSpacing : {false, true})
    {
        const FString AssetName = bLetterSpacing ? TEXT("NativeFontTracking") : TEXT("NativeFontSize");
        const FString PackageName = PackageDirectory / AssetName;
        const FString AssetPointerFile = EvidenceDirectory / (AssetName + TEXT(".txt"));
        TStrongObjectPtr<UWidgetBlueprint> Blueprint;
        TArray<FName> RefreshNames;
        if (bRefresh && bReload)
        {
            FString SavedObjectPath;
            if (!Test.TestTrue(TEXT("Read exact native font asset path from the write process"),
                FFileHelper::LoadFileToString(SavedObjectPath, *AssetPointerFile))) return false;
            if (!Test.TestNull(TEXT("Saved native font asset is not already in memory"),
                FindObject<UWidgetBlueprint>(nullptr, *SavedObjectPath))) return false;
            Blueprint.Reset(LoadObject<UWidgetBlueprint>(nullptr, *SavedObjectPath));
            if (!Test.TestNotNull(TEXT("Saved native font animation loads in a fresh process"), Blueprint.Get())) return false;
        }
        else
        {
            UPackage* Package = bRefresh ? CreatePackage(*PackageName) : GetTransientPackage();
            Blueprint.Reset(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
                UUserWidget::StaticClass(), Package, bRefresh ? FName(*AssetName) : MakeUniqueObjectName(Package,
                    UWidgetBlueprint::StaticClass(), TEXT("FontAnimation")), BPTYPE_Normal,
                UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
            auto* Text = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EditableText"));
            Text->SetText(FText::FromString(TEXT("Wi Wi")));
            Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 48));
            Text->bIsVariable = true;
            Blueprint->WidgetTree->RootWidget = Text;
            auto* Animation = UMGPrototypeAnimationBuilder::CreateAnimation(Blueprint.Get(), TEXT("FontMotion"), 1.0f);
            const FFrameNumber End(static_cast<int32>(Animation->MovieScene->GetTickResolution().AsDecimal()));
            const auto Keys = [&](FMovieSceneFloatChannel& Channel, float Start, float Finish)
            { Channel.AddLinearKey(0, Start); Channel.AddLinearKey(End, Finish); };
            if (bRefresh)
            {
                auto* Destination = NewObject<UTextBlock>();
                FSlateFontInfo Font = Text->GetFont(); Font.Size = 96; Font.LetterSpacing = 400;
                Destination->SetFont(Font);
                FString Error;
                NativeTextAnimation::FRefreshBuilder Refresh(Blueprint.Get());
                if (!Test.TestTrue(TEXT("Production native font tracks and persistent refresh endpoint build: ") + Error,
                    Refresh.AddFontTracks(Animation, Text, Destination, !bLetterSpacing, bLetterSpacing, Keys, Error)))
                { Test.AddError(Error); return false; }
                Refresh.Build([&](UWidgetAnimation*, const FString& BuildError) { Test.AddError(BuildError); });
            }
            else
            {
                const FGuid Binding = UMGPrototypeAnimationBuilder::BindWidget(Animation, Text);
                auto* Track = Animation->MovieScene->AddTrack<UMovieSceneFloatTrack>(Binding);
                Track->SetPropertyNameAndPath(TEXT("Size"), TEXT("Font.Size"));
                auto* Section = CastChecked<UMovieSceneFloatSection>(Track->CreateNewSection());
                Section->SetRange(Animation->MovieScene->GetPlaybackRange());
                Keys(*Section->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0), 48, 96);
                Track->AddSection(*Section);
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint.Get());
            FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
            if (!Test.TestTrue(TEXT("Ordinary native font animation Blueprint compiles"), Blueprint->Status != BS_Error)) return false;
            if (bRefresh)
            {
                const FString FileName = FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(FileName),true);
                FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
                Blueprint->SetFlags(RF_Public|RF_Standalone);
                if (!Test.TestTrue(TEXT("New native font package is fully loaded before save"), Package->IsFullyLoaded())) return false;
                if (!Test.TestTrue(TEXT("Native font animation asset saves with its generated endpoint"),
                    UPackage::SavePackage(Package,Blueprint.Get(),*FileName,Args))) return false;
                IFileManager::Get().MakeDirectory(*EvidenceDirectory,true);
                if (!Test.TestTrue(TEXT("Record the exact saved native font asset for fresh-process reload"),
                    FFileHelper::SaveStringToFile(Blueprint->GetPathName(), *AssetPointerFile,
                        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
            }
        }
        UWorld* World = GEditor->GetEditorWorldContext().World();
        const TStrongObjectPtr<UUserWidget> Instance(CreateWidget<UUserWidget>(World, Blueprint->GeneratedClass.Get()));
        const TSharedRef<SWidget> Slate = Instance->TakeWidget();
        auto* Label = CastChecked<UTextBlock>(Instance->WidgetTree->RootWidget);
        const auto TextSlate = StaticCastSharedPtr<STextBlock>(Label->GetCachedWidget());
        Slate->SlatePrepass();
        const FVector2D InitialSize = TextSlate->GetDesiredSize();
        const auto* Property = FindFProperty<FObjectProperty>(Instance->GetClass(), TEXT("FontMotion"));
        auto* RuntimeAnimation = Property ? Cast<UWidgetAnimation>(Property->GetObjectPropertyValue_InContainer(Instance.Get())) : nullptr;
        if (!Test.TestNotNull(TEXT("Compiled instance owns the native font animation"), RuntimeAnimation)) return false;
        if (bRefresh)
        {
            RefreshNames = NativeTextAnimation::RefreshFunctions(Blueprint.Get(), Blueprint->Animations[0]);
            if (!Test.TestEqual(TEXT("One owned native refresh endpoint"), RefreshNames.Num(),1)) return false;
            FWidgetAnimationDynamicEvent Finished;
            Finished.BindUFunction(Instance.Get(),RefreshNames[0]);
            Instance->BindToAnimationFinished(RuntimeAnimation,Finished);
        }
        Instance->PlayAnimation(RuntimeAnimation,0,1,EUMGSequencePlayMode::Forward,1,true);
        const auto Tick = [&](float Seconds)
        {
            ++GFrameCounter;
            UUMGSequenceTickManager::Get(Instance.Get())->OnWidgetTicked(Instance.Get());
            FSlateApplication::Get().OnPreTick().Broadcast(Seconds);
            World->GetLatentActionManager().BeginFrame();
            World->GetLatentActionManager().ProcessLatentActions(Instance.Get(), Seconds);
            FSlateApplication::Get().OnPostTick().Broadcast(Seconds);
        };
        Tick(0); Tick(0.5f); Tick(0.25f);
        Slate->SlatePrepass();
        const FSlateFontInfo ObjectFont = Label->GetFont();
        const FSlateFontInfo SlateFont = TextSlate->GetFont();
        Test.AddInfo(FString::Printf(TEXT("Native font production=%d reload=%d: UObject size=%.4f tracking=%d; Slate size=%.4f tracking=%d; width %.4f -> %.4f"),
            bRefresh,bReload,ObjectFont.Size,ObjectFont.LetterSpacing,SlateFont.Size,SlateFont.LetterSpacing,InitialSize.X,TextSlate->GetDesiredSize().X));
        if (bRefresh)
        {
            Test.TestTrue(TEXT("Continuous native font value reaches Slate"), bLetterSpacing
                ? ObjectFont.LetterSpacing > 200 && ObjectFont.LetterSpacing < 400 && SlateFont.LetterSpacing == ObjectFont.LetterSpacing
                : ObjectFont.Size > 48 && FMath::IsNearlyEqual(SlateFont.Size,ObjectFont.Size));
            Test.TestTrue(TEXT("Editable native text layout responds"),TextSlate->GetDesiredSize().X > InitialSize.X);
            Instance->StopAnimation(RuntimeAnimation); Tick(0);
            Test.TestTrue(TEXT("Stop restores the full native and Slate font"),FMath::IsNearlyEqual(Label->GetFont().Size,48.0f)
                && Label->GetFont().LetterSpacing == 0 && FMath::IsNearlyEqual(TextSlate->GetFont().Size,48.0f)
                && TextSlate->GetFont().LetterSpacing == 0);
            Instance->PlayAnimation(RuntimeAnimation,0,1,EUMGSequencePlayMode::Reverse,1,true);
            Tick(0); Tick(0.25f);
            Test.TestTrue(TEXT("Reverse playback evaluates the same live font endpoint"),bLetterSpacing
                ? TextSlate->GetFont().LetterSpacing > 0 : TextSlate->GetFont().Size > 48);
            Instance->StopAnimation(RuntimeAnimation); Tick(0);
        }
        else
        {
            Test.TestTrue(TEXT("Direct nested field writes do not call Slate SetFont"),ObjectFont.Size > 48
                && FMath::IsNearlyEqual(SlateFont.Size,48.0f) && FMath::IsNearlyEqual(TextSlate->GetDesiredSize().X,InitialSize.X));
            Instance->StopAllAnimations();
            break;
        }
    }
    return true;
}

bool FNativeTextFontAnimationRuntimeTest::RunTest(const FString&)
{
    return RunNativeTextFontAnimationProbe(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTextFontAnimationRefreshRuntimeTest,
    "Design2UMG.Text.NativeFontAnimationRefreshRuntime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeTextFontAnimationRefreshRuntimeTest::RunTest(const FString&)
{
    return RunNativeTextFontAnimationProbe(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTextMaterialRuntimeTest,
    "Design2UMG.Text.NativeFontMaterialRuntime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeTextMaterialRuntimeTest::RunTest(const FString&)
{
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(GetTransientPackage()));
    Material->MaterialDomain = MD_UI;
    Material->BlendMode = BLEND_Translucent;
    UMaterialExpressionTextureCoordinate* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material.Get());
    Design2UMGMaterialAccess::AddExpression(*Material, *UV);
    UMaterialExpressionComponentMask* Red = NewObject<UMaterialExpressionComponentMask>(Material.Get());
    Red->Input.Expression = UV;
    Red->R = true;
    Red->G = Red->B = Red->A = false;
    Design2UMGMaterialAccess::AddExpression(*Material, *Red);
    Material->GetExpressionInputForProperty(MP_EmissiveColor)[0].Expression = Red;
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Design2UMGMaterialAccess::CacheShadersForRendering(*Material);
    Design2UMGMaterialAccess::WaitForCompilation(*Material);

    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/NativeTextProbe");
    IFileManager::Get().MakeDirectory(*Directory, true);
    for (const FString& Text : {FString(TEXT("WiWi")), FString(TEXT("Wi\nWi"))})
    {
        TArray<TArray<FColor>> Images;
        for (bool bUseMaterial : {false, true})
        {
            FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 48);
            Font.FontMaterial = bUseMaterial ? Material.Get() : nullptr;
            const TSharedRef<STextBlock> Widget = SNew(STextBlock)
                .Text(FText::FromString(Text)).Font(Font).ColorAndOpacity(FLinearColor::White);
            const FVector2D Size(320, 180);
            TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size, TF_Nearest, true));
            FWidgetRenderer Renderer(true);
            Renderer.DrawWidget(Target.Get(), Widget, 1.f, Size, 0.f);
            FlushRenderingCommands();
            TArray<FColor>& Pixels = Images.AddDefaulted_GetRef();
            if (!TestTrue(TEXT("Read native text pixels"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)))
                return false;
            int32 VisiblePixels = 0;
            int32 MinRed = 255;
            int32 MaxRed = 0;
            for (const FColor& Pixel : Pixels)
            {
                if (Pixel.A > 240)
                {
                    ++VisiblePixels;
                    MinRed = FMath::Min(MinRed, int32(Pixel.R));
                    MaxRed = FMath::Max(MaxRed, int32(Pixel.R));
                }
            }
            TestTrue(TEXT("Text has opaque interior pixels"), VisiblePixels > 100);
            if (bUseMaterial)
                TestTrue(TEXT("Font material produces a spatially varying color"), MaxRed - MinRed > 50);
            const FString Name = FString::Printf(TEXT("%s-%s.png"), Text.Contains(TEXT("\n")) ? TEXT("multiline") : TEXT("unequal-width"), bUseMaterial ? TEXT("material") : TEXT("plain"));
            TArray64<uint8> Png;
            FImageUtils::PNGCompressImageArray(320, 180, Pixels, Png);
            TestTrue(TEXT("Write native text evidence"), FFileHelper::SaveArrayToFile(Png, *(Directory / Name)));
            AddInfo(FString::Printf(TEXT("%s opaque=%d red=[%d,%d]"), *Name, VisiblePixels, MinRed, MaxRed));
        }
        TestTrue(TEXT("Native font material changes rendered pixels"), Images[0] != Images[1]);
    }
    return true;
}

namespace
{
UMaterial* CreateNativeRetainedTextMaterial(UObject* Outer, bool bPreservePaint = false)
{
    const bool bAsset = Outer != GetTransientPackage();
    UMaterial* Material = NewObject<UMaterial>(Outer,
        MakeUniqueObjectName(Outer, UMaterial::StaticClass(), TEXT("TextGradient")),
        bAsset ? RF_Public | RF_Standalone : RF_Transient);
    Material->MaterialDomain = MD_UI;
    Material->BlendMode = BLEND_AlphaComposite;
    auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material);
    Design2UMGMaterialAccess::AddExpression(*Material, *UV);
    auto* Red = NewObject<UMaterialExpressionComponentMask>(Material);
    Red->Input.Expression = UV;
    Red->R = true;
    Red->G = Red->B = Red->A = false;
    Design2UMGMaterialAccess::AddExpression(*Material, *Red);
    auto* Texture = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
    Texture->ParameterName = TEXT("Texture");
    Texture->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    if (!Texture->Texture) return nullptr;
    Texture->SamplerType = SAMPLERTYPE_Color;
    Design2UMGMaterialAccess::AddExpression(*Material, *Texture);
    auto* Multiply = NewObject<UMaterialExpressionMultiply>(Material);
    Multiply->A.Expression = Red;
    Multiply->B.Connect(4, Texture); // Texture sample alpha; premultiplied output.
    Design2UMGMaterialAccess::AddExpression(*Material, *Multiply);
    Material->GetExpressionInputForProperty(MP_EmissiveColor)[0].Expression = bPreservePaint ? static_cast<UMaterialExpression*>(Texture) : Multiply;
    Material->GetExpressionInputForProperty(MP_Opacity)[0].Connect(4, Texture);
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Design2UMGMaterialAccess::CacheShadersForRendering(*Material);
    Design2UMGMaterialAccess::WaitForCompilation(*Material);

    return Material;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeRetainedTextMaterialRuntimeTest,
    "Design2UMG.Text.NativeRetainedTextMaterialRuntime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeRetainedTextMaterialRuntimeTest::RunTest(const FString&)
{
    TStrongObjectPtr<UMaterial> Material(CreateNativeRetainedTextMaterial(GetTransientPackage()));
    if (!TestNotNull(TEXT("Native retained text material"), Material.Get())) return false;

    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/NativeTextProbe");
    IFileManager::Get().MakeDirectory(*Directory, true);
    for (const int32 Width : {160, 320})
    {
        for (const bool bMultiline : {false, true})
        {
            const auto Text = SNew(STextBlock)
                .Text(FText::FromString(bMultiline ? TEXT("Wi\nWi") : TEXT("WiWi")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 48)).ColorAndOpacity(FLinearColor::White);
            const auto Retainer = SNew(SRetainerWidget).RenderOnPhase(false).RenderOnInvalidation(true)[Text];
            Retainer->SetTextureParameter(TEXT("Texture"));
            Retainer->SetEffectMaterial(Material.Get());
            Retainer->RequestRender();
            const FVector2D Size(Width, 180);
            TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size, TF_Nearest, true));
            FWidgetRenderer Renderer(true);
            Renderer.DrawWidget(Target.Get(), Retainer, 1.f, Size, 0.f);
            FlushRenderingCommands();
            TArray<FColor> Pixels;
            if (!TestTrue(TEXT("Read retained native text"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return false;
            int32 VisiblePixels = 0;
            for (const FColor& Pixel : Pixels) if (Pixel.A > 240) ++VisiblePixels;
            TestTrue(TEXT("Retained text is visible"), VisiblePixels > 100);
            double ErrorSum = 0.0;
            float MaxError = 0.f;
            int32 MeasuredPixels = 0;
            for (int32 Index = 0; Index < Pixels.Num(); ++Index)
            {
                // Exclude anti-aliased edges: this checks the spatial coordinate,
                // not coverage blending. ReadPixels returns sRGB-encoded color.
                if (Pixels[Index].A <= 250) continue;
                const float Expected = (float(Index % Width) + 0.5f) / float(Width);
                const float Error = FMath::Abs(FLinearColor(Pixels[Index]).R - Expected);
                ErrorSum += Error;
                MaxError = FMath::Max(MaxError, Error);
                ++MeasuredPixels;
            }
            if (!TestTrue(TEXT("Spatial gradient has measurable interior"), MeasuredPixels > 100)) return false;
            // Allow 8-bit sRGB quantization and up to 4/255 residual coverage.
            TestTrue(TEXT("Gradient follows physical x, not character index"), MaxError < 0.02f);
            TestTrue(TEXT("Mean spatial gradient error is below quantization tolerance"), ErrorSum / MeasuredPixels < 0.003);
            const FString Name = FString::Printf(TEXT("retained-%d-%s.png"), Width, bMultiline ? TEXT("multiline") : TEXT("unequal-width"));
            TArray64<uint8> Png;
            FImageUtils::PNGCompressImageArray(Width, 180, Pixels, Png);
            TestTrue(TEXT("Write retained text evidence"), FFileHelper::SaveArrayToFile(Png, *(Directory / Name)));
            AddInfo(FString::Printf(TEXT("%s opaque=%d"), *Name, VisiblePixels));
        }
    }
    return true;
}

namespace
{
bool SaveNativeTextProbeAsset(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

bool RenderNativeTextProbeAsset(UWidgetBlueprint* Blueprint, TArray<FColor>& Pixels)
{
    TStrongObjectPtr<UUserWidget> Instance(NewObject<UUserWidget>(GetTransientPackage(), Blueprint->GeneratedClass));
    if (!Instance->Initialize()) return false;
    const TSharedRef<SWidget> SlateWidget = Instance->TakeWidget();
    // Match UMGWidgetRenderer's existing offscreen resource preparation. Loading an
    // asset does not synchronously prepare its dynamic material's uniforms.
    if (URetainerBox* Retainer = Cast<URetainerBox>(Instance->WidgetTree->RootWidget))
    {
        if (UMaterialInstanceDynamic* Effect = Retainer->GetEffectMaterial())
        {
            Design2UMGMaterialAccess::WaitForCompilation(*Effect->GetMaterial());
            Effect->GetMaterial()->RecacheUniformExpressions(true);
        }
    }
    const FVector2D Size(160, 180);
    TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size, TF_Nearest, true));
    FWidgetRenderer Renderer(true);
    Renderer.DrawWidget(Target.Get(), SlateWidget, 1.f, Size, 0.f);
    FlushRenderingCommands();
    const bool bRead = Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
    Instance->ReleaseSlateResources(true);
    return bRead;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeRetainedTextAssetWriteTest,
    "Design2UMG.Text.NativeRetainedTextAssetPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeRetainedTextAssetWriteTest::RunTest(const FString&)
{
    // The driver runs the same probe in two separate editor processes.
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("D2U_NATIVE_TEXT_PROBE_RELOAD")) == TEXT("1"))
    {

        const FString Directory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/NativeTextProbe");
        FString AssetPath;
        if (!TestTrue(TEXT("Read prior process asset pointer"), FFileHelper::LoadFileToString(AssetPath, *(Directory / TEXT("asset-path.txt"))))) return false;
        if (!TestNull(TEXT("Asset is not already in memory"), FindObject<UWidgetBlueprint>(nullptr, *AssetPath))) return false;
        UWidgetBlueprint* BP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
        if (!TestNotNull(TEXT("Load native text asset from disk"), BP)) return false;
        TArray<FColor> Pixels;
        if (!TestTrue(TEXT("Render reloaded rich text asset"), RenderNativeTextProbeAsset(BP, Pixels))) return false;
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(160, 180, Pixels, Png);
        if (!TestTrue(TEXT("Save fresh-process asset image"), FFileHelper::SaveArrayToFile(Png, *(Directory / TEXT("asset-after.png"))))) return false;
        TArray64<uint8> Previous;
        if (!TestTrue(TEXT("Read original asset image"), FFileHelper::LoadFileToArray(Previous, *(Directory / TEXT("asset-before.png"))))) return false;
        return TestTrue(TEXT("Fresh-process render is identical"), Previous == Png);

    }

    const FString Folder = TEXT("/Game/Design2UMG/NativeTextProbe/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* MaterialPackage = CreatePackage(*(Folder / TEXT("TextGradient")));
    TStrongObjectPtr<UMaterial> Material(CreateNativeRetainedTextMaterial(MaterialPackage));
    if (!TestNotNull(TEXT("Create persistent native text material"), Material.Get())) return false;
    if (!TestTrue(TEXT("Save native effect material"), SaveNativeTextProbeAsset(Material.Get()))) return false;
    UPackage* Package = CreatePackage(*(Folder / TEXT("WBP_Text")));
    UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), Package, TEXT("WBP_Text"), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
    if (!TestNotNull(TEXT("Create native text blueprint"), BP)) return false;
    URetainerBox* Retainer = BP->WidgetTree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass(), TEXT("RetainedText"));
    URichTextBlock* Text = BP->WidgetTree->ConstructWidget<URichTextBlock>(URichTextBlock::StaticClass(), TEXT("StyledText"));
    UDataTable* Styles = NewObject<UDataTable>(BP, TEXT("TextStyles"));
    Styles->RowStruct = FRichTextStyleRow::StaticStruct();
    UFont* FontAsset = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    if (!TestNotNull(TEXT("Persistent font asset"), FontAsset)) return false;
    FRichTextStyleRow Normal;
    Normal.TextStyle.SetFont(FSlateFontInfo(FontAsset, 48, TEXT("Regular"))).SetColorAndOpacity(FLinearColor::White);
    Styles->AddRow(TEXT("Default"), Normal);
    FRichTextStyleRow Small = Normal;
    Small.TextStyle.SetFont(FSlateFontInfo(FontAsset, 28, TEXT("Regular")));
    Styles->AddRow(TEXT("Small"), Small);
    Text->SetTextStyleSet(Styles);
    Text->SetText(FText::FromString(TEXT("W<Small>i</>\nW<Small>i</>")));
    Retainer->SetContent(Text);
    Retainer->SetTextureParameter(TEXT("Texture"));
    Retainer->SetEffectMaterial(Material.Get());
    BP->WidgetTree->RootWidget = Retainer;
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (!TestTrue(TEXT("Native text blueprint compiles"), BP->Status != BS_Error)) return false;
    if (!TestTrue(TEXT("Save native text blueprint"), SaveNativeTextProbeAsset(BP))) return false;
    TArray<FColor> Pixels;
    if (!TestTrue(TEXT("Render saved rich text asset"), RenderNativeTextProbeAsset(BP, Pixels))) return false;
    int32 Visible = 0;
    for (const FColor& Pixel : Pixels) if (Pixel.A > 240) ++Visible;
    if (!TestTrue(TEXT("Rich text asset visible before exit"), Visible > 100)) return false;
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/NativeTextProbe");
    IFileManager::Get().MakeDirectory(*Directory, true);
    TArray64<uint8> Png;
    FImageUtils::PNGCompressImageArray(160, 180, Pixels, Png);
    TestTrue(TEXT("Save asset render before process exit"), FFileHelper::SaveArrayToFile(Png, *(Directory / TEXT("asset-before.png"))));
    return TestTrue(TEXT("Write fresh-process asset pointer"), FFileHelper::SaveStringToFile(BP->GetPathName(), *(Directory / TEXT("asset-path.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeRichTextRunIsolationTest,
    "Design2UMG.Text.NativeRichTextRunIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeRichTextRunIsolationTest::RunTest(const FString&)
{
    TStrongObjectPtr<UMaterial> IdentityMaterial(CreateNativeRetainedTextMaterial(GetTransientPackage(), true));
    if (!TestNotNull(TEXT("Identity effect material"), IdentityMaterial.Get())) return false;
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/NativeTextProbe");
    IFileManager::Get().MakeDirectory(*Directory, true);
    for (const bool bOverlap : {false, true})
    {
        TArray<TArray<FColor>> Images;
        for (const int32 Mode : {0, 1, 2})
        {
            const bool bSplit = Mode > 0;
            const bool bRetained = Mode == 2;
            const TSharedRef<SOverlay> Root = SNew(SOverlay);
            TArray<TUniquePtr<FSlateStyleSet>> Styles;
            const FLinearColor Colors[] = {FLinearColor::Red, FLinearColor::Green, FLinearColor::Blue};
            // Slate draws all outlines below all fills. Separate text widgets
            // must retain that ordering instead of placing a later run's outline
            // over an earlier run's fill.
            const int32 OutlineLayers = bSplit && bOverlap ? 1 : 0;
            const int32 LayerCount = bSplit ? 3 + OutlineLayers : 1;
            FTextBlockStyle Base;
            Base.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 36)).SetColorAndOpacity(FLinearColor::White);
            for (int32 Layer = 0; Layer < LayerCount; ++Layer)
            {
                TUniquePtr<FSlateStyleSet> Style = MakeUnique<FSlateStyleSet>(FName(*FString::Printf(TEXT("NativeRunMask%d"), Layer)));
                for (int32 Run = 0; Run < 3; ++Run)
                {
                    const bool bVisible = !bSplit || Run == Layer - OutlineLayers;
                    const bool bOutlineVisible = !bSplit || (OutlineLayers > 0 && Layer == 0);
                    FTextBlockStyle Row = Base;
                    FSlateFontInfo Font = Row.Font;
                    Font.LetterSpacing = bOverlap ? -200 : 0;
                    Font.OutlineSettings.OutlineSize = bOverlap ? 8 : 0;
                    Font.OutlineSettings.OutlineColor = bOutlineVisible ? Colors[Run] : FLinearColor::Transparent;
                    Row.SetFont(Font).SetColorAndOpacity(bVisible ? Colors[Run] : FLinearColor::Transparent);
                    Style->Set(FName(*FString::Printf(TEXT("r%d"), Run)), Row);
                }
                const TSharedRef<SRichTextBlock> Text = SNew(SRichTextBlock)
                    .Text(FText::FromString(TEXT("<r0>Wi Wi </><r1>Wi Wi </><r2>Wi Wi</>")))
                    .TextStyle(&Base).DecoratorStyleSet(Style.Get()).WrapTextAt(160.f);
                if (bRetained && Layer >= OutlineLayers)
                {
                    const auto Retainer = SNew(SRetainerWidget).RenderOnPhase(false).RenderOnInvalidation(true)[Text];
                    Retainer->SetTextureParameter(TEXT("Texture"));
                    Retainer->SetEffectMaterial(IdentityMaterial.Get());
                    Retainer->RequestRender();
                    Root->AddSlot()[Retainer];
                }
                else
                {
                    Root->AddSlot()[Text];
                }
                Styles.Add(MoveTemp(Style));
            }
            const FVector2D Size(160, 220);
            TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size, TF_Nearest, true));
            FWidgetRenderer Renderer(true);
            Renderer.DrawWidget(Target.Get(), Root, 1.f, Size, 0.f);
            FlushRenderingCommands();
            TArray<FColor>& Pixels = Images.AddDefaulted_GetRef();
            if (!TestTrue(TEXT("Read isolated runs"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return false;
            const FString Name = FString::Printf(TEXT("run-isolation-%s-%s.png"), bOverlap ? TEXT("overlap") : TEXT("normal"), bRetained ? TEXT("retained") : bSplit ? TEXT("split") : TEXT("direct"));
            TArray64<uint8> Png;
            FImageUtils::PNGCompressImageArray(160, 220, Pixels, Png);
            TestTrue(TEXT("Write run isolation evidence"), FFileHelper::SaveArrayToFile(Png, *(Directory / Name)));
            Root->ClearChildren(); // Style pointers must outlive all text widgets.
        }
        int32 DifferentPixels = 0;
        for (int32 Index = 0; Index < Images[0].Num(); ++Index)
            if (Images[0][Index] != Images[1][Index]) ++DifferentPixels;
        AddInfo(FString::Printf(TEXT("Run isolation overlap=%d different_pixels=%d"), bOverlap, DifferentPixels));
        TestEqual(TEXT("Isolating contiguous runs preserves compositing and wrapping"), DifferentPixels, 0);
        int32 AlphaDifferences = 0;
        int32 RetainedDifferences = 0;
        int32 MaxColorError = 0;
        int32 VisiblePixels = 0;
        for (int32 Index = 0; Index < Images[0].Num(); ++Index)
        {
            const FColor& Direct = Images[0][Index];
            const FColor& Retained = Images[2][Index];
            VisiblePixels += Direct.A > 240;
            AlphaDifferences += Direct.A != Retained.A;
            RetainedDifferences += Direct != Retained;
            MaxColorError = FMath::Max(MaxColorError, FMath::Max3(
                FMath::Abs(int32(Direct.R) - int32(Retained.R)),
                FMath::Abs(int32(Direct.G) - int32(Retained.G)),
                FMath::Abs(int32(Direct.B) - int32(Retained.B))));
        }
        TestTrue(TEXT("Direct reference contains visible glyphs"), VisiblePixels > 100);
        TestEqual(TEXT("Retaining run masks preserves glyph coverage"), AlphaDifferences, 0);
        // Retainer targets use PF_B8G8R8A8. This probe allows one additional
        // quantization step, NOT a relaxation of the project's visual gate.
        TestTrue(TEXT("Retained mask color stays within one 8-bit step"), MaxColorError <= 1);
        AddInfo(FString::Printf(TEXT("Retained isolation overlap=%d different_pixels=%d max_color_error=%d alpha_differences=%d"),
            bOverlap, RetainedDifferences, MaxColorError, AlphaDifferences));
    }
    return true;
}

#endif
