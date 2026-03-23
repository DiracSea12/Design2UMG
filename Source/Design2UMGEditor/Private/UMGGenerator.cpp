// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGGenerator.h"
#include "UMGWidgetUtils.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "DesignPathRasterizer.h"
#include "ProceduralShapeGenerator.h"
#include "ObjectTools.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "TextureResource.h"
#include "UObject/SavePackage.h"

namespace
{
	void DumpTemplateTextBlockFontState(const TCHAR* Stage, UWidgetBlueprint* WidgetBlueprint)
	{
		if (!UE_LOG_ACTIVE(LogDesign2UMG, Verbose) || !WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return;
		}

		WidgetBlueprint->WidgetTree->ForEachWidget([Stage, WidgetBlueprint](UWidget* Widget)
		{
			UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
			if (!TextBlock)
			{
				return;
			}

			const FSlateFontInfo FontInfo = TextBlock->GetFont();
			const UObject* FontObject = FontInfo.FontObject;
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("TextFont[%s]: blueprint='%s' widget='%s' text='%s' fontObject='%s' typeface='%s' size=%.1f tracking=%d wrap=%.1f autoWrap=%d"),
				Stage,
				*WidgetBlueprint->GetPathName(),
				*TextBlock->GetName(),
				*TextBlock->GetText().ToString(),
				FontObject ? *FontObject->GetPathName() : TEXT("<null>"),
				*FontInfo.TypefaceFontName.ToString(),
				FontInfo.Size,
				FontInfo.LetterSpacing,
				TextBlock->GetWrapTextAt(),
				TextBlock->GetAutoWrapText() ? 1 : 0);
		});
	}

	void ApplyDesignPreviewSize(UWidgetBlueprint* WBP, const FIntPoint& DesignSize, const TCHAR* Phase)
	{
		if (!WBP)
		{
			return;
		}

		UClass* GeneratedClass = WBP->GeneratedClass;
		if (!GeneratedClass)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ApplyDesignPreviewSize(%s): GeneratedClass is null"), Phase ? Phase : TEXT("unknown"));
			return;
		}

		UUserWidget* CDO = Cast<UUserWidget>(GeneratedClass->GetDefaultObject());
		if (!CDO)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ApplyDesignPreviewSize(%s): Failed to get UUserWidget CDO"), Phase ? Phase : TEXT("unknown"));
			return;
		}

		CDO->DesignTimeSize = FVector2D(DesignSize.X, DesignSize.Y);
		CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
		UE_LOG(LogDesign2UMG, Log, TEXT("Set DesignTimeSize to %.0fx%.0f (mode=Custom, phase=%s)"),
			CDO->DesignTimeSize.X, CDO->DesignTimeSize.Y, Phase ? Phase : TEXT("unknown"));
	}

	FIntPoint ReadDesignPreviewSize(UWidgetBlueprint* WBP)
	{
		if (!WBP || !WBP->GeneratedClass)
		{
			return FIntPoint::ZeroValue;
		}

		if (UUserWidget* CDO = Cast<UUserWidget>(WBP->GeneratedClass->GetDefaultObject()))
		{
			const FVector2D CurrentSize = CDO->DesignTimeSize;
			if (CurrentSize.X > 1.0f && CurrentSize.Y > 1.0f)
			{
				return FIntPoint(
					FMath::RoundToInt(CurrentSize.X),
					FMath::RoundToInt(CurrentSize.Y));
			}
		}

		return FIntPoint::ZeroValue;
	}

	FUMGSlotData MakeCanvasFillSlot()
	{
		FUMGSlotData Slot;
		Slot.Anchors.Min.X = 0.0f;
		Slot.Anchors.Min.Y = 0.0f;
		Slot.Anchors.Max.X = 1.0f;
		Slot.Anchors.Max.Y = 1.0f;
		Slot.Alignment.X = 0.0f;
		Slot.Alignment.Y = 0.0f;
		Slot.Offsets.Left = 0.0f;
		Slot.Offsets.Top = 0.0f;
		Slot.Offsets.Right = 0.0f;
		Slot.Offsets.Bottom = 0.0f;
		Slot.bAutoSize = false;
		return Slot;
	}

	FUMGWidgetNode WrapRootForViewport(
		FUMGWidgetNode&& InRoot,
		const FIntPoint& ViewportSize,
		const FIntPoint& ContentSize,
		bool bDownOnly)
	{
		FUMGWidgetNode SizeBoxNode;
		SizeBoxNode.Type = TEXT("SizeBox");
		SizeBoxNode.Id = TEXT("__d2u_root_sizebox");
		SizeBoxNode.Name = TEXT("D2U_RootSizeBox");
		SizeBoxNode.Bounds.X = 0.0f;
		SizeBoxNode.Bounds.Y = 0.0f;
		SizeBoxNode.Bounds.W = static_cast<float>(ContentSize.X);
		SizeBoxNode.Bounds.H = static_cast<float>(ContentSize.Y);
		SizeBoxNode.Properties.Add(TEXT("WidthOverride"), FString::FromInt(ContentSize.X));
		SizeBoxNode.Properties.Add(TEXT("HeightOverride"), FString::FromInt(ContentSize.Y));
		SizeBoxNode.Children.Add(MoveTemp(InRoot));

		FUMGWidgetNode ScaleBoxNode;
		ScaleBoxNode.Type = TEXT("ScaleBox");
		ScaleBoxNode.Id = TEXT("__d2u_root_scalebox");
		ScaleBoxNode.Name = TEXT("D2U_RootScaleBox");
		ScaleBoxNode.Bounds.X = 0.0f;
		ScaleBoxNode.Bounds.Y = 0.0f;
		ScaleBoxNode.Bounds.W = static_cast<float>(ViewportSize.X);
		ScaleBoxNode.Bounds.H = static_cast<float>(ViewportSize.Y);
		ScaleBoxNode.Slot = MakeCanvasFillSlot();
		ScaleBoxNode.Properties.Add(TEXT("Stretch"), TEXT("ScaleToFit"));
		ScaleBoxNode.Properties.Add(TEXT("StretchDirection"), bDownOnly ? TEXT("DownOnly") : TEXT("Both"));
		ScaleBoxNode.Children.Add(MoveTemp(SizeBoxNode));

		FUMGWidgetNode CanvasRoot;
		CanvasRoot.Type = TEXT("CanvasPanel");
		CanvasRoot.Id = TEXT("__d2u_viewport_root");
		CanvasRoot.Name = TEXT("D2U_ViewportRoot");
		CanvasRoot.Bounds.X = 0.0f;
		CanvasRoot.Bounds.Y = 0.0f;
		CanvasRoot.Bounds.W = static_cast<float>(ViewportSize.X);
		CanvasRoot.Bounds.H = static_cast<float>(ViewportSize.Y);
		CanvasRoot.Children.Add(MoveTemp(ScaleBoxNode));
		return CanvasRoot;
	}

	FString FindNodeProperty(const FUMGWidgetNode& Node, const TCHAR* Key)
	{
		FString Value;
		Node.Properties.TryGetString(Key, Value);
		return Value;
	}

	FString FindNodePropertyOr(const FUMGWidgetNode& Node, const TCHAR* Key, const FString& DefaultValue)
	{
		FString Value;
		return (Node.Properties.TryGetString(Key, Value) && !Value.IsEmpty()) ? Value : DefaultValue;
	}

	int32 GetNodeRoundedIntProperty(const FUMGWidgetNode& Node, const TCHAR* Key, int32 DefaultValue)
	{
		float Value = 0.0f;
		if (Node.Properties.TryGetFloat(Key, Value))
		{
			return FMath::RoundToInt(Value);
		}
		return DefaultValue;
	}

	void ReplaceNodeFillWithTexturePath(FUMGWidgetNode& Node, const FString& TexturePath)
	{
		Node.Properties.Remove(UMGPropertyKeys::PathGeometry);
		Node.Properties.Remove(UMGPropertyKeys::PathFillRule);
		Node.Properties.Remove(UMGPropertyKeys::PathFillColor);
		Node.Properties.Remove(UMGPropertyKeys::MaterialRef);
		Node.Properties.Remove(UMGPropertyKeys::MaterialParams);
		Node.Properties.Add(UMGPropertyKeys::TexturePath, TexturePath);
		Node.Brush.MaterialRef.Reset();
		Node.Brush.MaterialParams.Reset();
		Node.Brush.TexturePath = TexturePath;
	}

	uint32 MakePathTextureKey(const FUMGWidgetNode& Node)
	{
		FString Geometry;
		FString FillRule;
		FString FillColor;
		FString WidthOverride;
		FString HeightOverride;
		Node.Properties.TryGetString(UMGPropertyKeys::PathGeometry, Geometry);
		Node.Properties.TryGetString(UMGPropertyKeys::PathFillRule, FillRule);
		Node.Properties.TryGetString(UMGPropertyKeys::PathFillColor, FillColor);
		Node.Properties.TryGetString(UMGPropertyKeys::WidthOverride, WidthOverride);
		Node.Properties.TryGetString(UMGPropertyKeys::HeightOverride, HeightOverride);

		uint32 Key = Geometry.IsEmpty() ? 0u : GetTypeHash(Geometry);
		Key = HashCombine(Key, FillRule.IsEmpty() ? 0u : GetTypeHash(FillRule));
		Key = HashCombine(Key, FillColor.IsEmpty() ? 0u : GetTypeHash(FillColor));
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

	bool ApplyPathTextureUiSettings(UTexture2D* Texture)
	{
		if (!Texture)
		{
			return false;
		}

		Texture->SRGB = true;
		Texture->NeverStream = true;
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		return true;
	}

	bool ExtractTextureRawData(UTexture2D* Texture, TArray64<uint8>& OutRawData, int32& OutWidth, int32& OutHeight)
	{
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() <= 0)
		{
			return false;
		}

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		OutWidth = Mip.SizeX;
		OutHeight = Mip.SizeY;
		if (OutWidth <= 0 || OutHeight <= 0)
		{
			return false;
		}

		const int64 ExpectedBytes = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4;
		const void* LockedData = Mip.BulkData.LockReadOnly();
		if (!LockedData)
		{
			Mip.BulkData.Unlock();
			return false;
		}

		OutRawData.SetNumUninitialized(ExpectedBytes);
		FMemory::Memcpy(OutRawData.GetData(), LockedData, ExpectedBytes);
		Mip.BulkData.Unlock();
		return true;
	}

	bool CreateOrUpdatePathTextureAsset(
		const FString& ImportPath,
		const FString& AssetName,
		const TArray64<uint8>& RawData,
		int32 Width,
		int32 Height,
		FString& OutObjectPath)
	{
		const FString LongPackageName = ImportPath / AssetName;
		if (!FPackageName::IsValidLongPackageName(LongPackageName))
		{
			return false;
		}

		const FString ObjectPath = LongPackageName + TEXT(".") + AssetName;
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
		const bool bNewAsset = (Texture == nullptr);
		if (!Texture)
		{
			UPackage* Package = CreatePackage(*LongPackageName);
			if (!Package)
			{
				return false;
			}

			Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
			if (!Texture)
			{
				return false;
			}
		}

		UPackage* Package = Texture->GetOutermost();
		if (!Package)
		{
			return false;
		}

		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, RawData.GetData());
		ApplyPathTextureUiSettings(Texture);
		Texture->PostEditChange();
		Texture->UpdateResource();
		Texture->MarkPackageDirty();
		Package->MarkPackageDirty();

		if (bNewAsset)
		{
			FAssetRegistryModule::AssetCreated(Texture);
		}

		const FString PackageFilename = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs))
		{
			return false;
		}

		OutObjectPath = ObjectPath;
		return true;
	}

	void PersistPathTexturesInNodeTree(
		FUMGWidgetNode& Node,
		const FString& TextureImportPath,
		const FString& AssetPrefix,
		TMap<uint32, FString>& PathTextureCache)
	{
		FString Geometry;
		if (Node.Type == TEXT("Image") && Node.Properties.TryGetString(UMGPropertyKeys::PathGeometry, Geometry) && !Geometry.IsEmpty())
		{
			const uint32 TextureKey = MakePathTextureKey(Node);
			if (const FString* CachedPath = PathTextureCache.Find(TextureKey))
			{
				ReplaceNodeFillWithTexturePath(Node, *CachedPath);
			}
			else
			{
				const FString FillRule = FindNodePropertyOr(Node, UMGPropertyKeys::PathFillRule, TEXT("nonzero"));
				const FString FillColor = FindNodePropertyOr(Node, UMGPropertyKeys::PathFillColor, TEXT("#ffffffff"));
				const int32 Width = FMath::Max(1, GetNodeRoundedIntProperty(Node, UMGPropertyKeys::WidthOverride, 64));
				const int32 Height = FMath::Max(1, GetNodeRoundedIntProperty(Node, UMGPropertyKeys::HeightOverride, 64));

				if (!Geometry.IsEmpty())
				{
					constexpr int32 PathSupersample = 2;
					const int32 RasterWidth = Width * PathSupersample;
					const int32 RasterHeight = Height * PathSupersample;
					if (UTexture2D* Rasterized = FDesignPathRasterizer::Rasterize(*Geometry, RasterWidth, RasterHeight, FUMGValueParser::ParseColor(FillColor), FillRule))
					{
						TArray64<uint8> RawData;
						int32 RawWidth = 0;
						int32 RawHeight = 0;
						if (ExtractTextureRawData(Rasterized, RawData, RawWidth, RawHeight))
						{
							const FString SanitizedNodeName = ObjectTools::SanitizeObjectName(Node.Name.IsEmpty() ? Node.Id : Node.Name);
							const FString AssetName = ObjectTools::SanitizeObjectName(
								FString::Printf(TEXT("%s_%s_%08X"), *AssetPrefix, *SanitizedNodeName, TextureKey));

							FString ObjectPath;
							if (CreateOrUpdatePathTextureAsset(TextureImportPath, AssetName, RawData, RawWidth, RawHeight, ObjectPath))
							{
								ReplaceNodeFillWithTexturePath(Node, ObjectPath);
								PathTextureCache.Add(TextureKey, ObjectPath);
								UE_LOG(LogDesign2UMG, Log, TEXT("Persisted path texture '%s' -> %s"), *Node.Name, *ObjectPath);
							}
						}
					}
				}
			}
		}

		for (FUMGWidgetNode& Child : Node.Children)
		{
			PersistPathTexturesInNodeTree(Child, TextureImportPath, AssetPrefix, PathTextureCache);
		}
	}

	void PersistProceduralTexturesInNodeTree(
		FUMGWidgetNode& Node,
		const FString& TextureImportPath,
		const FString& AssetPrefix,
		TMap<uint32, FString>& ProceduralTextureCache)
	{
		const FString MaterialRef = FindNodePropertyOr(Node, UMGPropertyKeys::MaterialRef, Node.Brush.MaterialRef);
		if (!MaterialRef.IsEmpty())
		{
			const uint32 TextureKey = MakeProceduralTextureKey(Node);
			if (const FString* CachedPath = ProceduralTextureCache.Find(TextureKey))
			{
				ReplaceNodeFillWithTexturePath(Node, *CachedPath);
			}
			else
			{
				FUMGPropertyBag GenerationProps = Node.Properties;
				GenerationProps.Add(UMGPropertyKeys::MaterialRef, MaterialRef);
				FString ExistingMaterialParams;
				if (!Node.Brush.MaterialParams.IsEmpty()
					&& !(GenerationProps.TryGetString(UMGPropertyKeys::MaterialParams, ExistingMaterialParams) && !ExistingMaterialParams.IsEmpty()))
				{
					GenerationProps.Add(UMGPropertyKeys::MaterialParams, Node.Brush.MaterialParams);
				}

				if (UTexture2D* Generated = FProceduralShapeGenerator::GenerateFromMaterialRef(MaterialRef, GenerationProps, nullptr))
				{
					TArray64<uint8> RawData;
					int32 RawWidth = 0;
					int32 RawHeight = 0;
					if (ExtractTextureRawData(Generated, RawData, RawWidth, RawHeight))
					{
						const FString SanitizedNodeName = ObjectTools::SanitizeObjectName(Node.Name.IsEmpty() ? Node.Id : Node.Name);
						const FString SanitizedMaterialRef = ObjectTools::SanitizeObjectName(MaterialRef);
						const FString AssetName = ObjectTools::SanitizeObjectName(
							FString::Printf(TEXT("%s_%s_%s_%08X"), *AssetPrefix, *SanitizedNodeName, *SanitizedMaterialRef, TextureKey));

						FString ObjectPath;
						if (CreateOrUpdatePathTextureAsset(TextureImportPath, AssetName, RawData, RawWidth, RawHeight, ObjectPath))
						{
							ReplaceNodeFillWithTexturePath(Node, ObjectPath);
							ProceduralTextureCache.Add(TextureKey, ObjectPath);
							UE_LOG(LogDesign2UMG, Log, TEXT("Persisted procedural texture '%s' (%s) -> %s"), *Node.Name, *MaterialRef, *ObjectPath);
						}
					}
				}
			}
		}

		for (FUMGWidgetNode& Child : Node.Children)
		{
			PersistProceduralTexturesInNodeTree(Child, TextureImportPath, AssetPrefix, ProceduralTextureCache);
		}
	}
}

