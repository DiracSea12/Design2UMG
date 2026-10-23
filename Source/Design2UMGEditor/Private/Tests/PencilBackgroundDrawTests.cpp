// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"

#if WITH_DEV_AUTOMATION_TESTS && UE_VERSION_NEWER_THAN(5, 8, 0)
#include "Design2UMGMaterialAccess.h"
#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/RetainerBox.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "CanvasTypes.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FX/SlateRHIPostBufferProcessor.h"
#include "GenerateMips.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/SlateRHIRenderingPolicyInterface.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/DrawElements.h"
#include "RenderingThread.h"
#include "RHIUtilities.h"
#include "Slate/WidgetRenderer.h"
#include "Slate/SRetainerWidget.h"
#include "Styling/CoreStyle.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"

namespace
{
// This proxy is invoked at the public custom-draw boundary, not installed in the stock subsystem.
class FCurrentTargetMipProxy final : public FSlateRHIPostBufferProcessorProxy
{
public:
    void PostProcess_Renderthread(FRDGBuilder& GraphBuilder, const FScreenPassTexture& Input,
        const FScreenPassTexture& Output) override
    {
        AddSlatePostProcessCopy(GraphBuilder, Input, Output);
        FGenerateMips::Execute(GraphBuilder, GMaxRHIFeatureLevel, Output.Texture);
    }
};

class FCurrentTargetDraw final : public ICustomSlateElement
{
public:
    explicit FCurrentTargetDraw(FTextureRenderTargetResource* InResource, bool InClear = false) : Resource(InResource), bClear(InClear) {}

    void Draw_RenderThread(FRDGBuilder& GraphBuilder, const FDrawPassInputs& Inputs) override
    {
        ++Calls;
        InputSize = Inputs.OutputTexture->Desc.Extent;
        NativeSceneRect = Inputs.SceneViewRect;
        // Slate3D supplies the current texture, but does not populate SceneViewRect.
        const FScreenPassTexture Input(Inputs.OutputTexture);
        InputRect = Input.ViewRect;
        UsedPostBuffers = uint32(Inputs.UsedSlatePostBuffers);
        FRDGTextureRef MipTexture = GraphBuilder.RegisterExternalTexture(
            CreateRenderTarget(Resource->GetRenderTargetTexture(), TEXT("BackgroundCurrentTargetMip")));
        MipCount = MipTexture->Desc.NumMips;
        GraphBuilder.UseInternalAccessMode(MipTexture);
        Proxy.PostProcess_Renderthread(GraphBuilder,
            Input, FScreenPassTexture(MipTexture));
        GraphBuilder.UseExternalAccessMode(MipTexture, ERHIAccess::SRVMask);
        if (bClear) AddClearRenderTargetPass(GraphBuilder, Inputs.OutputTexture, FLinearColor::Transparent);
    }

    int32 Calls = 0;
    int32 MipCount = 0;
    FIntPoint InputSize = FIntPoint::ZeroValue;
    FIntRect InputRect;
    FIntRect NativeSceneRect;
    uint32 UsedPostBuffers = 0;

private:
    FTextureRenderTargetResource* Resource;
    bool bClear;
    FCurrentTargetMipProxy Proxy;
};

using FCurrentTargetDrawPtr = TSharedPtr<FCurrentTargetDraw, ESPMode::ThreadSafe>;

class SBackgroundCurrentTargetScene final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SBackgroundCurrentTargetScene) {}
        SLATE_ARGUMENT(FCurrentTargetDrawPtr, Drawer)
        SLATE_ARGUMENT(UMaterialInterface*, Material)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Drawer = Args._Drawer;
        ConsumerBrush.SetResourceObject(Args._Material);
        ConsumerBrush.ImageSize = FVector2D(16);
        ConsumerBrush.DrawAs = ESlateBrushDrawType::Image;
    }

    void SetMarker(FLinearColor Value)
    {
        Marker = Value;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    FVector2D ComputeDesiredSize(float) const override { return FVector2D(16); }

    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool) const override
    {
        const auto Paint = [&](int32 AtLayer, FVector2D Position, FVector2D Size,
            const FSlateBrush* Brush, FLinearColor Color)
        {
            FSlateDrawElement::MakeBox(Elements, AtLayer,
                Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)), Brush,
                ESlateDrawEffect::None, Color * Style.GetColorAndOpacityTint());
        };
        const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
        // P background and earlier C are in this Retainer; external M is not.
        Paint(Layer, FVector2D::ZeroVector, FVector2D(16), White, FLinearColor(0, 0, 1, .25f));
        Paint(Layer + 1, FVector2D::ZeroVector, FVector2D(8, 16), White, Marker);
        FSlateDrawElement::MakeCustom(Elements, Layer + 2, Drawer);
        // S consumes the mip immediately after the native copy/generation boundary.
        Paint(Layer + 3, FVector2D(8, 8), FVector2D(4, 8), &ConsumerBrush, FLinearColor::White);
        // D is later than S and must not be present in its capture.
        Paint(Layer + 4, FVector2D(12, 0), FVector2D(4), White, FLinearColor::Blue);
        Paint(Layer + 4, FVector2D(16, 0), FVector2D(4), White, FLinearColor::White);
        return Layer + 4;
    }

private:
    TSharedPtr<FCurrentTargetDraw, ESPMode::ThreadSafe> Drawer;
    FSlateBrush ConsumerBrush;
    FLinearColor Marker = FLinearColor::Red;
};

class FBackgroundCurrentTargetFrames final : public IAutomationLatentCommand
{
public:
    explicit FBackgroundCurrentTargetFrames(FAutomationTestBase& InTest) : Test(InTest) {}
    ~FBackgroundCurrentTargetFrames() override
    {
        Root.Reset();
        Retainer.Reset();
        Scene.Reset();
        FlushRenderingCommands();
        Drawer.Reset();
        if (Rendered.IsValid()) UKismetRenderingLibrary::ReleaseRenderTarget2D(Rendered.Get());
        if (Mip.IsValid()) UKismetRenderingLibrary::ReleaseRenderTarget2D(Mip.Get());
        FlushRenderingCommands();
    }

