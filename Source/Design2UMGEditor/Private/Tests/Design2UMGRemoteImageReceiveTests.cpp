// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/Build.h"

#if WITH_AUTOMATION_WORKER

#include "Misc/AutomationTest.h"
#include "DesignRemoteImageImporter.h"
#include "DesignRemoteImageImporterInternal.h"
#include "DesignImportPlanning.h"
#include "Design2UMGSettings.h"
#include "DesignImageResourceLimits.h"
#include "Design2UMGLog.h"
#include "UMGSchema.h"
#include "UMGWidgetTypes.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

using namespace DesignRemoteImageImporterInternal;

namespace
{
	/**
	 * Minimal in-process HTTP/1.1 server used only by the receive-budget tests.
	 *
	 * It exists so the assertions below exercise the *real* UE HTTP stack: requests are
	 * created by FHttpModule (libcurl on Windows), libcurl connects to this loopback
	 * listener, and the production IHttpRequest::SetResponseBodyReceiveStreamDelegateV2
	 * callback inside FImagePayloadIO::DownloadUrls is what actually receives, bills and
	 * rejects the bytes. Nothing in this file calls the budget object directly to
	 * simulate a receive.
	 */
	class FLocalTestHttpServer final : public FRunnable
	{
	public:
		struct FRoute
		{
			int32 BodyBytes = 0;
			int32 ChunkBytes = 512;
			int32 StatusCode = 200;
			/** Send Transfer-Encoding: chunked with no Content-Length at all. */
			bool bChunkedTransfer = false;
			/** Wait this long before writing anything, to force a client-side timeout. */
			double DelaySeconds = 0.0;
			/** Wait this long between body chunks, so each chunk is its own client read. */
			double InterChunkDelaySeconds = 0.0;
			/** When set, these exact bytes are the body (the synthetic fill is not used). */
			TArray<uint8> Body;
		};

		/**
		 * Holds every waiting connection until the whole batch is connected, then releases
		 * them together. Without it the listener would answer one connection after another
		 * and a "concurrent" test would really be a serial one.
		 */
		struct FRendezvous
		{
		TAtomic<int32> Expected{ 0 };
			TAtomic<int32> Arrived{ 0 };

			/** Opens the gate for everybody, including connections still waiting. */
		void Release() { Expected.Store(0); }

			void Wait(const double TimeoutSeconds = 5.0)
			{
				Arrived.IncrementExchange();
				const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
			while (Arrived.Load() < Expected.Load() && FPlatformTime::Seconds() < Deadline)
				{
					FPlatformProcess::Sleep(0.0005f);
				}
			}
		};

		FLocalTestHttpServer() = default;
		FLocalTestHttpServer(const FLocalTestHttpServer&) = delete;
		FLocalTestHttpServer& operator=(const FLocalTestHttpServer&) = delete;

		~FLocalTestHttpServer() override
		{
			Shutdown();
		}

		bool Start(const TMap<int32, FRoute>& InRoutes, const int32 PreferredPort = 45100)
		{
			Routes = InRoutes;
			Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
			if (!Subsystem)
			{
				return false;
			}

			Listener = Subsystem->CreateSocket(NAME_Stream, TEXT("Design2UMG ReceiveBudget listener"), false);
			if (!Listener)
			{
				return false;
			}

			Listener->SetReuseAddr(true);
			for (int32 Offset = 0; Offset < 64; ++Offset)
			{
				const FIPv4Endpoint Endpoint(FIPv4Address(127, 0, 0, 1), PreferredPort + Offset);
				if (Listener->Bind(*Endpoint.ToInternetAddr()))
				{
					Port = PreferredPort + Offset;
					break;
				}
			}
			if (Port == 0 || !Listener->Listen(32))
			{
				return false;
			}

			Thread = FRunnableThread::Create(this, TEXT("Design2UMG ReceiveBudget test server"));
			return Thread != nullptr;
		}

		/**
		 * FRunnable::Stop override. The thread machinery calls this from
		 * WaitForCompletion, so it may only signal the worker - it must never wait on
		 * the worker thread or the teardown would recurse into itself.
		 */
		virtual void Stop() override
		{
			bStopping = true;
		}

		/** Explicit teardown for the owning test: signal, join every thread, then free the socket. */
		void Shutdown()
		{
			bStopping = true;
			// Nobody is going to connect any more: let waiting workers through so the
			// joins below cannot block on a rendezvous that will never be satisfied.
			Rendezvous.Release();
			if (Thread)
			{
				Thread->WaitForCompletion();
				delete Thread;
				Thread = nullptr;
			}
			TArray<FRunnableThread*> ThreadsToJoin;
			TArray<FConnectionWorker*> RunnablesToDelete;
			{
				FScopeLock Lock(&WorkersMutex);
				ThreadsToJoin = MoveTemp(Workers);
				RunnablesToDelete = MoveTemp(WorkerRunnables);
				Workers.Reset();
				WorkerRunnables.Reset();
			}
			for (int32 Index = 0; Index < ThreadsToJoin.Num(); ++Index)
			{
				ThreadsToJoin[Index]->WaitForCompletion();
				delete ThreadsToJoin[Index];
				delete RunnablesToDelete[Index];
			}
			if (Listener)
			{
				Listener->Close();
				Subsystem->DestroySocket(Listener);
				Listener = nullptr;
			}
		}

		int32 GetPort() const { return Port; }

		/** Number of connections the server answers in a single batch; 0 disables the gate. */
		void SetRendezvousSize(const int32 ExpectedConnections) { Rendezvous.Expected.Store(ExpectedConnections); }

		/** How many requests the server has answered so far. */
		int32 GetRequestCount() const { return RequestCounter.Load(); }


		FString MakeUrl(const int32 RouteId) const
		{
			return FString::Printf(TEXT("http://127.0.0.1:%d/%d"), Port, RouteId);
		}

		// FRunnable
		virtual uint32 Run() override
		{
			while (!bStopping)
			{
				bool bPending = false;
				Listener->HasPendingConnection(bPending);
				if (!bPending)
				{
					FPlatformProcess::Sleep(0.001f);
					continue;
				}

				FSocket* Client = Listener->Accept(TEXT("Design2UMG ReceiveBudget client"));
				if (!Client)
				{
					continue;
				}

				// Each connection is answered on its own thread, so several bodies can be
				// on the wire at the same time instead of one after another.
				StartWorker(Client);
			}
			return 0;
		}

	private:
		/** Answers one accepted connection on its own thread, then closes it. */
		class FConnectionWorker final : public FRunnable
		{
		public:
			FConnectionWorker(
				FSocket* InClient,
				const TMap<int32, FRoute>& InRoutes,
				ISocketSubsystem* InSubsystem,
				FRendezvous* InRendezvous,
				TAtomic<int32>* InRequestCounter)
				: Client(InClient)
				, Routes(InRoutes)
				, Subsystem(InSubsystem)
				, Rendezvous(InRendezvous)
				, RequestCounter(InRequestCounter)
			{
			}

