// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignRemoteImageImporter.h"
#include "DesignRemoteImageImporterInternal.h"

#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "RenderingThread.h"
#include "TextureCompiler.h"
#include "UObject/SavePackage.h"

namespace
{
	using DesignRemoteImageImporterInternal::FImagePayloadIO;
	using DesignRemoteImageImporterInternal::FImportedTextureIndex;
	using DesignRemoteImageImporterInternal::FResolveContext;

	int32 GPendingTextureResourceUpdates = 0;

	void MarkPendingTextureResourceUpdate()
	{
		++GPendingTextureResourceUpdates;
	}

	void FlushPendingTextureResourceUpdatesIfNeeded()
	{
		if (GPendingTextureResourceUpdates <= 0)
		{
			return;
		}

		FlushRenderingCommands();
		GPendingTextureResourceUpdates = 0;
	}

	bool IsRemoteUrl(const FString& Value)
	{
		return Value.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
			|| Value.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
	}

	bool IsAssetPath(const FString& Value)
	{
		return Value.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive)
			|| Value.StartsWith(TEXT("/Engine/"), ESearchCase::CaseSensitive);
	}

	FString NormalizeUrl(const FString& InUrl)
	{
		FString Url = InUrl.TrimStartAndEnd();
		if (Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
		{
			return FString(TEXT("https://")) + Url.RightChop(8);
		}
		if (Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase))
		{
			return FString(TEXT("http://")) + Url.RightChop(7);
		}
		return Url;
	}

	FString MakeUrlHash(const FString& InUrl)
	{
		const FTCHARToUTF8 Utf8(*InUrl);
		uint8 Hash[FSHA1::DigestSize] = { 0 };
		FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash);
		return BytesToHex(Hash, UE_ARRAY_COUNT(Hash));
	}

	FString MakeContentHash(const TArray<uint8>& Data)
	{
		uint8 Hash[FSHA1::DigestSize] = { 0 };
		FSHA1::HashBuffer(Data.GetData(), Data.Num(), Hash);
		return BytesToHex(Hash, UE_ARRAY_COUNT(Hash));
	}

	FString SanitizeAssetName(const FString& Name)
	{
		FString Safe = Name;
		Safe.ReplaceInline(TEXT(" "), TEXT("_"));
		Safe.ReplaceInline(TEXT("."), TEXT("_"));
		Safe.ReplaceInline(TEXT("/"), TEXT("_"));
		Safe.ReplaceInline(TEXT("\\"), TEXT("_"));
		Safe.ReplaceInline(TEXT(":"), TEXT("_"));
		Safe.ReplaceInline(TEXT(";"), TEXT("_"));
		Safe.ReplaceInline(TEXT(","), TEXT("_"));
		Safe.ReplaceInline(TEXT("-"), TEXT("_"));
		return Safe.IsEmpty() ? TEXT("Image") : Safe;
	}

	FString BuildLegacyTextureAssetName(const FString& FrameName, const FString& WidgetName, const FString& WidgetId, const FString& SourceKey)
	{
		constexpr TCHAR TextureImportVersion[] = TEXT("ui4");
		const FString SafeFrame = SanitizeAssetName(FrameName).Left(24);
		FString NodeBase = WidgetName;
		if (NodeBase.IsEmpty())
		{
			NodeBase = WidgetId;
		}
		const FString SafeNode = SanitizeAssetName(NodeBase).Left(24);
		const FString SourceHash = MakeUrlHash(SourceKey).Left(10);
		return FString::Printf(TEXT("T_%s_%s_%s_%s"), *SafeFrame, *SafeNode, *SourceHash, TextureImportVersion);
	}

	FString BuildSharedTextureAssetName(const FString& ContentHash)
	{
		constexpr TCHAR TextureImportVersion[] = TEXT("ui5");
		const FString SafeHash = ContentHash.Left(16);
		return FString::Printf(TEXT("T_D2U_%s_%s"), *SafeHash, TextureImportVersion);
	}

	FString BuildSubImportPath(const FString& ImportPath, const FString& FrameName)
	{
		return ImportPath / SanitizeAssetName(FrameName);
	}

	FString BuildSharedImportPath(const FString& ImportPath)
	{
		return ImportPath / TEXT("_Shared");
	}

	FString BuildObjectPathFromImport(const FString& ImportPath, const FString& AssetName)
	{
		const FString LongPackageName = ImportPath + TEXT("/") + AssetName;
		return LongPackageName + TEXT(".") + AssetName;
	}

	FString BuildSourceKeyForLocal(const FString& LocalPath)
	{
		FString FullPath = FPaths::ConvertRelativePathToFull(LocalPath);
		FPaths::NormalizeFilename(FullPath);
		FullPath.ToLowerInline();
		return FString::Printf(TEXT("local://%s"), *FullPath);
	}

	FString BuildSourceKeyForRemote(const FString& Url)
	{
		return FString::Printf(TEXT("remote://%s"), *Url);
	}

	FString BuildSourceScopedContentKey(const FString& SourceKey, const FString& ContentHash)
	{
		return FString::Printf(TEXT("%s#content:%s"), *SourceKey, *ContentHash);
	}

	FString BuildVersionedSourceKey(const FString& SourceKey, const FString& ContentHash)
	{
		return FString::Printf(TEXT("%s#version:%s"), *SourceKey, *ContentHash);
	}

	FString NormalizeImportPath(const FString& InPath)
	{
		FString Path = InPath.TrimStartAndEnd();
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Path.EndsWith(TEXT("/")))
		{
			Path.LeftChopInline(1);
		}

		if (Path.IsEmpty())
		{
			return TEXT("/Game/Design2UMG/ImportedImages");
		}

		if (!Path.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive))
		{
			return TEXT("/Game/Design2UMG/ImportedImages");
		}

		if (!FPackageName::IsValidLongPackageName(Path))
		{
			return TEXT("/Game/Design2UMG/ImportedImages");
		}

		return Path;
	}

	FString BuildLongPackageName(const FString& ImportPath, const FString& AssetName)
	{
		return ImportPath + TEXT("/") + AssetName;
	}

	FString BuildObjectPath(const FString& LongPackageName, const FString& AssetName)
	{
		return LongPackageName + TEXT(".") + AssetName;
	}

	bool ApplyTextureUiSettings(UTexture2D* Texture)
	{
		if (!Texture)
		{
			return false;
		}

		bool bChanged = false;
		if (!Texture->SRGB)
		{
			Texture->SRGB = true;
			bChanged = true;
		}
		if (!Texture->NeverStream)
		{
			Texture->NeverStream = true;
			bChanged = true;
		}
		if (Texture->LODGroup != TEXTUREGROUP_UI)
		{
			Texture->LODGroup = TEXTUREGROUP_UI;
			bChanged = true;
		}
		if (Texture->CompressionSettings != TC_EditorIcon)
		{
			Texture->CompressionSettings = TC_EditorIcon;
			bChanged = true;
		}
		if (Texture->MipGenSettings != TMGS_NoMipmaps)
		{
			Texture->MipGenSettings = TMGS_NoMipmaps;
			bChanged = true;
		}
		if (Texture->Filter != TF_Bilinear)
		{
			Texture->Filter = TF_Bilinear;
			bChanged = true;
		}
		if (Texture->AddressX != TA_Clamp)
		{
			Texture->AddressX = TA_Clamp;
			bChanged = true;
		}
		if (Texture->AddressY != TA_Clamp)
		{
			Texture->AddressY = TA_Clamp;
			bChanged = true;
		}

		if (bChanged)
		{
			Texture->Modify();
			Texture->UpdateResource();
			Texture->MarkPackageDirty();
		}
		return bChanged;
	}

	bool EnsureUiTextureReady(UTexture2D* Texture)
	{
		if (!Texture)
		{
			return false;
		}

		Texture->ConditionalPostLoad();
		ApplyTextureUiSettings(Texture);

#if WITH_EDITOR
		FTextureCompilingManager::Get().FinishCompilation({ Texture });
#endif

		// Always force resource update + flush. After RewriteTextureAsset the
		// old GPU resource still exists (GetResource() != null) but contains
		// stale 32x32 data. Unconditional update ensures the GPU has the
		// correct pixels before the widget tree is rendered.
		Texture->UpdateResource();
		MarkPendingTextureResourceUpdate();
		return true;
	}

	bool CreateTextureAsset(
		const FString& ImportPath,
		const FString& AssetName,
		const TArray64<uint8>& RawData,
		int32 Width,
		int32 Height,
		FString& OutObjectPath,
		FString& OutError)
	{
		const FString LongPackageName = BuildLongPackageName(ImportPath, AssetName);
		if (!FPackageName::IsValidLongPackageName(LongPackageName))
		{
			OutError = FString::Printf(TEXT("Invalid asset path: %s"), *LongPackageName);
			return false;
		}

		const FString ObjectPath = BuildObjectPath(LongPackageName, AssetName);

		UPackage* Package = CreatePackage(*LongPackageName);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("Unable to create asset package: %s"), *LongPackageName);
			return false;
		}

		UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Texture)
		{
			OutError = FString::Printf(TEXT("Unable to create texture object: %s"), *AssetName);
			return false;
		}

		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, RawData.GetData());
		ApplyTextureUiSettings(Texture);
		Texture->PostEditChange();
		Texture->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(Texture);
		Package->MarkPackageDirty();

		const FString PackageFilename = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("Unable to save asset package: %s"), *PackageFilename);
			return false;
		}

		OutObjectPath = ObjectPath;
		return true;
	}

	bool RewriteTextureAsset(
		UTexture2D* Texture,
		const TArray64<uint8>& RawData,
		int32 Width,
		int32 Height,
		FString& OutError)
	{
		if (!Texture || Width <= 0 || Height <= 0 || RawData.Num() <= 0)
		{
			OutError = TEXT("Invalid arguments");
			return false;
		}

		UPackage* Package = Texture->GetPackage();
		if (!Package)
		{
			OutError = TEXT("Invalid texture package");
			return false;
		}

		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, RawData.GetData());
		ApplyTextureUiSettings(Texture);
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		Package->MarkPackageDirty();

		const FString LongPackageName = Package->GetName();
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("Failed to save texture package: %s"), *PackageFilename);
			return false;
		}

		return true;
	}

	bool ResolveTexturePath(FString& InOutTexturePath, const FString& WidgetName, const FString& WidgetId, FResolveContext& Context)
	{
		InOutTexturePath = InOutTexturePath.TrimStartAndEnd();
		if (InOutTexturePath.IsEmpty())
		{
			return true;
		}

		if (IsAssetPath(InOutTexturePath))
		{
			return true;
		}

		// Local file path: read directly and create a texture asset.
		if (!IsRemoteUrl(InOutTexturePath) && FPaths::FileExists(InOutTexturePath))
		{
			const FString LocalPath = InOutTexturePath;
			const FString LocalKey = BuildSourceKeyForLocal(LocalPath);

			if (const FString* CachedPath = Context.UrlToAssetPath.Find(LocalKey))
			{
				if (UTexture2D* CachedTexture = LoadObject<UTexture2D>(nullptr, **CachedPath))
				{
					EnsureUiTextureReady(CachedTexture);
				}
				InOutTexturePath = *CachedPath;
				++Context.Report.ReusedCount;
				return true;
			}

			TArray<uint8> FileData;
			if (!FFileHelper::LoadFileToArray(FileData, *LocalPath))
			{
				++Context.Report.FailedCount;
				Context.Report.AddMessage(FString::Printf(TEXT("Local file read failed for '%s': %s"), *WidgetName, *LocalPath));
				return false;
			}

			const FString ContentHash = MakeContentHash(FileData);
			const FString ContentKey = BuildSourceScopedContentKey(LocalKey, ContentHash);
			if (const FString* ExistingPath = Context.ContentHashToAsset.Find(ContentKey))
			{
				if (UTexture2D* CachedTexture = LoadObject<UTexture2D>(nullptr, **ExistingPath))
				{
					EnsureUiTextureReady(CachedTexture);
				}
				InOutTexturePath = *ExistingPath;
				++Context.Report.ReusedCount;
				Context.UrlToAssetPath.Add(LocalKey, *ExistingPath);
				return true;
			}

			const FString SharedAssetName = BuildSharedTextureAssetName(ContentHash);
			FString PersistedObjectPath;
			if (FImportedTextureIndex::TryGetPersistedPath(Context.ImportPath, SharedAssetName, PersistedObjectPath))
			{
				if (UTexture2D* PersistedTexture = LoadObject<UTexture2D>(nullptr, *PersistedObjectPath))
				{
					EnsureUiTextureReady(PersistedTexture);
					InOutTexturePath = PersistedObjectPath;
					++Context.Report.ReusedCount;
					Context.UrlToAssetPath.Add(LocalKey, PersistedObjectPath);
					Context.ContentHashToAsset.Add(ContentKey, PersistedObjectPath);
					Context.Report.AddMessage(FString::Printf(TEXT("Reused cross-run content: %s"), *PersistedObjectPath));
					return true;
				}

				FImportedTextureIndex::Forget(Context.ImportPath, SharedAssetName);
			}

			const FString SubDir = BuildSubImportPath(Context.ImportPath, Context.FrameName);
			const FString VersionedSourceKey = BuildVersionedSourceKey(LocalKey, ContentHash);
			const FString LegacyAssetName = BuildLegacyTextureAssetName(Context.FrameName, WidgetName, WidgetId, VersionedSourceKey);
			const FString ObjectPath = BuildObjectPathFromImport(SubDir, LegacyAssetName);
			if (UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *ObjectPath))
			{
				int32 SourceWidth = 0;
				int32 SourceHeight = 0;
				TArray64<uint8> SourceRawBGRA;
				FString SourceDecodeError;
				const bool bDecoded = FImagePayloadIO::DecodeToBGRA8(FileData, SourceWidth, SourceHeight, SourceRawBGRA, SourceDecodeError);
				if (bDecoded)
				{
					const int32 ExistingWidth = Existing->GetSizeX();
					const int32 ExistingHeight = Existing->GetSizeY();
					const bool bDimensionMismatch = ExistingWidth != SourceWidth || ExistingHeight != SourceHeight;
					const bool bLooksBroken = (ExistingWidth <= 1 || ExistingHeight <= 1) && (SourceWidth > 1 || SourceHeight > 1);
					if (bDimensionMismatch || bLooksBroken)
					{
						FString RewriteError;
						if (RewriteTextureAsset(Existing, SourceRawBGRA, SourceWidth, SourceHeight, RewriteError))
						{
							++Context.Report.RepairedCount;
							Context.Report.AddMessage(FString::Printf(TEXT("Repaired local texture: %s (%dx%d -> %dx%d)"),
								*ObjectPath, ExistingWidth, ExistingHeight, SourceWidth, SourceHeight));
						}
						else
						{
							Context.Report.AddMessage(FString::Printf(TEXT("Texture repair failed for '%s': %s"), *WidgetName, *RewriteError));
						}
					}
				}

				EnsureUiTextureReady(Existing);
				InOutTexturePath = ObjectPath;
				++Context.Report.ReusedCount;
				Context.UrlToAssetPath.Add(LocalKey, ObjectPath);
				Context.ContentHashToAsset.Add(ContentKey, ObjectPath);
				FImportedTextureIndex::Remember(Context.ImportPath, SharedAssetName, ObjectPath);
				return true;
			}

			int32 Width = 0, Height = 0;
			TArray64<uint8> RawBGRA;
			FString DecodeError;
			if (!FImagePayloadIO::DecodeToBGRA8(FileData, Width, Height, RawBGRA, DecodeError))
			{
				++Context.Report.FailedCount;
				Context.Report.AddMessage(FString::Printf(TEXT("Local image decode failed for '%s': %s"), *WidgetName, *DecodeError));
				return false;
			}

			FString ImportedObjectPath, ImportError;
			if (!CreateTextureAsset(BuildSharedImportPath(Context.ImportPath), SharedAssetName, RawBGRA, Width, Height, ImportedObjectPath, ImportError))
			{
				++Context.Report.FailedCount;
				Context.Report.AddMessage(FString::Printf(TEXT("Local image import failed for '%s': %s"), *WidgetName, *ImportError));
				return false;
			}

			if (UTexture2D* ImportedTexture = LoadObject<UTexture2D>(nullptr, *ImportedObjectPath))
			{
				EnsureUiTextureReady(ImportedTexture);
			}

			InOutTexturePath = ImportedObjectPath;
			++Context.Report.ImportedCount;
			Context.UrlToAssetPath.Add(LocalKey, ImportedObjectPath);
			Context.ContentHashToAsset.Add(ContentKey, ImportedObjectPath);
			FImportedTextureIndex::Remember(Context.ImportPath, SharedAssetName, ImportedObjectPath);
			Context.Report.AddMessage(FString::Printf(TEXT("Imported local texture: %s"), *ImportedObjectPath));
			return true;
		}

		if (!IsRemoteUrl(InOutTexturePath))
		{
			Context.Report.AddMessage(FString::Printf(TEXT("Skipped non-asset path for '%s': %s"), *WidgetName, *InOutTexturePath));
			return true;
		}

		const FString NormalizedUrl = NormalizeUrl(InOutTexturePath);
		const FString SubDir = BuildSubImportPath(Context.ImportPath, Context.FrameName);
		const FString SourceKey = BuildSourceKeyForRemote(NormalizedUrl);

		if (const FString* CachedPath = Context.UrlToAssetPath.Find(SourceKey))
		{
			if (UTexture2D* CachedTexture = LoadObject<UTexture2D>(nullptr, **CachedPath))
			{
				EnsureUiTextureReady(CachedTexture);
			}
			InOutTexturePath = *CachedPath;
			++Context.Report.ReusedCount;
			return true;
		}

		TArray<uint8> CompressedData;
		FString DownloadError;
		if (!FImagePayloadIO::DownloadUrl(NormalizedUrl, Context.TimeoutSeconds, CompressedData, DownloadError))
		{
			++Context.Report.FailedCount;
			Context.Report.AddMessage(FString::Printf(TEXT("Image '%s' download failed: %s (reason: %s)"), *WidgetName, *NormalizedUrl, *DownloadError));
			return false;
		}

		const FString ContentHash = MakeContentHash(CompressedData);
		const FString ContentKey = BuildSourceScopedContentKey(SourceKey, ContentHash);
		if (const FString* ExistingPath = Context.ContentHashToAsset.Find(ContentKey))
		{
			if (UTexture2D* CachedTexture = LoadObject<UTexture2D>(nullptr, **ExistingPath))
			{
				EnsureUiTextureReady(CachedTexture);
			}
			InOutTexturePath = *ExistingPath;
			++Context.Report.ReusedCount;
			Context.UrlToAssetPath.Add(SourceKey, *ExistingPath);
			Context.Report.AddMessage(FString::Printf(TEXT("Reused deduplicated content: %s"), **ExistingPath));
			return true;
		}

		const FString SharedAssetName = BuildSharedTextureAssetName(ContentHash);
		FString PersistedObjectPath;
		if (FImportedTextureIndex::TryGetPersistedPath(Context.ImportPath, SharedAssetName, PersistedObjectPath))
		{
			if (UTexture2D* PersistedTexture = LoadObject<UTexture2D>(nullptr, *PersistedObjectPath))
			{
				EnsureUiTextureReady(PersistedTexture);
				InOutTexturePath = PersistedObjectPath;
				++Context.Report.ReusedCount;
				Context.UrlToAssetPath.Add(SourceKey, PersistedObjectPath);
				Context.ContentHashToAsset.Add(ContentKey, PersistedObjectPath);
				Context.Report.AddMessage(FString::Printf(TEXT("Reused cross-run content: %s"), *PersistedObjectPath));
				return true;
			}

			FImportedTextureIndex::Forget(Context.ImportPath, SharedAssetName);
		}

		const FString VersionedSourceKey = BuildVersionedSourceKey(SourceKey, ContentHash);
		const FString LegacyAssetName = BuildLegacyTextureAssetName(Context.FrameName, WidgetName, WidgetId, VersionedSourceKey);
		const FString ObjectPath = BuildObjectPathFromImport(SubDir, LegacyAssetName);
		if (UTexture2D* ExistingTexture = LoadObject<UTexture2D>(nullptr, *ObjectPath))
		{
			EnsureUiTextureReady(ExistingTexture);
			InOutTexturePath = ObjectPath;
			++Context.Report.ReusedCount;
			Context.UrlToAssetPath.Add(SourceKey, ObjectPath);
			Context.ContentHashToAsset.Add(ContentKey, ObjectPath);
			FImportedTextureIndex::Remember(Context.ImportPath, SharedAssetName, ObjectPath);
			Context.Report.AddMessage(FString::Printf(TEXT("Reused existing texture: %s"), *ObjectPath));
			return true;
		}

		int32 Width = 0;
		int32 Height = 0;
		TArray64<uint8> RawBGRA;
		FString DecodeError;
		if (!FImagePayloadIO::DecodeToBGRA8(CompressedData, Width, Height, RawBGRA, DecodeError))
		{
			++Context.Report.FailedCount;
			Context.Report.AddMessage(FString::Printf(TEXT("Image '%s' decode failed: %s (reason: %s)"), *WidgetName, *NormalizedUrl, *DecodeError));
			return false;
		}

		FString ImportedObjectPath;
		FString ImportError;
		if (!CreateTextureAsset(BuildSharedImportPath(Context.ImportPath), SharedAssetName, RawBGRA, Width, Height, ImportedObjectPath, ImportError))
		{
			++Context.Report.FailedCount;
			Context.Report.AddMessage(FString::Printf(TEXT("Image '%s' import failed: %s (reason: %s)"), *WidgetName, *NormalizedUrl, *ImportError));
			return false;
		}

		if (UTexture2D* ImportedTexture = LoadObject<UTexture2D>(nullptr, *ImportedObjectPath))
		{
			EnsureUiTextureReady(ImportedTexture);
		}

		InOutTexturePath = ImportedObjectPath;
		++Context.Report.ImportedCount;
		Context.UrlToAssetPath.Add(SourceKey, ImportedObjectPath);
		Context.ContentHashToAsset.Add(ContentKey, ImportedObjectPath);
		FImportedTextureIndex::Remember(Context.ImportPath, SharedAssetName, ImportedObjectPath);
		Context.Report.AddMessage(FString::Printf(TEXT("Imported texture: %s"), *ImportedObjectPath));
		return true;
	}

	void ResolveNodeRecursive(FUMGWidgetNode& Node, FResolveContext& Context)
	{
		FString TexturePath = Node.Brush.Image.TexturePath.Get(Node.Brush.TexturePath);
		if (!TexturePath.IsEmpty())
		{
			const FString DebugName = Node.Name.IsEmpty() ? Node.Id : Node.Name;
			FString ResolvedTexturePath = TexturePath;
			ResolveTexturePath(
				ResolvedTexturePath,
				DebugName.IsEmpty() ? TEXT("UnnamedWidget") : DebugName,
				Node.Id,
				Context);
			// Keep the canonical typed owner in sync after import-time path rewriting.
			Node.Brush.TexturePath.Empty();
			Node.Brush.Image.TexturePath = ResolvedTexturePath;
		}

		for (FUMGWidgetNode& Child : Node.Children)
		{
			ResolveNodeRecursive(Child, Context);
		}
	}
}

