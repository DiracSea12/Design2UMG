// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PencilConnector.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"

#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
	TArray<int32> ParseExtensionVersionParts(const FString& VersionText)
	{
		TArray<int32> Parts;
		TArray<FString> Tokens;
		VersionText.ParseIntoArray(Tokens, TEXT("."));
		for (const FString& Token : Tokens)
		{
			Parts.Add(FCString::Atoi(*Token));
		}
		return Parts;
	}

	bool IsHigherExtensionVersion(const FString& CandidateVersion, const FString& CurrentBestVersion)
	{
		if (CurrentBestVersion.IsEmpty())
		{
			return true;
		}

		const TArray<int32> CandidateParts = ParseExtensionVersionParts(CandidateVersion);
		const TArray<int32> CurrentParts = ParseExtensionVersionParts(CurrentBestVersion);
		const int32 MaxParts = FMath::Max(CandidateParts.Num(), CurrentParts.Num());
		for (int32 Index = 0; Index < MaxParts; ++Index)
		{
			const int32 CandidatePart = CandidateParts.IsValidIndex(Index) ? CandidateParts[Index] : 0;
			const int32 CurrentPart = CurrentParts.IsValidIndex(Index) ? CurrentParts[Index] : 0;
			if (CandidatePart != CurrentPart)
			{
				return CandidatePart > CurrentPart;
			}
		}

		return false;
	}

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

	void SyncVSCodePencilAppAlias()
	{
		const FString AppsDir = GetPencilAppsDir();
		if (AppsDir.IsEmpty())
		{
			return;
		}

		const FString CanonicalPath = AppsDir / TEXT("visual_studio_code");
		const FString LegacyPath = AppsDir / TEXT("vscode");
		FString PortValue;
		for (const FString& CandidatePath : { CanonicalPath, LegacyPath })
		{
			if (!FPaths::FileExists(CandidatePath))
			{
				continue;
			}

			if (FFileHelper::LoadFileToString(PortValue, *CandidatePath))
			{
				PortValue.TrimStartAndEndInline();
				if (!PortValue.IsEmpty())
				{
					break;
				}
			}
		}

		if (PortValue.IsEmpty())
		{
			return;
		}

		IFileManager::Get().MakeDirectory(*AppsDir, true);
		for (const FString& TargetPath : { CanonicalPath, LegacyPath })
		{
			FString Existing;
			bool bHasUtf8Bom = false;
			if (FPaths::FileExists(TargetPath) && FFileHelper::LoadFileToString(Existing, *TargetPath))
			{
				Existing.TrimStartAndEndInline();
			}
			if (FPaths::FileExists(TargetPath))
			{
				TArray<uint8> ExistingBytes;
				if (FFileHelper::LoadFileToArray(ExistingBytes, *TargetPath) && ExistingBytes.Num() >= 3)
				{
					bHasUtf8Bom =
						ExistingBytes[0] == 0xEF &&
						ExistingBytes[1] == 0xBB &&
						ExistingBytes[2] == 0xBF;
				}
			}
			if (Existing != PortValue || bHasUtf8Bom)
			{
				FFileHelper::SaveStringToFile(
					PortValue,
					*TargetPath,
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			}
		}
	}

	void CompleteLaunchOnce(const TSharedRef<TFunction<void(bool bSuccess)>>& OnComplete, bool bSuccess)
	{
		if (*OnComplete)
		{
			(*OnComplete)(bSuccess);
			*OnComplete = nullptr;
		}
	}
}

FPencilConnector::~FPencilConnector()
{
	Shutdown();
}

FString FPencilConnector::DiscoverMCPServerPath()
{
	// 1. 用户手动指定
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

	// 2. 优先搜索 VS Code 扩展目录，其次再回退 antigravity。
#if PLATFORM_WINDOWS
	const FString UserHome = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
#else
	const FString UserHome = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
#endif
	TArray<FString> SearchDirs;
	SearchDirs.Add(UserHome / TEXT(".vscode/extensions"));
	SearchDirs.Add(UserHome / TEXT(".antigravity/extensions"));

	for (const FString& Dir : SearchDirs)
	{
		TArray<FString> ExtDirs;
		IFileManager::Get().FindFiles(ExtDirs, *(Dir / TEXT("highagency.pencildev-*")), false, true);

		FString BestPath;
		FString BestVersion;
		for (const FString& ExtDir : ExtDirs)
		{
			const FString FullPath = Dir / ExtDir / TEXT("out") / BinaryName;
			if (FPaths::FileExists(FullPath))
			{
				const FString VersionText = ExtDir.RightChop(FString(TEXT("highagency.pencildev-")).Len());
				if (IsHigherExtensionVersion(VersionText, BestVersion))
				{
					BestVersion = VersionText;
					BestPath = FullPath;
				}
			}
		}

		if (!BestPath.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("PencilConnector: Found MCP Server at %s (version %s)"), *BestPath, *BestVersion);
			return BestPath;
		}
	}

	return FString();
}

