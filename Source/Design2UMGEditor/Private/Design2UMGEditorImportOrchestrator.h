// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace Design2UMGEditorImportOrchestrator
{
	inline constexpr TCHAR DefaultPenOutputPath[] = TEXT("/Game/Design2UMG/PenConvert_Output");
	inline constexpr TCHAR DefaultFigmaOutputPath[] = TEXT("/Game/Design2UMG/FigmaConvert_Output");

	struct FPenConvertRequest
	{
		FString NodesPath;
		FString VarsPath;
		FString TargetNodeId;
		FString OutputPath = DefaultPenOutputPath;
		bool bUsePreview = true;
	};

	struct FPenImportRequest
	{
		FString FilePath;
		FString TargetNodeId;
		FString OutputPath = DefaultPenOutputPath;
		bool bUsePreview = true;
		bool bRenderPng = true;
	};

	struct FRegisteredSourceImportRequest
	{
		FName SourceId;
		FString JsonPath;
		FString OutputPath = DefaultPenOutputPath;
		bool bUsePreview = true;
	};

	struct FLocalFigmaImportRequest
	{
		FString JsonFilePath;
		FString OutputPath;
		bool bUsePreview = true;
	};

	void RunPenConvert(const FPenConvertRequest& Request);
	void RunPenImport(const FPenImportRequest& Request);
	void RunRegisteredSourceImport(const FRegisteredSourceImportRequest& Request);
	void RunFigmaImport(const FString& FileUrl, const FString& Token);
	void RunLocalFigmaImport(const FLocalFigmaImportRequest& Request);
	void ConnectToPencilAndImport();
	void ShutdownTransientState();
}
