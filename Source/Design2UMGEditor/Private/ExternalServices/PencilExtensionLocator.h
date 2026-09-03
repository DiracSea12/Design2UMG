// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Shared discovery of installed Pencil VS Code extension roots
 * ("$HOME/.vscode/extensions/highagency.pencildev-<version>" and the
 * ".antigravity/extensions" equivalent). Consumers resolve their own resource
 * files (for example the icon codepoint index) relative to the returned roots.
 */
namespace PencilExtensionLocator
{
	struct FExtensionRoot
	{
		/** Absolute path of the extension folder. */
		FString Directory;

		/** Version text parsed from the folder suffix (for example "0.6.70"). */
		FString Version;
	};

	/** Numeric version-priority comparison for extension folder suffixes; an empty current-best accepts anything. */
	bool IsHigherExtensionVersion(const FString& CandidateVersion, const FString& CurrentBestVersion);

	/** Default extension search directories in priority order ($HOME/.vscode/extensions, $HOME/.antigravity/extensions). */
	void GetDefaultSearchDirs(TArray<FString>& OutSearchDirs);

	/** Newest-first extension roots inside each search directory, preserving search-directory order. */
	void CollectExtensionRoots(const TArray<FString>& SearchDirs, TArray<FExtensionRoot>& OutRoots);

	/**
	 * Full candidate list for extension resource discovery: the settings override directory
	 * (UDesign2UMGSettings::PencilExtensionDirectory) first when set and present, then the
	 * auto-discovered roots. OutSearchedDirs lists every directory that was considered.
	 */
	void GetExtensionRootCandidates(TArray<FExtensionRoot>& OutCandidates, TArray<FString>& OutSearchedDirs);
}
