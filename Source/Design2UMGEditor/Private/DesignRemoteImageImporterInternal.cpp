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
#include "SslModule.h"
#include "Interfaces/ISslCertificateManager.h"
// OpenSSL's UI typedef collides with UObject's UI namespace. Rename only the
// third-party type token while reading its headers; no exported symbols change.
#pragma push_macro("UI")
#undef UI
#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include <openssl/ssl.h>
THIRD_PARTY_INCLUDES_END
#pragma pop_macro("UI")

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "curl/curl.h"
#include "Windows/HideWindowsPlatformTypes.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "curl/curl.h"
#endif

namespace DesignRemoteImageImporterInternal
{
	namespace
	{
		constexpr int64 Bgra8BytesPerPixel = 4;
		constexpr float HttpWaitTickSeconds = 0.01f;
		// Cap individual diagnostic lines to bound resource-resolution reports.

		bool ExtractUrlHost(const FString& Url, FString& OutHost)
		{
			CURLU* Parsed = curl_url();
			if (!Parsed) return false;
			const FTCHARToUTF8 Utf8Url(*Url);
			char* Host = nullptr;
			char* Zone = nullptr;
			const bool bValid = curl_url_set(Parsed, CURLUPART_URL, Utf8Url.Get(), CURLU_DISALLOW_USER) == CURLUE_OK
				&& curl_url_get(Parsed, CURLUPART_HOST, &Host, 0) == CURLUE_OK
				&& curl_url_get(Parsed, CURLUPART_ZONEID, &Zone, 0) == CURLUE_NO_ZONEID;
			if (bValid)
			{
				OutHost = UTF8_TO_TCHAR(Host);
				if (OutHost.StartsWith(TEXT("[")) && OutHost.EndsWith(TEXT("]")))
				{
					OutHost = OutHost.Mid(1, OutHost.Len() - 2);
				}
				while (OutHost.RemoveFromEnd(TEXT("."))) {}
				OutHost.ToLowerInline();
			}
			curl_free(Host);
			curl_free(Zone);
			curl_url_cleanup(Parsed);
			return bValid && !OutHost.IsEmpty();
		}

	}

	void FResolveReport::AddDiagnostic(
		const EDesignImageDiagnosticOutcome Outcome,
		const EDesignImageDiagnosticCategory Category,
		const FString& NodeId,
		const FString& NodeName,
		const FString& Resource,
		const FString& Reason,
		const FString& Message)
	{
		FDesignImageDiagnostic Diagnostic;
		Diagnostic.Outcome = Outcome;
		Diagnostic.Category = Category;
		Diagnostic.NodeId = NodeId;
		Diagnostic.NodeName = NodeName;
		Diagnostic.Resource = Resource;
		Diagnostic.Reason = Reason;
		Diagnostic.Message = Message;
		Result.AddDiagnostic(MoveTemp(Diagnostic));
	}

