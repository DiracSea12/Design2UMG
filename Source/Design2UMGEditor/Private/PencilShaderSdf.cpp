// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "PencilShaderSdf.h"
#include "Design2UMGMaterialAccess.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "MaterialShared.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "RHI.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
// Pencil 0.6.77: 5 fractional coordinate bits, positive interior distance,
// separable sigma=3 Gaussian before the Scharr gradient. Frame=(width,height,1/sdfScale,0).
const TCHAR* InitCode = TEXT(R"(
float2 p = UV * Frame.xy;
float a = Texture2DSampleLevel(Alpha, AlphaSampler, UV, 0).a;
float4 aa;
float2 offsets[4] = {float2(1,0),float2(0,1),float2(-1,0),float2(0,-1)};
[unroll] for(int k=0;k<4;k++) {
    float2 q = UV + offsets[k]/Frame.xy;
    aa[k] = all(q>=0)&&all(q<1) ? Texture2DSampleLevel(Alpha,AlphaSampler,saturate(q),0).a : 0;
}
float4 edge = float4((a-.5)*(aa.x-.5)<0,(a-.5)*(aa.y-.5)<0,(a-.5)*(aa.z-.5)<0,(a-.5)*(aa.w-.5)<0);
float count = dot(edge,edge);
if(count==0) return float4(1,1,1,1);
float4 x = (.5-a)/(aa-a+(1-edge))*(edge/count);
int2 fixedPoint = (int2)((p+x.xy-x.zw)*32.0+.5);
return float4((fixedPoint.x>>8)&255,fixedPoint.x&255,(fixedPoint.y>>8)&255,fixedPoint.y&255)/255.0;
)");
const TCHAR* JumpCode = TEXT(R"(
float2 p=UV*Frame.xy;
float4 closest=1; float best=1e20;
for(int i=-1;i<=1;i++) for(int j=-1;j<=1;j++) {
    float2 q=clamp((p+float2(i,j)*Jump)/Frame.xy,.5/Frame.xy,(Frame.xy-.5)/Frame.xy);
    float4 c=Input.Load(int3(int2(q*Frame.xy),0));
    uint2 hi=(uint2)(c.xz*255.0+.5),lo=(uint2)(c.yw*255.0+.5);
    float2 d=(float2)((hi<<8)|lo)/32.0-p;
    float distance=dot(d,d);
    if(distance<best){best=distance;closest=c;}
}
return closest;
)");
const TCHAR* DistanceCode = TEXT(R"(
float4 c=Input.Load(int3(clamp(int2(UV*Frame.xy),0,int2(Frame.xy)-1),0));
uint2 hi=(uint2)(c.xz*255.0+.5),lo=(uint2)(c.yw*255.0+.5);
float2 closest=(float2)((hi<<8)|lo)/32.0;
float sign=Texture2DSampleLevel(Alpha,AlphaSampler,UV,0).a>=.5 ? 1 : -1;
return float4(sign*length(closest-UV*Frame.xy)*Frame.z,0,0,1);
)");
const TCHAR* BlurCode = TEXT(R"(
float distance=0,weight=0;
// The separable sigma=3 Gaussian uses its +/-3 sigma support, with Clamp edges.
for(int i=-9;i<=9;i++) {
    float w=exp(-float(i*i)/18.0);
    float2 q=clamp(UV+Axis.xy*float(i)/Frame.xy,.5/Frame.xy,(Frame.xy-.5)/Frame.xy);
    distance+=w*Input.Load(int3(int2(q*Frame.xy),0)).r;weight+=w;
}
return float4(distance/weight,0,0,1);
)");
const TCHAR* GradientCode = TEXT(R"(
float d[9];int k=0;
for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++) {
    float2 q=clamp(UV+float2(x,y)/Frame.xy,.5/Frame.xy,(Frame.xy-.5)/Frame.xy);
    d[k++]=Input.Load(int3(int2(q*Frame.xy),0)).r;
}
float gx=(10.0/32.0)*(d[5]-d[3])+(3.0/32.0)*(d[2]-d[0]+d[8]-d[6]);
float gy=(10.0/32.0)*(d[1]-d[7])+(3.0/32.0)*(d[0]-d[6]+d[2]-d[8]);
return float4(d[4],gx,gy,1);
)");

