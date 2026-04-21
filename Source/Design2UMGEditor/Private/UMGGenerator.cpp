// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGGenerator.h"
#include "PreBuildResourceNormalizer.h"
#include "UMGWidgetNodeAccess.h"
#include "UObject/SavePackage.h"
#include "Design2UMGFigmaMetadataExtensions.h"
#include "Design2UMGPrototypeNavigationExtensions.h"
#include "UMGWidgetUtils.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "ObjectTools.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"

#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/WidgetAnimation.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "MovieScene.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectHash.h"

namespace
{
	constexpr TCHAR PrototypeAnimationPrefix[] = TEXT("__d2u_nav_anim_");
	constexpr int32 PrototypeAnimationSampleCount = 5;

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
		if (EasingType.IsEmpty() || EasingType.Equals(TEXT("LINEAR"), ESearchCase::IgnoreCase))
		{
			return Alpha;
		}
		if (EasingType.Equals(TEXT("EASE_IN"), ESearchCase::IgnoreCase))
		{
			return Alpha * Alpha;
		}
		if (EasingType.Equals(TEXT("EASE_OUT"), ESearchCase::IgnoreCase))
		{
			const float InvAlpha = 1.0f - Alpha;
			return 1.0f - (InvAlpha * InvAlpha);
		}
		if (EasingType.Equals(TEXT("EASE_IN_AND_OUT"), ESearchCase::IgnoreCase)
			|| EasingType.Equals(TEXT("GENTLE"), ESearchCase::IgnoreCase)
			|| EasingType.Equals(TEXT("CUSTOM_CUBIC_BEZIER"), ESearchCase::IgnoreCase)
			|| EasingType.Equals(TEXT("CUSTOM_SPRING"), ESearchCase::IgnoreCase))
		{
			return FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
		}
		if (EasingType.Equals(TEXT("EASE_IN_BACK"), ESearchCase::IgnoreCase))
		{
			constexpr float Overshoot = 1.70158f;
			return Alpha * Alpha * ((Overshoot + 1.0f) * Alpha - Overshoot);
		}
		if (EasingType.Equals(TEXT("EASE_OUT_BACK"), ESearchCase::IgnoreCase))
		{
			constexpr float Overshoot = 1.70158f;
			const float Shifted = Alpha - 1.0f;
			return 1.0f + (Shifted * Shifted * ((Overshoot + 1.0f) * Shifted + Overshoot));
		}
		return FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
	}

	FVector2D ResolvePrototypeAnimationOffset(const FBounds& Bounds, const FString& Direction)
	{
		const float Width = Bounds.W > KINDA_SMALL_NUMBER ? Bounds.W : 120.0f;
		const float Height = Bounds.H > KINDA_SMALL_NUMBER ? Bounds.H : 120.0f;
		if (Direction.Equals(TEXT("LEFT"), ESearchCase::IgnoreCase))
		{
			return FVector2D(-Width, 0.0f);
		}
		if (Direction.Equals(TEXT("TOP"), ESearchCase::IgnoreCase)
			|| Direction.Equals(TEXT("UP"), ESearchCase::IgnoreCase))
		{
			return FVector2D(0.0f, -Height);
		}
		if (Direction.Equals(TEXT("BOTTOM"), ESearchCase::IgnoreCase)
			|| Direction.Equals(TEXT("DOWN"), ESearchCase::IgnoreCase))
		{
			return FVector2D(0.0f, Height);
		}
		return FVector2D(Width, 0.0f);
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

	void AddPrototypeFloatKeys(
		FMovieSceneFloatChannel& Channel,
		const UMovieScene* MovieScene,
		const float DurationSeconds,
		const float StartValue,
		const float EndValue,
		const FString& EasingType)
	{
		Channel.Reset();
		const int32 SampleCount = FMath::Max(2, PrototypeAnimationSampleCount);
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const float Alpha = SampleCount == 1 ? 1.0f : static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
			const float EasedAlpha = EvaluatePrototypeAnimationAlpha(Alpha, EasingType);
			const float Value = FMath::Lerp(StartValue, EndValue, EasedAlpha);
			const FFrameNumber Frame = PrototypeSecondsToFrame(MovieScene, DurationSeconds * Alpha);
			AddKeyToChannel(&Channel, Frame, Value, EMovieSceneKeyInterpolation::Linear);
		}
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
		const int32 SampleCount = FMath::Max(2, PrototypeAnimationSampleCount);
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const float Alpha = SampleCount == 1 ? 1.0f : static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
			const float EasedAlpha = EvaluatePrototypeAnimationAlpha(Alpha, EasingType);
			const FVector2D Value = FMath::Lerp(StartTranslation, EndTranslation, EasedAlpha);
			const FFrameNumber Frame = PrototypeSecondsToFrame(MovieScene, DurationSeconds * Alpha);
			AddKeyToChannel(&Section->Translation[0], Frame, Value.X, EMovieSceneKeyInterpolation::Linear);
			AddKeyToChannel(&Section->Translation[1], Frame, Value.Y, EMovieSceneKeyInterpolation::Linear);
		}
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

	void GeneratePrototypeNavigationAnimations(
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

	void ConfigurePrototypeNavigationExtension(UWidgetBlueprint* WidgetBlueprint, const FUMGPrototypeNavigationData& NavigationData)
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

	void ConfigureFigmaMetadataExtension(
		UWidgetBlueprint* WidgetBlueprint,
		const FUMGSourceDocumentData& DocumentData,
		const TArray<FDesign2UMGFigmaWidgetMetadataRecord>& WidgetRecords)
	{
		if (!WidgetBlueprint)
		{
			return;
		}

		if (DocumentData.HasAnyComponentCatalog() || WidgetRecords.Num() > 0)
		{
			UDesign2UMGFigmaMetadataBlueprintExtension* Extension =
				UWidgetBlueprintExtension::RequestExtension<UDesign2UMGFigmaMetadataBlueprintExtension>(WidgetBlueprint);
			Extension->SetMetadata(DocumentData, WidgetRecords);
			return;
		}

		if (UDesign2UMGFigmaMetadataBlueprintExtension* ExistingExtension =
			UWidgetBlueprintExtension::GetExtension<UDesign2UMGFigmaMetadataBlueprintExtension>(WidgetBlueprint))
		{
			WidgetBlueprint->RemoveExtension(ExistingExtension);
		}
	}

	void DumpTemplateTextBlockFontState(const TCHAR* Stage, UWidgetBlueprint* WidgetBlueprint)
	{
		if (!UE_LOG_ACTIVE(LogDesign2UMG, Verbose) || !WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return;
		}

		WidgetBlueprint->WidgetTree->ForEachWidget([Stage, WidgetBlueprint](UWidget* Widget)
		{
			UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
			if (!TextBlock)
			{
				return;
			}

			const FSlateFontInfo FontInfo = TextBlock->GetFont();
			const UObject* FontObject = FontInfo.FontObject;
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("TextFont[%s]: blueprint='%s' widget='%s' text='%s' fontObject='%s' typeface='%s' size=%.1f tracking=%d wrap=%.1f autoWrap=%d"),
				Stage,
				*WidgetBlueprint->GetPathName(),
				*TextBlock->GetName(),
				*TextBlock->GetText().ToString(),
				FontObject ? *FontObject->GetPathName() : TEXT("<null>"),
				*FontInfo.TypefaceFontName.ToString(),
				FontInfo.Size,
				FontInfo.LetterSpacing,
				TextBlock->GetWrapTextAt(),
				TextBlock->GetAutoWrapText() ? 1 : 0);
		});
	}

	void ClearGeneratedWidgetNavigationState(UWidgetBlueprint* WidgetBlueprint)
	{
		if (!WidgetBlueprint)
		{
			return;
		}

		int32 ClearedWidgetCount = 0;
		TSet<UWidgetNavigation*> SanitizedNavigationObjects;
		auto ResetNavigationObject = [&SanitizedNavigationObjects](UWidgetNavigation* Navigation)
		{
			if (!Navigation || SanitizedNavigationObjects.Contains(Navigation))
			{
				return;
			}

			Navigation->Up = FWidgetNavigationData();
			Navigation->Down = FWidgetNavigationData();
			Navigation->Left = FWidgetNavigationData();
			Navigation->Right = FWidgetNavigationData();
			Navigation->Next = FWidgetNavigationData();
			Navigation->Previous = FWidgetNavigationData();
			Navigation->RoutingPolicy = EWidgetNavigationRoutingPolicy::Default;
			Navigation->NavigationMethod.Reset();
			SanitizedNavigationObjects.Add(Navigation);
		};

		auto ClearNavigation = [&ClearedWidgetCount, &ResetNavigationObject](UWidget* Widget)
		{
			if (!Widget || !Widget->Navigation)
			{
				return;
			}

			// Clear stale directional navigation payloads so UMG compile/validation
			// cannot resurrect old NavigationMethod state from previous template trees.
			ResetNavigationObject(Widget->Navigation);
			Widget->Navigation = nullptr;
			++ClearedWidgetCount;
		};

		auto ClearGeneratedClassNavigation = [&ClearNavigation](UClass* ClassObject)
		{
			if (UUserWidget* GeneratedCDO = ClassObject ? Cast<UUserWidget>(ClassObject->GetDefaultObject()) : nullptr)
			{
				ClearNavigation(GeneratedCDO);
				if (GeneratedCDO->WidgetTree)
				{
					GeneratedCDO->WidgetTree->ForEachWidget(ClearNavigation);
				}
			}
		};

		ClearGeneratedClassNavigation(WidgetBlueprint->GeneratedClass);
		ClearGeneratedClassNavigation(WidgetBlueprint->SkeletonGeneratedClass);

		if (WidgetBlueprint->WidgetTree)
		{
			WidgetBlueprint->WidgetTree->ForEachWidget(ClearNavigation);
		}

		if (UPackage* Package = WidgetBlueprint->GetOutermost())
		{
			ForEachObjectWithPackage(Package, [&ClearNavigation, &ResetNavigationObject](UObject* Object)
			{
				if (UWidget* Widget = Cast<UWidget>(Object))
				{
					ClearNavigation(Widget);
					return true;
				}

				if (UWidgetNavigation* Navigation = Cast<UWidgetNavigation>(Object))
				{
					ResetNavigationObject(Navigation);
				}

				return true;
			}, true, RF_Transient, EInternalObjectFlags::Garbage);
		}

		if (ClearedWidgetCount > 0 || SanitizedNavigationObjects.Num() > 0)
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Cleared stale widget navigation refs on %d widgets and sanitized %d navigation objects before compile"),
				ClearedWidgetCount,
				SanitizedNavigationObjects.Num());
		}
	}

	void ApplyDesignPreviewSize(UWidgetBlueprint* WBP, const FIntPoint& DesignSize, const TCHAR* Phase)
	{
		if (!WBP)
		{
			return;
		}

		UClass* GeneratedClass = WBP->GeneratedClass;
		if (!GeneratedClass)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ApplyDesignPreviewSize(%s): GeneratedClass is null"), Phase ? Phase : TEXT("unknown"));
			return;
		}

		UUserWidget* CDO = Cast<UUserWidget>(GeneratedClass->GetDefaultObject());
		if (!CDO)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ApplyDesignPreviewSize(%s): Failed to get UUserWidget CDO"), Phase ? Phase : TEXT("unknown"));
			return;
		}

		CDO->DesignTimeSize = FVector2D(DesignSize.X, DesignSize.Y);
		CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
		UE_LOG(LogDesign2UMG, Log, TEXT("Set DesignTimeSize to %.0fx%.0f (mode=Custom, phase=%s)"),
			CDO->DesignTimeSize.X, CDO->DesignTimeSize.Y, Phase ? Phase : TEXT("unknown"));
	}

	FIntPoint ReadDesignPreviewSize(UWidgetBlueprint* WBP)
	{
		if (!WBP || !WBP->GeneratedClass)
		{
			return FIntPoint::ZeroValue;
		}

		if (UUserWidget* CDO = Cast<UUserWidget>(WBP->GeneratedClass->GetDefaultObject()))
		{
			const FVector2D CurrentSize = CDO->DesignTimeSize;
			if (CurrentSize.X > 1.0f && CurrentSize.Y > 1.0f)
			{
				return FIntPoint(
					FMath::RoundToInt(CurrentSize.X),
					FMath::RoundToInt(CurrentSize.Y));
			}
		}

		return FIntPoint::ZeroValue;
	}

	FUMGSlotData MakeCanvasFillSlot()
	{
		FUMGSlotData Slot;
		Slot.Anchors.Min.X = 0.0f;
		Slot.Anchors.Min.Y = 0.0f;
		Slot.Anchors.Max.X = 1.0f;
		Slot.Anchors.Max.Y = 1.0f;
		Slot.Alignment.X = 0.0f;
		Slot.Alignment.Y = 0.0f;
		Slot.Offsets.Left = 0.0f;
		Slot.Offsets.Top = 0.0f;
		Slot.Offsets.Right = 0.0f;
		Slot.Offsets.Bottom = 0.0f;
		Slot.bAutoSize = false;
		return Slot;
	}

	FUMGWidgetNode WrapRootForViewport(
		FUMGWidgetNode&& InRoot,
		const FIntPoint& ViewportSize,
		const FIntPoint& ContentSize,
		bool bDownOnly)
	{
		FUMGWidgetNode SizeBoxNode;
		SizeBoxNode.Type = TEXT("SizeBox");
		SizeBoxNode.Id = TEXT("__d2u_root_sizebox");
		SizeBoxNode.Name = TEXT("D2U_RootSizeBox");
		SizeBoxNode.Bounds.X = 0.0f;
		SizeBoxNode.Bounds.Y = 0.0f;
		SizeBoxNode.Bounds.W = static_cast<float>(ContentSize.X);
		SizeBoxNode.Bounds.H = static_cast<float>(ContentSize.Y);
		SizeBoxNode.WidgetStyle.WidthOverride = ContentSize.X;
		SizeBoxNode.WidgetStyle.HeightOverride = ContentSize.Y;
		SizeBoxNode.Children.Add(MoveTemp(InRoot));

		FUMGWidgetNode ScaleBoxNode;
		ScaleBoxNode.Type = TEXT("ScaleBox");
		ScaleBoxNode.Id = TEXT("__d2u_root_scalebox");
		ScaleBoxNode.Name = TEXT("D2U_RootScaleBox");
		ScaleBoxNode.Bounds.X = 0.0f;
		ScaleBoxNode.Bounds.Y = 0.0f;
		ScaleBoxNode.Bounds.W = static_cast<float>(ViewportSize.X);
		ScaleBoxNode.Bounds.H = static_cast<float>(ViewportSize.Y);
		ScaleBoxNode.Slot = MakeCanvasFillSlot();
		ScaleBoxNode.WidgetStyle.Stretch = TEXT("ScaleToFit");
		ScaleBoxNode.WidgetStyle.StretchDirection = bDownOnly ? TEXT("DownOnly") : TEXT("Both");
		ScaleBoxNode.Children.Add(MoveTemp(SizeBoxNode));

		FUMGWidgetNode CanvasRoot;
		CanvasRoot.Type = TEXT("CanvasPanel");
		CanvasRoot.Id = TEXT("__d2u_viewport_root");
		CanvasRoot.Name = TEXT("D2U_ViewportRoot");
		CanvasRoot.Bounds.X = 0.0f;
		CanvasRoot.Bounds.Y = 0.0f;
		CanvasRoot.Bounds.W = static_cast<float>(ViewportSize.X);
		CanvasRoot.Bounds.H = static_cast<float>(ViewportSize.Y);
		CanvasRoot.Children.Add(MoveTemp(ScaleBoxNode));
		return CanvasRoot;
	}

}

