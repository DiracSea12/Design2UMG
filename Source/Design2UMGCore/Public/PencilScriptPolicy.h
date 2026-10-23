// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#pragma once

// Shared with the standalone C++17 worker; keep this header independent of UE.
namespace PencilScriptPolicy
{
    inline constexpr int MaxRequestUtf8Bytes = 1024 * 1024;
    inline constexpr int MaxResultJsonUtf8Bytes = 1024 * 1024;
    inline constexpr int MaxHostOutputBytes = 2 * 1024 * 1024;
    inline constexpr int MaxEditorReplyCharacters = 4 * 1024 * 1024;
    inline constexpr int MaxRuntimeMemoryBytes = 16 * 1024 * 1024;
    inline constexpr int MaxRuntimeStackBytes = 512 * 1024;
    inline constexpr int RuntimeExecutionMilliseconds = 2000;
    inline constexpr int MaxHostProcessMemoryBytes = 256 * 1024 * 1024;
    inline constexpr int MaxHostActiveProcesses = 1;
    inline constexpr int HostWaitMilliseconds = 5000;
    inline constexpr int HostTerminationWaitMilliseconds = 5000;
    inline constexpr double EditorWaitSeconds = 8.0;
}
