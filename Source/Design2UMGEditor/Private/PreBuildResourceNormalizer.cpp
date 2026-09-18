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
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "TextureCompiler.h"
#include "TextureResource.h"
#include "UObject/SavePackage.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
	bool GFailTextureSaveOnce = false;
#endif
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
	using FTextureRequestKey = TArray<uint8>;

	FTextureRequestKey MakePathTextureKey(const FUMGWidgetNode& Node)
	{
		FTextureRequestKey Key;
		FMemoryWriter Writer(Key);
		FUMGPathData Path = Node.Path;
		auto Width = Node.WidgetStyle.WidthOverride;
		auto Height = Node.WidgetStyle.HeightOverride;
		Writer << Path.Geometry << Path.FillRule << Path.FillColor << Path.StrokeColor
			<< Path.StrokeThickness << Path.StrokeCap << Path.StrokeJoin << Width << Height;
		return Key;
	}

	FTextureRequestKey MakeProceduralTextureKey(FUMGProceduralResourceRequest Request)
	{
		FTextureRequestKey Key;
		FMemoryWriter Writer(Key);
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
		FMemoryWriter Writer(Key);
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
		const FString AssetName = FString::Printf(TEXT("%s_%dx%d_%d_%s"), *AssetStem,
			Width, Height, static_cast<int32>(Filter), *FMD5::HashBytes(RawData.GetData(), RawData.Num()));
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

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
#if WITH_DEV_AUTOMATION_TESTS
		if (GFailTextureSaveOnce)
		{
			GFailTextureSaveOnce = false;
			return false;
		}
#endif
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

		OutPlan.Key = MakePathTextureKey(Node);
		OutPlan.Filter = TF_Nearest;
		OutPlan.Kind = FTextureMaterializationPlan::EKind::Path;
		const FString SanitizedName = ObjectTools::SanitizeObjectName(Node.Name.IsEmpty() ? Node.Id : Node.Name);
		OutPlan.AssetName = ObjectTools::SanitizeObjectName(FString::Printf(TEXT("%s_%s"), *AssetPrefix, *SanitizedName));
		return true;
	}

	UTexture2D* CreatePathTexture(const FUMGWidgetNode& Node, const FTextureMaterializationPlan&, FString& OutError)
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
		return FDesignPathRasterizer::Rasterize(
			*Geometry, RasterSize.X, RasterSize.Y,
			Fill, FillRule, Stroke, StrokeThickness, StrokeCap, StrokeJoin, &OutError);
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

		OutPlan.Key = MakeProceduralTextureKey(ResourceRequest);
		OutPlan.Filter = TF_Bilinear;
		OutPlan.Kind = FTextureMaterializationPlan::EKind::Procedural;
		OutPlan.ProceduralRequest = ResourceRequest;
		const FString SanitizedName = ObjectTools::SanitizeObjectName(Node.Name.IsEmpty() ? Node.Id : Node.Name);
		const FString SanitizedMat = ObjectTools::SanitizeObjectName(ResourceRequest.MaterialRef);
		OutPlan.AssetName = ObjectTools::SanitizeObjectName(FString::Printf(TEXT("%s_%s_%s"), *AssetPrefix, *SanitizedName, *SanitizedMat));
		return true;
	}

	UTexture2D* CreateProceduralTexture(const FUMGWidgetNode&, const FTextureMaterializationPlan& Plan, FString& OutError)
	{
		return FProceduralShapeGenerator::GenerateFromMaterialRef(Plan.ProceduralRequest, nullptr, &OutError);
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

	UTexture2D* CreateImageTransformTexture(const FUMGWidgetNode&, const FTextureMaterializationPlan& Plan, FString& OutError)
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
			Plan.ImageCropRequest.OutputHeight, &OutError);
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
		TFunctionRef<UTexture2D*(const FUMGWidgetNode&, const FTextureMaterializationPlan&, FString&)> CreateTexture,
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
				UTexture2D* Generated = CreateTexture(Node, Plan, CreationError);
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
			}
		}
		for (FUMGWidgetNode& Child : Node.Children)
		{
			if (!NormalizeTextureResourcesRecursive(Child, TextureImportPath, AssetPrefix, Cache, TryBuildPlan, CreateTexture, OutError)) return false;
		}
		return true;
	}
}

bool FPreBuildResourceNormalizer::NormalizePathTextures(
	FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError)
{
	if (OutError) OutError->Reset();
	TMap<FTextureRequestKey, FString> Cache;
	return NormalizeTextureResourcesRecursive(Root, TextureImportPath, AssetPrefix, Cache,
		TryBuildPathTextureMaterializationPlan, CreatePathTexture, OutError);
}

bool FPreBuildResourceNormalizer::NormalizeProceduralTextures(
	FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError)
{
	if (OutError) OutError->Reset();
	TMap<FTextureRequestKey, FString> Cache;
	return NormalizeTextureResourcesRecursive(Root, TextureImportPath, AssetPrefix, Cache,
		TryBuildProceduralTextureMaterializationPlan, CreateProceduralTexture, OutError);
}

bool FPreBuildResourceNormalizer::NormalizeImageTransformTextures(
	FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError)
{
	if (OutError) OutError->Reset();
	TMap<FTextureRequestKey, FString> Cache;
	return NormalizeTextureResourcesRecursive(Root, TextureImportPath, AssetPrefix, Cache,
		TryBuildImageTransformTextureMaterializationPlan, CreateImageTransformTexture, OutError);
}


bool FPreBuildResourceNormalizer::ValidatePathTextureBudget(const FUMGWidgetNode& Root, FString& OutError)
{
	OutError.Reset();
	const DesignImageResources::FImageResourceLimits Limits;
	DesignImageResources::FImageOperationBudget Budget(Limits);
	TSet<FTextureRequestKey> CountedRequests;
	TFunction<bool(const FUMGWidgetNode&)> Visit = [&](const FUMGWidgetNode& Node)
	{
		if (Node.Type == UMGWidgetTypes::Image && !Node.Path.Geometry.Get(FString()).IsEmpty())
		{
			FIntPoint Size;
			if (!FDesignPathRasterizer::TryGetRasterSize(
				Node.WidgetStyle.WidthOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize)),
				Node.WidgetStyle.HeightOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize)), Size))
			{
				OutError = FString::Printf(TEXT("Path '%s' has invalid or excessive texture dimensions"), *Node.Name);
				return false;
			}
			const FTextureRequestKey Key = MakePathTextureKey(Node);
			if (!CountedRequests.Contains(Key)
				&& !Budget.ConsumeDecodedPixels(static_cast<int64>(Size.X) * Size.Y, OutError))
			{
				OutError = FString::Printf(TEXT("Path '%s': %s"), *Node.Name, *OutError);
				return false;
			}
			CountedRequests.Add(Key);
		}
		for (const FUMGWidgetNode& Child : Node.Children) if (!Visit(Child)) return false;
		return true;
	};
	return Visit(Root);
}

#if WITH_DEV_AUTOMATION_TESTS
void FPreBuildResourceNormalizer::ArmTextureSaveFailureOnce()
{
	GFailTextureSaveOnce = true;
}
#endif
