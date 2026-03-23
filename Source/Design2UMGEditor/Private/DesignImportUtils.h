#pragma once

#include "CoreMinimal.h"
#include "DesignImportTypes.h"

struct FUMGWidgetData;
class UWidgetBlueprint;

namespace DesignImportUtils
{
	void ApplyPendingIconFontMappingChanges(const FUMGWidgetData& Data);
	UWidgetBlueprint* GenerateBlueprint(const FUMGWidgetData& Data, const FString& OutputPath);
	bool GenerateBlueprintAndPng(const FUMGWidgetData& Data, const FString& OutputPath);
	bool GenerateBlueprintAndCompiledPng(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FString& OutputPngFilePath,
		const FIntPoint& RequestedRenderSize = FIntPoint::ZeroValue,
		FString* OutGeneratedAssetPath = nullptr,
		FIntPoint* OutRenderedSize = nullptr);
	bool RenderCompiledBlueprintToPngFile(
		UWidgetBlueprint* WidgetBlueprint,
		const FString& OutputPngFilePath,
		const FIntPoint& RequestedRenderSize = FIntPoint::ZeroValue,
		FIntPoint* OutRenderedSize = nullptr);
}

