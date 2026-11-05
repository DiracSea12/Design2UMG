// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Slate/WidgetTransform.h"

class UWidgetBlueprint;
class UWidgetAnimation;
class UWidget;
struct FMovieSceneFloatChannel;
struct FUMGWidgetNode;
struct FUMGPrototypeNavigationData;
struct FUMGPrototypeTransitionData;

/**
 * Engine-standard animation tracks shared by source transitions and editor presets.
 * Generated assets need no plugin UObject classes.
 */
namespace UMGPrototypeAnimationBuilder
{
	using FChannelWriter = TFunctionRef<void(FMovieSceneFloatChannel&, float, float)>;

	UWidgetAnimation* CreateAnimation(UWidgetBlueprint* Blueprint, FName Name, float DurationSeconds);
	FGuid BindWidget(UWidgetAnimation* Animation, UWidget* Widget);
	void AddTransformTrack(UWidgetAnimation* Animation, const FGuid& Binding,
		const FWidgetTransform& Start, const FWidgetTransform& End, FChannelWriter WriteChannel);
	void AddFloatTrack(UWidgetAnimation* Animation, const FGuid& Binding, FName PropertyName,
		const FString& PropertyPath, float Start, float End, FChannelWriter WriteChannel);
	void AddOpacityTrack(UWidgetAnimation* Animation, const FGuid& Binding,
		float Start, float End, FChannelWriter WriteChannel);
	void AddColorTrack(UWidgetAnimation* Animation, const FGuid& Binding, FName PropertyName,
		const FString& PropertyPath, const FLinearColor& Start, const FLinearColor& End, FChannelWriter WriteChannel);
	/** Drop an owned animation without leaving a stale generated variable GUID. */
	void RemoveAnimation(UWidgetBlueprint* Blueprint, UWidgetAnimation* Animation);

	/** The same ownership rule used when replacing generated navigation animations. */
	bool IsGeneratedNavigationAnimation(const UWidgetAnimation* Animation);
	/** Resolve native paint producers after shader preparation has moved source placement. */
	void ResolvePreparedPlacements(UWidgetBlueprint* Blueprint, const TMap<UWidget*, UWidget*>& Placements,
		FUMGPrototypeNavigationData* NavigationData);
	/**
	 * Replace generated transition animations and record their names in navigation IR.
	 * Bind source identities through the completed native tree, keeping both real pages in one clip.
	 * Run before SaveWidgetBlueprint refreshes the skeleton used by graph wiring.
	 */
	void GenerateNavigationAnimations(
		UWidgetBlueprint* WidgetBlueprint,
		const FUMGWidgetNode& RootNode,
		FUMGPrototypeNavigationData& NavigationData);
}
