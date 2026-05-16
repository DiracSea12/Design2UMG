// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGPrototypeAnimationBuilder.h"
#include "Design2UMGPrototypeNavigationExtensions.h"

#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Components/WidgetSwitcher.h"
#include "MovieScene.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"

namespace UMGPrototypeAnimationBuilder
{
	constexpr TCHAR PrototypeAnimationPrefix[] = TEXT("__d2u_nav_anim_");
	constexpr int32 PrototypeAnimationSampleCount = 5;
	enum class EPrototypeAnimationEasing : uint8
	{
		Linear,
		EaseIn,
		EaseOut,
		EaseInOut,
		EaseInBack,
		EaseOutBack
	};

	enum class EPrototypeAnimationDirection : uint8
	{
		Right,
		Left,
		Up,
		Down
	};

	EPrototypeAnimationEasing ResolvePrototypeAnimationEasing(const FString& EasingType)
	{
		if (EasingType.IsEmpty())
		{
			return EPrototypeAnimationEasing::Linear;
		}

		static const TMap<FString, EPrototypeAnimationEasing> EasingTypeMap = {
			{ TEXT("LINEAR"), EPrototypeAnimationEasing::Linear },
			{ TEXT("EASE_IN"), EPrototypeAnimationEasing::EaseIn },
			{ TEXT("EASE_OUT"), EPrototypeAnimationEasing::EaseOut },
			{ TEXT("EASE_IN_AND_OUT"), EPrototypeAnimationEasing::EaseInOut },
			{ TEXT("GENTLE"), EPrototypeAnimationEasing::EaseInOut },
			{ TEXT("CUSTOM_CUBIC_BEZIER"), EPrototypeAnimationEasing::EaseInOut },
			{ TEXT("CUSTOM_SPRING"), EPrototypeAnimationEasing::EaseInOut },
			{ TEXT("EASE_IN_BACK"), EPrototypeAnimationEasing::EaseInBack },
			{ TEXT("EASE_OUT_BACK"), EPrototypeAnimationEasing::EaseOutBack },
		};

		if (const EPrototypeAnimationEasing* Found = EasingTypeMap.Find(EasingType.ToUpper()))
		{
			return *Found;
		}

		return EPrototypeAnimationEasing::EaseInOut;
	}

	EPrototypeAnimationDirection ResolvePrototypeAnimationDirection(const FString& Direction)
	{
		if (Direction.IsEmpty())
		{
			return EPrototypeAnimationDirection::Right;
		}

		static const TMap<FString, EPrototypeAnimationDirection> DirectionMap = {
			{ TEXT("LEFT"), EPrototypeAnimationDirection::Left },
			{ TEXT("TOP"), EPrototypeAnimationDirection::Up },
			{ TEXT("UP"), EPrototypeAnimationDirection::Up },
			{ TEXT("BOTTOM"), EPrototypeAnimationDirection::Down },
			{ TEXT("DOWN"), EPrototypeAnimationDirection::Down },
			{ TEXT("RIGHT"), EPrototypeAnimationDirection::Right },
		};

		if (const EPrototypeAnimationDirection* Found = DirectionMap.Find(Direction.ToUpper()))
		{
			return *Found;
		}

		return EPrototypeAnimationDirection::Right;
	}