			virtual uint32 Run() override
			{
				if (Rendezvous)
				{
					Rendezvous->Wait();
				}
				ServeConnection(Client, Routes, *RequestCounter);
				Client->Close();
				Subsystem->DestroySocket(Client);
				return 0;
			}

		private:
			FSocket* Client = nullptr;
			const TMap<int32, FRoute>& Routes;
			ISocketSubsystem* Subsystem = nullptr;
			FRendezvous* Rendezvous = nullptr;
			TAtomic<int32>* RequestCounter = nullptr;
		};

		void StartWorker(FSocket* Client)
		{
			// Heap-allocated: the runnable has to outlive this call, because its thread
			// only finishes when Shutdown() joins it.
			FConnectionWorker* Worker = new FConnectionWorker(Client, Routes, Subsystem, &Rendezvous,
				&RequestCounter);
			FRunnableThread* WorkerThread = FRunnableThread::Create(Worker, TEXT("Design2UMG ReceiveBudget client worker"));
			if (!WorkerThread)
			{
				// No thread available: answer inline rather than dropping the connection.
				Worker->Run();
				delete Worker;
				return;
			}
			FScopeLock Lock(&WorkersMutex);
			Workers.Add(WorkerThread);
			WorkerRunnables.Add(Worker);
		}

		/** Counts one answered request and records how many bodies are in flight at once. */
		static void ServeConnection(
			FSocket* Client,
			const TMap<int32, FRoute>& Routes,
			TAtomic<int32>& RequestCounter)
		{
			RequestCounter.IncrementExchange();
			const FString Request = ReadRequest(Client);
			const int32 RouteId = ParseRouteId(Request);
			const FRoute* Route = Routes.Find(RouteId);

			if (Route)
			{
				WriteRouteResponse(Client, *Route);
			}
			else
			{
				SendText(Client, TEXT("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
			}
		}

		static void WriteRouteResponse(FSocket* Client, const FRoute& Route)
		{
			if (Route.DelaySeconds > 0.0)
			{
				FPlatformProcess::Sleep(Route.DelaySeconds);
			}

			const int32 BodyLength = Route.Body.Num() > 0 ? Route.Body.Num() : Route.BodyBytes;
			FString Header = FString::Printf(
				TEXT("HTTP/1.1 %d OK\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n"),
				Route.StatusCode);
			if (Route.bChunkedTransfer)
			{
				Header += TEXT("Transfer-Encoding: chunked\r\n");
			}
			else
			{
				Header += FString::Printf(TEXT("Content-Length: %d\r\n"), BodyLength);
			}
			Header += TEXT("\r\n");
			if (!SendText(Client, Header))
			{
				return;
			}

			if (Route.Body.Num() > 0)
			{
				SendBytes(Client, Route.Body);
				return;
			}

			int32 Remaining = Route.BodyBytes;
			int32 NextByte = 0;
			while (Remaining > 0)
			{
				const int32 Chunk = FMath::Min(Route.ChunkBytes, Remaining);
				TArray<uint8> Payload;
				Payload.SetNumUninitialized(Chunk);
				for (int32 Index = 0; Index < Chunk; ++Index)
				{
					Payload[Index] = static_cast<uint8>((NextByte + Index) % 251);
				}

				if (Route.bChunkedTransfer)
				{
					if (!SendText(Client, FString::Printf(TEXT("%x\r\n"), Chunk))) return;
					if (!SendBytes(Client, Payload)) return;
					if (!SendText(Client, TEXT("\r\n"))) return;
				}
				else
				{
					if (!SendBytes(Client, Payload)) return;
				}

				Remaining -= Chunk;
				NextByte += Chunk;
				if (Remaining > 0)
				{
					// Yield so the client receives this chunk as its own read rather than
					// coalescing the whole body into one callback.
					FPlatformProcess::Sleep(FMath::Max(0.002f, static_cast<float>(Route.InterChunkDelaySeconds)));
				}
			}

			if (Route.bChunkedTransfer)
			{
				SendText(Client, TEXT("0\r\n\r\n"));
			}
		}

		static FString ReadRequest(FSocket* Client)
		{
			FString Request;
			uint8 Buffer[2048];
			const double Deadline = FPlatformTime::Seconds() + 5.0;
			while (FPlatformTime::Seconds() < Deadline && !Request.Contains(TEXT("\r\n\r\n")))
			{
				if (!Client->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(100)))
				{
					continue;
				}
				int32 BytesRead = 0;
				if (!Client->Recv(Buffer, static_cast<int32>(sizeof(Buffer)), BytesRead) || BytesRead <= 0)
				{
					break;
				}
				for (int32 Index = 0; Index < BytesRead; ++Index)
				{
					Request.AppendChar(static_cast<TCHAR>(Buffer[Index]));
				}
			}
			return Request;
		}

		static int32 ParseRouteId(const FString& Request)
		{
			const int32 SlashPos = Request.Find(TEXT("GET /"), ESearchCase::CaseSensitive);
			if (SlashPos == INDEX_NONE)
			{
				return INDEX_NONE;
			}
			FString Digits;
			for (int32 Pos = SlashPos + 5; Pos < Request.Len() && FChar::IsDigit(Request[Pos]); ++Pos)
			{
				Digits.AppendChar(Request[Pos]);
			}
			return Digits.IsEmpty() ? INDEX_NONE : FCString::Atoi(*Digits);
		}

		static bool SendText(FSocket* Client, const FString& Text)
		{
			const FTCHARToUTF8 Utf8(*Text);
			return SendBytes(Client, reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}

		static bool SendBytes(FSocket* Client, const TArray<uint8>& Bytes)
		{
			return SendBytes(Client, Bytes.GetData(), Bytes.Num());
		}

		static bool SendBytes(FSocket* Client, const uint8* Data, const int32 Count)
		{
			int32 Sent = 0;
			while (Sent < Count)
			{
				int32 SentNow = 0;
				if (!Client->Send(Data + Sent, Count - Sent, SentNow) || SentNow <= 0)
				{
					return false;
				}
				Sent += SentNow;
			}
			return true;
		}

		TMap<int32, FRoute> Routes;
		ISocketSubsystem* Subsystem = nullptr;
		FSocket* Listener = nullptr;
		FRunnableThread* Thread = nullptr;
		int32 Port = 0;
		TAtomic<bool> bStopping{ false };
		FRendezvous Rendezvous;
		TAtomic<int32> RequestCounter{ 0 };
		FCriticalSection WorkersMutex;
		TArray<FRunnableThread*> Workers;
		TArray<FConnectionWorker*> WorkerRunnables;
	};

	/** Keeps libcurl from routing the loopback test traffic through an inherited proxy. */
	struct FScopedNoProxyEnv
	{
		FScopedNoProxyEnv()
		{
			SavedNoProxy = FPlatformMisc::GetEnvironmentVariable(TEXT("no_proxy"));
			SavedNoProxyUpper = FPlatformMisc::GetEnvironmentVariable(TEXT("NO_PROXY"));
			FPlatformMisc::SetEnvironmentVar(TEXT("no_proxy"), TEXT("127.0.0.1,localhost"));
			FPlatformMisc::SetEnvironmentVar(TEXT("NO_PROXY"), TEXT("127.0.0.1,localhost"));
		}

		~FScopedNoProxyEnv()
		{
			FPlatformMisc::SetEnvironmentVar(TEXT("no_proxy"), *SavedNoProxy);
			FPlatformMisc::SetEnvironmentVar(TEXT("NO_PROXY"), *SavedNoProxyUpper);
		}

		FString SavedNoProxy;
		FString SavedNoProxyUpper;
	};

	/** Enables remote-image import for the tests that drive the two production entries. */
	struct FScopedRemoteImageSettings
	{
		FScopedRemoteImageSettings()
		{
			Settings = UDesign2UMGSettings::Get();
			if (!Settings)
			{
				return;
			}
			bSavedEnabled = Settings->bEnableRemoteImageImport;
			bSavedUnsafe = Settings->bAllowUnsafeRemoteImageUrls;
			Settings->bEnableRemoteImageImport = true;
			Settings->bAllowUnsafeRemoteImageUrls = true;
		}

		~FScopedRemoteImageSettings()
		{
			if (Settings)
			{
				Settings->bEnableRemoteImageImport = bSavedEnabled;
				Settings->bAllowUnsafeRemoteImageUrls = bSavedUnsafe;
			}
		}

		UDesign2UMGSettings* Settings = nullptr;
		bool bSavedEnabled = false;
		bool bSavedUnsafe = false;
	};

	DesignImageResources::FImageResourceLimits MakeLimits(const uint64 MaxSingle, const uint64 MaxTotal)
	{
		DesignImageResources::FImageResourceLimits Limits;
		Limits.MaxCompressedBytes = MaxSingle;
		Limits.MaxTotalCompressedBytes = MaxTotal;
		return Limits;
	}

	int64 AcceptedBytes(const TMap<FString, FUrlDownloadResult>& Results)
	{
		int64 Total = 0;
		for (const auto& Pair : Results)
		{
			if (Pair.Value.bSuccess)
			{
				Total += Pair.Value.Data.Num();
			}
		}
		return Total;
	}

	int32 SucceededCount(const TMap<FString, FUrlDownloadResult>& Results)
	{
		int32 Count = 0;
		for (const auto& Pair : Results)
		{
			if (Pair.Value.bSuccess)
			{
				++Count;
			}
		}
		return Count;
	}

	/**
	 * Pins the shared budget to exactly ExpectedBilledBytes bytes charged so far: filling
	 * the remainder must succeed (nothing more was billed) and one byte beyond it must
	 * fail (nothing less was billed).
	 */
	void AssertBilledBytes(
		FAutomationTestBase& Test,
		DesignImageResources::FImageOperationBudget& Budget,
		const uint64 Capacity,
		const uint64 ExpectedBilledBytes,
		const FString& What)
	{
		FString BudgetError;
		Test.TestTrue(FString::Printf(TEXT("%s: never more than %llu byte(s) billed"), *What,
			static_cast<unsigned long long>(ExpectedBilledBytes)),
			Budget.ConsumeCompressedBytes(Capacity - ExpectedBilledBytes, BudgetError));
		Test.TestFalse(FString::Printf(TEXT("%s: the full %llu byte(s) really were billed"), *What,
			static_cast<unsigned long long>(ExpectedBilledBytes)),
			Budget.ConsumeCompressedBytes(1, BudgetError));
	}

	/** A real PNG so the production decode path (used for local images) can be exercised. */
	TArray<uint8> MakePngBytes(const int32 Width, const int32 Height)
	{
		IImageWrapperModule& ImageWrapperModule =
			FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid())
		{
			return {};
		}
		TArray64<uint8> Raw;
		Raw.SetNumZeroed(static_cast<int64>(Width) * static_cast<int64>(Height) * 4);
		if (!Wrapper->SetRaw(Raw.GetData(), Raw.Num(), Width, Height, ERGBFormat::BGRA, 8))
		{
			return {};
		}
		const TArray64<uint8> Compressed = Wrapper->GetCompressed();
		TArray<uint8> Out;
		Out.Append(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
		return Out;
	}

	FUMGWidgetNode MakeReceiveBudgetImageNode(const FString& Id, const FString& TexturePath)
	{
		FUMGWidgetNode Node;
		Node.Type = UMGWidgetTypes::Image;
		Node.Id = Id;
		Node.Name = Id;
		Node.Bounds = FDesignPixelBounds{ 0.0f, 0.0f, 64.0f, 64.0f };
		Node.WidgetStyle.WidthOverride = 64.0f;
		Node.WidgetStyle.HeightOverride = 64.0f;
		Node.Brush.Image.TexturePath = TexturePath;
		return Node;
	}

	FUMGWidgetNode MakeReceiveBudgetCanvas(const FString& Name)
	{
		FUMGWidgetNode Root;
		Root.Type = UMGWidgetTypes::CanvasPanel;
		Root.Id = Name;
		Root.Name = Name;
		Root.Bounds = FDesignPixelBounds{ 0.0f, 0.0f, 320.0f, 240.0f };
		return Root;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveServerIsReachableTest,
	"Design2UMG.RemoteImages.ReceiveBudget.LoopbackServerIsReachable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveServerIsReachableTest::RunTest(const FString& Parameters)
{
	// Precondition for every test in this file: if the loopback server cannot be
	// reached, the remaining assertions would be vacuous rather than green.
	FScopedNoProxyEnv NoProxy;

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute Ok;
	Ok.BodyBytes = 700;
	Ok.ChunkBytes = 350;
	Routes.Add(0, Ok);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	const DesignImageResources::FImageResourceLimits Limits = MakeLimits(4096, 65536);
	DesignImageResources::FImageOperationBudget Budget(Limits);
	const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
		MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);

	TArray<FString> Urls;
	Urls.Add(Server.MakeUrl(0));
	TMap<FString, FUrlDownloadResult> Results;
	FImagePayloadIO::DownloadUrls(Urls, 10.0f, /*bAllowUnsafeRemoteUrls=*/true, Limits, BulkBudget, Results);

	TestEqual(TEXT("One result is produced"), Results.Num(), 1);
	const FUrlDownloadResult* Result = Results.Find(Server.MakeUrl(0));
	TestNotNull(TEXT("Result exists for the served URL"), Result);
	TestTrue(TEXT("A normal in-budget download succeeds"), Result && Result->bSuccess);
	TestEqual(TEXT("The whole body arrives through the receive stream"),
		Result ? Result->Data.Num() : 0, 700);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveSingleImageLimitEnforcedWhileReceivingTest,
	"Design2UMG.RemoteImages.ReceiveBudget.SingleImageLimitEnforcedWhileReceiving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveSingleImageLimitEnforcedWhileReceivingTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute AtLimit;   // exactly at the single-image limit
	AtLimit.BodyBytes = 1000;
	AtLimit.ChunkBytes = 500;
	Routes.Add(0, AtLimit);
	FLocalTestHttpServer::FRoute OverLimit; // one chunk beyond the single-image limit
	OverLimit.BodyBytes = 1500;
	OverLimit.ChunkBytes = 500;
	Routes.Add(1, OverLimit);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	const DesignImageResources::FImageResourceLimits Limits = MakeLimits(1000, 100000);
	DesignImageResources::FImageOperationBudget Budget(Limits);
	const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
		MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);

	TArray<FString> Urls;
	Urls.Add(Server.MakeUrl(0));
	Urls.Add(Server.MakeUrl(1));
	TMap<FString, FUrlDownloadResult> Results;
	FImagePayloadIO::DownloadUrls(Urls, 10.0f, true, Limits, BulkBudget, Results);

	const FUrlDownloadResult* AtLimitResult = Results.Find(Server.MakeUrl(0));
	TestNotNull(TEXT("At-limit result exists"), AtLimitResult);
	TestTrue(TEXT("A payload of exactly the single-image limit is accepted"),
		AtLimitResult && AtLimitResult->bSuccess);
	TestEqual(TEXT("Accepted payload keeps all 1000 bytes"),
		AtLimitResult ? AtLimitResult->Data.Num() : 0, 1000);

	const FUrlDownloadResult* OverLimitResult = Results.Find(Server.MakeUrl(1));
	TestNotNull(TEXT("Over-limit result exists"), OverLimitResult);
	TestFalse(TEXT("A payload beyond the single-image limit is rejected"),
		OverLimitResult && OverLimitResult->bSuccess);
	TestTrue(TEXT("Rejection names the single-image byte limit"),
		OverLimitResult && OverLimitResult->Error.Contains(TEXT("single-image byte limit")));
	TestEqual(TEXT("Rejected payload keeps no bytes"),
		OverLimitResult ? OverLimitResult->Data.Num() : 0, 0);

	// The over-limit chunk must be refused before it is billed: at most the two accepted
	// 1000-byte payloads can have been charged to the shared budget.
	FString BudgetError;
	TestTrue(TEXT("Rejected bytes are never billed"),
		Budget.ConsumeCompressedBytes(100000 - 2000, BudgetError));
	// And the accepted bytes really were charged while receiving: without the
	// receive-stage billing the budget would still be completely free here.
	TestFalse(TEXT("Accepted bytes are charged to the shared budget while receiving"),
		Budget.ConsumeCompressedBytes(2000, BudgetError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveCumulativeTotalAtLimitIsBilledTest,
	"Design2UMG.RemoteImages.ReceiveBudget.CumulativeTotalAtLimitIsBilled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveCumulativeTotalAtLimitIsBilledTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute Full;
	Full.BodyBytes = 1000;
	Full.ChunkBytes = 1000; // one chunk per image, so the billed total is unambiguous
	Routes.Add(0, Full);
	Routes.Add(1, Full);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	// Total exactly equals the two payloads; neither image overshoots the single limit.
	const DesignImageResources::FImageResourceLimits Limits = MakeLimits(1000, 2000);
	DesignImageResources::FImageOperationBudget Budget(Limits);

	TArray<FString> FirstUrl{ Server.MakeUrl(0) };
	TMap<FString, FUrlDownloadResult> FirstResults;
	{
		const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
			MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);
		FImagePayloadIO::DownloadUrls(FirstUrl, 10.0f, true, Limits, BulkBudget, FirstResults);
	}
	TArray<FString> SecondUrl{ Server.MakeUrl(1) };
	TMap<FString, FUrlDownloadResult> SecondResults;
	{
		const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
			MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);
		FImagePayloadIO::DownloadUrls(SecondUrl, 10.0f, true, Limits, BulkBudget, SecondResults);
	}

	TestEqual(TEXT("First image is accepted"), AcceptedBytes(FirstResults), static_cast<int64>(1000));
	TestEqual(TEXT("Second image is accepted"), AcceptedBytes(SecondResults), static_cast<int64>(1000));

	// The shared budget now sits exactly at its limit: any further byte must be refused,
	// proving the received bytes were really charged and not reset per URL or per batch.
	FString BudgetError;
	TestFalse(TEXT("Budget is exhausted after two 1000-byte images against a 2000-byte total"),
		Budget.ConsumeCompressedBytes(1, BudgetError));
	TestTrue(TEXT("Exhaustion reports the cumulative operation limit"),
		BudgetError.Contains(TEXT("Total compressed image data exceeds the operation limit")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveCumulativeTotalOverLimitRejectsTest,
	"Design2UMG.RemoteImages.ReceiveBudget.CumulativeTotalOverLimitRejects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveCumulativeTotalOverLimitRejectsTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute Full;
	Full.BodyBytes = 1000;
	Full.ChunkBytes = 1000;
	Routes.Add(0, Full);
	Routes.Add(1, Full);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	// One byte below what the two images together would cost.
	const DesignImageResources::FImageResourceLimits Limits = MakeLimits(1000, 1999);
	DesignImageResources::FImageOperationBudget Budget(Limits);

	TArray<FString> FirstUrl{ Server.MakeUrl(0) };
	TMap<FString, FUrlDownloadResult> FirstResults;
	{
		const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
			MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);
		FImagePayloadIO::DownloadUrls(FirstUrl, 10.0f, true, Limits, BulkBudget, FirstResults);
	}
	TestEqual(TEXT("First image still fits"), AcceptedBytes(FirstResults), static_cast<int64>(1000));

	TArray<FString> SecondUrl{ Server.MakeUrl(1) };
	TMap<FString, FUrlDownloadResult> SecondResults;
	{
		const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
			MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);
		FImagePayloadIO::DownloadUrls(SecondUrl, 10.0f, true, Limits, BulkBudget, SecondResults);
	}
	TestEqual(TEXT("Second image is not accepted"), AcceptedBytes(SecondResults), static_cast<int64>(0));

	const FUrlDownloadResult* Second = SecondResults.Find(Server.MakeUrl(1));
	TestNotNull(TEXT("Rejected image has a result"), Second);
	TestTrue(TEXT("Rejection names the cumulative operation limit"),
		Second && Second->Error.Contains(TEXT("Total compressed image data exceeds the operation limit")));
	TestFalse(TEXT("Cumulative rejection is not reported as the single-image limit"),
		Second && Second->Error.Contains(TEXT("single-image byte limit")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveChunkedResponseWithoutLengthIsLimitedByActualChunksTest,
	"Design2UMG.RemoteImages.ReceiveBudget.ChunkedResponseWithoutLengthIsLimitedByActualChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveChunkedResponseWithoutLengthIsLimitedByActualChunksTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute SmallChunked;
	SmallChunked.BodyBytes = 600;
	SmallChunked.ChunkBytes = 200;
	SmallChunked.bChunkedTransfer = true;
	Routes.Add(0, SmallChunked);

	FLocalTestHttpServer::FRoute BigChunked;
	BigChunked.BodyBytes = 1500;
	BigChunked.ChunkBytes = 300;
	BigChunked.bChunkedTransfer = true;
	Routes.Add(1, BigChunked);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	const DesignImageResources::FImageResourceLimits Limits = MakeLimits(1000, 100000);
	DesignImageResources::FImageOperationBudget Budget(Limits);
	const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
		MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);

	TArray<FString> Urls;
	Urls.Add(Server.MakeUrl(0));
	Urls.Add(Server.MakeUrl(1));
	TMap<FString, FUrlDownloadResult> Results;
	FImagePayloadIO::DownloadUrls(Urls, 10.0f, true, Limits, BulkBudget, Results);

	// A chunked response with no Content-Length header at all: the limit must come from
	// the bytes actually handed to the receive callback.
	const FUrlDownloadResult* Small = Results.Find(Server.MakeUrl(0));
	TestNotNull(TEXT("Small chunked result exists"), Small);
	TestTrue(TEXT("An in-limit chunked response is accepted"), Small && Small->bSuccess);
	TestEqual(TEXT("Chunked body is reassembled in full"),
		Small ? Small->Data.Num() : 0, 600);

	const FUrlDownloadResult* Big = Results.Find(Server.MakeUrl(1));
	TestNotNull(TEXT("Big chunked result exists"), Big);
	TestFalse(TEXT("An over-limit chunked response is rejected"), Big && Big->bSuccess);
	TestTrue(TEXT("Chunked rejection names the single-image byte limit"),
		Big && Big->Error.Contains(TEXT("single-image byte limit")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveTimedOutRequestKeepsTimeoutReasonTest,
	"Design2UMG.RemoteImages.ReceiveBudget.TimedOutRequestKeepsTimeoutReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveTimedOutRequestKeepsTimeoutReasonTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute Slow;
	Slow.BodyBytes = 1000;
	Slow.ChunkBytes = 500;
	// The first 500 bytes arrive immediately; the rest only after the client's deadline,
	// so termination happens with a partially received payload on the wire.
	Slow.InterChunkDelaySeconds = 3.0;
	Routes.Add(0, Slow);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	const DesignImageResources::FImageResourceLimits Limits = MakeLimits(4096, 65536);
	DesignImageResources::FImageOperationBudget Budget(Limits);
	const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
		MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);

	TArray<FString> Urls;
	Urls.Add(Server.MakeUrl(0));
	TMap<FString, FUrlDownloadResult> Results;
	// 1 second is the smallest per-request deadline DownloadUrls accepts.
	FImagePayloadIO::DownloadUrls(Urls, 1.0f, true, Limits, BulkBudget, Results);

	const FUrlDownloadResult* Result = Results.Find(Server.MakeUrl(0));
	TestNotNull(TEXT("Timed-out result exists"), Result);
	TestFalse(TEXT("A request that missed its deadline is not a success"), Result && Result->bSuccess);
	TestTrue(TEXT("Termination reason stays the timeout"),
		Result && Result->Error.Contains(TEXT("timed out")));
	TestFalse(TEXT("Termination reason is not overwritten by the late failure"),
		Result && Result->Error.Contains(TEXT("Network request failed")));
	TestEqual(TEXT("Timed-out request keeps no payload"), Result ? Result->Data.Num() : 0, 0);

	// Let the server's answer arrive after DownloadUrls already returned, so the late
	// callback runs against a request the wait loop has abandoned.
	FPlatformProcess::Sleep(3.5f);
	TestTrue(TEXT("Late response does not resurrect the request"),
		Result && !Result->bSuccess && Result->Error.Contains(TEXT("timed out")));
	TestEqual(TEXT("Late response does not append data"), Result ? Result->Data.Num() : 0, 0);
	// No data may be billed or appended after termination: the shared budget must hold
	// exactly the 500 bytes that arrived before the deadline - not one byte more. The
	// check runs after the late response so it also covers the abandoned callback.
	AssertBilledBytes(*this, Budget, 65536, 500, TEXT("Terminated request stops receiving"));
	return true;
}

// Concurrent downloads near the shared byte limit.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveConcurrentNearLimitRaceStaysWithinBudgetTest,
	"Design2UMG.RemoteImages.ReceiveBudget.ConcurrentNearLimitRaceStaysWithinBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveConcurrentNearLimitRaceStaysWithinBudgetTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;

	// Four concurrent responses share the budget; TCP controls receive chunk boundaries.
	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute Single;
	Single.BodyBytes = 1000;
	Single.ChunkBytes = 1000;
	for (int32 RouteId = 0; RouteId < 4; ++RouteId)
	{
		Routes.Add(RouteId, Single);
	}

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}
	// Hold all four connections until the last one is connected, then let them write
	// together: without this the listener would answer one connection after another and
	// the receive callbacks could never actually race for the last slice of budget.
	Server.SetRendezvousSize(4);

	// Room for three complete payloads and part of the fourth.
	const DesignImageResources::FImageResourceLimits Limits = MakeLimits(1000, 3500);
	DesignImageResources::FImageOperationBudget Budget(Limits);
	const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
		MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);

