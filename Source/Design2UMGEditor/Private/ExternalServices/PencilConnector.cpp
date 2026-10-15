// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PencilConnector.h"
#include "PencilExtensionLocator.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

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

void FPencilConnector::LaunchAsync(TFunction<void(bool)> OnComplete)
{
	Shutdown();
	const FString ServerPath = DiscoverMCPServerPath();
	if (ServerPath.IsEmpty())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PencilConnector: MCP server executable not found"));
		if (OnComplete) OnComplete(false);
		return;
	}
	const FString AppName = DetectAppName();
	Client.ConnectAsync(ServerPath, FString::Printf(TEXT("-app \"%s\""), *AppName), MoveTemp(OnComplete));
}

void FPencilConnector::Shutdown() { Client.Disconnect(); }