	const FUMGWidgetNode* FindWidgetNodeById(const FUMGWidgetNode& Node, const FString& TargetId)
	{
		if (Node.Id == TargetId)
		{
			return &Node;
		}

		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Found = FindWidgetNodeById(Child, TargetId))
			{
				return Found;
			}
		}
		return nullptr;
	}

	float EvaluatePrototypeAnimationAlpha(float Alpha, const FString& EasingType)
	{
		Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		switch (ResolvePrototypeAnimationEasing(EasingType))
		{
		case EPrototypeAnimationEasing::Linear:
			return Alpha;
		case EPrototypeAnimationEasing::EaseIn:
			return Alpha * Alpha;
		case EPrototypeAnimationEasing::EaseOut:
		{
			const float InvAlpha = 1.0f - Alpha;
			return 1.0f - (InvAlpha * InvAlpha);
		}
		case EPrototypeAnimationEasing::EaseInOut:
			return FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
		case EPrototypeAnimationEasing::EaseInBack:
		{
			constexpr float Overshoot = 1.70158f;
			return Alpha * Alpha * ((Overshoot + 1.0f) * Alpha - Overshoot);
		}
		case EPrototypeAnimationEasing::EaseOutBack:
		{
			constexpr float Overshoot = 1.70158f;
			const float Shifted = Alpha - 1.0f;
			return 1.0f + (Shifted * Shifted * ((Overshoot + 1.0f) * Shifted + Overshoot));
		}
		default:
			return FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
		}
	}

	FVector2D ResolvePrototypeAnimationOffset(const FDesignPixelBounds& Bounds, const FString& Direction)
	{
		const float Width = Bounds.W > KINDA_SMALL_NUMBER ? Bounds.W : 120.0f;
		const float Height = Bounds.H > KINDA_SMALL_NUMBER ? Bounds.H : 120.0f;
		switch (ResolvePrototypeAnimationDirection(Direction))
		{
		case EPrototypeAnimationDirection::Left:
			return FVector2D(-Width, 0.0f);
		case EPrototypeAnimationDirection::Up:
			return FVector2D(0.0f, -Height);
		case EPrototypeAnimationDirection::Down:
			return FVector2D(0.0f, Height);
		case EPrototypeAnimationDirection::Right:
		default:
			return FVector2D(Width, 0.0f);
		}
	}

	FName MakePrototypeAnimationName(const int32 BindingIndex, const TCHAR* Phase)
	{
		return FName(*FString::Printf(TEXT("%s%03d_%s"), PrototypeAnimationPrefix, BindingIndex, Phase));
	}

	FFrameNumber PrototypeSecondsToFrame(const UMovieScene* MovieScene, const float Seconds)
	{
		const FFrameRate TickResolution = MovieScene ? MovieScene->GetTickResolution() : FFrameRate(24000, 1);
		return FFrameNumber(static_cast<int32>(FMath::RoundToInt64(Seconds * TickResolution.AsDecimal())));
	}

	template<typename FCallback>
	void ForEachPrototypeSample(const UMovieScene* MovieScene, float DurationSeconds, const FString& EasingType, FCallback Callback)
	{
		const int32 SampleCount = FMath::Max(2, PrototypeAnimationSampleCount);
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const float Alpha = SampleCount == 1 ? 1.0f : static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
			const float EasedAlpha = EvaluatePrototypeAnimationAlpha(Alpha, EasingType);
			const FFrameNumber Frame = PrototypeSecondsToFrame(MovieScene, DurationSeconds * Alpha);
			Callback(EasedAlpha, Frame);
		}
	}

	void AddPrototypeFloatKeys(
		FMovieSceneFloatChannel& Channel,
		const UMovieScene* MovieScene,
		const float DurationSeconds,
		const float StartValue,
		const float EndValue,
		const FString& EasingType)
	{
		Channel.Reset();
		ForEachPrototypeSample(MovieScene, DurationSeconds, EasingType, [&](float EasedAlpha, FFrameNumber Frame)
		{
			AddKeyToChannel(&Channel, Frame, FMath::Lerp(StartValue, EndValue, EasedAlpha), EMovieSceneKeyInterpolation::Linear);
		});
	}

	void AddPrototypeTransformKeys(
		UMovieScene2DTransformSection* Section,
		const UMovieScene* MovieScene,
		const float DurationSeconds,
		const FVector2D& StartTranslation,
		const FVector2D& EndTranslation,
		const FString& EasingType)
	{
		if (!Section)
		{
			return;
		}

		Section->SetMask(FMovieScene2DTransformMask(EMovieScene2DTransformChannel::Translation));
		Section->Translation[0].Reset();
		Section->Translation[1].Reset();
		ForEachPrototypeSample(MovieScene, DurationSeconds, EasingType, [&](float EasedAlpha, FFrameNumber Frame)
		{
			const FVector2D Value = FMath::Lerp(StartTranslation, EndTranslation, EasedAlpha);
			AddKeyToChannel(&Section->Translation[0], Frame, Value.X, EMovieSceneKeyInterpolation::Linear);
			AddKeyToChannel(&Section->Translation[1], Frame, Value.Y, EMovieSceneKeyInterpolation::Linear);
		});
	}

	UWidgetAnimation* CreatePrototypeTransitionAnimation(
		UWidgetBlueprint* WidgetBlueprint,
		UWidget* AnimatedWidget,
		const FName AnimationName,
		const float DurationSeconds,
		const float StartOpacity,
		const float EndOpacity,
		const FVector2D& StartTranslation,
		const FVector2D& EndTranslation,
		const FString& EasingType)
	{
		if (!WidgetBlueprint || !AnimatedWidget || DurationSeconds <= KINDA_SMALL_NUMBER)
		{
			return nullptr;
		}

		UWidgetAnimation* Animation = NewObject<UWidgetAnimation>(WidgetBlueprint, AnimationName, RF_Transactional);
		if (!Animation)
		{
			return nullptr;
		}

		Animation->SetDisplayLabel(AnimationName.ToString());
		Animation->MovieScene = NewObject<UMovieScene>(Animation, AnimationName, RF_Transactional);
		if (!Animation->MovieScene)
		{
			return nullptr;
		}

		Animation->MovieScene->SetDisplayRate(FFrameRate(60, 1));
		const FFrameNumber EndFrame = PrototypeSecondsToFrame(Animation->MovieScene, DurationSeconds);
		Animation->MovieScene->SetPlaybackRange(TRange<FFrameNumber>(0, EndFrame.Value + 1));
		Animation->MovieScene->GetEditorData().WorkStart = 0.0;
		Animation->MovieScene->GetEditorData().WorkEnd = DurationSeconds;
		Animation->MovieScene->SetViewRange(0.0, DurationSeconds);
		Animation->MovieScene->SetWorkingRange(0.0, DurationSeconds);

		const FGuid BindingGuid = Animation->MovieScene->AddPossessable(AnimatedWidget->GetName(), AnimatedWidget->GetClass());
		FWidgetAnimationBinding AnimationBinding;
		AnimationBinding.AnimationGuid = BindingGuid;
		AnimationBinding.WidgetName = AnimatedWidget->GetFName();
		Animation->AnimationBindings.Add(AnimationBinding);

		if (!FMath::IsNearlyEqual(StartOpacity, EndOpacity, KINDA_SMALL_NUMBER))
		{
			UMovieSceneFloatTrack* OpacityTrack = Animation->MovieScene->AddTrack<UMovieSceneFloatTrack>(BindingGuid);
			OpacityTrack->SetPropertyNameAndPath(TEXT("RenderOpacity"), TEXT("RenderOpacity"));
			OpacityTrack->SetDisplayName(FText::FromString(TEXT("Render Opacity")));
			UMovieSceneFloatSection* OpacitySection = Cast<UMovieSceneFloatSection>(OpacityTrack->CreateNewSection());
			OpacitySection->SetRange(TRange<FFrameNumber>(0, EndFrame.Value + 1));
			AddPrototypeFloatKeys(OpacitySection->GetChannel(), Animation->MovieScene, DurationSeconds, StartOpacity, EndOpacity, EasingType);
			OpacityTrack->AddSection(*OpacitySection);
		}

		if (!StartTranslation.Equals(EndTranslation, KINDA_SMALL_NUMBER))
		{
			UMovieScene2DTransformTrack* TransformTrack = Animation->MovieScene->AddTrack<UMovieScene2DTransformTrack>(BindingGuid);
			TransformTrack->SetPropertyNameAndPath(TEXT("RenderTransform"), TEXT("RenderTransform"));
			TransformTrack->SetDisplayName(FText::FromString(TEXT("Render Transform")));
			UMovieScene2DTransformSection* TransformSection =
				Cast<UMovieScene2DTransformSection>(TransformTrack->CreateNewSection());
			TransformSection->SetRange(TRange<FFrameNumber>(0, EndFrame.Value + 1));
			AddPrototypeTransformKeys(TransformSection, Animation->MovieScene, DurationSeconds, StartTranslation, EndTranslation, EasingType);
			TransformTrack->AddSection(*TransformSection);
		}

		WidgetBlueprint->Animations.Add(Animation);
		return Animation;
	}

	void GenerateNavigationAnimations(
		UWidgetBlueprint* WidgetBlueprint,
		const FUMGWidgetNode& RootNode,
		FUMGPrototypeNavigationData& NavigationData)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree || !NavigationData.IsValid() || !NavigationData.RequiresSwitcher())
		{
			return;
		}

		const FUMGWidgetNode* SwitcherNode = FindWidgetNodeById(RootNode, NavigationData.SwitcherWidgetId);
		UWidget* AnimatedWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName));
		if (!SwitcherNode || !AnimatedWidget)
		{
			return;
		}

		for (int32 BindingIndex = 0; BindingIndex < NavigationData.ClickBindings.Num(); ++BindingIndex)
		{
			FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings[BindingIndex];
			FUMGPrototypeTransitionData& Transition = Binding.Transition;
			Transition.OutgoingAnimationName.Reset();
			Transition.IncomingAnimationName.Reset();
			if (!Transition.IsAnimated())
			{
				continue;
			}

			const float PhaseDuration = FMath::Max(Transition.DurationSeconds * 0.5f, 1.0f / 120.0f);
			const FVector2D SlideOffset = ResolvePrototypeAnimationOffset(SwitcherNode->Bounds, Transition.Direction);

			float OutStartOpacity = 1.0f;
			float OutEndOpacity = 1.0f;
			float InStartOpacity = 1.0f;
			float InEndOpacity = 1.0f;
			FVector2D OutStartTranslation = FVector2D::ZeroVector;
			FVector2D OutEndTranslation = FVector2D::ZeroVector;
			FVector2D InStartTranslation = FVector2D::ZeroVector;
			FVector2D InEndTranslation = FVector2D::ZeroVector;

			if (Transition.UsesFadeOnly())
			{
				OutEndOpacity = 0.0f;
				InStartOpacity = 0.0f;
			}
			else if (Transition.UsesSlide())
			{
				if (Transition.UsesBidirectionalSlide() || Transition.UsesOutgoingSlideOnly())
				{
					OutEndTranslation = -SlideOffset;
				}
				if (Transition.UsesBidirectionalSlide() || Transition.UsesIncomingSlideOnly())
				{
					InStartTranslation = SlideOffset;
				}
			}

			if (!FMath::IsNearlyEqual(OutStartOpacity, OutEndOpacity, KINDA_SMALL_NUMBER)
				|| !OutStartTranslation.Equals(OutEndTranslation, KINDA_SMALL_NUMBER))
			{
				const FName OutgoingName = MakePrototypeAnimationName(BindingIndex, TEXT("out"));
				if (CreatePrototypeTransitionAnimation(
					WidgetBlueprint,
					AnimatedWidget,
					OutgoingName,
					PhaseDuration,
					OutStartOpacity,
					OutEndOpacity,
					OutStartTranslation,
					OutEndTranslation,
					Transition.EasingType))
				{
					Transition.OutgoingAnimationName = OutgoingName.ToString();
				}
			}

			if (!FMath::IsNearlyEqual(InStartOpacity, InEndOpacity, KINDA_SMALL_NUMBER)
				|| !InStartTranslation.Equals(InEndTranslation, KINDA_SMALL_NUMBER))
			{
				const FName IncomingName = MakePrototypeAnimationName(BindingIndex, TEXT("in"));
				if (CreatePrototypeTransitionAnimation(
					WidgetBlueprint,
					AnimatedWidget,
					IncomingName,
					PhaseDuration,
					InStartOpacity,
					InEndOpacity,
					InStartTranslation,
					InEndTranslation,
					Transition.EasingType))
				{
					Transition.IncomingAnimationName = IncomingName.ToString();
				}
			}
		}
	}

	void ConfigureNavigationExtension(UWidgetBlueprint* WidgetBlueprint, const FUMGPrototypeNavigationData& NavigationData)
	{
		if (!WidgetBlueprint)
		{
			return;
		}

		if (NavigationData.IsValid())
		{
			UDesign2UMGPrototypeNavigationBlueprintExtension* Extension =
				UWidgetBlueprintExtension::RequestExtension<UDesign2UMGPrototypeNavigationBlueprintExtension>(WidgetBlueprint);
			Extension->SetNavigationData(NavigationData);
			return;
		}

		if (UDesign2UMGPrototypeNavigationBlueprintExtension* ExistingExtension =
			UWidgetBlueprintExtension::GetExtension<UDesign2UMGPrototypeNavigationBlueprintExtension>(WidgetBlueprint))
		{
			WidgetBlueprint->RemoveExtension(ExistingExtension);
		}
	}

}