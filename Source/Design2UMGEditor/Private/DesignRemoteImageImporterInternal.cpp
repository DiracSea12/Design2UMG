// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignRemoteImageImporterInternal.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "HAL/CriticalSection.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"

namespace DesignRemoteImageImporterInternal
{
	namespace
	{
		FCriticalSection GImportedTextureIndexMutex;
		TMap<FString, TMap<FString, FString>> GImportedTexturePathsByRoot;
		TSet<FString> GImportedTextureIndexedRoots;

		FString NormalizeImportPathForIndex(const FString& InPath)
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

		FString MakeImportedTextureRootKey(const FString& ImportPath)
		{
			FString RootKey = NormalizeImportPathForIndex(ImportPath);
			RootKey.ToLowerInline();
			return RootKey;
		}

		void BuildImportedTextureIndexIfNeeded(const FString& ImportPath)
		{
			const FString RootKey = MakeImportedTextureRootKey(ImportPath);
			{
				FScopeLock Lock(&GImportedTextureIndexMutex);
				if (GImportedTextureIndexedRoots.Contains(RootKey))
				{
					return;
				}
			}

			TMap<FString, FString> IndexedPaths;
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			TArray<FAssetData> Assets;
			AssetRegistryModule.Get().GetAssetsByPath(FName(*NormalizeImportPathForIndex(ImportPath)), Assets, true);
			for (const FAssetData& Asset : Assets)
			{
				if (Asset.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName())
				{
					IndexedPaths.Add(Asset.AssetName.ToString(), Asset.GetObjectPathString());
				}
			}

			FScopeLock Lock(&GImportedTextureIndexMutex);
			GImportedTexturePathsByRoot.Add(RootKey, MoveTemp(IndexedPaths));
			GImportedTextureIndexedRoots.Add(RootKey);
		}
	}

	void FResolveReport::AddMessage(const FString& Message)
	{
		if (Messages.Num() < 32)
		{
			Messages.Add(Message);
		}
	}

	FString FResolveReport::BuildSummary(const FString& ImportPath) const
	{
		FString Summary = FString::Printf(
			TEXT("Remote image import: imported=%d reused=%d repaired=%d failed=%d path=%s"),
			ImportedCount,
			ReusedCount,
			RepairedCount,
			FailedCount,
			*ImportPath);
		if (Messages.Num() > 0)
		{
			Summary += TEXT("\n");
			Summary += FString::Join(Messages, TEXT("\n"));
		}
		return Summary;
	}

	void FImportedTextureIndex::Remember(const FString& ImportPath, const FString& AssetName, const FString& ObjectPath)
	{
		FScopeLock Lock(&GImportedTextureIndexMutex);
		const FString RootKey = MakeImportedTextureRootKey(ImportPath);
		GImportedTexturePathsByRoot.FindOrAdd(RootKey).Add(AssetName, ObjectPath);
		GImportedTextureIndexedRoots.Add(RootKey);
	}

	void FImportedTextureIndex::Forget(const FString& ImportPath, const FString& AssetName)
	{
		FScopeLock Lock(&GImportedTextureIndexMutex);
		if (TMap<FString, FString>* IndexedPaths = GImportedTexturePathsByRoot.Find(MakeImportedTextureRootKey(ImportPath)))
		{
			IndexedPaths->Remove(AssetName);
		}
	}

	bool FImportedTextureIndex::TryGetPersistedPath(const FString& ImportPath, const FString& AssetName, FString& OutObjectPath)
	{
		BuildImportedTextureIndexIfNeeded(ImportPath);

		FScopeLock Lock(&GImportedTextureIndexMutex);
		if (const TMap<FString, FString>* IndexedPaths = GImportedTexturePathsByRoot.Find(MakeImportedTextureRootKey(ImportPath)))
		{
			if (const FString* FoundPath = IndexedPaths->Find(AssetName))
			{
				OutObjectPath = *FoundPath;
				return true;
			}
		}

		return false;
	}

	bool FImagePayloadIO::DownloadUrl(const FString& Url, float TimeoutSeconds, TArray<uint8>& OutData, FString& OutError)
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
					ErrorString = TEXT("Network request failed");
					return;
				}

				ResponseCode = Response->GetResponseCode();
				if (ResponseCode < 200 || ResponseCode >= 300)
				{
					ErrorString = FString::Printf(TEXT("Server returned an error (HTTP %d)"), ResponseCode);
					return;
				}

				Downloaded = Response->GetContent();
				if (Downloaded.Num() == 0)
				{
					ErrorString = TEXT("Server returned an empty response");
					return;
				}

				bSuccess = true;
			});

		if (!Request->ProcessRequest())
		{
			OutError = TEXT("Unable to start network request");
			return false;
		}

		FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::FullFlush);
		if (!bDone || !bSuccess)
		{
			OutError = ErrorString.IsEmpty()
				? FString::Printf(TEXT("Network request failed (HTTP %d)"), ResponseCode)
				: ErrorString;
			return false;
		}

		OutData = MoveTemp(Downloaded);
		return true;
	}

	bool FImagePayloadIO::DecodeToBGRA8(
		const TArray<uint8>& CompressedData,
		int32& OutWidth,
		int32& OutHeight,
		TArray64<uint8>& OutRawData,
		FString& OutError)
	{
		if (CompressedData.Num() == 0)
		{
			OutError = TEXT("Image data is empty");
			return false;
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const EImageFormat Format = ImageWrapperModule.DetectImageFormat(CompressedData.GetData(), CompressedData.Num());
		if (Format == EImageFormat::Invalid)
		{
			OutError = TEXT("Unsupported image format");
			return false;
		}

		TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
		{
			OutError = TEXT("Failed to parse image data");
			return false;
		}

		OutWidth = Wrapper->GetWidth();
		OutHeight = Wrapper->GetHeight();
		if (OutWidth <= 0 || OutHeight <= 0)
		{
			OutError = TEXT("Invalid image dimensions");
			return false;
		}

		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, OutRawData))
		{
			OutError = TEXT("Image decode failed");
			return false;
		}

		const int64 ExpectedSize = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4;
		if (OutRawData.Num() < ExpectedSize)
		{
			OutError = TEXT("Decoded image data size is invalid");
			return false;
		}

		return true;
	}
}
