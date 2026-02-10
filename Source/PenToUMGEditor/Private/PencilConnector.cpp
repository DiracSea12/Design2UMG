// Copyright Epic Games, Inc. All Rights Reserved.

#include "PencilConnector.h"
#include "PenToUMGLog.h"
#include "PenToUMGSettings.h"

#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

FPencilConnector::~FPencilConnector()
{
	Shutdown();
}

FString FPencilConnector::DiscoverMCPServerPath()
{
	// 1. 用户手动指定
	const UPenToUMGSettings* Settings = UPenToUMGSettings::Get();
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

	// 2. 搜索 antigravity 扩展目录
	const FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	TArray<FString> SearchDirs;
	SearchDirs.Add(UserProfile / TEXT(".antigravity/extensions"));
	SearchDirs.Add(UserProfile / TEXT(".vscode/extensions"));

	for (const FString& Dir : SearchDirs)
	{
		TArray<FString> ExtDirs;
		IFileManager::Get().FindFiles(ExtDirs, *(Dir / TEXT("highagency.pencildev-*")), false, true);

		for (const FString& ExtDir : ExtDirs)
		{
			const FString FullPath = Dir / ExtDir / TEXT("out") / BinaryName;
			if (FPaths::FileExists(FullPath))
			{
				UE_LOG(LogPenToUMG, Log, TEXT("PencilConnector: Found MCP Server at %s"), *FullPath);
				return FullPath;
			}
		}
	}

	return FString();
}

FString FPencilConnector::DetectAppName()
{
	// 1. 用户手动指定
	const UPenToUMGSettings* Settings = UPenToUMGSettings::Get();
	if (Settings && !Settings->PencilAppName.IsEmpty())
		return Settings->PencilAppName;

	// 2. 检测运行中的进程
#if PLATFORM_WINDOWS
	FPlatformProcess::FProcEnumerator ProcEnum;
	while (ProcEnum.MoveNext())
	{
		const FString Name = ProcEnum.GetCurrent().GetName();
		if (Name.Contains(TEXT("antigravity")))
			return TEXT("antigravity");
		if (Name.Equals(TEXT("Code.exe"), ESearchCase::IgnoreCase))
			return TEXT("code");
	}
#endif

	// 3. 默认
	return TEXT("antigravity");
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

	const FString ServerPath = DiscoverMCPServerPath();
	if (ServerPath.IsEmpty())
	{
		UE_LOG(LogPenToUMG, Error,
			TEXT("PencilConnector: MCP Server binary not found"));
		return false;
	}

	const FString AppName = DetectAppName();
	ServerPort = (Port > 0) ? Port : FindAvailablePort();

	const FString Args = FString::Printf(
		TEXT("-app %s -http -http-port %d"),
		*AppName, ServerPort);

	UE_LOG(LogPenToUMG, Log,
		TEXT("PencilConnector: Launching %s %s"), *ServerPath, *Args);

	ServerProcess = FPlatformProcess::CreateProc(
		*ServerPath, *Args,
		false, true, true,
		nullptr, 0, nullptr, nullptr);

	if (!ServerProcess.IsValid())
	{
		UE_LOG(LogPenToUMG, Error,
			TEXT("PencilConnector: Failed to launch MCP Server"));
		return false;
	}

	// 等待服务器就绪后连接
	FPlatformProcess::Sleep(1.0f);

	const FString Endpoint = FString::Printf(
		TEXT("http://localhost:%d/mcp"), ServerPort);

	if (!Client.Connect(Endpoint))
	{
		UE_LOG(LogPenToUMG, Error,
			TEXT("PencilConnector: Failed to connect to %s"), *Endpoint);
		Shutdown();
		return false;
	}

	return true;
}

void FPencilConnector::Shutdown()
{
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