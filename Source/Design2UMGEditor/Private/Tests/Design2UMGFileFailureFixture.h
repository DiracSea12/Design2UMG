// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTLS.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Editor.h"
#include "Tests/Design2UMGTestTransBuffer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace Design2UMGEditorTests
{
	class FScopedTransactionBuffer
	{
	public:
		TStrongObjectPtr<UDesign2UMGTestTransBuffer> Buffer{NewObject<UDesign2UMGTestTransBuffer>()};
		FScopedTransactionBuffer() : Previous(GEditor->Trans.Get())
		{
			check(!GEditor->IsTransactionActive());
			Buffer->Initialize(32 * 1024 * 1024);
			GEditor->Trans = Buffer.Get();
		}
		~FScopedTransactionBuffer() { GEditor->Trans = Previous.Get(); }
	private:
		TStrongObjectPtr<UTransactor> Previous;
	};

	inline bool IsPackageSaveTargetFile(const FString& Path)
	{
		return Path.StartsWith(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), ESearchCase::IgnoreCase)
			&& (Path.EndsWith(FPackageName::GetAssetPackageExtension(), ESearchCase::IgnoreCase)
				|| Path.EndsWith(FPackageName::GetMapPackageExtension(), ESearchCase::IgnoreCase));
	}

	// Reject one matching package through SavePackage's recoverable validation boundary.
	class FScopedPackageSaveFailure final
	{
		const uint32 TestThread = FPlatformTLS::GetCurrentThreadId();
		FCoreUObjectDelegates::FIsPackageOKToSaveDelegate PreviousSaveValidation;
		TFunction<bool(const FString&)> RejectSave;
		bool bRejected = false;

	public:
		explicit FScopedPackageSaveFailure(TFunction<bool(const FString&)> InRejectSave)
			: PreviousSaveValidation(MoveTemp(FCoreUObjectDelegates::IsPackageOKToSaveDelegate))
			, RejectSave(MoveTemp(InRejectSave))
		{
			FCoreUObjectDelegates::IsPackageOKToSaveDelegate.BindLambda(
				[this](UPackage* Package, const FString& Filename, FOutputDevice* Error)
				{
					const FString FullFilename = FPaths::ConvertRelativePathToFull(Filename);
					if (FPlatformTLS::GetCurrentThreadId() == TestThread && !bRejected
						&& RejectSave && RejectSave(FullFilename))
					{
						bRejected = true;
						return false;
					}
					return !PreviousSaveValidation.IsBound()
						|| PreviousSaveValidation.Execute(Package, Filename, Error);
				});
		}

		~FScopedPackageSaveFailure()
		{
			FCoreUObjectDelegates::IsPackageOKToSaveDelegate = MoveTemp(PreviousSaveValidation);
		}

		FScopedPackageSaveFailure(const FScopedPackageSaveFailure&) = delete;
		FScopedPackageSaveFailure& operator=(const FScopedPackageSaveFailure&) = delete;

		void AllowSaves()
		{
			RejectSave = nullptr;
		}
	};

	// Replace the existing OS boundary, not the plugin's save/rollback decisions.
	// Faults and observations apply only to the test thread, not background IO.
	// All non-rejected operations go to the previous platform file. No handles
	// retain this scoped wrapper after a test ends.
	class FScopedFileFailures final : public IPlatformFile
	{
		IPlatformFile* LowerLevel = nullptr;
		const uint32 TestThread = FPlatformTLS::GetCurrentThreadId();
	public:
		TFunction<bool(const FString&)> RejectWrite;
		TFunction<bool(const FString&, const FString&)> RejectMove;
		TFunction<void(const FString&)> OnRead;

		FScopedFileFailures()
		{
			SetLowerLevel(&FPlatformFileManager::Get().GetPlatformFile());
			FPlatformFileManager::Get().SetPlatformFile(*this);
		}
		~FScopedFileFailures() override
		{
			FPlatformFileManager::Get().SetPlatformFile(*GetLowerLevel());
		}
		FScopedFileFailures(const FScopedFileFailures&) = delete;
		FScopedFileFailures& operator=(const FScopedFileFailures&) = delete;
		bool Initialize(IPlatformFile* Inner, const TCHAR*) override { LowerLevel = Inner; return Inner != nullptr; }
		IPlatformFile* GetLowerLevel() override { return LowerLevel; }
		void SetLowerLevel(IPlatformFile* Inner) override { LowerLevel = Inner; }
		const TCHAR* GetName() const override { return TEXT("Design2UMGTestFile"); }
		bool FileExists(const TCHAR* Path) override { return LowerLevel->FileExists(Path); }
		int64 FileSize(const TCHAR* Path) override { return LowerLevel->FileSize(Path); }
		bool DeleteFile(const TCHAR* Path) override { return LowerLevel->DeleteFile(Path); }
		bool IsReadOnly(const TCHAR* Path) override { return LowerLevel->IsReadOnly(Path); }
		bool SetReadOnly(const TCHAR* Path, bool Value) override { return LowerLevel->SetReadOnly(Path, Value); }
		FDateTime GetTimeStamp(const TCHAR* Path) override { return LowerLevel->GetTimeStamp(Path); }
		void SetTimeStamp(const TCHAR* Path, FDateTime Time) override { LowerLevel->SetTimeStamp(Path, Time); }
		FDateTime GetAccessTimeStamp(const TCHAR* Path) override { return LowerLevel->GetAccessTimeStamp(Path); }
		FString GetFilenameOnDisk(const TCHAR* Path) override { return LowerLevel->GetFilenameOnDisk(Path); }
		bool DirectoryExists(const TCHAR* Path) override { return LowerLevel->DirectoryExists(Path); }
		bool CreateDirectory(const TCHAR* Path) override { return LowerLevel->CreateDirectory(Path); }
		bool DeleteDirectory(const TCHAR* Path) override { return LowerLevel->DeleteDirectory(Path); }
		FFileStatData GetStatData(const TCHAR* Path) override { return LowerLevel->GetStatData(Path); }
		bool IterateDirectory(const TCHAR* Path, FDirectoryVisitor& Visitor) override { return LowerLevel->IterateDirectory(Path, Visitor); }
		bool IterateDirectoryStat(const TCHAR* Path, FDirectoryStatVisitor& Visitor) override { return LowerLevel->IterateDirectoryStat(Path, Visitor); }
		IAsyncReadFileHandle* OpenAsyncRead(const TCHAR* Path, bool AllowWrite = false) override { return LowerLevel->OpenAsyncRead(Path, AllowWrite); }

		IFileHandle* OpenRead(const TCHAR* Filename, bool bAllowWrite = false) override
		{
			if (FPlatformTLS::GetCurrentThreadId() == TestThread && OnRead) OnRead(FPaths::ConvertRelativePathToFull(Filename));
			return GetLowerLevel()->OpenRead(Filename, bAllowWrite);
		}
		IFileHandle* OpenWrite(const TCHAR* Filename, bool bAppend = false, bool bAllowRead = false) override
		{
			if (FPlatformTLS::GetCurrentThreadId() == TestThread && RejectWrite && RejectWrite(FPaths::ConvertRelativePathToFull(Filename))) return nullptr;
			return GetLowerLevel()->OpenWrite(Filename, bAppend, bAllowRead);
		}
		bool MoveFile(const TCHAR* To, const TCHAR* From) override
		{
			if (FPlatformTLS::GetCurrentThreadId() == TestThread && RejectMove
				&& RejectMove(FPaths::ConvertRelativePathToFull(To), FPaths::ConvertRelativePathToFull(From))) return false;
			return GetLowerLevel()->MoveFile(To, From);
		}
	};
}
