// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignRemoteImageImporter.h"

#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "UObject/SavePackage.h"
#include "TextureResource.h"

namespace
{
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

	struct FResolveContext
	{
		FString ImportPath;
		FString FrameName;
		float TimeoutSeconds = 20.0f;
		int32 ImportedCount = 0;
		int32 ReusedCount = 0;
		int32 RepairedCount = 0;
		int32 FailedCount = 0;
		TArray<FString> Messages;
		TMap<FString, FString> UrlToAssetPath;      // URL去重缓存
		TMap<FString, FString> ContentHashToAsset;   // 内容hash去重缓存（本地/远程共享）
	};

	void AddReportMessage(FResolveContext& Context, const FString& Message)
	{
		if (Context.Messages.Num() < 32)
		{
			Context.Messages.Add(Message);
		}
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

		FString BuildTextureAssetName(const FString& FrameName, const FString& WidgetName, const FString& WidgetId, const FString& SourceKey)
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

	FString BuildSubImportPath(const FString& ImportPath, const FString& FrameName)
	{
		return ImportPath / SanitizeAssetName(FrameName);
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

	bool DownloadUrl(const FString& Url, float TimeoutSeconds, TArray<uint8>& OutData, FString& OutError)
	{
		bool bDone = false;
		bool bSuccess = false;
		int32 ResponseCode = 0;
		TArray<uint8> Downloaded;
		FString ErrorString;

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(Url);
		Request->SetVerb(TEXT("GET"));
		Request->SetHeader(TEXT("User-Agent"), TEXT("Design2UMG/RemoteImageImporter"));
		Request->SetTimeout(FMath::Max(1.0f, TimeoutSeconds));
		Request->OnProcessRequestComplete().BindLambda(
			[&bDone, &bSuccess, &ResponseCode, &Downloaded, &ErrorString](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
			{
				bDone = true;
				if (!bWasSuccessful || !Response.IsValid())
				{
					ErrorString = TEXT("网络请求失败");
					return;
				}

				ResponseCode = Response->GetResponseCode();
				if (ResponseCode < 200 || ResponseCode >= 300)
				{
					ErrorString = FString::Printf(TEXT("服务器返回错误 (HTTP %d)"), ResponseCode);
					return;
				}

				Downloaded = Response->GetContent();
				if (Downloaded.Num() == 0)
				{
					ErrorString = TEXT("服务器返回了空内容");
					return;
				}

				bSuccess = true;
			});

		if (!Request->ProcessRequest())
		{
			OutError = TEXT("无法发起网络请求");
			return false;
		}

		FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::FullFlush);
		if (!bDone || !bSuccess)
		{
			OutError = ErrorString.IsEmpty()
				? FString::Printf(TEXT("网络请求失败 (HTTP %d)"), ResponseCode)
				: ErrorString;
			return false;
		}

		OutData = MoveTemp(Downloaded);
		return true;
	}

	bool DecodeToBGRA8(const TArray<uint8>& CompressedData, int32& OutWidth, int32& OutHeight, TArray64<uint8>& OutRawData, FString& OutError)
	{
		if (CompressedData.Num() == 0)
		{
			OutError = TEXT("图片数据为空");
			return false;
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const EImageFormat Format = ImageWrapperModule.DetectImageFormat(CompressedData.GetData(), CompressedData.Num());
		if (Format == EImageFormat::Invalid)
		{
			OutError = TEXT("不支持的图片格式");
			return false;
		}

		TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
		{
			OutError = TEXT("图片数据解析失败");
			return false;
		}

		OutWidth = Wrapper->GetWidth();
		OutHeight = Wrapper->GetHeight();
		if (OutWidth <= 0 || OutHeight <= 0)
		{
			OutError = TEXT("图片尺寸无效");
			return false;
		}

		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, OutRawData))
		{
			OutError = TEXT("图片解码失败");
			return false;
		}

		const int64 ExpectedSize = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4;
		if (OutRawData.Num() < ExpectedSize)
		{
			OutError = TEXT("图片解码数据大小异常");
			return false;
		}

		return true;
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
			OutError = FString::Printf(TEXT("资产路径无效: %s"), *LongPackageName);
			return false;
		}

		const FString ObjectPath = BuildObjectPath(LongPackageName, AssetName);

		UPackage* Package = CreatePackage(*LongPackageName);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("无法创建资产包: %s"), *LongPackageName);
			return false;
		}

		UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Texture)
		{
			OutError = FString::Printf(TEXT("无法创建纹理对象: %s"), *AssetName);
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
			OutError = FString::Printf(TEXT("无法保存资产包: %s"), *PackageFilename);
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
			OutError = TEXT("参数无效");
			return false;
		}

		UPackage* Package = Texture->GetPackage();
		if (!Package)
		{
			OutError = TEXT("纹理包无效");
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
			OutError = FString::Printf(TEXT("保存纹理失败: %s"), *PackageFilename);
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

		// 本地文件路径：直接读取并创建纹理资产
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
				++Context.ReusedCount;
				return true;
			}

			TArray<uint8> FileData;
			if (!FFileHelper::LoadFileToArray(FileData, *LocalPath))
			{
				++Context.FailedCount;
				AddReportMessage(Context, FString::Printf(TEXT("本地文件读取失败 '%s': %s"), *WidgetName, *LocalPath));
				return false;
			}

			// 以内容 hash 作为缓存键，避免同路径文件更新后误复用旧纹理。
			const FString ContentHash = MakeContentHash(FileData);
			const FString ContentKey = FString::Printf(TEXT("local-content://%s"), *ContentHash);
			if (const FString* ExistingPath = Context.ContentHashToAsset.Find(ContentKey))
			{
				if (UTexture2D* CachedTexture = LoadObject<UTexture2D>(nullptr, **ExistingPath))
				{
					EnsureUiTextureReady(CachedTexture);
				}
				InOutTexturePath = *ExistingPath;
				++Context.ReusedCount;
				Context.UrlToAssetPath.Add(LocalKey, *ExistingPath);
				return true;
			}

				const FString SubDir = BuildSubImportPath(Context.ImportPath, Context.FrameName);
				const FString SourceKey = FString::Printf(TEXT("%s#%s"), *LocalKey, *ContentHash.Left(10));
				const FString AssetName = BuildTextureAssetName(Context.FrameName, WidgetName, WidgetId, SourceKey);
				const FString ObjectPath = BuildObjectPathFromImport(SubDir, AssetName);
				if (UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *ObjectPath))
				{
					int32 SourceWidth = 0;
					int32 SourceHeight = 0;
					TArray64<uint8> SourceRawBGRA;
					FString SourceDecodeError;
					const bool bDecoded = DecodeToBGRA8(FileData, SourceWidth, SourceHeight, SourceRawBGRA, SourceDecodeError);
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
								++Context.RepairedCount;
								AddReportMessage(Context, FString::Printf(TEXT("修复本地纹理: %s (%dx%d -> %dx%d)"),
									*ObjectPath, ExistingWidth, ExistingHeight, SourceWidth, SourceHeight));
							}
							else
							{
								AddReportMessage(Context, FString::Printf(TEXT("修复纹理失败 '%s': %s"), *WidgetName, *RewriteError));
							}
						}
					}

					EnsureUiTextureReady(Existing);
					InOutTexturePath = ObjectPath;
					++Context.ReusedCount;
					Context.UrlToAssetPath.Add(LocalKey, ObjectPath);
					Context.ContentHashToAsset.Add(ContentKey, ObjectPath);
					return true;
				}

			int32 Width = 0, Height = 0;
			TArray64<uint8> RawBGRA;
			FString DecodeError;
			if (!DecodeToBGRA8(FileData, Width, Height, RawBGRA, DecodeError))
			{
				++Context.FailedCount;
				AddReportMessage(Context, FString::Printf(TEXT("本地图片解码失败 '%s': %s"), *WidgetName, *DecodeError));
				return false;
			}

			FString ImportedObjectPath, ImportError;
			if (!CreateTextureAsset(SubDir, AssetName, RawBGRA, Width, Height, ImportedObjectPath, ImportError))
			{
				++Context.FailedCount;
				AddReportMessage(Context, FString::Printf(TEXT("本地图片导入失败 '%s': %s"), *WidgetName, *ImportError));
				return false;
			}

			InOutTexturePath = ImportedObjectPath;
			++Context.ImportedCount;
			Context.UrlToAssetPath.Add(LocalKey, ImportedObjectPath);
			Context.ContentHashToAsset.Add(ContentKey, ImportedObjectPath);
			AddReportMessage(Context, FString::Printf(TEXT("已导入本地纹理: %s"), *ImportedObjectPath));
			return true;
		}

		if (!IsRemoteUrl(InOutTexturePath))
		{
			AddReportMessage(Context, FString::Printf(TEXT("跳过非资产路径 '%s': %s"), *WidgetName, *InOutTexturePath));
			return true;
		}

		const FString NormalizedUrl = NormalizeUrl(InOutTexturePath);
		const FString SubDir = BuildSubImportPath(Context.ImportPath, Context.FrameName);
		const FString SourceKey = BuildSourceKeyForRemote(NormalizedUrl);
		const FString AssetName = BuildTextureAssetName(Context.FrameName, WidgetName, WidgetId, SourceKey);
		const FString ObjectPath = BuildObjectPathFromImport(SubDir, AssetName);

		// URL去重：已下载过的直接复用
		if (const FString* CachedPath = Context.UrlToAssetPath.Find(NormalizedUrl))
		{
			if (UTexture2D* CachedTexture = LoadObject<UTexture2D>(nullptr, **CachedPath))
			{
				EnsureUiTextureReady(CachedTexture);
			}
			InOutTexturePath = *CachedPath;
			++Context.ReusedCount;
			return true;
		}

		// 构建子目录和命名: ImportedImages/{FrameName}/T_{FrameName}_{NodeName}
		// Reuse existing asset
		if (UTexture2D* ExistingTexture = LoadObject<UTexture2D>(nullptr, *ObjectPath))
		{
			EnsureUiTextureReady(ExistingTexture);
			InOutTexturePath = ObjectPath;
			++Context.ReusedCount;
			Context.UrlToAssetPath.Add(NormalizedUrl, ObjectPath);
			AddReportMessage(Context, FString::Printf(TEXT("复用已有纹理: %s"), *ObjectPath));
			return true;
		}

		TArray<uint8> CompressedData;
		FString DownloadError;
		if (!DownloadUrl(NormalizedUrl, Context.TimeoutSeconds, CompressedData, DownloadError))
		{
			++Context.FailedCount;
			AddReportMessage(Context, FString::Printf(TEXT("图片 '%s' 下载失败: %s (原因: %s)"), *WidgetName, *NormalizedUrl, *DownloadError));
			return false;
		}

		// 内容hash去重：不同URL但内容相同的图片复用同一资产
		const FString ContentHash = MakeContentHash(CompressedData);
		if (const FString* ExistingPath = Context.ContentHashToAsset.Find(ContentHash))
		{
			if (UTexture2D* CachedTexture = LoadObject<UTexture2D>(nullptr, **ExistingPath))
			{
				EnsureUiTextureReady(CachedTexture);
			}
			InOutTexturePath = *ExistingPath;
			++Context.ReusedCount;
			Context.UrlToAssetPath.Add(NormalizedUrl, *ExistingPath);
			AddReportMessage(Context, FString::Printf(TEXT("内容去重复用: %s"), **ExistingPath));
			return true;
		}

		int32 Width = 0;
		int32 Height = 0;
		TArray64<uint8> RawBGRA;
		FString DecodeError;
		if (!DecodeToBGRA8(CompressedData, Width, Height, RawBGRA, DecodeError))
		{
			++Context.FailedCount;
			AddReportMessage(Context, FString::Printf(TEXT("图片 '%s' 解码失败: %s (原因: %s)"), *WidgetName, *NormalizedUrl, *DecodeError));
			return false;
		}

		FString ImportedObjectPath;
		FString ImportError;
		if (!CreateTextureAsset(SubDir, AssetName, RawBGRA, Width, Height, ImportedObjectPath, ImportError))
		{
			++Context.FailedCount;
			AddReportMessage(Context, FString::Printf(TEXT("图片 '%s' 导入失败: %s (原因: %s)"), *WidgetName, *NormalizedUrl, *ImportError));
			return false;
		}

		InOutTexturePath = ImportedObjectPath;
		++Context.ImportedCount;
		Context.UrlToAssetPath.Add(NormalizedUrl, ImportedObjectPath);
		Context.ContentHashToAsset.Add(ContentHash, ImportedObjectPath);
		AddReportMessage(Context, FString::Printf(TEXT("已导入纹理: %s"), *ImportedObjectPath));
		return true;
	}

	void ResolveNodeRecursive(FUMGWidgetNode& Node, FResolveContext& Context)
	{
		if (FString* TexturePath = Node.Properties.Find(TEXT("TexturePath")))
		{
			const FString DebugName = Node.Name.IsEmpty() ? Node.Id : Node.Name;
			ResolveTexturePath(
				*TexturePath,
				DebugName.IsEmpty() ? TEXT("UnnamedWidget") : DebugName,
				Node.Id,
				Context);
			// 同步 typed 字段，避免 typed-first 读到未 resolve 的原始 URL
			Node.Brush.TexturePath = *TexturePath;
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
			*OutReport = TEXT("远程图片导入: 设置不可用");
		}
		return false;
	}

	if (!Settings->bEnableRemoteImageImport)
	{
		if (OutReport)
		{
			*OutReport = TEXT("远程图片导入: 已禁用");
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
			FString Summary = FString::Printf(
				TEXT("远程图片导入: 新增=%d 复用=%d 修复=%d 失败=%d 路径=%s"),
				Context.ImportedCount,
				Context.ReusedCount,
				Context.RepairedCount,
				Context.FailedCount,
				*Context.ImportPath);
		if (Context.Messages.Num() > 0)
		{
			Summary += TEXT("\n");
			Summary += FString::Join(Context.Messages, TEXT("\n"));
		}
		*OutReport = MoveTemp(Summary);
	}

	if (Context.FailedCount > 0)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Remote image import finished with failures (%d)"), Context.FailedCount);
		UE_LOG(LogDesign2UMG, Warning, TEXT("%d 张图片下载失败，生成将继续但部分图片可能缺失。"), Context.FailedCount);
		return false;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("Remote image import finished: imported=%d reused=%d repaired=%d"),
		Context.ImportedCount, Context.ReusedCount, Context.RepairedCount);
	return true;
}

// ============================================================================
// ResolveTransientTextures - 预览用：下载远程图片为内存纹理
// ============================================================================

namespace
{
	void CollectRemoteUrls(const FUMGWidgetNode& Node, TSet<FString>& OutUrls)
	{
		if (const FString* TexturePath = Node.Properties.Find(TEXT("TexturePath")))
		{
			const FString Trimmed = TexturePath->TrimStartAndEnd();
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

		// RF_DuplicateTransient 使 FDuplicateDataWriter 写 nullptr 而非复制，避免空 mip 副本。
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
		if (!DownloadUrl(NormalizeUrl(Url), Timeout, CompressedData, Error))
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("Transient download failed: %s (%s)"), *Url, *Error);
			UE_LOG(LogDesign2UMG, Warning, TEXT("图片下载失败: %s (原因: %s)"), *Url, *Error);
			continue;
		}

		int32 Width = 0, Height = 0;
		TArray64<uint8> RawBGRA;
		if (!DecodeToBGRA8(CompressedData, Width, Height, RawBGRA, Error))
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("Transient decode failed: %s (%s)"), *Url, *Error);
			UE_LOG(LogDesign2UMG, Warning, TEXT("图片解码失败: %s (原因: %s)"), *Url, *Error);
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