UWidgetBlueprint* FUMGGenerator::GenerateWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath)
{
	UWidgetBlueprint* WBP = BuildWidgetBlueprint(WidgetData, PackagePath);
	if (WBP)
		SaveWidgetBlueprint(WBP);
	return WBP;
}

UWidgetBlueprint* FUMGGenerator::BuildWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath)
{
	FString AssetName = ObjectTools::SanitizeObjectName(WidgetData.WidgetName);
	FString FullPath = PackagePath / AssetName;

	// 检查资产冲突
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	FString TestPath = FullPath + TEXT(".") + AssetName;
	if (AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid())
	{
		int32 Suffix = 1;
		FString NewName;
		constexpr int32 MaxAttempts = 1000;

		do {
			NewName = FString::Printf(TEXT("%s_%d"), *AssetName, Suffix);
			TestPath = PackagePath / NewName + TEXT(".") + NewName;
			++Suffix;
		} while (AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid() && Suffix <= MaxAttempts);

		if (Suffix > MaxAttempts && AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid())
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to find unique name for asset '%s'"), *WidgetData.WidgetName);
			UE_LOG(LogDesign2UMG, Error, TEXT("无法为资产 '%s' 生成唯一名称。请清理输出目录后重试。"), *WidgetData.WidgetName);
			return nullptr;
		}

		AssetName = NewName;
		FullPath = PackagePath / AssetName;
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Asset renamed to %s"), *AssetName);
		UE_LOG(LogDesign2UMG, Warning, TEXT("输出路径已存在同名资产，自动重命名为 %s"), *AssetName);
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to create package at %s"), *FullPath);
		UE_LOG(LogDesign2UMG, Error, TEXT("无法创建资产包。请检查输出路径是否有效: %s"), *FullPath);
		return nullptr;
	}

	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(
		FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(), Package, FName(*AssetName),
			BPTYPE_Normal, UWidgetBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(), NAME_None
		)
	);

	if (!WBP || !WBP->WidgetTree)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to create Widget Blueprint"));
		UE_LOG(LogDesign2UMG, Error, TEXT("无法创建 Widget Blueprint。请检查引擎版本兼容性。"));
		Package->ClearDirtyFlag();
		Package->SetFlags(RF_Transient);
		return nullptr;
	}

	// v2.2: 应用Fixer修正Bounds（创建可修改副本）
	FUMGWidgetNode FixedRoot = WidgetData.RootWidget;
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	const FIntPoint ConfigDesignSize = Settings->GetDesignSize();
	const int32 RootWidth = FMath::CeilToInt(FixedRoot.Bounds.W);
	const int32 RootHeight = FMath::CeilToInt(FixedRoot.Bounds.H);
	const bool bHasValidRootSize = RootWidth >= 2 && RootHeight >= 2;
	// Fidelity-first: Fixer/LayoutCalc 使用设计稿根节点的真实像素尺寸，
	// 避免被用户配置尺寸放大/压缩而产生系统性偏移。
	FIntPoint DesignSize = bHasValidRootSize
		? FIntPoint(RootWidth, RootHeight)
		: ConfigDesignSize;
	if (bHasValidRootSize && (DesignSize != ConfigDesignSize))
	{
		UE_LOG(LogDesign2UMG, Log,
			TEXT("Using root design size %dx%d (config %dx%d, root bounds %.0fx%.0f)"),
			DesignSize.X, DesignSize.Y,
			ConfigDesignSize.X, ConfigDesignSize.Y,
			FixedRoot.Bounds.W, FixedRoot.Bounds.H);
	}
	else if (!bHasValidRootSize)
	{
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("Root bounds invalid (%.2fx%.2f), fallback to config design size %dx%d"),
			FixedRoot.Bounds.W, FixedRoot.Bounds.H,
			ConfigDesignSize.X, ConfigDesignSize.Y);
	}
	FUMGWidgetUtils::EffectiveDesignSize = DesignSize;
	if (Settings->bEnableLayoutFixer)
	{
		FUMGWidgetUtils::ApplyFixer(FixedRoot, DesignSize);
	}

	const bool bOversizedRoot =
		(FixedRoot.Bounds.W > static_cast<float>(ConfigDesignSize.X)) ||
		(FixedRoot.Bounds.H > static_cast<float>(ConfigDesignSize.Y));
	if (Settings->bAutoWrapOversizedRootForViewport && bOversizedRoot)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("Wrapping oversized root for viewport fit: root=%.0fx%.0f config=%dx%d stretch=%s"),
			FixedRoot.Bounds.W, FixedRoot.Bounds.H,
			ConfigDesignSize.X, ConfigDesignSize.Y,
			Settings->bViewportScaleDownOnly ? TEXT("DownOnly") : TEXT("Both"));
		FixedRoot = WrapRootForViewport(
			MoveTemp(FixedRoot),
			ConfigDesignSize,
			DesignSize,
			Settings->bViewportScaleDownOnly);
	}

	{
		TMap<uint32, FString> PathTextureCache;
		const FString PathTextureImportPath = PackagePath / TEXT("D2U_PathTextures");
		PersistPathTexturesInNodeTree(FixedRoot, PathTextureImportPath, AssetName, PathTextureCache);

		TMap<uint32, FString> ProceduralTextureCache;
		const FString ProceduralTextureImportPath = PackagePath / TEXT("D2U_ProceduralTextures");
		PersistProceduralTexturesInNodeTree(FixedRoot, ProceduralTextureImportPath, AssetName, ProceduralTextureCache);
	}

	// Dump widget tree only in verbose mode.
	if (UE_LOG_ACTIVE(LogDesign2UMG, Verbose))
	{
		TFunction<void(const FUMGWidgetNode&, int32)> DumpNode = [&](const FUMGWidgetNode& Node, int32 Depth)
		{
			FString Indent;
			for (int32 i = 0; i < Depth; ++i) Indent += TEXT("  ");

			FString SlotInfo = FString::Printf(TEXT("slot[h=%s v=%s sz=%s fw=%.1f off=(%.0f,%.0f,%.0f,%.0f) anc=(%.2f,%.2f,%.2f,%.2f) aln=(%.2f,%.2f) auto=%d]"),
				*Node.Slot.HorizontalAlignment, *Node.Slot.VerticalAlignment,
				*Node.Slot.SizeRule, Node.Slot.FillWeight,
				Node.Slot.Offsets.Left, Node.Slot.Offsets.Top, Node.Slot.Offsets.Right, Node.Slot.Offsets.Bottom,
				Node.Slot.Anchors.Min.X, Node.Slot.Anchors.Min.Y, Node.Slot.Anchors.Max.X, Node.Slot.Anchors.Max.Y,
				Node.Slot.Alignment.X, Node.Slot.Alignment.Y,
				Node.Slot.bAutoSize ? 1 : 0);

			FString PropsInfo;
			Node.Properties.ForEachSerialized([&PropsInfo](const FString& Key, const FString& Value)
			{
				if (Key == TEXT("PathGeometry") || Key == TEXT("MaterialParams")) return;
				PropsInfo += FString::Printf(TEXT(" %s=%s"), *Key, *Value.Left(40));
			});

			UE_LOG(LogDesign2UMG, Verbose, TEXT("TREE: %s[%s] '%s' bounds=(%.1f,%.1f,%.1f,%.1f) %s%s"),
				*Indent, *Node.Type, *Node.Name,
				Node.Bounds.X, Node.Bounds.Y, Node.Bounds.W, Node.Bounds.H,
				*SlotInfo, *PropsInfo);

			for (const auto& Child : Node.Children)
				DumpNode(Child, Depth + 1);
		};
		UE_LOG(LogDesign2UMG, Verbose, TEXT("=== Widget Tree Dump (after Fixer) ==="));
		DumpNode(FixedRoot, 0);
		UE_LOG(LogDesign2UMG, Verbose, TEXT("=== End Widget Tree Dump ==="));
	}

	// 使用统一的 Widget 创建函数
	UWidget* Root = FUMGWidgetUtils::CreateWidgetRecursive(WBP->WidgetTree, FixedRoot);
	if (!Root)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to create root widget for '%s'"), *WidgetData.WidgetName);
		UE_LOG(LogDesign2UMG, Error, TEXT("无法创建根控件 '%s'。设计文件可能包含不支持的节点类型。"), *WidgetData.WidgetName);
		Package->ClearDirtyFlag();
		Package->SetFlags(RF_Transient);
		return nullptr;
	}
	WBP->WidgetTree->RootWidget = Root;
	DumpTemplateTextBlockFontState(TEXT("template-precompile"), WBP);

	// 设置编辑器显示尺寸到 CDO；保存时会在编译后再次回写，避免编译重置默认值。
	ApplyDesignPreviewSize(WBP, DesignSize, TEXT("pre-compile"));

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(WBP);

	UE_LOG(LogDesign2UMG, Log, TEXT("Built Widget Blueprint at %s (not yet compiled/saved)"), *FullPath);
	return WBP;
}

