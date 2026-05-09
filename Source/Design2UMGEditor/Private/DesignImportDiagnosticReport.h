// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;
struct FUMGWidgetData;

namespace DesignImportDiagnosticReport
{
	void Write(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FString& ImageReport,
		const FString& RequestedOutputPath,
		const FString& RequestedRenderPngPath,
		const FString& GeneratedAssetPath,
		const FString& RenderPngPath);
}
