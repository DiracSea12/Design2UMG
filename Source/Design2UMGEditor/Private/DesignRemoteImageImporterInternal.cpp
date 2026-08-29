// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignRemoteImageImporterInternal.h"

#include "CoreGlobals.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"

namespace DesignRemoteImageImporterInternal
{
	namespace
	{
		constexpr int64 Bgra8BytesPerPixel = 4;
		constexpr float HttpWaitTickSeconds = 0.01f;

		bool TryAddWithinLimit(const uint64 Current, const uint64 Increment, const uint64 Limit, uint64& OutTotal)
		{
			if (Increment > Limit || Current > Limit - Increment)
			{
				return false;
			}
			OutTotal = Current + Increment;
			return true;
		}

		bool IsIpv4Literal(const FString& Host)
		{
			bool bHasDigit = false;
			for (const TCHAR Character : Host)
			{
				if (FChar::IsDigit(Character))
				{
					bHasDigit = true;
				}
				else if (Character != TEXT('.'))
				{
					return false;
				}
			}
			return bHasDigit;
		}

		bool ExtractUrlHost(const FString& Url, FString& OutHost)
		{
			const int32 SchemeEnd = Url.Find(TEXT("://"), ESearchCase::IgnoreCase, ESearchDir::FromStart);
			if (SchemeEnd == INDEX_NONE)
			{
				return false;
			}

			FString Authority = Url.Mid(SchemeEnd + 3);
			int32 PathStart = INDEX_NONE;
			for (const TCHAR Delimiter : { TEXT('/'), TEXT('?'), TEXT('#') })
			{
				const int32 Candidate = Authority.FindChar(Delimiter, PathStart) ? PathStart : INDEX_NONE;
				if (Candidate != INDEX_NONE)
				{
					Authority.LeftInline(Candidate);
				}
			}
			if (Authority.IsEmpty() || Authority.Contains(TEXT("@")))
			{
				return false;
			}

			if (Authority.StartsWith(TEXT("[")))
			{
				const int32 ClosingBracket = Authority.Find(TEXT("]"));
				if (ClosingBracket == INDEX_NONE)
				{
					return false;
				}
				OutHost = Authority.Mid(1, ClosingBracket - 1);
			}
			else
			{
				int32 PortSeparator = INDEX_NONE;
				OutHost = Authority.FindChar(TEXT(':'), PortSeparator)
					? Authority.Left(PortSeparator)
					: Authority;
			}

			OutHost.TrimStartAndEndInline();
			while (OutHost.RemoveFromEnd(TEXT(".")))
			{
			}
			OutHost.ToLowerInline();
			return !OutHost.IsEmpty();
		}

	}

	bool FImageOperationBudget::ConsumeCompressedBytes(const uint64 ByteCount, FString& OutError)
	{
		uint64 NewTotal = 0;
		if (!TryAddWithinLimit(TotalCompressedBytes, ByteCount, Limits.MaxTotalCompressedBytes, NewTotal))
		{
			OutError = FString::Printf(
				TEXT("Total compressed image data exceeds the operation limit (%llu bytes)"),
				static_cast<unsigned long long>(Limits.MaxTotalCompressedBytes));
			return false;
		}
		TotalCompressedBytes = NewTotal;
		OutError.Reset();
		return true;
	}

