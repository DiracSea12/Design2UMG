// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
#include "UMGSchema.h"

class UUserWidget;
class UWidgetTree;
class FDesign2UMGComponentMappingBuildState;
class UWidgetBlueprint;
class UTexture2D;
struct FDesign2UMGGenerationOutput;
struct FDesign2UMGGenerationSettingsSnapshot;
struct FUMGPrototypeNavigationData;

/** Import-scoped component templates and native UMG instance-slot overrides. No runtime plugin type. */
class FDesign2UMGComponentBlueprintContext
{
public:
	struct FComponent
	{
		FUMGWidgetData Data;
		FString PackageFolder;
		FString ClassPath;
	};
	TMap<FString, FComponent> Components;
	TMap<FString, FString> NodeIdToKey;
	FString DocumentScope;
	FString Error;

	FString ResolveKey(const FUMGWidgetNode& Node) const;
	bool ApplyInstance(UUserWidget* Instance, UWidgetTree* OwnerTree, const FUMGWidgetNode& Node,
		const FDesign2UMGGenerationSettingsSnapshot& Settings, const TMap<FString, UTexture2D*>* Textures,
		const FIntPoint* DesignSize,
		FDesign2UMGComponentMappingBuildState* Ownership = nullptr);

	static UWidgetBlueprint* Build(const FUMGWidgetData& Data, const FString& Folder,
		const FDesign2UMGGenerationSettingsSnapshot* Settings, FDesign2UMGGenerationOutput* Output,
		FUMGPrototypeNavigationData* Navigation, UWidgetBlueprint* TargetBlueprint = nullptr);
};
