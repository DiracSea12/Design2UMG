// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignRemoteImageImporter.h"
#include "DesignRemoteImageImporterInternal.h"

#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "DesignPreviewContracts.h"
#include "GeneratedAssetLifecycle.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
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
	using DesignRemoteImageImporterInternal::FImageProcessingFailure;
	using DesignImageResources::FImageResourceLimits;
	using DesignRemoteImageImporterInternal::FResolveContext;
	using DesignRemoteImageImporterInternal::FUrlDownloadResult;

	// Shared textures deduplicate image content; the tag versions the asset family.
	constexpr TCHAR SharedTextureImportVersion[] = TEXT("ui5");

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

	// Single trim/is-remote/normalize gate shared by CollectRemoteUrls and ResolveTexturePath,
	// so both sides agree on the exact key used to look up pre-downloaded URL payloads.
	FString TryNormalizeRemoteUrl(const FString& InTexturePath)
	{
		const FString Trimmed = InTexturePath.TrimStartAndEnd();
		return FDesignRemoteImageImporter::IsHttpUrl(Trimmed) ? NormalizeUrl(Trimmed) : FString();
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

	// Keep content-addressed texture asset names bounded.
	constexpr int32 MaxSharedContentHashChars = 16;

	FString BuildSharedTextureAssetName(const FString& ContentHash)
	{
		const FString SafeHash = ContentHash.Left(MaxSharedContentHashChars);
		return FString::Printf(TEXT("T_D2U_%s_%s"), *SafeHash, SharedTextureImportVersion);
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

	FString NormalizeImportPath(const FString& InPath)
	{
		constexpr TCHAR DefaultImportPath[] = TEXT("/Game/Design2UMG/ImportedImages");
		FString Path = InPath.TrimStartAndEnd();
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Path.EndsWith(TEXT("/")))
		{
			Path.LeftChopInline(1);
		}

		if (Path.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("DesignRemoteImageImporter: empty RemoteImageImportPath; falling back to %s"),
				DefaultImportPath);
			return DefaultImportPath;
		}

		if (!Path.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive))
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("DesignRemoteImageImporter: RemoteImageImportPath '%s' is not under /Game; falling back to %s"),
				*Path,
				DefaultImportPath);
			return DefaultImportPath;
		}

		if (!FPackageName::IsValidLongPackageName(Path))
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("DesignRemoteImageImporter: RemoteImageImportPath '%s' is not a valid long package name; falling back to %s"),
				*Path,
				DefaultImportPath);
			return DefaultImportPath;
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

	void EnsureUiTextureReady(UTexture2D* Texture, bool& bTextureResourceUpdatePending)
	{
		if (!Texture)
		{
			return;
		}

		Texture->ConditionalPostLoad();
		ApplyTextureUiSettings(Texture);

#if WITH_EDITOR
		FTextureCompilingManager::Get().FinishCompilation({ Texture });
#endif

		// Refresh the GPU resource after applying UI texture settings before rendering.
		Texture->UpdateResource();
		bTextureResourceUpdatePending = true;
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

		Package->MarkPackageDirty();

		const FString PackageFilename = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetAssetPackageExtension());
		const bool bPackageFileExisted = IFileManager::Get().FileExists(*PackageFilename);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs))
		{
			if (!bPackageFileExisted)
			{
				IFileManager::Get().Delete(*PackageFilename, false, true);
			}
			GeneratedAssetLifecycle::DiscardUnsavedAsset(Texture);
			OutError = FString::Printf(TEXT("Unable to save asset package: %s"), *PackageFilename);
			return false;
		}
		GeneratedAssetLifecycle::RegisterSavedAsset(Texture);

		OutObjectPath = ObjectPath;
		return true;
	}

	// Reuse an asset this run already resolved for the same source key.
	bool TryReuseUrlCachedTexture(const FString& SourceKey, FString& InOutTexturePath, FResolveContext& Context)
	{
		if (const FString* CachedPath = Context.UrlToAssetPath.Find(SourceKey))
		{
			if (UTexture2D* CachedTexture = LoadObject<UTexture2D>(nullptr, **CachedPath))
			{
				EnsureUiTextureReady(CachedTexture, Context.bTextureResourceUpdatePending);
			}
			InOutTexturePath = *CachedPath;
			Context.Report.AddDiagnostic(
				EDesignImageDiagnosticOutcome::Reused,
				EDesignImageDiagnosticCategory::None,
				FString(),
				FString(),
				*CachedPath,
				FString(),
				FString());
			return true;
		}
		return false;
	}

	// Reuse a content-deduplicated asset recorded earlier in this run.
	bool TryReuseContentKeyedTexture(
		const FString& SourceKey,
		const FString& ContentKey,
		const TCHAR* ReusedMessagePrefix,
		FString& InOutTexturePath,
		FResolveContext& Context)
	{
		if (const FString* ExistingPath = Context.ContentHashToAsset.Find(ContentKey))
		{
			if (UTexture2D* CachedTexture = LoadObject<UTexture2D>(nullptr, **ExistingPath))
			{
				EnsureUiTextureReady(CachedTexture, Context.bTextureResourceUpdatePending);
			}
			InOutTexturePath = *ExistingPath;
			Context.UrlToAssetPath.Add(SourceKey, *ExistingPath);
			const FString Message = ReusedMessagePrefix
				? FString::Printf(TEXT("%s%s"), ReusedMessagePrefix, **ExistingPath)
				: FString();
			Context.Report.AddDiagnostic(
				EDesignImageDiagnosticOutcome::Reused,
				EDesignImageDiagnosticCategory::None,
				FString(),
				FString(),
				*ExistingPath,
				FString(),
				Message);
			return true;
		}
		return false;
	}

	// Adopt an existing texture for reuse: refresh it, point the widget at it and record
	// it in both run caches.
	void RegisterReusedTexture(
		const FString& SourceKey,
		const FString& ContentKey,
		const FString& ObjectPath,
		UTexture2D* Texture,
		const TCHAR* ReusedMessagePrefix,
		FString& InOutTexturePath,
		FResolveContext& Context)
	{
		EnsureUiTextureReady(Texture, Context.bTextureResourceUpdatePending);
		InOutTexturePath = ObjectPath;
		Context.UrlToAssetPath.Add(SourceKey, ObjectPath);
		Context.ContentHashToAsset.Add(ContentKey, ObjectPath);
		const FString Message = ReusedMessagePrefix
			? FString::Printf(TEXT("%s%s"), ReusedMessagePrefix, *ObjectPath)
			: FString();
		Context.Report.AddDiagnostic(
			EDesignImageDiagnosticOutcome::Reused,
			EDesignImageDiagnosticCategory::None,
			FString(),
			FString(),
			ObjectPath,
			FString(),
			Message);
	}

	// Reuse a texture persisted by a previous run under {ImportPath}/_Shared/.
	bool TryReusePersistedSharedTexture(
		const FString& SourceKey,
		const FString& ContentHash,
		const FString& ContentKey,
		FString& InOutTexturePath,
		FResolveContext& Context)
	{
		const FString PersistedObjectPath = BuildObjectPathFromImport(
			BuildSharedImportPath(Context.ImportPath),
			BuildSharedTextureAssetName(ContentHash));
		UTexture2D* PersistedTexture = LoadObject<UTexture2D>(nullptr, *PersistedObjectPath);
		if (!PersistedTexture)
		{
			return false;
		}

		RegisterReusedTexture(
			SourceKey,
			ContentKey,
			PersistedObjectPath,
			PersistedTexture,
			TEXT("Reused cross-run content: "),
			InOutTexturePath,
			Context);
		return true;
	}

	// Adopt a freshly created shared texture: refresh it, point the widget at it and record
	// it in both run caches.
	void RegisterImportedTexture(
		const FString& SourceKey,
		const FString& ContentKey,
		const FString& ImportedObjectPath,
		const TCHAR* ImportedMessagePrefix,
		FString& InOutTexturePath,
		FResolveContext& Context)
	{
		if (UTexture2D* ImportedTexture = LoadObject<UTexture2D>(nullptr, *ImportedObjectPath))
		{
			EnsureUiTextureReady(ImportedTexture, Context.bTextureResourceUpdatePending);
		}
		InOutTexturePath = ImportedObjectPath;
		Context.UrlToAssetPath.Add(SourceKey, ImportedObjectPath);
		Context.ContentHashToAsset.Add(ContentKey, ImportedObjectPath);
		const FString Message = FString::Printf(TEXT("%s%s"), ImportedMessagePrefix, *ImportedObjectPath);
		Context.Report.AddDiagnostic(
			EDesignImageDiagnosticOutcome::Imported,
			EDesignImageDiagnosticCategory::None,
			FString(),
			FString(),
			ImportedObjectPath,
			FString(),
			Message);
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
		if (!FDesignRemoteImageImporter::IsHttpUrl(InOutTexturePath) && FPaths::FileExists(InOutTexturePath))
		{
			const FString LocalPath = InOutTexturePath;
			const FString LocalKey = BuildSourceKeyForLocal(LocalPath);

			if (TryReuseUrlCachedTexture(LocalKey, InOutTexturePath, Context))
			{
				return true;
			}

			TArray<uint8> FileData;
			FString ReadError;
			if (!FImagePayloadIO::LoadLocalFile(LocalPath, Context.ImageLimits, FileData, ReadError))
			{
				const FString Message = FString::Printf(TEXT("Local file read failed for '%s': %s (%s)"), *WidgetName, *LocalPath, *ReadError);
				Context.Report.AddDiagnostic(
					EDesignImageDiagnosticOutcome::Failed,
					EDesignImageDiagnosticCategory::FileRead,
					WidgetId,
					WidgetName,
					LocalPath,
					ReadError,
					Message);
				return false;
			}
			if (!Context.OperationBudget.ConsumeCompressedBytes(static_cast<uint64>(FileData.Num()), ReadError))
			{
				const FString Message = FString::Printf(TEXT("Local image budget exceeded for '%s': %s"), *WidgetName, *ReadError);
				Context.Report.AddDiagnostic(
					EDesignImageDiagnosticOutcome::Failed,
					EDesignImageDiagnosticCategory::Budget,
					WidgetId,
					WidgetName,
					LocalPath,
					ReadError,
					Message);
				return false;
			}

			const FString ContentHash = MakeContentHash(FileData);
			const FString ContentKey = BuildSourceScopedContentKey(LocalKey, ContentHash);
			if (TryReuseContentKeyedTexture(LocalKey, ContentKey, nullptr, InOutTexturePath, Context))
			{
				return true;
			}
			if (TryReusePersistedSharedTexture(LocalKey, ContentHash, ContentKey, InOutTexturePath, Context))
			{
				return true;
			}

			const FString SharedAssetName = BuildSharedTextureAssetName(ContentHash);

			int32 Width = 0, Height = 0;
			TArray64<uint8> RawBGRA;
			FString DecodeError;
			EDesignImageDiagnosticCategory DecodeCategory = EDesignImageDiagnosticCategory::Decode;
			if (!FImagePayloadIO::DecodeToBGRA8(
				FileData,
				Context.ImageLimits,
				Width,
				Height,
				RawBGRA,
				DecodeError,
				&Context.OperationBudget,
				&DecodeCategory))
			{
				const FString Message = FString::Printf(TEXT("Local image decode failed for '%s': %s"), *WidgetName, *DecodeError);
				Context.Report.AddDiagnostic(
					EDesignImageDiagnosticOutcome::Failed,
					DecodeCategory,
					WidgetId,
					WidgetName,
					LocalPath,
					DecodeError,
					Message);
				return false;
			}

			FString ImportedObjectPath, ImportError;
			if (!CreateTextureAsset(BuildSharedImportPath(Context.ImportPath), SharedAssetName, RawBGRA, Width, Height, ImportedObjectPath, ImportError))
			{
				const FString Message = FString::Printf(TEXT("Local image import failed for '%s': %s"), *WidgetName, *ImportError);
				Context.Report.AddDiagnostic(
					EDesignImageDiagnosticOutcome::Failed,
					EDesignImageDiagnosticCategory::TextureImport,
					WidgetId,
					WidgetName,
					LocalPath,
					ImportError,
					Message);
				return false;
			}
			RegisterImportedTexture(LocalKey, ContentKey, ImportedObjectPath, TEXT("Imported local texture: "), InOutTexturePath, Context);
			return true;
		}

		if (!FDesignRemoteImageImporter::IsHttpUrl(InOutTexturePath))
		{
			const FString Message = FString::Printf(TEXT("Skipped non-asset path for '%s': %s"), *WidgetName, *InOutTexturePath);
			Context.Report.AddDiagnostic(
				EDesignImageDiagnosticOutcome::Skipped,
				EDesignImageDiagnosticCategory::UnsupportedPath,
				WidgetId,
				WidgetName,
				InOutTexturePath,
				TEXT("Path is neither a UE asset, an existing local file, nor an HTTP URL"),
				Message);
			return true;
		}

		if (!Context.bResolveRemoteImages)
		{
			const FString Reason = TEXT("Remote image import is disabled");
			const FString Message = FString::Printf(TEXT("Skipped remote image for '%s': %s"), *WidgetName, *Reason);
			Context.Report.AddDiagnostic(
				EDesignImageDiagnosticOutcome::Skipped,
				EDesignImageDiagnosticCategory::Disabled,
				WidgetId,
				WidgetName,
				InOutTexturePath,
				Reason,
				Message);
			return true;
		}

		const FString NormalizedUrl = TryNormalizeRemoteUrl(InOutTexturePath);
		const FString SourceKey = BuildSourceKeyForRemote(NormalizedUrl);

		if (TryReuseUrlCachedTexture(SourceKey, InOutTexturePath, Context))
		{
			return true;
		}

		if (const FImageProcessingFailure* PreviousFailure = Context.UrlProcessingErrors.Find(NormalizedUrl))
		{
			const FString Message = FString::Printf(
				TEXT("Image '%s' reused cached processing failure: %s (reason: %s)"),
				*WidgetName,
				*NormalizedUrl,
				*PreviousFailure->Reason);
			Context.Report.AddDiagnostic(
				EDesignImageDiagnosticOutcome::Failed,
				PreviousFailure->Category,
				WidgetId,
				WidgetName,
				NormalizedUrl,
				PreviousFailure->Reason,
				Message);
			return false;
		}

		// UrlDownloads is pre-populated by ResolveRemoteImages for every URL that
		// CollectRemoteUrls gathered (both sides normalize through TryNormalizeRemoteUrl).
		FUrlDownloadResult& Download = Context.UrlDownloads.FindChecked(NormalizedUrl);
		if (!Download.bSuccess)
		{
			const FString Message = FString::Printf(TEXT("Image '%s' download failed: %s (reason: %s)"), *WidgetName, *NormalizedUrl, *Download.Error);
			Context.Report.AddDiagnostic(
				EDesignImageDiagnosticOutcome::Failed,
				Download.FailureCategory,
				WidgetId,
				WidgetName,
				NormalizedUrl,
				Download.Error,
				Message);
			return false;
		}
		// Keep the URL outcome for repeated nodes, but move its payload into this one
		// processing attempt. The allocation is released on return instead of being
		// retained by the batch result until the whole tree finishes.
		TArray<uint8> CompressedData = Download.TakePayload();
		// The cumulative compressed-byte budget was already consumed per chunk while
		// the payload was received (see FImagePayloadIO::DownloadUrls). Do not bill it
		// a second time here; only the decode-pixel budget below still applies.

		const FString ContentHash = MakeContentHash(CompressedData);
		const FString ContentKey = BuildSourceScopedContentKey(SourceKey, ContentHash);
		if (TryReuseContentKeyedTexture(SourceKey, ContentKey, TEXT("Reused deduplicated content: "), InOutTexturePath, Context))
		{
			return true;
		}
		if (TryReusePersistedSharedTexture(SourceKey, ContentHash, ContentKey, InOutTexturePath, Context))
		{
			return true;
		}

		const FString SharedAssetName = BuildSharedTextureAssetName(ContentHash);

		int32 Width = 0;
		int32 Height = 0;
		TArray64<uint8> RawBGRA;
		FString DecodeError;
		EDesignImageDiagnosticCategory DecodeCategory = EDesignImageDiagnosticCategory::Decode;
		if (!FImagePayloadIO::DecodeToBGRA8(
			CompressedData,
			Context.ImageLimits,
			Width,
			Height,
			RawBGRA,
			DecodeError,
			&Context.OperationBudget,
			&DecodeCategory))
		{
			Context.UrlProcessingErrors.Add(NormalizedUrl, { DecodeCategory, DecodeError });
			const FString Message = FString::Printf(TEXT("Image '%s' decode failed: %s (reason: %s)"), *WidgetName, *NormalizedUrl, *DecodeError);
			Context.Report.AddDiagnostic(
				EDesignImageDiagnosticOutcome::Failed,
				DecodeCategory,
				WidgetId,
				WidgetName,
				NormalizedUrl,
				DecodeError,
				Message);
			return false;
		}
		CompressedData.Empty();

		FString ImportedObjectPath;
		FString ImportError;
		if (!CreateTextureAsset(BuildSharedImportPath(Context.ImportPath), SharedAssetName, RawBGRA, Width, Height, ImportedObjectPath, ImportError))
		{
			Context.UrlProcessingErrors.Add(NormalizedUrl, { EDesignImageDiagnosticCategory::TextureImport, ImportError });
			const FString Message = FString::Printf(TEXT("Image '%s' import failed: %s (reason: %s)"), *WidgetName, *NormalizedUrl, *ImportError);
			Context.Report.AddDiagnostic(
				EDesignImageDiagnosticOutcome::Failed,
				EDesignImageDiagnosticCategory::TextureImport,
				WidgetId,
				WidgetName,
				NormalizedUrl,
				ImportError,
				Message);
			return false;
		}
		RegisterImportedTexture(SourceKey, ContentKey, ImportedObjectPath, TEXT("Imported texture: "), InOutTexturePath, Context);
		return true;
	}

	void ResolveNodeRecursive(FUMGWidgetNode& Node, FResolveContext& Context)
	{
		FString TexturePath = Node.Brush.Image.TexturePath.Get(FString());
		if (!TexturePath.IsEmpty())
		{
			const FString DebugName = Node.Name.IsEmpty() ? Node.Id : Node.Name;
			FString ResolvedTexturePath = TexturePath;
			const int32 FirstDiagnostic = Context.Report.Result.Diagnostics.Num();
			ResolveTexturePath(
				ResolvedTexturePath,
				DebugName.IsEmpty() ? TEXT("UnnamedWidget") : DebugName,
				Node.Id,
				Context);
			// Success and cache-hit events need the same source identity as failures;
			// the output asset path alone cannot identify a later source retry.
			for (int32 Index = FirstDiagnostic; Index < Context.Report.Result.Diagnostics.Num(); ++Index)
			{
				FDesignImageDiagnostic& Diagnostic = Context.Report.Result.Diagnostics[Index];
				Diagnostic.NodeId = Node.Id;
				Diagnostic.NodeName = DebugName;
				Diagnostic.Resource = TexturePath.TrimStartAndEnd();
				if (IsAssetPath(ResolvedTexturePath))
				{
					Diagnostic.ResolvedAssetPath = ResolvedTexturePath;
				}
			}
			// Keep the canonical typed owner in sync after import-time path rewriting.
			Node.Brush.Image.TexturePath = ResolvedTexturePath;
		}

		for (FUMGWidgetNode& Child : Node.Children)
		{
			ResolveNodeRecursive(Child, Context);
		}
	}

	void CollectRemoteUrls(const FUMGWidgetNode& Node, TSet<FString>& OutUrls)
	{
		const FString TexturePath = Node.Brush.Image.TexturePath.Get(FString());
		const FString NormalizedUrl = TryNormalizeRemoteUrl(TexturePath);
		if (!NormalizedUrl.IsEmpty())
		{
			OutUrls.Add(NormalizedUrl);
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			CollectRemoteUrls(Child, OutUrls);
		}
	}
}