UWidgetBlueprint* FUMGGenerator::GenerateWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath)
{
	UWidgetBlueprint* WBP = BuildWidgetBlueprint(WidgetData, PackagePath);
	if (WBP)
		SaveWidgetBlueprint(WBP);
	return WBP;
}

UWidgetBlueprint* FUMGGenerator::BuildWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath)
{
	FString AssetName = ObjectTools::SanitizeObjectName(WidgetData.WidgetName);
	FString FullPath = PackagePath / AssetName;

	// Check for asset name collisions.
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	FString TestPath = FullPath + TEXT(".") + AssetName;
	if (AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid())
	{
		int32 Suffix = 1;
		FString NewName;
		constexpr int32 MaxAttempts = 1000;

		do {
			NewName = FString::Printf(TEXT("%s_%d"), *AssetName, Suffix);
			TestPath = PackagePath / NewName + TEXT(".") + NewName;
			++Suffix;
		} while (AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid() && Suffix <= MaxAttempts);

		if (Suffix > MaxAttempts && AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid())
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to find unique name for asset '%s'"), *WidgetData.WidgetName);
			UE_LOG(LogDesign2UMG, Error, TEXT("Failed to generate a unique asset name for '%s'. Clean the output directory and retry."), *WidgetData.WidgetName);
			return nullptr;
		}

		AssetName = NewName;
		FullPath = PackagePath / AssetName;
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Asset renamed to %s"), *AssetName);
		UE_LOG(LogDesign2UMG, Warning, TEXT("Output path already contained an asset with this name; auto-renamed to %s."), *AssetName);
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to create package at %s"), *FullPath);
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create the asset package. Check that the output path is valid: %s"), *FullPath);
		return nullptr;
	}

	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(
		FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(), Package, FName(*AssetName),
			BPTYPE_Normal, UWidgetBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(), NAME_None
		)
	);

	if (!WBP || !WBP->WidgetTree)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to create Widget Blueprint"));
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create the Widget Blueprint. Check engine-version compatibility."));
		Package->ClearDirtyFlag();
		Package->SetFlags(RF_Transient);
		return nullptr;
	}

	FUMGPrototypeNavigationData NavigationData = WidgetData.PrototypeNavigation();

	// v2.2: apply the fixer to a writable copy so bounds can be corrected in place.
	FUMGWidgetNode FixedRoot = WidgetData.RootWidget;
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	const FIntPoint ConfigDesignSize = Settings->GetDesignSize();
	const int32 RootWidth = FMath::CeilToInt(FixedRoot.Bounds.W);
	const int32 RootHeight = FMath::CeilToInt(FixedRoot.Bounds.H);
	const bool bHasValidRootSize = RootWidth >= 2 && RootHeight >= 2;
	// Fidelity-first: drive Fixer/LayoutCalc from the design root's real pixel size
	// to avoid systematic drift from user-configured preview scaling.
	FIntPoint DesignSize = bHasValidRootSize
		? FIntPoint(RootWidth, RootHeight)
		: ConfigDesignSize;
	if (bHasValidRootSize && (DesignSize != ConfigDesignSize))
	{
		UE_LOG(LogDesign2UMG, Log,
			TEXT("Using root design size %dx%d (config %dx%d, root bounds %.0fx%.0f)"),
			DesignSize.X, DesignSize.Y,
			ConfigDesignSize.X, ConfigDesignSize.Y,
			FixedRoot.Bounds.W, FixedRoot.Bounds.H);
	}
	else if (!bHasValidRootSize)
	{
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("Root bounds invalid (%.2fx%.2f), fallback to config design size %dx%d"),
			FixedRoot.Bounds.W, FixedRoot.Bounds.H,
			ConfigDesignSize.X, ConfigDesignSize.Y);
	}
	if (Settings->bEnableLayoutFixer)
	{
		FUMGWidgetUtils::ApplyFixer(FixedRoot, DesignSize);
	}

	const bool bOversizedRoot =
		(FixedRoot.Bounds.W > static_cast<float>(ConfigDesignSize.X)) ||
		(FixedRoot.Bounds.H > static_cast<float>(ConfigDesignSize.Y));
	if (Settings->bAutoWrapOversizedRootForViewport && bOversizedRoot)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("Wrapping oversized root for viewport fit: root=%.0fx%.0f config=%dx%d stretch=%s"),
			FixedRoot.Bounds.W, FixedRoot.Bounds.H,
			ConfigDesignSize.X, ConfigDesignSize.Y,
			Settings->bViewportScaleDownOnly ? TEXT("DownOnly") : TEXT("Both"));
		FixedRoot = WrapRootForViewport(
			MoveTemp(FixedRoot),
			ConfigDesignSize,
			DesignSize,
			Settings->bViewportScaleDownOnly);
	}

	FPreBuildResourceNormalizer::NormalizePathTextures(FixedRoot, PackagePath / TEXT("D2U_PathTextures"), AssetName);
	FPreBuildResourceNormalizer::NormalizeProceduralTextures(FixedRoot, PackagePath / TEXT("D2U_ProceduralTextures"), AssetName);

	// Dump widget tree only in verbose mode.
	if (UE_LOG_ACTIVE(LogDesign2UMG, Verbose))
	{
		TFunction<void(const FUMGWidgetNode&, int32)> DumpNode = [&](const FUMGWidgetNode& Node, int32 Depth)
		{
			FString Indent;
			for (int32 i = 0; i < Depth; ++i) Indent += TEXT("  ");

			FString SlotInfo = FString::Printf(TEXT("slot[h=%s v=%s sz=%s fw=%.1f off=(%.0f,%.0f,%.0f,%.0f) anc=(%.2f,%.2f,%.2f,%.2f) aln=(%.2f,%.2f) auto=%d]"),
				*Node.Slot.HorizontalAlignment, *Node.Slot.VerticalAlignment,
				*Node.Slot.SizeRule, Node.Slot.FillWeight,
				Node.Slot.Offsets.Left, Node.Slot.Offsets.Top, Node.Slot.Offsets.Right, Node.Slot.Offsets.Bottom,
				Node.Slot.Anchors.Min.X, Node.Slot.Anchors.Min.Y, Node.Slot.Anchors.Max.X, Node.Slot.Anchors.Max.Y,
				Node.Slot.Alignment.X, Node.Slot.Alignment.Y,
				Node.Slot.bAutoSize ? 1 : 0);

			FString PropsInfo;
			const FUMGPropertyBag TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
			TypedProperties.ForEachSerialized([&PropsInfo](const FString& Key, const FString& Value)
			{
				if (Key == TEXT("PathGeometry") || Key == TEXT("MaterialParams")) return;
				PropsInfo += FString::Printf(TEXT(" %s=%s"), *Key, *Value.Left(40));
			});

			UE_LOG(LogDesign2UMG, Verbose, TEXT("TREE: %s[%s] '%s' bounds=(%.1f,%.1f,%.1f,%.1f) %s%s"),
				*Indent, *Node.Type, *Node.Name,
				Node.Bounds.X, Node.Bounds.Y, Node.Bounds.W, Node.Bounds.H,
				*SlotInfo, *PropsInfo);

			for (const auto& Child : Node.Children)
				DumpNode(Child, Depth + 1);
		};
		UE_LOG(LogDesign2UMG, Verbose, TEXT("=== Widget Tree Dump (after Fixer) ==="));
		DumpNode(FixedRoot, 0);
		UE_LOG(LogDesign2UMG, Verbose, TEXT("=== End Widget Tree Dump ==="));
	}

	// Use the shared widget creation path.
	TArray<FDesign2UMGFigmaWidgetMetadataRecord> FigmaMetadataRecords;
	UWidget* Root = FUMGWidgetUtils::CreateWidgetRecursive(WBP->WidgetTree, FixedRoot, nullptr, nullptr, &FigmaMetadataRecords, &DesignSize);
	if (!Root)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to create root widget for '%s'"), *WidgetData.WidgetName);
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create root widget '%s'. The design may contain unsupported node types."), *WidgetData.WidgetName);
		Package->ClearDirtyFlag();
		Package->SetFlags(RF_Transient);
		return nullptr;
	}
	WBP->WidgetTree->RootWidget = Root;
	GeneratePrototypeNavigationAnimations(WBP, FixedRoot, NavigationData);
	ConfigurePrototypeNavigationExtension(WBP, NavigationData);
	ConfigureFigmaMetadataExtension(WBP, WidgetData.SourceDocument(), FigmaMetadataRecords);
	ClearGeneratedWidgetNavigationState(WBP);
	DumpTemplateTextBlockFontState(TEXT("template-precompile"), WBP);

	// Push the editor preview size into the CDO. SaveWidgetBlueprint writes it back
	// after compilation so the compile step cannot reset the default value.
	ApplyDesignPreviewSize(WBP, DesignSize, TEXT("pre-compile"));

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(WBP);

	UE_LOG(LogDesign2UMG, Log, TEXT("Built Widget Blueprint at %s (not yet compiled/saved)"), *FullPath);
	return WBP;
}

