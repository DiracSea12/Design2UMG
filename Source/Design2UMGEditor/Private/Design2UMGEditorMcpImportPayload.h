// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace Design2UMGEditorMcpImportPayload
{
	/** Extracts the marked document JSON printed by Pencil execute. */
	bool ParseReadDocumentResponse(const FString& Response, FString& OutNodesJson, FString& OutVarsJson);

	/** Routes Pencil nodes and variables into the frame-selection import flow. */
	void HandleMCPDataReady(const FString& NodesJson, const FString& VarsJson);
}