	FDesignImageImportResult FResolveReport::BuildResult(const FString& ImportPath)
	{
		Result.bWasExecuted = true;
		Result.ImportPath = ImportPath;
		Result.RefreshDisplaySummary();
		return MoveTemp(Result);
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

	bool FImagePayloadIO::IsPublicNetworkAddress(const TConstArrayView<uint8> Address)
	{
		if (Address.Num() == 4)
		{
			const uint8 A = Address[0], B = Address[1], C = Address[2];
			// IANA IPv4 special-purpose registry: PCP/TURN anycast are globally
			// reachable exceptions within the otherwise restricted 192.0.0.0/24.
			if (A == 192 && B == 0 && C == 0 && (Address[3] == 9 || Address[3] == 10)) return true;
			return !(A == 0 || A == 10 || A == 127 || A >= 224
				|| (A == 100 && B >= 64 && B <= 127)
				|| (A == 169 && B == 254) || (A == 172 && B >= 16 && B <= 31)
				|| (A == 192 && (B == 168 || (B == 0 && (C == 0 || C == 2))))
				|| (A == 192 && B == 88 && C == 99)
				|| (A == 198 && (B == 18 || B == 19 || (B == 51 && C == 100)))
				|| (A == 203 && B == 0 && C == 113));
		}
		// Only native global unicast. Reject mapped, NAT64, 6to4, Teredo and
		// special-purpose ranges rather than allowing a tunnel to hide a local peer.
		return Address.Num() == 16 && (Address[0] & 0xe0) == 0x20
			&& !(Address[0] == 0x20 && Address[1] == 0x02)
			&& !(Address[0] == 0x20 && Address[1] == 0x01 && Address[2] < 2)
			&& !(Address[0] == 0x20 && Address[1] == 0x01 && Address[2] == 0x0d && Address[3] == 0xb8)
			&& !(Address[0] == 0x3f && Address[1] == 0xff && (Address[2] & 0xf0) == 0);
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
		if (bAllowUnsafeRemoteUrls) return true;
		const FTCHARToUTF8 Utf8Host(*Host);
		uint8 Address[16] = {};
		const bool bV6 = Host.Contains(TEXT(":"));
		const bool bLiteral = inet_pton(bV6 ? AF_INET6 : AF_INET, Utf8Host.Get(), Address) == 1;
		if (bLiteral)
		{
			if (IsPublicNetworkAddress(MakeArrayView(Address, bV6 ? 16 : 4))) return true;
			OutError = TEXT("Remote image URL points to a non-public IP address");
			return false;
		}
		if (bV6 || Host == TEXT("localhost") || Host.EndsWith(TEXT(".localhost"))
			|| Host.EndsWith(TEXT(".local")) || !Host.Contains(TEXT(".")))
		{
			OutError = TEXT("Remote image URL points to a local-style or invalid host");
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

	EDesignImageDiagnosticCategory GetDownloadFailureCategory(const FDownloadState& State)
	{
		return State.bPayloadLimitExceeded || State.bBudgetRejected
			? EDesignImageDiagnosticCategory::Budget
			: EDesignImageDiagnosticCategory::Download;
	}

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
					Result.FailureCategory = GetDownloadFailureCategory(*Entry.State);
					continue;
					}

					// Late callbacks for cancelled requests are dropped by the
					// State mutex/bDone guard, so no delegate unbinding is needed.
				Entry.Request->CancelRequest();
				Result.Error = TEXT("Network request timed out");
				Result.FailureCategory = EDesignImageDiagnosticCategory::Download;
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
				Result.FailureCategory = GetDownloadFailureCategory(*Entry.State);
			}
			}
		}
	}


	namespace
	{
		struct FPublicImageTransfer
		{
			FString Url;
			CURL* Handle = nullptr;
			TSharedRef<FDownloadState, ESPMode::ThreadSafe> State = MakeShared<FDownloadState, ESPMode::ThreadSafe>();
			TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> Budget;
			uint64 MaxBytes;
			bool bBlockedDestination = false;

			FPublicImageTransfer(const FString& InUrl,
				const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>& InBudget,
				uint64 InMaxBytes) : Url(InUrl), Budget(InBudget), MaxBytes(InMaxBytes) {}
		};

		curl_socket_t OpenPublicImageSocket(void* UserData, curlsocktype Purpose, curl_sockaddr* Address)
		{
			FPublicImageTransfer& Transfer = *static_cast<FPublicImageTransfer*>(UserData);
			TConstArrayView<uint8> Bytes;
			if (Address->family == AF_INET)
			{
				const auto* V4 = reinterpret_cast<const sockaddr_in*>(&Address->addr);
				Bytes = MakeArrayView(reinterpret_cast<const uint8*>(&V4->sin_addr), 4);
			}
			else if (Address->family == AF_INET6)
			{
				const auto* V6 = reinterpret_cast<const sockaddr_in6*>(&Address->addr);
				Bytes = MakeArrayView(reinterpret_cast<const uint8*>(&V6->sin6_addr), 16);
			}
			// Runs for every new connection, including DNS retries and redirects.
			// The checked sockaddr is the very address libcurl passes to connect().
			if (Purpose != CURLSOCKTYPE_IPCXN || !FImagePayloadIO::IsPublicNetworkAddress(Bytes))
			{
				Transfer.bBlockedDestination = true;
				return CURL_SOCKET_BAD;
			}
			return socket(Address->family, Address->socktype, Address->protocol);
		}

		size_t ReceivePublicImage(char* Data, size_t Size, size_t Count, void* UserData)
		{
			FPublicImageTransfer& Transfer = *static_cast<FPublicImageTransfer*>(UserData);
			if (Size != 0 && Count > static_cast<size_t>(MAX_int32) / Size)
			{
				return 0;
			}
			const int64 Bytes = static_cast<int64>(Size * Count);
			if (!AcceptChunkForSingleImageLimit(Transfer.State, Bytes, Transfer.MaxBytes)
				|| !ChargeChunkToOperationBudget(Transfer.State, Transfer.Budget, Bytes))
			{
				return 0;
			}
			AppendAcceptedChunk(Transfer.State, reinterpret_cast<const uint8*>(Data), Bytes);
			return static_cast<size_t>(Bytes);
		}

		int VerifyImageCertificate(int Preverified, X509_STORE_CTX* Context)
		{
			if (Preverified == 1)
			{
				SSL* Connection = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(Context, SSL_get_ex_data_X509_STORE_CTX_idx()));
				const char* Host = Connection ? SSL_get_servername(Connection, TLSEXT_NAMETYPE_host_name) : nullptr;
				const FString Domain = Host ? UTF8_TO_TCHAR(Host) : TEXT("");
				return FSslModule::Get().GetCertificateManager().VerifySslCertificates(Context, Domain) ? 1 : 0;
			}
			return Preverified;
		}

		CURLcode ConfigureImageCertificates(CURL*, void* Context, void*)
		{
			SSL_CTX* SslContext = static_cast<SSL_CTX*>(Context);
			FSslModule::Get().GetCertificateManager().AddCertificatesToSslContext(SslContext);
			SSL_CTX_set_verify(SslContext, SSL_CTX_get_verify_mode(SslContext), &VerifyImageCertificate);
			return CURLE_OK;
		}

		void DownloadPublicImages(const TArray<FString>& Urls, const float TimeoutSeconds,
			const DesignImageResources::FImageResourceLimits& Limits,
			const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>& Budget,
			TMap<FString, FUrlDownloadResult>& OutResults,
			const TFunction<bool()>& IsCancellationRequested)
		{
			// A private multi handle has no shared DNS/connection cache. No proxy or
			// alternate transport may bypass the destination check. TLS uses UE roots.
			FSslModule::Get();
			const bool bCurlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
			CURLM* Multi = bCurlReady ? curl_multi_init() : nullptr;
			TArray<TUniquePtr<FPublicImageTransfer>> Transfers;
			for (const FString& Url : Urls)
			{
				FUrlDownloadResult& Result = OutResults.FindOrAdd(Url);
				Result = FUrlDownloadResult();
			if (!FImagePayloadIO::ValidateRemoteUrl(Url, false, Result.Error))
			{
				Result.FailureCategory = EDesignImageDiagnosticCategory::Validation;
				continue;
			}
				auto Transfer = MakeUnique<FPublicImageTransfer>(Url, Budget, Limits.MaxCompressedBytes);
				Transfer->Handle = Multi ? curl_easy_init() : nullptr;
				CURL* Handle = Transfer->Handle;
				const FTCHARToUTF8 Utf8Url(*Url);
				const long TimeoutMs = static_cast<long>(FMath::Clamp(static_cast<double>(TimeoutSeconds) * 1000.0, 1000.0, 2147483647.0));
				const bool bConfigured = Handle
					&& curl_easy_setopt(Handle, CURLOPT_URL, Utf8Url.Get()) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_PROTOCOLS_STR, "https") == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_REDIR_PROTOCOLS_STR, "https") == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_MAXREDIRS, 10L) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_PROXY, "") == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_FRESH_CONNECT, 1L) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_FORBID_REUSE, 1L) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_CAINFO, nullptr) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_SSL_CTX_FUNCTION, &ConfigureImageCertificates) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_OPENSOCKETFUNCTION, &OpenPublicImageSocket) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_OPENSOCKETDATA, Transfer.Get()) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_WRITEFUNCTION, &ReceivePublicImage) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_WRITEDATA, Transfer.Get()) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_PRIVATE, Transfer.Get()) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_USERAGENT, "Design2UMG/RemoteImageImporter") == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_TIMEOUT_MS, TimeoutMs) == CURLE_OK
					&& curl_easy_setopt(Handle, CURLOPT_NOSIGNAL, 1L) == CURLE_OK;
				if (!bConfigured || curl_multi_add_handle(Multi, Handle) != CURLM_OK)
				{
					Result.Error = TEXT("Failed to initialize destination-checked HTTPS request");
					if (Handle) curl_easy_cleanup(Handle);
					continue;
				}
				Transfers.Add(MoveTemp(Transfer));
			}
			const bool bShowProgress = !FApp::IsUnattended() && FSlateApplication::IsInitialized();
			FScopedSlowTask Progress(static_cast<float>(Transfers.Num()),
				NSLOCTEXT("Design2UMG", "DownloadingPublicImages", "Downloading remote images..."), bShowProgress);
			if (bShowProgress && !Transfers.IsEmpty()) Progress.MakeDialog(true);
			bool bCancelled = false;
			CURLMcode MultiResult = CURLM_OK;
			int Running = 0;
			do
			{
				if (!Multi || Transfers.IsEmpty()) break;
				if ((IsCancellationRequested && IsCancellationRequested()) || Progress.ShouldCancel())
				{
					bCancelled = true;
					break;
				}
				MultiResult = curl_multi_perform(Multi, &Running);
				int Queued = 0;
				while (CURLMsg* Message = curl_multi_info_read(Multi, &Queued))
				{
					if (Message->msg != CURLMSG_DONE) continue;
					FPublicImageTransfer* Transfer = nullptr;
					curl_easy_getinfo(Message->easy_handle, CURLINFO_PRIVATE, &Transfer);
					long Status = 0;
					curl_easy_getinfo(Message->easy_handle, CURLINFO_RESPONSE_CODE, &Status);
					FUrlDownloadResult& Result = OutResults.FindChecked(Transfer->Url);
					Result.bSuccess = Transfer->State->ErrorString.IsEmpty()
						&& Message->data.result == CURLE_OK && Status >= 200 && Status < 300
						&& !Transfer->State->Downloaded.IsEmpty();
					if (Result.bSuccess)
					{
						Result.Data = MoveTemp(Transfer->State->Downloaded);
						Result.Error.Reset();
					}
				else
				{
					Result.Data.Reset();
					Result.FailureCategory = GetDownloadFailureCategory(*Transfer->State);
						if (!Transfer->State->ErrorString.IsEmpty())
						{
							Result.Error = Transfer->State->ErrorString;
						}
						else if (Message->data.result == CURLE_COULDNT_CONNECT && Transfer->bBlockedDestination)
						{
							Result.Error = TEXT("HTTPS connection failed; non-public destination candidates were rejected");
						}
						else if (Message->data.result != CURLE_OK)
						{
							Result.Error = FString::Printf(TEXT("HTTPS image request failed: %s (HTTP %ld)"),
								UTF8_TO_TCHAR(curl_easy_strerror(Message->data.result)), Status);
						}
						else if (Status < 200 || Status >= 300)
						{
							Result.Error = FString::Printf(TEXT("Server returned an error (HTTP %ld)"), Status);
						}
						else
						{
							Result.Error = TEXT("Server returned an empty response");
						}
					}
					Transfer->State->bDone = true;
					Progress.EnterProgressFrame();
				}
				if (Running > 0 && MultiResult == CURLM_OK) FPlatformProcess::Sleep(HttpWaitTickSeconds);
			} while (Running > 0 && MultiResult == CURLM_OK);
			for (const auto& Transfer : Transfers)
			{
				// Removal synchronously stops all callbacks before the borrowed budget dies.
				curl_multi_remove_handle(Multi, Transfer->Handle);
				curl_easy_cleanup(Transfer->Handle);
				if (!Transfer->State->bDone)
				{
					FUrlDownloadResult& Result = OutResults.FindChecked(Transfer->Url);
				Result.bSuccess = false;
				Result.Data.Reset();
				Result.FailureCategory = GetDownloadFailureCategory(*Transfer->State);
				Result.Error = !Transfer->State->ErrorString.IsEmpty() ? Transfer->State->ErrorString
						: bCancelled ? TEXT("cancelled by user") : TEXT("HTTPS transfer manager failed");
				}
			}
			if (Multi) curl_multi_cleanup(Multi);
			if (bCurlReady) curl_global_cleanup();
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
		if (!bAllowUnsafeRemoteUrls)
		{
			DownloadPublicImages(Urls, TimeoutSeconds, Limits, BulkBudget, OutResults, IsCancellationRequested);
			return;
		}
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
				Result.FailureCategory = EDesignImageDiagnosticCategory::Validation;
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
				Result.FailureCategory = EDesignImageDiagnosticCategory::Configuration;
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
		DesignImageResources::FImageOperationBudget* OperationBudget,
		EDesignImageDiagnosticCategory* OutFailureCategory)
	{
		OutWidth = 0;
		OutHeight = 0;
		OutRawData.Reset();
		OutError.Reset();
		if (OutFailureCategory)
		{
			*OutFailureCategory = EDesignImageDiagnosticCategory::Decode;
		}
		if (CompressedData.Num() == 0)
		{
			OutError = TEXT("Image data is empty");
			return false;
		}
		if (!ValidateCompressedByteCount(static_cast<uint64>(CompressedData.Num()), Limits, OutError))
		{
			if (OutFailureCategory)
			{
				*OutFailureCategory = EDesignImageDiagnosticCategory::Budget;
			}
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
			if (OutFailureCategory)
			{
				*OutFailureCategory = EDesignImageDiagnosticCategory::Budget;
			}
			return false;
		}
		if (OperationBudget
			&& !OperationBudget->ConsumeDecodedPixels(static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight), OutError))
		{
			if (OutFailureCategory)
			{
				*OutFailureCategory = EDesignImageDiagnosticCategory::Budget;
			}
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
