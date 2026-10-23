// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "PenParser.h"
#include "DesignSchema.h"
#include "Design2UMGConverter.h"
#include "DesignImportPlanning.h"
#include "DesignPreviewContracts.h"
#include "PencilShaderCompiler.h"
#include "PencilShaderMaterials.h"
#include "Design2UMGWidgetMetadata.h"
#include "PencilShaderRuntimeBindings.h"
#include "Misc/EngineVersionComparison.h"
#include "Design2UMGGenerationManifest.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "TextureCompiler.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlot.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "PreBuildResourceNormalizer.h"
#include "UMGWidgetTreeBuilder.h"
#include "UMGWidgetRenderer.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "Components/RetainerBox.h"
#include "Components/Border.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#if UE_VERSION_OLDER_THAN(5, 2, 0)
#include "MaterialShared.h"
#else
#include "MaterialDomain.h"
#endif
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Design2UMGMaterialAccess.h"
#include "WidgetBlueprint.h"
#include "ObjectTools.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "UObject/UObjectHash.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilShaderDefaultPrecisionTest,
    "Design2UMG.Resources.PencilShaderDefaultPrecision",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FPencilShaderDefaultPrecisionTest::RunTest(const FString&)
{
    const FString Source = TEXT("#version 100\n#extension GL_OES_standard_derivatives : enable\n"
        "// precision mediump float; is only a comment.\n"
        "uniform vec2 Extent;\nvoid main(){gl_FragColor=vec4(gl_FragCoord.xy/Extent,0.0,1.0);}\n");
    PencilShaderSyntax::FProgram Program;
    FString Error;
    if (!TestTrue(TEXT("Implicit float precision compiles through production ANGLE"),
        PencilShaderCompiler::Compile(Source, Program, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString Prepared(UTF8_TO_TCHAR(Program.Source.c_str()));
    TestTrue(TEXT("Version and extension remain before the default precision"), Prepared.StartsWith(
        TEXT("#version 100\n#extension GL_OES_standard_derivatives : enable\n")));
    TestTrue(TEXT("Default precision preserves the original declaration line"),
        Prepared.Contains(TEXT("precision highp float;\n#line 4\nuniform vec2 Extent;")));

    const std::string Explicit = "#version 100\nprecision lowp float;\nvoid main(){gl_FragColor=vec4(1.0);}";
    std::string Message;
    TestTrue(TEXT("Explicit precision source prepares"), PencilShaderSyntax::PrepareSource(Explicit, Program, Message));
    TestTrue(TEXT("Explicit precision is preserved without a second declaration"), Program.Source == Explicit);

    TestFalse(TEXT("An invalid implicit-precision shader still fails compilation"), PencilShaderCompiler::Compile(
        TEXT("#version 100\nvoid main(){\n gl_FragColor=unknownValue;\n}\n"), Program, Error));
    TestTrue(TEXT("Compile diagnostics retain authored line numbers"), Error.Contains(TEXT(":3:")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilShaderSamplerImagesTest,
    "Design2UMG.Resources.PencilShaderSamplerImages",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FPencilShaderSamplerImagesTest::RunTest(const FString&)
{
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Design2UMG/ShaderImages")/Id);
    const FString ShaderDirectory = Directory/TEXT("shaders");
    const FString ShaderFile = ShaderDirectory/TEXT("image.glsl");
    const FString ImageFile = ShaderDirectory/TEXT("image.png");
    const FString DocumentFile = Directory/TEXT("source.pen");
    const FString Folder = TEXT("/Game/Design2UMG/Automation/ShaderImages_") + Id;
    const FString MaterialDirectory = TEXT("/Game/Design2UMG/GeneratedMaterials");
    const FString TextureDirectory = TEXT("/Game/Design2UMG/GeneratedTextures");
    auto& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    AssetRegistry.ScanPathsSynchronous({MaterialDirectory, TextureDirectory});
    TSet<FString> ExistingAssets;
    for (const auto& AssetDirectory : {MaterialDirectory, TextureDirectory})
    {
        TArray<FAssetData> Assets;
        AssetRegistry.GetAssetsByPath(FName(*AssetDirectory), Assets, true);
        for (const auto& Asset : Assets)
            ExistingAssets.Add(Asset.PackageName.ToString() + TEXT(".") + Asset.AssetName.ToString());
    }
    TArray<UObject*> Materials, Textures;
    ON_SCOPE_EXIT {
        if (Materials.Num()) ObjectTools::ForceDeleteObjects(Materials, false);
        if (Textures.Num()) ObjectTools::ForceDeleteObjects(Textures, false);
        Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder);
        IFileManager::Get().Delete(*ImageFile);
        IFileManager::Get().Delete(*ShaderFile);
        IFileManager::Get().Delete(*DocumentFile);
        IFileManager::Get().DeleteDirectory(*ShaderDirectory, false, false);
        IFileManager::Get().DeleteDirectory(*Directory, false, false);
    };
    IFileManager::Get().MakeDirectory(*ShaderDirectory, true);
    TArray<FColor> Pixels; Pixels.Init(FColor::White, 4);
    TArray64<uint8> Png;
    FImageUtils::PNGCompressImageArray(2, 2, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
    if (!TestTrue(TEXT("Write local sampler image"), FFileHelper::SaveArrayToFile(Png, *ImageFile)) ||
        !TestTrue(TEXT("Write defaults with explicit override"), FFileHelper::SaveStringToFile(TEXT(
            "/** @default image.png @mipmap true */ uniform sampler2D DefaultImage;\n"
            "/** @default absent.png */ uniform sampler2D OverrideImage;\n"
            "void main(){gl_FragColor=vec4(vec2(textureSize(DefaultImage,1))/vec2(textureSize(DefaultImage)),1.0,1.0)*texture2D(OverrideImage,vec2(0.5));}"),
            *ShaderFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) ||
        !TestTrue(TEXT("Write document outside shader directory"), FFileHelper::SaveStringToFile(TEXT(R"JSON(
            {"version":"2.21","children":[{"id":"image","type":"rectangle","width":8,"height":8,
            "fill":{"type":"shader","url":"shaders/image.glsl","uniforms":{
            "OverrideImage":"/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"}}}]}
            )JSON"), *DocumentFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
    FDesignDocument Document;
    if (!TestTrue(TEXT("Parse real shader image document"), FPenParser::ParseDocumentFile(DocumentFile, Document))) return false;
    FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings());
    FDesignImageImportSettingsSnapshot Settings;
    Settings.bEnableRemoteImageImport = false;
    Settings.RemoteImageImportPath = Folder;
    const auto Images = DesignImportPlanning::ResolveImages(Data, nullptr, &Settings);
    TestFalse(TEXT("Default local image imports and absent overridden default is never imported"), Images.HasFailures());
    const FDesignShaderData* Shader = nullptr;
    TFunction<void(const FUMGWidgetNode&)> Find = [&](const FUMGWidgetNode& Node) {
        if (Node.Brush.Shader.IsSet()) Shader = &Node.Brush.Shader.GetValue();
        for (const auto& Child : Node.Children) Find(Child);
    };
    Find(Data.RootWidget);
    if (!TestNotNull(TEXT("Real conversion retains shader payload"), Shader)) return false;
    const auto* Imported = Shader->Uniforms.Find(TEXT("DefaultImage"));
    if (!TestTrue(TEXT("Default image resolves relative to shader and becomes an imported asset"),
        Imported && Imported->String.StartsWith(Folder))) return false;
    const FString ImportedPath = Imported->String;
    FString Error;
    if (!TestTrue(TEXT("Imported effective values build actual ANGLE/native materials"),
        FPreBuildResourceNormalizer::NormalizeGeneratedTextures(Data.RootWidget, Folder, TEXT("Images"), Error)))
    { AddError(Error); return false; }
    TFunction<void(const FUMGWidgetNode&)> Check = [&](const FUMGWidgetNode& Node) {
        if (Node.Brush.Shader.IsSet())
        {
            auto* Material = LoadObject<UMaterial>(nullptr, *Node.Brush.MaterialRef.Get(FString()));
            if (TestNotNull(TEXT("Default image material exists"), Material))
            {
                if (Material->GetPathName().StartsWith(MaterialDirectory + TEXT("/")) &&
                    !ExistingAssets.Contains(Material->GetPathName()))
                    Materials.AddUnique(Material);
                UTexture* Bound = nullptr;
                TestTrue(TEXT("Default sampler reaches native material binding"),
                    Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Input0")), Bound) && Bound);
                for (const FName Parameter : {FName(TEXT("Input0")), FName(TEXT("Input1"))})
                {
                    Bound = nullptr;
                    if (Material->GetTextureParameterValue(FMaterialParameterInfo(Parameter), Bound) && Bound &&
                        Bound->GetPathName().StartsWith(TextureDirectory + TEXT("/")) &&
                        !ExistingAssets.Contains(Bound->GetPathName()))
                        Textures.AddUnique(Bound);
                }
            }
        }
        for (const auto& Child : Node.Children) Check(Child);
    };
    Check(Data.RootWidget);
    TestTrue(TEXT("Planning records the actual shader-relative source"), Images.Diagnostics.ContainsByPredicate(
        [&](const FDesignImageDiagnostic& Diagnostic) { return Diagnostic.Resource == ImageFile && Diagnostic.ResolvedAssetPath == ImportedPath; }));
    const FString RemoteUrl = TEXT("https://example.com/shader-image.png");
    FString RemoteSource;
    if (!FFileHelper::LoadFileToString(RemoteSource, *ShaderFile)) return false;
    RemoteSource.ReplaceInline(TEXT("@default image.png"), *(TEXT("@default ") + RemoteUrl));
    if (!FFileHelper::SaveStringToFile(RemoteSource, *ShaderFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
    TFunction<void(FUMGWidgetNode&)> ResetDefault = [&](FUMGWidgetNode& Node) {
        if (Node.Brush.Shader.IsSet()) Node.Brush.Shader->Uniforms.Remove(TEXT("DefaultImage"));
        for (auto& Child : Node.Children) ResetDefault(Child);
    };
    ResetDefault(Data.RootWidget);
    const auto Remote = DesignImportPlanning::ResolveImages(Data, nullptr, &Settings);
    TestTrue(TEXT("Remote default reaches existing disabled-download diagnostic without network access"),
        Remote.Diagnostics.ContainsByPredicate([&](const FDesignImageDiagnostic& Diagnostic) {
            return Diagnostic.Resource == RemoteUrl && Diagnostic.Category == EDesignImageDiagnosticCategory::Disabled;
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilShaderPipelineTest,
    "Design2UMG.Resources.PencilShaderPipeline",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FPencilShaderPipelineTest::RunTest(const FString&)
{
    {
        static const TCHAR* const Modes[] = {
            TEXT("normal"), TEXT("darken"), TEXT("multiply"), TEXT("linearBurn"), TEXT("colorBurn"), TEXT("light"),
            TEXT("screen"), TEXT("linearDodge"), TEXT("colorDodge"), TEXT("overlay"), TEXT("softLight"), TEXT("hardLight"),
            TEXT("difference"), TEXT("exclusion"), TEXT("hue"), TEXT("saturation"), TEXT("color"), TEXT("luminosity")
        };
        FString Error;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Modes); ++Index)
        {
            int32 Mode = INDEX_NONE;
            TestTrue(TEXT("Canonical Pencil blend mode resolves"), PencilShaderMaterials::BlendModeIndex(Modes[Index], Mode, Error));
            TestEqual(TEXT("Blend mode retains source enum order"), Mode, Index);
        }
        int32 Mode = INDEX_NONE;
        TestFalse(TEXT("Unknown blend spelling is rejected"), PencilShaderMaterials::BlendModeIndex(TEXT("lighten"), Mode, Error));
        FUMGWidgetData Data, Parsed;
        Data.RootWidget.Type = TEXT("Image");
        Data.RootWidget.Bounds.W = 32; Data.RootWidget.Bounds.H = 32;
        Data.RootWidget.Render.PaintBlendMode = TEXT("overlay");
        Data.RootWidget.Effect.InnerShadowBlendMode = TEXT("multiply");
        TestTrue(TEXT("Inset blend is part of typed effect payload"), Data.RootWidget.Effect.HasAny());
        Data.RootWidget.Render.BackgroundCaptureInsets = FMargin(1.25f,2.5f,3.75f,4.125f);
        FUMGBoxShadowLayerData Layer; Layer.BlendMode = TEXT("screen");
        Data.RootWidget.Shadow.BoxShadows.Add(Layer);
        TestTrue(TEXT("Paint blend is part of typed render payload"), Data.RootWidget.Render.HasAny());
        if (!TestTrue(TEXT("Paint and shadow blend serialize"), FUMGJsonParser::ParseJson(
            FUMGJsonSerializer::Serialize(Data), Parsed, &Error)))
        {
            AddError(Error);
            return false;
        }
        TestEqual(TEXT("Paint blend survives JSON"), Parsed.RootWidget.Render.PaintBlendMode.Get(FString()), FString(TEXT("overlay")));
        TestEqual(TEXT("Inset shadow blend survives JSON"), Parsed.RootWidget.Effect.InnerShadowBlendMode.Get(FString()), FString(TEXT("multiply")));
        Data.RootWidget.Effect.InnerShadowBlendMode.Reset();
        TestFalse(TEXT("Normal inset shadow adds no effect payload"), Data.RootWidget.Effect.HasAny());
        const FString NormalJson = FUMGJsonSerializer::Serialize(Data);
        TestFalse(TEXT("Unset inset blend is omitted from JSON"), NormalJson.Contains(TEXT("InnerShadowBlendMode")));
        FUMGWidgetData NormalParsed;
        if (!TestTrue(TEXT("Normal inset payload parses"), FUMGJsonParser::ParseJson(NormalJson, NormalParsed, &Error)))
        {
            AddError(Error);
            return false;
        }
        TestFalse(TEXT("Normal inset blend remains unset"), NormalParsed.RootWidget.Effect.InnerShadowBlendMode.IsSet());
        TestTrue(TEXT("Capture insets retain logical pixel values"), Parsed.RootWidget.Render.BackgroundCaptureInsets.Get(FMargin()) == FMargin(1.25f,2.5f,3.75f,4.125f));
        UObject* MetadataOwner = NewObject<UImage>();
        Design2UMGWidgetMetadata::SetBackgroundCaptureInsets(MetadataOwner, FMargin(1.25f,2.5f,3.75f,4.125f));
        TOptional<FMargin> Insets;
        TestTrue(TEXT("Generation metadata conveys capture insets"), Design2UMGWidgetMetadata::ConsumeBackgroundCaptureInsets(MetadataOwner, Insets, Error) && Insets.IsSet() && Insets.GetValue() == FMargin(1.25f,2.5f,3.75f,4.125f));
        TestTrue(TEXT("Generation metadata is consumed once"), Design2UMGWidgetMetadata::ConsumeBackgroundCaptureInsets(MetadataOwner, Insets, Error) && !Insets.IsSet());
        Design2UMGWidgetMetadata::PackageMetadata(MetadataOwner).SetValue(MetadataOwner, Design2UMGWidgetMetadata::BackgroundCaptureInsetsKey, TEXT("broken"));
        TestFalse(TEXT("Malformed generation metadata stops preparation"), Design2UMGWidgetMetadata::ConsumeBackgroundCaptureInsets(MetadataOwner, Insets, Error));
        if (!TestEqual(TEXT("Shadow layer survives JSON"), Parsed.RootWidget.Shadow.BoxShadows.Num(), 1)) return false;
        TestEqual(TEXT("Independent shadow blend survives JSON"), Parsed.RootWidget.Shadow.BoxShadows[0].BlendMode, FString(TEXT("screen")));
        PencilShaderSyntax::FProgram Program;
        if (!TestTrue(TEXT("Background raw and mip compile"), PencilShaderCompiler::Compile(
            TEXT("uniform sampler2D Raw; /** @mipmap */ uniform sampler2D Mip; void main(){gl_FragColor=texture2D(Raw,vec2(0.3))+texture2D(Mip,vec2(0.2));}"), Program, Error))) return false;
        const FString Code(UTF8_TO_TCHAR(Program.Code.c_str()));
        TestTrue(TEXT("Background sampler role stays independent of object role"), Code.Contains(TEXT("Input0IsBackground")) && Code.Contains(TEXT("Input1IsBackground")));
        TestTrue(TEXT("Background UV and full derivatives are explicit inputs"), Code.Contains(TEXT("BackgroundU")) && Code.Contains(TEXT("BackgroundV")) && Code.Contains(TEXT("D2UTextureDerivative")));
    }

    const FString Id=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Directory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Design2UMG/ShaderTests"),Id);
    const FString Folder=TEXT("/Game/Design2UMG/Automation/Shader_")+Id;
    const FString ShaderFile=Directory/TEXT("effect.glsl"),DocumentFile=Directory/TEXT("source.pen"),SdfFile=Directory/TEXT("sdf.glsl");
    TArray<UObject*> Materials,Textures;
    ON_SCOPE_EXIT
    {
        if(Materials.Num())ObjectTools::ForceDeleteObjects(Materials,false);
        if(Textures.Num())ObjectTools::ForceDeleteObjects(Textures,false);
        Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder);
        IFileManager::Get().Delete(*ShaderFile);IFileManager::Get().Delete(*DocumentFile);IFileManager::Get().Delete(*SdfFile);
        IFileManager::Get().DeleteDirectory(*Directory,false,false);
    };
    IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Source=TEXT(R"GLSL(#version 100
precision mediump float;
/** @resolution */uniform vec2 Extent;
/** @time */uniform float Clock;
/** @mouse */uniform vec2 Cursor;
uniform sampler2D Albedo;
/** @mipmap true */uniform sampler2D Detail;
uniform vec4 Tint;
uniform float Gain;
/**
 * @default 2
 * @label Repeat count
 */
uniform int Repeat;
void main(){vec2 s=vec2(textureSize(Albedo,0));vec2 uv=gl_FragCoord.xy/Extent;gl_FragColor=texture2D(Albedo,uv)*texture2D(Detail,uv)*Tint*(Gain+fract(Clock*0.01)/s.x)*float(Repeat)*(0.5+0.5*step(0.0,Cursor.x));}
)GLSL");
    if(!TestTrue(TEXT("Own shader fixture writes"),FFileHelper::SaveStringToFile(Source,*ShaderFile,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))return false;
    FString Json=TEXT(R"JSON({"version":"2.21","variables":{"gain":{"type":"number","value":0.5}},"children":[
    {"id":"shaderHost","name":"ShaderHost","type":"frame","width":17,"height":13,"cornerRadius":4,
    "fill":{"type":"shader","url":"effect.glsl","opacity":0.5,"uniforms":{"Albedo":"/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture","Detail":"DETAIL_TEXTURE","Tint":"#ffffffff","Gain":"$gain"}},
    "effect":{"type":"shader","url":"effect.glsl","uniforms":{"Albedo":"pencil:object","Detail":"DETAIL_TEXTURE","Tint":[1,1,1,1],"Gain":1}},
    "children":[{"id":"overflow","type":"rectangle","x":-4,"y":-3,"width":5,"height":5,"fill":"#ff0000"}]}]})JSON");
    UTexture2D* SourceTexture=NewObject<UTexture2D>(CreatePackage(*(Folder/TEXT("Source"))),TEXT("Source"),RF_Public|RF_Standalone);
    TArray<FColor> Pixels;Pixels.Init(FColor(64,128,192,128),64);
    SourceTexture->Source.Init(8,8,1,1,TSF_BGRA8,reinterpret_cast<const uint8*>(Pixels.GetData()));
    SourceTexture->MipGenSettings=TMGS_NoMipmaps;SourceTexture->Filter=TF_Bilinear;SourceTexture->AddressX=TA_Clamp;SourceTexture->AddressY=TA_Clamp;
    SourceTexture->PostEditChange();FTextureCompilingManager::Get().FinishCompilation({SourceTexture});
    Textures.Add(SourceTexture);Json.ReplaceInline(TEXT("DETAIL_TEXTURE"),*SourceTexture->GetPathName());
    Json.ReplaceInline(TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"),*SourceTexture->GetPathName());
    if(!TestTrue(TEXT("Own document fixture writes"),FFileHelper::SaveStringToFile(Json,*DocumentFile,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))return false;
    FDesignDocument Doc;
    if(!TestTrue(TEXT("Shader source uses the real document-relative parser"),FPenParser::ParseDocumentFile(DocumentFile,Doc)))return false;
    if(!TestTrue(TEXT("Shader fill is explicit source data"),Doc.Children.Num()==1&&Doc.Children[0]->Visual.TopmostFill().Shader.IsSet()))return false;
    TestEqual(TEXT("Shader number variable resolves"),Doc.Children[0]->Visual.TopmostFill().Shader->Uniforms[TEXT("Gain")].String,FString(TEXT("0.5")));
    PencilShaderSyntax::FProgram Program;FString Error;
    FUMGWidgetNode MaskNode;
    MaskNode.Type=TEXT("Border");MaskNode.Brush.Shader=FDesignShaderData();
    MaskNode.Bounds.W=40;MaskNode.Bounds.H=20;MaskNode.Brush.OutlineCornerRadius=2;
    if(!TestTrue(TEXT("Shader mask accepts pixel corner radius"),PencilShaderMaterials::PrepareGeometryMasks(MaskNode,Error)))return false;
    TestEqual(TEXT("Shader mask converts pixel radius to half-min fraction"),MaskNode.Brush.Shape->CornerRadius.GetValue(),0.2f);
    MaskNode.Brush.MaterialRef.Reset();MaskNode.Brush.CornerRadii=FVector4(1,2,3,4);
    if(!TestTrue(TEXT("Shader mask accepts individual pixel radii"),PencilShaderMaterials::PrepareGeometryMasks(MaskNode,Error)))return false;
    TestTrue(TEXT("Shader mask converts all individual radii"),MaskNode.Brush.Shape->CornerRadii->Equals(FVector4(0.1,0.2,0.3,0.4)));
    MaskNode.Brush.MaterialRef.Reset();MaskNode.Bounds.W=10;
    MaskNode.Brush.CornerRadii=FVector4(999,2,3,4);
    if(!TestTrue(TEXT("Shader mask accepts the native capsule radius contract"),PencilShaderMaterials::PrepareGeometryMasks(MaskNode,Error)))return false;
    TestTrue(TEXT("Individual capsule sentinel overrides scalar and uses native half-height radius"),!MaskNode.Brush.Shape->CornerRadii.IsSet()&&MaskNode.Brush.Shape->CornerRadius.GetValue()==2.0f);
    if(!TestTrue(TEXT("Actual ANGLE translation builds self-contained code"),PencilShaderCompiler::Compile(Source,Program,Error)))
    {AddError(Error);return false;}
    TestFalse(TEXT("No backend holes survive"),FString(UTF8_TO_TCHAR(Program.Code.c_str())).Contains(TEXT("@@")));
    const FString Wrapped(UTF8_TO_TCHAR(Program.Code.c_str()));
    TestFalse(TEXT("ANGLE globals become legal instance state inside the local Custom struct"),Wrapped.Contains(TEXT("static const ")));
    TestTrue(TEXT("Fragment and sampler coordinates both use the source upward Y axis"),Wrapped.Contains(TEXT("Resolution.y-NodePosition.y"))&&Wrapped.Contains(TEXT("float2(uv.x,1-uv.y)")));
    TestTrue(TEXT("Only object samplers apply the transparent outside range contract"),Wrapped.Contains(TEXT("D2UObjectFlags"))&&Wrapped.Contains(TEXT("D2UCoverage")));
    bool DefaultRead=false,MipmapRead=false;int32 DetailIndex=INDEX_NONE,AlbedoIndex=INDEX_NONE,TintIndex=INDEX_NONE;
    for(size_t I=0;I<Program.Uniforms.size();++I)if(Program.Uniforms[I].Name=="Tint")TintIndex=int32(I);
    for(size_t I=0;I<Program.Uniforms.size();++I)if(Program.Uniforms[I].Name=="Albedo")AlbedoIndex=int32(I);
    for(size_t I=0;I<Program.Uniforms.size();++I)if(Program.Uniforms[I].Name=="Detail"){MipmapRead=Program.Uniforms[I].Mipmap;DetailIndex=int32(I);}
    TestTrue(TEXT("Mipmap annotation is explicit production data"),MipmapRead);
    for(const auto& U:Program.Uniforms)if(U.Name=="Repeat")DefaultRead=U.DefaultValue=="2";
    TestTrue(TEXT("Default annotations are parsed from multiline block comments"),DefaultRead);
    FUMGWidgetData Data=FDesign2UMGConverter::Convert(Doc,FDesign2UMGConversionSettings());
    FUMGWidgetData Parsed;
    if(!TestTrue(TEXT("Shader source and bindings survive UMG JSON"),FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(Data),Parsed,&Error)))return false;
    const FDesignShaderData* EffectShader=nullptr;
    int32 UnownedFillShaders = 0;
    TFunction<void(const FUMGWidgetNode&)> FindEffect=[&](const auto& N)
    {
        if (N.Brush.Shader.IsSet() && !N.Brush.Shader->bFillPaint) ++UnownedFillShaders;
        if (N.Effect.Shader.IsSet()) EffectShader = &N.Effect.Shader.GetValue();
        for (const auto& Child : N.Children) FindEffect(Child);
    };
    FindEffect(Parsed.RootWidget);
    TestEqual(TEXT("Transferred shader fill leaves no shader on original content"), UnownedFillShaders, 0);
    if(!TestNotNull(TEXT("Object effect becomes a native retainer contract"),EffectShader))return false;
    TestEqual(TEXT("Capture insets survive the actual UMG JSON path"),EffectShader->CaptureInsets.X,4.0);
    if(!TestTrue(TEXT("Shader materials and static masks persist through existing normalization"),
        FPreBuildResourceNormalizer::NormalizeGeneratedTextures(Parsed.RootWidget,Folder,TEXT("Shader"),Error)))
    {AddError(Error);return false;}
    UTexture2D* FillGeometryMask=nullptr;UMaterial* FillMaterial=nullptr;
    TFunction<void(const FUMGWidgetNode&)> Collect=[&](const FUMGWidgetNode& N)
    {
        if(N.Brush.Shader.IsSet())if(auto M=LoadObject<UMaterialInterface>(nullptr,*N.Brush.MaterialRef.Get(FString())))
        {
            FillMaterial=Cast<UMaterial>(M);Materials.AddUnique(M);UTexture* Bound=nullptr;
            if(M->GetTextureParameterValue(FMaterialParameterInfo(TEXT("GeometryMask")),Bound))FillGeometryMask=Cast<UTexture2D>(Bound);
        }
        if(N.Effect.Shader.IsSet())if(auto M=LoadObject<UMaterialInterface>(nullptr,*N.Effect.EffectMaterial.Get(FString())))Materials.AddUnique(M);
        for(const auto& Child:N.Children)Collect(Child);
    };Collect(Parsed.RootWidget);
    TestEqual(TEXT("Fill and object effect use material assets, not baked shader textures"),Materials.Num(),2);
    for(UObject* Object:Materials)
    {
        auto* Material=CastChecked<UMaterial>(Object);
        const auto* ColorOutput=Cast<UMaterialExpressionCustom>(Material->GetExpressionInputForProperty(MP_EmissiveColor)->Expression);
        TestEqual(TEXT("Only the ordinary fill inverts Slate's final display transfer"),ColorOutput!=nullptr,Material==FillMaterial);
        if(ColorOutput)
        {
            TestTrue(TEXT("Fill output uses piecewise sRGB transfer, not a single gamma exponent"),ColorOutput->Code.Contains(TEXT("0.04045"))&&ColorOutput->Code.Contains(TEXT("12.92")));
            const FCustomInput* Encoded=ColorOutput->Inputs.FindByPredicate([](const FCustomInput& Input){return Input.InputName==TEXT("EncodedRGB");});
            const FCustomInput* Vertex=ColorOutput->Inputs.FindByPredicate([](const FCustomInput& Input){return Input.InputName==TEXT("VertexOpacity");});
            const auto* Rgb=Encoded?Cast<UMaterialExpressionComponentMask>(Encoded->Input.Expression):nullptr;
            const auto* Alpha=Cast<UMaterialExpressionComponentMask>(Material->GetExpressionInputForProperty(MP_Opacity)->Expression);
            TestTrue(TEXT("Transfer reads the final covered premultiplied output and leaves alpha unchanged"),Rgb&&Alpha&&Rgb->Input.Expression==Alpha->Input.Expression&&Rgb->R&&Rgb->G&&Rgb->B&&!Rgb->A&&Alpha->A);
            TestTrue(TEXT("Inherited native opacity scales premultiplied RGB before transfer"),Vertex&&Cast<UMaterialExpressionVertexColor>(Vertex->Input.Expression)&&Vertex->Input.OutputIndex==4&&ColorOutput->Code.Contains(TEXT("EncodedRGB*VertexOpacity")));
        }
    }
    const FName TintInputName(*FString::Printf(TEXT("Input%d"), TintIndex));
    for(UObject* Object:Materials)
    {
        int32 TintConnections = 0, DrawRectConnections = 0;
        for(const UMaterialExpression* E:Design2UMGMaterialAccess::GetExpressions(*CastChecked<UMaterial>(Object)))
            if(const auto* Custom=Cast<UMaterialExpressionCustom>(E))
            {
                const bool IsShader = Custom->Inputs.ContainsByPredicate(
                    [&](const FCustomInput& Input) { return Input.InputName == TintInputName; });
                for(const auto& Input:Custom->Inputs)
                    if(const auto* Parameter=Cast<UMaterialExpressionVectorParameter>(Input.Input.Expression))
                    {
                        TestEqual(TEXT("Full vector Custom inputs use the actual RGBA output pin"),Input.Input.OutputIndex,5);
                        if (IsShader)
                        {
                            TintConnections += Input.InputName == TintInputName && Parameter->ParameterName == TintInputName;
                            DrawRectConnections += Input.InputName == TEXT("DrawRect") && Parameter->ParameterName == TEXT("D2UDrawRect");
                        }
                    }
            }
        TestEqual(TEXT("Each material shader preserves Tint alpha"), TintConnections, 1);
        TestEqual(TEXT("Each material shader preserves DrawRect height"), DrawRectConnections, 1);
    }
    int32 PlainImageBindings=0;
    for(UObject* Object:Materials)
    {
        UTexture* Bound=nullptr;
        const bool Found=CastChecked<UMaterialInterface>(Object)->GetTextureParameterValue(FMaterialParameterInfo(*FString::Printf(TEXT("Input%d"),DetailIndex)),Bound);
        auto Mipped=Cast<UTexture2D>(Bound);
        TestTrue(TEXT("Annotated image binds a separate real trilinear mip chain"),Found&&Mipped&&Mipped!=SourceTexture&&Mipped->GetNumMips()>1&&Mipped->Filter==TF_Trilinear&&Mipped->AddressX==TA_Wrap&&Mipped->AddressY==TA_Wrap&&!Mipped->SRGB);
        if(Mipped)Textures.AddUnique(Mipped);
        if(CastChecked<UMaterialInterface>(Object)->GetTextureParameterValue(FMaterialParameterInfo(*FString::Printf(TEXT("Input%d"),AlbedoIndex)),Bound))
        {
            ++PlainImageBindings;auto Plain=Cast<UTexture2D>(Bound);
            TestTrue(TEXT("Unmipmapped shader image also repeats without changing the shared UI source"),Plain&&Plain!=SourceTexture&&Plain->MipGenSettings==TMGS_NoMipmaps&&Plain->Filter==TF_Bilinear&&Plain->AddressX==TA_Wrap&&Plain->AddressY==TA_Wrap&&!Plain->SRGB);
            FImage SamplerPixels;
            if(Plain&&TestTrue(TEXT("Shader image retains readable source pixels"),FImageUtils::GetTexture2DSourceImage(Plain,SamplerPixels)))
                TestEqual(TEXT("Shader image premultiplies encoded source channels before filtering"),SamplerPixels.AsBGRA8()[0],FColor(32,64,96,128));
            if(Plain)Textures.AddUnique(Plain);
        }
    }
    TestEqual(TEXT("The fill ordinary image is bound separately from the object sampler"),PlainImageBindings,1);
    TestTrue(TEXT("Shared UI source remains sRGB, unmipmapped and bilinear"),SourceTexture->SRGB&&SourceTexture->MipGenSettings==TMGS_NoMipmaps&&SourceTexture->Filter==TF_Bilinear&&SourceTexture->AddressX==TA_Clamp&&SourceTexture->AddressY==TA_Clamp);
    Pixels.Init(FColor::Red,64);SourceTexture->Source.Init(8,8,1,1,TSF_BGRA8,reinterpret_cast<const uint8*>(Pixels.GetData()));
    SourceTexture->PostEditChange();FTextureCompilingManager::Get().FinishCompilation({SourceTexture});
    if(!TestTrue(TEXT("Changed source pixels rebuild through the same production normalizer"),FPreBuildResourceNormalizer::NormalizeGeneratedTextures(Parsed.RootWidget,Folder,TEXT("Shader"),Error)))return false;
    Collect(Parsed.RootWidget);
    TestEqual(TEXT("Source identity invalidates cached materials pointing at old shader image assets"),Materials.Num(),4);
    for(UObject* Object:Materials)
    {
        UTexture* Bound=nullptr;
        CastChecked<UMaterialInterface>(Object)->GetTextureParameterValue(FMaterialParameterInfo(*FString::Printf(TEXT("Input%d"),DetailIndex)),Bound);
        if(auto Mipped=Cast<UTexture2D>(Bound))Textures.AddUnique(Mipped);
        if(CastChecked<UMaterialInterface>(Object)->GetTextureParameterValue(FMaterialParameterInfo(*FString::Printf(TEXT("Input%d"),AlbedoIndex)),Bound))
            if(auto Plain=Cast<UTexture2D>(Bound))Textures.AddUnique(Plain);
    }


    TStrongObjectPtr<UWidgetBlueprint> Blueprint(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(),GetTransientPackage(),FName(*(FString(TEXT("ShaderBindings_"))+Id)),BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass())));
    if(!TestNotNull(TEXT("Native shader blueprint creates"),Blueprint.Get()))return false;
    UWidget* Widget=FUMGWidgetTreeBuilder::CreateWidgetRecursive(Blueprint->WidgetTree,Parsed.RootWidget);
    Blueprint->WidgetTree->RootWidget=Widget;
    URetainerBox* Retainer=nullptr;bool NativeMaterialBrush=false;
    Blueprint->WidgetTree->ForEachWidget([&](UWidget* Child)
    {
        if(auto R=Cast<URetainerBox>(Child))if(Design2UMGWidgetPropertyAccess::GetRetainerTextureParameter(*R)==TEXT("D2UObject"))Retainer=R;
        if(auto B=Cast<UBorder>(Child))NativeMaterialBrush|=Cast<UMaterialInterface>(B->Background.GetResourceObject())!=nullptr;
        if(auto I=Cast<UImage>(Child))NativeMaterialBrush|=Cast<UMaterialInterface>(Design2UMGWidgetPropertyAccess::GetBrush(*I).GetResourceObject())!=nullptr;
    });
    if(!TestNotNull(TEXT("Generated effect uses engine RetainerBox"),Retainer))return false;
    auto Padding=Cast<UBorder>(Retainer->GetContent());
    TestTrue(TEXT("Capture padding is an actual native border"),Padding&&Padding->GetPadding().Left==4&&Padding->GetPadding().Top==3);
    TestTrue(TEXT("Generated fill uses a native material brush"),NativeMaterialBrush);
    UEdGraph* Graph=Blueprint->UbergraphPages[0];
    UK2Node_Event* Tick=nullptr;
    for(UEdGraphNode* N:Graph->Nodes)if(auto Event=Cast<UK2Node_Event>(N))if(Event->EventReference.GetMemberName()==TEXT("Tick"))Tick=Event;
    if(!Tick)
    {
        Tick=NewObject<UK2Node_Event>(Graph);
        Tick->EventReference.SetExternalMember(TEXT("Tick"),UUserWidget::StaticClass());Tick->bOverrideFunction=true;
        Graph->AddNode(Tick,false,false);Tick->CreateNewGuid();Tick->PostPlacedNewNode();Tick->AllocateDefaultPins();
    }
    auto UserCall=NewObject<UK2Node_CallFunction>(Graph);
    UserCall->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
    Graph->AddNode(UserCall,false,false);UserCall->CreateNewGuid();UserCall->PostPlacedNewNode();UserCall->AllocateDefaultPins();
    Tick->GetThenPin()->MakeLinkTo(UserCall->GetExecPin());
    auto* UserPaint = FBlueprintEditorUtils::CreateNewGraph(Blueprint.Get(), TEXT("OnPaint"), UEdGraph::StaticClass(),
                                                            UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(Blueprint.Get(), UserPaint, false, UUserWidget::StaticClass());
    UK2Node_FunctionEntry* UserEntry = nullptr;
    UK2Node_FunctionResult* UserResult = nullptr;
    for (UEdGraphNode* Node : UserPaint->Nodes)
    {
        if (auto* Entry = Cast<UK2Node_FunctionEntry>(Node))
            UserEntry = Entry;
        if (auto* Result = Cast<UK2Node_FunctionResult>(Node))
            UserResult = Result;
    }
    if (!TestNotNull(TEXT("User OnPaint has its native entry"), UserEntry) ||
        !TestNotNull(TEXT("User OnPaint has its native result"), UserResult))
        return false;
    UEdGraphPin* UserPaintThen = UserEntry->FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* UserPaintExec = UserResult->FindPin(UEdGraphSchema_K2::PN_Execute);
    if (!TestNotNull(TEXT("User OnPaint exposes native execution output"), UserPaintThen) ||
        !TestNotNull(TEXT("User OnPaint exposes native execution input"), UserPaintExec))
        return false;
    UserPaintThen->MakeLinkTo(UserPaintExec);
    TArray<UWidget*> OriginalTargets;
    Blueprint->WidgetTree->ForEachWidget([&](UWidget* Child) {
        if (Child == Retainer)
            OriginalTargets.Add(Child);
        else if (auto* Border = Cast<UBorder>(Child))
        {
            if (Cast<UMaterialInterface>(Border->Background.GetResourceObject()))
                OriginalTargets.Add(Child);
        }
        else if (auto* Image = Cast<UImage>(Child))
        {
            if (Cast<UMaterialInterface>(Design2UMGWidgetPropertyAccess::GetBrush(*Image).GetResourceObject()))
                OriginalTargets.Add(Child);
        }
    });
    TestEqual(TEXT("Fill and effect provide two actual native targets"), OriginalTargets.Num(), 2);
    const int32 ParentStateCount = Blueprint->NewVariables.Num();
    const int32 UserTickNodeCount = Graph->Nodes.Num();
    const int32 UserPaintNodeCount = UserPaint->Nodes.Num();
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
    const auto OriginalWidgetGuids = Blueprint->WidgetVariableNameToGuidMap;
#endif
    TArray<FString> GeneratedNames;
    auto InputCall = [](UEdGraphNode* Node, FName Input) -> UK2Node_CallFunction* {
        UEdGraphPin* Pin = Node ? Node->FindPin(Input) : nullptr;
        return Pin && Pin->LinkedTo.Num() == 1 ? Cast<UK2Node_CallFunction>(Pin->LinkedTo[0]->GetOwningNode())
                                               : nullptr;
    };
    auto Calls = [](UK2Node_CallFunction* Node, FName Function) {
        return Node && Node->FunctionReference.GetMemberName() == Function;
    };
    auto Literal = [&](UK2Node_CallFunction* Node, double Value) {
        UEdGraphPin* Pin = Node ? Node->FindPin(TEXT("Value")) : nullptr;
        return Calls(Node, TEXT("MakeLiteralDouble")) && Pin && FCString::Atod(*Pin->DefaultValue) == Value;
    };
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        if (!TestTrue(TEXT("Runtime surface preparation succeeds"),
                      PencilShaderRuntimeBindings::Prepare(Blueprint.Get(), Error, &GeneratedNames)))
        {
            AddError(Error);
            return false;
        }
        TestEqual(TEXT("Surface preparation leaves parent state unchanged"), Blueprint->NewVariables.Num(),
                  ParentStateCount);
        TestEqual(TEXT("Surface preparation leaves user Tick nodes unchanged"), Graph->Nodes.Num(), UserTickNodeCount);
        TestEqual(TEXT("Surface preparation leaves user OnPaint nodes unchanged"), UserPaint->Nodes.Num(),
                  UserPaintNodeCount);
        TestTrue(TEXT("User Tick chain remains directly connected"),
                 Graph->Nodes.Contains(Tick) && Graph->Nodes.Contains(UserCall) &&
                     Tick->GetThenPin()->LinkedTo.Contains(UserCall->GetExecPin()));
        TestTrue(TEXT("User OnPaint chain remains directly connected"),
                 UserPaintThen->LinkedTo.Contains(UserPaintExec));
        TArray<UUserWidget*> Wrappers;
        Blueprint->WidgetTree->ForEachWidget([&](UWidget* Child) {
            if (auto* Surface = Cast<UUserWidget>(Child))
                if (OriginalTargets.Contains(Surface->GetContentForSlot(TEXT("Surface"))))
                    Wrappers.Add(Surface);
        });
        TestEqual(TEXT("Repeated preparation retains exactly one native surface per target"), Wrappers.Num(),
                  OriginalTargets.Num());
        TestEqual(TEXT("Generated names contain only current surfaces"), GeneratedNames.Num(), Wrappers.Num());
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
        for (const auto& Entry : OriginalWidgetGuids)
            TestEqual(TEXT("Surface preparation preserves existing widget GUIDs"),
                      Blueprint->WidgetVariableNameToGuidMap.FindRef(Entry.Key), Entry.Value);
#endif
        for (UUserWidget* Surface : Wrappers)
        {
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
            if (!Blueprint->WidgetVariableNameToGuidMap.IsEmpty())
                TestTrue(TEXT("New surfaces join an initialized widget GUID map before compilation"),
                         Blueprint->WidgetVariableNameToGuidMap.FindRef(Surface->GetFName()).IsValid());
#endif
            UWidget* Original = Surface->GetContentForSlot(TEXT("Surface"));
            TestTrue(TEXT("Original native widget keeps its name, identity and outer"),
                     Blueprint->WidgetTree->FindWidget(Original->GetFName()) == Original &&
                         Original->GetOuter() == Blueprint->WidgetTree);
            TestTrue(TEXT("Only native surface wrappers enter generated names"),
                     GeneratedNames.Contains(Surface->GetName()) && !GeneratedNames.Contains(Original->GetName()));
            auto* SurfaceBlueprint = Cast<UWidgetBlueprint>(Surface->GetClass()->ClassGeneratedBy);
            if (!TestNotNull(TEXT("Surface is an engine UserWidget blueprint"), SurfaceBlueprint))
                return false;
            TestTrue(TEXT("Surface blueprint is an independently generated native asset"),
                     SurfaceBlueprint->GetPathName().StartsWith(TEXT("/Game/Design2UMG/GeneratedShaderWidgets/")));
            UWidget* SurfaceRoot = SurfaceBlueprint->WidgetTree->RootWidget;
            TestTrue(TEXT("Template serializes original content for the actual native named slot"),
                     Surface->WidgetTree == nullptr && SurfaceRoot && SurfaceRoot->IsA<UNamedSlot>() &&
                         SurfaceRoot->GetFName() == TEXT("Surface") &&
                         Surface->GetContentForSlot(SurfaceRoot->GetFName()) == Original);
            FDesign2UMGGenerationManifestSidecar SurfaceManifest;
            FString ManifestError;
            if (!TestTrue(TEXT("Saved shader surface ownership record can be read"),
                          Design2UMGGenerationManifest::TryLoadSidecarForAsset(
                              SurfaceBlueprint->GetOutermost()->GetName(), SurfaceManifest, ManifestError)))
            {
                AddError(ManifestError);
                return false;
            }
            const auto* RootRecord = SurfaceManifest.FindInstanceByDesignNodeId(SurfaceManifest.SourceRootNodeId);
            TestTrue(TEXT("Saved ownership records the actual generated surface root"),
                     SurfaceRoot && RootRecord && RootRecord->Kind == Design2UMGManifestKinds::Generated &&
                         RootRecord->WidgetName == SurfaceRoot->GetName() &&
                         SurfaceManifest.SourceRootNodeId ==
                             SurfaceManifest.DocumentScope + TEXT("/") + SurfaceRoot->GetName() &&
                         SurfaceManifest.D2UOwned.Contains(SurfaceRoot->GetName()));
            UFunction* PaintFunction = Surface->GetClass()->FindFunctionByName(TEXT("OnPaint"));
            TestTrue(TEXT("Native surface OnPaint remains const"),
                     PaintFunction && PaintFunction->GetOuterUClass() == Surface->GetClass() &&
                         PaintFunction->HasAnyFunctionFlags(FUNC_Const));
            UEdGraph* Paint = nullptr;
            for (UEdGraph* Function : SurfaceBlueprint->FunctionGraphs)
                if (Function->GetFName() == TEXT("OnPaint"))
                    Paint = Function;
            if (!TestNotNull(TEXT("Surface owns its actual parameter graph"), Paint))
                return false;
            int32 MouseCalls = 0, CenterCoordinates = 0, SmoothedPositions = 0;
            bool ReadsMouse = false, ReadsResolution = false, StoresMouse = false;
            for (UEdGraphNode* Node : Paint->Nodes)
            {
                auto* Call = Cast<UK2Node_CallFunction>(Node);
                if (!Call)
                    continue;
                if (Calls(Call, TEXT("GetMousePositionOnPlatform")))
                {
                    ++MouseCalls;
                    TestTrue(TEXT("Surface mouse sampling is executed, not a dangling pure read"),
                             Call->GetExecPin() && Call->GetExecPin()->LinkedTo.Num() == 1 && Call->GetThenPin() &&
                                 Call->GetThenPin()->LinkedTo.Num() == 1);
                }
                if (Calls(Call, TEXT("K2_GetVectorParameterValue")))
                {
                    UEdGraphPin* Parameter = Call->FindPin(TEXT("ParameterName"));
                    ReadsMouse |= Parameter && Parameter->DefaultValue == TEXT("D2UMouse");
                    ReadsResolution |= Parameter && Parameter->DefaultValue == TEXT("D2UResolution");
                }
                if (Calls(Call, TEXT("Multiply_DoubleDouble")) && Literal(InputCall(Call, TEXT("B")), 0.5))
                {
                    UEdGraphPin* Input = Call->FindPin(TEXT("A"));
                    UEdGraphPin* Dimension = Input && Input->LinkedTo.Num() == 1 ? Input->LinkedTo[0] : nullptr;
                    UEdGraphPin* Result = Call->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
                    UEdGraphPin* Center = Result && Result->LinkedTo.Num() == 1 ? Result->LinkedTo[0] : nullptr;
                    if (Dimension && Calls(InputCall(Call, TEXT("A")), TEXT("BreakVector2D")) &&
                        (Dimension->PinName == TEXT("X") || Dimension->PinName == TEXT("Y")) && Center &&
                        Center->PinName == TEXT("B") &&
                        Calls(Cast<UK2Node_CallFunction>(Center->GetOwningNode()), TEXT("SelectFloat")))
                        ++CenterCoordinates;
                }
                if (Calls(Call, TEXT("SetVectorParameterValue")))
                {
                    UEdGraphPin* Parameter = Call->FindPin(TEXT("ParameterName"));
                    if (!Parameter || Parameter->DefaultValue != TEXT("D2UMouse"))
                        continue;
                    StoresMouse = true;
                    auto* Color = InputCall(Call, TEXT("Value"));
                    auto* UpY = InputCall(Color, TEXT("G"));
                    auto* Position = InputCall(UpY, TEXT("B"));
                    auto* Sum = InputCall(Position, TEXT("InVec"));
                    auto* Weighted = InputCall(Sum, TEXT("B"));
                    auto* Exponential = InputCall(Weighted, TEXT("B"));
                    auto* Rate = InputCall(Exponential, TEXT("A"));
                    if (Calls(Color, TEXT("MakeColor")) && Calls(UpY, TEXT("Subtract_DoubleDouble")) &&
                        Calls(InputCall(UpY, TEXT("A")), TEXT("BreakVector2D")) &&
                        Calls(Position, TEXT("BreakVector2D")) &&
                        UpY->FindPin(TEXT("A"))->LinkedTo[0]->PinName == TEXT("Y") &&
                        UpY->FindPin(TEXT("B"))->LinkedTo[0]->PinName == TEXT("Y") &&
                        Calls(Sum, TEXT("Add_Vector2DVector2D")) && Calls(Weighted, TEXT("Multiply_Vector2DFloat")) &&
                        Calls(InputCall(Weighted, TEXT("A")), TEXT("Subtract_Vector2DVector2D")) &&
                        Calls(Exponential, TEXT("Exp")) && Calls(Rate, TEXT("Multiply_DoubleDouble")) &&
                        Literal(InputCall(Rate, TEXT("B")), -5))
                        ++SmoothedPositions;
                    TestTrue(TEXT("Per-instance MID stores clock and initialized state with mouse position"),
                             Calls(InputCall(Color, TEXT("B")), TEXT("GetRealTimeSeconds")) &&
                                 Literal(InputCall(Color, TEXT("A")), 1));
                }
            }
            TestEqual(TEXT("Each native surface samples the platform mouse once"), MouseCalls, 1);
            TestTrue(TEXT("Per-instance MID reads previous mouse and height before replacement"),
                     ReadsMouse && ReadsResolution && StoresMouse);
            TestEqual(TEXT("Mouse initializes both coordinates from half the logical size"), CenterCoordinates, 2);
            TestEqual(TEXT("Mouse output retains exponential smoothing and source Y flip"), SmoothedPositions, 1);
        }
    }
    if(!TestNotNull(TEXT("Shader fill has a real geometry mask dependency"),FillGeometryMask))return false;
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    FillGeometryMask->Source.ForceGenerateGuid();FillGeometryMask->PostEditChange();
    TArray<uint8> RenderedPng;FIntPoint RenderedSize;
    if(!TestTrue(TEXT("Native renderer prepares material-bound texture resources"),
        FUMGWidgetRenderer::RenderWidgetToPng(Blueprint.Get(),RenderedPng,RenderedSize,&Error)))
    {AddError(Error);return false;}
    TestFalse(TEXT("Material-bound mask compilation is complete before rendering returns"),FillGeometryMask->IsCompiling());
    TestEqual(TEXT("Material-bound mask uses its source dimensions, not the async placeholder"),static_cast<int64>(FillGeometryMask->GetSizeX()),FillGeometryMask->Source.GetSizeX());
    const FString SdfSource = TEXT(
        "precision mediump float;\n/** @sdf */uniform sampler2D DistanceField;\n/** @sdf */uniform sampler2D DistanceAlias;\nvoid main(){float d=(texture2D(DistanceField,vec2(0.5)).r+texture2D(DistanceAlias,vec2(0.5)).r)*0.5;gl_FragColor=vec4(vec3(step(0.0,d)),1.0);}");
    if (!TestTrue(
            TEXT("Own SDF fixture writes"),
            FFileHelper::SaveStringToFile(SdfSource, *SdfFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
        return false;
    FUMGWidgetNode SdfNode;
    SdfNode.Type = TEXT("Image");
    SdfNode.Brush.Shader = FDesignShaderData();
    SdfNode.Brush.Shader->Url = SdfFile;
    SdfNode.Brush.Shader->Size = FVector2D(17, 13);
    FDesignShaderValue IgnoredSdfUri;
    IgnoredSdfUri.Kind = FDesignShaderValue::EKind::String;
    IgnoredSdfUri.String = TEXT("not-an-sdf-image.png");
    SdfNode.Brush.Shader->Uniforms.Add(TEXT("DistanceField"), IgnoredSdfUri);
    if (!TestTrue(TEXT("SDF binding uses its native floating-point resource, not an image URI"),
                  FPreBuildResourceNormalizer::NormalizeGeneratedTextures(SdfNode, Folder, TEXT("Sdf"), Error)))
        return false;
    auto *SdfMaterial = LoadObject<UMaterial>(nullptr, *SdfNode.Brush.MaterialRef.Get(FString()));
    if (!TestNotNull(TEXT("SDF fill material is persisted"), SdfMaterial))
        return false;
    Materials.AddUnique(SdfMaterial);
    UTexture *SdfTexture = nullptr;
    TestTrue(TEXT("SDF parameter retains a floating-point native render target"),
             SdfMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("D2USdf")), SdfTexture) &&
                 Cast<UTextureRenderTarget2D>(SdfTexture) &&
                 CastChecked<UTextureRenderTarget2D>(SdfTexture)->RenderTargetFormat == RTF_RGBA16f);
    int32 SdfParameters = 0;
    bool HasResolution = false, HasDrawRect = false;
    for (const UMaterialExpression *E : Design2UMGMaterialAccess::GetExpressions(*SdfMaterial))
    {
        if (const auto *T = Cast<UMaterialExpressionTextureObjectParameter>(E))
            SdfParameters += T->ParameterName == TEXT("D2USdf") ? 1 : 0;
        if (const auto *V = Cast<UMaterialExpressionVectorParameter>(E))
        {
            HasResolution |= V->ParameterName == TEXT("D2UResolution");
            HasDrawRect |= V->ParameterName == TEXT("D2UDrawRect");
        }
    }
    TestTrue(
        TEXT(
            "All SDF uniforms share one dynamic parameter and retain real geometry bindings without coordinate builtins"),
        SdfParameters == 1 && HasResolution && HasDrawRect);
    PencilShaderSyntax::FProgram SdfProgram;
    if (!TestTrue(TEXT("SDF annotation uses the production GLSL compiler"),
                  PencilShaderCompiler::Compile(SdfSource, SdfProgram, Error)))
        return false;
    const FString SdfCode(UTF8_TO_TCHAR(SdfProgram.Code.c_str()));
    TestTrue(TEXT("SDF clamps to actual texture texel centers independently of native render-target wrap"),SdfCode.Contains(TEXT("GetDimensions(width,height)"))&&SdfCode.Contains(TEXT("clamp(mapped,halfTexel,1-halfTexel)")));
    TestFalse(TEXT("Constant-UV SDF does not assign a nonexistent ANGLE fragment coordinate field"),
              SdfCode.Contains(TEXT("State.gl_FragCoord=")));
    PencilShaderSyntax::FProgram UnusedCoordinateProgram;
    if(!TestTrue(TEXT("Unused source coordinate compiles through production ANGLE"),PencilShaderCompiler::Compile(TEXT("precision mediump float;void main(){vec4 unused=gl_FragCoord;gl_FragColor=vec4(1.0);}"),UnusedCoordinateProgram,Error)))return false;
    TestTrue(TEXT("Source coordinate metadata is preserved while inactive ANGLE state is not assigned"),UnusedCoordinateProgram.UsesCoordinates&&!FString(UTF8_TO_TCHAR(UnusedCoordinateProgram.Code.c_str())).Contains(TEXT("State.gl_FragCoord=")));
    TestTrue(TEXT("SDF maps DrawRect coordinates without object transparency coverage"),
             SdfCode.Contains(TEXT("D2USdfFlags")) &&
                 SdfCode.Contains(TEXT("D2UObjectFlags[i]==0&&D2USdfFlags[i]==0")) &&
                 SdfCode.Contains(TEXT("return 1-D2UObjectFlags[i]+D2UObjectFlags[i]*")));
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    TestTrue(TEXT("Native runtime bindings compile"),
             Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings);
    return true;
}

namespace
{
// Observe one draw on each natural frame for manual and imported prefix captures.
class FRetainerPrefixFrames final : public IAutomationLatentCommand
{
public:
    explicit FRetainerPrefixFrames(FAutomationTestBase& InTest) : Test(InTest) {}
    ~FRetainerPrefixFrames() override
    {
        if (Root.IsValid())
            Root->ReleaseSlateResources(true);
        Slate.Reset();
        Root.Reset();
        if (Rendered.IsValid())
            UKismetRenderingLibrary::ReleaseRenderTarget2D(Rendered.Get());
        FlushRenderingCommands();
        if (Blueprint.IsValid()) Blueprint->ClearFlags(RF_Standalone);
        Blueprint.Reset();
        if (!AssetFolder.IsEmpty())
            Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(AssetFolder);
    }

    FAutomationTestBase& Test;
    TStrongObjectPtr<UWidgetBlueprint> Blueprint;
    TStrongObjectPtr<UUserWidget> Root;
    TStrongObjectPtr<UTextureRenderTarget2D> Rendered;
    TSharedPtr<SWidget> Slate;
    URetainerBox* Capture = nullptr;
    UBorder* Marker = nullptr;
    UImage* ImageMarker = nullptr;
    bool bFillComposition = false;
    UImage* Output = nullptr;
    FString AssetFolder;
    FString EvidenceDirectory;
    uint64 PreviousFrame = MAX_uint64;
    int32 Draws = 0;

    bool Update() override
    {
        if (PreviousFrame == GFrameCounter)
            return false;
        PreviousFrame = GFrameCounter;
        const FLinearColor Color = Draws == 0 ? FLinearColor::Red : FLinearColor::Green;
        if (ImageMarker) ImageMarker->SetColorAndOpacity(Color);
        else Marker->SetBrushColor(Color);
        Capture->RequestRender();
        FWidgetRenderer Renderer(true);
        Renderer.DrawWidget(Rendered.Get(), Slate.ToSharedRef(), FVector2D(16), 1.f / 60.f, false);
        FlushRenderingCommands();
        ++Draws;

        UMaterialInstanceDynamic* Display = Output->GetDynamicMaterial();
        auto* Raw = Display ? Cast<UTextureRenderTarget2D>(Display->K2_GetTextureParameterValue(TEXT("D2UObject"))) : nullptr;
        auto* Mip = Display ? Cast<UTextureRenderTarget2D>(Display->K2_GetTextureParameterValue(TEXT("D2UObjectMip"))) : nullptr;
        if (!Test.TestNotNull(TEXT("This paint publishes the actual prefix capture"), Raw) ||
            !Test.TestNotNull(TEXT("This paint publishes the auto-mip copy"), Mip))
            return true;
        int32 MipCount = 0;
        FTextureRenderTargetResource* Resource = Mip->GameThread_GetRenderTargetResource();
        if (Resource)
        {
            ENQUEUE_RENDER_COMMAND(ReadPrefixMipCount)([Resource, &MipCount](FRHICommandListImmediate&) {
                if (const auto Texture = Resource->GetRenderTargetTexture())
                    MipCount = Texture->GetNumMips();
            });
            FlushRenderingCommands();
        }
        UWorld* World = Root->GetWorld();
        const FLinearColor RawLeft = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Raw, 3, 8, false);
        const FLinearColor RawRight = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Raw, 14, 2, false);
        const FLinearColor CopiedLeft = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Mip, 3, 8, false);
        const FLinearColor Consumer = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Rendered.Get(), 3, 8, false);
        const FLinearColor Future = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Rendered.Get(), 14, 2, false);
        const FLinearColor Blank = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Rendered.Get(), 9, 8, false);
        // Kismet raw reads return byte units for BGRA8, but unit values for floating-point targets.
        const auto UnitAlpha = [](UTextureRenderTarget2D* Target, float Value) {
            return Target->GetFormat() == PF_B8G8R8A8 ? Value / 255.f : Value;
        };
        const float RawAlpha = UnitAlpha(Raw, RawLeft.A);
        const float CopyAlpha = UnitAlpha(Mip, CopiedLeft.A);
        const float ConsumerAlpha = UnitAlpha(Rendered.Get(), Consumer.A);
        const FString Observation = FString::Printf(
            TEXT("frame=%llu draw=%d mips=%d rawLeft=%s rawRight=%s copyLeft=%s consumerLOD4=%s future=%s blank=%s generatedFill=%d formats(raw,mip,output)=(%d,%d,%d)\n"),
            static_cast<unsigned long long>(GFrameCounter), Draws, MipCount, *RawLeft.ToString(),
            *RawRight.ToString(), *CopiedLeft.ToString(), *Consumer.ToString(), *Future.ToString(),
            *Blank.ToString(), int32(bFillComposition),
            int32(Raw->GetFormat()), int32(Mip->GetFormat()), int32(Rendered->GetFormat()));
        Test.AddInfo(Observation);
        Test.TestTrue(TEXT("Retain original frame observations"), FFileHelper::SaveStringToFile(Observation,
            *(EvidenceDirectory / TEXT("observations.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
            &IFileManager::Get(), FILEWRITE_Append));
        Test.TestTrue(TEXT("Prefix has the actual fixture dimensions"),
            Raw->SizeX == 16 && Raw->SizeY == 16 && Mip->SizeX == 16 && Mip->SizeY == 16);
        Test.TestEqual(TEXT("The GPU has a real 1x1 mip 4, not a clamped mip-0 allocation"), MipCount, 5);
        Test.TestTrue(TEXT("The source marker keeps its local half opacity"), FMath::Abs(RawAlpha - 0.5f) < 0.08f);
        Test.TestTrue(TEXT("Copy preserves source alpha"), FMath::Abs(CopyAlpha - RawAlpha) < 0.03f);
        Test.TestTrue(TEXT("Clip excludes overflow; capture excludes future and consumer draws"),
            UnitAlpha(Raw, RawRight.A) < 0.03f && RawRight.B < 0.03f && RawLeft.B < 0.03f);
        Test.TestTrue(TEXT("The later blue draw actually reaches the final target"), Future.B > 0.5f);
        // Only half of the prefix is covered: mip 4 must halve its alpha, unlike mip 0 at UV .25.
        const float MipAlpha = RawAlpha * 0.5f;
        const float ExpectedAlpha = bFillComposition ? MipAlpha + RawAlpha * (1.f - MipAlpha) : MipAlpha;
        Test.TestTrue(TEXT("The displayed consumer really reads the current nonzero mip"),
            FMath::Abs(ConsumerAlpha - ExpectedAlpha) < 0.08f && Consumer.B > 0.05f);
        if (bFillComposition)
            Test.TestTrue(TEXT("Generated fill leaves unpainted pixels transparent"), UnitAlpha(Rendered.Get(), Blank.A) < 0.03f);
        const bool RedFrame = Draws == 1;
        Test.TestTrue(TEXT("Raw capture contains this frame's marker, not the previous frame"),
            RedFrame ? RawLeft.R > 0.2f && RawLeft.G < 0.03f : RawLeft.G > 0.2f && RawLeft.R < 0.03f);
        Test.TestTrue(TEXT("Mip consumer contains this frame's marker, not future/self/previous content"),
            RedFrame ? Consumer.R > Consumer.G + 0.03f : Consumer.G > Consumer.R + 0.03f);
        return Draws == 2;
    }
};

bool QueuePrefixFrames(FAutomationTestBase& Test, const TSharedRef<FRetainerPrefixFrames>& Frames, UWorld* World)
{
    FKismetEditorUtilities::CompileBlueprint(Frames->Blueprint.Get());
    if (!Test.TestTrue(TEXT("Prefix generated blueprint compiles"), Frames->Blueprint->Status == BS_UpToDate))
        return false;
    Frames->Root.Reset(CreateWidget<UUserWidget>(World, TSubclassOf<UUserWidget>(Frames->Blueprint->GeneratedClass.Get())));
    Frames->Rendered.Reset(FWidgetRenderer::CreateTargetFor(FVector2D(16), TF_Nearest, true));
    if (!Test.TestNotNull(TEXT("Runtime prefix root"), Frames->Root.Get()) || !Test.TestNotNull(TEXT("Prefix output target"), Frames->Rendered.Get()))
        return false;
    Frames->Slate = Frames->Root->TakeWidget();
    int32 CaptureCount = 0;
    int32 ImageMarkerCount = 0;
    int32 OutputCount = 0;
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Frames->Root.Get(), Objects, true);
    for (UObject* RuntimeObject : Objects)
    {
        if (auto* Retainer = Cast<URetainerBox>(RuntimeObject))
        {
            Frames->Capture = Retainer;
            ++CaptureCount;
        }
        if (auto* Border = Cast<UBorder>(RuntimeObject); Border && Border->GetFName() == TEXT("FrameMarker"))
            Frames->Marker = Border;
        if (auto* Image = Cast<UImage>(RuntimeObject))
        {
            auto* Material = Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject());
            FLinearColor Role;
            if (!Frames->bFillComposition || (Material && Material->GetVectorParameterValue(
                FMaterialParameterInfo(TEXT("D2UFillObject")), Role)))
            {
                Frames->Output = Image;
                ++OutputCount;
            }
            else if (Cast<UTexture2D>(Image->GetBrush().GetResourceObject()))
            {
                Frames->ImageMarker = Image;
                ++ImageMarkerCount;
            }
        }
    }
    Test.TestEqual(TEXT("Exactly one native prefix capture"), CaptureCount, 1);
    Test.TestEqual(TEXT("Exactly one actual mip consumer"), OutputCount, 1);
    if (Frames->bFillComposition)
        Test.TestEqual(TEXT("Exactly one imported image marker"), ImageMarkerCount, 1);
    if (!Test.TestNotNull(TEXT("Actual native Retainer"), Frames->Capture) ||
        !Test.TestNotNull(TEXT("Actual frame marker"), Frames->bFillComposition ? static_cast<UWidget*>(Frames->ImageMarker) : static_cast<UWidget*>(Frames->Marker)) ||
        !Test.TestNotNull(TEXT("Actual later mip consumer"), Frames->Output))
        return false;
    FAutomationTestFramework::Get().EnqueueLatentCommand(Frames);
    return true;
}

bool QueueGeneratedFillPrefixFrames(FAutomationTestBase& Test)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!Test.TestNotNull(TEXT("Generated fill trial has a real editor world"), World)) return false;
    const auto* Defer = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.DeferRetainedRenderingRenderThread"));
    if (!Test.TestTrue(TEXT("Generated fill exercises immediate native Retainer rendering"), Defer && Defer->GetInt() == 0))
        return false;
    TSharedRef<FRetainerPrefixFrames> Frames = MakeShared<FRetainerPrefixFrames>(Test);
    Frames->bFillComposition = true;
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Frames->EvidenceDirectory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/GeneratedFillPrefixFrames") / Id;
    Frames->AssetFolder = TEXT("/Game/Design2UMG/Automation/GeneratedFillPrefixFrames_") + Id;
    if (!Test.TestTrue(TEXT("Create generated prefix evidence directory"), IFileManager::Get().MakeDirectory(*Frames->EvidenceDirectory, true)))
        return false;
    TArray<FColor> Pixels;
    Pixels.SetNumZeroed(16 * 16);
    for (int32 Y = 0; Y < 16; ++Y)
        for (int32 X = 0; X < 8; ++X)
            Pixels[Y * 16 + X] = FColor(255, 255, 255, 128);
    TArray64<uint8> Png;
    FImageUtils::PNGCompressImageArray(16, 16, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
    if (!Test.TestTrue(TEXT("Write owned half-transparent prefix image"), FFileHelper::SaveArrayToFile(Png,
        *(Frames->EvidenceDirectory / TEXT("marker.png"))))) return false;
    const FString Source = TEXT(R"GLSL(#version 100
#extension GL_EXT_shader_texture_lod : require
precision mediump float;
/**
 * @default pencil:object
 * @mipmap true
 */
uniform sampler2D MipObject;
/** @default pencil:object */ uniform sampler2D RawObject;
void main(){
    vec4 p=texture2DLodEXT(MipObject,vec2(0.25),4.0);
    vec4 r=texture2D(RawObject,vec2(0.25));
    gl_FragColor=gl_FragCoord.x<8.0?vec4(p.rg,0.75*p.a+r.b,p.a):vec4(0.0);
}
)GLSL");
    const FString Json = TEXT(R"JSON({"version":"2.21","children":[
      {"id":"generatedPrefix","name":"GeneratedPrefix","type":"frame","width":16,"height":16,
       "layout":"none","clip":true,
       "fill":[{"type":"image","url":"marker.png","mode":"stretch"},{"type":"shader","url":"prefix.glsl"}],
       "children":[{"id":"later","type":"rectangle","x":12,"y":0,"width":4,"height":4,"fill":"#0000ff"}]}
    ]})JSON");
    const FString DocumentFile = Frames->EvidenceDirectory / TEXT("source.pen");
    if (!Test.TestTrue(TEXT("Write generated prefix shader"), FFileHelper::SaveStringToFile(Source,
        *(Frames->EvidenceDirectory / TEXT("prefix.glsl")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) ||
        !Test.TestTrue(TEXT("Write generated prefix Pencil source"), FFileHelper::SaveStringToFile(Json,
        *DocumentFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
    FDesignDocument Document;
    if (!Test.TestTrue(TEXT("Parse actual generated prefix source"), FPenParser::ParseDocumentFile(DocumentFile, Document)))
        return false;
    FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings());
    FDesignImageImportSettingsSnapshot ImageSettings;
    ImageSettings.bEnableRemoteImageImport = false;
    ImageSettings.RemoteImageImportPath = Frames->AssetFolder / TEXT("Images");
    const FDesignImageImportResult Images = DesignImportPlanning::ResolveImages(Data, nullptr, &ImageSettings);
    if (!Test.TestFalse(TEXT("Resolve owned local prefix image before texture normalization"), Images.HasFailures()))
        return false;
    FString Error;
    if (!Test.TestTrue(TEXT("Normalize actual generated prefix fills"), FPreBuildResourceNormalizer::NormalizeGeneratedTextures(
        Data.RootWidget, Frames->AssetFolder, TEXT("GeneratedPrefix"), Error)))
    { Test.AddError(Error); return false; }
    Frames->Blueprint.Reset(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), GetTransientPackage(), *FString(TEXT("GeneratedFillPrefixFrames_") + Id), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
    if (!Test.TestNotNull(TEXT("Actual generated prefix blueprint"), Frames->Blueprint.Get())) return false;
    Frames->Blueprint->WidgetTree->RootWidget = FUMGWidgetTreeBuilder::CreateWidgetRecursive(Frames->Blueprint->WidgetTree, Data.RootWidget);
    if (!Test.TestTrue(TEXT("Prepare actual generated fill bindings"), PencilShaderRuntimeBindings::Prepare(Frames->Blueprint.Get(), Error)))
    { Test.AddError(Error); return false; }
    // Keep the real generated fill material: this trial observes CaptureFillPrefix and its composite output.
    return QueuePrefixFrames(Test, Frames, World);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilShaderFillObjectTest,
    "Design2UMG.Resources.PencilShaderFillObject", EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FPencilShaderFillObjectTest::RunTest(const FString&)
{
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Directory = FPaths::ProjectSavedDir()/TEXT("Design2UMG/ShaderFill")/Id;
    const FString Folder = TEXT("/Game/Design2UMG/Automation/ShaderFill_") + Id;
    const FString ShaderFile = Directory/TEXT("fill.glsl");
    const FString DocumentFile = Directory/TEXT("source.pen");
    TArray<UObject*> Materials;
    ON_SCOPE_EXIT
    {
        if (!Materials.IsEmpty()) ObjectTools::ForceDeleteObjects(Materials, false);
        Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder);
        IFileManager::Get().Delete(*ShaderFile);
        IFileManager::Get().Delete(*DocumentFile);
        IFileManager::Get().DeleteDirectory(*Directory, false, false);
    };
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Source = TEXT(R"GLSL(#version 100
precision mediump float;
/** @resolution */ uniform vec2 Extent;
/**
 * @default pencil:object
 * @mipmap true
 */
uniform sampler2D Previous;
/** @sdf */ uniform sampler2D Shape;
void main(){vec2 uv=gl_FragCoord.xy/Extent;gl_FragColor=texture2D(Previous,uv)*step(0.0,texture2D(Shape,uv).r);}
)GLSL");
    const FString Json = TEXT(R"JSON({"version":"2.21","children":[
      {"id":"layered","type":"frame","width":32,"height":24,"opacity":0.6,"clip":true,"cornerRadius":4,
       "fill":["#ff000080",{"type":"shader","url":"fill.glsl","opacity":0.5},"#0000ff40"],
       "effect":{"type":"shadow","shadowType":"outer","color":"#00000080","blur":2,"offset":{"x":2,"y":1}},
       "children":[{"id":"kept","name":"Kept","type":"rectangle","width":3,"height":3,"fill":"#ffffff"}]},
      {"id":"empty","type":"frame","width":16,"height":16,"fill":{"type":"shader","url":"fill.glsl"},
       "children":[{"id":"emptyKept","type":"rectangle","width":2,"height":2,"fill":"#ffffff"}]},
      {"id":"ordinary","type":"rectangle","width":16,"height":16,
       "fill":{"type":"shader","url":"fill.glsl","uniforms":{"Previous":"/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"}}}
    ]})JSON");
    if (!TestTrue(TEXT("Own fill shader writes"), FFileHelper::SaveStringToFile(Source, *ShaderFile,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) ||
        !TestTrue(TEXT("Own fill document writes"), FFileHelper::SaveStringToFile(Json, *DocumentFile,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
    FDesignDocument Document;
    if (!TestTrue(TEXT("Real Pencil fill parses"), FPenParser::ParseDocumentFile(DocumentFile, Document))) return false;
    FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings());
    TFunction<void(FUMGWidgetNode&)> UseNativeContent = [&](FUMGWidgetNode& Node)
    {
        if (Node.Id == TEXT("kept")) Node.Type = TEXT("Button");
        for (auto& Child : Node.Children) UseNativeContent(Child);
    };
    UseNativeContent(Data.RootWidget);
    FString Error;
    FUMGWidgetData Parsed;
    if (!TestTrue(TEXT("Fill role survives generated JSON"), FUMGJsonParser::ParseJson(
        FUMGJsonSerializer::Serialize(Data), Parsed, &Error))) return false;
    int32 FillRoles = 0, UnownedFillShaders = 0;
    TFunction<void(const FUMGWidgetNode&)> CountRoles = [&](const FUMGWidgetNode& Node)
    {
        if (Node.Brush.Shader.IsSet())
        {
            if (Node.Brush.Shader->bFillPaint) ++FillRoles;
            else ++UnownedFillShaders;
        }
        for (const auto& Child : Node.Children) CountRoles(Child);
    };
    CountRoles(Parsed.RootWidget);
    TestEqual(TEXT("All three source fill roles, including single fills, are explicit"), FillRoles, 3);
    TestEqual(TEXT("Transferred object fill leaves no shader on original content"), UnownedFillShaders, 0);
    if (!TestTrue(TEXT("Real normalization folds default object fills"),
        FPreBuildResourceNormalizer::NormalizeGeneratedTextures(Parsed.RootWidget, Folder, TEXT("Fill"), Error)))
    { AddError(Error); return false; }
    int32 Captures = 0, Ordinary = 0, Kept = 0, EmptyKept = 0, ObjectOpacity = 0;
    const FUMGWidgetNode* LayeredCapture = nullptr;
    const FUMGWidgetNode* EmptyCapture = nullptr;
    TFunction<void(const FUMGWidgetNode&)> Inspect = [&](const FUMGWidgetNode& Node)
    {
        if (Node.Id == TEXT("kept")) ++Kept;
        if (Node.Id == TEXT("emptyKept")) ++EmptyKept;
        if (FMath::IsNearlyEqual(Node.Render.RenderOpacity.Get(1.0f), 0.6f)) ++ObjectOpacity;
        if (Node.Brush.Shader.IsSet()) ++Ordinary;
        if (Node.Effect.Shader.IsSet() && Node.Effect.Shader->bFillPaint)
        {
            ++Captures;
            if (Node.Effect.Shader->Size.X == 32) LayeredCapture = &Node;
            else EmptyCapture = &Node;
            auto* Material = LoadObject<UMaterial>(nullptr, *Node.Effect.EffectMaterial.Get(FString()));
            if (Material)
            {
                Materials.AddUnique(Material);
                const auto* Alpha = Cast<UMaterialExpressionComponentMask>(Material->GetExpressionInputForProperty(MP_Opacity)->Expression);
                const auto* Composite = Alpha ? Cast<UMaterialExpressionCustom>(Alpha->Input.Expression) : nullptr;
                const auto* Prefix = Composite ? Composite->Inputs.FindByPredicate(
                    [](const FCustomInput& Input) { return Input.InputName == TEXT("Prefix"); }) : nullptr;
                const auto* Raw = Prefix ? Cast<UMaterialExpressionTextureObjectParameter>(Prefix->Input.Expression) : nullptr;
                TestTrue(TEXT("Fill alpha-over reads raw prefix, never its low LOD"),
                    Raw && Raw->ParameterName == TEXT("D2UObject") && Composite->Code.Contains(TEXT("Prefix.SampleLevel(PrefixSampler,UV,0)")));
                TestTrue(TEXT("Only shader contribution receives coverage"), Composite &&
                    Composite->Code.Contains(TEXT("ShaderColor*(FillMask.SampleLevel")) &&
                    Composite->Code.Contains(TEXT("fill+Prefix.SampleLevel")));
            }
            else AddError(TEXT("Fill capture material missing"));
        }
        if (Node.Brush.MaterialRef.IsSet())
            if (auto* Material = LoadObject<UMaterial>(nullptr, *Node.Brush.MaterialRef.GetValue())) Materials.AddUnique(Material);
        for (const auto& Child : Node.Children) Inspect(Child);
    };
    Inspect(Parsed.RootWidget);
    TestEqual(TEXT("Only the two real object samplers create captures"), Captures, 2);
    TestEqual(TEXT("Explicit image override remains a normal shader image"), Ordinary, 1);
    TestEqual(TEXT("Source child remains once"), Kept, 1);
    TestEqual(TEXT("Single-fill future child remains outside the empty prefix"), EmptyKept, 1);
    TestEqual(TEXT("Object opacity has one outer owner"), ObjectOpacity, 1);
    if (!TestNotNull(TEXT("Layered fill capture exists"), LayeredCapture) ||
        !TestNotNull(TEXT("Empty fill capture exists"), EmptyCapture)) return false;
    const auto PrefixContent = [](const FUMGWidgetNode& Capture) -> const FUMGWidgetNode&
    {
        const FUMGWidgetNode& Content = Capture.Children[0];
        return Content.Type == TEXT("Border") ? Content.Children[0] : Content;
    };
    TestEqual(TEXT("Empty prefix is transparent with no future content"), PrefixContent(*EmptyCapture).Children.Num(), 0);
    TestEqual(TEXT("Layered prefix contains shadow segment and preceding fill only"), PrefixContent(*LayeredCapture).Children.Num(), 2);
    TestTrue(TEXT("Shadow paint has capture outsets"), LayeredCapture->Effect.Shader->CaptureInsets.X > 0);
    TestTrue(TEXT("Mask texture remains bound to fill capture"), LayeredCapture->Brush.Image.TexturePath.IsSet());
    FUMGWidgetData Reloaded;
    if (!TestTrue(TEXT("Folded role and mask survive JSON"), FUMGJsonParser::ParseJson(
        FUMGJsonSerializer::Serialize(Parsed), Reloaded, &Error))) return false;
    if (!TestTrue(TEXT("Folded material preparation is repeatable"),
        FPreBuildResourceNormalizer::NormalizeGeneratedTextures(Reloaded.RootWidget, Folder, TEXT("Fill"), Error)))
    { AddError(Error); return false; }
    TStrongObjectPtr<UWidgetBlueprint> Blueprint(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), GetTransientPackage(), *FString(TEXT("FillObject_") + Id), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
    if (!TestNotNull(TEXT("Native fill blueprint creates"), Blueprint.Get())) return false;
    Blueprint->ClearFlags(RF_Standalone);
    const FIntPoint DesignSize(32, 24);
    Blueprint->WidgetTree->RootWidget = FUMGWidgetTreeBuilder::CreateWidgetRecursive(
        Blueprint->WidgetTree, Reloaded.RootWidget, nullptr, nullptr, &DesignSize);
    UButton* Content = nullptr;
    Blueprint->WidgetTree->ForEachWidget([&](UWidget* Widget) { if (auto* Button = Cast<UButton>(Widget)) Content = Button; });
    if (!TestNotNull(TEXT("Real native interactive content is present"), Content)) return false;
    Content->SetIsEnabled(false);
    Content->SetRenderOpacity(0.35f);
    if (!TestTrue(TEXT("Reuse native fill OnPaint/SDF/mip lifecycle"),
        PencilShaderRuntimeBindings::Prepare(Blueprint.Get(), Error))) { AddError(Error); return false; }
    int32 SameContent = 0;
    Blueprint->WidgetTree->ForEachWidget([&](UWidget* Widget) { if (Widget == Content) ++SameContent; });
    TestEqual(TEXT("Native content instance is retained once"), SameContent, 1);
    TestFalse(TEXT("Content interaction state is preserved"), Content->GetIsEnabled());
    TestEqual(TEXT("Content local opacity is preserved"), Content->GetRenderOpacity(), 0.35f);
    return QueueGeneratedFillPrefixFrames(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilShaderRetainerPrefixFramesTest,
    "Design2UMG.Resources.PencilShaderRetainerPrefixFrames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilShaderRetainerPrefixFramesTest::RunTest(const FString&)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("Native prefix trial has a real editor world"), World))
        return false;
    const auto* Defer = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.DeferRetainedRenderingRenderThread"));
    AddInfo(FString::Printf(TEXT("Retainer native defer cvar=%d; trial does not change it"), Defer ? Defer->GetInt() : -1));
    if (!TestTrue(TEXT("This trial exercises the immediate native Retainer route"), Defer && Defer->GetInt() == 0))
        return false;
    TSharedRef<FRetainerPrefixFrames> Frames = MakeShared<FRetainerPrefixFrames>(*this);
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Frames->EvidenceDirectory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/RetainerPrefixFrames") / Id;
    if (!TestTrue(TEXT("Create prefix evidence directory"), IFileManager::Get().MakeDirectory(*Frames->EvidenceDirectory, true)))
        return false;
    const FString Source = Frames->EvidenceDirectory / TEXT("prefix.glsl");
    // Existing production preparation supplies the native OnPaint copy graph.
    if (!TestTrue(TEXT("Write the object-mip binding fixture"), FFileHelper::SaveStringToFile(TEXT(
        "precision mediump float;\n/** @mipmap true */ uniform sampler2D MipObject;\n"
        "uniform sampler2D RawObject;\nvoid main(){gl_FragColor=0.5*"
        "(texture2D(MipObject,vec2(0.5))+texture2D(RawObject,vec2(0.5)));}"),
        *Source, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
        return false;
    Frames->AssetFolder = TEXT("/Game/Design2UMG/Automation/PrefixFrames_") + Id;
    FUMGWidgetNode Node;
    Node.Type = TEXT("RetainerBox");
    Node.Bounds.W = Node.Bounds.H = 16;
    FDesignShaderData Shader;
    Shader.Url = Source;
    Shader.Size = FVector2D(16);
    FDesignShaderValue Object;
    Object.Kind = FDesignShaderValue::EKind::String;
    Object.String = TEXT("pencil:object");
    Shader.Uniforms.Add(TEXT("MipObject"), Object);
    Shader.Uniforms.Add(TEXT("RawObject"), Object);
    Node.Effect.Shader = Shader;
    FString Error;
    if (!TestTrue(TEXT("Prepare existing object-mip materials"), FPreBuildResourceNormalizer::NormalizeGeneratedTextures(
        Node, Frames->AssetFolder, TEXT("PrefixFrames"), Error)))
    {
        AddError(Error);
        return false;
    }
    auto* Effect = LoadObject<UMaterialInterface>(nullptr, *Node.Effect.EffectMaterial.Get(FString()));
    Frames->Blueprint.Reset(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), GetTransientPackage(), *FString(TEXT("PrefixFrames_") + Id), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
    if (!TestNotNull(TEXT("Native prefix blueprint"), Frames->Blueprint.Get()) || !TestNotNull(TEXT("Prefix effect"), Effect))
        return false;
    UWidgetTree* Tree = Frames->Blueprint->WidgetTree;
    auto* Canvas = Tree->ConstructWidget<UCanvasPanel>();
    Tree->RootWidget = Canvas;
    auto* Capture = Tree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass(), TEXT("PrefixCapture"));
    Capture->SetEffectMaterial(Effect);
    Capture->SetTextureParameter(TEXT("D2UObject"));
    Capture->SetRenderingPhase(0, 1);
    Capture->SetRetainRendering(true);
    Capture->SetClipping(EWidgetClipping::ClipToBounds);
    Canvas->AddChildToCanvas(Capture)->SetSize(FVector2D(16));
    auto* Prefix = Tree->ConstructWidget<UCanvasPanel>();
    Capture->SetContent(Prefix);
    auto AddBlock = [&](UCanvasPanel* Parent, FName Name, FVector2D Position, FVector2D Size, FLinearColor Color) {
        auto* Block = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
        Block->SetPadding(FMargin(0));
        Block->SetBrushColor(Color);
        auto* Slot = Parent->AddChildToCanvas(Block);
        Slot->SetPosition(Position);
        Slot->SetSize(Size);
        return Block;
    };
    AddBlock(Prefix, TEXT("FrameMarker"), FVector2D::ZeroVector, FVector2D(8, 16), FLinearColor::Red)->SetRenderOpacity(0.5f);
    AddBlock(Prefix, TEXT("ClippedOverflow"), FVector2D(16, 0), FVector2D(4), FLinearColor::Blue);
    AddBlock(Canvas, TEXT("LaterForeground"), FVector2D(12, 0), FVector2D(4), FLinearColor::Blue);
    if (!TestTrue(TEXT("Reuse native generated OnPaint/mip lifecycle"), PencilShaderRuntimeBindings::Prepare(Frames->Blueprint.Get(), Error)))
    {
        AddError(Error);
        return false;
    }
    // A test-only native material selects a real high mip; the production binding names stay unchanged.
    TStrongObjectPtr<UMaterial> Consumer(NewObject<UMaterial>(GetTransientPackage()));
    Consumer->MaterialDomain = MD_UI;
    Consumer->BlendMode = BLEND_Translucent;
    auto* Texture = NewObject<UMaterialExpressionTextureObjectParameter>(Consumer.Get());
    Texture->ParameterName = TEXT("D2UObjectMip");
    Texture->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    Texture->SamplerType = SAMPLERTYPE_Color;
    auto* RawTexture = NewObject<UMaterialExpressionTextureObjectParameter>(Consumer.Get());
    RawTexture->ParameterName = TEXT("D2UObject");
    RawTexture->Texture = Texture->Texture;
    RawTexture->SamplerType = SAMPLERTYPE_Color;
    auto* Sample = NewObject<UMaterialExpressionCustom>(Consumer.Get());
    Sample->OutputType = CMOT_Float4;
    Sample->Code = TEXT("float4 p=Texture2DSampleLevel(Source,SourceSampler,float2(0.25,0.25),4);"
        "float rawBlue=Texture2DSampleLevel(RawSource,RawSourceSampler,float2(0.25,0.25),0).b;"
        "return float4(p.rg,0.75+rawBlue,p.a);");
    FCustomInput Input;
    Input.InputName = TEXT("Source");
    Input.Input.Expression = Texture;
    Sample->Inputs.Add(Input);
    FCustomInput RawInput;
    RawInput.InputName = TEXT("RawSource");
    RawInput.Input.Expression = RawTexture;
    Sample->Inputs.Add(RawInput);
    auto* RGB = NewObject<UMaterialExpressionComponentMask>(Consumer.Get());
    RGB->Input.Expression = Sample;
    RGB->R = RGB->G = RGB->B = true;
    RGB->A = false;
    auto* Alpha = NewObject<UMaterialExpressionComponentMask>(Consumer.Get());
    Alpha->Input.Expression = Sample;
    Alpha->R = Alpha->G = Alpha->B = false;
    Alpha->A = true;
    const TArray<UMaterialExpression*> Expressions{Texture, RawTexture, Sample, RGB, Alpha};
    for (UMaterialExpression* Expression : Expressions)
        Design2UMGMaterialAccess::AddExpression(*Consumer, *Expression);
    Consumer->GetExpressionInputForProperty(MP_EmissiveColor)->Expression = RGB;
    Consumer->GetExpressionInputForProperty(MP_Opacity)->Expression = Alpha;
    Consumer->PostEditChange();
    Design2UMGMaterialAccess::WaitForCompilation(*Consumer);
    Tree->ForEachWidget([&](UWidget* Widget) {
        if (auto* Image = Cast<UImage>(Widget))
            Image->SetBrushFromMaterial(Consumer.Get());
    });
    return QueuePrefixFrames(*this, Frames, World);
}
#endif
