// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "DesignImportTypes.h"
#include "Misc/AutomationTest.h"

struct FUMGWidgetData;
class UWidgetBlueprint;

namespace DesignImportUtils
{
#if defined(WITH_AUTOMATION_TESTS) && WITH_AUTOMATION_TESTS
	enum class EAutomationGenerationCancelPoint : uint8
	{
		None,
		BeforeBuild,
		AfterBuildBeforeSave,
	};

	void SetAutomationGenerationCancelPoint(EAutomationGenerationCancelPoint CancelPoint);
	void ResetAutomationGenerationCancelPoint();
#endif

	void ApplyPendingIconFontMappingChanges(const FUMGWidgetData& Data);
	UWidgetBlueprint* GenerateBlueprint(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr);
	bool GenerateBlueprintAndPng(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		FString* OutGeneratedAssetPath = nullptr,
		FString* OutRenderPngPath = nullptr,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr);
	bool GenerateBlueprintAndCompiledPng(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FString& OutputPngFilePath,
		FString* OutGeneratedAssetPath = nullptr,
		FIntPoint* OutRenderedSize = nullptr,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr);
	bool RenderCompiledBlueprintToPngFile(
		UWidgetBlueprint* WidgetBlueprint,
		const FString& OutputPngFilePath,
		FIntPoint* OutRenderedSize = nullptr);
}

