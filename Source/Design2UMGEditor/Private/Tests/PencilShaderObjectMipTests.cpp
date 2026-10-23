// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/RetainerBox.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Design2UMGMaterialAccess.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "PencilShaderRuntimeBindings.h"
#include "ObjectTools.h"
#include "PreBuildResourceNormalizer.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "UMGSchema.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilShaderObjectMipBindingTest,
    "Design2UMG.Resources.PencilShaderObjectMipBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilShaderObjectMipBindingTest::RunTest(const FString&)
{
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Source = FPaths::ProjectSavedDir() / TEXT("Design2UMG/ShaderTests") / (Id + TEXT(".glsl"));
    TArray<UObject*> Materials;
    ON_SCOPE_EXIT {
        if (!Materials.IsEmpty())
            ObjectTools::ForceDeleteObjects(Materials, false);
        IFileManager::Get().Delete(*Source);
    };
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Source), true);
    if (!TestTrue(TEXT("Object sampler fixture writes"), FFileHelper::SaveStringToFile(TEXT(
        "precision mediump float;\n/** @mipmap true */ uniform sampler2D MipObject;\n"
        "uniform sampler2D RawObject;\nvoid main(){gl_FragColor=0.5*"
        "(texture2D(MipObject,vec2(textureSize(MipObject,2))/vec2(textureSize(MipObject)))+"
        "texture2D(RawObject,vec2(textureSize(RawObject,8))/vec2(textureSize(RawObject))));}"),
        *Source, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
        return false;

    FUMGWidgetNode Node;
    Node.Type = TEXT("RetainerBox");
    Node.Bounds.W = 20;
    Node.Bounds.H = 12;
    FDesignShaderData Shader;
    Shader.Url = Source;
    Shader.Size = FVector2D(20, 12);
    FDesignShaderValue Object;
    Object.Kind = FDesignShaderValue::EKind::String;
    Object.String = TEXT("pencil:object");
    Shader.Uniforms.Add(TEXT("MipObject"), Object);
    Shader.Uniforms.Add(TEXT("RawObject"), Object);
    Node.Effect.Shader = Shader;
    FString Error;
    if (!TestTrue(TEXT("Object mip passes production material preparation"),
        FPreBuildResourceNormalizer::NormalizeGeneratedTextures(
            Node, TEXT("/Game/Design2UMG/Automation/ObjectMips_") + Id, TEXT("ObjectMips"), Error)))
    {
        AddError(Error);
        return false;
    }
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Node.Effect.EffectMaterial.Get(FString()));
    if (!TestNotNull(TEXT("Effect material exists"), Material))
        return false;
    Materials.Add(Material);
    bool NativeMipQuery = false;
    for (const UMaterialExpression* Expression : Design2UMGMaterialAccess::GetExpressions(*CastChecked<UMaterial>(Material)))
        if (const auto* Query = Cast<UMaterialExpressionCustom>(Expression))
            if (Query->Code.Contains(TEXT("GetDimensions")))
                for (const auto& Input : Query->Inputs)
                    if (const auto* Texture = Cast<UMaterialExpressionTextureObjectParameter>(Input.Input.Expression))
                        NativeMipQuery |= Texture->ParameterName == TEXT("D2UObjectMip");
    TestTrue(TEXT("LOD size queries actual mip count from the native runtime object binding"), NativeMipQuery);
    UTexture* MipDefault = nullptr;
    TestTrue(TEXT("Mip sampler has its own runtime texture parameter"),
        Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("D2UObjectMip")), MipDefault));
    UTexture* RawDefault = nullptr;
    TestTrue(TEXT("Unmipped sampler keeps its independent raw object parameter"),
        Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("D2UObject")), RawDefault));

    TStrongObjectPtr<UWidgetBlueprint> Blueprint(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), GetTransientPackage(), *FString(TEXT("ObjectMip_") + Id), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
    if (!TestNotNull(TEXT("Native parent blueprint creates"), Blueprint.Get()))
        return false;
    auto* Capture = Blueprint->WidgetTree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass(), TEXT("ObjectCapture"));
    Capture->SetEffectMaterial(Material);
    Capture->SetTextureParameter(TEXT("D2UObject"));
    auto* Content = Blueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ActualContent"));
    Content->SetBrushColor(FLinearColor(0.2f, 0.4f, 0.6f, 0.5f));
    Content->SetPadding(FMargin(0));
    Capture->SetContent(Content);
    Blueprint->WidgetTree->RootWidget = Capture;

    for (int32 Preparation = 0; Preparation < 2; ++Preparation)
    {
        if (!TestTrue(TEXT("Repeated object mip preparation succeeds"),
            PencilShaderRuntimeBindings::Prepare(Blueprint.Get(), Error)))
        {
            AddError(Error);
            return false;
        }
        auto* Overlay = Cast<UOverlay>(Blueprint->WidgetTree->RootWidget);
        auto* Surface = Overlay && Overlay->GetChildrenCount() == 2 ? Cast<UUserWidget>(Overlay->GetChildAt(0)) : nullptr;
        if (!TestNotNull(TEXT("Native UserWidget holds the original source"), Surface))
            return false;
        TestTrue(TEXT("Original capture and content remain unique"),
            Surface->GetContentForSlot(TEXT("Surface")) == Capture && Capture->GetContent() == Content);
        auto* Generated = Cast<UWidgetBlueprint>(Surface->GetClass()->ClassGeneratedBy);
        TestTrue(TEXT("Source precedes final image in a native overlay"),
            Overlay && Overlay->GetChildrenCount() == 2 && Overlay->GetChildAt(1)->IsA<UImage>());
        auto* Output = Overlay ? Cast<UImage>(Overlay->GetChildAt(1)) : nullptr;
        TestTrue(TEXT("Final image owns the actual shader material, capture has a separate transparent material"),
            Output && Design2UMGWidgetPropertyAccess::GetBrush(*Output).GetResourceObject() == Material &&
            Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*Capture) != Material);
        bool AutoMips = false;
        bool CopyOnPaint = false;
        bool RecreateOnPaint = false;
        bool ReleaseOnPaint = false;
        bool ResizeOnPaint = false;
        bool StoreOnPaint = false;
        TArray<UEdGraph*> Graphs;
        if (Generated)
            Generated->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
            for (UEdGraphNode* GraphNode : Graph->Nodes)
                if (auto* Call = Cast<UK2Node_CallFunction>(GraphNode))
                {
                    if (Call->FunctionReference.GetMemberName() == TEXT("CreateRenderTarget2D"))
                    {
                        if (UEdGraphPin* Pin = Call->FindPin(TEXT("bAutoGenerateMipMaps")))
                            AutoMips |= Pin->DefaultValue == TEXT("true");
                        RecreateOnPaint |= Graph->GetFName() == TEXT("OnPaint") &&
                            Call->FindPin(TEXT("Width")) && !Call->FindPin(TEXT("Width"))->LinkedTo.IsEmpty() &&
                            Call->FindPin(TEXT("Height")) && !Call->FindPin(TEXT("Height"))->LinkedTo.IsEmpty();
                    }
                    CopyOnPaint |= Graph->GetFName() == TEXT("OnPaint") &&
                        Call->FunctionReference.GetMemberName() == TEXT("DrawMaterialToRenderTarget");
                    ReleaseOnPaint |= Graph->GetFName() == TEXT("OnPaint") &&
                        Call->FunctionReference.GetMemberName() == TEXT("ReleaseRenderTarget2D");
                    ResizeOnPaint |= Graph->GetFName() == TEXT("OnPaint") &&
                        Call->FunctionReference.GetMemberName() == TEXT("ResizeRenderTarget2D");
                    StoreOnPaint |= Graph->GetFName() == TEXT("OnPaint") &&
                        Call->FunctionReference.GetMemberName() == TEXT("SetObjectPropertyByName") &&
                        Call->FindPin(TEXT("PropertyName")) &&
                        Call->FindPin(TEXT("PropertyName"))->DefaultValue == TEXT("ObjectMip");
                }
        TestTrue(TEXT("Native lifecycle creates auto-mips and copies during paint"), AutoMips && CopyOnPaint);
        TestTrue(TEXT("Physical dimensions recreate the mip target using shared native create/release APIs"),
            RecreateOnPaint && ReleaseOnPaint && StoreOnPaint && !ResizeOnPaint);
    }
    TStrongObjectPtr<UWidgetTree> Clone(DuplicateObject<UWidgetTree>(Blueprint->WidgetTree, GetTransientPackage()));
    auto* CloneOverlay = Cast<UOverlay>(Clone->RootWidget);
    auto* CloneSurface = CloneOverlay && CloneOverlay->GetChildrenCount() == 2 ? Cast<UUserWidget>(CloneOverlay->GetChildAt(0)) : nullptr;
    auto* OutputProperty = CloneSurface ? FindFProperty<FObjectPropertyBase>(CloneSurface->GetClass(), TEXT("ShaderOutput")) : nullptr;
    TestTrue(TEXT("Duplicated instances remap their real output image, not the template or another instance"),
        CloneOverlay && OutputProperty &&
        OutputProperty->GetObjectPropertyValue_InContainer(CloneSurface) == CloneOverlay->GetChildAt(1) &&
        CloneOverlay->GetChildAt(1) != Cast<UOverlay>(Blueprint->WidgetTree->RootWidget)->GetChildAt(1));
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    if (!TestTrue(TEXT("Parent compiles with the generated native mip surface"), Blueprint->Status == BS_UpToDate))
        return false;

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("Object mip runtime test has a real editor world"), World))
        return false;
    struct FRuntimeInstance
    {
        TStrongObjectPtr<UUserWidget> Root;
        TSharedPtr<SWidget> Slate;
        UUserWidget* Surface = nullptr;
        URetainerBox* Capture = nullptr;
        UImage* Output = nullptr;
        UMaterialInstanceDynamic* Display = nullptr;
    } A, B;
    const FVector2D DrawSize(20, 12);
    TStrongObjectPtr<UTextureRenderTarget2D> Rendered(FWidgetRenderer::CreateTargetFor(DrawSize, TF_Nearest, true));
    if (!TestNotNull(TEXT("Create native object mip runtime output"), Rendered.Get()))
        return false;
    ON_SCOPE_EXIT {
        for (FRuntimeInstance* Instance : {&A, &B})
            if (Instance->Root.IsValid())
            {
                Instance->Root->ReleaseSlateResources(true);
                Instance->Slate.Reset();
            }
        UKismetRenderingLibrary::ReleaseRenderTarget2D(Rendered.Get());
        FlushRenderingCommands();
    };
    const auto Observe = [&](FRuntimeInstance& Instance) {
        auto* Overlay = Cast<UOverlay>(Instance.Root->WidgetTree->RootWidget);
        if (!TestTrue(TEXT("Runtime composition keeps its source and final image"),
            Overlay && Overlay->GetChildrenCount() == 2))
            return false;
        Instance.Surface = Cast<UUserWidget>(Overlay->GetChildAt(0));
        Instance.Output = Cast<UImage>(Overlay->GetChildAt(1));
        if (!TestNotNull(TEXT("Runtime source is a native UserWidget"), Instance.Surface) ||
            !TestNotNull(TEXT("Runtime output is a native Image"), Instance.Output))
            return false;
        Instance.Capture = Cast<URetainerBox>(Instance.Surface->GetContentForSlot(TEXT("Surface")));
        auto* Property = FindFProperty<FObjectPropertyBase>(Instance.Surface->GetClass(), TEXT("ShaderOutput"));
        if (!TestNotNull(TEXT("Runtime capture is the actual Retainer instance"), Instance.Capture) ||
            !TestTrue(TEXT("Runtime source points to its own displayed image"),
                Property && Property->GetObjectPropertyValue_InContainer(Instance.Surface) == Instance.Output))
            return false;
        Instance.Display = Instance.Output->GetDynamicMaterial();
        return TestNotNull(TEXT("Runtime image has its real display MID"), Instance.Display) &&
            TestNotNull(TEXT("Runtime source has its own effect MID"), Instance.Capture->GetEffectMaterial());
    };
    for (FRuntimeInstance* Instance : {&A, &B})
    {
        Instance->Root.Reset(CreateWidget<UUserWidget>(World,
            TSubclassOf<UUserWidget>(Blueprint->GeneratedClass.Get())));
        if (!TestNotNull(TEXT("Create actual object mip runtime instance"), Instance->Root.Get()) ||
            !TestFalse(TEXT("Runtime instance is not a Designer template"), Instance->Root->IsDesignTime()))
            return false;
        Instance->Slate = Instance->Root->TakeWidget();
        if (!Observe(*Instance))
            return false;
    }
    auto* OtherContent = Cast<UBorder>(B.Capture->GetContent());
    if (!TestNotNull(TEXT("Second instance has its actual source content"), OtherContent))
        return false;
    OtherContent->SetBrushColor(FLinearColor(0.6f, 0.2f, 0.3f, 0.75f));
    FWidgetRenderer Renderer(true);
    const auto Draw = [&](FRuntimeInstance& Instance) {
        Instance.Capture->RequestRender();
        Renderer.DrawWidget(Rendered.Get(), Instance.Slate.ToSharedRef(), DrawSize, 1.f / 60.f, false);
        FlushRenderingCommands();
    };
    const auto MemberTarget = [](UUserWidget* Surface) {
        auto* Property = FindFProperty<FObjectPropertyBase>(Surface->GetClass(), TEXT("ObjectMip"));
        return Property ? Cast<UTextureRenderTarget2D>(Property->GetObjectPropertyValue_InContainer(Surface)) : nullptr;
    };
    const auto MipCount = [](UTextureRenderTarget2D* Target) {
        int32 Count = 0;
        FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
        if (Resource)
        {
            ENQUEUE_RENDER_COMMAND(ReadObjectMipCount)([Resource, &Count](FRHICommandListImmediate&) {
                const auto Texture = Resource->GetRenderTargetTexture();
                if (Texture)
                    Count = Texture->GetNumMips();
            });
            FlushRenderingCommands();
        }
        return Count;
    };
    const auto CheckFirstFrame = [&](FRuntimeInstance& Instance, const TCHAR* Label) {
        UTextureRenderTarget2D* Raw = Cast<UTextureRenderTarget2D>(
            Instance.Display->K2_GetTextureParameterValue(TEXT("D2UObject")));
        UTextureRenderTarget2D* Mip = Cast<UTextureRenderTarget2D>(
            Instance.Display->K2_GetTextureParameterValue(TEXT("D2UObjectMip")));
        if (!TestNotNull(TEXT("First paint binds a real raw capture"), Raw) ||
            !TestNotNull(TEXT("First paint binds a real mip target"), Mip))
            return false;
        if (!TestTrue(TEXT("Display reads its actual capture, not a default or feedback target"),
            Raw == Instance.Capture->GetEffectMaterial()->K2_GetTextureParameterValue(TEXT("D2UObject")) &&
            Raw != Mip && Raw != RawDefault && Mip != MipDefault) ||
            !TestTrue(TEXT("Display mip target belongs to its actual source instance"),
                Mip == MemberTarget(Instance.Surface) && Mip->IsIn(Instance.Root.Get())) ||
            !TestTrue(TEXT("Raw and mip resources use the actual first-frame physical size"),
                Raw->SizeX == 20 && Raw->SizeY == 12 && Mip->SizeX == 20 && Mip->SizeY == 12) ||
            !TestEqual(TEXT("The native GPU mip resource has all five levels for 20x12"), MipCount(Mip), 5))
            return false;
        const FLinearColor RawValue = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Raw, 10, 6, false);
        const FLinearColor MipValue = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Mip, 10, 6, false);
        const FLinearColor DisplayValue = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Rendered.Get(), 10, 6, false);
        AddInfo(FString::Printf(TEXT("Object mip %s size=20x12 levels=5 raw=%s mip=%s rawRGBA=%s mipRGBA=%s displayRGBA=%s"),
            Label, *Raw->GetPathName(), *Mip->GetPathName(),
            *RawValue.ToString(), *MipValue.ToString(), *DisplayValue.ToString()));
        return true;
    };
    TStrongObjectPtr<UTextureRenderTarget2D> InitialMip(MemberTarget(A.Surface));
    if (!TestNotNull(TEXT("PreConstruct creates the initial instance target"), InitialMip.Get()) ||
        !TestTrue(TEXT("Initial target is 1x1 before the actual capture size is known"),
            InitialMip->SizeX == 1 && InitialMip->SizeY == 1))
        return false;
    Draw(A);
    if (!CheckFirstFrame(A, TEXT("A first paint")))
        return false;
    if (!TestTrue(TEXT("First paint replaces and releases the 1x1 target before binding the actual capture size"),
        MemberTarget(A.Surface) != InitialMip.Get() && InitialMip->GetResource() == nullptr))
        return false;
    Draw(B);
    if (!CheckFirstFrame(B, TEXT("B first paint")))
        return false;
    if (!TestTrue(TEXT("Actual instances keep separate MIDs, raw captures and mip targets"),
        A.Display != B.Display &&
        A.Display->K2_GetTextureParameterValue(TEXT("D2UObject")) != B.Display->K2_GetTextureParameterValue(TEXT("D2UObject")) &&
        MemberTarget(A.Surface) != MemberTarget(B.Surface)))
        return false;

    TStrongObjectPtr<UTextureRenderTarget2D> PreviousMip(MemberTarget(A.Surface));
    UTextureRenderTarget2D* OtherMip = MemberTarget(B.Surface);
    A.Root->ReleaseSlateResources(true);
    A.Slate.Reset();
    FlushRenderingCommands();
    if (!TestNull(TEXT("Native Destruct clears the released instance target member"), MemberTarget(A.Surface)) ||
        !TestNull(TEXT("Native Destruct releases the previous render target resource"), PreviousMip->GetResource()) ||
        !TestTrue(TEXT("Releasing A leaves B's actual mip resource intact"),
            MemberTarget(B.Surface) == OtherMip && MipCount(OtherMip) == 5))
        return false;
    A.Slate = A.Root->TakeWidget();
    if (!Observe(A))
        return false;
    Draw(A);
    if (!CheckFirstFrame(A, TEXT("A first paint after rebuild")))
        return false;
    return TestTrue(TEXT("Rebuild allocates a new instance target without changing the other instance"),
        MemberTarget(A.Surface) != PreviousMip.Get() && MemberTarget(B.Surface) == OtherMip);
}
#endif
