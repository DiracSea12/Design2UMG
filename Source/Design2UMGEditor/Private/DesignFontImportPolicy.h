// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreTypes.h"

namespace DesignFontImportPolicy
{
	constexpr int32 MaxFontBytes = 33554432;
	constexpr float DownloadTimeoutSeconds = 30.0f;
	constexpr double WorkerTimeoutSeconds = 30.0;

	constexpr bool FitsByteLimit(int64 ByteCount, int64 ReceivedBytes)
	{
		return ByteCount <= MaxFontBytes - ReceivedBytes;
	}
}
