// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

// Single source for prototype transition math shared by UMGPrototypeAnimationBuilder
// (generation-time animation key sampling) and the prototype navigation user-widget
// extension (runtime transition ticking). Easing and direction string parsing, the
// easing curves, and the silent GENTLE / CUSTOM_CUBIC_BEZIER / CUSTOM_SPRING ->
// EaseInOut mapping must stay identical on both sides; change them here only.
namespace PrototypeTransitionMath
{
	enum class EEasing : uint8
	{
		Linear,
		EaseIn,
		EaseOut,
		EaseInOut,
		EaseInBack,
		EaseOutBack
	};

	enum class EDirection : uint8
	{
		Right,
		Left,
		Up,
		Down
	};

	inline EEasing ResolveEasing(const FString& EasingType)
	{
		if (EasingType.IsEmpty())
		{
			return EEasing::Linear;
		}

		static const TMap<FString, EEasing> EasingTypeMap = {
			{ TEXT("LINEAR"), EEasing::Linear },
			{ TEXT("EASE_IN"), EEasing::EaseIn },
			{ TEXT("EASE_OUT"), EEasing::EaseOut },
			{ TEXT("EASE_IN_AND_OUT"), EEasing::EaseInOut },
			{ TEXT("GENTLE"), EEasing::EaseInOut },
			{ TEXT("CUSTOM_CUBIC_BEZIER"), EEasing::EaseInOut },
			{ TEXT("CUSTOM_SPRING"), EEasing::EaseInOut },
			{ TEXT("EASE_IN_BACK"), EEasing::EaseInBack },
			{ TEXT("EASE_OUT_BACK"), EEasing::EaseOutBack },
		};

		if (const EEasing* Found = EasingTypeMap.Find(EasingType.ToUpper()))
		{
			return *Found;
		}

		return EEasing::EaseInOut;
	}

	inline float EvaluateAlpha(float Alpha, const FString& EasingType)
	{
		Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		switch (ResolveEasing(EasingType))
		{
		case EEasing::Linear:
			return Alpha;
		case EEasing::EaseIn:
			return Alpha * Alpha;
		case EEasing::EaseOut:
		{
			const float InvAlpha = 1.0f - Alpha;
			return 1.0f - (InvAlpha * InvAlpha);
		}
		case EEasing::EaseInBack:
		{
			constexpr float Overshoot = 1.70158f;
			return Alpha * Alpha * ((Overshoot + 1.0f) * Alpha - Overshoot);
		}
		case EEasing::EaseOutBack:
		{
			constexpr float Overshoot = 1.70158f;
			const float Shifted = Alpha - 1.0f;
			return 1.0f + (Shifted * Shifted * ((Overshoot + 1.0f) * Shifted + Overshoot));
		}
		case EEasing::EaseInOut:
		default:
			return FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
		}
	}

	inline EDirection ResolveDirection(const FString& Direction)
	{
		if (Direction.IsEmpty())
		{
			return EDirection::Right;
		}

		static const TMap<FString, EDirection> DirectionMap = {
			{ TEXT("LEFT"), EDirection::Left },
			{ TEXT("TOP"), EDirection::Up },
			{ TEXT("UP"), EDirection::Up },
			{ TEXT("BOTTOM"), EDirection::Down },
			{ TEXT("DOWN"), EDirection::Down },
			{ TEXT("RIGHT"), EDirection::Right },
		};

		if (const EDirection* Found = DirectionMap.Find(Direction.ToUpper()))
		{
			return *Found;
		}

		return EDirection::Right;
	}
}
