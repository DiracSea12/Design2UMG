// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "PreBuildResourceNormalizer.h"
#include "DesignResourceResolver.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "UMGWidgetTypes.h"
#include "UMGImageCropUtils.h"
#include "Design2UMGLog.h"
#include "DesignPathRasterDefaults.h"
#include "DesignPathRasterizer.h"
#include "ProceduralShapeGenerator.h"
#include "ObjectTools.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "TextureCompiler.h"
#include "TextureResource.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	void ReplaceNodeFillWithTexturePath(FUMGWidgetNode& Node, const FString& TexturePath)
	{
		Node.Path = FUMGPathData();
		Node.Brush.MaterialRef.Reset();
		Node.Brush.MaterialParams.Reset();
		Node.Brush.Shape.Reset();
		Node.Brush.Image.TexturePath = TexturePath;
		Node.Brush.Gradient = FUMGVisualBrushData::FGradientData();
	}

	// Archive bytes retain optional presence and exact values. TMap/TSet compare the
	// complete key; their hash is only an index, never resource identity.
	struct FTextureRequestKey
	{
		TArray<uint8> Bytes;

		bool operator==(const FTextureRequestKey& Other) const
		{
			return Bytes == Other.Bytes;
		}

		friend uint32 GetTypeHash(const FTextureRequestKey& Key)
		{
			return FCrc::MemCrc32(Key.Bytes.GetData(), Key.Bytes.Num());
		}
	};

	FTextureRequestKey MakePathTextureKey(const FUMGWidgetNode& Node)
	{
		FTextureRequestKey Key;
		FMemoryWriter Writer(Key.Bytes);
		FUMGPathData Path = Node.Path;
		auto Width = Node.WidgetStyle.WidthOverride;
		auto Height = Node.WidgetStyle.HeightOverride;
		Writer << Path.Geometry << Path.FillRule << Path.FillColor << Path.StrokeColor
			<< Path.StrokeThickness << Path.StrokeCap << Path.StrokeJoin << Width << Height;
		return Key;
	}

	bool HasVisiblePathPaint(const FUMGWidgetNode& Node)
	{
		const bool bHasVisibleFill = Node.Path.FillColor.IsSet()
			&& Node.Path.FillColor.GetValue().A > KINDA_SMALL_NUMBER;
		const bool bHasVisibleStroke = Node.Path.StrokeColor.IsSet()
			&& Node.Path.StrokeColor.GetValue().A > KINDA_SMALL_NUMBER
			&& Node.Path.StrokeThickness.Get(0.0f) > KINDA_SMALL_NUMBER;
		return bHasVisibleFill || bHasVisibleStroke;
	}

	FTextureRequestKey MakeProceduralTextureKey(FUMGProceduralResourceRequest Request)
	{
		FTextureRequestKey Key;
		FMemoryWriter Writer(Key.Bytes);
		Writer << Request.MaterialRef << Request.MaterialParams << Request.WidthOverride
			<< Request.HeightOverride << Request.ShadowBlur << Request.BrushColor;
		bool bHasShape = Request.Shape.IsSet();
		Writer << bHasShape;
		if (bHasShape)
		{
			auto& Shape = Request.Shape.GetValue();
			Writer << Shape.Color << Shape.Thickness << Shape.CornerRadius << Shape.CornerRadii
				<< Shape.BlurPadding << Shape.StripeColor << Shape.StripeWidth << Shape.StripeGap << Shape.Inset;
		}
		auto& Gradient = Request.Gradient;
		Writer << Gradient.Type << Gradient.Rotation << Gradient.StartPoint << Gradient.EndPoint
			<< Gradient.Center << Gradient.Size;
		int32 StopCount = Gradient.Stops.Num();
		Writer << StopCount;
		for (auto& Stop : Gradient.Stops) Writer << Stop.Position << Stop.Color;
		return Key;
	}

	FTextureRequestKey MakeImageTransformTextureKey(FString TexturePath, UMGImageCropUtils::FImageCropRequest Request)
	{
		FTextureRequestKey Key;
		FMemoryWriter Writer(Key.Bytes);
		auto& Transform = Request.Transform;
		Writer << TexturePath << Transform.A << Transform.B << Transform.TX
			<< Transform.C << Transform.D << Transform.TY << Request.OutputWidth << Request.OutputHeight;
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

	bool PersistGeneratedTexture(
		const FString& ImportPath,
		const FString& AssetStem,
		const TArray64<uint8>& RawData,
		int32 Width, int32 Height,
		const TextureFilter Filter,
		FString& OutObjectPath)
	{
		// Changed output gets a new asset. Never mutate a dependency still referenced
		// by the old WidgetTree, even when a later resource or Blueprint save fails.
		FString AssetName = FString::Printf(TEXT("%s_%dx%d_%d_%s"), *AssetStem,
			Width, Height, static_cast<int32>(Filter), *FMD5::HashBytes(RawData.GetData(), RawData.Num()));
		// Keep generated filenames compact enough for nested Windows cook paths.
		if (AssetName.Len() > 64)
		{
			const FTCHARToUTF8 Identity(*AssetName);
			int32 PrefixLength = 31;
			// Keep a UTF-16 surrogate pair intact at the truncation boundary.
			if (AssetName[PrefixLength - 1] >= 0xD800 && AssetName[PrefixLength - 1] <= 0xDBFF) --PrefixLength;
			AssetName = AssetName.Left(PrefixLength) + TEXT("_")
				+ FMD5::HashBytes(reinterpret_cast<const uint8*>(Identity.Get()), Identity.Length());
		}
		const FString LongPackageName = ImportPath / AssetName;
		if (!FPackageName::IsValidLongPackageName(LongPackageName)) return false;
		const FString ObjectPath = LongPackageName + TEXT(".") + AssetName;
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetAssetPackageExtension());
		if (UTexture2D* Existing = DesignResourceResolver::LoadTextureFromPath(ObjectPath, DesignResourceResolver::ELoadMode::LoadOnly))
		{
			TArray64<uint8> ExistingPixels;
			// A digest locates a candidate; compare its actual data before reusing it.
			if (Existing->Source.GetSizeX() != Width || Existing->Source.GetSizeY() != Height
				|| Existing->Source.GetFormat() != TSF_BGRA8 || Existing->Filter != Filter || !Existing->SRGB
				|| !Existing->Source.GetMipData(ExistingPixels, 0) || ExistingPixels != RawData
				|| !IFileManager::Get().FileExists(*PackageFilename)) return false;
			if (!EnsurePersistedTextureReady(Existing)) return false;
			OutObjectPath = ObjectPath;
			return true;
		}
		UPackage* Package = CreatePackage(*LongPackageName);
		if (!Package) return false;
		UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Texture) return false;

		bool bSucceeded = false;
		ON_SCOPE_EXIT
		{
			if (!bSucceeded)
			{
				Texture->ClearFlags(RF_Public | RF_Standalone);
				Texture->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				Texture->MarkAsGarbage();
				Package->SetDirtyFlag(false);
			}
		};
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

		// SavePackage's final file move can be fatal when its directory is missing.
		// Surface directory creation failure while the scoped asset cleanup still runs.
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true)) return false;

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError; // The caller reports failure through its structured result.
		if (!UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs)) return false;
		if (!EnsurePersistedTextureReady(Texture))
		{
			return false;
		}

		bSucceeded = true;
		FAssetRegistryModule::AssetCreated(Texture);
		OutObjectPath = ObjectPath;
		return true;
	}

	struct FTextureMaterializationPlan
	{
		enum class EKind : uint8
		{
			Path,
			Procedural,
			ImageTransform,
		};

		FTextureRequestKey Key;
		TextureFilter Filter = TF_Bilinear;
		FString AssetName;
		EKind Kind = EKind::Path;
		FUMGProceduralResourceRequest ProceduralRequest;
		FString SourceTexturePath;
		UMGImageCropUtils::FImageCropRequest ImageCropRequest;
	};

	bool TryBuildPathTextureMaterializationPlan(
		const FUMGWidgetNode& Node,
		const FString& AssetPrefix,
		FTextureMaterializationPlan& OutPlan)
	{
		const FString Geometry = Node.Path.Geometry.Get(FString());
		if (Node.Type != UMGWidgetTypes::Image || Geometry.IsEmpty())
		{
			return false;
		}
		if (!HasVisiblePathPaint(Node))
		{
			// A transparent path is a valid empty visual, not a failed texture request.
			return false;
		}

		OutPlan.Key = MakePathTextureKey(Node);
		OutPlan.Filter = TF_Nearest;
		OutPlan.Kind = FTextureMaterializationPlan::EKind::Path;
		const FString SanitizedName = ObjectTools::SanitizeObjectName(Node.Name.IsEmpty() ? Node.Id : Node.Name);
		OutPlan.AssetName = ObjectTools::SanitizeObjectName(FString::Printf(TEXT("%s_%s"), *AssetPrefix, *SanitizedName));
		return true;
	}

	UTexture2D* CreatePathTexture(const FUMGWidgetNode& Node, const FTextureMaterializationPlan&, FString& OutError,
		DesignImageResources::FImageOperationBudget* Budget)
	{
		const FString Geometry = Node.Path.Geometry.Get(FString());
		const FString FillRule = Node.Path.FillRule.Get(FString()).IsEmpty() ? DesignPathRasterDefaults::FillRule : Node.Path.FillRule.GetValue();
		const FString StrokeCap = Node.Path.StrokeCap.Get(FString()).IsEmpty() ? DesignPathRasterDefaults::StrokeCap : Node.Path.StrokeCap.GetValue();
		const FString StrokeJoin = Node.Path.StrokeJoin.Get(FString()).IsEmpty() ? DesignPathRasterDefaults::StrokeJoin : Node.Path.StrokeJoin.GetValue();
		const float StrokeThickness = Node.Path.StrokeThickness.Get(0.0f);
		const float Width = Node.WidgetStyle.WidthOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize));
		const float Height = Node.WidgetStyle.HeightOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize));
		FIntPoint RasterSize;
		if (!FDesignPathRasterizer::TryGetRasterSize(Width, Height, RasterSize)) return nullptr;

		// Preserve the rasterizer's existing byte-color precision without a string round trip.
		TOptional<FLinearColor> Fill = Node.Path.FillColor;
		if (Fill.IsSet()) Fill = FLinearColor::FromSRGBColor(Fill->ToFColor(true));
		TOptional<FLinearColor> Stroke = Node.Path.StrokeColor;
		if (Stroke.IsSet()) Stroke = FLinearColor::FromSRGBColor(Stroke->ToFColor(true));
		// The rasterizer bills this raster against the materialization budget on a cache
		// miss, i.e. exactly when it allocates; a cache hit allocates nothing and is free.
		UTexture2D* Texture = FDesignPathRasterizer::Rasterize(
			*Geometry, RasterSize.X, RasterSize.Y,
			Fill, FillRule, Stroke, StrokeThickness, StrokeCap, StrokeJoin, &OutError, Budget);
		if (!Texture && !OutError.IsEmpty())
		{
			// Name the node here: the refusal happens inside the rasterizer, which has no idea
			// which design node it was drawing for, and callers report this string verbatim.
			OutError = FString::Printf(TEXT("Path '%s': %s"), *Node.Name, *OutError);
		}
		return Texture;
	}

	bool TryBuildProceduralTextureMaterializationPlan(
		const FUMGWidgetNode& Node,
		const FString& AssetPrefix,
		FTextureMaterializationPlan& OutPlan)
	{
		FUMGProceduralResourceRequest ResourceRequest;
		if (!Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, ResourceRequest)
			|| !ResourceRequest.HasNonEmptyMaterialRef())
		{
			return false;
		}

		if (!FProceduralShapeGenerator::IsSupportedMaterialRef(ResourceRequest.MaterialRef))
		{
			const FString SubstitutePath = Node.Brush.Image.TexturePath.Get(FString()).TrimStartAndEnd();
			UTexture2D* SubstituteTexture = SubstitutePath.IsEmpty()
				? nullptr
				: FindObject<UTexture2D>(nullptr, *SubstitutePath);
			if (!SubstituteTexture && !SubstitutePath.IsEmpty())
			{
				const FString PackageName = FSoftObjectPath(SubstitutePath).GetLongPackageName();
				if (!PackageName.IsEmpty() && FPackageName::DoesPackageExist(PackageName))
				{
					SubstituteTexture = DesignResourceResolver::LoadTextureFromPath(
						SubstitutePath, DesignResourceResolver::ELoadMode::LoadThenFind);
				}
			}

			if (SubstituteTexture)
			{
				// Only a loaded or loadable texture is a valid external-material substitute.
				return false;
			}
		}

		OutPlan.Key = MakeProceduralTextureKey(ResourceRequest);
		OutPlan.Filter = TF_Bilinear;
		OutPlan.Kind = FTextureMaterializationPlan::EKind::Procedural;
		OutPlan.ProceduralRequest = ResourceRequest;
		const FString SanitizedName = ObjectTools::SanitizeObjectName(Node.Name.IsEmpty() ? Node.Id : Node.Name);
		const FString SanitizedMat = ObjectTools::SanitizeObjectName(ResourceRequest.MaterialRef);
		OutPlan.AssetName = ObjectTools::SanitizeObjectName(FString::Printf(TEXT("%s_%s_%s"), *AssetPrefix, *SanitizedName, *SanitizedMat));
		return true;
	}

	UTexture2D* CreateProceduralTexture(const FUMGWidgetNode&, const FTextureMaterializationPlan& Plan, FString& OutError,
		DesignImageResources::FImageOperationBudget* Budget)
	{
		return FProceduralShapeGenerator::GenerateFromMaterialRef(Plan.ProceduralRequest, nullptr, &OutError, Budget);
	}

	bool TryBuildImageTransformTextureMaterializationPlan(
		const FUMGWidgetNode& Node,
		const FString& AssetPrefix,
		FTextureMaterializationPlan& OutPlan)
	{
		const FString TexturePath = Node.Brush.Image.TexturePath.Get(FString()).TrimStartAndEnd();
		if (TexturePath.IsEmpty()
			|| !UMGImageCropUtils::TryBuildCropRequest(Node, OutPlan.ImageCropRequest)
			|| UMGImageCropUtils::CanUseUvRegion(OutPlan.ImageCropRequest.Transform))
		{
			return false;
		}

		OutPlan.Key = MakeImageTransformTextureKey(TexturePath, OutPlan.ImageCropRequest);
		OutPlan.Filter = TF_Bilinear;
		OutPlan.Kind = FTextureMaterializationPlan::EKind::ImageTransform;
		OutPlan.SourceTexturePath = TexturePath;
		const FString SanitizedName = ObjectTools::SanitizeObjectName(Node.Name.IsEmpty() ? Node.Id : Node.Name);
		OutPlan.AssetName = ObjectTools::SanitizeObjectName(
			FString::Printf(TEXT("%s_%s_ImageTransform"), *AssetPrefix, *SanitizedName));
		return true;
	}

	UTexture2D* CreateImageTransformTexture(const FUMGWidgetNode&, const FTextureMaterializationPlan& Plan, FString& OutError,
		DesignImageResources::FImageOperationBudget* Budget)
	{
		UTexture2D* SourceTexture = DesignResourceResolver::LoadTextureFromPath(
			Plan.SourceTexturePath,
			DesignResourceResolver::ELoadMode::LoadThenFind);
		if (!SourceTexture)
		{
			return nullptr;
		}

		const FUMGVisualBrushData::FImageTransformData& Transform = Plan.ImageCropRequest.Transform;
		return FProceduralShapeGenerator::ApplyAffineCrop(
			SourceTexture,
			Transform.A,
			Transform.B,
			Transform.TX,
			Transform.C,
			Transform.D,
			Transform.TY,
			Plan.ImageCropRequest.OutputWidth,
			Plan.ImageCropRequest.OutputHeight, &OutError, Budget);
	}

	void ApplyMaterializedTextureToNode(
		FUMGWidgetNode& Node,
		const FString& TexturePath,
		const FTextureMaterializationPlan::EKind Kind)
	{
		ReplaceNodeFillWithTexturePath(Node, TexturePath);
		if (Kind == FTextureMaterializationPlan::EKind::ImageTransform)
		{
			Node.Brush.Image.Transform.Reset();
			Node.Brush.Image.Mode.Reset();
		}
	}

	bool NormalizeTextureResourcesRecursive(
		FUMGWidgetNode& Node, const FString& TextureImportPath, const FString& AssetPrefix,
		TMap<FTextureRequestKey, FString>& Cache,
		TFunctionRef<bool(const FUMGWidgetNode&, const FString&, FTextureMaterializationPlan&)> TryBuildPlan,
		TFunctionRef<UTexture2D*(const FUMGWidgetNode&, const FTextureMaterializationPlan&, FString&, DesignImageResources::FImageOperationBudget* Budget)> CreateTexture,
		DesignImageResources::FImageOperationBudget* Budget,
		FString* OutError)
	{
		FTextureMaterializationPlan Plan;
		if (TryBuildPlan(Node, AssetPrefix, Plan))
		{
			if (const FString* Cached = Cache.Find(Plan.Key))
			{
				ApplyMaterializedTextureToNode(Node, *Cached, Plan.Kind);
			}
			else
			{
				FString CreationError;
				UTexture2D* Generated = CreateTexture(Node, Plan, CreationError, Budget);
				if (!CreationError.IsEmpty())
				{
					const FString Error = FString::Printf(TEXT("Failed to create generated texture for '%s': %s"), *Node.Name, *CreationError);
					if (OutError) *OutError = Error;
					UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Error);
					return false;
				}
				if (Generated)
				{
					// This intermediate is not attached to a widget: only its persisted copy is.
					ON_SCOPE_EXIT { FProceduralShapeGenerator::ReleaseTransientTexture(Generated); };
					TArray64<uint8> RawData;
					int32 RawW = 0, RawH = 0;
					FString ObjectPath;
					const TextureFilter Filter = Plan.Kind == FTextureMaterializationPlan::EKind::Procedural
						? Generated->Filter.GetValue() : Plan.Filter;
					if (!ExtractTextureRawData(Generated, RawData, RawW, RawH)
						|| !PersistGeneratedTexture(TextureImportPath, Plan.AssetName, RawData, RawW, RawH, Filter, ObjectPath))
					{
						const FString Error = FString::Printf(TEXT("Failed to persist generated texture for '%s': %s/%s"),
							*Node.Name, *TextureImportPath, *Plan.AssetName);
						if (OutError) *OutError = Error;
						UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Error);
						return false;
					}
					ApplyMaterializedTextureToNode(Node, ObjectPath, Plan.Kind);
					Cache.Add(Plan.Key, ObjectPath);
					UE_LOG(LogDesign2UMG, Log, TEXT("Persisted generated texture '%s' -> %s"), *Node.Name, *ObjectPath);
				}
				else
				{
					// A planned resource that produces neither a texture nor an error is still a failure.
					const FString Error = FString::Printf(
						TEXT("Required generated texture for node '%s' ('%s') was not produced and reported no error"),
						*Node.Name, *Plan.AssetName);
					if (OutError) *OutError = Error;
					UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Error);
					return false;
				}
			}
		}
		for (FUMGWidgetNode& Child : Node.Children)
		{
			if (!NormalizeTextureResourcesRecursive(Child, TextureImportPath, AssetPrefix, Cache, TryBuildPlan, CreateTexture, Budget, OutError)) return false;
		}
		return true;
	}
}

