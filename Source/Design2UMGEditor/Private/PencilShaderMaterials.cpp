// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "PencilShaderMaterials.h"
#include "PencilShaderCompiler.h"
#include "PencilShaderTextures.h"
#include "PencilShaderSdf.h"
#include "Design2UMGMaterialAccess.h"
#include "DesignResourceResolver.h"
#include "UMGSchema.h"
#include "UMGRetainerCapture.h"
#include "UMGWidgetTypes.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureProperty.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "MaterialShared.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/EngineVersionComparison.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include <cctype>
namespace
{
// VectorParameter output zero is RGB; its separate RGBA output preserves the fourth component.
constexpr int32 VectorParameterRgbaOutput=5;
// Slate encodes UI material RGB after coverage, before gamma-space blending.
// Invert that transfer on the complete premultiplied fill result; Retainer effects use NoGamma.
// Slate applies inherited vertex opacity only to alpha, so account for it once in RGB here.
constexpr TCHAR FillColorTransferCode[]=TEXT(
    "float3 CoveredRGB=EncodedRGB*VertexOpacity;\n"
    "float3 Low=CoveredRGB/12.92;\n"
    "float3 High=pow((abs(CoveredRGB)+0.055)/1.055,2.4);\n"
    "return float3(CoveredRGB.r<=0.04045?Low.r:High.r,"
    "CoveredRGB.g<=0.04045?Low.g:High.g,CoveredRGB.b<=0.04045?Low.b:High.b);\n");
constexpr TCHAR FillCompositeCode[] = TEXT(
                "float2 maskUV=(UV*DrawRect.zw+DrawRect.xy)/Resolution;\n"
                "float coverage=float(all(maskUV>=0)&&all(maskUV<=1));\n"
                "float4 fill=ShaderColor*(FillMask.SampleLevel(FillMaskSampler,saturate(maskUV),0).a*coverage);\n"
                "return fill+Prefix.SampleLevel(PrefixSampler,UV,0)*(1-fill.a)*FillRole.x*(1-CompositeSource);\n");

template<class T>T* Expression(UMaterial* M)
{T* E=NewObject<T>(M);Design2UMGMaterialAccess::AddExpression(*M,*E);return E;}
UMaterialExpressionComponentMask* Mask(UMaterial* M,UMaterialExpression* Input,int32 Count,bool Alpha=false)
{
    auto E=Expression<UMaterialExpressionComponentMask>(M);E->Input.Expression=Input;
    E->R=!Alpha;E->G=!Alpha&&Count>=2;E->B=!Alpha&&Count>=3;E->A=Alpha||Count==4;return E;
}
void Input(UMaterialExpressionCustom* Custom,const FString& Name,UMaterialExpression* E,int32 OutputIndex=0)
{FCustomInput I;I.InputName=FName(*Name);I.Input.Expression=E;I.Input.OutputIndex=OutputIndex;Custom->Inputs.Add(I);}
bool Numbers(const FDesignShaderData& Shader,const PencilShaderSyntax::FUniform& U,TArray<float>& Out,FString& Error)
{
    if(const auto* Value=Shader.Uniforms.Find(UTF8_TO_TCHAR(U.Name.c_str())))
    {
        if(Value->Kind==FDesignShaderValue::EKind::Numbers)
        {for(double N:Value->Numbers)Out.Add(static_cast<float>(N));return true;}
        if(Value->Kind==FDesignShaderValue::EKind::Boolean){Out.Add(Value->Boolean?1.f:0.f);return true;}
    }
    FString Text=PencilShaderSyntax::ResolveStringUniform(Shader,U);
    if(Text.IsEmpty())return true;
    if(Text.StartsWith(TEXT("#")))
    {
        const FColor C=FColor::FromHex(Text);Out={C.R/255.f,C.G/255.f,C.B/255.f,C.A/255.f};return true;
    }
    if(Text==TEXT("true")||Text==TEXT("false")){Out.Add(Text==TEXT("true")?1.f:0.f);return true;}
    for(const TCHAR C:{TCHAR(','),TCHAR('['),TCHAR(']'),TCHAR('('),TCHAR(')')})Text.ReplaceCharInline(C,TCHAR(' '));
    TArray<FString> Parts;Text.ParseIntoArrayWS(Parts);
    for(const auto& Part:Parts)
    {float N;if(!FDefaultValueHelper::ParseFloat(Part,N)||!FMath::IsFinite(N)){Error=FString(TEXT("Invalid shader uniform value: "))+UTF8_TO_TCHAR(U.Name.c_str());return false;}Out.Add(N);}
    return true;
}
bool EnsureCompiled(UMaterial* M,FString& Error)
{
    const auto GetResource=[M]() { return M->GetMaterialResource(
#if UE_VERSION_OLDER_THAN(5,7,0)
        GMaxRHIFeatureLevel
#else
        GMaxRHIShaderPlatform
#endif
    ); };
    const auto IsReady=[](const FMaterialResource* Resource) {
        return Resource&&Resource->GetCompileErrors().IsEmpty()&&Resource->GetGameThreadShaderMap()&&Resource->GetGameThreadShaderMap()->IsValidForRendering();
    };
    // Re-entering CacheShaders can replace a reused material's already renderable map.
    Design2UMGMaterialAccess::WaitForCompilation(*M);
    if(IsReady(GetResource()))return true;
    Design2UMGMaterialAccess::CacheShadersForRendering(*M);Design2UMGMaterialAccess::WaitForCompilation(*M);
    const auto* Resource=GetResource();
    if(IsReady(Resource))return true;
    Error=FString::Printf(TEXT("Shader material has no renderable shader map: %s; %s"),*M->GetPathName(),
        Resource&&!Resource->GetCompileErrors().IsEmpty()?*FString::Join(Resource->GetCompileErrors(),TEXT("; ")):TEXT("no compile errors"));return false;
}
UMaterial *Build(const FDesignShaderData &Shader, bool bEffect, const FString &MaskPath, FString &Error,
                 DesignImageResources::FImageOperationBudget &Budget, bool* OutFillObject = nullptr,
                 bool* OutContext = nullptr, bool ForceContext = false)
{
    if (OutFillObject) *OutFillObject = false;
    int32 BlendMode = 0;
    if (!PencilShaderMaterials::BlendModeIndex(Shader.BlendMode, BlendMode, Error)) return nullptr;
    FString SourceCode;
    if (!FFileHelper::LoadFileToString(SourceCode, *Shader.Url))
    {
        Error = TEXT("Shader source is missing: ") + Shader.Url;
        return nullptr;
    }
    PencilShaderSyntax::FProgram Program;
    if (!PencilShaderCompiler::Compile(SourceCode, Program, Error))
        return nullptr;
    FString Identity;
    auto Writer = TJsonWriterFactory<>::Create(&Identity);
    FJsonSerializer::Serialize(DesignShaderJson::Serialize(Shader), Writer);
    const auto NeedsBinding = [&](const PencilShaderSyntax::FUniform& Uniform)
    {
        if (!Uniform.MappedName.empty()) return true;
        for (const auto& Size : Program.Uniforms)
            if (Size.SizeOf == Uniform.Name && !Size.MappedName.empty()) return true;
        return false;
    };
    TMap<int32, FString> SamplerValues;
    TMap<int32, UTexture2D *> SamplerImages;
    UTexture *SdfSampler = nullptr;
    bool ObjectMip = false;
    bool ObjectSampling = false;
    bool BackgroundSampling = false;
    for (size_t UniformIndex = 0; UniformIndex < Program.Uniforms.size(); ++UniformIndex)
    {
        const auto &Uniform = Program.Uniforms[UniformIndex];
        if (!NeedsBinding(Uniform))
            continue;
        if (Uniform.Sdf)
        {
            if (!SdfSampler)
                SdfSampler = PencilShaderSdf::SamplerDefault(Error);
            if (!SdfSampler)
                return nullptr;
            continue;
        }
        if (Uniform.Type != "sampler2D")
            continue;
        const FString Value = PencilShaderSyntax::ResolveStringUniform(Shader, Uniform);
        SamplerValues.Add(int32(UniformIndex), Value);
        if (Value == TEXT("pencil:background"))
        {
            BackgroundSampling = true;
            continue;
        }
        if (Value == TEXT("pencil:object"))
        {
            ObjectSampling = true;
            ObjectMip |= Uniform.Mipmap;
            continue;
        }
        UTexture2D *Texture = PencilShaderTextures::SamplerImage(DesignResourceResolver::LoadTextureFromPath(Value),
                                                                 Uniform.Mipmap, Budget, Error);
        if (!Texture)
        {
            Error += TEXT(" (") + Value + TEXT(")");
            return nullptr;
        }
        SamplerImages.Add(int32(UniformIndex), Texture);
        Identity += Texture->GetPathName();
    }
    const bool FillObject = Shader.bFillPaint && ObjectSampling;
    const bool ContextSource = BackgroundSampling || BlendMode != 0 || ForceContext;
    if (OutContext) *OutContext = ContextSource;
    if (OutFillObject) *OutFillObject = FillObject && !ContextSource;
    Identity += UTF8_TO_TCHAR(Program.Code.c_str());
    if (SdfSampler)
        Identity += SdfSampler->GetPathName();
    Identity += MaskPath;
    Identity += FillObject ? TEXT("fill-over-object") : (bEffect ? TEXT("effect") : TEXT("fill"));
    if (FillObject) Identity += FillCompositeCode;
    Identity += ContextSource ? TEXT("|independent-background-affine|composite-source")
        : TEXT("|independent-background-affine|ordinary-source");
    UTexture* BackgroundDefault = BackgroundSampling ? PencilShaderSdf::SamplerDefault(Error) : nullptr;
    if (BackgroundSampling && !BackgroundDefault) return nullptr;
    UTexture* ObjectMipDefault = ObjectMip ? PencilShaderSdf::SamplerDefault(Error) : nullptr;
    if (ObjectMip && !ObjectMipDefault)
        return nullptr;
    const bool SlateImageOutput = (!bEffect && (!FillObject || ContextSource)) || ObjectMip;
    if (SlateImageOutput)
        Identity += FillColorTransferCode;
    const FString Name = TEXT("D2U_PencilShader_") + FMD5::HashAnsiString(*Identity);
    const FString PackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/") + Name;
    if (UMaterial *Existing = LoadObject<UMaterial>(nullptr, *(PackageName + TEXT(".") + Name)))
        return EnsureCompiled(Existing, Error) ? Existing : nullptr;
    UPackage *Package = CreatePackage(*PackageName);
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone));
    UMaterial *MaterialAsset = Material.Get();
    MaterialAsset->MaterialDomain = MD_UI;
    MaterialAsset->BlendMode = BLEND_AlphaComposite;
    MaterialAsset->SetShadingModel(MSM_Unlit);
    auto ShaderExpression = Expression<UMaterialExpressionCustom>(MaterialAsset);
    ShaderExpression->OutputType = CMOT_Float4;
    ShaderExpression->Code = UTF8_TO_TCHAR(Program.Code.c_str());
    TMap<FString, UMaterialExpressionTextureObjectParameter *> TextureParameters;
    UMaterialExpressionTextureObjectParameter *ObjectTextureParameter = nullptr;
    UMaterialExpressionTextureObjectParameter *SdfTextureParameter = nullptr;
    bool bValid = true;
    for (size_t UniformIndex = 0; UniformIndex < Program.Uniforms.size(); ++UniformIndex)
    {
        const auto &Uniform = Program.Uniforms[UniformIndex];
        if (Uniform.Type != "sampler2D" || !NeedsBinding(Uniform))
            continue;
        const FString Value = Uniform.Sdf ? FString() : SamplerValues.FindChecked(int32(UniformIndex));

        if (Value == TEXT("pencil:object") && !bEffect && !FillObject)
        {
            Error = TEXT("Shader fill object sampling is pending");
            bValid = false;
            break;
        }
        auto TextureParameter = Uniform.Sdf && SdfTextureParameter
                                    ? SdfTextureParameter
                                    : Expression<UMaterialExpressionTextureObjectParameter>(MaterialAsset);
        if (Uniform.Sdf)
        {
            TextureParameter->ParameterName = TEXT("D2USdf");
            TextureParameter->Texture = SdfSampler;
            TextureParameter->SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture(SdfSampler);
            SdfTextureParameter = TextureParameter;
        }
        else
        {
            TextureParameter->ParameterName = Value == TEXT("pencil:object")
                ? FName(Uniform.Mipmap ? TEXT("D2UObjectMip") : TEXT("D2UObject"))
                : Value == TEXT("pencil:background")
                    ? FName(Uniform.Mipmap ? TEXT("D2UBackgroundMip") : TEXT("D2UBackground"))
                    : FName(*FString::Printf(TEXT("Input%d"), int32(UniformIndex)));
            TextureParameter->Texture =
                Value == TEXT("pencil:object")
                    ? LoadObject<UTexture2D>(nullptr,
                                             TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"))
                    : Value == TEXT("pencil:background") ? BackgroundDefault : SamplerImages.FindChecked(int32(UniformIndex));
            if (Value == TEXT("pencil:object") && Uniform.Mipmap)
                TextureParameter->Texture = ObjectMipDefault;
            if (Value != TEXT("pencil:object") || Uniform.Mipmap)
                TextureParameter->SamplerType = SAMPLERTYPE_LinearColor;
        }
        if (!TextureParameter->Texture)
        {
            Error = TEXT("Shader sampler image is unresolved: ") + Value;
            bValid = false;
            break;
        }
        TextureParameters.Add(UTF8_TO_TCHAR(Uniform.Name.c_str()), TextureParameter);
        if (Value == TEXT("pencil:object"))
            ObjectTextureParameter = TextureParameter;
        Input(ShaderExpression, FString::Printf(TEXT("Input%d"), int32(UniformIndex)), TextureParameter);
        auto ObjectFlag = Expression<UMaterialExpressionConstant>(MaterialAsset);
        ObjectFlag->R = Value == TEXT("pencil:object") ? 1.f : 0.f;
        Input(ShaderExpression, FString::Printf(TEXT("Input%dIsObject"), int32(UniformIndex)), ObjectFlag);
        auto BackgroundFlag = Expression<UMaterialExpressionConstant>(MaterialAsset);
        BackgroundFlag->R = Value == TEXT("pencil:background") ? 1.0f : 0.0f;
        Input(ShaderExpression, FString::Printf(TEXT("Input%dIsBackground"), int32(UniformIndex)), BackgroundFlag);
    }
    if (bEffect && !ObjectTextureParameter)
    {
        ObjectTextureParameter = Expression<UMaterialExpressionTextureObjectParameter>(MaterialAsset);
        ObjectTextureParameter->ParameterName = TEXT("D2UObject");
        ObjectTextureParameter->Texture =
            LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    }
    bool bNeedsCoordinates = Program.UsesCoordinates || ObjectTextureParameter || SdfTextureParameter || BackgroundSampling;
    for (const auto &Uniform : Program.Uniforms)
        bNeedsCoordinates |= !Uniform.MappedName.empty() && (Uniform.Resolution || Uniform.Mouse);
    auto SizeParameter = Expression<UMaterialExpressionVectorParameter>(MaterialAsset);
    SizeParameter->ParameterName = bNeedsCoordinates ? TEXT("D2UResolution") : TEXT("Resolution");
    SizeParameter->DefaultValue = FLinearColor(Shader.Size.X, Shader.Size.Y, 0, 0);
    UMaterialExpression *ResolutionExpression = Mask(MaterialAsset, SizeParameter, 2);
    Input(ShaderExpression, TEXT("Resolution"), ResolutionExpression);
    auto DrawRectParameter = Expression<UMaterialExpressionVectorParameter>(MaterialAsset);
    DrawRectParameter->ParameterName = bNeedsCoordinates ? TEXT("D2UDrawRect") : TEXT("DrawRect");
    const FVector4 Insets = Shader.bFillPaint && !bEffect && !FillObject ? FVector4(0,0,0,0) : Shader.CaptureInsets;
    DrawRectParameter->DefaultValue = FLinearColor(-Insets.X, -Insets.Y,
        Shader.Size.X + Insets.X + Insets.Z, Shader.Size.Y + Insets.Y + Insets.W);
    Input(ShaderExpression, TEXT("DrawRect"), DrawRectParameter, VectorParameterRgbaOutput);
    Input(ShaderExpression, TEXT("UV"), Expression<UMaterialExpressionTextureCoordinate>(MaterialAsset));
    auto OpacityParameter = Expression<UMaterialExpressionScalarParameter>(MaterialAsset);
    OpacityParameter->ParameterName = TEXT("Opacity");
    OpacityParameter->DefaultValue = Shader.Opacity;
    Input(ShaderExpression, TEXT("Opacity"), OpacityParameter);
    auto* FillShape = Expression<UMaterialExpressionConstant>(MaterialAsset);
    FillShape->R = FillObject ? 1.0f : 0.0f;
    Input(ShaderExpression, TEXT("FillShape"), FillShape);
    if (Program.Code.find("BackgroundU") != std::string::npos)
    {
        auto U = Expression<UMaterialExpressionVectorParameter>(MaterialAsset);
        U->ParameterName = TEXT("D2UBackgroundU"); U->DefaultValue = FLinearColor(1,0,0,0);
        auto V = Expression<UMaterialExpressionVectorParameter>(MaterialAsset);
        V->ParameterName = TEXT("D2UBackgroundV"); V->DefaultValue = FLinearColor(0,1,0,0);
        Input(ShaderExpression, TEXT("BackgroundU"), U, VectorParameterRgbaOutput);
        Input(ShaderExpression, TEXT("BackgroundV"), V, VectorParameterRgbaOutput);
    }
    auto GeometryMaskParameter = Expression<UMaterialExpressionTextureObjectParameter>(MaterialAsset);
    GeometryMaskParameter->ParameterName = TEXT("GeometryMask");
    GeometryMaskParameter->Texture =
        MaskPath.IsEmpty()
            ? LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"))
            : DesignResourceResolver::LoadTextureFromPath(MaskPath);
    if (FillObject)
    {
        auto* Coverage = Expression<UMaterialExpressionTextureObjectParameter>(MaterialAsset);
        Coverage->ParameterName = TEXT("ShaderCoverage");
        Coverage->Texture = LoadObject<UTexture2D>(nullptr,
            TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
        Input(ShaderExpression, TEXT("Mask"), Coverage);
    }
    else
        Input(ShaderExpression, TEXT("Mask"), GeometryMaskParameter);
    for (size_t UniformIndex = 0; bValid && UniformIndex < Program.Uniforms.size(); ++UniformIndex)
    {
        const auto &Uniform = Program.Uniforms[UniformIndex];
        if (Uniform.Type == "sampler2D" || Uniform.MappedName.empty())
            continue;
        if ((Uniform.Time || Uniform.Resolution || Uniform.Mouse) &&
            Shader.Uniforms.Contains(UTF8_TO_TCHAR(Uniform.Name.c_str())))
        {
            Error = TEXT("Shader time/resolution/mouse uniforms must not be overridden");
            bValid = false;
            break;
        }
        UMaterialExpression *UniformExpression = nullptr;
        int32 OutputIndex = 0;
        if (Uniform.Resolution)
            UniformExpression = ResolutionExpression;
        else if (Uniform.Mouse)
        {
            auto Cursor = Expression<UMaterialExpressionVectorParameter>(MaterialAsset);
            Cursor->ParameterName = TEXT("D2UMouse");
            Cursor->DefaultValue = FLinearColor(Shader.Size.X / 2, Shader.Size.Y / 2, 0, 0);
            UniformExpression = Mask(MaterialAsset, Cursor, 2);
        }
        else if (Uniform.Time)
        {
            auto Time = Expression<UMaterialExpressionTime>(MaterialAsset);
            Time->bIgnorePause = true;
            UniformExpression = Time;
        }
        else if (!Uniform.SizeOf.empty())
        {
            auto *TextureParameter = TextureParameters.Find(UTF8_TO_TCHAR(Uniform.SizeOf.c_str()));
            if (!TextureParameter)
            {
                Error = TEXT("Shader textureSize sampler binding is missing");
                bValid = false;
                break;
            }
            if (Uniform.Type == "int")
            {
                auto Levels = Expression<UMaterialExpressionCustom>(MaterialAsset);
                Levels->OutputType = CMOT_Float1;
                Levels->Code = TEXT("uint width,height,levels; Texture.GetDimensions(0,width,height,levels); return levels;");
                Input(Levels, TEXT("Texture"), *TextureParameter);
                UniformExpression = Levels;
            }
            else
            {
                auto Size = Expression<UMaterialExpressionTextureProperty>(MaterialAsset);
                Size->TextureObject.Expression = *TextureParameter;
                UniformExpression = Size;
            }
        }
        else
        {
            const int32 Count = (!Uniform.Type.empty() && std::isdigit(static_cast<unsigned char>(Uniform.Type.back())))
                                    ? Uniform.Type.back() - '0'
                                    : 1;
            TArray<float> Values;
            if (!Numbers(Shader, Uniform, Values, Error))
            {
                bValid = false;
                break;
            }
            if (Values.Num() > 0 && Values.Num() != Count && !(Values.Num() == 4 && (Count == 3 || Count == 4)))
            {
                Error = TEXT("Shader uniform component count does not match: ") +
                        FString(UTF8_TO_TCHAR(Uniform.Name.c_str()));
                bValid = false;
                break;
            }
            while (Values.Num() < Count)
                Values.Add(0);
            const FName Parameter(*FString::Printf(TEXT("Input%d"), int32(UniformIndex)));
            if (Count == 1)
            {
                auto ValueParameter = Expression<UMaterialExpressionScalarParameter>(MaterialAsset);
                ValueParameter->ParameterName = Parameter;
                ValueParameter->DefaultValue = Values[0];
                UniformExpression = ValueParameter;
            }
            else
            {
                auto ValueParameter = Expression<UMaterialExpressionVectorParameter>(MaterialAsset);
                ValueParameter->ParameterName = Parameter;
                ValueParameter->DefaultValue =
                    FLinearColor(Values[0], Values[1], Count >= 3 ? Values[2] : 0, Count >= 4 ? Values[3] : 0);
                UniformExpression = Count == 4 ? static_cast<UMaterialExpression *>(ValueParameter)
                                               : Mask(MaterialAsset, ValueParameter, Count);
                if (Count == 4)
                    OutputIndex = VectorParameterRgbaOutput;
            }
        }
        Input(ShaderExpression, FString::Printf(TEXT("Input%d"), int32(UniformIndex)), UniformExpression, OutputIndex);
    }
    if (bValid)
    {
        UMaterialExpression* Result = ShaderExpression;
        if (FillObject)
        {
            auto* RawPrefix = Expression<UMaterialExpressionTextureObjectParameter>(MaterialAsset);
            RawPrefix->ParameterName = TEXT("D2UObject");
            RawPrefix->Texture = LoadObject<UTexture2D>(nullptr,
                TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
            RawPrefix->SamplerType = SAMPLERTYPE_LinearColor;
            auto* Role = Expression<UMaterialExpressionVectorParameter>(MaterialAsset);
            Role->ParameterName = TEXT("D2UFillObject");
            Role->DefaultValue = FLinearColor(1,0,0,0);
            auto* Composite = Expression<UMaterialExpressionCustom>(MaterialAsset);
            Composite->OutputType = CMOT_Float4;
            Composite->Code = FillCompositeCode;
            Input(Composite, TEXT("ShaderColor"), ShaderExpression);
            Input(Composite, TEXT("Prefix"), RawPrefix);
            Input(Composite, TEXT("FillMask"), GeometryMaskParameter);
            Input(Composite, TEXT("FillRole"), Role, VectorParameterRgbaOutput);
        auto SourceRole = Expression<UMaterialExpressionScalarParameter>(MaterialAsset);
        SourceRole->ParameterName = TEXT("D2UCompositeSource");
        SourceRole->DefaultValue = ContextSource ? 1 : 0;
        Input(Composite, TEXT("CompositeSource"), SourceRole);
            Input(Composite, TEXT("UV"), Expression<UMaterialExpressionTextureCoordinate>(MaterialAsset));
            Input(Composite, TEXT("DrawRect"), DrawRectParameter, VectorParameterRgbaOutput);
            Input(Composite, TEXT("Resolution"), ResolutionExpression);
            Result = Composite;
        }
        UMaterialExpression *OutputColor = Mask(MaterialAsset, Result, 3);
        if (SlateImageOutput)
        {
            auto FillTransfer = Expression<UMaterialExpressionCustom>(MaterialAsset);
            FillTransfer->OutputType = CMOT_Float3;
            FillTransfer->Code = FillColorTransferCode;
            Input(FillTransfer, TEXT("EncodedRGB"), OutputColor);
            Input(FillTransfer, TEXT("VertexOpacity"), Expression<UMaterialExpressionVertexColor>(MaterialAsset), 4);
            OutputColor = FillTransfer;
        }
        MaterialAsset->GetExpressionInputForProperty(MP_EmissiveColor)->Expression = OutputColor;
        MaterialAsset->GetExpressionInputForProperty(MP_Opacity)->Expression = Mask(MaterialAsset, Result, 1, true);
        MaterialAsset->PreEditChange(nullptr);
        MaterialAsset->PostEditChange();
        bValid = EnsureCompiled(MaterialAsset, Error);
    }

    if (bValid)
    {
        const FString File =
            FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        if (UPackage::SavePackage(Package, MaterialAsset, *File, Args))
        {
            FAssetRegistryModule::AssetCreated(MaterialAsset);
            return MaterialAsset;
        }
        Error = TEXT("Shader material save failed: ") + File;
    }
    MaterialAsset->ClearFlags(RF_Public | RF_Standalone);
    MaterialAsset->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
    MaterialAsset->MarkAsGarbage();
    Package->SetDirtyFlag(false);
    return nullptr;
}
}
namespace
{
UMaterial* CaptureMaterial(bool Copy, FString& Error)
{
    const FString CopyCode = TEXT("return Texture2DSampleLevel(Source, SourceSampler, UV, 0);");
    const FString Identity = Copy ? CopyCode + TEXT("|surface|inverse-canvas-alpha") : TEXT("transparent-ui-capture");
    const FString Name = TEXT("D2U_PencilCapture_") + FMD5::HashAnsiString(*Identity);
    const FString PackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/") + Name;
    if (auto* Existing = LoadObject<UMaterial>(nullptr, *(PackageName + TEXT(".") + Name)))
        return EnsureCompiled(Existing, Error) ? Existing : nullptr;
    UPackage* Package = CreatePackage(*PackageName);
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone));
    Material->MaterialDomain = Copy ? MD_Surface : MD_UI;
    Material->BlendMode = BLEND_AlphaComposite;
    Material->SetShadingModel(MSM_Unlit);
    if (Copy)
    {
        auto* Program = Expression<UMaterialExpressionCustom>(Material.Get());
        Program->Code = CopyCode;
        Program->OutputType = CMOT_Float4;
        auto* Source = Expression<UMaterialExpressionTextureObjectParameter>(Material.Get());
        Source->ParameterName = TEXT("Source");
        Source->Texture = LoadObject<UTexture2D>(nullptr,
            TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
        Source->SamplerType = SAMPLERTYPE_Color;
        Input(Program, TEXT("Source"), Source);
        Input(Program, TEXT("UV"), Expression<UMaterialExpressionTextureCoordinate>(Material.Get()));
        Material->GetExpressionInputForProperty(MP_EmissiveColor)->Expression = Mask(Material.Get(), Program, 3);
        // Canvas stores inverse opacity in its alpha channel; the cleared alpha is one.
        auto* Opacity = Expression<UMaterialExpressionOneMinus>(Material.Get());
        Opacity->Input.Expression = Mask(Material.Get(), Program, 1, true);
        Material->GetExpressionInputForProperty(MP_Opacity)->Expression = Opacity;
    }
    else
    {
        auto* Zero = Expression<UMaterialExpressionConstant>(Material.Get());
        Zero->R = 0;
        Material->GetExpressionInputForProperty(MP_EmissiveColor)->Expression = Zero;
        Material->GetExpressionInputForProperty(MP_Opacity)->Expression = Zero;
    }
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    if (EnsureCompiled(Material.Get(), Error))
    {
        const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        if (UPackage::SavePackage(Package, Material.Get(), *File, Args))
        {
            FAssetRegistryModule::AssetCreated(Material.Get());
            return Material.Get();
        }
        Error = TEXT("Object capture material save failed: ") + File;
    }
    Material->ClearFlags(RF_Public | RF_Standalone);
    Material->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
    Material->MarkAsGarbage();
    Package->SetDirtyFlag(false);
    return nullptr;
}

enum class EBackgroundPass { Composite, Consumer, Identity, Opacity };
UMaterial* BackgroundMaterial(EBackgroundPass Pass, int32 BlendMode, FString& Error)
{
    const bool Canvas = Pass == EBackgroundPass::Composite;
    const bool Consumer = Pass == EBackgroundPass::Consumer;
    FString Code = Canvas ? PencilShaderMaterials::BlendLibrary() + TEXT(R"D2U(float4 d=Texture2DSampleLevel(Destination,DestinationSampler,UV,0);
if(SourceOpacity==0)return d;
float2 q=float2(dot(float3(UV,1),SourceU.xyz),dot(float3(UV,1),SourceV.xyz));
if(!all(q>=0)||!all(q<=1))return d;
float4 s=Texture2DSampleLevel(Source,SourceSampler,q,0)*SourceOpacity;
FD2UBlend blend;return blend.Mix(s,d,Mode);)D2U"): Consumer ? TEXT(R"D2U(float2 p=float2(dot(float3(UV,1),CompositionU.xyz),dot(float3(UV,1),CompositionV.xyz));
float2 q=float2(dot(float3(p,1),SourceU.xyz),dot(float3(p,1),SourceV.xyz));
if(!all(p>=0)||!all(p<=1)||!all(q>=0)||!all(q<=1))return float4(0,0,0,0);
float4 s=Texture2DSampleLevel(Source,SourceSampler,q,0);
float4 d=Texture2DSampleLevel(Destination,DestinationSampler,p,0);
float4 c=Texture2DSampleLevel(Composition,CompositionSampler,p,0);
return float4(c.rgb-d.rgb*(1-s.a),s.a);)D2U")
        : TEXT("return Texture2DSampleLevel(Source,SourceSampler,UV,0)*GroupOpacity;");
    const FString Identity = Code + FillColorTransferCode + FString::Printf(TEXT("|background-pass|%d|%d|true-alpha-canvas-inverse-opacity"), int32(Pass), BlendMode);
    const FString Name = TEXT("D2U_PencilBackground_") + FMD5::HashAnsiString(*Identity);
    const FString PackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/") + Name;
    if (auto* Existing = LoadObject<UMaterial>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn))
        return EnsureCompiled(Existing, Error) ? Existing : nullptr;
    UTexture* Default = PencilShaderSdf::SamplerDefault(Error);
    if (!Default) return nullptr;
    UPackage* Package = CreatePackage(*PackageName);
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone));
    Material->MaterialDomain = Canvas ? MD_Surface : MD_UI;
    Material->BlendMode = BLEND_AlphaComposite;
    Material->SetShadingModel(MSM_Unlit);
    auto* Program = Expression<UMaterialExpressionCustom>(Material.Get());
    Program->OutputType = CMOT_Float4;
    Program->Code = Code;
    auto Texture = [&](const TCHAR* Parameter, const TCHAR* Argument)
    {
        auto* Value = Expression<UMaterialExpressionTextureObjectParameter>(Material.Get());
        Value->ParameterName = Parameter; Value->Texture = Default; Value->SamplerType = SAMPLERTYPE_LinearColor;
        Input(Program, Argument, Value);
    };
    auto Row = [&](const TCHAR* Parameter, const TCHAR* Argument, FLinearColor Value)
    {
        auto* RowValue = Expression<UMaterialExpressionVectorParameter>(Material.Get());
        RowValue->ParameterName = Parameter; RowValue->DefaultValue = Value;
        Input(Program, Argument, RowValue, VectorParameterRgbaOutput);
    };
    Input(Program, TEXT("UV"), Expression<UMaterialExpressionTextureCoordinate>(Material.Get()));
    if (Canvas || Consumer)
    {
        Texture(TEXT("D2USource"), TEXT("Source")); Texture(TEXT("D2UDestination"), TEXT("Destination"));
        Row(TEXT("D2USourceU"), TEXT("SourceU"), FLinearColor(1,0,0,0));
        Row(TEXT("D2USourceV"), TEXT("SourceV"), FLinearColor(0,1,0,0));
        auto* Mode = Expression<UMaterialExpressionScalarParameter>(Material.Get());
        Mode->ParameterName = TEXT("D2UBlendMode"); Mode->DefaultValue = BlendMode;
        Input(Program, TEXT("Mode"), Mode);
        if (Canvas)
        {
            auto* Opacity = Expression<UMaterialExpressionScalarParameter>(Material.Get());
            Opacity->ParameterName = TEXT("D2USourceOpacity"); Opacity->DefaultValue = 1;
            Input(Program, TEXT("SourceOpacity"), Opacity);
        }
        if (Consumer)
        {
            Texture(TEXT("D2UComposition"), TEXT("Composition"));
            Row(TEXT("D2UCompositionU"), TEXT("CompositionU"), FLinearColor(1,0,0,0));
            Row(TEXT("D2UCompositionV"), TEXT("CompositionV"), FLinearColor(0,1,0,0));
        }
    }
    else
    {
        Texture(TEXT("Texture"), TEXT("Source"));
        auto* Opacity = Expression<UMaterialExpressionScalarParameter>(Material.Get());
        Opacity->ParameterName = TEXT("D2UGroupOpacity"); Opacity->DefaultValue = 1;
        Input(Program, TEXT("GroupOpacity"), Opacity);
    }
    UMaterialExpression* Color = Mask(Material.Get(), Program, 3);
    if (Consumer)
    {
        auto* Transfer = Expression<UMaterialExpressionCustom>(Material.Get());
        Transfer->OutputType = CMOT_Float3; Transfer->Code = FillColorTransferCode;
        Input(Transfer, TEXT("EncodedRGB"), Color);
        auto* One = Expression<UMaterialExpressionConstant>(Material.Get()); One->R = 1;
        Input(Transfer, TEXT("VertexOpacity"), One);
        auto* OutputRole = Expression<UMaterialExpressionScalarParameter>(Material.Get());
        OutputRole->ParameterName = TEXT("D2UOutputLinearize"); OutputRole->DefaultValue = 1;
        auto* Select = Expression<UMaterialExpressionCustom>(Material.Get());
        Select->OutputType = CMOT_Float3; Select->Code = TEXT("return lerp(EncodedRGB,LinearRGB,OutputLinearize);");
        Input(Select, TEXT("EncodedRGB"), Color); Input(Select, TEXT("LinearRGB"), Transfer); Input(Select, TEXT("OutputLinearize"), OutputRole);
        Color = Select;
    }
    Material->GetExpressionInputForProperty(MP_EmissiveColor)->Expression = Color;
    UMaterialExpression* Alpha = Mask(Material.Get(), Program, 1, true);
    if (Canvas)
    {
        auto* InverseCanvasOpacity = Expression<UMaterialExpressionOneMinus>(Material.Get());
        InverseCanvasOpacity->Input.Expression = Alpha; Alpha = InverseCanvasOpacity;
    }
    Material->GetExpressionInputForProperty(MP_Opacity)->Expression = Alpha;
    Material->PreEditChange(nullptr); Material->PostEditChange();
    if (EnsureCompiled(Material.Get(), Error))
    {
        const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
        if (UPackage::SavePackage(Package, Material.Get(), *File, Args)) { FAssetRegistryModule::AssetCreated(Material.Get()); return Material.Get(); }
        Error = TEXT("Background material save failed: ") + File;
    }
    Material->ClearFlags(RF_Public | RF_Standalone);
    Material->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
    Material->MarkAsGarbage(); Package->SetDirtyFlag(false); return nullptr;
}

} // namespace


