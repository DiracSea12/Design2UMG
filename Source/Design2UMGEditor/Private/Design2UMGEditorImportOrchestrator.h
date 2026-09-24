// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportDefaults.h"

namespace Design2UMGEditorImportOrchestrator
{
	struct FPenConvertRequest
	{
		FString NodesPath;
		FString VarsPath;
		FString TargetNodeId;
		FString OutputPath = DesignImportDefaults::PenOutputPath;
		bool bUsePreview = true;
	};

	struct FPenImportRequest
	{
		FString FilePath;
		FString TargetNodeId;
		FString OutputPath = DesignImportDefaults::PenOutputPath;
		bool bUsePreview = true;
		bool bRenderPng = true;
	};

	struct FRegisteredSourceImportRequest
	{
		FName SourceId;
		FString JsonPath;
		FString OutputPath = DesignImportDefaults::PenOutputPath;
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
	void RunDesignReimport(const FString& TargetAssetPath, const FString& JsonFilePath, bool bGenerateResolutionCopy);
	void ConnectToPencilAndImport();
	void ShutdownTransientState();
}
