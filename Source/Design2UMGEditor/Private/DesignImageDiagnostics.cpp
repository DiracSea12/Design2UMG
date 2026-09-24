// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImageDiagnostics.h"

namespace
{
	FString BuildFailureCategorySummary(const TArray<FDesignImageDiagnostic>& Diagnostics)
	{
		TMap<EDesignImageDiagnosticCategory, int32> Counts;
		for (const FDesignImageDiagnostic& Diagnostic : Diagnostics)
		{
			if (Diagnostic.Outcome == EDesignImageDiagnosticOutcome::Failed)
			{
				++Counts.FindOrAdd(Diagnostic.Category);
			}
		}

		TArray<FString> Parts;
		for (const EDesignImageDiagnosticCategory Category : {
			EDesignImageDiagnosticCategory::Configuration,
			EDesignImageDiagnosticCategory::Budget,
			EDesignImageDiagnosticCategory::Validation,
			EDesignImageDiagnosticCategory::FileRead,
			EDesignImageDiagnosticCategory::Download,
			EDesignImageDiagnosticCategory::Decode,
			EDesignImageDiagnosticCategory::TextureImport,
			EDesignImageDiagnosticCategory::TextureRefresh,
			EDesignImageDiagnosticCategory::UnresolvedReference,
			EDesignImageDiagnosticCategory::UnsupportedPath,
			EDesignImageDiagnosticCategory::Disabled,
			EDesignImageDiagnosticCategory::None })
		{
			if (const int32* Count = Counts.Find(Category))
			{
				Parts.Add(FString::Printf(TEXT("%s=%d"), LexToString(Category), *Count));
			}
		}
		return FString::Join(Parts, TEXT(" "));
	}
}

const TCHAR* LexToString(const EDesignImageDiagnosticOutcome Outcome)
{
	switch (Outcome)
	{
	case EDesignImageDiagnosticOutcome::Imported: return TEXT("imported");
	case EDesignImageDiagnosticOutcome::Reused: return TEXT("reused");
	case EDesignImageDiagnosticOutcome::Refreshed: return TEXT("refreshed");
	case EDesignImageDiagnosticOutcome::Skipped: return TEXT("skipped");
	case EDesignImageDiagnosticOutcome::Failed: return TEXT("failed");
	default: return TEXT("skipped");
	}
}

const TCHAR* LexToString(const EDesignImageDiagnosticCategory Category)
{
	switch (Category)
	{
	case EDesignImageDiagnosticCategory::Configuration: return TEXT("configuration");
	case EDesignImageDiagnosticCategory::Budget: return TEXT("budget");
	case EDesignImageDiagnosticCategory::Validation: return TEXT("validation");
	case EDesignImageDiagnosticCategory::FileRead: return TEXT("file_read");
	case EDesignImageDiagnosticCategory::Download: return TEXT("download");
	case EDesignImageDiagnosticCategory::Decode: return TEXT("decode");
	case EDesignImageDiagnosticCategory::TextureImport: return TEXT("texture_import");
	case EDesignImageDiagnosticCategory::TextureRefresh: return TEXT("texture_refresh");
	case EDesignImageDiagnosticCategory::UnsupportedPath: return TEXT("unsupported_path");
	case EDesignImageDiagnosticCategory::Disabled: return TEXT("disabled");
	case EDesignImageDiagnosticCategory::UnresolvedReference: return TEXT("unresolved_reference");
	case EDesignImageDiagnosticCategory::None: return TEXT("none");
	default: return TEXT("none");
	}
}

void FDesignImageImportResult::AddDiagnostic(FDesignImageDiagnostic Diagnostic)
{
	switch (Diagnostic.Outcome)
	{
	case EDesignImageDiagnosticOutcome::Imported: ++ImportedCount; break;
	case EDesignImageDiagnosticOutcome::Reused: ++ReusedCount; break;
	case EDesignImageDiagnosticOutcome::Refreshed: ++RefreshedCount; break;
	case EDesignImageDiagnosticOutcome::Skipped: ++SkippedCount; break;
	case EDesignImageDiagnosticOutcome::Failed: ++FailedCount; break;
	default: break;
	}
	Diagnostics.Add(MoveTemp(Diagnostic));
}

void FDesignImageImportResult::RefreshDisplaySummary(const int32 MaxDisplayedDetails)
{
	DisplaySummary = FString::Printf(
		TEXT("Remote image import: imported=%d reused=%d refreshed=%d skipped=%d failed=%d path=%s"),
		ImportedCount,
		ReusedCount,
		RefreshedCount,
		SkippedCount,
		FailedCount,
		*ImportPath);

	if (FailedCount > 0)
	{
		DisplaySummary += FString::Printf(
			TEXT("\nFailure categories: %s"),
			*BuildFailureCategorySummary(Diagnostics));
	}

	const int32 DisplayedCount = FMath::Clamp(MaxDisplayedDetails, 0, Diagnostics.Num());
	for (int32 Index = 0; Index < DisplayedCount; ++Index)
	{
		if (!Diagnostics[Index].Message.IsEmpty())
		{
			DisplaySummary += TEXT("\n");
			DisplaySummary += Diagnostics[Index].Message;
		}
	}

	const int32 OmittedCount = Diagnostics.Num() - DisplayedCount;
	if (OmittedCount > 0)
	{
		DisplaySummary += FString::Printf(TEXT("\n%d image diagnostic detail(s) omitted"), OmittedCount);
	}
}
