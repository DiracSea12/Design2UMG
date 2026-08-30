// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace Design2UMGEditorMcpImportPayload
{
	/** Parses the Pencil MCP batch_get/get_variables payloads and routes them into the frame-selection import flow. */
	void HandleMCPDataReady(const FString& NodesJson, const FString& VarsJson);
}
