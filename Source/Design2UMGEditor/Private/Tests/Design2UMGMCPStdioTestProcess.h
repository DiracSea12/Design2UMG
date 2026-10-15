// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "MCPStdioClient.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace Design2UMGEditorTests
{
	inline FString StdioTestPython()
	{
#if PLATFORM_WINDOWS
		return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/Python3/Win64/python.exe"));
#elif PLATFORM_MAC
		return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/Python3/Mac/bin/python3"));
#else
		return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/Python3/Linux/bin/python3"));
#endif
	}

	inline FString StdioTestArguments(const FString& Mode = TEXT("normal"))
	{
		const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		const FString Script = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures/mcp_stdio_server.py"));
		return FString::Printf(TEXT("-u \"%s\" %s"), *Script, *Mode);
	}

	inline bool PumpStdioUntil(TFunctionRef<bool()> Done, double Seconds = 5.0)
	{
		const double Deadline = FPlatformTime::Seconds() + Seconds;
		while (!Done() && FPlatformTime::Seconds() < Deadline)
		{
			FTSTicker::GetCoreTicker().Tick(0.01f);
			FPlatformProcess::Sleep(0.01f);
		}
		return Done();
	}

	inline bool ConnectStdioFixture(FAutomationTestBase& Test, FMCPStdioClient& Client)
	{
		bool bDone = false, bConnected = false;
		Client.ConnectAsync(StdioTestPython(), StdioTestArguments(), [&](bool bSuccess) { bConnected = bSuccess; bDone = true; });
		const bool bCompleted = PumpStdioUntil([&]() { return bDone; });
		if (!bCompleted) Client.Disconnect();
		return Test.TestTrue(TEXT("Stdio fixture handshake completes"), bCompleted && bConnected && Client.IsConnected());
	}
}