FString PencilShaderMaterials::BlendLibrary()
{
    return TEXT(R"D2U(struct FD2UBlend {
float Lum(float3 c){return dot(c,float3(0.3,0.59,0.11));}
float Sat(float3 c){return max(c.r,max(c.g,c.b))-min(c.r,min(c.g,c.b));}
float3 Clip(float3 c,float a){float l=Lum(c),n=min(c.r,min(c.g,c.b)),x=max(c.r,max(c.g,c.b));if(n<0)c=l+(c-l)*l/(l-n);if(x>a)c=l+(c-l)*(a-l)/(x-l);return c;}
float3 SetLum(float3 c,float l,float a){return Clip(c+l-Lum(c),a);}
float3 SetSat(float3 c,float s){float n=min(c.r,min(c.g,c.b)),x=max(c.r,max(c.g,c.b));return x>n?(c-n)*(s/(x-n)):float3(0,0,0);}
float Burn(float s,float d,float a){if(d>=a)return a;if(s<=0)return 0;return a-min(a,a*(a-d)/s);}
float Dodge(float s,float d,float a){if(d<=0)return 0;if(s>=a)return a;return min(a,a*d/(a-s));}
float Soft(float s,float d,float a){if(s<=a*0.5)return d-(a-2*s)*d*(a-d)/(a*a);float v=d<=a*0.25?((16*d/a-12)*d/a+4)*d:sqrt(a*d);return d+(2*s-a)*(v-d)/a;}
float4 Mix(float4 s,float4 d,float mode){float a=s.a*d.a;float3 b=0;if(a>0){float3 u=s.rgb*d.a,v=d.rgb*s.a,p=s.rgb*d.rgb;
if(mode<0.5)b=u;
else if(mode<1.5)b=min(u,v);
else if(mode<2.5)b=p;
else if(mode<3.5)b=max(0,u+v-a);
else if(mode<4.5)b=float3(Burn(u.r,v.r,a),Burn(u.g,v.g,a),Burn(u.b,v.b,a));
else if(mode<5.5)b=max(u,v);
else if(mode<6.5)b=u+v-p;
else if(mode<7.5)b=min(a,u+v);
else if(mode<8.5)b=float3(Dodge(u.r,v.r,a),Dodge(u.g,v.g,a),Dodge(u.b,v.b,a));
else if(mode<9.5)b=float3(v.r<=a*0.5?2*p.r:a-2*(a-u.r-v.r+p.r),v.g<=a*0.5?2*p.g:a-2*(a-u.g-v.g+p.g),v.b<=a*0.5?2*p.b:a-2*(a-u.b-v.b+p.b));
else if(mode<10.5)b=float3(Soft(u.r,v.r,a),Soft(u.g,v.g,a),Soft(u.b,v.b,a));
else if(mode<11.5)b=float3(u.r<=a*0.5?2*p.r:a-2*(a-u.r-v.r+p.r),u.g<=a*0.5?2*p.g:a-2*(a-u.g-v.g+p.g),u.b<=a*0.5?2*p.b:a-2*(a-u.b-v.b+p.b));
else if(mode<12.5)b=abs(v-u);
else if(mode<13.5)b=u+v-2*p;
else if(mode<14.5)b=SetLum(SetSat(u,Sat(v)),Lum(v),a);
else if(mode<15.5)b=SetLum(SetSat(v,Sat(u)),Lum(v),a);
else if(mode<16.5)b=SetLum(u,Lum(v),a);
else b=SetLum(v,Lum(u),a);
}return float4(s.rgb*(1-d.a)+d.rgb*(1-s.a)+b,s.a+d.a*(1-s.a));}
};
)D2U");
}

