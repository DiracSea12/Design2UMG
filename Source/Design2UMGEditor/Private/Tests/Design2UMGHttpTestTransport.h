// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "HttpModule.h"
#include "HttpRequestAdapter.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/EngineVersionComparison.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace Design2UMGEditorTests
{
	/** Raw HTTP response only: parsing and status handling remain in the clients. */
	class FHttpTestResponse final : public IHttpResponse
	{
	public:
		FHttpTestResponse(const FString& InUrl, const FString& Body, int32 InCode,
			const TMap<FString, FString>& InHeaders = {})
			: Url(InUrl), Code(InCode), Headers(InHeaders)
		{
			const FTCHARToUTF8 Utf8(*Body);
			Content.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}

#if UE_VERSION_OLDER_THAN(5, 6, 0)
		FString GetURL() const override { return Url; }
#else
		const FString& GetURL() const override { return Url; }
#endif
#if !UE_VERSION_OLDER_THAN(5, 4, 0)
		EHttpRequestStatus::Type GetStatus() const override { return EHttpRequestStatus::Succeeded; }
		const FString& GetEffectiveURL() const override { return Url; }
		EHttpFailureReason GetFailureReason() const override { return EHttpFailureReason::None; }
#endif
		FString GetURLParameter(const FString&) const override { return FString(); }
		FString GetHeader(const FString& Name) const override
		{
			for (const auto& Header : Headers)
			{
				if (Header.Key.Equals(Name, ESearchCase::IgnoreCase)) return Header.Value;
			}
			return FString();
		}
		TArray<FString> GetAllHeaders() const override
		{
			TArray<FString> Result;
			for (const auto& Header : Headers) Result.Add(Header.Key + TEXT(": ") + Header.Value);
			return Result;
		}
		FString GetContentType() const override { return GetHeader(TEXT("Content-Type")); }
#if UE_VERSION_OLDER_THAN(5, 3, 0)
		int32 GetContentLength() const override { return Content.Num(); }
#else
		uint64 GetContentLength() const override { return Content.Num(); }
#endif
		const TArray<uint8>& GetContent() const override { return Content; }
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
		TArray<uint8> TakeContent() override { return MoveTemp(Content); }
#endif
		int32 GetResponseCode() const override { return Code; }
		FString GetContentAsString() const override
		{
			const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Content.GetData()), Content.Num());
			return FString(Text.Length(), Text.Get());
		}
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
		FUtf8StringView GetContentAsUtf8StringView() const override
		{
			return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Content.GetData()), Content.Num());
		}
#endif

	private:
		FString Url;
		int32 Code;
		TMap<FString, FString> Headers;
		TArray<uint8> Content;
	};

	/** Declare before the client so cancellation is observed during client teardown. */
	struct FScopedHttpTestTransport final
	{
		TFunction<bool(int32, const FHttpRequestPtr&)> StartRequest;
		TMap<int32, FHttpRequestPtr> Pending;
		TArray<int32> CancelledIds;
		int32 LastStartedId = 0;
		bool bStartRequests = true;

		FScopedHttpTestTransport()
		{
			StartRequest = [this](int32 Id, const FHttpRequestPtr& Request)
			{
				LastStartedId = Id;
				if (!bStartRequests) return false;
				Pending.Add(Id, Request);
				return true;
			};
		}
		FScopedHttpTestTransport(const FScopedHttpTestTransport&) = delete;
		FScopedHttpTestTransport& operator=(const FScopedHttpTestTransport&) = delete;

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreateRequest()
		{
			return MakeShared<FRequest, ESPMode::ThreadSafe>(*this, ++NextId);
		}

		static void Reply(const FHttpRequestPtr& Request, bool bConnected, const FHttpResponsePtr& Response)
		{
			// A callback may start another request or destroy its original holder.
			const auto Completion = Request->OnProcessRequestComplete();
			Completion.ExecuteIfBound(Request, Response, bConnected);
		}

		static void Reply(const FHttpRequestPtr& Request, const FString& Body, int32 Code = 200,
			const TMap<FString, FString>& Headers = {})
		{
			Reply(Request, true, MakeShared<FHttpTestResponse, ESPMode::ThreadSafe>(Request->GetURL(), Body, Code, Headers));
		}

		void Complete(int32 Id, bool bConnected, const FHttpResponsePtr& Response)
		{
			const FHttpRequestPtr Request = Pending.FindAndRemoveChecked(Id);
			Reply(Request, bConnected, Response);
		}

		void Complete(int32 Id, const FString& Body, int32 Code = 200,
			const TMap<FString, FString>& Headers = {})
		{
			const FHttpRequestPtr Request = Pending.FindAndRemoveChecked(Id);
			Reply(Request, Body, Code, Headers);
		}
	private:
		int32 NextId = 0;
		class FRequest final : public FHttpRequestAdapterBase
		{
		public:
			FRequest(FScopedHttpTestTransport& InTransport, int32 InId)
				: FHttpRequestAdapterBase(FHttpModule::Get().CreateRequest()), Transport(InTransport), Id(InId) {}
			bool ProcessRequest() override { return Transport.StartRequest(Id, AsShared()); }
			void CancelRequest() override { Transport.CancelledIds.Add(Id); }
		private:
			FScopedHttpTestTransport& Transport;
			int32 Id;
		};

	};
}
#endif