	TArray<FString> Urls;
	for (int32 RouteId = 0; RouteId < 4; ++RouteId)
	{
		Urls.Add(Server.MakeUrl(RouteId));
	}
	TMap<FString, FUrlDownloadResult> Results;
	FImagePayloadIO::DownloadUrls(Urls, 10.0f, true, Limits, BulkBudget, Results);

	TestEqual(TEXT("Every URL gets a result"), Results.Num(), 4);
	TestEqual(TEXT("All four requests reached the server"), Server.GetRequestCount(), 4);
	TestTrue(TEXT("The complete response set exceeds the budget"), SucceededCount(Results) < 4);
	for (const auto& Pair : Results)
	{
		if (Pair.Value.bSuccess)
		{
			TestEqual(TEXT("Successful response is complete"), Pair.Value.Data.Num(), 1000);
		}
		else
		{
			TestTrue(TEXT("Rejection reports the shared limit, not a transport error"),
				Pair.Value.Error.Contains(TEXT("Total compressed image data exceeds the operation limit")));
			TestEqual(TEXT("Rejected response releases its payload"), Pair.Value.Data.Num(), 0);
		}
	}

	// Failed responses can consume fragments before rejection. Measure remaining
	// capacity instead of assuming that each Send became a single callback.
	uint64 Remaining = 0;
	FString Error;
	while (Remaining <= Limits.MaxTotalCompressedBytes && Budget.ConsumeCompressedBytes(1, Error)) ++Remaining;
	TestTrue(TEXT("Remaining capacity is bounded"), Remaining <= Limits.MaxTotalCompressedBytes);
	TestTrue(TEXT("At least one response reaches the shared limit"), Remaining < 1000);
	TestTrue(TEXT("All retained payload bytes were billed"),
		Remaining + static_cast<uint64>(AcceptedBytes(Results)) <= Limits.MaxTotalCompressedBytes);

