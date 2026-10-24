// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Interfaces/IHttpRequest.h"
#include <atomic>

namespace PencilFontTool
{
    struct FDownload
    {
        FString Version, Url, ArchiveSha256;
        int64 ArchiveBytes = 0, InstalledBytes = 0;
        FString InstallDirectory() const;
    };

    bool ParseDownload(const FString& Json, FDownload& Out, FString& Error);
    bool ReadDownload(FDownload& Out, FString& Error);
    FString DefaultStorageDirectory();
    FString StorageDirectory();
    bool SetStorageDirectory(const FString& Directory, FString& Error);
    bool IsInstalled();
    bool HasInstallation();
    bool IsBusy();
    bool AcquireWorker(FString& Worker, FString& Error);
    void ReleaseWorker();
    bool Remove(FString& Error);
    void Shutdown();

    // Owned by the service until completion; UI only polls and cancels its own operation.
    struct FInstall
    {
        std::atomic<bool> Cancelled{false};
        TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request;
        TFuture<FString> Work;
        FString Error;
        FString Directory;
        bool bComplete = false;
        void Cancel();
        bool Poll();
    };
    TSharedPtr<FInstall, ESPMode::ThreadSafe> Install(const FDownload& Download);
}
