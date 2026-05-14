// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "PreBuildResourceNormalizer.h"
#include "DesignResourceResolver.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "Design2UMGLog.h"
#include "DesignPathRasterizer.h"
#include "ProceduralShapeGenerator.h"
#include "ObjectTools.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "TextureCompiler.h"
#include "TextureResource.h"
#include "UObject/SavePackage.h"

namespace
{
	void ReplaceNodeFillWithTexturePath(FUMGWidgetNode& Node, const FString& TexturePath)
	{
		Node.Path = FUMGPathData();
		Node.Brush.MaterialRef.Reset();
		Node.Brush.MaterialParams.Reset();
		Node.Brush.TexturePath.Empty();
		Node.Brush.Image.TexturePath = TexturePath;
		Node.Brush.Gradient = FUMGVisualBrushData::FGradientData();
	}

	uint32 MakePathTextureKey(const FUMGWidgetNode& Node)
	{
		const FString Geometry = Node.Path.Geometry.Get(FString());
		const FString FillRule = Node.Path.FillRule.Get(FString());
		const FString FillColor = Node.Path.FillColor.IsSet() ? Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.FillColor.GetValue()) : FString();
		const FString StrokeColor = Node.Path.StrokeColor.IsSet() ? Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.StrokeColor.GetValue()) : FString();
		const FString StrokeThickness =
			Node.Path.StrokeThickness.IsSet() && Node.Path.StrokeThickness.GetValue() > 0.0f ? FString::SanitizeFloat(Node.Path.StrokeThickness.GetValue()) : FString();
		const FString StrokeCap = Node.Path.StrokeCap.Get(FString());
		const FString StrokeJoin = Node.Path.StrokeJoin.Get(FString());
		const FString WidthOverride =
			(Node.WidgetStyle.WidthOverride.IsSet() && Node.WidgetStyle.WidthOverride.GetValue() > 0.0f) ? FString::SanitizeFloat(Node.WidgetStyle.WidthOverride.GetValue()) : FString();
		const FString HeightOverride =
			(Node.WidgetStyle.HeightOverride.IsSet() && Node.WidgetStyle.HeightOverride.GetValue() > 0.0f) ? FString::SanitizeFloat(Node.WidgetStyle.HeightOverride.GetValue()) : FString();

		uint32 Key = Geometry.IsEmpty() ? 0u : GetTypeHash(Geometry);
		Key = HashCombine(Key, FillRule.IsEmpty() ? 0u : GetTypeHash(FillRule));
		Key = HashCombine(Key, FillColor.IsEmpty() ? 0u : GetTypeHash(FillColor));
		Key = HashCombine(Key, StrokeColor.IsEmpty() ? 0u : GetTypeHash(StrokeColor));
		Key = HashCombine(Key, StrokeThickness.IsEmpty() ? 0u : GetTypeHash(StrokeThickness));
		Key = HashCombine(Key, StrokeCap.IsEmpty() ? 0u : GetTypeHash(StrokeCap));
		Key = HashCombine(Key, StrokeJoin.IsEmpty() ? 0u : GetTypeHash(StrokeJoin));
		Key = HashCombine(Key, WidthOverride.IsEmpty() ? 0u : GetTypeHash(WidthOverride));
		Key = HashCombine(Key, HeightOverride.IsEmpty() ? 0u : GetTypeHash(HeightOverride));
		return Key;
	}

	uint32 MakeProceduralTextureKey(const FUMGWidgetNode& Node)
	{
		FUMGProceduralResourceRequest Request;
		if (!Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, Request))
		{
			return 0u;
		}

		const FString MaterialRef = Request.MaterialRef;
		const FString MaterialParams = Request.MaterialParams.Get(FString());
		const FString TextureWidth = Request.WidthOverride.IsSet()
			? Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Request.WidthOverride.GetValue())
			: FString();
		const FString TextureHeight = Request.HeightOverride.IsSet()
			? Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Request.HeightOverride.GetValue())
			: FString();
		const FString ShadowBlur = Request.ShadowBlur.IsSet()
			? Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Request.ShadowBlur.GetValue())
			: FString();
		const FString BrushColor = Request.BrushColor.IsSet()
			? Design2UMGWidgetNodeAccess::SerializeWidgetColor(Request.BrushColor.GetValue())
			: FString();

		uint32 Key = GetTypeHash(MaterialRef);
		Key = HashCombine(Key, GetTypeHash(MaterialParams));
		Key = HashCombine(Key, GetTypeHash(TextureWidth));
		Key = HashCombine(Key, GetTypeHash(TextureHeight));
		Key = HashCombine(Key, GetTypeHash(ShadowBlur));
		Key = HashCombine(Key, GetTypeHash(BrushColor));
		return Key;
	}

	bool ApplyPathTextureUiSettings(UTexture2D* Texture, const TextureFilter Filter = TF_Bilinear)
	{
		if (!Texture) return false;
		Texture->SRGB = true;
		Texture->NeverStream = true;
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->Filter = Filter;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		return true;
	}

	bool EnsurePersistedTextureReady(UTexture2D* Texture)
	{
		if (!Texture)
		{
			return false;
		}

		Texture->ConditionalPostLoad();
#if WITH_EDITOR
		FTextureCompilingManager::Get().FinishCompilation({ Texture });
#endif
		Texture->UpdateResource();
		if (!GUsingNullRHI)
		{
			FlushRenderingCommands();
		}
		return true;
	}

	bool ExtractTextureRawData(UTexture2D* Texture, TArray64<uint8>& OutRawData, int32& OutWidth, int32& OutHeight)
	{
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() <= 0)
			return false;

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		OutWidth = Mip.SizeX;
		OutHeight = Mip.SizeY;
		if (OutWidth <= 0 || OutHeight <= 0) return false;

		const int64 ExpectedBytes = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4;
		const void* LockedData = Mip.BulkData.LockReadOnly();
		if (!LockedData) { Mip.BulkData.Unlock(); return false; }

		OutRawData.SetNumUninitialized(ExpectedBytes);
		FMemory::Memcpy(OutRawData.GetData(), LockedData, ExpectedBytes);
		Mip.BulkData.Unlock();
		return true;
	}

	bool CreateOrUpdatePathTextureAsset(
		const FString& ImportPath,
		const FString& AssetName,
		const TArray64<uint8>& RawData,
		int32 Width, int32 Height,
		const TextureFilter Filter,
		FString& OutObjectPath)
	{
		const FString LongPackageName = ImportPath / AssetName;
		if (!FPackageName::IsValidLongPackageName(LongPackageName)) return false;

		const FString ObjectPath = LongPackageName + TEXT(".") + AssetName;
		UTexture2D* Texture = DesignResourceResolver::LoadTextureFromPath(ObjectPath, DesignResourceResolver::ELoadMode::LoadOnly);
		const bool bNewAsset = (Texture == nullptr);
		if (!Texture)
		{
			UPackage* Package = CreatePackage(*LongPackageName);
			if (!Package) return false;
			Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
			if (!Texture) return false;
		}

		UPackage* Package = Texture->GetOutermost();
		if (!Package) return false;

		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, RawData.GetData());
		ApplyPathTextureUiSettings(Texture, Filter);
		Texture->PostEditChange();
		if (!EnsurePersistedTextureReady(Texture))
		{
			return false;
		}
		Texture->MarkPackageDirty();
		Package->MarkPackageDirty();

		if (bNewAsset) FAssetRegistryModule::AssetCreated(Texture);

		const FString PackageFilename = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs)) return false;
		if (!EnsurePersistedTextureReady(Texture))
		{
			return false;
		}

		OutObjectPath = ObjectPath;
		return true;
	}

	void NormalizePathTexturesRecursive(
		FUMGWidgetNode& Node,
		const FString& TextureImportPath,
		const FString& AssetPrefix,
		TMap<uint32, FString>& Cache)
	{
		const FString Geometry = Node.Path.Geometry.Get(FString());
		if (Node.Type == TEXT("Image") && !Geometry.IsEmpty())
		{
			const uint32 Key = MakePathTextureKey(Node);
			if (const FString* Cached = Cache.Find(Key))
			{
				ReplaceNodeFillWithTexturePath(Node, *Cached);
			}
			else
			{
				const FString FillRule = Node.Path.FillRule.Get(FString()).IsEmpty() ? TEXT("nonzero") : Node.Path.FillRule.GetValue();
				const FString FillColor = Node.Path.FillColor.IsSet() ? Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.FillColor.GetValue()) : FString();
				const FString StrokeColor = Node.Path.StrokeColor.IsSet() ? Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.StrokeColor.GetValue()) : FString();
				const FString StrokeCap = Node.Path.StrokeCap.Get(FString()).IsEmpty() ? TEXT("butt") : Node.Path.StrokeCap.GetValue();
				const FString StrokeJoin = Node.Path.StrokeJoin.Get(FString()).IsEmpty() ? TEXT("miter") : Node.Path.StrokeJoin.GetValue();
				const float StrokeThickness = Node.Path.StrokeThickness.Get(0.0f);
				const int32 Width  = (Node.WidgetStyle.WidthOverride.IsSet()  && Node.WidgetStyle.WidthOverride.GetValue()  > 0.0f) ? FMath::RoundToInt(Node.WidgetStyle.WidthOverride.GetValue())
					: 64;
				const int32 Height = (Node.WidgetStyle.HeightOverride.IsSet() && Node.WidgetStyle.HeightOverride.GetValue() > 0.0f) ? FMath::RoundToInt(Node.WidgetStyle.HeightOverride.GetValue())
					: 64;

				constexpr int32 PathSupersample = 2;
				const TOptional<FLinearColor> OptFill = FillColor.IsEmpty() ? TOptional<FLinearColor>() : TOptional<FLinearColor>(FUMGValueParser::ParseColor(FillColor));
				const TOptional<FLinearColor> OptStroke = StrokeColor.IsEmpty() ? TOptional<FLinearColor>() : TOptional<FLinearColor>(FUMGValueParser::ParseColor(StrokeColor));
				if (UTexture2D* Rasterized = FDesignPathRasterizer::Rasterize(
					*Geometry, Width * PathSupersample, Height * PathSupersample,
					OptFill, FillRule, OptStroke, StrokeThickness, StrokeCap, StrokeJoin))
				{
					TArray64<uint8> RawData;
					int32 RawW = 0, RawH = 0;
					if (ExtractTextureRawData(Rasterized, RawData, RawW, RawH))
					{
						const FString SanitizedName = ObjectTools::SanitizeObjectName(Node.Name.IsEmpty() ? Node.Id : Node.Name);
						const FString AssetName = ObjectTools::SanitizeObjectName(FString::Printf(TEXT("%s_%s_%08X"), *AssetPrefix, *SanitizedName, Key));
						FString ObjectPath;
						if (CreateOrUpdatePathTextureAsset(TextureImportPath, AssetName, RawData, RawW, RawH, TF_Nearest, ObjectPath))
						{
							ReplaceNodeFillWithTexturePath(Node, ObjectPath);
							Cache.Add(Key, ObjectPath);
							UE_LOG(LogDesign2UMG, Log, TEXT("Persisted path texture '%s' -> %s"), *Node.Name, *ObjectPath);
						}
					}
				}
			}
		}

		for (FUMGWidgetNode& Child : Node.Children)
			NormalizePathTexturesRecursive(Child, TextureImportPath, AssetPrefix, Cache);
	}

	void NormalizeProceduralTexturesRecursive(
		FUMGWidgetNode& Node,
		const FString& TextureImportPath,
		const FString& AssetPrefix,
		TMap<uint32, FString>& Cache)
	{
		FUMGProceduralResourceRequest ResourceRequest;
		if (Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, ResourceRequest)
			&& ResourceRequest.HasNonEmptyMaterialRef())
		{
			const uint32 Key = MakeProceduralTextureKey(Node);
			if (const FString* Cached = Cache.Find(Key))
			{
				ReplaceNodeFillWithTexturePath(Node, *Cached);
			}
			else
			{
				if (UTexture2D* Generated = DesignResourceResolver::ResolveTextureFromMaterialRef(ResourceRequest, nullptr))
				{
					TArray64<uint8> RawData;
					int32 RawW = 0, RawH = 0;
					if (ExtractTextureRawData(Generated, RawData, RawW, RawH))
					{
						const FString SanitizedName = ObjectTools::SanitizeObjectName(Node.Name.IsEmpty() ? Node.Id : Node.Name);
						const FString SanitizedMat = ObjectTools::SanitizeObjectName(ResourceRequest.MaterialRef);
						const FString AssetName = ObjectTools::SanitizeObjectName(FString::Printf(TEXT("%s_%s_%s_%08X"), *AssetPrefix, *SanitizedName, *SanitizedMat, Key));
						FString ObjectPath;
						if (CreateOrUpdatePathTextureAsset(TextureImportPath, AssetName, RawData, RawW, RawH, TF_Bilinear, ObjectPath))
						{
							ReplaceNodeFillWithTexturePath(Node, ObjectPath);
							Cache.Add(Key, ObjectPath);
							UE_LOG(LogDesign2UMG, Log, TEXT("Persisted procedural texture '%s' (%s) -> %s"), *Node.Name, *ResourceRequest.MaterialRef, *ObjectPath);
						}
					}
				}
			}
		}

		for (FUMGWidgetNode& Child : Node.Children)
			NormalizeProceduralTexturesRecursive(Child, TextureImportPath, AssetPrefix, Cache);
	}
}

void FPreBuildResourceNormalizer::NormalizePathTextures(FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix)
{
	TMap<uint32, FString> Cache;
	NormalizePathTexturesRecursive(Root, TextureImportPath, AssetPrefix, Cache);
}

void FPreBuildResourceNormalizer::NormalizeProceduralTextures(FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix)
{
	TMap<uint32, FString> Cache;
	NormalizeProceduralTexturesRecursive(Root, TextureImportPath, AssetPrefix, Cache);
}