	return true;
}

// Late completion preserves the budget-limit rejection reason.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveBudgetRejectedRequestKeepsBudgetReasonTest,
	"Design2UMG.RemoteImages.ReceiveBudget.BudgetRejectedRequestKeepsBudgetReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveBudgetRejectedRequestKeepsBudgetReasonTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	// 3 x 500 bytes against a 1000-byte single-image limit: the third chunk is refused
	// mid-stream and the transfer is aborted, so the completion callback arrives late for
	// a request the receive stage already terminated.
	FLocalTestHttpServer::FRoute OverLimit;
	OverLimit.BodyBytes = 1500;
	OverLimit.ChunkBytes = 500;
	OverLimit.InterChunkDelaySeconds = 0.05;
	Routes.Add(0, OverLimit);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	const DesignImageResources::FImageResourceLimits Limits = MakeLimits(1000, 100000);
	DesignImageResources::FImageOperationBudget Budget(Limits);
	const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
		MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);

	TArray<FString> Urls;
	Urls.Add(Server.MakeUrl(0));
	TMap<FString, FUrlDownloadResult> Results;
	FImagePayloadIO::DownloadUrls(Urls, 10.0f, true, Limits, BulkBudget, Results);

	const FUrlDownloadResult* Result = Results.Find(Server.MakeUrl(0));
	TestNotNull(TEXT("Rejected result exists"), Result);
	TestFalse(TEXT("A request rejected by the receive budget is not a success"), Result && Result->bSuccess);
	TestTrue(TEXT("The late completion callback keeps the budget-limit reason"),
		Result && Result->Error.Contains(TEXT("single-image byte limit")));
	TestFalse(TEXT("The budget reason is not overwritten by the aborted transport"),
		Result && Result->Error.Contains(TEXT("Network request failed")));
	TestEqual(TEXT("No partial payload is handed out after the rejection"),
		Result ? Result->Data.Num() : 0, 0);

	// Exactly the two accepted chunks (1000 bytes) are billed; the refused third chunk is
	// never charged, and the aborted tail is not appended or billed either.
	AssertBilledBytes(*this, Budget, 100000, 1000, TEXT("Refused chunk is not billed"));
	return true;
}

