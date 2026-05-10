// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "PreBuildResourceNormalizer.h"
#include "DesignResourceResolver.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParsing.h"
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
	FString FindNodeProperty(const FUMGWidgetNode& Node, const TCHAR* Key)
	{
		FString Value;
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, Key, Value);
		return Value;
	}

	FString FindNodePropertyOr(const FUMGWidgetNode& Node, const TCHAR* Key, const FString& DefaultValue)
	{
		FString Value;
		return Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, Key, Value) ? Value : DefaultValue;
	}

	int32 GetNodeRoundedIntProperty(const FUMGWidgetNode& Node, const TCHAR* Key, int32 DefaultValue)
	{
		FString SerializedValue;
		if (Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, Key, SerializedValue))
		{
			return FMath::RoundToInt(FCString::Atof(*SerializedValue));
		}
		return DefaultValue;
	}

	void ReplaceNodeFillWithTexturePath(FUMGWidgetNode& Node, const FString& TexturePath)
	{
		Node.Path = FUMGPathData();
		Node.Brush.MaterialRef.Reset();
		Node.Brush.bHasMaterialRef = false;
		Node.Brush.MaterialParams.Reset();
		Node.Brush.bHasMaterialParams = false;
		Node.Brush.TexturePath = TexturePath;
		Node.Brush.Image.TexturePath = TexturePath;
		Node.Brush.Image.bHasTexturePath = true;
		Node.Brush.Gradient = FUMGVisualBrushData::FGradientData();
	}

	FString GetPathField(const FUMGWidgetNode& Node, const TCHAR* BagKey, const FString& TypedValue)
	{
		if (!TypedValue.IsEmpty()) return TypedValue;
		FString BagValue;
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, BagKey, BagValue);
		return BagValue;
	}

	uint32 MakePathTextureKey(const FUMGWidgetNode& Node)
	{
		const FString Geometry    = GetPathField(Node, UMGPropertyKeys::PathGeometry,        Node.Path.Geometry);
		const FString FillRule    = GetPathField(Node, UMGPropertyKeys::PathFillRule,         Node.Path.FillRule);
		const FString FillColor   = GetPathField(Node, UMGPropertyKeys::PathFillColor,        Node.Path.bHasFillColor   ? Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.FillColor)   : FString());
		const FString StrokeColor = GetPathField(Node, UMGPropertyKeys::PathStrokeColor,      Node.Path.bHasStrokeColor ? Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.StrokeColor) : FString());
		const FString StrokeThickness = GetPathField(Node, UMGPropertyKeys::PathStrokeThickness,
			Node.Path.StrokeThickness > 0.0f ? FString::SanitizeFloat(Node.Path.StrokeThickness) : FString());
		const FString StrokeCap   = GetPathField(Node, UMGPropertyKeys::PathStrokeCap,        Node.Path.StrokeCap);
		const FString StrokeJoin  = GetPathField(Node, UMGPropertyKeys::PathStrokeJoin,       Node.Path.StrokeJoin);
		const FString WidthOverride  = GetPathField(Node, UMGPropertyKeys::WidthOverride,
			(Node.WidgetStyle.WidthOverride.IsSet()  && Node.WidgetStyle.WidthOverride.GetValue()  > 0.0f) ? FString::SanitizeFloat(Node.WidgetStyle.WidthOverride.GetValue())  : FString());
		const FString HeightOverride = GetPathField(Node, UMGPropertyKeys::HeightOverride,
			(Node.WidgetStyle.HeightOverride.IsSet() && Node.WidgetStyle.HeightOverride.GetValue() > 0.0f) ? FString::SanitizeFloat(Node.WidgetStyle.HeightOverride.GetValue()) : FString());

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
		const FString MaterialRef = FindNodePropertyOr(Node, UMGPropertyKeys::MaterialRef, Node.Brush.MaterialRef);
		const FString MaterialParams = FindNodePropertyOr(Node, UMGPropertyKeys::MaterialParams, Node.Brush.MaterialParams);
		const FString TextureWidth = FindNodeProperty(Node, UMGPropertyKeys::WidthOverride);
		const FString TextureHeight = FindNodeProperty(Node, UMGPropertyKeys::HeightOverride);
		const FString ShadowBlur = FindNodeProperty(Node, UMGPropertyKeys::ShadowBlur);
		const FString BrushColor = FindNodeProperty(Node, UMGPropertyKeys::BrushColor);

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
		const FString Geometry = GetPathField(Node, UMGPropertyKeys::PathGeometry, Node.Path.Geometry);
		if (Node.Type == TEXT("Image") && !Geometry.IsEmpty())
		{
			const uint32 Key = MakePathTextureKey(Node);
			if (const FString* Cached = Cache.Find(Key))
			{
				ReplaceNodeFillWithTexturePath(Node, *Cached);
			}
			else
			{
				const FString FillRule    = GetPathField(Node, UMGPropertyKeys::PathFillRule,    Node.Path.FillRule.IsEmpty()    ? TEXT("nonzero") : Node.Path.FillRule);
				const FString FillColor   = GetPathField(Node, UMGPropertyKeys::PathFillColor,   Node.Path.bHasFillColor   ? Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.FillColor)   : FString());
				const FString StrokeColor = GetPathField(Node, UMGPropertyKeys::PathStrokeColor, Node.Path.bHasStrokeColor ? Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.StrokeColor) : FString());
				const FString StrokeCap   = GetPathField(Node, UMGPropertyKeys::PathStrokeCap,   Node.Path.StrokeCap.IsEmpty()   ? TEXT("butt")    : Node.Path.StrokeCap);
				const FString StrokeJoin  = GetPathField(Node, UMGPropertyKeys::PathStrokeJoin,  Node.Path.StrokeJoin.IsEmpty()  ? TEXT("miter")   : Node.Path.StrokeJoin);
				const float StrokeThickness = Node.Path.StrokeThickness > 0.0f
					? Node.Path.StrokeThickness
					: FCString::Atof(*FindNodePropertyOr(Node, UMGPropertyKeys::PathStrokeThickness, TEXT("0")));
				const int32 Width  = (Node.WidgetStyle.WidthOverride.IsSet()  && Node.WidgetStyle.WidthOverride.GetValue()  > 0.0f) ? FMath::RoundToInt(Node.WidgetStyle.WidthOverride.GetValue())
					: FMath::Max(1, GetNodeRoundedIntProperty(Node, UMGPropertyKeys::WidthOverride, 64));
				const int32 Height = (Node.WidgetStyle.HeightOverride.IsSet() && Node.WidgetStyle.HeightOverride.GetValue() > 0.0f) ? FMath::RoundToInt(Node.WidgetStyle.HeightOverride.GetValue())
					: FMath::Max(1, GetNodeRoundedIntProperty(Node, UMGPropertyKeys::HeightOverride, 64));

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
		const FString MaterialRef = FindNodePropertyOr(Node, UMGPropertyKeys::MaterialRef, Node.Brush.MaterialRef);
		if (!MaterialRef.IsEmpty())
		{
			const uint32 Key = MakeProceduralTextureKey(Node);
			if (const FString* Cached = Cache.Find(Key))
			{
				ReplaceNodeFillWithTexturePath(Node, *Cached);
			}
			else
			{
				FUMGPropertyBag GenerationProps = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
				if (UTexture2D* Generated = DesignResourceResolver::ResolveTextureFromMaterialRef(MaterialRef, GenerationProps, nullptr))
				{
					TArray64<uint8> RawData;
					int32 RawW = 0, RawH = 0;
					if (ExtractTextureRawData(Generated, RawData, RawW, RawH))
					{
						const FString SanitizedName = ObjectTools::SanitizeObjectName(Node.Name.IsEmpty() ? Node.Id : Node.Name);
						const FString SanitizedMat = ObjectTools::SanitizeObjectName(MaterialRef);
						const FString AssetName = ObjectTools::SanitizeObjectName(FString::Printf(TEXT("%s_%s_%s_%08X"), *AssetPrefix, *SanitizedName, *SanitizedMat, Key));
						FString ObjectPath;
						if (CreateOrUpdatePathTextureAsset(TextureImportPath, AssetName, RawData, RawW, RawH, TF_Bilinear, ObjectPath))
						{
							ReplaceNodeFillWithTexturePath(Node, ObjectPath);
							Cache.Add(Key, ObjectPath);
							UE_LOG(LogDesign2UMG, Log, TEXT("Persisted procedural texture '%s' (%s) -> %s"), *Node.Name, *MaterialRef, *ObjectPath);
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