    bool Update() override
    {
        if (PreviousFrame == GFrameCounter) return false;
        PreviousFrame = GFrameCounter;
        Scene->SetMarker(Draws == 0 ? FLinearColor::Red : FLinearColor::Green);
        Retainer->RequestRender();
        const int32 CallsBefore = Drawer->Calls;
        FWidgetRenderer Renderer(true);
        Renderer.DrawWidget(Rendered.Get(), Root.ToSharedRef(), FVector2D(16), 1.f / 60.f, false);
        FlushRenderingCommands();
        ++Draws;
        const auto Read = [&](UTextureRenderTarget2D* Target, int32 X, int32 Y)
        {
            FLinearColor Color = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Target, X, Y, false);
            if (Target->GetFormat() == PF_B8G8R8A8) Color /= 255.f;
            return Color;
        };
        const FLinearColor Earlier = Read(Mip.Get(), 3, 8);
        const FLinearColor Background = Read(Mip.Get(), 14, 2);
        const FLinearColor BeforeS = Read(Mip.Get(), 9, 12);
        const FLinearColor ConsumerPixel = Read(Rendered.Get(), 9, 12);
        const FLinearColor FinalBackground = Read(Rendered.Get(), 14, 6);
        const FLinearColor LaterD = Read(Rendered.Get(), 14, 2);
        const FLinearColor LaterE = Read(Rendered.Get(), 14, 14);
        const FString Observation = FString::Printf(TEXT(
            "frame=%llu draw=%d proxyCalls=%d size=%dx%d rect=%s nativeSceneRect=%s usedPostBuffers=%u mips=%d "
            "C=%s P_without_external_M_or_later_D=%s beforeS=%s consumerLOD4=%s finalBackground=%s laterD=%s laterE=%s formats=%d,%d\n"),
            static_cast<unsigned long long>(GFrameCounter), Draws, Drawer->Calls - CallsBefore,
            Drawer->InputSize.X, Drawer->InputSize.Y, *Drawer->InputRect.ToString(), *Drawer->NativeSceneRect.ToString(), Drawer->UsedPostBuffers,
            Drawer->MipCount, *Earlier.ToString(), *Background.ToString(), *BeforeS.ToString(), *ConsumerPixel.ToString(), *FinalBackground.ToString(),
            *LaterD.ToString(), *LaterE.ToString(), int32(Mip->GetFormat()), int32(Rendered->GetFormat()));
        Test.AddInfo(Observation);
        Test.TestTrue(TEXT("Save actual frame observations"), FFileHelper::SaveStringToFile(Observation,
            *(EvidenceDirectory / TEXT("observations.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
            &IFileManager::Get(), FILEWRITE_Append));
        Test.TestEqual(TEXT("One native capture during this frame's single DrawWidget"), Drawer->Calls - CallsBefore, 1);
        Test.TestEqual(TEXT("Actual Retainer target extent"), Drawer->InputSize, FIntPoint(16, 16));
        Test.TestTrue(TEXT("Copy uses this complete offscreen target, not an unfilled scene viewport"),
            Drawer->InputRect == FIntRect(0, 0, 16, 16));
        Test.TestEqual(TEXT("Actual five-level GPU mip texture"), Drawer->MipCount, 5);
        Test.TestTrue(TEXT("Current local target excludes yellow external M and later opaque D"),
            Background.R < .05f && Background.G < .05f && Background.B > .1f && Background.A < .4f);
        Test.TestTrue(TEXT("Current target excludes S itself"),
            BeforeS.R < .05f && BeforeS.G < .05f && BeforeS.A < .4f);
        const bool RedFrame = Draws == 1;
        Test.TestTrue(TEXT("Captured C is from this frame, not previous frame"),
            RedFrame ? Earlier.R > Earlier.G + .2f : Earlier.G > Earlier.R + .2f);
        Test.TestTrue(TEXT("Later S consumes current-frame LOD4 and material marker"),
            ConsumerPixel.B > .2f && (RedFrame ? ConsumerPixel.R > ConsumerPixel.G + .1f : ConsumerPixel.G > ConsumerPixel.R + .1f));
        Test.TestTrue(TEXT("Later D remains visible outside captured prefix"), LaterD.B > FinalBackground.B + .1f);
        Test.TestTrue(TEXT("External later E remains visible"), LaterE.G > .8f && LaterE.B > .8f);
        return Draws == 2;
    }

    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    TStrongObjectPtr<UMaterial> Material;
    TStrongObjectPtr<UMaterialInstanceDynamic> Consumer;
    TStrongObjectPtr<UTextureRenderTarget2D> Mip;
    TStrongObjectPtr<UTextureRenderTarget2D> Rendered;
    TSharedPtr<SWidget> Root;
    TSharedPtr<SRetainerWidget> Retainer;
    TSharedPtr<SBackgroundCurrentTargetScene> Scene;
    TSharedPtr<FCurrentTargetDraw, ESPMode::ThreadSafe> Drawer;
    FString EvidenceDirectory;
    uint64 PreviousFrame = MAX_uint64;
    int32 Draws = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilBackgroundCurrentTargetMipTest,
    "Design2UMG.Resources.PencilBackgroundCurrentTargetMip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilBackgroundCurrentTargetMipTest::RunTest(const FString&)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("Editor world"), World)) return false;
    IConsoleVariable* Defer = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.DeferRetainedRenderingRenderThread"));
    if (!TestTrue(TEXT("This same-frame fixture requires existing non-deferred Retainer mode"),
        Defer && Defer->GetInt() == 0)) return false;
    TSharedRef<FBackgroundCurrentTargetFrames> Frames = MakeShared<FBackgroundCurrentTargetFrames>(*this);
    Frames->World = World;
    Frames->EvidenceDirectory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/BackgroundCurrentTargetMip") /
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!TestTrue(TEXT("Create evidence directory"), IFileManager::Get().MakeDirectory(*Frames->EvidenceDirectory, true)))
        return false;
    Frames->Mip.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
    Frames->Mip->RenderTargetFormat = RTF_RGBA16f;
    Frames->Mip->bAutoGenerateMips = true;
    Frames->Mip->bSupportsUAV = true;
    Frames->Mip->ClearColor = FLinearColor::Black;
    Frames->Mip->InitAutoFormat(16, 16);
    Frames->Mip->UpdateResourceImmediate(true);
    Frames->Rendered.Reset(FWidgetRenderer::CreateTargetFor(FVector2D(16), TF_Nearest, true));
    Frames->Material.Reset(NewObject<UMaterial>(GetTransientPackage()));
    Frames->Material->MaterialDomain = MD_UI;
    Frames->Material->BlendMode = BLEND_Translucent;
    auto* Texture = NewObject<UMaterialExpressionTextureObjectParameter>(Frames->Material.Get());
    Texture->ParameterName = TEXT("CurrentTargetMip");
    Texture->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    Texture->SamplerType = SAMPLERTYPE_Color;
    auto* Sample = NewObject<UMaterialExpressionCustom>(Frames->Material.Get());
    Sample->OutputType = CMOT_Float3;
    Sample->Code = TEXT("float4 p=Texture2DSampleLevel(Source,SourceSampler,float2(0.5,0.5),4);return float3(p.rg,0.75);");
    FCustomInput Input;
    Input.InputName = TEXT("Source");
    Input.Input.Expression = Texture;
    Sample->Inputs.Add(Input);
    Design2UMGMaterialAccess::AddExpression(*Frames->Material, *Texture);
    Design2UMGMaterialAccess::AddExpression(*Frames->Material, *Sample);
    Frames->Material->GetExpressionInputForProperty(MP_EmissiveColor)->Expression = Sample;
    Frames->Material->PostEditChange();
    Design2UMGMaterialAccess::WaitForCompilation(*Frames->Material);
    Frames->Consumer.Reset(UMaterialInstanceDynamic::Create(Frames->Material.Get(), GetTransientPackage()));
    Frames->Consumer->SetTextureParameterValue(TEXT("CurrentTargetMip"), Frames->Mip.Get());
    Frames->Drawer = MakeShared<FCurrentTargetDraw, ESPMode::ThreadSafe>(Frames->Mip->GameThread_GetRenderTargetResource());
    SAssignNew(Frames->Scene, SBackgroundCurrentTargetScene).Drawer(Frames->Drawer).Material(Frames->Consumer.Get());
    SAssignNew(Frames->Retainer, SRetainerWidget).RenderOnPhase(true).Phase(0).PhaseCount(1)
        [Frames->Scene.ToSharedRef()];
    Frames->Retainer->SetWorld(World);
    Frames->Retainer->SetRetainedRendering(true);
    Frames->Retainer->SetRenderOpacity(.6f);
    Frames->Retainer->SetClipping(EWidgetClipping::ClipToBounds);
    Frames->Root = SNew(SOverlay)
        + SOverlay::Slot()[SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor::Yellow)]
        + SOverlay::Slot()[Frames->Retainer.ToSharedRef()]
        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
            [SNew(SBox).WidthOverride(4).HeightOverride(4)
                [SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(0, 1, 1, 1))]];
    AddInfo(TEXT("Test-only native custom-draw proxy; stock subsystem unchanged. Retainer-local current target is not arbitrary external background."));
    FAutomationTestFramework::Get().EnqueueLatentCommand(Frames);
    return true;
}

namespace
{
TStrongObjectPtr<UMaterial> MakeSegmentMaterial(EMaterialDomain Domain, const TCHAR* Code)
{
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(GetTransientPackage()));
    Material->MaterialDomain = Domain; Material->BlendMode = BLEND_AlphaComposite;
    Material->SetShadingModel(MSM_Unlit);
    auto* Sample = NewObject<UMaterialExpressionCustom>(Material.Get()); Sample->Code = Code; Sample->OutputType = CMOT_Float4;
    const TCHAR* Parameters[] = {TEXT("Prefix"), TEXT("RootMap"), TEXT("Combined"), TEXT("Texture")};
    const TCHAR* Inputs[] = {TEXT("Pfx"), TEXT("Map"), TEXT("BG"), TEXT("Tail")};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        auto* Texture = NewObject<UMaterialExpressionTextureObjectParameter>(Material.Get());
        Texture->ParameterName = Parameters[Index]; Texture->SamplerType = SAMPLERTYPE_Color;
        Texture->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
        FCustomInput Input; Input.InputName = Inputs[Index]; Input.Input.Expression = Texture; Sample->Inputs.Add(Input);
        Design2UMGMaterialAccess::AddExpression(*Material, *Texture);
    }
    auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material.Get());
    FCustomInput UVInput; UVInput.InputName = TEXT("UV"); UVInput.Input.Expression = UV; Sample->Inputs.Add(UVInput);
    auto* RGB = NewObject<UMaterialExpressionComponentMask>(Material.Get()); RGB->Input.Expression = Sample;
    RGB->R = RGB->G = RGB->B = true; RGB->A = false;
    auto* Alpha = NewObject<UMaterialExpressionComponentMask>(Material.Get()); Alpha->Input.Expression = Sample;
    Alpha->R = Alpha->G = Alpha->B = false; Alpha->A = true;
    for (UMaterialExpression* Expression : TArray<UMaterialExpression*>{UV, Sample, RGB, Alpha})
        Design2UMGMaterialAccess::AddExpression(*Material, *Expression);
    Material->GetExpressionInputForProperty(MP_EmissiveColor)->Expression = RGB;
    Material->GetExpressionInputForProperty(MP_Opacity)->Expression = Alpha;
    if (Domain == MD_Surface) { auto* Inverse = NewObject<UMaterialExpressionOneMinus>(Material.Get());
        Inverse->Input.Expression = Alpha; Design2UMGMaterialAccess::AddExpression(*Material, *Inverse);
        Material->GetExpressionInputForProperty(MP_Opacity)->Expression = Inverse; }
    Material->PostEditChange(); Design2UMGMaterialAccess::WaitForCompilation(*Material); return Material;
}

