// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;
class UWidgetAnimation;

namespace Design2UMGAnimationPresets
{
	enum class EPreset : uint8
	{
		ScreenFade,
		ScreenSlideLeft,
		ScreenSlideRight,
		ScreenSlideTop,
		ScreenSlideBottom,
		ScreenScalePop,
		ButtonScale,
		ButtonLift,
		ButtonFade,
		ButtonScaleAndLift,
		ButtonPop
	};

	struct FSettings
	{
		explicit FSettings(EPreset InPreset = EPreset::ScreenFade);
		EPreset Preset = EPreset::ScreenFade;
		FName TargetWidgetName;
		FString TargetNodeId;
		float DurationSeconds = 0.25f;
		float Distance = 32.0f;
		float Scale = 1.05f;
		float Opacity = 0;
		float PressScale = 0.95f;
		float PressDistance = 2.0f;
		float PressOpacity = 0.55f;
		float Overshoot = 0.05f;
		float HoverPlaybackSpeed = 1.0f;
		float PressPlaybackSpeed = 2.0f;
	};

	/** Names refer to real UWidgetAnimation subobjects on the selected Blueprint. */
	struct FBindingPlan
	{
		FName TargetWidgetName;
		FName EntranceAnimationName;
		FName FeedbackAnimationName;
		float IdleTime = 0;
		float HoverTime = 0;
		float PressTime = 0;
		bool bTransform = false;
		bool bOpacity = false;
		float HoverPlaybackSpeed = 1.0f;
		float PressPlaybackSpeed = 2.0f;
	};

	bool IsScreenPreset(EPreset Preset);
	bool IsOwnedAnimation(const UWidgetAnimation* Animation);
	FText GetPresetLabel(EPreset Preset);
	bool BuildAnimations(UWidgetBlueprint* Blueprint, const FSettings& Settings,
		FBindingPlan& OutPlan, FString& OutError);
	bool Apply(UWidgetBlueprint* Blueprint, const FSettings& Settings, TArray<FString>& OutWarnings);
	void Restore(UWidgetBlueprint* Blueprint, TArray<FString>& OutWarnings);
	/** Current scoped IR identities, supplied by the completed native tree before source reimport wiring. */
	void RecordSourceTargets(UWidgetBlueprint* Blueprint, const TMap<FName, FString>& TargetIds);
	void OpenDialog();
}

namespace Design2UMGPrototypeGraphWiring
{
	bool ValidateAnimationPresetBindings(UWidgetBlueprint* Blueprint,
		const Design2UMGAnimationPresets::FBindingPlan& Plan, FString& OutError);
	bool WireAnimationPresetBindings(UWidgetBlueprint* Blueprint,
		const Design2UMGAnimationPresets::FBindingPlan& Plan, TArray<FString>& OutWarnings);
}
