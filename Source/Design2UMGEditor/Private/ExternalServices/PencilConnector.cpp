// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PencilConnector.h"
#include "PencilExtensionLocator.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
	FString GetPencilAppsDir()
	{
#if PLATFORM_WINDOWS
		const FString UserHome = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
#else
		const FString UserHome = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
#endif
		return UserHome / TEXT(".pencil/apps");
	}

	FString DetectRegisteredPencilAppName()
	{
		const FString AppsDir = GetPencilAppsDir();
		if (AppsDir.IsEmpty())
		{
			return FString();
		}

		for (const FString& PreferredName : { TEXT("visual_studio_code"), TEXT("vscode"), TEXT("antigravity") })
		{
			if (FPaths::FileExists(AppsDir / PreferredName))
			{
				return PreferredName;
			}
		}

		return FString();
	}

	void CompleteLaunchOnce(const TSharedRef<TFunction<void(bool bSuccess)>>& OnComplete, bool bSuccess)
	{
		if (*OnComplete)
		{
			(*OnComplete)(bSuccess);
			*OnComplete = nullptr;
		}
	}

	/** (Re)arm a shared retry ticker handle; the only place that schedules retry tickers. */
	void ScheduleTickerOnHandle(const TSharedPtr<FTSTicker::FDelegateHandle>& Handle, FTickerDelegate&& RetryDelegate, const float DelaySeconds)
	{
		if (Handle->IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(*Handle);
		}
		*Handle = FTSTicker::GetCoreTicker().AddTicker(MoveTemp(RetryDelegate), DelaySeconds);
	}
}

FPencilConnector::~FPencilConnector()
{
	Shutdown();
}

FString FPencilConnector::DiscoverMCPServerPath()
{
	// 1. User-specified path.
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (Settings && !Settings->MCPServerPath.IsEmpty())
	{
		if (FPaths::FileExists(Settings->MCPServerPath))
			return Settings->MCPServerPath;
	}

#if PLATFORM_WINDOWS
	const FString BinaryName = TEXT("mcp-server-windows-x64.exe");
#elif PLATFORM_MAC
	#if PLATFORM_CPU_ARM_FAMILY
		const FString BinaryName = TEXT("mcp-server-darwin-arm64");
	#else
		const FString BinaryName = TEXT("mcp-server-darwin-x64");
	#endif
#else
	const FString BinaryName = TEXT("mcp-server-linux-x64");
#endif

	// 2. Scan VS Code extension directories first, then fall back to antigravity.
	// Candidates are ordered per search directory (newest extension first), and the
	// first candidate containing the server binary wins.
	TArray<PencilExtensionLocator::FExtensionRoot> Candidates;
	TArray<FString> SearchedDirs;
	PencilExtensionLocator::GetExtensionRootCandidates(Candidates, SearchedDirs);

	for (const PencilExtensionLocator::FExtensionRoot& Candidate : Candidates)
	{
		const FString FullPath = Candidate.Directory / TEXT("out") / BinaryName;
		if (FPaths::FileExists(FullPath))
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("PencilConnector: Found MCP Server at %s (version %s)"), *FullPath, *Candidate.Version);
			return FullPath;
		}
	}

	return FString();
}

FString FPencilConnector::DetectAppName()
{
	// 1. User-specified app id.
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (Settings && !Settings->PencilAppName.IsEmpty())
		return Settings->PencilAppName;

	// 2. Prefer app ids already registered by Pencil instead of assuming a fixed alias.
	if (const FString RegisteredAppName = DetectRegisteredPencilAppName(); !RegisteredAppName.IsEmpty())
	{
		return RegisteredAppName;
	}

	// 3. Detect a running Pencil-compatible process.
	FPlatformProcess::FProcEnumerator ProcEnum;
	while (ProcEnum.MoveNext())
	{
		const FString Name = ProcEnum.GetCurrent().GetName();
#if PLATFORM_WINDOWS
		if (Name.Equals(TEXT("Code.exe"), ESearchCase::IgnoreCase))
			return TEXT("visual_studio_code");
#else
		if (Name.Equals(TEXT("code"), ESearchCase::IgnoreCase))
			return TEXT("visual_studio_code");
#endif
		if (Name.Contains(TEXT("antigravity")))
			return TEXT("antigravity");
	}

	// 4. Fall back to VS Code's Pencil app id by default.
	return TEXT("visual_studio_code");
}

int32 FPencilConnector::FindAvailablePort(int32 StartPort)
{
	ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSub)
		return StartPort;

	for (int32 Port = StartPort; Port < StartPort + 100; ++Port)
	{
		FSocket* TestSocket = SocketSub->CreateSocket(NAME_Stream, TEXT("PortTest"), false);
		if (!TestSocket)
			continue;

		TSharedRef<FInternetAddr> Addr = SocketSub->CreateInternetAddr();
		Addr->SetIp(0x7F000001); // 127.0.0.1
		Addr->SetPort(Port);

		const bool bBound = TestSocket->Bind(*Addr);
		SocketSub->DestroySocket(TestSocket);

		if (bBound)
			return Port;
	}

	return StartPort;
}