bool FPreBuildResourceNormalizer::NormalizeGeneratedTextures(
	FUMGWidgetNode& Root, const FString& PackagePath, const FString& AssetPrefix, FString& OutError, DesignImageResources::FImageOperationBudget* Budget)
{
	DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
	if (!Budget) Budget = &LocalBudget;
	return NormalizePathTextures(Root, PackagePath / TEXT("D2U_PathTextures"), AssetPrefix, &OutError, Budget)
		&& NormalizeProceduralTextures(Root, PackagePath / TEXT("D2U_ProceduralTextures"), AssetPrefix, &OutError, Budget)
		&& NormalizeImageTransformTextures(Root, PackagePath / TEXT("D2U_ImageTransforms"), AssetPrefix, &OutError, Budget);
}

bool FPreBuildResourceNormalizer::NormalizePathTextures(
	FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError,
	DesignImageResources::FImageOperationBudget* Budget)
{
	if (OutError) OutError->Reset();
	DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
	if (!Budget) Budget = &LocalBudget;
	TMap<FTextureRequestKey, FString> Cache;
	return NormalizeTextureResourcesRecursive(Root, TextureImportPath, AssetPrefix, Cache,
		TryBuildPathTextureMaterializationPlan, CreatePathTexture, Budget, OutError);
}