bool PencilShaderMaterials::BlendModeIndex(const FString& Name, int32& OutMode, FString& Error)
{
    static const TCHAR* const Names[] = { TEXT("normal"), TEXT("darken"), TEXT("multiply"), TEXT("linearBurn"), TEXT("colorBurn"), TEXT("light"), TEXT("screen"), TEXT("linearDodge"), TEXT("colorDodge"), TEXT("overlay"), TEXT("softLight"), TEXT("hardLight"), TEXT("difference"), TEXT("exclusion"), TEXT("hue"), TEXT("saturation"), TEXT("color"), TEXT("luminosity") };
    if (Name.IsEmpty()) { OutMode = 0; return true; }
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
        if (Name.Equals(Names[Index], ESearchCase::IgnoreCase)) { OutMode = Index; return true; }
    Error = TEXT("Unsupported paint blend mode: ") + Name; return false;
}
bool PencilShaderMaterials::PrepareBackgroundMaterials(UMaterial*& Composite, UMaterial*& Consumer, UMaterial*& Identity, UMaterial*& Opacity, FString& Error)
{
    Composite = BackgroundMaterial(EBackgroundPass::Composite, 0, Error);
    Consumer = Composite ? BackgroundMaterial(EBackgroundPass::Consumer, 0, Error) : nullptr;
    Identity = Consumer ? BackgroundMaterial(EBackgroundPass::Identity, 0, Error) : nullptr;
    Opacity = Identity ? BackgroundMaterial(EBackgroundPass::Opacity, 0, Error) : nullptr;
    return Composite && Consumer && Identity && Opacity;
}