void FPencilConnector::LaunchAsync(int32 Port, TFunction<void(bool bSuccess)> OnComplete)
{
	Shutdown();
	RetryTickerHandle = MakeShared<FTSTicker::FDelegateHandle>();
	const TSharedRef<TFunction<void(bool bSuccess)>> SharedComplete =
		MakeShared<TFunction<void(bool bSuccess)>>(MoveTemp(OnComplete));

	const FString ServerPath = DiscoverMCPServerPath();
	if (ServerPath.IsEmpty())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PencilConnector: MCP Server binary not found"));
		CompleteLaunchOnce(SharedComplete, false);
		return;
	}

	const FString AppName = DetectAppName();
	const int32 ServerPort = (Port > 0) ? Port : FindAvailablePort();

	const FString Args = FString::Printf(TEXT("-app %s -http -http-port %d"), *AppName, ServerPort);
	UE_LOG(LogDesign2UMG, Log, TEXT("PencilConnector: Launching %s %s"), *ServerPath, *Args);

	const FProcHandle Process = FPlatformProcess::CreateProc(
		*ServerPath, *Args,
		false, true, true,
		nullptr, 0, nullptr, nullptr);

	if (!Process.IsValid())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PencilConnector: Failed to launch MCP Server"));
		CompleteLaunchOnce(SharedComplete, false);
		return;
	}

	const TSharedPtr<FLaunchSession, ESPMode::ThreadSafe> Session = MakeShared<FLaunchSession, ESPMode::ThreadSafe>(Client);
	Session->ServerProcess = Process;
	Session->TickerHandle = RetryTickerHandle;
	ActiveLaunchSession = Session;

	const FString Endpoint = FString::Printf(TEXT("http://localhost:%d/mcp"), ServerPort);
	constexpr double MaxWaitSeconds = 10.0;
	const double DeadlineSeconds = FPlatformTime::Seconds() + MaxWaitSeconds;
	TryConnectAsync(Session, Endpoint, DeadlineSeconds, SharedComplete);
}

// Launch callbacks retain shared state, check cancellation and complete at most once.
// Shutdown removes pending retry tickers; destruction uses the same shutdown path.
void FPencilConnector::TryConnectAsync(
	const TSharedPtr<FLaunchSession, ESPMode::ThreadSafe>& Session,
	const FString& Endpoint,
	double DeadlineSeconds,
	const TSharedRef<TFunction<void(bool bSuccess)>>& OnComplete)
{
	if (Session->bCancelled)
	{
		CompleteLaunchOnce(OnComplete, false);
		return;
	}

	if (!Session->ServerProcess.IsValid() || !FPlatformProcess::IsProcRunning(Session->ServerProcess))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PencilConnector: MCP Server process exited prematurely"));
		ShutdownLaunchSession(Session);
		CompleteLaunchOnce(OnComplete, false);
		return;
	}

	Session->Client->ConnectAsync(Endpoint, [Session, Endpoint, DeadlineSeconds, OnComplete](bool bConnected)
	{
		if (Session->bCancelled)
		{
			CompleteLaunchOnce(OnComplete, false);
			return;
		}

		if (bConnected)
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("PencilConnector: Connected asynchronously"));
			CompleteLaunchOnce(OnComplete, true);
			return;
		}

		if (FPlatformTime::Seconds() >= DeadlineSeconds)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("PencilConnector: Timed out waiting for %s"), *Endpoint);
			ShutdownLaunchSession(Session);
			CompleteLaunchOnce(OnComplete, false);
			return;
		}

		ScheduleTickerOnHandle(Session->TickerHandle,
			FTickerDelegate::CreateLambda([Session, Endpoint, DeadlineSeconds, OnComplete](float)
			{
				Session->TickerHandle->Reset();
				TryConnectAsync(Session, Endpoint, DeadlineSeconds, OnComplete);
				return false;
			}),
			0.2f);
	});
}

void FPencilConnector::ScheduleRetry(FTickerDelegate&& RetryDelegate, const float DelaySeconds)
{
	ScheduleTickerOnHandle(RetryTickerHandle, MoveTemp(RetryDelegate), DelaySeconds);
}

void FPencilConnector::ShutdownLaunchSession(const TSharedPtr<FLaunchSession, ESPMode::ThreadSafe>& Session)
{
	Session->bCancelled = true;
	if (Session->TickerHandle->IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(*Session->TickerHandle);
		Session->TickerHandle->Reset();
	}
	Session->Client->Disconnect();

	if (Session->ServerProcess.IsValid())
	{
		FPlatformProcess::TerminateProc(Session->ServerProcess, true);
		FPlatformProcess::CloseProc(Session->ServerProcess);
		Session->ServerProcess.Reset();
	}
}

void FPencilConnector::Shutdown()
{
	if (ActiveLaunchSession.IsValid())
	{
		ShutdownLaunchSession(ActiveLaunchSession);
		ActiveLaunchSession.Reset();
	}
	if (RetryTickerHandle.IsValid() && RetryTickerHandle->IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(*RetryTickerHandle);
	}
	RetryTickerHandle.Reset();
}

bool FPencilConnector::IsRunning() const
{
	return ActiveLaunchSession.IsValid()
		&& ActiveLaunchSession->ServerProcess.IsValid()
		&& FPlatformProcess::IsProcRunning(ActiveLaunchSession->ServerProcess)
		&& Client->IsConnected();
}