	bool FImageOperationBudget::ConsumeDecodedPixels(const int64 PixelCount, FString& OutError)
	{
		if (PixelCount < 0 || static_cast<uint64>(PixelCount) > TNumericLimits<uint64>::Max() / Bgra8BytesPerPixel)
		{
			OutError = TEXT("Decoded image size is invalid");
			return false;
		}

		const uint64 DecodedBytes = static_cast<uint64>(PixelCount) * Bgra8BytesPerPixel;
		uint64 NewTotal = 0;
		if (!TryAddWithinLimit(TotalDecodedBytes, DecodedBytes, Limits.MaxTotalDecodedBytes, NewTotal))
		{
			OutError = FString::Printf(
				TEXT("Total decoded image data exceeds the operation limit (%llu bytes)"),
				static_cast<unsigned long long>(Limits.MaxTotalDecodedBytes));
			return false;
		}
		TotalDecodedBytes = NewTotal;
		OutError.Reset();
		return true;
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

	bool FImagePayloadIO::ValidateCompressedByteCount(
		const uint64 ByteCount,
		const FImageResourceLimits& Limits,
		FString& OutError)
	{
		OutError.Reset();
		if (ByteCount > Limits.MaxCompressedBytes)
		{
			OutError = FString::Printf(
				TEXT("Image payload is too large (%llu bytes, limit %llu)"),
				static_cast<unsigned long long>(ByteCount),
				static_cast<unsigned long long>(Limits.MaxCompressedBytes));
			return false;
		}
		return true;
	}

	bool FImagePayloadIO::ValidateDimensions(
		const int32 Width,
		const int32 Height,
		const FImageResourceLimits& Limits,
		FString& OutError)
	{
		OutError.Reset();
		if (Width <= 0 || Height <= 0)
		{
			OutError = TEXT("Invalid image dimensions");
			return false;
		}
		if (Width > Limits.MaxDimension || Height > Limits.MaxDimension)
		{
			OutError = FString::Printf(
				TEXT("Image dimensions exceed the limit (%dx%d, maximum side %d)"),
				Width,
				Height,
				Limits.MaxDimension);
			return false;
		}
		const int64 MaxBgraPixels = TNumericLimits<int64>::Max() / Bgra8BytesPerPixel;
		const int64 EffectiveMaxPixels = FMath::Min(Limits.MaxPixels, MaxBgraPixels);
		if (EffectiveMaxPixels < 1
			|| static_cast<int64>(Width) > EffectiveMaxPixels / static_cast<int64>(Height))
		{
			OutError = FString::Printf(
				TEXT("Image pixel count exceeds the limit (%dx%d, limit %lld)"),
				Width,
				Height,
				static_cast<long long>(Limits.MaxPixels));
			return false;
		}
		return true;
	}

	bool FImagePayloadIO::ValidateUrlCount(
		const int32 UrlCount,
		const FImageResourceLimits& Limits,
		FString& OutError)
	{
		OutError.Reset();
		if (UrlCount > Limits.MaxUrlsPerOperation)
		{
			OutError = FString::Printf(
				TEXT("Too many unique remote images (%d, limit %d)"),
				UrlCount,
				Limits.MaxUrlsPerOperation);
			return false;
		}
		return true;
	}

	bool FImagePayloadIO::ValidateRemoteUrl(
		const FString& Url,
		const bool bAllowUnsafeRemoteUrls,
		FString& OutError)
	{
		OutError.Reset();
		const bool bHttps = Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
		const bool bHttp = Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase);
		if (!bHttps && !(bAllowUnsafeRemoteUrls && bHttp))
		{
			OutError = TEXT("Remote image URL must use HTTPS");
			return false;
		}

		FString Host;
		if (!ExtractUrlHost(Url, Host))
		{
			OutError = TEXT("Remote image URL has an invalid host");
			return false;
		}
		if (!bAllowUnsafeRemoteUrls
			&& (Host == TEXT("localhost")
				|| Host.EndsWith(TEXT(".localhost"))
				|| Host.EndsWith(TEXT(".local"))
				|| !Host.Contains(TEXT("."))
				|| Host.Contains(TEXT(":"))
				|| IsIpv4Literal(Host)))
		{
			OutError = TEXT("Remote image URL points to a local-style or IP-literal host");
			return false;
		}
		return true;
	}

	bool FImagePayloadIO::LoadLocalFile(
		const FString& FilePath,
		const FImageResourceLimits& Limits,
		TArray<uint8>& OutData,
		FString& OutError)
	{
		OutData.Reset();
		const int64 FileSize = IFileManager::Get().FileSize(*FilePath);
		if (FileSize < 0)
		{
			OutError = TEXT("Local image file is unavailable");
			return false;
		}
		if (!ValidateCompressedByteCount(static_cast<uint64>(FileSize), Limits, OutError))
		{
			return false;
		}
		if (!FFileHelper::LoadFileToArray(OutData, *FilePath))
		{
			OutError = TEXT("Local image file could not be read");
			return false;
		}
		if (!ValidateCompressedByteCount(static_cast<uint64>(OutData.Num()), Limits, OutError))
		{
			OutData.Reset();
			return false;
		}
		return true;
	}