void FUMGGenerator::SaveWidgetBlueprint(UWidgetBlueprint* WBP)
{
	if (!WBP) return;

	FIntPoint DesiredDesignSize = ReadDesignPreviewSize(WBP);
	if (DesiredDesignSize.X <= 1 || DesiredDesignSize.Y <= 1)
	{
		DesiredDesignSize = UDesign2UMGSettings::Get()->GetDesignSize();
	}

	// 编译（此时 transient 纹理数据可能丢失，但渲染已完成）
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

	// MarkBlueprintAsStructurallyModified 可能触发中间编译填充 GUID map，
	// 清除后让 CompileBlueprint 走 IsEmpty() 分支从 widget tree 重建，避免 stale entry
	WBP->WidgetVariableNameToGuidMap.Empty();
	FKismetEditorUtilities::CompileBlueprint(WBP);
	DumpTemplateTextBlockFontState(TEXT("template-postcompile"), WBP);
	ApplyDesignPreviewSize(WBP, DesiredDesignSize, TEXT("post-compile"));

	UPackage* Package = WBP->GetOutermost();
	FString FullPath = Package->GetName();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString FileName = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, WBP, *FileName, SaveArgs);

	UE_LOG(LogDesign2UMG, Log, TEXT("Saved Widget Blueprint at %s"), *FullPath);
}