class FCombinedBackgroundDraw final : public ICustomSlateElement
{
public:
    FCombinedBackgroundDraw(FTextureRenderTargetResource* InMap, FTextureRenderTargetResource* InCombined,
        const FMaterialRenderProxy* InMaterial) : Map(InMap), Combined(InCombined), Material(InMaterial) {}
    void Draw_RenderThread(FRDGBuilder& GraphBuilder, const FDrawPassInputs& Inputs) override
    {
        ++Calls;
        FRDGTextureRef MapTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Map->GetRenderTargetTexture(), TEXT("SegmentRootMap")));
        FRDGTextureRef CombinedTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Combined->GetRenderTargetTexture(), TEXT("SegmentCombined")));
        GraphBuilder.UseInternalAccessMode(MapTexture);
        AddSlatePostProcessCopy(GraphBuilder, FScreenPassTexture(Inputs.OutputTexture), FScreenPassTexture(MapTexture));
        GraphBuilder.UseExternalAccessMode(MapTexture, ERHIAccess::SRVMask);
        GraphBuilder.UseInternalAccessMode(CombinedTexture); AddClearRenderTargetPass(GraphBuilder, CombinedTexture, FLinearColor::Black);
        FCanvas* Canvas = FCanvas::Create(GraphBuilder, CombinedTexture, nullptr, FGameTime::CreateDilated(0, 0, 0, 0), GMaxRHIFeatureLevel);
        Canvas->AddTileRenderItem(0, 0, 16, 16, 0, 0, 1, 1, Material, FHitProxyId(), true, FColor::White);
        Canvas->Flush_RenderThread(GraphBuilder);
        FGenerateMips::Execute(GraphBuilder, GMaxRHIFeatureLevel, CombinedTexture);
        Mips = CombinedTexture->Desc.NumMips; GraphBuilder.UseExternalAccessMode(CombinedTexture, ERHIAccess::SRVMask);
    }
    int32 Calls = 0, Mips = 0;
private:
    FTextureRenderTargetResource* Map;
    FTextureRenderTargetResource* Combined;
    const FMaterialRenderProxy* Material;
};

class SSegmentBoundary final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SSegmentBoundary) {} SLATE_END_ARGS()
    void Construct(const FArguments&) {}
    FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle&, bool) const override
    { FSlateDrawElement::MakeCustom(Elements, Layer, Drawer); return Layer + 1; }
    TSharedPtr<FCombinedBackgroundDraw, ESPMode::ThreadSafe> Drawer;
};

