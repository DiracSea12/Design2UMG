// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace Design2UMGEditorMcpImportPayload
{
	/** Read an optional native path for relative resources; untitled documents have none. */
	bool ParseActiveDocumentPath(const FString& Response, FString& OutFilePath);

	/** Extract the nodes and current variables/themes printed by Pencil execute. */
	bool ParseReadDocumentResponse(const FString& Response, FString& OutNodesJson, FString& OutVarsJson);

	/** Select and import a root from this in-memory snapshot. */
	void HandleMCPDataReady(const FString& NodesJson, const FString& VarsJson, const FString& SourceFilePath);
}
