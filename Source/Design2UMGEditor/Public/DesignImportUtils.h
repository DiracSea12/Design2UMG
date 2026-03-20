// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportTypes.h"

struct FUMGWidgetData;
class UWidgetBlueprint;

/**
 * Shared import utilities used by the editor module and factories.
 */
namespace DesignImportUtils
{
	/** Generate a widget blueprint and return it. */
	UWidgetBlueprint* GenerateBlueprint(const FUMGWidgetData& Data, const FString& OutputPath);

	/** Generate a widget blueprint and render a widget-tree PNG preview. */
	bool GenerateBlueprintAndPng(const FUMGWidgetData& Data, const FString& OutputPath);

	/** Generate and compile a blueprint, then render the final compiled PNG. */
	bool GenerateBlueprintAndCompiledPng(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FString& OutputPngFilePath,
		const FIntPoint& RequestedRenderSize = FIntPoint::ZeroValue,
		FString* OutGeneratedAssetPath = nullptr,
		FIntPoint* OutRenderedSize = nullptr);

	/** Render an existing compiled widget blueprint to a PNG file. */
	bool RenderCompiledBlueprintToPngFile(
		UWidgetBlueprint* WidgetBlueprint,
		const FString& OutputPngFilePath,
		const FIntPoint& RequestedRenderSize = FIntPoint::ZeroValue,
		FIntPoint* OutRenderedSize = nullptr);

	/** Parse edited JSON, resolve images, and generate the blueprint. */
	void GenerateFromEditedJson(const FString& EditedJson, const FString& OutputPath);

	/** Resolve remote image references into local imports. */
	void ResolveImages(FUMGWidgetData& Data);

	/**
	 * Open the preview window.
	 * @return true if the window was opened
	 */
	bool OpenPreviewWindow(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FText& WindowTitle);

	/**
	 * Show a frame picker dialog.
	 * @param OnFrameSelected Callback invoked with the selected frame id
	 */
	using FFrameSelectedCallback = TFunction<void(const FString& FrameId)>;
	void ShowFrameSelector(
		const TArray<FDesignFrameInfo>& Frames,
		const FText& WindowTitle,
		const FText& HintText,
		FFrameSelectedCallback OnFrameSelected);
}