class FSegmentedBackgroundFrames final : public IAutomationLatentCommand
{
public:
    explicit FSegmentedBackgroundFrames(FAutomationTestBase& Test) : Base(MakeShared<FBackgroundCurrentTargetFrames>(Test)) {}
    ~FSegmentedBackgroundFrames() override { Base->Root.Reset(); Base->Retainer.Reset(); Base->Scene.Reset(); FlushRenderingCommands(); }
    bool Update() override
    {
        if (Base->PreviousFrame == GFrameCounter) return false;
        Base->PreviousFrame = GFrameCounter; Base->Scene->SetMarker(Base->Draws == 0 ? FLinearColor::Red : FLinearColor::Green);
        Base->Retainer->RequestRender(); const int32 LocalBefore = Base->Drawer->Calls, OuterBefore = Outer->Calls;
        FWidgetRenderer Renderer(true); Renderer.DrawWidget(Base->Rendered.Get(), Base->Root.ToSharedRef(), FVector2D(16), 1.f / 60.f, false);
        FlushRenderingCommands(); ++Base->Draws;
        const auto Read = [&](UTextureRenderTarget2D* Target, int32 X, int32 Y) {
            FLinearColor Value = UKismetRenderingLibrary::ReadRenderTargetRawPixel(Base->World, Target, X, Y, false);
            return Target->GetFormat() == PF_B8G8R8A8 ? Value / 255.f : Value; };
        auto* Suffix = Cast<UTextureRenderTarget2D>(Base->Consumer->K2_GetTextureParameterValue(TEXT("Texture")));
        if (!Base->Test.TestNotNull(TEXT("Actual native Retainer suffix target"), Suffix)) return true;
        const FLinearColor Prefix = Read(Base->Mip.Get(), 3, 8), SuffixEmpty = Read(Suffix, 3, 8), SuffixD = Read(Suffix, 14, 2);
        const FLinearColor RootMap = Read(Map.Get(), 14, 6), CombinedC = Read(Combined.Get(), 3, 8), CombinedB = Read(Combined.Get(), 14, 6);
        const FLinearColor ConsumerS = Read(Base->Rendered.Get(), 9, 12), PanelC = Read(Base->Rendered.Get(), 3, 8), E = Read(Base->Rendered.Get(), 14, 14);
        const FString Observation = FString::Printf(TEXT("frame=%llu draw=%d local=%d outer=%d mips=%d prefixMips=%d prefix=%s suffixEmpty=%s suffixD=%s M=%s BG_C=%s BG_B=%s S_LOD4=%s P_opacity=%s E=%s\n"),
            static_cast<unsigned long long>(GFrameCounter), Base->Draws, Base->Drawer->Calls-LocalBefore, Outer->Calls-OuterBefore, Outer->Mips, Base->Drawer->MipCount,
            *Prefix.ToString(), *SuffixEmpty.ToString(), *SuffixD.ToString(), *RootMap.ToString(), *CombinedC.ToString(), *CombinedB.ToString(), *ConsumerS.ToString(), *PanelC.ToString(), *E.ToString());
        Base->Test.AddInfo(Observation); Base->Test.TestTrue(TEXT("Save segmented observations"), FFileHelper::SaveStringToFile(Observation,
            *(Base->EvidenceDirectory / TEXT("observations.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append));
        Base->Test.TestEqual(TEXT("Local prefix capture exactly once"), Base->Drawer->Calls-LocalBefore, 1);
        Base->Test.TestEqual(TEXT("Outer combined capture exactly once"), Outer->Calls-OuterBefore, 1);
        Base->Test.TestEqual(TEXT("True combined BG mip chain"), Outer->Mips, 5);
        Base->Test.TestTrue(TEXT("Clear separates C from suffix while later D draws once"), SuffixEmpty.A < .02f && SuffixD.B > .8f && SuffixD.A > .8f);
        Base->Test.TestTrue(TEXT("Outer prefix is M before P output"), RootMap.R > .8f && RootMap.G > .8f && RootMap.B < .02f);
        Base->Test.TestTrue(TEXT("Combined BG physically contains M under translucent P background"), CombinedB.R > .2f && CombinedB.G > .2f && CombinedB.B > .02f && CombinedB.A > .98f);
        const bool Red = Base->Draws == 1;
        Base->Test.TestTrue(TEXT("Prefix and physically combined BG are current-frame C"), Red ? Prefix.R > Prefix.G+.2f && CombinedC.R > CombinedC.G+.2f : Prefix.G > Prefix.R+.2f && CombinedC.G > CombinedC.R+.2f);
        Base->Test.TestTrue(TEXT("Later S samples current combined LOD4"), ConsumerS.B > .2f && (Red ? ConsumerS.R > ConsumerS.G+.1f : ConsumerS.G > ConsumerS.R+.1f));
        const float TransmittedM = Red ? PanelC.G : PanelC.R;
        Base->Test.TestTrue(TEXT("Logical P opacity .6 applied once by final UI material"), TransmittedM > .28f && TransmittedM < .52f);
        Base->Test.TestTrue(TEXT("Later external E preserved"), E.G > .8f && E.B > .8f);
        return Base->Draws == 2;
    }
    TSharedRef<FBackgroundCurrentTargetFrames> Base;
    TStrongObjectPtr<UTextureRenderTarget2D> Map, Combined;
    TStrongObjectPtr<UMaterial> Copy, Hidden;
    TStrongObjectPtr<UMaterialInstanceDynamic> CopyInstance;
    TSharedPtr<FCombinedBackgroundDraw, ESPMode::ThreadSafe> Outer;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilBackgroundSegmentedMipTest, "Design2UMG.Resources.PencilBackgroundSegmentedMip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilBackgroundSegmentedMipTest::RunTest(const FString&)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    const auto* Defer = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.DeferRetainedRenderingRenderThread"));
    if (!TestNotNull(TEXT("Editor world"), World) || !TestTrue(TEXT("Segment trial requires immediate Retainer update"), Defer && Defer->GetInt() == 0)) return false;
    auto Frames = MakeShared<FSegmentedBackgroundFrames>(*this); auto& Base = *Frames->Base; Base.World = World;
    Base.EvidenceDirectory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/BackgroundSegmentedMip") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!TestTrue(TEXT("Create segment evidence directory"), IFileManager::Get().MakeDirectory(*Base.EvidenceDirectory, true))) return false;
    const auto Target = [](bool Mips) { auto* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
        RT->bAutoGenerateMips = Mips; RT->bSupportsUAV = Mips; RT->ClearColor = FLinearColor::Transparent;
        RT->InitCustomFormat(16, 16, PF_FloatRGBA, true); RT->UpdateResourceImmediate(true); return RT; };
    Base.Mip.Reset(Target(true)); Base.Rendered.Reset(Target(false)); Frames->Map.Reset(Target(false)); Frames->Combined.Reset(Target(true));
    Frames->Hidden = MakeSegmentMaterial(MD_UI, TEXT("return float4(0,0,0,0);"));
    Frames->Copy = MakeSegmentMaterial(MD_Surface, TEXT("float4 p=Texture2DSampleLevel(Pfx,PfxSampler,UV,0);float4 m=Texture2DSampleLevel(Map,MapSampler,UV,0);return float4(p.rgb+m.rgb*(1-p.a),1);"));
    Base.Material = MakeSegmentMaterial(MD_UI, TEXT("float4 p=Texture2DSampleLevel(Pfx,PfxSampler,UV,0);if(UV.x>=0.5&&UV.x<0.75&&UV.y>=0.5){float4 b=Texture2DSampleLevel(BG,BGSampler,float2(0.5,0.5),4);p=float4(b.rg,0.75,1);}float4 d=Texture2DSampleLevel(Tail,TailSampler,UV,0);return (d+p*(1-d.a))*0.6;"));
    Frames->CopyInstance.Reset(UMaterialInstanceDynamic::Create(Frames->Copy.Get(), GetTransientPackage()));
    Frames->CopyInstance->SetTextureParameterValue(TEXT("Prefix"), Base.Mip.Get()); Frames->CopyInstance->SetTextureParameterValue(TEXT("RootMap"), Frames->Map.Get());
    Base.Drawer = MakeShared<FCurrentTargetDraw, ESPMode::ThreadSafe>(Base.Mip->GameThread_GetRenderTargetResource(), true);
    SAssignNew(Base.Scene, SBackgroundCurrentTargetScene).Drawer(Base.Drawer).Material(Frames->Hidden.Get());
    SAssignNew(Base.Retainer, SRetainerWidget).RenderOnPhase(true).Phase(0).PhaseCount(1)[Base.Scene.ToSharedRef()];
    Base.Retainer->SetWorld(World); Base.Retainer->SetRetainedRendering(true); Base.Retainer->SetClipping(EWidgetClipping::ClipToBounds);
    Base.Retainer->SetEffectMaterial(Base.Material.Get()); Base.Retainer->SetTextureParameter(TEXT("Texture"));
    Base.Consumer.Reset(Base.Retainer->GetEffectMaterial()); Base.Consumer->SetTextureParameterValue(TEXT("Prefix"), Base.Mip.Get()); Base.Consumer->SetTextureParameterValue(TEXT("Combined"), Frames->Combined.Get());
    Frames->Outer = MakeShared<FCombinedBackgroundDraw, ESPMode::ThreadSafe>(Frames->Map->GameThread_GetRenderTargetResource(), Frames->Combined->GameThread_GetRenderTargetResource(), Frames->CopyInstance->GetRenderProxy());
    auto Boundary = SNew(SSegmentBoundary).Visibility(EVisibility::HitTestInvisible); Boundary->Drawer = Frames->Outer;
    Base.Root = SNew(SOverlay)
        + SOverlay::Slot()[SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::Yellow)]
        + SOverlay::Slot()[Boundary]
        + SOverlay::Slot()[Base.Retainer.ToSharedRef()]
        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)[SNew(SBox).WidthOverride(4).HeightOverride(4)[SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,1,1,1))]];
    AddInfo(TEXT("One logical P opacity .6 is applied by final UI material; native Retainer opacity stays 1. No general dynamic ancestor-style claim."));
    FAutomationTestFramework::Get().EnqueueLatentCommand(Frames); return true;
}
namespace
{
bool BuildPrefixPaintBlueprint(UWidgetBlueprint& Blueprint)
{
    using namespace Design2UMGPrototypeGraphWiring;
    const TPair<FName, UClass*> Variables[] = {
        {TEXT("ProducerM"), URetainerBox::StaticClass()}, {TEXT("ProducerQ"), URetainerBox::StaticClass()},
        {TEXT("ProducerP"), URetainerBox::StaticClass()}, {TEXT("CopyMaterial"), UMaterialInstanceDynamic::StaticClass()},
        {TEXT("ConsumerMaterial"), UMaterialInstanceDynamic::StaticClass()},
        {TEXT("CombinedTarget"), UTextureRenderTarget2D::StaticClass()}, {TEXT("PaintWorld"), UObject::StaticClass()}};
    for (const auto& Variable : Variables)
    {
        FEdGraphPinType Type;
        Type.PinCategory = UEdGraphSchema_K2::PC_Object;
        Type.PinSubCategoryObject = Variable.Value;
        if (!FBlueprintEditorUtils::AddMemberVariable(&Blueprint, Variable.Key, Type)) return false;
    }
    FKismetEditorUtilities::CompileBlueprint(&Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
    if (Blueprint.Status == BS_Error || !Blueprint.SkeletonGeneratedClass) return false;
    UFunction* PaintFunction = UUserWidget::StaticClass()->FindFunctionByName(TEXT("OnPaint"));
    if (!PaintFunction) return false;
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(&Blueprint, TEXT("OnPaint"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(&Blueprint, Graph, false, UUserWidget::StaticClass());
    TArray<UK2Node_FunctionEntry*> Entries;
    Graph->GetNodesOfClass(Entries);
    if (Entries.Num() != 1) return false;
    TArray<UEdGraphNode*> ExecChain{Entries[0]};
    const auto* Schema = GetDefault<UEdGraphSchema_K2>();
    bool Linked = true;
    const auto Link = [&](UEdGraphPin* Output, UEdGraphPin* Input)
    {
        Linked = Output && Input && Schema->TryCreateConnection(Output, Input) && Linked;
    };
    const auto ReadVariable = [&](FName Name) -> UEdGraphPin*
    {
        const FProperty* Property = Blueprint.SkeletonGeneratedClass->FindPropertyByName(Name);
        return Property ? CreateVariableGet(Graph, &Blueprint, Property)->FindPin(Name, EGPD_Output) : nullptr;
    };
    const auto Call = [&](UClass* Class, FName Name) -> UK2Node_CallFunction*
    {
        UFunction* Function = Class->FindFunctionByName(Name);
        if (!Function) { Linked = false; return nullptr; }
        auto* Node = CreateMarkerNode<UK2Node_CallFunction>(Graph, [Function](UK2Node_CallFunction* NewNode) { NewNode->SetFromFunction(Function); });
        if (Node->FindPin(UEdGraphSchema_K2::PN_Execute)) ExecChain.Add(Node);
        return Node;
    };
    const auto SetTexture = [&](FName MaterialVariable, FName Parameter, UEdGraphPin* Texture)
    {
        auto* Set = Call(UMaterialInstanceDynamic::StaticClass(), TEXT("SetTextureParameterValue"));
        if (!Set) return;
        Link(ReadVariable(MaterialVariable), Set->FindPin(UEdGraphSchema_K2::PN_Self));
        Schema->TrySetDefaultValue(*Set->FindPinChecked(TEXT("ParameterName")), Parameter.ToString());
        Link(Texture, Set->FindPin(TEXT("Value")));
    };
    const FName ProducerNames[] = {TEXT("ProducerM"), TEXT("ProducerQ"), TEXT("ProducerP")};
    const FName Parameters[] = {TEXT("Texture"), TEXT("RootMap"), TEXT("Prefix")};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto* Effect = Call(URetainerBox::StaticClass(), TEXT("GetEffectMaterial"));
        auto* Texture = Call(UMaterialInstanceDynamic::StaticClass(), TEXT("K2_GetTextureParameterValue"));
        if (!Effect || !Texture) return false;
        Link(ReadVariable(ProducerNames[Index]), Effect->FindPin(UEdGraphSchema_K2::PN_Self));
        Link(Effect->GetReturnValuePin(), Texture->FindPin(UEdGraphSchema_K2::PN_Self));
        Schema->TrySetDefaultValue(*Texture->FindPinChecked(TEXT("ParameterName")), TEXT("Texture"));
        SetTexture(TEXT("CopyMaterial"), Parameters[Index], Texture->GetReturnValuePin());
        if (Index > 0) SetTexture(TEXT("ConsumerMaterial"), Parameters[Index], Texture->GetReturnValuePin());
    }
    auto* Clear = Call(UKismetRenderingLibrary::StaticClass(), TEXT("ClearRenderTarget2D"));
    if (!Clear) return false;
    Link(ReadVariable(TEXT("PaintWorld")), Clear->FindPin(TEXT("WorldContextObject")));
    Link(ReadVariable(TEXT("CombinedTarget")), Clear->FindPin(TEXT("TextureRenderTarget")));
    Schema->TrySetDefaultValue(*Clear->FindPinChecked(TEXT("ClearColor")), TEXT("(R=0,G=0,B=0,A=1)"));
    auto* Draw = Call(UKismetRenderingLibrary::StaticClass(), TEXT("DrawMaterialToRenderTarget"));
    if (!Draw) return false;
    Link(ReadVariable(TEXT("PaintWorld")), Draw->FindPin(TEXT("WorldContextObject")));
    Link(ReadVariable(TEXT("CombinedTarget")), Draw->FindPin(TEXT("TextureRenderTarget")));
    Link(ReadVariable(TEXT("CopyMaterial")), Draw->FindPin(TEXT("Material")));
    TArray<UK2Node_FunctionResult*> Results;
    Graph->GetNodesOfClass(Results);
    for (auto* Result : Results)
    {
        if (auto* Context = Result->FindPin(TEXT("Context"), EGPD_Input)) Link(Entries[0]->FindPin(TEXT("Context"), EGPD_Output), Context);
        ExecChain.Add(Result);
    }
    for (int32 Index = 0; Index + 1 < ExecChain.Num(); ++Index)
    {
        Link(ExecChain[Index]->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output),
            ExecChain[Index + 1]->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input));
    }
    if (!Linked) return false;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
    FKismetEditorUtilities::CompileBlueprint(&Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
    const UFunction* CompiledPaint = Blueprint.GeneratedClass ? Blueprint.GeneratedClass->FindFunctionByName(TEXT("OnPaint")) : nullptr;
    return Blueprint.Status != BS_Error && CompiledPaint && CompiledPaint->GetSuperFunction() == PaintFunction && CompiledPaint->Script.Num() > 0;
}

class FGeneratedPrefixFrames final : public IAutomationLatentCommand
{
public:
    explicit FGeneratedPrefixFrames(FAutomationTestBase& InTest) : Test(InTest) {}
    ~FGeneratedPrefixFrames() override
    {
        Root.Reset();
        if (Tree.IsValid()) Tree->ReleaseSlateResources(true);
        FlushRenderingCommands();
        if (Combined.IsValid()) UKismetRenderingLibrary::ReleaseRenderTarget2D(Combined.Get());
        if (Rendered.IsValid()) UKismetRenderingLibrary::ReleaseRenderTarget2D(Rendered.Get());
        FlushRenderingCommands();
    }
    bool Update() override
    {
        if (PreviousFrame == GFrameCounter) return false;
        PreviousFrame = GFrameCounter;
        const bool Red = Draws == 0;
        DynamicContent->SetColorAndOpacity(Red ? FLinearColor::Red : FLinearColor::Green);
        for (auto* Producer : Producers) Producer->RequestRender();
        Group->RequestRender();
        FWidgetRenderer Renderer(true);
        Renderer.DrawWidget(Rendered.Get(), Root.ToSharedRef(), FVector2D(20, 16), 1.f / 60.f, false);
        FlushRenderingCommands();
        ++Draws;
        const auto Read = [&](UTextureRenderTarget2D* Target, int32 X, int32 Y)
        {
            FLinearColor Value = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Target, X, Y, false);
            return Target->GetFormat() == PF_B8G8R8A8 ? Value / 255.f : Value;
        };
        auto* EffectP = Producers[2]->GetEffectMaterial();
        auto* EffectGroup = Group->GetEffectMaterial();
        if (!Test.TestNotNull(TEXT("Native producer effect MID"), EffectP) || !Test.TestNotNull(TEXT("Native group effect MID"), EffectGroup)) return true;
        auto* RawP = Cast<UTextureRenderTarget2D>(EffectP->K2_GetTextureParameterValue(TEXT("Texture")));
        auto* RawGroup = Cast<UTextureRenderTarget2D>(EffectGroup->K2_GetTextureParameterValue(TEXT("Texture")));
        if (!Test.TestNotNull(TEXT("Native producer P target"), RawP) || !Test.TestNotNull(TEXT("Native group target"), RawGroup)) return true;
        const FLinearColor P = Read(RawP, 3, 8), PEmpty = Read(RawP, 15, 2);
        const FLinearColor C = Read(Combined.Get(), 3, 8), BeforeD = Read(Combined.Get(), 14, 2), BeforeE = Read(Combined.Get(), 14, 14);
        const FLinearColor S = Read(Rendered.Get(), 9, 12), RawS = Read(RawGroup, 9, 12), Panel = Read(Rendered.Get(), 3, 8);
        const FLinearColor D = Read(Rendered.Get(), 14, 2), E = Read(Rendered.Get(), 14, 14), Outside = Read(Rendered.Get(), 17, 2);
        const int32 Mips = Combined->GameThread_GetRenderTargetResource()->GetRenderTargetTexture()->GetNumMips();
        const FString Observation = FString::Printf(TEXT("frame=%llu draw=%d compiled_OnPaint_bytes=%d mips=%d P=%s Pempty=%s CombinedC=%s CombinedBeforeD=%s CombinedBeforeE=%s RawS_LOD4=%s S_LOD4=%s PanelOpacity=%s D=%s E=%s outsideClip=%s\n"),
            static_cast<unsigned long long>(GFrameCounter), Draws, Blueprint->GeneratedClass->FindFunctionByName(TEXT("OnPaint"))->Script.Num(), Mips,
            *P.ToString(), *PEmpty.ToString(), *C.ToString(), *BeforeD.ToString(), *BeforeE.ToString(), *RawS.ToString(), *S.ToString(), *Panel.ToString(), *D.ToString(), *E.ToString(), *Outside.ToString());
        Test.AddInfo(Observation);
        Test.TestTrue(TEXT("Save generated BP observations"), FFileHelper::SaveStringToFile(Observation, *(EvidenceDirectory / TEXT("observations.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append));
        Test.TestEqual(TEXT("Actual combined texture has mip chain"), Mips, 5);
        Test.TestTrue(TEXT("Producer native opacity remains one"), P.A > .95f && PEmpty.A < .05f);
        Test.TestTrue(TEXT("Generated OnPaint composes current dynamic P"), C.A > .95f && (Red ? C.R > .9f && C.G < .1f : C.G > .9f && C.R < .1f));
        Test.TestTrue(TEXT("Combined prefix excludes later D and E"), BeforeD.R > .65f && BeforeD.G > .65f && BeforeD.B > .18f && BeforeD.B < .32f && BeforeE.Equals(BeforeD, .04f));
        Test.TestTrue(TEXT("Consumer samples true combined mip rather than local pixel"), RawS.B > .08f && RawS.B < .18f && (Red ? RawS.R > RawS.G + .35f : RawS.G > RawS.R + .35f));
        Test.TestTrue(TEXT("Final scalar opacity applies once after nested content"), Red ? Panel.R > .9f && Panel.G > .3f && Panel.G < .5f : Panel.G > .9f && Panel.R > .3f && Panel.R < .5f);
        Test.TestTrue(TEXT("Final S retains current-frame mip signal"), Red ? S.R > S.G + .2f : S.G > S.R + .2f);
        Test.TestTrue(TEXT("Later D survives final group opacity"), D.B > .5f && D.R > .3f && D.R < .5f && D.G > .3f && D.G < .5f);
        Test.TestTrue(TEXT("External later E remains opaque"), E.G > .9f && E.B > .9f && E.R < .1f);
        Test.TestTrue(TEXT("Native producer clip excludes outside sentinel"), Outside.A < .05f);
        return Draws == 2;
    }
    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    TStrongObjectPtr<UWidgetBlueprint> Blueprint;
    TStrongObjectPtr<UOverlay> Tree;
    TStrongObjectPtr<UMaterial> Identity, Hidden, Copy, Consumer, Opacity;
    TStrongObjectPtr<UMaterialInstanceDynamic> CopyMID, ConsumerMID;
    TStrongObjectPtr<UTextureRenderTarget2D> Combined, Rendered;
    TSharedPtr<SWidget> Root;
    URetainerBox* Producers[3]{};
    URetainerBox* Group = nullptr;
    UImage* DynamicContent = nullptr;
    FString EvidenceDirectory;
    uint64 PreviousFrame = MAX_uint64;
    int32 Draws = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilBackgroundGeneratedPrefixMipTest,
    "Design2UMG.Resources.PencilBackgroundGeneratedPrefixMip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilBackgroundGeneratedPrefixMipTest::RunTest(const FString&)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    const auto* Defer = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.DeferRetainedRenderingRenderThread"));
    if (!TestNotNull(TEXT("Editor world"), World) || !TestTrue(TEXT("Generated BP candidate requires immediate Retainer update"), Defer && Defer->GetInt() == 0)) return false;
    auto Frames = MakeShared<FGeneratedPrefixFrames>(*this);
    Frames->World = World;
    Frames->EvidenceDirectory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/BackgroundGeneratedPrefixMip") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!TestTrue(TEXT("Create generated BP evidence directory"), IFileManager::Get().MakeDirectory(*Frames->EvidenceDirectory, true))) return false;
    Frames->Combined.Reset(UKismetRenderingLibrary::CreateRenderTarget2D(World, 16, 16, RTF_RGBA16f, FLinearColor(0, 0, 0, 1), true, true));
    Frames->Rendered.Reset(UKismetRenderingLibrary::CreateRenderTarget2D(World, 20, 16, RTF_RGBA16f, FLinearColor::Transparent));
    if (!TestNotNull(TEXT("Combined mip target"), Frames->Combined.Get()) || !TestNotNull(TEXT("Output target"), Frames->Rendered.Get())) return false;
    Frames->Identity = MakeSegmentMaterial(MD_UI, TEXT("return Texture2DSampleLevel(Tail,TailSampler,UV,0);"));
    Frames->Hidden = MakeSegmentMaterial(MD_UI, TEXT("return float4(0,0,0,0);"));
    Frames->Copy = MakeSegmentMaterial(MD_Surface, TEXT("float4 p=Texture2DSampleLevel(Pfx,PfxSampler,UV,0);float4 q=Texture2DSampleLevel(Map,MapSampler,UV,0);float4 m=Texture2DSampleLevel(Tail,TailSampler,UV,0);float4 c=p+q*(1-p.a);return c+m*(1-c.a);"));
    Frames->Consumer = MakeSegmentMaterial(MD_UI, TEXT("float4 p=Texture2DSampleLevel(Pfx,PfxSampler,UV,0);float4 q=Texture2DSampleLevel(Map,MapSampler,UV,0);float4 c=p+q*(1-p.a);if(UV.x>=0.5&&UV.x<0.75&&UV.y>=0.5)c=Texture2DSampleLevel(BG,BGSampler,float2(0.5,0.5),4);float3 lo=c.rgb/12.92;float3 hi=pow((abs(c.rgb)+0.055)/1.055,2.4);return float4(c.r<=0.04045?lo.r:hi.r,c.g<=0.04045?lo.g:hi.g,c.b<=0.04045?lo.b:hi.b,c.a);"));
    Frames->Opacity = MakeSegmentMaterial(MD_UI, TEXT("return Texture2DSampleLevel(Tail,TailSampler,UV,0);"));
    auto* OpacityParameter = NewObject<UMaterialExpressionScalarParameter>(Frames->Opacity.Get());
    OpacityParameter->ParameterName = TEXT("GroupOpacity");
    OpacityParameter->DefaultValue = .6f;
    Design2UMGMaterialAccess::AddExpression(*Frames->Opacity, *OpacityParameter);
    for (EMaterialProperty Property : {MP_EmissiveColor, MP_Opacity})
    {
        FExpressionInput* Output = Frames->Opacity->GetExpressionInputForProperty(Property);
        auto* Multiply = NewObject<UMaterialExpressionMultiply>(Frames->Opacity.Get());
        Multiply->A = *Output;
        Multiply->B.Expression = OpacityParameter;
        Design2UMGMaterialAccess::AddExpression(*Frames->Opacity, *Multiply);
        Output->Expression = Multiply;
    }
    Frames->Opacity->PostEditChange();
    Design2UMGMaterialAccess::WaitForCompilation(*Frames->Opacity);
    Frames->CopyMID.Reset(UMaterialInstanceDynamic::Create(Frames->Copy.Get(), GetTransientPackage()));
    Frames->Blueprint.Reset(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UUserWidget::StaticClass(), GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UWidgetBlueprint::StaticClass(), TEXT("PrefixPaintMarker")), BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
    if (!TestNotNull(TEXT("Generated marker Blueprint"), Frames->Blueprint.Get())) return false;
    Frames->Blueprint->WidgetTree = NewObject<UWidgetTree>(Frames->Blueprint.Get(), TEXT("WidgetTree"));
    if (!TestTrue(TEXT("Compile actual OnPaint native-Kismet graph"), BuildPrefixPaintBlueprint(*Frames->Blueprint))) return false;
    UUserWidget* Marker = CreateWidget<UUserWidget>(World, TSubclassOf<UUserWidget>(Frames->Blueprint->GeneratedClass.Get()));
    if (!TestNotNull(TEXT("Generated empty paint marker instance"), Marker)) return false;
    Marker->ForceVolatile(true);
    Frames->Tree.Reset(NewObject<UOverlay>(World));
    const auto AddLayer = [](UOverlay* Panel, UWidget* Child)
    {
        UOverlaySlot* Slot = Panel->AddChildToOverlay(Child);
        Slot->SetHorizontalAlignment(HAlign_Fill); Slot->SetVerticalAlignment(VAlign_Fill);
    };
    const auto AddRect = [World](UCanvasPanel* Canvas, FVector2D Position, FVector2D Size, FLinearColor Color)
    {
        auto* Image = NewObject<UImage>(World);
        Image->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
        Image->SetColorAndOpacity(Color);
        UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Image);
        Slot->SetPosition(Position); Slot->SetSize(Size);
        return Image;
    };
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto* Canvas = NewObject<UCanvasPanel>(World);
        UImage* Content = AddRect(Canvas, FVector2D::ZeroVector, FVector2D(Index == 2 ? 8 : 16, 16), Index == 0 ? FLinearColor::Yellow : Index == 1 ? FLinearColor(0, 0, 1, .25f) : FLinearColor::Red);
        if (Index == 2) { Frames->DynamicContent = Content; AddRect(Canvas, FVector2D(16, 0), FVector2D(4), FLinearColor::White); }
        auto* Producer = NewObject<URetainerBox>(World);
        Producer->SetContent(Canvas); Producer->SetRetainRendering(true); Producer->SetRenderingPhase(0, 1);
        Producer->SetClipping(EWidgetClipping::ClipToBounds); Producer->SetRenderOpacity(1);
        Producer->SetTextureParameter(TEXT("Texture")); Producer->SetEffectMaterial(Index == 0 ? Frames->Identity.Get() : Frames->Hidden.Get());
        Frames->Producers[Index] = Producer;
    }
    auto* Q = NewObject<UOverlay>(World);
    auto* P = NewObject<UOverlay>(World);
    AddLayer(Q, Frames->Producers[1]); AddLayer(Q, P);
    AddLayer(P, Frames->Producers[2]); AddLayer(P, Marker);
    auto* ConsumerRetainer = NewObject<URetainerBox>(World);
    ConsumerRetainer->SetContent(NewObject<UCanvasPanel>(World));
    ConsumerRetainer->SetRetainRendering(true); ConsumerRetainer->SetRenderingPhase(0, 1);
    ConsumerRetainer->SetRenderOpacity(1); ConsumerRetainer->SetTextureParameter(TEXT("Texture"));
    ConsumerRetainer->SetEffectMaterial(Frames->Consumer.Get()); AddLayer(P, ConsumerRetainer);
    auto* LaterD = NewObject<UCanvasPanel>(World);
    AddRect(LaterD, FVector2D(12, 0), FVector2D(4), FLinearColor::Blue); AddLayer(P, LaterD);
    Frames->Group = NewObject<URetainerBox>(World);
    Frames->Group->SetContent(Q); Frames->Group->SetRetainRendering(true); Frames->Group->SetRenderingPhase(0, 1);
    Frames->Group->SetRenderOpacity(1); Frames->Group->SetEffectMaterial(Frames->Opacity.Get()); Frames->Group->SetTextureParameter(TEXT("Texture"));
    AddLayer(Frames->Tree.Get(), Frames->Producers[0]); AddLayer(Frames->Tree.Get(), Frames->Group);
    auto* LaterE = NewObject<UCanvasPanel>(World);
    AddRect(LaterE, FVector2D(12), FVector2D(4), FLinearColor(0, 1, 1, 1)); AddLayer(Frames->Tree.Get(), LaterE);
    Frames->Root = SNew(SOverlay) + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)[SNew(SBox).WidthOverride(16).HeightOverride(16)[Frames->Tree->TakeWidget()]];
    Frames->ConsumerMID.Reset(ConsumerRetainer->GetEffectMaterial());
    if (!TestNotNull(TEXT("Native S effect consumer MID"), Frames->ConsumerMID.Get())) return false;
    Frames->ConsumerMID->SetTextureParameterValue(TEXT("Combined"), Frames->Combined.Get());
    const TPair<FName, UObject*> Bindings[] = {{TEXT("ProducerM"), Frames->Producers[0]}, {TEXT("ProducerQ"), Frames->Producers[1]}, {TEXT("ProducerP"), Frames->Producers[2]},
        {TEXT("CopyMaterial"), Frames->CopyMID.Get()}, {TEXT("ConsumerMaterial"), Frames->ConsumerMID.Get()}, {TEXT("CombinedTarget"), Frames->Combined.Get()}, {TEXT("PaintWorld"), World}};
    for (const auto& Binding : Bindings)
    {
        auto* Property = FindFProperty<FObjectProperty>(Marker->GetClass(), Binding.Key);
        if (!TestNotNull(TEXT("Generated marker object member"), Property)) return false;
        Property->SetObjectPropertyValue_InContainer(Marker, Binding.Value);
    }
    AddInfo(TEXT("Generated OnPaint only: reads native Retainer textures, updates native MIDs, calls DrawMaterialToRenderTarget. GroupOpacity=.6 belongs solely to final material; native RenderOpacity=1. Defer=0 required; no claim for arbitrary native opacity edits, Defer=1, or production cost."));
    FAutomationTestFramework::Get().EnqueueLatentCommand(Frames);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/RetainerBox.h"
#include "Design2UMGWidgetMetadata.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PencilShaderRuntimeBindings.h"
#include "PreBuildResourceNormalizer.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/CoreStyle.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "TextureResource.h"
#include "UMGSchema.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
class FNativeBackgroundProductionFrames final : public IAutomationLatentCommand
{
public:
    explicit FNativeBackgroundProductionFrames(FAutomationTestBase& InTest) : Test(InTest) {}
    ~FNativeBackgroundProductionFrames() override
    {
        Slate.Reset();
        if (Instance.IsValid()) Instance->ReleaseSlateResources(true);
        Instance.Reset();
        if (Rendered.IsValid()) UKismetRenderingLibrary::ReleaseRenderTarget2D(Rendered.Get());
        FlushRenderingCommands();
        Blueprint.Reset();
        if (!AssetFolder.IsEmpty()) Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(AssetFolder);
    }