// Partial-success contract: imported textures stay saved even when other
// resources fail. The result records every outcome so generation can continue
// for unaffected images while callers surface the degradation.
bool FDesignRemoteImageImporter::IsHttpUrl(const FString& Value)
{
	return Value.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
		|| Value.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
}

FDesignImageImportResult FDesignRemoteImageImporter::ResolveRemoteImages(FUMGWidgetData& InOutData,
	const FDesignImageImportSettingsSnapshot& Settings)
{
	TSet<FString> RemoteUrls;
	if (Settings.bEnableRemoteImageImport)
	{
		CollectRemoteUrls(InOutData.RootWidget, RemoteUrls);
	}

	FResolveContext Context;
	FString LimitError;
	if (!FImagePayloadIO::ValidateUrlCount(RemoteUrls.Num(), Context.ImageLimits, LimitError))
	{
		Context.Report.AddDiagnostic(
			EDesignImageDiagnosticOutcome::Failed,
			EDesignImageDiagnosticCategory::Budget,
			FString(),
			FString(),
			FString(),
			LimitError,
			LimitError);
		UE_LOG(LogDesign2UMG, Error, TEXT("Remote image import rejected: %s"), *LimitError);
		return Context.Report.BuildResult(FString());
	}

	Context.ImportPath = NormalizeImportPath(Settings.RemoteImageImportPath);
	Context.TimeoutSeconds = FMath::Max(1.0f, Settings.RemoteImageTimeoutSeconds);
	Context.bResolveRemoteImages = Settings.bEnableRemoteImageImport;
	Context.bAllowUnsafeRemoteUrls = Settings.bAllowUnsafeRemoteImageUrls;

	if (RemoteUrls.Num() > 0)
	{
		const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
			MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Context.OperationBudget);
		FImagePayloadIO::DownloadUrls(
			RemoteUrls.Array(),
			Context.TimeoutSeconds,
			Context.bAllowUnsafeRemoteUrls,
			Context.ImageLimits,
			BulkBudget,
			Context.UrlDownloads);
	}

	ResolveNodeRecursive(InOutData.RootWidget, Context);
	if (Context.bTextureResourceUpdatePending)
	{
		FlushRenderingCommands();
	}

	FDesignImageImportResult Result = Context.Report.BuildResult(Context.ImportPath);
	if (Result.HasFailures())
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Remote image import finished with failures (%d)"), Result.FailedCount);
		UE_LOG(LogDesign2UMG, Warning, TEXT("%d image resolutions failed; generation will continue but some images may be missing."), Result.FailedCount);
	}
	else
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("Remote image import finished: imported=%d reused=%d refreshed=%d"),
			Result.ImportedCount, Result.ReusedCount, Result.RefreshedCount);
	}
	return Result;
}