bool PencilShaderMaterials::PrepareObjectCapture(UMaterial*& TransparentOutput, UMaterial*& Copy, FString& Error)
{
    TransparentOutput = CaptureMaterial(false, Error);
    Copy = TransparentOutput ? CaptureMaterial(true, Error) : nullptr;
    return TransparentOutput && Copy;
}

namespace
{
FUMGWidgetNode CaptureFillPrefix(FUMGWidgetNode&& Fill, TArray<FUMGWidgetNode>&& Painted, FUMGShadowData&& Shadow)
{
    FUMGWidgetNode Prefix;
    Prefix.Type = UMGWidgetTypes::Overlay;
    Prefix.Id = Fill.Id + TEXT("_prefix");
    Prefix.Name = Fill.Name + TEXT("_prefix");
    Prefix.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
    Prefix.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
    Prefix.Shadow = MoveTemp(Shadow);
    Prefix.Children = MoveTemp(Painted);

    FUMGWidgetNode Retainer;
    Retainer.Type = UMGWidgetTypes::RetainerBox;
    Retainer.Id = Fill.Id + TEXT("_object");
    Retainer.Name = Fill.Name + TEXT("_object");
    Retainer.Bounds = Fill.Bounds;
    Retainer.Slot = Fill.Slot;
    Retainer.Render = MoveTemp(Fill.Render);
    Retainer.Path = MoveTemp(Fill.Path);
    Retainer.Brush.Image = MoveTemp(Fill.Brush.Image);
    Retainer.Effect.Shader = MoveTemp(Fill.Brush.Shader);
    Retainer.Effect.EffectMaterial = Fill.Brush.MaterialRef;
    Retainer.Effect.TextureParameter = TEXT("D2UObject");
    Retainer.Effect.bRetainRendering = true;
    const FDesignShaderData& Shader = Retainer.Effect.Shader.GetValue();
    const FVector2D Size = Shader.Size;
    Prefix.Bounds.W = Size.X;
    Prefix.Bounds.H = Size.Y;
    const FMargin Insets(Shader.CaptureInsets.X, Shader.CaptureInsets.Y,
                         Shader.CaptureInsets.Z, Shader.CaptureInsets.W);
    Retainer.Children.Add(MoveTemp(Prefix));
    FUMGWidgetNode Surface = UMGRetainerCapture::Wrap(MoveTemp(Retainer), Size, Insets);
    Surface.WidgetStyle = MoveTemp(Fill.WidgetStyle);
    return Surface;
}

bool PrepareNode(FUMGWidgetNode& Root, FString& Error,
                 DesignImageResources::FImageOperationBudget& Budget, bool& OutFillObject)
{
    OutFillObject = false;
    int32 PaintMode = 0;
    if (Root.Effect.EffectMaterial.Get(FString()) != UMGPropertyKeys::RetainerSubtreeShadowEffectRef)
    {
        if (!PencilShaderMaterials::BlendModeIndex(Root.Render.PaintBlendMode.Get(FString()), PaintMode, Error)) return false;
        Root.Render.PaintBlendMode.Reset();
    }
    bool Context = PaintMode != 0;
    TArray<FUMGWidgetNode> Children = MoveTemp(Root.Children);
    TArray<FUMGWidgetNode> Painted;
    Painted.Reserve(Children.Num());
    for (int32 Index = 0; Index < Children.Num(); ++Index)
    {
        FUMGWidgetNode& Child = Children[Index];
        bool FillObject = false;
        if (!PrepareNode(Child, Error, Budget, FillObject))
        {
            for (int32 Remaining = Index; Remaining < Children.Num(); ++Remaining)
                Painted.Add(MoveTemp(Children[Remaining]));
            Root.Children = MoveTemp(Painted);
            return false;
        }
        if (FillObject)
        {
            FUMGWidgetNode Surface = CaptureFillPrefix(MoveTemp(Child), MoveTemp(Painted), MoveTemp(Root.Shadow));
            Painted.Reset();
            Painted.Add(MoveTemp(Surface));
        }
        else
            Painted.Add(MoveTemp(Child));
    }
    Root.Children = MoveTemp(Painted);
    if (Root.Brush.Shader.IsSet())
    {
        UMaterial* Material = Build(Root.Brush.Shader.GetValue(), false,
            Root.Brush.Image.TexturePath.Get(FString()), Error, Budget, &OutFillObject, &Context, Context);
        if (!Material) return false;
        Root.Brush.MaterialRef = Material->GetPathName();
    }
    if (Root.Effect.Shader.IsSet())
    {
        const bool FillPaint = Root.Effect.Shader->bFillPaint;
        UMaterial* Material = Build(Root.Effect.Shader.GetValue(), true,
            FillPaint ? Root.Brush.Image.TexturePath.Get(FString()) : FString(), Error, Budget, nullptr, &Context, Context);
        if (!Material) return false;
        Root.Effect.EffectMaterial = Material->GetPathName();
        Root.Effect.TextureParameter = TEXT("D2UObject");
    }
    if (Context)
    {
        const FDesignShaderData* Shader = Root.Brush.Shader.IsSet() ? &Root.Brush.Shader.GetValue()
            : Root.Effect.Shader.IsSet() ? &Root.Effect.Shader.GetValue() : nullptr;
        if (PaintMode == 0 && Shader && !PencilShaderMaterials::BlendModeIndex(Shader->BlendMode, PaintMode, Error)) return false;
        UMaterial* Consumer = BackgroundMaterial(EBackgroundPass::Consumer, PaintMode, Error);
        if (!Consumer) return false;
        FUMGWidgetNode Source = MoveTemp(Root);
        FUMGWidgetNode Wrapper;
        Wrapper.Type = UMGWidgetTypes::RetainerBox;
        Wrapper.Id = Source.Id + TEXT("_composition");
        Wrapper.Name = Source.Name + TEXT("_composition");
        Wrapper.Bounds = Source.Bounds;
        Wrapper.Slot = Source.Slot;
        Wrapper.WidgetStyle.WidthOverride = MoveTemp(Source.WidgetStyle.WidthOverride);
        Source.WidgetStyle.WidthOverride.Reset();
        Wrapper.WidgetStyle.HeightOverride = MoveTemp(Source.WidgetStyle.HeightOverride);
        Source.WidgetStyle.HeightOverride.Reset();
        Wrapper.WidgetStyle.MinDesiredWidth = MoveTemp(Source.WidgetStyle.MinDesiredWidth);
        Source.WidgetStyle.MinDesiredWidth.Reset();
        Wrapper.WidgetStyle.MinDesiredHeight = MoveTemp(Source.WidgetStyle.MinDesiredHeight);
        Source.WidgetStyle.MinDesiredHeight.Reset();
        Wrapper.WidgetStyle.Visibility = MoveTemp(Source.WidgetStyle.Visibility);
        Source.WidgetStyle.Visibility.Reset();
        Wrapper.WidgetStyle.IsEnabled = MoveTemp(Source.WidgetStyle.IsEnabled);
        Source.WidgetStyle.IsEnabled.Reset();
        const TOptional<float> Opacity = Source.Render.RenderOpacity;
        Wrapper.Render = MoveTemp(Source.Render);
        Wrapper.Render.RenderOpacity.Reset();
        Source.Render = FUMGRenderStyleData();
        Source.Render.RenderOpacity = Opacity;
        Source.Bounds.X = 0; Source.Bounds.Y = 0;
        Source.Slot = FUMGSlotData();
        Source.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
        Source.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
        Wrapper.Effect.EffectMaterial = Consumer->GetPathName();
        Wrapper.Effect.TextureParameter = TEXT("D2USource");
        Wrapper.Effect.bRetainRendering = true;
        Wrapper.Children.Add(MoveTemp(Source));
        Root = MoveTemp(Wrapper);
        OutFillObject = false;
    }
    return true;
}
}