    bool Update() override
    {
        if (PreviousFrame == GFrameCounter) return false;
        PreviousFrame = GFrameCounter;
        const bool Red = Draws == 0;
        Prefix->SetColorAndOpacity(Red ? FLinearColor::Red : FLinearColor::Green);
        Object->SetColorAndOpacity(Red ? FLinearColor::Blue : FLinearColor::Yellow);
        for (auto* Capture : Captures) Capture->RequestRender();
        FWidgetRenderer Renderer(true);
        Renderer.DrawWidget(Rendered.Get(), Slate.ToSharedRef(), FVector2D(16), 1.f / 60.f, false);
        FlushRenderingCommands();
        ++Draws;

        auto* Material = Shader->GetEffectMaterial();
        if (!Test.TestNotNull(TEXT("Painted production shader MID"), Material)) return true;
        auto* Background = Cast<UTextureRenderTarget2D>(Material->K2_GetTextureParameterValue(TEXT("D2UBackground")));
        auto* BackgroundMip = Cast<UTextureRenderTarget2D>(Material->K2_GetTextureParameterValue(TEXT("D2UBackgroundMip")));
        auto* RawObject = Cast<UTextureRenderTarget2D>(Material->K2_GetTextureParameterValue(TEXT("D2UObject")));
        auto* ObjectMip = Cast<UTextureRenderTarget2D>(Material->K2_GetTextureParameterValue(TEXT("D2UObjectMip")));
        if (!Test.TestNotNull(TEXT("OnPaint publishes current background"), Background)
            || !Test.TestNotNull(TEXT("OnPaint publishes current background mip"), BackgroundMip)
            || !Test.TestNotNull(TEXT("OnPaint publishes independent object"), RawObject)
            || !Test.TestNotNull(TEXT("OnPaint publishes current object mip"), ObjectMip)) return true;
        Test.TestTrue(TEXT("Raw and mip samplers bind the generated native boundary targets"),
            Background == BackgroundMip && RawObject == ObjectMip && Background != RawObject
            && ReadObject(TEXT("Background")) == Background && ReadObject(TEXT("ObjectMip")) == ObjectMip);
        if (Draws == 1)
        {
            FirstBackground = Background;
            FirstObject = ObjectMip;
        }
        else Test.TestTrue(TEXT("Next natural frame updates the existing per-instance targets"),
            FirstBackground == Background && FirstObject == ObjectMip);

        int32 BackgroundMips = 0, ObjectMips = 0;
        auto* BackgroundResource = BackgroundMip->GameThread_GetRenderTargetResource();
        auto* ObjectResource = ObjectMip->GameThread_GetRenderTargetResource();
        ENQUEUE_RENDER_COMMAND(ReadNativeBackgroundMips)([BackgroundResource, ObjectResource, &BackgroundMips, &ObjectMips](FRHICommandListImmediate&)
        {
            BackgroundMips = BackgroundResource->GetRenderTargetTexture()->GetNumMips();
            ObjectMips = ObjectResource->GetRenderTargetTexture()->GetNumMips();
        });
        FlushRenderingCommands();
        const auto Read = [&](UTextureRenderTarget2D* Target, int32 X, int32 Y)
        {
            FLinearColor Value = UKismetRenderingLibrary::ReadRenderTargetRawPixel(Instance->GetWorld(), Target, X, Y, false);
            return Target->GetFormat() == PF_B8G8R8A8 ? Value / 255.f : Value;
        };
        const FLinearColor BackgroundPrefix = Read(Background, 2, 8), BackgroundObject = Read(Background, 10, 8);
        const FLinearColor BackgroundLater = Read(Background, 14, 2), ObjectPrefix = Read(RawObject, 2, 8);
        const FLinearColor ObjectPixel = Read(RawObject, 10, 8), ObjectLater = Read(RawObject, 14, 2);
        const FLinearColor RawBackgroundConsumer = Read(Rendered.Get(), 2, 12), MipBackgroundConsumer = Read(Rendered.Get(), 6, 12);
        const FLinearColor RawObjectConsumer = Read(Rendered.Get(), 10, 12), MipObjectConsumer = Read(Rendered.Get(), 14, 12);
        const FLinearColor Later = Read(Rendered.Get(), 14, 2);
        const FString Observation = FString::Printf(TEXT(
            "frame=%llu draw=%d OnPaint_bytes=%d background=%s object=%s dimensions=%dx%d,%dx%d mips=%d,%d "
            "backgroundPrefix=%s backgroundObject=%s backgroundLater=%s objectPrefix=%s objectPixel=%s objectLater=%s "
            "rawBGConsumer=%s mipBGConsumer=%s rawObjectConsumer=%s mipObjectConsumer=%s later=%s\n"),
            static_cast<unsigned long long>(GFrameCounter), Draws, Boundary->GetClass()->FindFunctionByName(TEXT("OnPaint"))->Script.Num(),
            *Background->GetPathName(), *ObjectMip->GetPathName(), Background->SizeX, Background->SizeY,
            ObjectMip->SizeX, ObjectMip->SizeY, BackgroundMips, ObjectMips, *BackgroundPrefix.ToString(),
            *BackgroundObject.ToString(), *BackgroundLater.ToString(), *ObjectPrefix.ToString(), *ObjectPixel.ToString(),
            *ObjectLater.ToString(), *RawBackgroundConsumer.ToString(), *MipBackgroundConsumer.ToString(),
            *RawObjectConsumer.ToString(), *MipObjectConsumer.ToString(), *Later.ToString());
        Test.AddInfo(Observation);
        Test.TestTrue(TEXT("Save production natural-frame observations"), FFileHelper::SaveStringToFile(Observation,
            *(EvidenceDirectory / TEXT("observations.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
            &IFileManager::Get(), FILEWRITE_Append));
        Test.TestTrue(TEXT("Both actual native textures have the consumer dimensions and real mip 4"),
            Background->SizeX == 16 && Background->SizeY == 16 && ObjectMip->SizeX == 16 && ObjectMip->SizeY == 16
            && BackgroundMips == 5 && ObjectMips == 5);
        Test.TestTrue(TEXT("Production background contains this frame's earlier sibling"),
            Red ? BackgroundPrefix.R > .8f && BackgroundPrefix.G < .05f
                : BackgroundPrefix.G > .8f && BackgroundPrefix.R < .05f);
        Test.TestTrue(TEXT("Production background includes the current local object"),
            Red ? BackgroundObject.B > .8f && BackgroundObject.R < .05f
                : BackgroundObject.R > .8f && BackgroundObject.G > .8f && BackgroundObject.B < .05f);
        Test.TestTrue(TEXT("Object texture is independent of earlier sibling and later layers"),
            ObjectPrefix.A < .05f && ObjectLater.A < .05f);
        Test.TestTrue(TEXT("Production object texture updates on this natural frame"),
            Red ? ObjectPixel.B > .8f && ObjectPixel.R < .05f
                : ObjectPixel.R > .8f && ObjectPixel.G > .8f && ObjectPixel.B < .05f);
        Test.TestTrue(TEXT("Background excludes its shader consumer and the later sentinel"), BackgroundLater.A < .05f);
        Test.TestTrue(TEXT("Actual production shader samples the current raw background"),
            Red ? RawBackgroundConsumer.R > RawBackgroundConsumer.G + .2f
                : RawBackgroundConsumer.G > RawBackgroundConsumer.R + .2f);
        Test.TestTrue(TEXT("Actual production shader samples this frame's nonzero background mip"),
            Red ? MipBackgroundConsumer.R > MipBackgroundConsumer.G + .05f && MipBackgroundConsumer.B > .02f
                : MipBackgroundConsumer.G > MipBackgroundConsumer.R + .05f && MipBackgroundConsumer.B < .05f);
        Test.TestTrue(TEXT("Actual production shader samples this frame's raw object"),
            Red ? RawObjectConsumer.B > .8f && RawObjectConsumer.R < .05f
                : RawObjectConsumer.R > .8f && RawObjectConsumer.G > .8f && RawObjectConsumer.B < .05f);
        Test.TestTrue(TEXT("Actual production shader samples a current generated object mip, not mip zero"),
            Red ? MipObjectConsumer.B > .02f && MipObjectConsumer.B < RawObjectConsumer.B - .1f && MipObjectConsumer.R < .05f
                : MipObjectConsumer.R > .02f && MipObjectConsumer.G > .02f
                    && MipObjectConsumer.R < RawObjectConsumer.R - .1f && MipObjectConsumer.B < .05f);
        Test.TestTrue(TEXT("Excluded later sentinel really paints in the final tree"), Later.R > .8f && Later.B > .8f && Later.G < .05f);
        return Draws == 2;
    }

    UObject* ReadObject(FName Name) const
    {
        const auto* Property = FindFProperty<FObjectPropertyBase>(Boundary->GetClass(), Name);
        return Property ? Property->GetObjectPropertyValue_InContainer(Boundary) : nullptr;
    }

    FAutomationTestBase& Test;
    TStrongObjectPtr<UWidgetBlueprint> Blueprint;
    TStrongObjectPtr<UUserWidget> Instance;
    TStrongObjectPtr<UTextureRenderTarget2D> Rendered;
    TSharedPtr<SWidget> Slate;
    UImage* Prefix = nullptr;
    UImage* Object = nullptr;
    URetainerBox* Shader = nullptr;
    UUserWidget* Boundary = nullptr;
    TArray<URetainerBox*> Captures;
    UTextureRenderTarget2D* FirstBackground = nullptr;
    UTextureRenderTarget2D* FirstObject = nullptr;
    FString AssetFolder, EvidenceDirectory;
    uint64 PreviousFrame = MAX_uint64;
    int32 Draws = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilNativeBackgroundProductionFramesTest,
    "Design2UMG.Resources.PencilNativeBackgroundProductionFrames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilNativeBackgroundProductionFramesTest::RunTest(const FString&)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    const auto* Defer = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.DeferRetainedRenderingRenderThread"));
    if (!TestNotNull(TEXT("Production background editor world"), World)
        || !TestTrue(TEXT("Existing immediate Retainer mode"), Defer && Defer->GetInt() == 0)) return false;
    auto Frames = MakeShared<FNativeBackgroundProductionFrames>(*this);
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Frames->EvidenceDirectory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/NativeBackgroundProductionFrames") / Id;
    Frames->AssetFolder = TEXT("/Game/Design2UMG/Automation/NativeBackgroundProductionFrames_") + Id;
    if (!TestTrue(TEXT("Create production frame evidence directory"), IFileManager::Get().MakeDirectory(*Frames->EvidenceDirectory, true))) return false;
    const FString Source = Frames->EvidenceDirectory / TEXT("samplers.glsl");
    if (!TestTrue(TEXT("Write production raw and mip sampler fixture"), FFileHelper::SaveStringToFile(TEXT(R"GLSL(#version 100
#extension GL_EXT_shader_texture_lod : require
precision mediump float;
uniform sampler2D RawBackground;
/** @mipmap true */ uniform sampler2D MipBackground;
uniform sampler2D RawObject;
/** @mipmap true */ uniform sampler2D MipObject;
void main(){
    vec4 p;
    if(gl_FragCoord.x<4.0)p=texture2D(RawBackground,vec2(0.125,0.5));
    else if(gl_FragCoord.x<8.0)p=texture2DLodEXT(MipBackground,vec2(0.5),4.0);
    else if(gl_FragCoord.x<12.0)p=texture2D(RawObject,vec2(0.625,0.5));
    else p=texture2DLodEXT(MipObject,vec2(0.5),4.0);
    gl_FragColor=vec4(p.rgb,1.0);
}
)GLSL"), *Source, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
    FUMGWidgetNode Node;
    Node.Type = TEXT("RetainerBox");
    Node.Bounds.W = Node.Bounds.H = 16;
    FDesignShaderData Shader;
    Shader.Url = Source;
    Shader.Size = FVector2D(16);
    for (const TCHAR* Uniform : {TEXT("RawBackground"), TEXT("MipBackground"), TEXT("RawObject"), TEXT("MipObject")})
    {
        FDesignShaderValue Value;
        Value.Kind = FDesignShaderValue::EKind::String;
        Value.String = FString(Uniform).Contains(TEXT("Background")) ? TEXT("pencil:background") : TEXT("pencil:object");
        Shader.Uniforms.Add(Uniform, Value);
    }
    Node.Effect.Shader = Shader;
    FString Error;
    if (!TestTrue(TEXT("Prepare actual production raw and mip shader"), FPreBuildResourceNormalizer::NormalizeGeneratedTextures(
        Node, Frames->AssetFolder, TEXT("NativeSamplers"), Error))) { AddError(Error); return false; }
    if (!TestEqual(TEXT("Production material preparation creates one composition source"), Node.Children.Num(), 1)) return false;
    auto* ShaderMaterial = LoadObject<UMaterialInterface>(nullptr, *Node.Children[0].Effect.EffectMaterial.Get(FString()));
    auto* Consumer = LoadObject<UMaterialInterface>(nullptr, *Node.Effect.EffectMaterial.Get(FString()));
    if (!TestNotNull(TEXT("Production sampler material"), ShaderMaterial)
        || !TestNotNull(TEXT("Production composition material"), Consumer)) return false;
    Frames->Blueprint.Reset(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UUserWidget::StaticClass(),
        GetTransientPackage(), *FString(TEXT("NativeBackgroundFrames_") + Id), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
    if (!TestNotNull(TEXT("Production frame blueprint"), Frames->Blueprint.Get())) return false;
    UWidgetTree* Tree = Frames->Blueprint->WidgetTree;
    auto* Root = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("NativeScene"));
    const auto Fill = [](UOverlay* Panel, UWidget* Widget)
    {
        auto* Slot = Panel->AddChildToOverlay(Widget);
        Slot->SetHorizontalAlignment(HAlign_Fill);
        Slot->SetVerticalAlignment(VAlign_Fill);
    };
    const auto Rect = [Tree](UCanvasPanel* Canvas, FName Name, FVector2D Position, FVector2D Size, FLinearColor Color)
    {
        auto* Image = Tree->ConstructWidget<UImage>(UImage::StaticClass(), Name);
        Image->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
        Image->SetColorAndOpacity(Color);
        auto* Slot = Canvas->AddChildToCanvas(Image);
        Slot->SetPosition(Position);
        Slot->SetSize(Size);
        return Image;
    };
    auto* Prefix = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("EarlierSibling"));
    Rect(Prefix, TEXT("PrefixMarker"), FVector2D::ZeroVector, FVector2D(8, 16), FLinearColor::Red);
    Fill(Root, Prefix);
    Design2UMGWidgetMetadata::SetBackgroundCaptureInsets(Prefix, FMargin(0));
    auto* Object = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LocalObject"));
    Rect(Object, TEXT("ObjectMarker"), FVector2D(8, 0), FVector2D(4, 16), FLinearColor::Blue);
    auto* Effect = Tree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass(), TEXT("SamplerEffect"));
    Effect->SetEffectMaterial(ShaderMaterial);
    Effect->SetTextureParameter(TEXT("D2UObject"));
    Effect->SetRetainRendering(true);
    Effect->SetRenderingPhase(0, 1);
    Effect->SetContent(Object);
    auto* BackgroundConsumer = Tree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass(), TEXT("BackgroundConsumer"));
    BackgroundConsumer->SetEffectMaterial(Consumer);
    BackgroundConsumer->SetTextureParameter(TEXT("D2USource"));
    BackgroundConsumer->SetRetainRendering(true);
    BackgroundConsumer->SetRenderingPhase(0, 1);
    BackgroundConsumer->SetContent(Effect);
    Fill(Root, BackgroundConsumer);
    auto* Later = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LaterLayer"));
    Rect(Later, TEXT("LaterSentinel"), FVector2D(12, 0), FVector2D(4), FLinearColor(1, 0, 1, 1));
    Fill(Root, Later);
    Tree->RootWidget = Root;
    if (!TestTrue(TEXT("Generate actual production paint boundaries"), PencilShaderRuntimeBindings::Prepare(Frames->Blueprint.Get(), Error)))
    { AddError(Error); return false; }
    FKismetEditorUtilities::CompileBlueprint(Frames->Blueprint.Get(), EBlueprintCompileOptions::SkipGarbageCollection);
    if (!TestTrue(TEXT("Production generated parent compiles"), Frames->Blueprint->Status == BS_UpToDate)) return false;
    Frames->Instance.Reset(CreateWidget<UUserWidget>(World, TSubclassOf<UUserWidget>(Frames->Blueprint->GeneratedClass.Get())));
    Frames->Rendered.Reset(UKismetRenderingLibrary::CreateRenderTarget2D(World, 16, 16, RTF_RGBA16f, FLinearColor::Transparent));
    if (!TestNotNull(TEXT("Production parent instance"), Frames->Instance.Get())
        || !TestNotNull(TEXT("Production frame output"), Frames->Rendered.Get())) return false;
    Frames->Slate = Frames->Instance->TakeWidget();
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Frames->Instance.Get(), Objects, true);
    int32 BeforeBoundaries = 0;
    for (auto* RuntimeObject : Objects)
    {
        if (auto* Capture = Cast<URetainerBox>(RuntimeObject)) Frames->Captures.Add(Capture);
        if (RuntimeObject->GetFName() == TEXT("SamplerEffect")) Frames->Shader = Cast<URetainerBox>(RuntimeObject);
        if (RuntimeObject->GetFName() == TEXT("PrefixMarker")) Frames->Prefix = Cast<UImage>(RuntimeObject);
        if (RuntimeObject->GetFName() == TEXT("ObjectMarker")) Frames->Object = Cast<UImage>(RuntimeObject);
        if (auto* Marker = Cast<UUserWidget>(RuntimeObject))
            if (FindFProperty<FObjectPropertyBase>(Marker->GetClass(), TEXT("Background"))
                && FindFProperty<FObjectPropertyBase>(Marker->GetClass(), TEXT("ObjectMip")))
            {
                Frames->Boundary = Marker;
                ++BeforeBoundaries;
            }
    }
    if (!TestNotNull(TEXT("Instantiated native prefix marker"), Frames->Prefix)
        || !TestNotNull(TEXT("Instantiated native object marker"), Frames->Object)
        || !TestNotNull(TEXT("Instantiated production shader"), Frames->Shader)
        || !TestNotNull(TEXT("Instantiated production before-paint boundary"), Frames->Boundary)
        || !TestEqual(TEXT("Exactly one production object/background boundary"), BeforeBoundaries, 1)) return false;
    const UFunction* Paint = Frames->Boundary->GetClass()->FindFunctionByName(TEXT("OnPaint"));
    if (!TestTrue(TEXT("Production boundary owns compiled native OnPaint bytecode"),
        Paint && Paint->GetSuperFunction() == UUserWidget::StaticClass()->FindFunctionByName(TEXT("OnPaint")) && Paint->Script.Num() > 0)) return false;
    AddInfo(TEXT("Production Prepare/NormalizeGeneratedTextures only; native generated tree paints once on each of two natural frames. Raw and mip shader samplers observe independently changing sibling/object content; later sentinel is excluded from both captures."));
    FAutomationTestFramework::Get().EnqueueLatentCommand(Frames);
    return true;
}
#endif
