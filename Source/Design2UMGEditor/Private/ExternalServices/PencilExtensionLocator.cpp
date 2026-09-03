// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PencilExtensionLocator.h"
#include "Design2UMGSettings.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

namespace
{
	constexpr const TCHAR* ExtensionDirPattern = TEXT("highagency.pencildev-*");
	const FString ExtensionDirPrefix(TEXT("highagency.pencildev-"));

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

	FString GetUserHomeDir()
	{
#if PLATFORM_WINDOWS
		return FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
#else
		return FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
#endif
	}
}

namespace PencilExtensionLocator
{
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

	void GetDefaultSearchDirs(TArray<FString>& OutSearchDirs)
	{
		const FString UserHome = GetUserHomeDir();
		if (!UserHome.IsEmpty())
		{
			OutSearchDirs.Add(UserHome / TEXT(".vscode/extensions"));
			OutSearchDirs.Add(UserHome / TEXT(".antigravity/extensions"));
		}
	}

	void CollectExtensionRoots(const TArray<FString>& SearchDirs, TArray<FExtensionRoot>& OutRoots)
	{
		for (const FString& Dir : SearchDirs)
		{
			if (Dir.IsEmpty())
			{
				continue;
			}

			TArray<FString> ExtDirs;
			IFileManager::Get().FindFiles(ExtDirs, *(Dir / ExtensionDirPattern), false, true);

			// Within one search directory, order extensions newest-first so consumers can take
			// the first candidate that actually contains their resource file. Search-directory
			// priority is preserved: an earlier directory always wins over a later one.
			TArray<FExtensionRoot> DirRoots;
			for (const FString& ExtDir : ExtDirs)
			{
				const FString VersionText = ExtDir.RightChop(ExtensionDirPrefix.Len());
				int32 InsertIndex = 0;
				while (InsertIndex < DirRoots.Num()
					&& !IsHigherExtensionVersion(VersionText, DirRoots[InsertIndex].Version))
				{
					++InsertIndex;
				}
				FExtensionRoot Root;
				Root.Directory = Dir / ExtDir;
				Root.Version = VersionText;
				DirRoots.Insert(MoveTemp(Root), InsertIndex);
			}
			OutRoots.Append(MoveTemp(DirRoots));
		}
	}

	void GetExtensionRootCandidates(TArray<FExtensionRoot>& OutCandidates, TArray<FString>& OutSearchedDirs)
	{
		OutCandidates.Reset();
		OutSearchedDirs.Reset();

		const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
		if (Settings && !Settings->PencilExtensionDirectory.IsEmpty())
		{
			const FString OverrideDir = FPaths::ConvertRelativePathToFull(Settings->PencilExtensionDirectory);
			OutSearchedDirs.Add(OverrideDir);
			if (IFileManager::Get().DirectoryExists(*OverrideDir))
			{
				FExtensionRoot Root;
				Root.Directory = OverrideDir;
				const FString BaseName = FPaths::GetBaseFilename(OverrideDir);
				if (BaseName.StartsWith(ExtensionDirPrefix))
				{
					Root.Version = BaseName.RightChop(ExtensionDirPrefix.Len());
				}
				OutCandidates.Add(MoveTemp(Root));
			}
		}

		TArray<FString> SearchDirs;
		GetDefaultSearchDirs(SearchDirs);
		for (const FString& Dir : SearchDirs)
		{
			if (!Dir.IsEmpty())
			{
				OutSearchedDirs.Add(Dir);
			}
		}
		CollectExtensionRoots(SearchDirs, OutCandidates);
	}
}
