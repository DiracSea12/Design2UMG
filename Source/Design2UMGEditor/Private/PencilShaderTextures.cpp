// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "PencilShaderTextures.h"
#include "PencilShaderSyntax.h"
#include "DesignShaderData.h"
#include "Misc/FileHelper.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "DesignImageResourceLimits.h"
#include "GeneratedAssetLifecycle.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "TextureCompiler.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
bool Ready(UTexture2D* Texture,bool bMipmap,FString& Error)
{
    Texture->ConditionalPostLoad();
    FTextureCompilingManager::Get().FinishCompilation({Texture});
    if(Texture->GetNumMips()<1||(bMipmap&&Texture->GetNumMips()<2&&(Texture->Source.GetSizeX()>1||Texture->Source.GetSizeY()>1)))
    {
        Error=TEXT("Shader sampler texture has no usable mip chain: ")+Texture->GetPathName();return false;
    }
    Texture->UpdateResource();return true;
}
}

TMap<FString, FString> PencilShaderTextures::ImageUniforms(const FDesignShaderData& Shader)
{
    FString Source;
    PencilShaderSyntax::FProgram Program;
    std::string Error;
    TMap<FString, FString> Images;
    if (FFileHelper::LoadFileToString(Source, *Shader.Url) &&
        PencilShaderSyntax::PrepareSource(TCHAR_TO_UTF8(*Source), Program, Error))
    {
        for (const auto& Uniform : Program.Uniforms)
        {
            if (Uniform.Type != "sampler2D" || Uniform.Sdf) continue;
            FString Value = PencilShaderSyntax::ResolveStringUniform(Shader, Uniform);
            if (!Value.IsEmpty() && !Value.StartsWith(TEXT("pencil:")))
                Images.Add(UTF8_TO_TCHAR(Uniform.Name.c_str()), MoveTemp(Value));
        }
    }
    return Images;
}

UTexture2D *PencilShaderTextures::SamplerImage(UTexture2D *Source, bool bMipmap,
                                               DesignImageResources::FImageOperationBudget &Budget, FString &Error)
{
    if (!Source || !Source->Source.IsValid())
    {
        Error = TEXT("Shader sampler image generation requires the image texture source");
        return nullptr;
    }
    const FString Identity = Source->GetPathName() + Source->Source.GetId().ToString() +
                             FString::Printf(TEXT("_EncodedPremultipliedRGBA8_%s_Repeat"),
                                             bMipmap ? TEXT("SimpleAverage_Trilinear") : TEXT("NoMip_Bilinear"));
    const FString Name = TEXT("D2U_ShaderTexture_") + FMD5::HashAnsiString(*Identity);
    const FString PackageName = TEXT("/Game/Design2UMG/GeneratedTextures/") + Name;
    if (auto Existing = LoadObject<UTexture2D>(nullptr, *(PackageName + TEXT(".") + Name)))
        return Ready(Existing, bMipmap, Error) ? Existing : nullptr;
    int64 Pixels = 0;
    int32 Width = Source->Source.GetSizeX(), Height = Source->Source.GetSizeY();
    for (;;)
    {
        Pixels += int64(Width) * Height;
        if (!bMipmap || (Width == 1 && Height == 1))
            break;
        Width = FMath::Max(1, Width / 2);
        Height = FMath::Max(1, Height / 2);
    }
    if (!Budget.ConsumeDecodedPixels(Pixels, Error))
        return nullptr;
    FImage Image;
    if (!FImageUtils::GetTexture2DSourceImage(Source, Image))
    {
        Error = TEXT("Shader sampler image source could not be read: ") + Source->GetPathName();
        return nullptr;
    }
    // Pencil uploads raw RGBA8 with premultiplied alpha and no colour-space conversion.
    // Premultiply before interpolation and mip generation, not after sampling.
    Image.ChangeFormat(ERawImageFormat::BGRA8, Image.GetGammaSpace());
    for (FColor &Color : Image.AsBGRA8())
    {
        Color.R = static_cast<uint8>((uint32(Color.R) * Color.A + 127) / 255);
        Color.G = static_cast<uint8>((uint32(Color.G) * Color.A + 127) / 255);
        Color.B = static_cast<uint8>((uint32(Color.B) * Color.A + 127) / 255);
    }
    UPackage *Package = CreatePackage(*PackageName);
    TStrongObjectPtr<UTexture2D> Texture(DuplicateObject<UTexture2D>(Source, Package, *Name));
    Texture->SetFlags(RF_Public | RF_Standalone);
    Texture->ClearFlags(RF_Transient);
    Texture->PreEditChange(nullptr);
    Texture->Source.Init(Image.SizeX, Image.SizeY, 1, 1, TSF_BGRA8, Image.RawData.GetData());
    Texture->SRGB = false;
    Texture->CompressionSettings = TC_EditorIcon;
    Texture->CompressionNoAlpha = false;
    Texture->MipGenSettings = bMipmap ? TMGS_SimpleAverage : TMGS_NoMipmaps;
    Texture->Filter = bMipmap ? TF_Trilinear : TF_Bilinear;
    Texture->AddressX = TA_Wrap;
    Texture->AddressY = TA_Wrap;
    Texture->NeverStream = true;
    Texture->LODGroup = TEXTUREGROUP_UI;
    Texture->LODBias = 0;
    Texture->MaxTextureSize = 0;
    Texture->PostEditChange();
    if (Ready(Texture.Get(), bMipmap, Error))
    {
        const FString File =
            FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        if (UPackage::SavePackage(Package, Texture.Get(), *File, Args))
        {
            FAssetRegistryModule::AssetCreated(Texture.Get());
            return Texture.Get();
        }
        Error = TEXT("Shader sampler texture save failed: ") + File;
    }
    GeneratedAssetLifecycle::DiscardUnsavedAsset(Texture.Get());
    Package->SetDirtyFlag(false);
    return nullptr;
}