template <class T> T* CreateSdfExpression(UMaterial* Material)
{
    T* Result = NewObject<T>(Material);
    Design2UMGMaterialAccess::AddExpression(*Material, *Result);
    return Result;
}

bool SaveAsset(UObject* Asset, FString& Error)
{
    UPackage* Package = Asset->GetOutermost();
    const FString File =
        FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    if (UPackage::SavePackage(Package, Asset, *File, Args))
    {
        FAssetRegistryModule::AssetCreated(Asset);
        return true;
    }
    Error = TEXT("Pencil SDF asset save failed: ") + File;
    return false;
}

bool Compile(UMaterial* Material, const TCHAR* Origin, FString& Error)
{
    struct FCompileState
    {
        bool bResource = false;
        bool bShaderMap = false;
        bool bCaching = false;
        TArray<FString> Errors;
    };
    const auto GetResource = [Material]() -> const FMaterialResource*
    {
        return Material->GetMaterialResource(
#if UE_VERSION_OLDER_THAN(5, 7, 0)
            GMaxRHIFeatureLevel
#else
            GMaxRHIShaderPlatform
#endif
        );
    };
    const auto CaptureState = [](const FMaterialResource* Resource)
    {
        FCompileState State;
        State.bResource = Resource != nullptr;
        if (Resource)
        {
            State.bShaderMap = Resource->GetGameThreadShaderMap() != nullptr;
            State.bCaching = Resource->IsCachingShaders();
            State.Errors = Resource->GetCompileErrors();
        }
        return State;
    };
    const auto IsReady = [](const FMaterialResource* Resource)
    {
        return Resource && Resource->GetCompileErrors().IsEmpty() && Resource->GetGameThreadShaderMap() &&
            Resource->GetGameThreadShaderMap()->IsValidForRendering();
    };
    // Re-entering CacheShaders can replace an existing map during on-demand compilation.
    Design2UMGMaterialAccess::WaitForCompilation(*Material);
    if (IsReady(GetResource()))
        return true;

    const FCompileState BeforeCache = CaptureState(GetResource());
    Design2UMGMaterialAccess::CacheShadersForRendering(*Material);
    const FCompileState AfterCache = CaptureState(GetResource());
    Design2UMGMaterialAccess::WaitForCompilation(*Material);
    const FMaterialResource* Resource = GetResource();
    if (IsReady(Resource))
    {
        return true;
    }
    const FCompileState AfterEnsure = CaptureState(Resource);
    const auto DescribeState = [](const FCompileState& State)
    {
        const FString Errors = FString::Join(State.Errors, TEXT("; "));
        return FString::Printf(TEXT("resource=%d shaderMap=%d caching=%d compileErrors=%s"),
            State.bResource, State.bShaderMap, State.bCaching, Errors.IsEmpty() ? TEXT("<none>") : *Errors);
    };
    Error = FString::Printf(
        TEXT("Pencil SDF material has no renderable shader map: material=%s platform=%d origin=%s ")
        TEXT("beforeCache={%s} afterCache={%s} afterEnsure={%s}"),
        *Material->GetPathName(), static_cast<int32>(GMaxRHIShaderPlatform), Origin,
        *DescribeState(BeforeCache), *DescribeState(AfterCache), *DescribeState(AfterEnsure));
    return false;
}

