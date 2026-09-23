// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignRemoteImageImporterInternal.h"

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
		// Cap individual diagnostic lines to bound resource-resolution reports.
		constexpr int32 MaxResolveReportMessages = 32;

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

	void FResolveReport::AddMessage(const FString& Message)
	{
		if (Messages.Num() < MaxResolveReportMessages)
		{
			Messages.Add(Message);
		}
	}

	FString FResolveReport::BuildSummary(const FString& ImportPath) const
	{
		FString Summary = FString::Printf(
			TEXT("Remote image import: imported=%d reused=%d refreshed=%d failed=%d path=%s"),
			ImportedCount,
			ReusedCount,
			RefreshedCount,
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
		const DesignImageResources::FImageResourceLimits& Limits,
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
		const DesignImageResources::FImageResourceLimits& Limits,
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
		const DesignImageResources::FImageResourceLimits& Limits,
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
		const DesignImageResources::FImageResourceLimits& Limits,
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

	namespace
	{
		/** Shared per-download outcome state: written by the HTTP callbacks, polled by the wait loop. */
		struct FDownloadState
		{
			FCriticalSection Mutex;
			TAtomic<bool> bDone{ false };
			TAtomic<bool> bPayloadLimitExceeded{ false };
			/** Guarded by Mutex: set before cancelling a user-aborted download so its late HTTP callback keeps the cancel error. */
			bool bCancelledByUser = false;
			/** Guarded by Mutex: set when the wait loop cancels this request after its own per-request deadline; the late HTTP callback keeps the timeout error. */
			bool bTimedOut = false;
			/** Guarded by Mutex: cumulative operation budget exceeded during receive; ErrorString already holds the reason. */
			bool bBudgetRejected = false;
			bool bSuccess = false;
			int32 ResponseCode = 0;
			/** Running count of bytes received for this single request (single-image budget guard). */
			int64 ReceivedBytes = 0;
			TArray<uint8> Downloaded;
			FString ErrorString;
		};

		/**
		 * Finalize the HTTP outcome while holding the state mutex.
		 * The streaming receiver owns the body. Preserve any terminal reason already recorded
		 * by the receiver or wait loop, including when a completion arrives late.
		 */
		void ResolveDownloadOutcome(
			const TSharedRef<FDownloadState, ESPMode::ThreadSafe>& State,
			const FHttpRequestPtr& Request,
			const FHttpResponsePtr& Response,
			const bool bWasSuccessful)
		{
			if (State->bPayloadLimitExceeded)
			{
				State->ErrorString = TEXT("Image payload exceeded the single-image byte limit during download");
				return;
			}
			if (State->bBudgetRejected)
			{
				// ErrorString already holds the accurate cumulative budget reason.
				return;
			}
			if (State->bCancelledByUser)
			{
				State->ErrorString = TEXT("cancelled by user");
				return;
			}
			if (State->bTimedOut)
			{
				State->ErrorString = TEXT("Network request timed out");
				return;
			}
			if (!bWasSuccessful || !Response.IsValid())
			{
				State->ErrorString = Request.IsValid() && Request->GetFailureReason() == EHttpFailureReason::TimedOut
					? TEXT("Network request timed out") : TEXT("Network request failed");
				return;
			}

			State->ResponseCode = Response->GetResponseCode();
			if (State->ResponseCode < 200 || State->ResponseCode >= 300)
			{
				State->ErrorString = FString::Printf(TEXT("Server returned an error (HTTP %d)"), State->ResponseCode);
				return;
			}

			if (State->Downloaded.Num() == 0)
			{
				State->ErrorString = TEXT("Server returned an empty response");
				return;
			}
			State->bSuccess = true;
		}

		/** A started request together with the state its callbacks write into. */
		struct FPendingRequest
		{
			FString Url;
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request;
			TSharedRef<FDownloadState, ESPMode::ThreadSafe> State;
			/** Per-request deadline: this request's start time plus TimeoutSeconds. */
			double DeadlineSeconds = 0.0;
		};

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreateGetRequest(
			const FString& Url,
			const float TimeoutSeconds)
		{
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
			Request->SetURL(Url);
			Request->SetVerb(TEXT("GET"));
			Request->SetHeader(TEXT("User-Agent"), TEXT("Design2UMG/RemoteImageImporter"));
			Request->SetTimeout(TimeoutSeconds);
			return Request;
		}

		/** Aborts the transfer as soon as progress reports more bytes than one image may hold. */
		void BindPayloadLimitGuard(
			const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request,
			const TSharedRef<FDownloadState, ESPMode::ThreadSafe>& State,
			const uint64 MaxCompressedBytes)
		{
			Request->OnRequestProgress64().BindLambda(
				[State, MaxCompressedBytes](FHttpRequestPtr ProgressRequest, uint64, uint64 BytesReceived)
				{
					if (BytesReceived > MaxCompressedBytes)
					{
						State->bPayloadLimitExceeded = true;
						ProgressRequest->CancelRequest();
					}
				});
		}

		/** Resolves the response on completion, or keeps the reason an earlier stage recorded. */
		void BindCompletionHandler(
			const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request,
			const TSharedRef<FDownloadState, ESPMode::ThreadSafe>& State)
		{
			Request->OnProcessRequestComplete().BindLambda(
				[State](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
				{
					FScopeLock Lock(&State->Mutex);
					if (State->bCancelledByUser || State->bTimedOut)
					{
						// Late callback for a request the wait loop already resolved
						// (user cancel / per-request timeout): keep the recorded error.
						State->bDone = true;
						return;
					}
					ResolveDownloadOutcome(State, Request, Response, bWasSuccessful);
					State->bDone = true;
				});
		}

		/** True while this request may still receive; a terminated one stops without billing. */
		bool IsStillReceiving(const TSharedRef<FDownloadState, ESPMode::ThreadSafe>& State)
		{
			FScopeLock Lock(&State->Mutex);
			return !State->bCancelledByUser
				&& !State->bTimedOut
				&& !State->bPayloadLimitExceeded
				&& !State->bBudgetRejected;
		}

		/**
		 * Single-image guard: refuses a chunk that would push this one request past its
		 * own ceiling, and records the reason before the transfer is aborted.
		 */
		bool AcceptChunkForSingleImageLimit(
			const TSharedRef<FDownloadState, ESPMode::ThreadSafe>& State,
			const int64 ChunkLen,
			const uint64 MaxCompressedBytes)
		{
			FScopeLock Lock(&State->Mutex);
			if (State->ReceivedBytes + ChunkLen <= static_cast<int64>(MaxCompressedBytes))
			{
				return true;
			}
			State->bPayloadLimitExceeded = true;
			State->ErrorString = TEXT("Image payload exceeded the single-image byte limit during download");
			return false;
		}

		/**
		 * Shared cumulative guard: charges the chunk to the operation budget under the
		 * batch mutex, so concurrent receives check and count in one critical section.
		 */
		bool ChargeChunkToOperationBudget(
			const TSharedRef<FDownloadState, ESPMode::ThreadSafe>& State,
			const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>& BulkBudget,
			const int64 ChunkLen)
		{
			FString BudgetError;
			{
				FScopeLock Lock(&BulkBudget->Mutex);
				if (BulkBudget->Budget->ConsumeCompressedBytes(static_cast<uint64>(ChunkLen), BudgetError))
				{
					return true;
				}
			}
			FScopeLock StateLock(&State->Mutex);
			State->bBudgetRejected = true;
			State->ErrorString = BudgetError;
			return false;
		}

		/** Takes ownership of an accepted chunk: the only place bytes are kept and counted. */
		void AppendAcceptedChunk(
			const TSharedRef<FDownloadState, ESPMode::ThreadSafe>& State,
			const uint8* Data,
			const int64 ChunkLen)
		{
			FScopeLock Lock(&State->Mutex);
			State->Downloaded.Append(Data, ChunkLen);
			State->ReceivedBytes += ChunkLen;
		}

		/**
		 * Receive into the owned payload while enforcing per-image and shared byte limits.
		 * Reject excess chunks before buffering or billing; accepted bytes are not refunded.
		 * Return false when the HTTP backend does not support streaming.
		 */
		bool InstallBudgetedStreamReceive(
			const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request,
			const TSharedRef<FDownloadState, ESPMode::ThreadSafe>& State,
			const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>& BulkBudget,
			const DesignImageResources::FImageResourceLimits& Limits)
		{
			return Request->SetResponseBodyReceiveStreamDelegateV2(
				FHttpRequestStreamDelegateV2::CreateLambda(
					// HTTP callbacks share request state and the budget mutex. DownloadUrls owns the
					// borrowed budget and stops receive callbacks before returning.
					[State, BulkBudget, MaxCompressedBytes = Limits.MaxCompressedBytes](void* Ptr, int64& InOutLength)
					{
						const int64 ChunkLen = InOutLength;
						if (!IsStillReceiving(State)
							|| !AcceptChunkForSingleImageLimit(State, ChunkLen, MaxCompressedBytes)
							|| !ChargeChunkToOperationBudget(State, BulkBudget, ChunkLen))
						{
							InOutLength = 0;
							return;
						}
						AppendAcceptedChunk(State, static_cast<const uint8*>(Ptr), ChunkLen);
						InOutLength = ChunkLen;
					}));
		}

		/** Requests that have neither finished nor been given a terminal reason by the wait loop. */
		int32 CountUnfinishedRequests(const TArray<FPendingRequest>& Pending)
		{
			int32 StillPending = 0;
			for (const FPendingRequest& Entry : Pending)
			{
				FScopeLock Lock(&Entry.State->Mutex);
				if (!Entry.State->bDone && !Entry.State->bTimedOut)
				{
					++StillPending;
				}
			}
			return StillPending;
		}

		/**
		 * Per-request deadline: cancels any request whose own window elapsed. Once marked,
		 * CountUnfinishedRequests stops waiting for it, so the wait loop always terminates
		 * even if a cancelled request's late callback never arrives.
		 */
		void CancelExpiredRequests(TArray<FPendingRequest>& Pending)
		{
			const double Now = FPlatformTime::Seconds();
			for (FPendingRequest& Entry : Pending)
			{
				if (Now < Entry.DeadlineSeconds)
				{
					continue;
				}
				bool bNeedsCancel = false;
				{
					FScopeLock Lock(&Entry.State->Mutex);
					if (!Entry.State->bDone && !Entry.State->bTimedOut)
					{
						Entry.State->bTimedOut = true;
						Entry.State->ErrorString = TEXT("Network request timed out");
						bNeedsCancel = true;
					}
				}
				if (bNeedsCancel)
				{
					Entry.Request->CancelRequest();
				}
			}
		}

		/**
		 * User abort: every unfinished request is marked cancelled by the user and its late
		 * HTTP callback keeps that reason. A download that completes in this window keeps
		 * its result instead of being overwritten with the cancel marker.
		 */
		void CancelRequestsByUser(TArray<FPendingRequest>& Pending)
		{
			for (FPendingRequest& Entry : Pending)
			{
				bool bNeedsCancel = false;
				{
					FScopeLock Lock(&Entry.State->Mutex);
					if (Entry.State->bDone)
					{
						continue;
					}
					Entry.State->bCancelledByUser = true;
					Entry.State->ErrorString = TEXT("cancelled by user");
					bNeedsCancel = true;
				}
				if (bNeedsCancel)
				{
					Entry.Request->CancelRequest();
				}
			}
		}

		/** One wait/cancellation path; only the progress UI is optional. */
		bool WaitForRequests(
			TArray<FPendingRequest>& Pending,
			const TFunction<bool()>& IsCancellationRequested)
		{
			FHttpManager& HttpManager = FHttpModule::Get().GetHttpManager();
			const bool bShowProgress = !FApp::IsUnattended() && FSlateApplication::IsInitialized();
			FScopedSlowTask SlowTask(static_cast<float>(Pending.Num()),
				NSLOCTEXT("Design2UMG", "DownloadingRemoteImages", "Downloading remote images..."), bShowProgress);
			if (bShowProgress && Pending.Num() > 0) SlowTask.MakeDialog(true);
			int32 Remaining = CountUnfinishedRequests(Pending);
			SlowTask.EnterProgressFrame(static_cast<float>(Pending.Num() - Remaining));
			while (Remaining > 0)
			{
				HttpManager.Tick(HttpWaitTickSeconds);
				if ((IsCancellationRequested && IsCancellationRequested()) || SlowTask.ShouldCancel())
				{
					CancelRequestsByUser(Pending);
					return true;
				}
				CancelExpiredRequests(Pending);
				const int32 StillRemaining = CountUnfinishedRequests(Pending);
				SlowTask.EnterProgressFrame(static_cast<float>(Remaining - StillRemaining));
				Remaining = StillRemaining;
				if (Remaining > 0) FPlatformProcess::Sleep(HttpWaitTickSeconds);
			}
			return false;
		}

		/** Copies each finished download into its result entry; unfinished ones keep their terminal reason. */
		void CollectResults(
			TArray<FPendingRequest>& Pending,
			const bool bUserCancelled,
			TMap<FString, FUrlDownloadResult>& OutResults)
		{
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
	}

	void FImagePayloadIO::DownloadUrls(
		const TArray<FString>& Urls,
		const float TimeoutSeconds,
		const bool bAllowUnsafeRemoteUrls,
		const DesignImageResources::FImageResourceLimits& Limits,
		const TSharedRef<FBulkDownloadBudget, ESPMode::ThreadSafe>& BulkBudget,
		TMap<FString, FUrlDownloadResult>& OutResults,
		const TFunction<bool()>& IsCancellationRequested)
	{
		const float EffectiveTimeoutSeconds = FMath::Max(1.0f, TimeoutSeconds);
		TArray<FPendingRequest> Pending;

		// Stage 1 - start every request. A URL that cannot be started at all is reported
		// as a failure right here; the rest are collected for the shared wait loop.
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
			const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateGetRequest(Url, EffectiveTimeoutSeconds);
			BindPayloadLimitGuard(Request, State, Limits.MaxCompressedBytes);
			BindCompletionHandler(Request, State);
			if (!InstallBudgetedStreamReceive(Request, State, BulkBudget, Limits))
			{
				// The active HTTP backend cannot pipe the body through the receive
				// stream; fail the URL explicitly rather than silently falling back
				// to the unlimited default response cache.
				Result.Error = TEXT("Remote image import cannot enforce the receive-byte budget: the active HTTP backend does not support response-body streaming");
				continue;
			}

			if (!Request->ProcessRequest())
			{
				Result.Error = TEXT("Unable to start network request");
				continue;
			}
			Pending.Add({ Url, Request, State, FPlatformTime::Seconds() + EffectiveTimeoutSeconds });
		}

		// Stage 2 - pump the HTTP manager until every request is finished, expired or cancelled.
		const bool bUserCancelled = WaitForRequests(Pending, IsCancellationRequested);

		// Stage 3 - hand each outcome to its result entry.
		CollectResults(Pending, bUserCancelled, OutResults);
	}

	bool FImagePayloadIO::DecodeToBGRA8(
		const TArray<uint8>& CompressedData,
		const DesignImageResources::FImageResourceLimits& Limits,
		int32& OutWidth,
		int32& OutHeight,
		TArray64<uint8>& OutRawData,
		FString& OutError,
		DesignImageResources::FImageOperationBudget* OperationBudget)
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
