// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeNavigationExtensions.h"

#include "UMGWidgetUtils.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Layout/Geometry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Design2UMGPrototypeNavigationExtensions)

namespace
{
	FName MakePrototypeRuntimeWidgetName(const FString& RawName, const FString& FallbackPrefix)
	{
		return FUMGWidgetUtils::SanitizeWidgetName(RawName, FallbackPrefix);
	}

	UWidgetSwitcher* FindPrototypeSwitcher(UWidgetTree* WidgetTree, const FUMGPrototypeNavigationData& NavigationData)
	{
		if (!WidgetTree || NavigationData.SwitcherWidgetName.IsEmpty())
		{
			return nullptr;
		}

		return Cast<UWidgetSwitcher>(WidgetTree->FindWidget(
			MakePrototypeRuntimeWidgetName(NavigationData.SwitcherWidgetName, TEXT("PrototypeSwitcher"))));
	}

	UButton* FindPrototypeTriggerButton(UWidgetTree* WidgetTree, const FUMGPrototypeNavigationBinding& Binding)
	{
		if (!WidgetTree || Binding.TriggerWidgetName.IsEmpty())
		{
			return nullptr;
		}

		return Cast<UButton>(WidgetTree->FindWidget(
			MakePrototypeRuntimeWidgetName(Binding.TriggerWidgetName, TEXT("PrototypeTrigger"))));
	}
}

void UDesign2UMGPrototypeNavigationClickProxy::Initialize(
	UDesign2UMGPrototypeNavigationUserWidgetExtension* InOwner,
	const FUMGPrototypeNavigationBinding& InBinding)
{
	OwnerExtension = InOwner;
	Binding = InBinding;
}

