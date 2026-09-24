// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

enum class EDesignImageDiagnosticOutcome : uint8
{
	Imported,
	Reused,
	Refreshed,
	Skipped,
	Failed
};

enum class EDesignImageDiagnosticCategory : uint8
{
	None,
	Configuration,
	Budget,
	Validation,
	FileRead,
	Download,
	Decode,
	TextureImport,
	TextureRefresh,
	UnsupportedPath,
	Disabled,
	UnresolvedReference
};

/** One image-resolution event. Structured fields are authoritative; Message is display text. */
struct DESIGN2UMGEDITOR_API FDesignImageDiagnostic
{
	EDesignImageDiagnosticOutcome Outcome = EDesignImageDiagnosticOutcome::Skipped;
	EDesignImageDiagnosticCategory Category = EDesignImageDiagnosticCategory::None;
	FString NodeId;
	FString NodeName;
	FString Resource;
	/** Asset path retained in the node after processing Resource, including partial failure. */
	FString ResolvedAssetPath;
	FString Reason;
	FString Message;
};

/** Image-resolution facts retained across passes until their source is retried. */
struct DESIGN2UMGEDITOR_API FDesignImageImportResult
{
	/** Distinguishes an executed empty pass from analysis that was never requested. */
	bool bWasExecuted = false;
	FString ImportPath;
	int32 ImportedCount = 0;
	int32 ReusedCount = 0;
	int32 RefreshedCount = 0;
	int32 SkippedCount = 0;
	int32 FailedCount = 0;
	TArray<FDesignImageDiagnostic> Diagnostics;
	FString DisplaySummary;

	bool HasFailures() const { return FailedCount > 0; }
	bool HasDiagnostics() const { return Diagnostics.Num() > 0; }

	void AddDiagnostic(FDesignImageDiagnostic Diagnostic);
	void RefreshDisplaySummary(int32 MaxDisplayedDetails = 32);
};

DESIGN2UMGEDITOR_API const TCHAR* LexToString(EDesignImageDiagnosticOutcome Outcome);
DESIGN2UMGEDITOR_API const TCHAR* LexToString(EDesignImageDiagnosticCategory Category);
