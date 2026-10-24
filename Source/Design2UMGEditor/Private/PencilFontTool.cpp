// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#include "PencilFontTool.h"
#include "PencilFontToolText.h"
#include "Misc/ConfigCacheIni.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include "Async/Async.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Serialization/Archive.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace PencilFontTool
{
namespace
{
    constexpr const TCHAR* PinnedVersion = TEXT("1.0.0");
    constexpr const TCHAR* DownloadUrl = TEXT("https://github.com/DiracSea12/Design2UMG/releases/download/pencil-font-worker-1.0.0/PencilFontWorker-1.0.0-Win64.zip");
    FCriticalSection UseLock;
    int32 Workers = 0;
    TUniquePtr<FArchive> WorkerFileLock;
    TSharedPtr<FInstall, ESPMode::ThreadSafe> ActiveInstall;

    FString Hash(const TArray<uint8>& Bytes)
    {
        uint8 Digest[SHA256_DIGEST_LENGTH];
        SHA256(Bytes.GetData(), Bytes.Num(), Digest);
        return BytesToHex(Digest, sizeof(Digest));
    }
    bool HashFile(const FString& Path, const FString& Expected)
    {
        TArray<uint8> Bytes;
        return FFileHelper::LoadFileToArray(Bytes, *Path) && Hash(Bytes).Equals(Expected, ESearchCase::IgnoreCase);
    }
    bool IsHash(const FString& Value)
    {
        if (Value.Len() != 64) return false;
        for (TCHAR C : Value) if (!FChar::IsHexDigit(C)) return false;
        return true;
    }
    bool ReadObject(const FString& Path, TSharedPtr<FJsonObject>& Object)
    {
        FString Text;
        return FFileHelper::LoadFileToString(Text, *Path) && DesignJsonUtils::TryParseObject(Text, Object);
    }
    bool IsWithin(const FString& Path, const FString& Root)
    {
        FString Full = FPaths::ConvertRelativePathToFull(Path);
        FString FullRoot = FPaths::ConvertRelativePathToFull(Root);
        FPaths::NormalizeDirectoryName(Full);
        FPaths::NormalizeDirectoryName(FullRoot);
        if (!FPaths::CollapseRelativeDirectories(Full) || !FPaths::CollapseRelativeDirectories(FullRoot)) return false;
        return Full.StartsWith(FullRoot + TEXT("/"), ESearchCase::IgnoreCase);
    }
    FString ToolRoot() { return StorageDirectory() / TEXT("PencilFontWorker"); }
    // Approved plugin ancestors may be junctions. Only the managed subtree rejects links.
    bool SafePath(const FString& Path, const FString& Root)
    {
        if (Path != Root && !IsWithin(Path, Root)) return false;
#if PLATFORM_WINDOWS
        FString Current = FPaths::ConvertRelativePathToFull(Path);
        const FString Stop = FPaths::GetPath(FPaths::ConvertRelativePathToFull(Root));
        while (Current != Stop)
        {
            const DWORD Attributes = GetFileAttributesW(*Current);
            if (Attributes != INVALID_FILE_ATTRIBUTES)
            {
                if (Attributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
            }
            else if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) return false;
            const FString Parent = FPaths::GetPath(Current);
            if (Parent == Current || Parent.IsEmpty()) return false;
            Current = Parent;
        }
#endif
        return true;
    }
    bool SafeRelative(const FString& Path)
    {
        TArray<FString> Parts;
        Path.ParseIntoArray(Parts, TEXT("/"), false);
        if (Path.IsEmpty() || Path.Contains(TEXT("\\")) || Path.Contains(TEXT(":"))) return false;
        for (const FString& Part : Parts)
            if (Part.IsEmpty() || Part == TEXT(".") || Part == TEXT("..") || Part.EndsWith(TEXT(".")) || Part.EndsWith(TEXT(" "))) return false;
        return Path == TEXT("PencilFontWorker.exe") || Path.StartsWith(TEXT("_internal/")) || Path.StartsWith(TEXT("notices/"));
    }
    bool CollectStaging(const FString& Directory, const FString& Root, TArray<FString>& Files, TArray<FString>& Directories)
    {
        if (!SafePath(Directory, Root)) return false;
        TArray<FString> Names;
        IFileManager::Get().FindFiles(Names, *(Directory / TEXT("*")), true, true);
        for (const FString& Name : Names)
        {
            const FString Path = Directory / Name;
            if (!SafePath(Path, Root)) return false;
            if (FPaths::DirectoryExists(Path))
            {
                if (!CollectStaging(Path, Root, Files, Directories)) return false;
            }
            else Files.Add(Path);
        }
        Directories.Add(Directory);
        return true;
    }
    void DeleteStaging(const FString& Directory, const FString& Root)
    {
        // Called only for a unique staging directory created by this operation.
        TArray<FString> Files, Directories;
        if (!CollectStaging(Directory, Root, Files, Directories)) return;
        for (const FString& File : Files)
            if (SafePath(File, Root)) IFileManager::Get().Delete(*File, false, false, true);
        for (const FString& Empty : Directories)
            if (SafePath(Empty, Root)) IFileManager::Get().DeleteDirectory(*Empty, false, false);
    }
    TUniquePtr<FArchive> LockToolFiles(const FString& Root)
    {
        if (!SafePath(Root / TEXT("in-use.lock"), Root) || !IFileManager::Get().MakeDirectory(*Root, true)) return nullptr;
        // Append mode obtains an exclusive writer without truncating an existing lock file.
        return TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*(Root / TEXT("in-use.lock")), FILEWRITE_Silent | FILEWRITE_Append));
    }
    bool Installed(const FDownload& Download)
    {
        TSharedPtr<FJsonObject> Receipt;
        FString ArchiveHash, IdentityHash;
        const FString Directory = Download.InstallDirectory();
        return SafePath(Directory / TEXT("installed.json"), FPaths::GetPath(Directory))
            && SafePath(Directory / TEXT("identity.json"), FPaths::GetPath(Directory))
            && ReadObject(Directory / TEXT("installed.json"), Receipt)
            && Receipt->TryGetStringField(TEXT("archive_sha256"), ArchiveHash)
            && ArchiveHash.Equals(Download.ArchiveSha256, ESearchCase::IgnoreCase)
            && Receipt->TryGetStringField(TEXT("identity_sha256"), IdentityHash) && IsHash(IdentityHash)
            && HashFile(Directory / TEXT("identity.json"), IdentityHash)
            && FPaths::FileExists(Directory / TEXT("PencilFontWorker.exe"))
            && FPaths::DirectoryExists(Directory / TEXT("_internal"))
            && FPaths::DirectoryExists(Directory / TEXT("notices"));
    }
    bool VerifyFiles(const FString& Directory, FString& Error)
    {
        TSharedPtr<FJsonObject> Identity;
        const TSharedPtr<FJsonObject>* Files = nullptr;
        FString BinaryHash;
        double Protocol = 0;
        if (!SafePath(Directory / TEXT("identity.json"), FPaths::GetPath(Directory))
            || !ReadObject(Directory / TEXT("identity.json"), Identity)
            || !Identity->TryGetNumberField(TEXT("protocol"), Protocol) || Protocol != 1
            || !Identity->TryGetStringField(TEXT("binary_sha256"), BinaryHash) || !IsHash(BinaryHash)
            || !Identity->TryGetObjectField(TEXT("files_sha256"), Files) || !Files
            || !(*Files)->HasField(TEXT("PencilFontWorker.exe")))
        {
            Error = FPencilFontToolText().Get(TEXT("InvalidIdentity")); return false;
        }
        for (const auto& Pair : (*Files)->Values)
        {
            FString Expected;
            const FString RelativePath(*Pair.Key);
            const FString File = Directory / RelativePath;
            if (!SafeRelative(RelativePath) || !SafePath(File, FPaths::GetPath(Directory))
                || !Pair.Value->TryGetString(Expected) || !IsHash(Expected) || !HashFile(File, Expected))
            {
                Error = FPencilFontToolText().Get(TEXT("FileVerificationFailed"), {{TEXT("File"), RelativePath}}); return false;
            }
        }
        if (!HashFile(Directory / TEXT("PencilFontWorker.exe"), BinaryHash))
        {
            Error = FPencilFontToolText().Get(TEXT("BinaryVerificationFailed")); return false;
        }
        return true;
    }
    FString QuotePS(FString Value)
    {
        Value.ReplaceInline(TEXT("'"), TEXT("''"));
        return TEXT("'") + Value + TEXT("'");
    }
    FString Extract(const FString& Archive, const FString& Staging, int64 InstalledBytes, const FInstall& Operation)
    {
        // The package is already authenticated. Still constrain every entry before extraction.
        const FString Script = TEXT("$ErrorActionPreference='Stop'; ")
            TEXT("function LongPath($path) { $path=[IO.Path]::GetFullPath($path); ")
            TEXT("if($path.StartsWith('\\\\')) { return '\\\\?\\UNC\\'+$path.Substring(2) }; return '\\\\?\\'+$path }; ")
            TEXT("Add-Type -AssemblyName System.IO.Compression.FileSystem; ")
            TEXT("$root=[IO.Path]::GetFullPath(") + QuotePS(Staging) + TEXT(")+[IO.Path]::DirectorySeparatorChar; ")
            TEXT("if(([IO.File]::GetAttributes((LongPath $root)) -band [IO.FileAttributes]::ReparsePoint) -or ")
            TEXT("[IO.Directory]::GetFileSystemEntries((LongPath $root)).Length -ne 0) { throw 'Staging is not empty or is a link' }; ")
            TEXT("$zip=[IO.Compression.ZipFile]::OpenRead((LongPath ") + QuotePS(Archive) + TEXT(")); try { [long]$size=0; ")
            TEXT("foreach($entry in $zip.Entries) { $name=$entry.FullName.Replace('\\','/'); ")
            TEXT("$dest=[IO.Path]::GetFullPath([IO.Path]::Combine($root,$name)); ")
            TEXT("if(!$dest.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -or $name.Contains(':') ")
            TEXT("-or (($entry.ExternalAttributes -shr 16) -band 61440) -eq 40960) { throw 'Unsafe ZIP entry' }; ")
            TEXT("if($name -ne 'PencilFontWorker.exe' -and $name -ne 'identity.json' -and ")
            TEXT("!$name.StartsWith('_internal/') -and !$name.StartsWith('notices/')) { throw 'Unexpected ZIP root' }; ")
            TEXT("$size += $entry.Length }; if($size -ne ") + LexToString(InstalledBytes) + TEXT(") { throw 'ZIP size mismatch' }; ")
            TEXT("foreach($entry in $zip.Entries) { $dest=[IO.Path]::GetFullPath([IO.Path]::Combine($root,$entry.FullName)); ")
            TEXT("[IO.Directory]::CreateDirectory((LongPath ([IO.Path]::GetDirectoryName($dest)))) > $null; ")
            TEXT("[IO.Compression.ZipFileExtensions]::ExtractToFile($entry,(LongPath $dest)) } } finally { $zip.Dispose() }");
        const FString Encoded = FBase64::Encode(reinterpret_cast<const uint8*>(*Script), Script.Len() * sizeof(TCHAR));
        const FString PowerShell = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")) / TEXT("System32/WindowsPowerShell/v1.0/powershell.exe");
        FProcHandle Process = FPlatformProcess::CreateProc(*PowerShell,
            *(TEXT("-NoLogo -NoProfile -NonInteractive -EncodedCommand ") + Encoded), false, true, true, nullptr, 0, nullptr, nullptr);
        if (!Process.IsValid()) return FPencilFontToolText().Get(TEXT("ExtractStartFailed"));
        ON_SCOPE_EXIT { FPlatformProcess::CloseProc(Process); };
        const double Deadline = FPlatformTime::Seconds() + 120;
        while (FPlatformProcess::IsProcRunning(Process))
        {
            if (Operation.Cancelled.load() || FPlatformTime::Seconds() > Deadline)
            {
                FPlatformProcess::TerminateProc(Process, true);
                FPlatformProcess::WaitForProc(Process);
                return FPencilFontToolText().Get(TEXT("ExtractCancelled"));
            }
            FPlatformProcess::Sleep(0.02f);
        }
        int32 Code = -1;
        if (!FPlatformProcess::GetProcReturnCode(Process, &Code) || Code != 0) return FPencilFontToolText().Get(TEXT("ExtractFailed"));
        return FString();
    }
    FString InstallArchive(const FDownload& Download, const TArray<uint8>& Bytes, const FInstall& Operation)
    {
        if (Operation.Cancelled.load()) return FPencilFontToolText().Get(TEXT("InstallCancelled"));
        if (Bytes.Num() != Download.ArchiveBytes || !Hash(Bytes).Equals(Download.ArchiveSha256, ESearchCase::IgnoreCase))
            return FPencilFontToolText().Get(TEXT("ArchiveVerificationFailed"));
        const FString Destination = Operation.Directory, Root = FPaths::GetPath(Destination);
        if (!SafePath(Destination, Root) || FPaths::DirectoryExists(Destination) || FPaths::FileExists(Destination))
            return FPencilFontToolText().Get(TEXT("DestinationOccupied"));
        const FString Work = Root / (TEXT("staging-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
        const FString Staging = Work / TEXT("files"), Archive = Work / TEXT("worker.zip");
        if (!SafePath(Work, Root) || FPaths::DirectoryExists(Work)) return FPencilFontToolText().Get(TEXT("InvalidDirectory"));
        if (!IFileManager::Get().MakeDirectory(*Work, true)) return FPencilFontToolText().Get(TEXT("CreateStagingFailed"));
        ON_SCOPE_EXIT { DeleteStaging(Work, Root); };
        if (!IFileManager::Get().MakeDirectory(*Staging, true)) return FPencilFontToolText().Get(TEXT("CreateStagingFailed"));
        if (!SafePath(Archive, Root) || !FFileHelper::SaveArrayToFile(Bytes, *Archive)) return FPencilFontToolText().Get(TEXT("SaveArchiveFailed"));
        if (!SafePath(Staging, Root)) return FPencilFontToolText().Get(TEXT("UnsafePath"));
        FString Error = Extract(Archive, Staging, Download.InstalledBytes, Operation);
        if (!Error.IsEmpty()) return Error;
        if (!FPaths::DirectoryExists(Staging / TEXT("_internal")) || !FPaths::DirectoryExists(Staging / TEXT("notices"))
            || !VerifyFiles(Staging, Error)) return Error.IsEmpty() ? FPencilFontToolText().Get(TEXT("MissingDependencies")) : Error;
        TArray<uint8> Identity;
        if (!FFileHelper::LoadFileToArray(Identity, *(Staging / TEXT("identity.json")))) return FPencilFontToolText().Get(TEXT("ReadIdentityFailed"));
        const FString Receipt = FString::Printf(TEXT("{\"archive_sha256\":\"%s\",\"identity_sha256\":\"%s\"}"),
            *Download.ArchiveSha256, *Hash(Identity));
        if (!FFileHelper::SaveStringToFile(Receipt, *(Staging / TEXT("installed.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            return FPencilFontToolText().Get(TEXT("SaveReceiptFailed"));
        FScopeLock Lock(&UseLock);
        if (Operation.Cancelled.load()) return FPencilFontToolText().Get(TEXT("InstallCancelled"));
        if (Workers != 0) return FPencilFontToolText().Get(TEXT("Busy"));
        const auto FileLock = LockToolFiles(Root);
        if (!FileLock) return FPencilFontToolText().Get(TEXT("OtherEditorBusy"));
        if (!SafePath(Destination, Root) || FPaths::DirectoryExists(Destination) || FPaths::FileExists(Destination))
            return FPencilFontToolText().Get(TEXT("DestinationOccupied"));
        if (!IFileManager::Get().Move(*Destination, *Staging, false, true, false, true)) return FPencilFontToolText().Get(TEXT("InstallMoveFailed"));
        return FString();
    }
}

FString DefaultStorageDirectory()
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
    return Plugin ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Saved/Tools")) : FString();
}
FString StorageDirectory()
{
    FString Directory;
    if (GConfig) GConfig->GetString(TEXT("Design2UMG.FontTool"), TEXT("StorageDirectory"), Directory, GEditorPerProjectIni);
    return Directory.IsEmpty() ? DefaultStorageDirectory() : Directory;
}
bool SetStorageDirectory(const FString& Directory, FString& Error)
{
    if (IsBusy()) { Error = FPencilFontToolText().Get(TEXT("BusyLocation")); return false; }
    FString Full = Directory.TrimStartAndEnd();
    FPaths::NormalizeDirectoryName(Full);
    if (Full.IsEmpty() || FPaths::IsRelative(Full) || !FPaths::CollapseRelativeDirectories(Full))
    { Error = FPencilFontToolText().Get(TEXT("InvalidDirectory")); return false; }
    if (Full.Equals(StorageDirectory(), ESearchCase::IgnoreCase)) { Error.Reset(); return true; }
    if (HasInstallation()) { Error = FPencilFontToolText().Get(TEXT("RemoveBeforeLocation")); return false; }
    if (!GConfig) { Error = FPencilFontToolText().Get(TEXT("SaveLocationFailed")); return false; }
    GConfig->SetString(TEXT("Design2UMG.FontTool"), TEXT("StorageDirectory"), *Full, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
    Error.Reset(); return true;
}
FString FDownload::InstallDirectory() const { return ToolRoot() / Version; }

bool ParseDownload(const FString& Json, FDownload& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Object;
    double Archive = 0, InstalledSize = 0;
    FDownload Value;
    if (!DesignJsonUtils::TryParseObject(Json, Object)
        || !Object->TryGetStringField(TEXT("version"), Value.Version) || Value.Version != PinnedVersion
        || !Object->TryGetStringField(TEXT("url"), Value.Url) || Value.Url != DownloadUrl
        || !Object->TryGetStringField(TEXT("archive_sha256"), Value.ArchiveSha256) || !IsHash(Value.ArchiveSha256)
        || !Object->TryGetNumberField(TEXT("archive_bytes"), Archive) || Archive <= 0 || Archive > MAX_int32 || Archive != FMath::FloorToDouble(Archive)
        || !Object->TryGetNumberField(TEXT("installed_bytes"), InstalledSize) || InstalledSize <= 0 || InstalledSize > MAX_int32 || InstalledSize != FMath::FloorToDouble(InstalledSize))
    {
        Error = FPencilFontToolText().Get(TEXT("InvalidManifest")); return false;
    }
    Value.ArchiveBytes = static_cast<int64>(Archive);
    Value.InstalledBytes = static_cast<int64>(InstalledSize);
    Out = MoveTemp(Value); Error.Reset(); return true;
}

bool ReadDownload(FDownload& Out, FString& Error)
{
#if !PLATFORM_WINDOWS
    Error = FPencilFontToolText().Get(TEXT("UnsupportedPlatform")); return false;
#else
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
    FString Json;
    if (!Plugin || !FFileHelper::LoadFileToString(Json, *(Plugin->GetBaseDir() / TEXT("Resources/PencilFontWorkerDownload.json"))))
    { Error = FPencilFontToolText().Get(TEXT("MissingManifest")); return false; }
    return ParseDownload(Json, Out, Error);
#endif
}
bool IsInstalled()
{
    FDownload Download; FString Error;
    return ReadDownload(Download, Error) && Installed(Download);
}
bool HasInstallation()
{
    FDownload Download; FString Error;
    if (!ReadDownload(Download, Error)) return false;
    const FString Directory = Download.InstallDirectory();
    return SafePath(Directory, ToolRoot()) && (FPaths::FileExists(Directory / TEXT("installed.json"))
        || FPaths::FileExists(Directory / TEXT("identity.json")) || FPaths::FileExists(Directory / TEXT("PencilFontWorker.exe")));
}
bool IsBusy()
{
    if (ActiveInstall) ActiveInstall->Poll();
    FScopeLock Lock(&UseLock);
    return Workers > 0 || (ActiveInstall && !ActiveInstall->bComplete);
}
bool AcquireWorker(FString& Worker, FString& Error)
{
    if (ActiveInstall) ActiveInstall->Poll();
    FScopeLock Lock(&UseLock);
    FDownload Download;
    if (!ReadDownload(Download, Error)) return false;
    if (!Installed(Download)) { Error = FPencilFontToolText().Get(TEXT("NotInstalled")); return false; }
    if (ActiveInstall && !ActiveInstall->bComplete) { Error = FPencilFontToolText().Get(TEXT("Installing")); return false; }
    TUniquePtr<FArchive> FileLock;
    if (Workers == 0)
    {
        FileLock = LockToolFiles(ToolRoot());
        if (!FileLock) { Error = FPencilFontToolText().Get(TEXT("OtherEditorBusy")); return false; }
    }
    if (!VerifyFiles(Download.InstallDirectory(), Error)) return false;
    Worker = Download.InstallDirectory() / TEXT("PencilFontWorker.exe");
    if (FileLock) WorkerFileLock = MoveTemp(FileLock);
    ++Workers; return true;
}
void ReleaseWorker() { FScopeLock Lock(&UseLock); check(Workers > 0); if (--Workers == 0) WorkerFileLock.Reset(); }
bool Remove(FString& Error)
{
    if (IsBusy()) { Error = FPencilFontToolText().Get(TEXT("BusyRemove")); return false; }
    FScopeLock Lock(&UseLock);
    FDownload Download;
    if (!ReadDownload(Download, Error)) return false;
    const FString Directory = Download.InstallDirectory(), Root = ToolRoot();
    if (!SafePath(Directory / TEXT("installed.json"), Root) || !SafePath(Directory / TEXT("identity.json"), Root))
    { Error = FPencilFontToolText().Get(TEXT("UnsafePath")); return false; }
    const auto FileLock = LockToolFiles(Root);
    if (!FileLock) { Error = FPencilFontToolText().Get(TEXT("OtherEditorBusy")); return false; }
    TSharedPtr<FJsonObject> Receipt, Identity;
    const TSharedPtr<FJsonObject>* Files = nullptr;
    FString ArchiveHash, IdentityHash, BinaryHash;
    double Protocol = 0;
    if (!ReadObject(Directory / TEXT("installed.json"), Receipt)
        || !Receipt->TryGetStringField(TEXT("archive_sha256"), ArchiveHash) || !ArchiveHash.Equals(Download.ArchiveSha256, ESearchCase::IgnoreCase)
        || !Receipt->TryGetStringField(TEXT("identity_sha256"), IdentityHash) || !IsHash(IdentityHash)
        || !HashFile(Directory / TEXT("identity.json"), IdentityHash)
        || !ReadObject(Directory / TEXT("identity.json"), Identity)
        || !Identity->TryGetNumberField(TEXT("protocol"), Protocol) || Protocol != 1
        || !Identity->TryGetStringField(TEXT("binary_sha256"), BinaryHash) || !IsHash(BinaryHash)
        || !Identity->TryGetObjectField(TEXT("files_sha256"), Files) || !Files || (*Files)->Values.IsEmpty()
        || !(*Files)->HasField(TEXT("PencilFontWorker.exe")))
    { Error = FPencilFontToolText().Get(TEXT("InvalidReceipt")); return false; }
    TArray<FString> Owned;
    TSet<FString> Directories;
    for (const auto& Pair : (*Files)->Values)
    {
        FString Expected;
        const FString RelativePath(*Pair.Key);
        const FString File = Directory / RelativePath;
        if (!SafeRelative(RelativePath) || !SafePath(File, Root) || !Pair.Value->TryGetString(Expected) || !IsHash(Expected)
            || (Pair.Key == TEXT("PencilFontWorker.exe") && !Expected.Equals(BinaryHash, ESearchCase::IgnoreCase))
            || (FPaths::FileExists(File) && !HashFile(File, Expected)) || FPaths::DirectoryExists(File))
        { Error = FPencilFontToolText().Get(TEXT("UnsafeOwnedFile"), {{TEXT("File"), File}}); return false; }
        Owned.Add(File);
        for (FString Parent = FPaths::GetPath(File); Parent != Directory; Parent = FPaths::GetPath(Parent)) Directories.Add(Parent);
    }
    // Validate all owned paths before deletion; extra user files are never enumerated as owned.
    for (const FString& File : Owned)
        if (!SafePath(File, Root) || !IFileManager::Get().Delete(*File, false, false, true))
        { Error = FPencilFontToolText().Get(TEXT("RemoveFailed"), {{TEXT("File"), File}}); return false; }
    // Preserve the receipt bytes until both records are removed so a failed identity deletion can be retried.
    const FString ReceiptFile = Directory / TEXT("installed.json"), IdentityFile = Directory / TEXT("identity.json");
    TArray<uint8> ReceiptBytes;
    if (!FFileHelper::LoadFileToArray(ReceiptBytes, *ReceiptFile))
    { Error = FPencilFontToolText().Get(TEXT("InvalidReceipt")); return false; }
    if (!SafePath(ReceiptFile, Root) || !IFileManager::Get().Delete(*ReceiptFile, false, false, true))
    { Error = FPencilFontToolText().Get(TEXT("RemoveFailed"), {{TEXT("File"), ReceiptFile}}); return false; }
    if (!SafePath(IdentityFile, Root) || !IFileManager::Get().Delete(*IdentityFile, false, false, true))
    {
        Error = FPencilFontToolText().Get(TEXT("RemoveFailed"), {{TEXT("File"), IdentityFile}});
        if (!SafePath(ReceiptFile, Root) || !FFileHelper::SaveArrayToFile(ReceiptBytes, *ReceiptFile))
            Error += TEXT("\n") + FPencilFontToolText().Get(TEXT("SaveReceiptFailed"));
        return false;
    }
    TArray<FString> EmptyDirectories = Directories.Array();
    EmptyDirectories.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });
    EmptyDirectories.Add(Directory);
    for (const FString& Empty : EmptyDirectories)
        if (SafePath(Empty, Root)) IFileManager::Get().DeleteDirectory(*Empty, false, false);
    Error.Reset(); return true;
}
void FInstall::Cancel()
{
    if (Poll()) return;
    { FScopeLock Lock(&UseLock); Cancelled.store(true); }
    if (Request) { Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); Request.Reset(); }
    if (!Work.IsValid()) { bComplete = true; Error = FPencilFontToolText().Get(TEXT("InstallCancelled")); }
}
bool FInstall::Poll()
{
    if (!bComplete && Work.IsValid() && Work.IsReady()) { Error = Work.Get(); bComplete = true; }
    return bComplete;
}
TSharedPtr<FInstall, ESPMode::ThreadSafe> Install(const FDownload& Download)
{
    if (IsBusy()) return nullptr;
    auto Operation = MakeShared<FInstall, ESPMode::ThreadSafe>();
    Operation->Directory = Download.InstallDirectory();
    ActiveInstall = Operation;
    const FString Root = FPaths::GetPath(Operation->Directory);
    if (!SafePath(Operation->Directory, Root) || FPaths::DirectoryExists(Operation->Directory) || FPaths::FileExists(Operation->Directory))
    {
        Operation->Error = FPencilFontToolText().Get(TEXT("DestinationOccupied"));
        Operation->bComplete = true;
        return Operation;
    }
    const auto StartupRequest = FHttpModule::Get().CreateRequest();
    Operation->Request = StartupRequest;
    StartupRequest->SetURL(Download.Url);
    StartupRequest->SetVerb(TEXT("GET"));
    StartupRequest->SetTimeout(180);
    StartupRequest->OnProcessRequestComplete().BindLambda(
        [Operation, Download](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
        {
            Operation->Request.Reset();
            if (Operation->Cancelled.load()) { Operation->Error = FPencilFontToolText().Get(TEXT("InstallCancelled")); Operation->bComplete = true; return; }
            if (!bSuccess || !Response || Response->GetResponseCode() != 200)
            { Operation->Error = FPencilFontToolText().Get(TEXT("DownloadFailed")); Operation->bComplete = true; return; }
            TArray<uint8> Bytes = Response->GetContent();
            Operation->Work = Async(EAsyncExecution::ThreadPool, [Operation, Download, Bytes = MoveTemp(Bytes)]()
            { return InstallArchive(Download, Bytes, *Operation); });
        });
    if (!StartupRequest->ProcessRequest())
    {
        StartupRequest->OnProcessRequestComplete().Unbind();
        Operation->Request.Reset();
        if (!Operation->bComplete)
        {
            Operation->Error = FPencilFontToolText().Get(TEXT("DownloadStartFailed")); Operation->bComplete = true;
        }
    }
    return Operation;
}
void Shutdown()
{
    if (!ActiveInstall) return;
    ActiveInstall->Cancel();
    if (ActiveInstall->Work.IsValid()) ActiveInstall->Work.Wait();
    ActiveInstall.Reset();
}
}