UMaterial* Build(const TCHAR* Code, UTexture* Placeholder, FString& Error)
{
    const FString Name = TEXT("D2U_PencilSdf_") + FMD5::HashAnsiString(Code);
    const FString PackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/") + Name;
    if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *(PackageName + TEXT(".") + Name)))
    {
        return Compile(Existing, TEXT("loaded"), Error) ? Existing : nullptr;
    }
    UPackage* Package = CreatePackage(*PackageName);
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone));
    UMaterial* Result = Material.Get();
    Result->MaterialDomain = MD_Surface;
    Result->BlendMode = BLEND_AlphaComposite;
    Result->SetShadingModel(MSM_Unlit);
    Result->bAllowNegativeEmissiveColor = true;
    auto* Program = CreateSdfExpression<UMaterialExpressionCustom>(Result);
    Program->Code = Code;
    Program->OutputType = CMOT_Float4;
    const auto Input = [&](FName InputName, UMaterialExpression* Value, int32 OutputIndex = 0)
    {
        FCustomInput Item;
        Item.InputName = InputName;
        Item.Input.Expression = Value;
        Item.Input.OutputIndex = OutputIndex;
        Program->Inputs.Add(Item);
    };
    Input(TEXT("UV"), CreateSdfExpression<UMaterialExpressionTextureCoordinate>(Result));
    for (FName Parameter : {FName(TEXT("Input")), FName(TEXT("Alpha"))})
    {
        auto* Texture = CreateSdfExpression<UMaterialExpressionTextureObjectParameter>(Result);
        Texture->ParameterName = Parameter;
        Texture->Texture = Placeholder;
        Texture->SamplerType = SAMPLERTYPE_Color;
        Input(Parameter, Texture);
    }
    for (FName Parameter : {FName(TEXT("Frame")), FName(TEXT("Axis"))})
    {
        auto* Vector = CreateSdfExpression<UMaterialExpressionVectorParameter>(Result);
        Vector->ParameterName = Parameter;
        Vector->DefaultValue = Parameter == TEXT("Frame") ? FLinearColor(1, 1, 1, 0) : FLinearColor::Transparent;
        Input(Parameter, Vector, 5);
    }
    auto* Jump = CreateSdfExpression<UMaterialExpressionScalarParameter>(Result);
    Jump->ParameterName = TEXT("Jump");
    Jump->DefaultValue = 1;
    Input(TEXT("Jump"), Jump);
    auto* RGB = CreateSdfExpression<UMaterialExpressionComponentMask>(Result);
    RGB->Input.Expression = Program;
    RGB->R = RGB->G = RGB->B = true;
    auto* Alpha = CreateSdfExpression<UMaterialExpressionComponentMask>(Result);
    Alpha->Input.Expression = Program;
    Alpha->A = true;
    auto* Opacity = CreateSdfExpression<UMaterialExpressionOneMinus>(Result);
    Opacity->Input.Expression = Alpha;
    Result->GetExpressionInputForProperty(MP_EmissiveColor)->Expression = RGB;
    Result->GetExpressionInputForProperty(MP_Opacity)->Expression = Opacity;
    Result->PreEditChange(nullptr);
    Result->PostEditChange();
    if (Compile(Result, TEXT("created"), Error) && SaveAsset(Result, Error))
    {
        return Result;
    }
    Result->ClearFlags(RF_Public | RF_Standalone);
    Result->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
    Result->MarkAsGarbage();
    Package->SetDirtyFlag(false);
    return nullptr;
}
} // namespace

UTexture* PencilShaderSdf::SamplerDefault(FString& Error)
{
    const TCHAR* PackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/D2U_PencilSdfSampler");
    const TCHAR* ObjectPath = TEXT("/Game/Design2UMG/GeneratedMaterials/D2U_PencilSdfSampler.D2U_PencilSdfSampler");
    if (auto* Existing = LoadObject<UTextureRenderTarget2D>(nullptr, ObjectPath))
    {
        return Existing;
    }
    UPackage* Package = CreatePackage(PackageName);
    TStrongObjectPtr<UTextureRenderTarget2D> Texture(
        NewObject<UTextureRenderTarget2D>(Package, TEXT("D2U_PencilSdfSampler"), RF_Public | RF_Standalone));
    Texture->RenderTargetFormat = RTF_RGBA16f;
    Texture->ClearColor = FLinearColor(0, 0, 0, 1);
    Texture->InitAutoFormat(1, 1);
    if (SaveAsset(Texture.Get(), Error))
    {
        return Texture.Get();
    }
    Texture->ClearFlags(RF_Public | RF_Standalone);
    Texture->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
    Texture->MarkAsGarbage();
    Package->SetDirtyFlag(false);
    return nullptr;
}

bool PencilShaderSdf::BuildMaterials(FPassMaterials& Out, FString& Error)
{
    UTexture* Placeholder = SamplerDefault(Error);
    if (!Placeholder)
    {
        return false;
    }
    Out.Init = Build(InitCode, Placeholder, Error);
    if (!Out.Init)
        return false;
    Out.Jump = Build(JumpCode, Placeholder, Error);
    if (!Out.Jump)
        return false;
    Out.Distance = Build(DistanceCode, Placeholder, Error);
    if (!Out.Distance)
        return false;
    Out.Blur = Build(BlurCode, Placeholder, Error);
    if (!Out.Blur)
        return false;
    Out.Gradient = Build(GradientCode, Placeholder, Error);
    return Out.Gradient != nullptr;
}