bool FPreBuildResourceNormalizer::NormalizeProceduralTextures(
	FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError, DesignImageResources::FImageOperationBudget* Budget)
{
	DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
	if (!Budget) Budget = &LocalBudget;
	if (OutError) OutError->Reset();
	TMap<FTextureRequestKey, FString> Cache;
	return NormalizeTextureResourcesRecursive(Root, TextureImportPath, AssetPrefix, Cache,
		TryBuildProceduralTextureMaterializationPlan, CreateProceduralTexture, Budget, OutError);
}

bool FPreBuildResourceNormalizer::NormalizeImageTransformTextures(
	FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError, DesignImageResources::FImageOperationBudget* Budget)
{
	DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
	if (!Budget) Budget = &LocalBudget;
	if (OutError) OutError->Reset();
	TMap<FTextureRequestKey, FString> Cache;
	return NormalizeTextureResourcesRecursive(Root, TextureImportPath, AssetPrefix, Cache,
		TryBuildImageTransformTextureMaterializationPlan, CreateImageTransformTexture, Budget, OutError);
}

bool FPreBuildResourceNormalizer::ValidateGeneratedTextureRequests(
	const FUMGWidgetNode& Root,
	FString& OutError)
{
	OutError.Reset();
	const DesignImageResources::FImageResourceLimits Limits = DesignImageResources::GetGeneratedTextureLimits();
	TFunction<bool(const FUMGWidgetNode&)> Visit = [&](const FUMGWidgetNode& Node)
	{
		if (Node.Type == UMGWidgetTypes::Image
			&& !Node.Path.Geometry.Get(FString()).IsEmpty()
			&& HasVisiblePathPaint(Node))
		{
			FIntPoint RasterSize;
			if (!FDesignPathRasterizer::TryGetRasterSize(
				Node.WidgetStyle.WidthOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize)),
				Node.WidgetStyle.HeightOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize)),
				RasterSize))
			{
				OutError = FString::Printf(
					TEXT("Failed to create generated texture for '%s': invalid or excessive path texture dimensions"),
					*Node.Name);
				return false;
			}
		}

		FUMGProceduralResourceRequest ResourceRequest;
		if (Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, ResourceRequest)
			&& ResourceRequest.HasNonEmptyMaterialRef()
			&& FProceduralShapeGenerator::IsSupportedMaterialRef(ResourceRequest.MaterialRef))
		{
			FIntPoint Size;
			if (!Limits.TryGetPixelSize(
				ResourceRequest.WidthOverride.Get(64.0f),
				ResourceRequest.HeightOverride.Get(64.0f), Size))
			{
				OutError = FString::Printf(
					TEXT("Failed to create generated texture for '%s': invalid or excessive procedural texture dimensions"),
					*Node.Name);
				return false;
			}
		}

		UMGImageCropUtils::FImageCropRequest CropRequest;
		if (Node.Brush.Image.Transform.IsSet()
			&& Node.Brush.Image.Mode.Get(FString()).Equals(TEXT("Crop"), ESearchCase::IgnoreCase)
			&& UMGImageCropUtils::TryBuildCropRequest(Node, CropRequest)
			&& !UMGImageCropUtils::CanUseUvRegion(CropRequest.Transform)
			&& !Limits.AllowsDimensions(CropRequest.OutputWidth, CropRequest.OutputHeight))
		{
			OutError = FString::Printf(
				TEXT("Failed to create generated texture for '%s': invalid or excessive affine crop dimensions"),
				*Node.Name);
			return false;
		}

		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (!Visit(Child)) return false;
		}
		return true;
	};
	return Visit(Root);
}