// Shared URLs are downloaded and billed once.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveSharedUrlAcrossNodesDownloadsOnceTest,
	"Design2UMG.RemoteImages.ReceiveBudget.SharedUrlAcrossNodesDownloadsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveSharedUrlAcrossNodesDownloadsOnceTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;
	FScopedRemoteImageSettings ScopedSettings;
	if (!ScopedSettings.Settings)
	{
		AddError(TEXT("Design2UMG settings are unavailable"));
		return false;
	}

	const TArray<uint8> SmallPng = MakePngBytes(16, 16);
	if (!TestTrue(TEXT("A small test PNG could be encoded"), SmallPng.Num() > 0))
	{
		return false;
	}

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute Small;
	Small.Body = SmallPng;
	Small.BodyBytes = SmallPng.Num();
	Small.ChunkBytes = FMath::Max(1, SmallPng.Num());
	Routes.Add(0, Small);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	// The preview entry deduplicates URLs before downloading. Two nodes sharing one URL
	// must produce one transfer and one budget charge.
	const FString SharedUrl = Server.MakeUrl(0);
	FUMGWidgetNode Root = MakeReceiveBudgetCanvas(TEXT("ReceiveBudget_SharedUrlRoot"));
	Root.Children.Add(MakeReceiveBudgetImageNode(TEXT("ReceiveBudget_Shared_A"), SharedUrl));
	Root.Children.Add(MakeReceiveBudgetImageNode(TEXT("ReceiveBudget_Shared_B"), SharedUrl));

	TMap<FString, UTexture2D*> TextureMap;
	FDesignRemoteImageImporter::ResolveTransientTextures(Root, TextureMap);

	TestEqual(TEXT("Two nodes sharing one URL reach the server only once"), Server.GetRequestCount(), 1);
	TestEqual(TEXT("The shared URL resolves to one texture"), TextureMap.Num(), 1);
	UTexture2D* const* SharedTexture = TextureMap.Find(SharedUrl);
	TestNotNull(TEXT("The shared URL has a resolved texture"), SharedTexture);
	TestTrue(TEXT("The resolved texture is valid"), SharedTexture && *SharedTexture != nullptr);
	FDesignRemoteImageImporter::ReleaseTransientTextures(TextureMap);

	// Billing follows the transfer, so one transfer costs exactly one payload. A second
	// transfer of the same URL - which is what the old duplicate-URL assertion accepted -
	// would have charged the shared budget twice.
	const DesignImageResources::FImageResourceLimits Limits = MakeLimits(4096, 65536);
	DesignImageResources::FImageOperationBudget Budget(Limits);
	const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
		MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);
	TArray<FString> DedupedUrls;
	DedupedUrls.Add(SharedUrl);
	TMap<FString, FUrlDownloadResult> Results;
	FImagePayloadIO::DownloadUrls(DedupedUrls, 10.0f, true, Limits, BulkBudget, Results);

	TestEqual(TEXT("The deduplicated URL downloads one payload"),
		AcceptedBytes(Results), static_cast<int64>(SmallPng.Num()));
	AssertBilledBytes(*this, Budget, 65536, static_cast<uint64>(SmallPng.Num()),
		TEXT("One shared URL is billed once, not once per node"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveConsumedPayloadLeavesDownloadCacheTest,
	"Design2UMG.RemoteImages.ReceiveBudget.ConsumedPayloadLeavesDownloadCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveConsumedPayloadLeavesDownloadCacheTest::RunTest(const FString& Parameters)
{
	FUrlDownloadResult Download;
	Download.bSuccess = true;
	Download.Data.SetNumUninitialized(4096);
	FMemory::Memset(Download.Data.GetData(), 0x5a, Download.Data.Num());

	TArray<uint8> Payload = Download.TakePayload();
	TestTrue(TEXT("Transport success remains cached after payload transfer"), Download.bSuccess);
	TestEqual(TEXT("The cached result no longer owns payload bytes"), Download.Data.Num(), 0);
	TestEqual(TEXT("The cached result releases its payload allocation"),
		Download.Data.GetAllocatedSize(), static_cast<SIZE_T>(0));
	TestEqual(TEXT("The current consumer receives the complete payload"), Payload.Num(), 4096);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveSharedInvalidUrlCachesProcessingFailureTest,
	"Design2UMG.RemoteImages.ReceiveBudget.SharedInvalidUrlCachesProcessingFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveSharedInvalidUrlCachesProcessingFailureTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;
	FScopedRemoteImageSettings ScopedSettings;
	if (!ScopedSettings.Settings)
	{
		AddError(TEXT("Design2UMG settings unavailable"));
		return false;
	}

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute InvalidImage;
	InvalidImage.BodyBytes = 1024;
	InvalidImage.ChunkBytes = 1024;
	Routes.Add(0, InvalidImage);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	const FString SharedUrl = Server.MakeUrl(0);
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ReceiveBudget_SharedInvalid");
	Data.RootWidget = MakeReceiveBudgetCanvas(TEXT("ReceiveBudget_SharedInvalidRoot"));
	Data.RootWidget.Children.Add(MakeReceiveBudgetImageNode(TEXT("ReceiveBudget_Invalid_A"), SharedUrl));
	Data.RootWidget.Children.Add(MakeReceiveBudgetImageNode(TEXT("ReceiveBudget_Invalid_B"), SharedUrl));

	const FDesignImageImportResult ImageResult = FDesignRemoteImageImporter::ResolveRemoteImages(Data);
	const FString& Report = ImageResult.DisplaySummary;
	TestEqual(TEXT("Two nodes still cause one transport request"), Server.GetRequestCount(), 1);
	TestTrue(TEXT("Both nodes report the shared processing failure"), Report.Contains(TEXT("failed=2")));
	TestTrue(TEXT("The repeated node uses the cached processing outcome"),
		Report.Contains(TEXT("reused cached processing failure")));
	return true;
}

// Local decoding and remote reception share one operation budget.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveLocalDecodeAndRemoteReceiveShareOneBudgetTest,
	"Design2UMG.RemoteImages.ReceiveBudget.LocalDecodeAndRemoteReceiveShareOneBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveLocalDecodeAndRemoteReceiveShareOneBudgetTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute Full;
	Full.BodyBytes = 1000;
	Full.ChunkBytes = 1000;
	Routes.Add(0, Full);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	DesignImageResources::FImageResourceLimits Limits = MakeLimits(1000, 1500);
	Limits.MaxTotalDecodedBytes = 4096; // 1024 BGRA pixels
	DesignImageResources::FImageOperationBudget Budget(Limits);

	// Local side: the production decode path used for local image files draws from the
	// same budget object.
	const TArray<uint8> LocalPng = MakePngBytes(16, 16);
	TestTrue(TEXT("A local test PNG could be encoded"), LocalPng.Num() > 0);
	int32 Width = 0, Height = 0;
	TArray64<uint8> Raw;
	FString DecodeError;
	TestTrue(TEXT("A local image decodes against the shared budget"),
		FImagePayloadIO::DecodeToBGRA8(LocalPng, Limits, Width, Height, Raw, DecodeError, &Budget));
	TestEqual(TEXT("Decoded local image keeps its dimensions"), Width, 16);

	// Remote side: the receive stage draws from the same budget object.
	const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
		MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);
	TArray<FString> Urls;
	Urls.Add(Server.MakeUrl(0));
	TMap<FString, FUrlDownloadResult> Results;
	FImagePayloadIO::DownloadUrls(Urls, 10.0f, true, Limits, BulkBudget, Results);
	TestEqual(TEXT("The remote payload is received"), AcceptedBytes(Results), static_cast<int64>(1000));

	// Both stages moved the *same* budget: the local decode consumed the decoded ceiling
	// and the remote receive consumed 1000 of the 1500 compressed bytes.
	FString DecodedError;
	TestTrue(TEXT("The local decode is visible on the shared budget"),
		Budget.ConsumeDecodedPixels(1024 - 256, DecodedError));
	TestFalse(TEXT("The local decode is not charged to a private budget"),
		Budget.ConsumeDecodedPixels(256, DecodedError));
	AssertBilledBytes(*this, Budget, 1500, 1000, TEXT("The remote receive is visible too"));
	return true;
}