void UDesign2UMGPrototypeNavigationClickProxy::HandleClicked()
{
	if (OwnerExtension)
	{
		OwnerExtension->HandleNavigationRequest(Binding);
	}
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::Configure(const FUMGPrototypeNavigationData& InNavigationData)
{
	StopActiveTransition(false);
	NavigationData = InNavigationData;
	ClickProxies.Reset();
	BoundUserWidget = nullptr;
	BoundSwitcher = nullptr;
	ActiveBinding = FUMGPrototypeNavigationBinding();
	TransitionBaseTranslation = FVector2D::ZeroVector;
	TransitionBaseOpacity = 1.0f;
	TransitionElapsedSeconds = 0.0f;
	bTransitionSwitchedTarget = false;
	bBindingsInitialized = false;
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::BindNavigation(UUserWidget* UserWidget)
{
	if (bBindingsInitialized || !NavigationData.IsValid() || !UserWidget || !UserWidget->WidgetTree)
	{
		return;
	}

	UWidgetSwitcher* Switcher = FindPrototypeSwitcher(UserWidget->WidgetTree, NavigationData);
	if (!Switcher)
	{
		return;
	}

	BoundUserWidget = UserWidget;
	BoundSwitcher = Switcher;
	if (Switcher->GetNumWidgets() > 0)
	{
		Switcher->SetActiveWidgetIndex(FMath::Clamp(NavigationData.InitialPageIndex, 0, Switcher->GetNumWidgets() - 1));
	}

	for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
	{
		if (!Binding.IsValid())
		{
			continue;
		}

		UButton* TriggerButton = FindPrototypeTriggerButton(UserWidget->WidgetTree, Binding);
		if (!TriggerButton || Binding.TargetPageIndex < 0 || Binding.TargetPageIndex >= Switcher->GetNumWidgets())
		{
			continue;
		}

		UDesign2UMGPrototypeNavigationClickProxy* ClickProxy =
			NewObject<UDesign2UMGPrototypeNavigationClickProxy>(this);
		ClickProxy->Initialize(this, Binding);
		TriggerButton->OnClicked.AddUniqueDynamic(
			ClickProxy,
			&UDesign2UMGPrototypeNavigationClickProxy::HandleClicked);
		ClickProxies.Add(ClickProxy);
	}

	bBindingsInitialized = true;
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::HandleNavigationRequest(
	const FUMGPrototypeNavigationBinding& Binding)
{
	if (!BoundSwitcher || !Binding.IsValid())
	{
		return;
	}

	if (Binding.TargetPageIndex < 0 || Binding.TargetPageIndex >= BoundSwitcher->GetNumWidgets())
	{
		return;
	}

	if (BoundSwitcher->GetActiveWidgetIndex() == Binding.TargetPageIndex)
	{
		return;
	}

	if (TransitionTickerHandle.IsValid())
	{
		return;
	}

	if (!Binding.Transition.IsAnimated())
	{
		BoundSwitcher->SetActiveWidgetIndex(Binding.TargetPageIndex);
		return;
	}

	ActiveBinding = Binding;
	TransitionBaseOpacity = BoundSwitcher->GetRenderOpacity();
	TransitionBaseTranslation = BoundSwitcher->GetRenderTransform().Translation;
	TransitionElapsedSeconds = 0.0f;
	bTransitionSwitchedTarget = false;
	TransitionTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this,
			&UDesign2UMGPrototypeNavigationUserWidgetExtension::TickActiveTransition));
}

bool UDesign2UMGPrototypeNavigationUserWidgetExtension::TickActiveTransition(float DeltaTime)
{
	if (!BoundSwitcher || !ActiveBinding.IsValid() || !ActiveBinding.Transition.IsAnimated())
	{
		StopActiveTransition(false);
		return false;
	}

	const FUMGPrototypeTransitionData& Transition = ActiveBinding.Transition;
	const float DurationSeconds = FMath::Max(Transition.DurationSeconds, KINDA_SMALL_NUMBER);
	TransitionElapsedSeconds += FMath::Max(0.0f, DeltaTime);

	const float RawAlpha = FMath::Clamp(TransitionElapsedSeconds / DurationSeconds, 0.0f, 1.0f);
	const FVector2D SlideOffset = ResolveTransitionOffset(BoundSwitcher, Transition.Direction);

	if (Transition.UsesFadeOnly())
	{
		if (RawAlpha < 0.5f)
		{
			const float PhaseAlpha = EvaluateEasingAlpha(RawAlpha * 2.0f, Transition.EasingType);
			BoundSwitcher->SetRenderOpacity(FMath::Lerp(TransitionBaseOpacity, 0.0f, PhaseAlpha));
			BoundSwitcher->SetRenderTranslation(TransitionBaseTranslation);
		}
		else
		{
			if (!bTransitionSwitchedTarget)
			{
				BoundSwitcher->SetActiveWidgetIndex(ActiveBinding.TargetPageIndex);
				bTransitionSwitchedTarget = true;
			}

			const float PhaseAlpha = EvaluateEasingAlpha((RawAlpha - 0.5f) * 2.0f, Transition.EasingType);
			BoundSwitcher->SetRenderOpacity(FMath::Lerp(0.0f, TransitionBaseOpacity, PhaseAlpha));
			BoundSwitcher->SetRenderTranslation(TransitionBaseTranslation);
		}
	}
	else if (Transition.UsesSlide())
	{
		BoundSwitcher->SetRenderOpacity(TransitionBaseOpacity);
		if (Transition.UsesBidirectionalSlide() || Transition.UsesOutgoingSlideOnly())
		{
			if (RawAlpha < 0.5f)
			{
				const float PhaseAlpha = EvaluateEasingAlpha(RawAlpha * 2.0f, Transition.EasingType);
				BoundSwitcher->SetRenderTranslation(TransitionBaseTranslation - (SlideOffset * PhaseAlpha));
			}
			else
			{
				if (!bTransitionSwitchedTarget)
				{
					BoundSwitcher->SetActiveWidgetIndex(ActiveBinding.TargetPageIndex);
					bTransitionSwitchedTarget = true;
				}

				if (Transition.UsesBidirectionalSlide())
				{
					const float PhaseAlpha = EvaluateEasingAlpha((RawAlpha - 0.5f) * 2.0f, Transition.EasingType);
					BoundSwitcher->SetRenderTranslation(TransitionBaseTranslation + (SlideOffset * (1.0f - PhaseAlpha)));
				}
				else
				{
					BoundSwitcher->SetRenderTranslation(TransitionBaseTranslation);
				}
			}
		}
		else
		{
			if (RawAlpha < 0.5f)
			{
				BoundSwitcher->SetRenderTranslation(TransitionBaseTranslation);
			}
			else
			{
				if (!bTransitionSwitchedTarget)
				{
					BoundSwitcher->SetActiveWidgetIndex(ActiveBinding.TargetPageIndex);
					bTransitionSwitchedTarget = true;
				}

				const float PhaseAlpha = EvaluateEasingAlpha((RawAlpha - 0.5f) * 2.0f, Transition.EasingType);
				BoundSwitcher->SetRenderTranslation(TransitionBaseTranslation + (SlideOffset * (1.0f - PhaseAlpha)));
			}
		}
	}
	else
	{
		BoundSwitcher->SetActiveWidgetIndex(ActiveBinding.TargetPageIndex);
	}

	if (RawAlpha >= 1.0f)
	{
		StopActiveTransition(true);
		return false;
	}

	return true;
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::StopActiveTransition(const bool bSnapToFinalState)
{
	if (TransitionTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TransitionTickerHandle);
		TransitionTickerHandle.Reset();
	}

	if (BoundSwitcher)
	{
		if (bSnapToFinalState && ActiveBinding.IsValid())
		{
			BoundSwitcher->SetActiveWidgetIndex(ActiveBinding.TargetPageIndex);
		}
		BoundSwitcher->SetRenderOpacity(TransitionBaseOpacity);
		BoundSwitcher->SetRenderTranslation(TransitionBaseTranslation);
	}

	ActiveBinding = FUMGPrototypeNavigationBinding();
	TransitionElapsedSeconds = 0.0f;
	bTransitionSwitchedTarget = false;
}

float UDesign2UMGPrototypeNavigationUserWidgetExtension::EvaluateEasingAlpha(
	float Alpha,
	const FString& EasingType)
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

FVector2D UDesign2UMGPrototypeNavigationUserWidgetExtension::ResolveTransitionOffset(
	const UWidgetSwitcher* Switcher,
	const FString& Direction)
{
	if (!Switcher)
	{
		return FVector2D(120.0f, 0.0f);
	}

	FVector2D LocalSize = Switcher->GetCachedGeometry().GetLocalSize();
	if (LocalSize.IsNearlyZero())
	{
		LocalSize = Switcher->GetDesiredSize();
	}
	if (LocalSize.IsNearlyZero())
	{
		if (const UWidget* ActiveWidget = Switcher->GetActiveWidget())
		{
			LocalSize = ActiveWidget->GetDesiredSize();
		}
	}

	if (LocalSize.X <= KINDA_SMALL_NUMBER)
	{
		LocalSize.X = 120.0f;
	}
	if (LocalSize.Y <= KINDA_SMALL_NUMBER)
	{
		LocalSize.Y = 120.0f;
	}

	if (Direction.Equals(TEXT("LEFT"), ESearchCase::IgnoreCase))
	{
		return FVector2D(-LocalSize.X, 0.0f);
	}
	if (Direction.Equals(TEXT("TOP"), ESearchCase::IgnoreCase)
		|| Direction.Equals(TEXT("UP"), ESearchCase::IgnoreCase))
	{
		return FVector2D(0.0f, -LocalSize.Y);
	}
	if (Direction.Equals(TEXT("BOTTOM"), ESearchCase::IgnoreCase)
		|| Direction.Equals(TEXT("DOWN"), ESearchCase::IgnoreCase))
	{
		return FVector2D(0.0f, LocalSize.Y);
	}

	return FVector2D(LocalSize.X, 0.0f);
}

#if WITH_EDITOR
bool UDesign2UMGPrototypeNavigationUserWidgetExtension::VerifyBindings(const UWidgetTree* WidgetTree) const
{
	if (!NavigationData.IsValid() || !WidgetTree)
	{
		return true;
	}

	if (!FindPrototypeSwitcher(const_cast<UWidgetTree*>(WidgetTree), NavigationData))
	{
		return false;
	}

	for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
	{
		if (!Binding.IsValid())
		{
			return false;
		}

		if (!FindPrototypeTriggerButton(const_cast<UWidgetTree*>(WidgetTree), Binding))
		{
			return false;
		}
	}

	return true;
}
#endif

void UDesign2UMGPrototypeNavigationClassExtension::SetNavigationData(const FUMGPrototypeNavigationData& InNavigationData)
{
	NavigationData = InNavigationData;
}

void UDesign2UMGPrototypeNavigationClassExtension::EnsureNavigationBound(UUserWidget* UserWidget)
{
	if (!NavigationData.IsValid() || !UserWidget)
	{
		return;
	}

	UDesign2UMGPrototypeNavigationUserWidgetExtension* UserExtension =
		UserWidget->GetExtension<UDesign2UMGPrototypeNavigationUserWidgetExtension>();
	if (!UserExtension)
	{
		UserExtension = UserWidget->AddExtension<UDesign2UMGPrototypeNavigationUserWidgetExtension>();
	}

	if (!UserExtension || UserExtension->IsBound())
	{
		return;
	}

	UserExtension->Configure(NavigationData);
	UserExtension->BindNavigation(UserWidget);
}

void UDesign2UMGPrototypeNavigationClassExtension::Initialize(UUserWidget* UserWidget)
{
	EnsureNavigationBound(UserWidget);
}

void UDesign2UMGPrototypeNavigationClassExtension::Construct(UUserWidget* UserWidget)
{
	EnsureNavigationBound(UserWidget);
}

#if WITH_EDITOR
void UDesign2UMGPrototypeNavigationClassExtension::InitializeInEditor(UUserWidget* UserWidget)
{
	EnsureNavigationBound(UserWidget);
}

bool UDesign2UMGPrototypeNavigationClassExtension::VerifyBindings(const UWidgetTree* WidgetTree) const
{
	if (!NavigationData.IsValid() || !WidgetTree)
	{
		return true;
	}

	if (!FindPrototypeSwitcher(const_cast<UWidgetTree*>(WidgetTree), NavigationData))
	{
		return false;
	}

	for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
	{
		if (!Binding.IsValid())
		{
			return false;
		}

		if (!FindPrototypeTriggerButton(const_cast<UWidgetTree*>(WidgetTree), Binding))
		{
			return false;
		}
	}

	return true;
}
#endif

void UDesign2UMGPrototypeNavigationBlueprintExtension::SetNavigationData(
	const FUMGPrototypeNavigationData& InNavigationData)
{
	NavigationData = InNavigationData;
}

void UDesign2UMGPrototypeNavigationBlueprintExtension::HandleBeginCompilation(
	FWidgetBlueprintCompilerContext& InCreationContext)
{
	Super::HandleBeginCompilation(InCreationContext);
	CompilerContext = &InCreationContext;
}

void UDesign2UMGPrototypeNavigationBlueprintExtension::HandleFinishCompilingClass(
	UWidgetBlueprintGeneratedClass* Class)
{
	Super::HandleFinishCompilingClass(Class);

	if (!CompilerContext || !NavigationData.IsValid() || !Class)
	{
		return;
	}

	UDesign2UMGPrototypeNavigationClassExtension* NewExtension =
		NewObject<UDesign2UMGPrototypeNavigationClassExtension>(Class);
	NewExtension->SetNavigationData(NavigationData);
	CompilerContext->AddExtension(Class, NewExtension);
}

bool UDesign2UMGPrototypeNavigationBlueprintExtension::HandleValidateGeneratedClass(
	UWidgetBlueprintGeneratedClass* Class)
{
	if (!NavigationData.IsValid() || !Class)
	{
		return true;
	}

	const UDesign2UMGPrototypeNavigationClassExtension* RuntimeExtension =
		Class->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>();
	return RuntimeExtension ? RuntimeExtension->VerifyBindings(Class->GetWidgetTreeArchetype()) : false;
}

void UDesign2UMGPrototypeNavigationBlueprintExtension::HandleEndCompilation()
{
	CompilerContext = nullptr;
}