// ============================================================================
// ResolveTransientTextures - preview path: download remote images as in-memory textures.
// ============================================================================

namespace
{
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
		return Texture;
	}
}

void FDesignRemoteImageImporter::ResolveTransientTextures(const FUMGWidgetNode& RootNode, TMap<FString, UTexture2D*>& OutTextureMap)
{
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!Settings || !Settings->bEnableRemoteImageImport)
	{
		return;
	}
	const float Timeout = FMath::Max(1.0f, Settings->RemoteImageTimeoutSeconds);

	TSet<FString> Urls;
	CollectRemoteUrls(RootNode, Urls);
	const FImageResourceLimits ImageLimits;
	DesignImageResources::FImageOperationBudget OperationBudget(ImageLimits);
	bool bTextureResourceUpdatePending = false;
	FString LimitError;
	if (!FImagePayloadIO::ValidateUrlCount(Urls.Num(), ImageLimits, LimitError))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Transient remote image import rejected: %s"), *LimitError);
		return;
	}

	TArray<FString> PendingUrls;
	for (const FString& Url : Urls)
	{
		if (!OutTextureMap.Contains(Url))
		{
			PendingUrls.Add(Url);
		}
	}

	TMap<FString, FUrlDownloadResult> Downloads;
	if (PendingUrls.Num() > 0)
	{
		const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
			MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(OperationBudget);
		FImagePayloadIO::DownloadUrls(PendingUrls, Timeout, Settings->bAllowUnsafeRemoteImageUrls, ImageLimits, BulkBudget, Downloads);
	}

	for (const FString& Url : PendingUrls)
	{
			FUrlDownloadResult& Download = Downloads.FindChecked(Url);
		if (!Download.bSuccess)
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("Transient download failed: %s (%s)"), *Url, *Download.Error);
			UE_LOG(LogDesign2UMG, Warning, TEXT("Image download failed: %s (reason: %s)"), *Url, *Download.Error);
			continue;
		}
		// The cumulative compressed-byte budget was already consumed per chunk while
		// the payload was received; only the decode-pixel budget below still applies.

			// Move the accepted body out of the batch result before decoding. Each loop
			// iteration releases its payload on every success/failure exit, while the URL
			// result still retains transport status and error text.
			TArray<uint8> CompressedData = Download.TakePayload();
			int32 Width = 0, Height = 0;
		TArray64<uint8> RawBGRA;
		FString Error;
			if (!FImagePayloadIO::DecodeToBGRA8(CompressedData, ImageLimits, Width, Height, RawBGRA, Error, &OperationBudget))
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("Transient decode failed: %s (%s)"), *Url, *Error);
			UE_LOG(LogDesign2UMG, Warning, TEXT("Image decode failed: %s (reason: %s)"), *Url, *Error);
			continue;
		}
		CompressedData.Empty();

		UTexture2D* Texture = CreateTransientTexture(RawBGRA, Width, Height);
		if (Texture)
		{
			OutTextureMap.Add(Url, Texture);
			bTextureResourceUpdatePending = true;
		}
	}

	if (bTextureResourceUpdatePending)
	{
		FlushRenderingCommands();
	}
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