// Local file bytes and remote receive chunks share the compressed-byte budget.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveLocalFileAndRemoteReceiveShareOneCompressedBudgetTest,
	"Design2UMG.RemoteImages.ReceiveBudget.LocalFileAndRemoteReceiveShareOneCompressedBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveLocalFileAndRemoteReceiveShareOneCompressedBudgetTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute Full;
	Full.BodyBytes = 1000;
	Full.ChunkBytes = 1000;
	Routes.Add(0, Full);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	// A real PNG on disk, read through the production local-file loader.
	const TArray<uint8> LocalPng = MakePngBytes(16, 16);
	if (!TestTrue(TEXT("A local test PNG could be encoded"), LocalPng.Num() > 0))
	{
		return false;
	}
	const FString LocalPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("Design2UMG_ReceiveBudget_LocalFile.png")));
	if (!TestTrue(TEXT("The local test PNG could be written to disk"),
			FFileHelper::SaveArrayToFile(LocalPng, *LocalPath)))
	{
		return false;
	}
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*LocalPath); };

	// The ceiling holds the local file plus the remote payload exactly, so the remote
	// receive can only succeed if the local file really was charged to the same budget.
	const uint64 LocalBytes = static_cast<uint64>(LocalPng.Num());
	const DesignImageResources::FImageResourceLimits Limits = MakeLimits(4096, LocalBytes + 1000);
	DesignImageResources::FImageOperationBudget Budget(Limits);

	TArray<uint8> FileData;
	FString ReadError;
	TestTrue(TEXT("The production local-file loader reads the PNG"),
		FImagePayloadIO::LoadLocalFile(LocalPath, Limits, FileData, ReadError));
	TestEqual(TEXT("The loader returns the whole file"), FileData.Num(), LocalPng.Num());

	// The production local branch charges the loaded bytes with exactly this call.
	FString BudgetError;
	TestTrue(TEXT("The local file fits the shared compressed budget"),
		Budget.ConsumeCompressedBytes(static_cast<uint64>(FileData.Num()), BudgetError));

	const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
		MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);
	TArray<FString> Urls;
	Urls.Add(Server.MakeUrl(0));
	TMap<FString, FUrlDownloadResult> Results;
	FImagePayloadIO::DownloadUrls(Urls, 10.0f, true, Limits, BulkBudget, Results);
	TestEqual(TEXT("The remote payload is received"), AcceptedBytes(Results), static_cast<int64>(1000));

	// Both stages moved the same cumulative compressed budget, and together they fill it.
	AssertBilledBytes(*this, Budget, LocalBytes + 1000, LocalBytes + 1000,
		TEXT("Local file bytes and remote received bytes share one compressed budget"));

	// Control: on a fresh budget the same remote payload alone still fits, so the
	// exhaustion above is the local file's charge and not the payload being too big.
	DesignImageResources::FImageOperationBudget FreshBudget(Limits);
	const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> FreshBulkBudget =
		MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(FreshBudget);
	TMap<FString, FUrlDownloadResult> FreshResults;
	FImagePayloadIO::DownloadUrls(Urls, 10.0f, true, Limits, FreshBulkBudget, FreshResults);
	TestEqual(TEXT("Without the local file the same payload still fits"),
		AcceptedBytes(FreshResults), static_cast<int64>(1000));
	return true;
}

