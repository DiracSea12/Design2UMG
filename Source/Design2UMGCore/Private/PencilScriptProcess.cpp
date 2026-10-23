// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#include "PencilScriptProcess.h"
#include "PencilScriptPolicy.h"
#include "Interfaces/IPluginManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace PencilScriptProcess
{
namespace
{
	bool ReadJSON(const FString& Text, TSharedPtr<FJsonObject>& Out)
	{
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
	}
	bool Flag(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, bool Expected)
	{
		bool Value;
		return Object->TryGetBoolField(Key, Value) && Value == Expected;
	}
	bool Code(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Expected)
	{
		double Value;
		return Object->TryGetNumberField(Key, Value) && Value == Expected;
	}
#if PLATFORM_WINDOWS
	bool VerifyBinary(const FString& File, const FString& Expected)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *File)) return false;
		BCRYPT_ALG_HANDLE Algorithm = nullptr;
		if (BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
		uint8 Digest[32];
		const NTSTATUS Status = BCryptHash(Algorithm, nullptr, 0, Bytes.GetData(), Bytes.Num(), Digest, sizeof(Digest));
		BCryptCloseAlgorithmProvider(Algorithm, 0);
		return Status >= 0 && BytesToHex(Digest, sizeof(Digest)).Equals(Expected, ESearchCase::IgnoreCase);
	}
#endif
}

bool Evaluate(const FString& Program, FString& OutJSON, FString& Error)
{
	OutJSON.Reset(); Error.Reset();
#if !PLATFORM_WINDOWS
	Error = TEXT("Pencil script execution is currently implemented for Windows only."); return false;
#else
	FTCHARToUTF8 ProgramBytes(*Program);
	if (ProgramBytes.Length() > PencilScriptPolicy::MaxRequestUtf8Bytes)
	{
		Error = FString::Printf(TEXT("Pencil script request exceeds %d MiB."),
			PencilScriptPolicy::MaxRequestUtf8Bytes / (1024 * 1024));
		return false;
	}

	const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
	if (!Plugin.IsValid()) { Error = TEXT("Design2UMG plugin location is unavailable."); return false; }
	const FString WorkerDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/PencilScriptWorker/Win64"));
	const FString OriginalExe = FPaths::Combine(WorkerDir, TEXT("PencilScriptWorker.exe"));
	FString IdentityText, Hash;
	TSharedPtr<FJsonObject> Identity;
	if (!FFileHelper::LoadFileToString(IdentityText, *FPaths::Combine(WorkerDir, TEXT("identity.json")))
		|| !ReadJSON(IdentityText, Identity) || !Code(Identity, TEXT("protocol"), 1)
		|| !Identity->TryGetStringField(TEXT("binary_sha256"), Hash) || !VerifyBinary(OriginalExe, Hash))
	{
		Error = TEXT("Bundled Pencil script worker identity verification failed."); return false;
	}
	// Each invocation owns its executable ACL and temp files; installed plugin files stay untouched.
	const FString TempDir = FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("Design2UMG-Script-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (!IFileManager::Get().MakeDirectory(*TempDir, true)) { Error = TEXT("Script temporary directory creation failed."); return false; }
	struct FTempScope
	{
		FString Directory;
		~FTempScope()
		{
			IFileManager::Get().Delete(*FPaths::Combine(Directory, TEXT("request.js")), false, true);
			IFileManager::Get().Delete(*FPaths::Combine(Directory, TEXT("PencilScriptWorker.exe")), false, true);
			IFileManager::Get().DeleteDirectory(*Directory, false, false);
		}
	} Temp{TempDir};
	const FString Exe = FPaths::Combine(TempDir, TEXT("PencilScriptWorker.exe"));
	const FString Request = FPaths::Combine(TempDir, TEXT("request.js"));
	if (IFileManager::Get().Copy(*Exe, *OriginalExe) != COPY_OK
		|| !FFileHelper::SaveStringToFile(Program, *Request, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Error = TEXT("Script temporary input preparation failed."); return false;
	}
	void* ReadPipe = nullptr; void* WritePipe = nullptr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe)) { Error = TEXT("Script helper output pipe creation failed."); return false; }
	struct FPipeScope { void* Read; void* Write; ~FPipeScope() { FPlatformProcess::ClosePipe(Read, Write); } } Pipe{ReadPipe, WritePipe};
	FProcHandle Process = FPlatformProcess::CreateProc(*Exe, *FString::Printf(TEXT("\"%s\""), *Request), false, true, true, nullptr, 0, *TempDir, WritePipe);
	if (!Process.IsValid()) { Error = TEXT("Script helper launch failed."); return false; }
	const double Deadline = FPlatformTime::Seconds() + PencilScriptPolicy::EditorWaitSeconds;
	FString Reply;
	bool TimedOut = false, Overflow = false;
	while (FPlatformProcess::IsProcRunning(Process))
	{
		Reply += FPlatformProcess::ReadPipe(ReadPipe);
		Overflow = Reply.Len() > PencilScriptPolicy::MaxEditorReplyCharacters;
		TimedOut = FPlatformTime::Seconds() >= Deadline;
		if (Overflow || TimedOut) { FPlatformProcess::TerminateProc(Process, true); break; }
		FPlatformProcess::Sleep(0.005f);
	}
	FPlatformProcess::WaitForProc(Process);
	Reply += FPlatformProcess::ReadPipe(ReadPipe);
	int32 ExitCode = -1;
	FPlatformProcess::GetProcReturnCode(Process, &ExitCode); FPlatformProcess::CloseProc(Process);
	if (TimedOut || Overflow || ExitCode != 0) { Error = TEXT("Pencil script helper failed or exceeded its process/output budget."); return false; }
	TSharedPtr<FJsonObject> Host, Worker;
	FString WorkerText;
	if (!ReadJSON(Reply, Host) || !Code(Host, TEXT("protocol"), 1) || !Flag(Host, TEXT("contained"), true)
		|| !Flag(Host, TEXT("lpac_policy"), true) || !Code(Host, TEXT("profile_create_hr"), 0)
		|| !Code(Host, TEXT("profile_delete_hr"), 0) || !Code(Host, TEXT("image_acl_restore_error"), 0)
		|| !Flag(Host, TEXT("hard_timeout"), false) || !Flag(Host, TEXT("output_overflow"), false)
		|| !Flag(Host, TEXT("input_complete"), true) || !Code(Host, TEXT("worker_exit"), 0)
		|| !Host->TryGetStringField(TEXT("worker_stdout"), WorkerText) || !ReadJSON(WorkerText, Worker)
		|| !Code(Worker, TEXT("protocol"), 1) || !Flag(Worker, TEXT("isolated"), true)
		|| !Flag(Worker, TEXT("runtime_finalized"), true))
	{
		Error = TEXT("Pencil script process isolation/lifecycle verification failed."); return false;
	}
	if (!Code(Worker, TEXT("run_code"), 0) || !Code(Worker, TEXT("result_code"), 0))
	{
		FString Exception; Worker->TryGetStringField(TEXT("exception"), Exception);
		Error = TEXT("Pencil script evaluation failed: ") + Exception; return false;
	}
	if (!Worker->TryGetStringField(TEXT("result_json"), OutJSON)) { Error = TEXT("Pencil script returned no JSON result."); return false; }
	return true;
#endif
}
}