bool PencilShaderMaterials::Prepare(FUMGWidgetNode& Root, FString& Error, DesignImageResources::FImageOperationBudget& Budget)
{
    bool FillObject = false;
    return PrepareNode(Root, Error, Budget, FillObject);
}
bool PencilShaderMaterials::PrepareGeometryMasks(FUMGWidgetNode& Root,FString& Error)
{
    for(auto& Child:Root.Children)if(!PrepareGeometryMasks(Child,Error))return false;
    if(!Root.Brush.Shader.IsSet())return true;
    if(Root.Type!=TEXT("Border")&&Root.Type!=TEXT("Image"))
    {Error=TEXT("Shader fill requires a native image or border paint surface");return false;}
    if(Root.Brush.OutlineThickness.Get(0)>0)
    {Error=TEXT("Shader fill with a native outline requires separate paint layers");return false;}
    if(Root.Path.HasPaintGeometry()&&Root.Path.StrokeThickness.Get(0)>0&&Root.Path.FillColor.IsSet())
    {Error=TEXT("Shader fill with a combined path stroke requires separate paint layers");return false;}
    if(!Root.Path.HasPaintGeometry()&&(Root.Brush.CornerRadii.IsSet()||Root.Brush.OutlineCornerRadius.IsSet())&&!Root.Brush.MaterialRef.IsSet())
    {
        Root.Brush.MaterialRef=TEXT("roundedrect");
        FUMGProceduralResourceRequest Request;
        Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Root,Request);
        const float Width=Request.WidthOverride.Get(64.0f),Height=Request.HeightOverride.Get(64.0f);
        const float HalfMin=FMath::Min(Width,Height)*0.5f;
        if(HalfMin<=0)
        {Error=TEXT("Shader geometry mask requires positive paint dimensions");return false;}
        FUMGVisualBrushData::FShapeData Shape;Shape.Color=FLinearColor::White;
        // Border radii are pixels; procedural shapes use fractions of half-min extent.
        // Native per-corner radii take precedence, including the half-height capsule sentinel.
        const bool bPerCorner=Root.Brush.CornerRadii.IsSet();
        const FVector4 Radii=Root.Brush.CornerRadii.Get(FVector4(0,0,0,0));
        const float Radius=bPerCorner?FMath::Max3(Radii.X,Radii.Y,FMath::Max(Radii.Z,Radii.W)):Root.Brush.OutlineCornerRadius.Get(0);
        if(Radius>=UMGPropertyKeys::CapsuleRadiusSentinel)Shape.CornerRadius=(Height*0.5f)/HalfMin;
        else if(bPerCorner)Shape.CornerRadii=Radii/HalfMin;
        else Shape.CornerRadius=FMath::Max(0.0f,Radius)/HalfMin;
        Root.Brush.Shape=Shape;
    }
    return true;
}