bool FDesignRemoteImageImporter::ResolveRemoteImages(FUMGWidgetData& InOutData, FString* OutReport)
{
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!Settings)
	{
		if (OutReport)
		{
			*OutReport = TEXT("Remote image import: settings unavailable");
		}
		return false;
	}

	if (!Settings->bEnableRemoteImageImport)
	{
		if (OutReport)
		{
			*OutReport = TEXT("Remote image import: disabled");
		}
		return true;
	}

	FResolveContext Context;
	Context.ImportPath = NormalizeImportPath(Settings->RemoteImageImportPath);
	Context.TimeoutSeconds = FMath::Max(1.0f, Settings->RemoteImageTimeoutSeconds);
	Context.FrameName = InOutData.WidgetName;

	ResolveNodeRecursive(InOutData.RootWidget, Context);
	FlushPendingTextureResourceUpdatesIfNeeded();

	if (OutReport)
	{
		*OutReport = Context.Report.BuildSummary(Context.ImportPath);
	}

	if (Context.Report.FailedCount > 0)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Remote image import finished with failures (%d)"), Context.Report.FailedCount);
		UE_LOG(LogDesign2UMG, Warning, TEXT("%d image downloads failed; generation will continue but some images may be missing."), Context.Report.FailedCount);
		return false;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("Remote image import finished: imported=%d reused=%d repaired=%d"),
		Context.Report.ImportedCount, Context.Report.ReusedCount, Context.Report.RepairedCount);
	return true;
}