	void FImagePayloadIO::DownloadUrls(
		const TArray<FString>& Urls,
		const float TimeoutSeconds,
		const bool bAllowUnsafeRemoteUrls,
		const FImageResourceLimits& Limits,
		TMap<FString, FUrlDownloadResult>& OutResults)
	{
		struct FDownloadState
		{
			FCriticalSection Mutex;
			TAtomic<bool> bDone{ false };
			TAtomic<bool> bPayloadLimitExceeded{ false };
			/** Guarded by Mutex: set before cancelling a user-aborted download so its late HTTP callback keeps the cancel error. */
			bool bCancelledByUser = false;
			bool bSuccess = false;
			int32 ResponseCode = 0;
			TArray<uint8> Downloaded;
			FString ErrorString;
		};
		struct FPendingRequest
		{
			FString Url;
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request;
			TSharedRef<FDownloadState, ESPMode::ThreadSafe> State;
		};

		const float EffectiveTimeoutSeconds = FMath::Max(1.0f, TimeoutSeconds);
		const double Deadline = FPlatformTime::Seconds() + EffectiveTimeoutSeconds;
		TArray<FPendingRequest> Pending;

		for (const FString& Url : Urls)
		{
			FUrlDownloadResult& Result = OutResults.FindOrAdd(Url);
			Result.bSuccess = false;
			Result.Data.Reset();
			Result.Error.Reset();
			if (!ValidateRemoteUrl(Url, bAllowUnsafeRemoteUrls, Result.Error))
			{
				continue;
			}

			const TSharedRef<FDownloadState, ESPMode::ThreadSafe> State = MakeShared<FDownloadState, ESPMode::ThreadSafe>();
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
			Request->SetURL(Url);
			Request->SetVerb(TEXT("GET"));
			Request->SetHeader(TEXT("User-Agent"), TEXT("Design2UMG/RemoteImageImporter"));
			Request->SetTimeout(EffectiveTimeoutSeconds);
			Request->OnRequestProgress64().BindLambda(
				[State, MaxCompressedBytes = Limits.MaxCompressedBytes](FHttpRequestPtr ProgressRequest, uint64, uint64 BytesReceived)
				{
					if (BytesReceived > MaxCompressedBytes)
					{
						State->bPayloadLimitExceeded = true;
						ProgressRequest->CancelRequest();
					}
				});
			Request->OnProcessRequestComplete().BindLambda(
				[State, Limits](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
				{
					FScopeLock Lock(&State->Mutex);
					if (State->bCancelledByUser)
					{
						// Late callback for a user-cancelled download: keep the cancel error.
						State->bDone = true;
						return;
					}
					if (State->bPayloadLimitExceeded)
					{
						State->ErrorString = TEXT("Image payload exceeded the byte limit during download");
					}
					else if (!bWasSuccessful || !Response.IsValid())
					{
						State->ErrorString = TEXT("Network request failed");
					}
					else
					{
						State->ResponseCode = Response->GetResponseCode();
						if (State->ResponseCode < 200 || State->ResponseCode >= 300)
						{
							State->ErrorString = FString::Printf(TEXT("Server returned an error (HTTP %d)"), State->ResponseCode);
						}
						else if (ValidateCompressedByteCount(Response->GetContentLength(), Limits, State->ErrorString))
						{
							const TArray<uint8>& ResponseContent = Response->GetContent();
							if (ValidateCompressedByteCount(static_cast<uint64>(ResponseContent.Num()), Limits, State->ErrorString))
							{
								State->Downloaded = ResponseContent;
								if (State->Downloaded.Num() == 0)
								{
									State->ErrorString = TEXT("Server returned an empty response");
								}
								else
								{
									State->bSuccess = true;
								}
							}
						}
					}
					State->bDone = true;
				});

			if (!Request->ProcessRequest())
			{
				Result.Error = TEXT("Unable to start network request");
				continue;
			}
			Pending.Add({ Url, Request, State });
		}

		FHttpManager& HttpManager = FHttpModule::Get().GetHttpManager();
		const auto CountPending = [&Pending]()
		{
			int32 StillPending = 0;
			for (const FPendingRequest& Entry : Pending)
			{
				if (!Entry.State->bDone)
				{
					++StillPending;
				}
			}
			return StillPending;
		};

		// Interactive sessions keep the editor responsive through a cancellable
		// slow task; unattended/automation runs keep the legacy tick+sleep pump
		// so batch and regression behavior stays identical.
		const bool bInteractiveWait = !FApp::IsUnattended()
			&& !GIsAutomationTesting
			&& FSlateApplication::IsInitialized();
		bool bUserCancelled = false;
		int32 Remaining = Pending.Num();
		if (bInteractiveWait && Remaining > 0)
		{
			FScopedSlowTask SlowTask(static_cast<float>(Pending.Num()), NSLOCTEXT("Design2UMG", "DownloadingRemoteImages", "Downloading remote images..."), true);
			SlowTask.MakeDialog(/*bShowCancelButton=*/true);
			while (Remaining > 0 && FPlatformTime::Seconds() < Deadline)
			{
				HttpManager.Tick(HttpWaitTickSeconds);
				if (SlowTask.ShouldCancel())
				{
					bUserCancelled = true;
					for (FPendingRequest& Entry : Pending)
					{
						if (Entry.State->bDone)
						{
							continue;
						}
						FScopeLock Lock(&Entry.State->Mutex);
						Entry.State->bCancelledByUser = true;
						Entry.State->ErrorString = TEXT("cancelled by user");
						Entry.Request->CancelRequest();
					}
					break;
				}
				const int32 CompletedThisRound = Remaining - CountPending();
				Remaining -= CompletedThisRound;
				SlowTask.EnterProgressFrame(static_cast<float>(CompletedThisRound));
			}
		}
		else
		{
			while (Remaining > 0 && FPlatformTime::Seconds() < Deadline)
			{
				HttpManager.Tick(HttpWaitTickSeconds);
				FPlatformProcess::Sleep(HttpWaitTickSeconds);
				Remaining = CountPending();
			}
		}

		for (FPendingRequest& Entry : Pending)
		{
			FUrlDownloadResult& Result = OutResults.FindOrAdd(Entry.Url);
			if (!Entry.State->bDone)
			{
				if (bUserCancelled)
				{
					// Every unfinished request was marked and given the cancel
					// error above; its late HTTP callback keeps that error.
					FScopeLock Lock(&Entry.State->Mutex);
					Result.Error = Entry.State->ErrorString;
					continue;
				}

				// Late callbacks for cancelled requests are dropped by the
				// State mutex/bDone guard, so no delegate unbinding is needed.
				Entry.Request->CancelRequest();
				Result.Error = TEXT("Network request timed out");
				continue;
			}

			FScopeLock Lock(&Entry.State->Mutex);
			if (Entry.State->bSuccess)
			{
				Result.bSuccess = true;
				Result.Data = MoveTemp(Entry.State->Downloaded);
			}
			else
			{
				Result.Error = Entry.State->ErrorString.IsEmpty()
					? FString::Printf(TEXT("Network request failed (HTTP %d)"), Entry.State->ResponseCode)
					: Entry.State->ErrorString;
			}
		}
	}

	bool FImagePayloadIO::DecodeToBGRA8(
		const TArray<uint8>& CompressedData,
		const FImageResourceLimits& Limits,
		int32& OutWidth,
		int32& OutHeight,
		TArray64<uint8>& OutRawData,
		FString& OutError,
		FImageOperationBudget* OperationBudget)
	{
		OutWidth = 0;
		OutHeight = 0;
		OutRawData.Reset();
		OutError.Reset();
		if (CompressedData.Num() == 0)
		{
			OutError = TEXT("Image data is empty");
			return false;
		}
		if (!ValidateCompressedByteCount(static_cast<uint64>(CompressedData.Num()), Limits, OutError))
		{
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
		if (!ValidateDimensions(OutWidth, OutHeight, Limits, OutError))
		{
			return false;
		}
		if (OperationBudget
			&& !OperationBudget->ConsumeDecodedPixels(static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight), OutError))
		{
			return false;
		}

		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, OutRawData))
		{
			OutError = TEXT("Image decode failed");
			return false;
		}

		const int64 PixelCount = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight);
		const int64 ExpectedSize = PixelCount * Bgra8BytesPerPixel;
		if (OutRawData.Num() < ExpectedSize)
		{
			OutError = TEXT("Decoded image data size is invalid");
			return false;
		}

		return true;
	}
}