FString FPencilConnector::DetectAppName()
{
	// 1. 用户手动指定
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (Settings && !Settings->PencilAppName.IsEmpty())
		return Settings->PencilAppName;

	// 2. 优先使用 Pencil 已注册过的 app id，避免继续假设固定别名。
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

	// 4. 默认回退到 VS Code 的 Pencil app id。
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

bool FPencilConnector::Launch(int32 Port)
{
	Shutdown();
	SyncVSCodePencilAppAlias();

	const FString ServerPath = DiscoverMCPServerPath();
	if (ServerPath.IsEmpty())
	{
		UE_LOG(LogDesign2UMG, Error,
			TEXT("PencilConnector: MCP Server binary not found"));
		return false;
	}

	const FString AppName = DetectAppName();
	ServerPort = (Port > 0) ? Port : FindAvailablePort();

	const FString Args = FString::Printf(
		TEXT("-app %s -http -http-port %d"),
		*AppName, ServerPort);

	UE_LOG(LogDesign2UMG, Log,
		TEXT("PencilConnector: Launching %s %s"), *ServerPath, *Args);

	ServerProcess = FPlatformProcess::CreateProc(
		*ServerPath, *Args,
		false, true, true,
		nullptr, 0, nullptr, nullptr);

	if (!ServerProcess.IsValid())
	{
		UE_LOG(LogDesign2UMG, Error,
			TEXT("PencilConnector: Failed to launch MCP Server"));
		return false;
	}

	const FString Endpoint = FString::Printf(
		TEXT("http://localhost:%d/mcp"), ServerPort);

	// Poll until the local MCP server is ready.
	constexpr float PollInterval = 0.2f;
	constexpr float MaxWait = 10.0f;
	float Elapsed = 0.0f;

	while (Elapsed < MaxWait)
	{
		if (!FPlatformProcess::IsProcRunning(ServerProcess))
		{
			UE_LOG(LogDesign2UMG, Error,
				TEXT("PencilConnector: MCP Server process exited prematurely"));
			Shutdown();
			return false;
		}

		if (Client.Connect(Endpoint))
		{
			UE_LOG(LogDesign2UMG, Log,
				TEXT("PencilConnector: Connected after %.1fs"), Elapsed);
			return true;
		}

		FPlatformProcess::Sleep(PollInterval);
		Elapsed += PollInterval;
	}

	UE_LOG(LogDesign2UMG, Error,
		TEXT("PencilConnector: Timed out waiting for %s (%.0fs)"), *Endpoint, MaxWait);
	Shutdown();
	return false;
}

void FPencilConnector::LaunchAsync(int32 Port, TFunction<void(bool bSuccess)> OnComplete)
{
	Shutdown();
	SyncVSCodePencilAppAlias();
	const int32 LaunchToken = ++ActiveLaunchToken;
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
	ServerPort = (Port > 0) ? Port : FindAvailablePort();

	const FString Args = FString::Printf(TEXT("-app %s -http -http-port %d"), *AppName, ServerPort);
	UE_LOG(LogDesign2UMG, Log, TEXT("PencilConnector: Launching %s %s"), *ServerPath, *Args);

	ServerProcess = FPlatformProcess::CreateProc(
		*ServerPath, *Args,
		false, true, true,
		nullptr, 0, nullptr, nullptr);

	if (!ServerProcess.IsValid())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PencilConnector: Failed to launch MCP Server"));
		CompleteLaunchOnce(SharedComplete, false);
		return;
	}

	const FString Endpoint = FString::Printf(TEXT("http://localhost:%d/mcp"), ServerPort);
	constexpr double MaxWaitSeconds = 10.0;
	const double DeadlineSeconds = FPlatformTime::Seconds() + MaxWaitSeconds;
	TryConnectAsync(Endpoint, DeadlineSeconds, LaunchToken, SharedComplete);
}

void FPencilConnector::TryConnectAsync(
	const FString& Endpoint,
	double DeadlineSeconds,
	int32 LaunchToken,
	const TSharedRef<TFunction<void(bool bSuccess)>>& OnComplete)
{
	if (LaunchToken != ActiveLaunchToken)
	{
		CompleteLaunchOnce(OnComplete, false);
		return;
	}

	if (!ServerProcess.IsValid() || !FPlatformProcess::IsProcRunning(ServerProcess))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PencilConnector: MCP Server process exited prematurely"));
		Shutdown();
		CompleteLaunchOnce(OnComplete, false);
		return;
	}

	Client.ConnectAsync(Endpoint, [this, Endpoint, DeadlineSeconds, LaunchToken, OnComplete](bool bConnected)
	{
		if (LaunchToken != ActiveLaunchToken)
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
			Shutdown();
			CompleteLaunchOnce(OnComplete, false);
			return;
		}

		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([this, Endpoint, DeadlineSeconds, LaunchToken, OnComplete](float)
			{
				TryConnectAsync(Endpoint, DeadlineSeconds, LaunchToken, OnComplete);
				return false;
			}),
			0.2f);
	});
}

void FPencilConnector::Shutdown()
{
	++ActiveLaunchToken;
	Client.Disconnect();

	if (ServerProcess.IsValid())
	{
		FPlatformProcess::TerminateProc(ServerProcess, true);
		FPlatformProcess::CloseProc(ServerProcess);
		ServerProcess.Reset();
	}

	ServerPort = 0;
}

bool FPencilConnector::IsRunning() const
{
	return ServerProcess.IsValid()
		&& FPlatformProcess::IsProcRunning(ServerProcess)
		&& Client.IsConnected();
}