// User cancellation remains the terminal outcome after a late callback.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveUserCancelledDownloadKeepsCancelReasonTest,
	"Design2UMG.RemoteImages.ReceiveBudget.UserCancelledDownloadKeepsCancelReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveUserCancelledDownloadKeepsCancelReasonTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;

	// The body is written in two 500-byte chunks two seconds apart, and the cancellation
	// probe fires in between: the transfer is abandoned after the first chunk was already
	// accepted and billed, with the rest of the body still on its way.
	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute Staggered;
	Staggered.BodyBytes = 1000;
	Staggered.ChunkBytes = 500;
	Staggered.InterChunkDelaySeconds = 2.0;
	Routes.Add(0, Staggered);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	const DesignImageResources::FImageResourceLimits Limits = MakeLimits(4096, 65536);
	DesignImageResources::FImageOperationBudget Budget(Limits);
	const TSharedRef<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe> BulkBudget =
		MakeShared<FImagePayloadIO::FBulkDownloadBudget, ESPMode::ThreadSafe>(Budget);

	TArray<FString> Urls;
	Urls.Add(Server.MakeUrl(0));
	TMap<FString, FUrlDownloadResult> Results;
	// The probe stands in for the interactive slow-task cancel button: it feeds the same
	// terminal path, which is otherwise unreachable from an unattended automation run.
	const double CancelAt = FPlatformTime::Seconds() + 0.8;
	FImagePayloadIO::DownloadUrls(Urls, 10.0f, true, Limits, BulkBudget, Results,
		[CancelAt]() { return FPlatformTime::Seconds() >= CancelAt; });

	const FUrlDownloadResult* Result = Results.Find(Server.MakeUrl(0));
	TestNotNull(TEXT("Cancelled result exists"), Result);
	TestFalse(TEXT("A cancelled download is not a success"), Result && Result->bSuccess);
	TestTrue(TEXT("Termination reason is the user cancel"),
		Result && Result->Error.Contains(TEXT("cancelled by user")));
	TestFalse(TEXT("The cancel reason is not overwritten by the aborted transport"),
		Result && Result->Error.Contains(TEXT("Network request failed")));
	TestEqual(TEXT("A cancelled download keeps no payload"), Result ? Result->Data.Num() : 0, 0);

	// Let the server finish writing the rest of the body after DownloadUrls returned, so
	// the late completion callback runs against a request the wait loop has abandoned.
	FPlatformProcess::Sleep(2.5f);
	TestTrue(TEXT("Late response does not resurrect the cancelled request"),
		Result && !Result->bSuccess && Result->Error.Contains(TEXT("cancelled by user")));
	TestEqual(TEXT("Late response does not append data"), Result ? Result->Data.Num() : 0, 0);

	// No data may be billed or appended after termination: the shared budget holds exactly
	// the 500 bytes that arrived before the cancel - not one byte more. The check runs
	// after the late response so it also covers the abandoned callback.
	AssertBilledBytes(*this, Budget, 65536, 500, TEXT("Cancelled request stops receiving"));
	return true;
}