void FUMGGenerator::SaveWidgetBlueprint(UWidgetBlueprint* WBP)
{
	if (!WBP) return;

	FIntPoint DesiredDesignSize = ReadDesignPreviewSize(WBP);
	if (DesiredDesignSize.X <= 1 || DesiredDesignSize.Y <= 1)
	{
		DesiredDesignSize = UDesign2UMGSettings::Get()->GetDesignSize();
	}

	// Compile after rendering; transient texture data may already be gone at this point.
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

	// MarkBlueprintAsStructurallyModified can populate the GUID map during an
	// intermediate compile. Clear it so CompileBlueprint rebuilds from the widget tree
	// instead of carrying stale entries forward.
	WBP->WidgetVariableNameToGuidMap.Empty();
	ClearGeneratedWidgetNavigationState(WBP);
	const EBlueprintCompileOptions CompileOptions =
		EBlueprintCompileOptions::SkipDefaultObjectValidation |
		EBlueprintCompileOptions::SkipFiBSearchMetaUpdate;
	FKismetEditorUtilities::CompileBlueprint(WBP, CompileOptions);
	DumpTemplateTextBlockFontState(TEXT("template-postcompile"), WBP);
	ApplyDesignPreviewSize(WBP, DesiredDesignSize, TEXT("post-compile"));

	UPackage* Package = WBP->GetOutermost();
	FString FullPath = Package->GetName();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString FileName = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, WBP, *FileName, SaveArgs);

	UE_LOG(LogDesign2UMG, Log, TEXT("Saved Widget Blueprint at %s"), *FullPath);
}