// ============================================================================
// ResolveTransientTextures - preview path: download remote images as in-memory textures.
// ============================================================================

namespace
{
	void CollectRemoteUrls(const FUMGWidgetNode& Node, TSet<FString>& OutUrls)
	{
		const FString TexturePath = Node.Brush.Image.TexturePath.Get(Node.Brush.TexturePath);
		if (!TexturePath.IsEmpty())
		{
			const FString Trimmed = TexturePath.TrimStartAndEnd();
			if (IsRemoteUrl(Trimmed))
			{
				OutUrls.Add(Trimmed);
			}
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			CollectRemoteUrls(Child, OutUrls);
		}
	}

	UTexture2D* CreateTransientTexture(const TArray64<uint8>& RawBGRA, int32 Width, int32 Height)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture) return nullptr;

		// RF_DuplicateTransient makes FDuplicateDataWriter write nullptr instead of copying, avoiding empty mip duplicates.
		Texture->AddToRoot();
		Texture->SetFlags(RF_DuplicateTransient);

		void* MipData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(MipData, RawBGRA.GetData(), RawBGRA.Num());
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();

		ApplyTextureUiSettings(Texture);
		MarkPendingTextureResourceUpdate();
		return Texture;
	}
}

void FDesignRemoteImageImporter::ResolveTransientTextures(const FUMGWidgetNode& RootNode, TMap<FString, UTexture2D*>& OutTextureMap)
{
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	const float Timeout = Settings ? FMath::Max(1.0f, Settings->RemoteImageTimeoutSeconds) : 20.0f;

	TSet<FString> Urls;
	CollectRemoteUrls(RootNode, Urls);

	for (const FString& Url : Urls)
	{
		if (OutTextureMap.Contains(Url)) continue;

		TArray<uint8> CompressedData;
		FString Error;
		if (!FImagePayloadIO::DownloadUrl(NormalizeUrl(Url), Timeout, CompressedData, Error))
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("Transient download failed: %s (%s)"), *Url, *Error);
			UE_LOG(LogDesign2UMG, Warning, TEXT("Image download failed: %s (reason: %s)"), *Url, *Error);
			continue;
		}

		int32 Width = 0, Height = 0;
		TArray64<uint8> RawBGRA;
		if (!FImagePayloadIO::DecodeToBGRA8(CompressedData, Width, Height, RawBGRA, Error))
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("Transient decode failed: %s (%s)"), *Url, *Error);
			UE_LOG(LogDesign2UMG, Warning, TEXT("Image decode failed: %s (reason: %s)"), *Url, *Error);
			continue;
		}

		UTexture2D* Texture = CreateTransientTexture(RawBGRA, Width, Height);
		if (Texture)
		{
			OutTextureMap.Add(Url, Texture);
		}
	}

	FlushPendingTextureResourceUpdatesIfNeeded();
}

void FDesignRemoteImageImporter::ReleaseTransientTextures(TMap<FString, UTexture2D*>& InOutTextureMap)
{
	for (auto& Pair : InOutTextureMap)
	{
		if (IsValid(Pair.Value) && Pair.Value->IsRooted())
			Pair.Value->RemoveFromRoot();
	}
	InOutTextureMap.Empty();
}