// Import-entry receive limits.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceiveImportEntryEnforcesReceiveBudgetTest,
	"Design2UMG.RemoteImages.ReceiveBudget.ImportEntryEnforcesReceiveBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceiveImportEntryEnforcesReceiveBudgetTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;
	FScopedRemoteImageSettings ScopedSettings;
	if (!ScopedSettings.Settings)
	{
		AddError(TEXT("Design2UMG settings are unavailable"));
		return false;
	}

	// One payload larger than the default 32 MiB single-image limit.
	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute Huge;
	Huge.BodyBytes = 40 * 1024 * 1024;
	Huge.ChunkBytes = 4 * 1024 * 1024;
	Routes.Add(0, Huge);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ReceiveBudget_ImportEntry");
	Data.RootWidget = MakeReceiveBudgetCanvas(TEXT("ReceiveBudget_ImportRoot"));
	Data.RootWidget.Children.Add(MakeReceiveBudgetImageNode(TEXT("ReceiveBudget_Huge"), Server.MakeUrl(0)));

	const FDesignImageImportResult ImageResult = DesignImportPlanning::ResolveImages(Data);
	const FString& Report = ImageResult.DisplaySummary;

	TestTrue(TEXT("The formal import entry reports the failure"), Report.Contains(TEXT("failed=1")));
	TestTrue(TEXT("The formal import entry is stopped by the receive byte budget"),
		Report.Contains(TEXT("single-image byte limit")));
	TestEqual(TEXT("The typed result records one failure"), ImageResult.FailedCount, 1);
	if (TestEqual(TEXT("The typed result records one diagnostic"), ImageResult.Diagnostics.Num(), 1))
	{
		TestEqual(
			TEXT("The diagnostic identifies a budget failure"),
			static_cast<uint8>(ImageResult.Diagnostics[0].Category),
			static_cast<uint8>(EDesignImageDiagnosticCategory::Budget));
	}
	TestEqual(TEXT("Image failures do not enter general warnings"), Data.Warnings.Num(), 0);
	return true;
}

// Preview-entry receive limits.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteImageReceivePreviewEntryEnforcesReceiveBudgetTest,
	"Design2UMG.RemoteImages.ReceiveBudget.PreviewEntryEnforcesReceiveBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteImageReceivePreviewEntryEnforcesReceiveBudgetTest::RunTest(const FString& Parameters)
{
	FScopedNoProxyEnv NoProxy;
	FScopedRemoteImageSettings ScopedSettings;
	if (!ScopedSettings.Settings)
	{
		AddError(TEXT("Design2UMG settings are unavailable"));
		return false;
	}

	const TArray<uint8> SmallPng = MakePngBytes(16, 16);
	if (!TestTrue(TEXT("A small test PNG could be encoded"), SmallPng.Num() > 0))
	{
		return false;
	}

	TMap<int32, FLocalTestHttpServer::FRoute> Routes;
	FLocalTestHttpServer::FRoute Small;
	Small.Body = SmallPng;
	Small.BodyBytes = SmallPng.Num();
	Small.ChunkBytes = FMath::Max(1, SmallPng.Num());
	Routes.Add(0, Small);
	FLocalTestHttpServer::FRoute Huge;
	Huge.BodyBytes = 40 * 1024 * 1024;
	Huge.ChunkBytes = 4 * 1024 * 1024;
	Routes.Add(1, Huge);

	FLocalTestHttpServer Server;
	if (!Server.Start(Routes))
	{
		AddError(TEXT("Loopback test server could not be started"));
		return false;
	}

	FUMGWidgetNode Root = MakeReceiveBudgetCanvas(TEXT("ReceiveBudget_PreviewRoot"));
	Root.Children.Add(MakeReceiveBudgetImageNode(TEXT("ReceiveBudget_Small"), Server.MakeUrl(0)));
	Root.Children.Add(MakeReceiveBudgetImageNode(TEXT("ReceiveBudget_Huge"), Server.MakeUrl(1)));

	TMap<FString, UTexture2D*> TextureMap;
	FDesignRemoteImageImporter::ResolveTransientTextures(Root, TextureMap);

	UTexture2D* const* SmallTexture = TextureMap.Find(Server.MakeUrl(0));
	TestNotNull(TEXT("An in-budget preview image still resolves to a transient texture"), SmallTexture);
	TestTrue(TEXT("The resolved preview texture is valid"), SmallTexture && *SmallTexture != nullptr);
	TestNull(TEXT("An over-budget preview image produces no texture"),
		TextureMap.Find(Server.MakeUrl(1)) ? *TextureMap.Find(Server.MakeUrl(1)) : nullptr);

	FDesignRemoteImageImporter::ReleaseTransientTextures(TextureMap);
	TestEqual(TEXT("Releasing the preview textures empties the map"), TextureMap.Num(), 0);
	return true;
}


// Timeout and user-cancellation cases above are the lifecycle contract: each
// holds a real receive callback in flight, ends it through production code, then
// waits past the server's late write and verifies that neither payload nor budget
// changes after DownloadUrls returns. A shared-reference-count watcher was removed
// because it sampled a wrapper implementation detail rather than this behavior.

#endif // WITH_AUTOMATION_WORKER
