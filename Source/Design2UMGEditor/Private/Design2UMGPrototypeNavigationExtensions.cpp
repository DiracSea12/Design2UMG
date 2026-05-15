// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeNavigationExtensions.h"

#include "Design2UMGPrototypeRuntimeValueResolver.h"
#include "Design2UMGPrototypeNavigationInput.h"
#include "Design2UMGLog.h"
#include "UMGWidgetUtils.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "HAL/PlatformProcess.h"
#include "Layout/Geometry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Design2UMGPrototypeNavigationExtensions)

bool GDesign2UMGPrototypeNavigationCaptureOpenUrl = false;
FString GDesign2UMGPrototypeNavigationLastOpenedUrl;

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

	UOverlay* FindPrototypeOverlayHost(UWidgetTree* WidgetTree, const FUMGPrototypeNavigationData& NavigationData)
	{
		if (!WidgetTree || NavigationData.OverlayHostWidgetName.IsEmpty())
		{
			return nullptr;
		}

		return Cast<UOverlay>(WidgetTree->FindWidget(
			MakePrototypeRuntimeWidgetName(NavigationData.OverlayHostWidgetName, TEXT("PrototypeOverlayHost"))));
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

void UDesign2UMGPrototypeNavigationTriggerProxy::Initialize(
	UDesign2UMGPrototypeNavigationUserWidgetExtension* InOwner,
	const FUMGPrototypeNavigationBinding& InBinding)
{
	OwnerExtension = InOwner;
	Binding = InBinding;
}

void UDesign2UMGPrototypeNavigationTriggerProxy::HandleClicked()
{
	if (OwnerExtension)
	{
		OwnerExtension->HandleNavigationRequest(Binding);
	}
}

void UDesign2UMGPrototypeNavigationTriggerProxy::HandleHovered()
{
	if (OwnerExtension)
	{
		OwnerExtension->HandleNavigationRequest(Binding);
	}
}

void UDesign2UMGPrototypeNavigationTriggerProxy::HandlePressed()
{
	if (OwnerExtension)
	{
		OwnerExtension->HandleNavigationRequest(Binding);
	}
}

FReply UDesign2UMGPrototypeNavigationTriggerProxy::HandleDragDetected(
	const FGeometry&,
	const FPointerEvent&)
{
	if (OwnerExtension)
	{
		OwnerExtension->HandleNavigationRequest(Binding);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::Configure(const FUMGPrototypeNavigationData& InNavigationData)
{
	ClearGeneratedTransitionPlayback(true);
	StopActiveTransition(false);
	Design2UMGPrototypeNavigationInput::UnregisterKeyboardExtension(this);
	Design2UMGPrototypeNavigationInput::ResetHistory(this);
	NavigationData = InNavigationData;
	TriggerProxies.Reset();
	BoundUserWidget = nullptr;
	BoundSwitcher = nullptr;
	BoundOverlayHost = nullptr;
	PrototypePageWidgets.Reset();
	OverlayStack.Reset();
	PrototypeVariableValues.Reset();
	PrototypeVariableModes.Reset();
	ActiveBinding = FUMGPrototypeNavigationBinding();
	ActiveGeneratedTransitionAnimation = nullptr;
	PendingIncomingAnimation = nullptr;
	TransitionBaseTranslation = FVector2D::ZeroVector;
	TransitionBaseOpacity = 1.0f;
	TransitionElapsedSeconds = 0.0f;
	bTransitionSwitchedTarget = false;
	GeneratedTransitionStage = EDesign2UMGPrototypeGeneratedTransitionStage::None;
	bBindingsInitialized = false;
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::BindNavigation(UUserWidget* UserWidget)
{
	if (bBindingsInitialized || !NavigationData.IsValid() || !UserWidget || !UserWidget->WidgetTree)
	{
		return;
	}

	UWidgetSwitcher* Switcher = nullptr;
	if (NavigationData.RequiresSwitcher())
	{
		Switcher = FindPrototypeSwitcher(UserWidget->WidgetTree, NavigationData);
		if (!Switcher)
		{
			return;
		}
	}

	UOverlay* OverlayHost = nullptr;
	if (NavigationData.HasOverlayBindings())
	{
		OverlayHost = FindPrototypeOverlayHost(UserWidget->WidgetTree, NavigationData);
		if (!OverlayHost)
		{
			return;
		}

		OverlayHost->SetVisibility(ESlateVisibility::Collapsed);
	}

	BoundUserWidget = UserWidget;
	BoundSwitcher = Switcher;
	BoundOverlayHost = OverlayHost;
	PrototypePageWidgets.Reset();
	OverlayStack.Reset();
	if (Switcher && Switcher->GetNumWidgets() > 0)
	{
		Switcher->SetActiveWidgetIndex(FMath::Clamp(NavigationData.InitialPageIndex, 0, Switcher->GetNumWidgets() - 1));
	}
	if (Switcher)
	{
		const int32 PageCount = FMath::Min(NavigationData.PageIds.Num(), Switcher->GetNumWidgets());
		for (int32 PageIndex = 0; PageIndex < PageCount; ++PageIndex)
		{
			const FString& PageId = NavigationData.PageIds[PageIndex];
			if (PageId.IsEmpty())
			{
				continue;
			}

			if (UWidget* PageWidget = Switcher->GetWidgetAtIndex(PageIndex))
			{
				PrototypePageWidgets.Add(PageId, PageWidget);
			}
		}
	}
	UE_LOG(LogDesign2UMG, Verbose, TEXT("Prototype bind: pages=%d overlayHost=%s"),
		PrototypePageWidgets.Num(),
		BoundOverlayHost ? *BoundOverlayHost->GetName() : TEXT("<none>"));

	for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
	{
		if (!Binding.IsValid())
		{
			continue;
		}

		if (Binding.RequiresSwitcher() && !Switcher)
		{
			continue;
		}

		if (Binding.RequiresPageTarget()
			&& (!Switcher || Binding.TargetPageIndex < 0 || Binding.TargetPageIndex >= Switcher->GetNumWidgets()))
		{
			continue;
		}

		UButton* TriggerButton = FindPrototypeTriggerButton(UserWidget->WidgetTree, Binding);
		if (Binding.UsesKeyboardTrigger())
		{
			continue;
		}
		if (!TriggerButton)
		{
			continue;
		}

		UDesign2UMGPrototypeNavigationTriggerProxy* TriggerProxy =
			NewObject<UDesign2UMGPrototypeNavigationTriggerProxy>(this);
		TriggerProxy->Initialize(this, Binding);
		if (Binding.UsesDragTrigger())
		{
			TriggerButton->SetAllowDragDrop(true);
			TriggerButton->OnButtonDragDetected.BindUObject(
				TriggerProxy,
				&UDesign2UMGPrototypeNavigationTriggerProxy::HandleDragDetected);
		}
		else if (Binding.UsesHoverTrigger())
		{
			TriggerButton->OnHovered.AddUniqueDynamic(
				TriggerProxy,
				&UDesign2UMGPrototypeNavigationTriggerProxy::HandleHovered);
		}
		else if (Binding.UsesPressTrigger())
		{
			TriggerButton->OnPressed.AddUniqueDynamic(
				TriggerProxy,
				&UDesign2UMGPrototypeNavigationTriggerProxy::HandlePressed);
		}
		else
		{
			TriggerButton->OnClicked.AddUniqueDynamic(
				TriggerProxy,
				&UDesign2UMGPrototypeNavigationTriggerProxy::HandleClicked);
		}
		TriggerProxies.Add(TriggerProxy);
	}

	if (HasKeyboardBindings())
	{
		BoundUserWidget->SetIsFocusable(true);
		Design2UMGPrototypeNavigationInput::RegisterKeyboardExtension(this);
	}

	bBindingsInitialized = true;
}

bool UDesign2UMGPrototypeNavigationUserWidgetExtension::HasKeyboardBindings() const
{
	for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
	{
		if (Binding.UsesKeyboardTrigger())
		{
			return true;
		}
	}

	return false;
}

bool UDesign2UMGPrototypeNavigationUserWidgetExtension::HasKeyboardFocusForPrototypeInput() const
{
	return BoundUserWidget
		&& (BoundUserWidget->HasKeyboardFocus()
			|| BoundUserWidget->HasAnyUserFocus()
			|| BoundUserWidget->HasFocusedDescendants());
}

bool UDesign2UMGPrototypeNavigationUserWidgetExtension::ShouldCaptureKeyboardInput() const
{
	return HasKeyboardBindings() && HasKeyboardFocusForPrototypeInput();
}

bool UDesign2UMGPrototypeNavigationUserWidgetExtension::HandleKeyboardInput(
	const FKey& Key,
	const bool bIsKeyDown)
{
	if (!HasKeyboardBindings())
	{
		return false;
	}

	bool bMatchedBinding = false;
	for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
	{
		if (!Binding.IsValid())
		{
			continue;
		}

		if ((bIsKeyDown && !Binding.UsesKeyDownTrigger())
			|| (!bIsKeyDown && !Binding.UsesKeyUpTrigger()))
		{
			continue;
		}

		bool bMatchesKey = false;
		for (const FString& KeyCode : Binding.TriggerKeyCodes)
		{
			if (Design2UMGPrototypeNavigationInput::MatchesKeyCode(Key, KeyCode))
			{
				bMatchesKey = true;
				break;
			}
		}
		if (!bMatchesKey)
		{
			continue;
		}

		bMatchedBinding = true;
		HandleNavigationRequest(Binding);
	}

	return bMatchedBinding;
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::BeginDestroy()
{
	Design2UMGPrototypeNavigationInput::UnregisterKeyboardExtension(this);
	Super::BeginDestroy();
}

bool UDesign2UMGPrototypeNavigationUserWidgetExtension::ExecutePrototypeStepBinding(
	const FUMGPrototypeNavigationBinding& SourceBinding,
	const FUMGPrototypeNavigationStep& Step)
{
	FUMGPrototypeNavigationBinding StepBinding = SourceBinding;
	StepBinding.Steps.Reset();
	StepBinding.TargetPageId = Step.TargetPageId;
	StepBinding.TargetPageIndex = Step.TargetPageIndex;
	StepBinding.ExternalUrl = Step.ExternalUrl;
	StepBinding.Transition = Step.Transition;

	switch (Step.Kind)
	{
	case EUMGPrototypeNavigationStepKind::Navigate:
		StepBinding.Action = EUMGPrototypeNavigationAction::Navigate;
		break;
	case EUMGPrototypeNavigationStepKind::Overlay:
		StepBinding.Action = EUMGPrototypeNavigationAction::Overlay;
		break;
	case EUMGPrototypeNavigationStepKind::Back:
		StepBinding.Action = EUMGPrototypeNavigationAction::Back;
		StepBinding.TargetPageId.Reset();
		StepBinding.TargetPageIndex = INDEX_NONE;
		break;
	case EUMGPrototypeNavigationStepKind::OpenLink:
		StepBinding.Action = EUMGPrototypeNavigationAction::OpenLink;
		break;
	default:
		return false;
	}

	HandleNavigationRequest(StepBinding);
	return true;
}

bool UDesign2UMGPrototypeNavigationUserWidgetExtension::ExecutePrototypeProgram(
	const FUMGPrototypeNavigationBinding& Binding)
{
	if (Binding.Steps.Num() == 0)
	{
		return false;
	}

	int32 ProgramCounter = 0;
	while (Binding.Steps.IsValidIndex(ProgramCounter))
	{
		const FUMGPrototypeNavigationStep& Step = Binding.Steps[ProgramCounter];
		switch (Step.Kind)
		{
		case EUMGPrototypeNavigationStepKind::ConditionJump:
			ProgramCounter = Design2UMGPrototypeRuntimeValueResolver::EvaluateConditionJson(
				Step.ConditionJson,
				PrototypeVariableValues,
				PrototypeVariableModes)
				? (ProgramCounter + 1)
				: Step.JumpTargetIndex;
			break;
		case EUMGPrototypeNavigationStepKind::Jump:
			ProgramCounter = Step.JumpTargetIndex;
			break;
		case EUMGPrototypeNavigationStepKind::SetVariable:
			PrototypeVariableValues.FindOrAdd(Step.VariableId) = Step.VariableValueJson;
			++ProgramCounter;
			break;
		case EUMGPrototypeNavigationStepKind::SetVariableMode:
			PrototypeVariableModes.FindOrAdd(Step.VariableCollectionId) = Step.VariableModeId;
			++ProgramCounter;
			break;
		default:
			return ExecutePrototypeStepBinding(Binding, Step);
		}
	}

	return true;
}

UWidget* UDesign2UMGPrototypeNavigationUserWidgetExtension::ResolvePrototypePageWidget(const FString& PageId) const
{
	if (const TObjectPtr<UWidget>* FoundWidget = PrototypePageWidgets.Find(PageId))
	{
		return FoundWidget->Get();
	}

	return nullptr;
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::RestoreOverlayPage(
	const FDesign2UMGPrototypeOverlayEntry& Entry)
{
	if (!BoundSwitcher || !Entry.PageWidget)
	{
		return;
	}

	UWidget* ActiveBaseWidget = BoundSwitcher->GetActiveWidget();

	if (UPanelWidget* CurrentParent = Cast<UPanelWidget>(Entry.PageWidget->GetParent()))
	{
		CurrentParent->RemoveChild(Entry.PageWidget);
	}

	BoundSwitcher->InsertChildAt(
		FMath::Clamp(Entry.OriginalIndex, 0, BoundSwitcher->GetChildrenCount()),
		Entry.PageWidget);

	if (ActiveBaseWidget)
	{
		const int32 ActiveWidgetIndex = BoundSwitcher->GetChildIndex(ActiveBaseWidget);
		if (ActiveWidgetIndex != INDEX_NONE)
		{
			BoundSwitcher->SetActiveWidgetIndex(ActiveWidgetIndex);
		}
	}
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::RestoreAllOverlayPages()
{
	for (int32 EntryIndex = OverlayStack.Num() - 1; EntryIndex >= 0; --EntryIndex)
	{
		RestoreOverlayPage(OverlayStack[EntryIndex]);
	}

	OverlayStack.Reset();
	if (BoundOverlayHost)
	{
		BoundOverlayHost->SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool UDesign2UMGPrototypeNavigationUserWidgetExtension::PushOverlayPage(
	const FUMGPrototypeNavigationBinding& Binding)
{
	if (!BoundSwitcher || !BoundOverlayHost || Binding.TargetPageId.IsEmpty())
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Prototype overlay skipped: switcher=%d overlayHost=%d target='%s'"),
			BoundSwitcher != nullptr,
			BoundOverlayHost != nullptr,
			*Binding.TargetPageId);
		return false;
	}

	UWidget* TargetPageWidget = ResolvePrototypePageWidget(Binding.TargetPageId);
	if (!TargetPageWidget)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Prototype overlay skipped: page '%s' not found"), *Binding.TargetPageId);
		return false;
	}

	if (TargetPageWidget->GetParent() == BoundOverlayHost)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Prototype overlay skipped: page '%s' already active in overlay host"), *Binding.TargetPageId);
		return false;
	}

	if (UPanelWidget* CurrentParent = Cast<UPanelWidget>(TargetPageWidget->GetParent()))
	{
		CurrentParent->RemoveChild(TargetPageWidget);
	}

	FDesign2UMGPrototypeOverlayEntry& OverlayEntry = OverlayStack.AddDefaulted_GetRef();
	OverlayEntry.PageId = Binding.TargetPageId;
	OverlayEntry.OriginalIndex = Binding.TargetPageIndex;
	OverlayEntry.PageWidget = TargetPageWidget;

	if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(BoundOverlayHost->AddChild(TargetPageWidget)))
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Fill);
	}
	BoundOverlayHost->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UE_LOG(LogDesign2UMG, Verbose, TEXT("Prototype overlay opened: page '%s' -> host '%s'"),
		*Binding.TargetPageId,
		*BoundOverlayHost->GetName());
	return true;
}

bool UDesign2UMGPrototypeNavigationUserWidgetExtension::PopOverlayPage()
{
	if (OverlayStack.Num() == 0)
	{
		return false;
	}

	const FDesign2UMGPrototypeOverlayEntry OverlayEntry = OverlayStack.Pop(EAllowShrinking::No);
	RestoreOverlayPage(OverlayEntry);
	if (BoundOverlayHost && OverlayStack.Num() == 0)
	{
		BoundOverlayHost->SetVisibility(ESlateVisibility::Collapsed);
	}
	return true;
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::HandleNavigationRequest(
	const FUMGPrototypeNavigationBinding& Binding)
{
	if (!Binding.IsValid())
	{
		return;
	}

	if (Binding.Steps.Num() > 0)
	{
		ExecutePrototypeProgram(Binding);
		return;
	}

	if (Binding.Action != EUMGPrototypeNavigationAction::OpenLink && TransitionTickerHandle.IsValid())
	{
		return;
	}
	if (Binding.Action != EUMGPrototypeNavigationAction::OpenLink
		&& GeneratedTransitionStage != EDesign2UMGPrototypeGeneratedTransitionStage::None)
	{
		return;
	}

	if (Binding.Action == EUMGPrototypeNavigationAction::OpenLink)
	{
		const FString& Url = Binding.ExternalUrl;
		if (Url.IsEmpty())
		{
			return;
		}

		GDesign2UMGPrototypeNavigationLastOpenedUrl = Url;
		if (!GDesign2UMGPrototypeNavigationCaptureOpenUrl)
		{
			FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
		}
		return;
	}

	if (!BoundSwitcher)
	{
		return;
	}

	const int32 CurrentPageIndex = BoundSwitcher->GetActiveWidgetIndex();
	FUMGPrototypeNavigationBinding ResolvedBinding = Binding;
	if (Binding.Action == EUMGPrototypeNavigationAction::Overlay)
	{
		PushOverlayPage(Binding);
		return;
	}
	if (Binding.Action == EUMGPrototypeNavigationAction::Back)
	{
		if (PopOverlayPage())
		{
			return;
		}

		int32 PreviousPageIndex = INDEX_NONE;
		if (!Design2UMGPrototypeNavigationInput::TryPopHistory(this, CurrentPageIndex, PreviousPageIndex))
		{
			return;
		}

		ResolvedBinding.TargetPageIndex = PreviousPageIndex;
		if (NavigationData.PageIds.IsValidIndex(PreviousPageIndex))
		{
			ResolvedBinding.TargetPageId = NavigationData.PageIds[PreviousPageIndex];
		}
	}
	else
	{
		if (OverlayStack.Num() > 0)
		{
			RestoreAllOverlayPages();
		}

		if (Binding.TargetPageIndex < 0 || Binding.TargetPageIndex >= BoundSwitcher->GetNumWidgets())
		{
			return;
		}
		if (CurrentPageIndex == Binding.TargetPageIndex)
		{
			return;
		}

		Design2UMGPrototypeNavigationInput::PushHistory(this, CurrentPageIndex);
	}

	if (ResolvedBinding.TargetPageIndex < 0 || ResolvedBinding.TargetPageIndex >= BoundSwitcher->GetNumWidgets())
	{
		return;
	}

	if (CurrentPageIndex == ResolvedBinding.TargetPageIndex)
	{
		return;
	}

	if (TryPlayGeneratedTransition(ResolvedBinding))
	{
		return;
	}

	if (!ResolvedBinding.Transition.IsAnimated())
	{
		BoundSwitcher->SetActiveWidgetIndex(ResolvedBinding.TargetPageIndex);
		return;
	}

	ActiveBinding = ResolvedBinding;
	TransitionBaseOpacity = BoundSwitcher->GetRenderOpacity();
	TransitionBaseTranslation = BoundSwitcher->GetRenderTransform().Translation;
	TransitionElapsedSeconds = 0.0f;
	bTransitionSwitchedTarget = false;
	TransitionTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this,
			&UDesign2UMGPrototypeNavigationUserWidgetExtension::TickActiveTransition));
}

UWidgetAnimation* UDesign2UMGPrototypeNavigationUserWidgetExtension::ResolveGeneratedTransitionAnimation(
	const FString& AnimationName) const
{
	if (!BoundUserWidget || AnimationName.IsEmpty())
	{
		return nullptr;
	}

	const FName TargetName(*AnimationName);
	const UWidgetBlueprintGeneratedClass* GeneratedClass =
		Cast<UWidgetBlueprintGeneratedClass>(BoundUserWidget->GetClass());
	if (!GeneratedClass)
	{
		return nullptr;
	}

	for (UWidgetAnimation* Animation : GeneratedClass->Animations)
	{
		if (!Animation)
		{
			continue;
		}
		if (Animation->GetFName() == TargetName
			|| Animation->GetDisplayLabel().Equals(AnimationName, ESearchCase::IgnoreCase))
		{
			return Animation;
		}
	}
	return nullptr;
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::StartGeneratedTransitionStage(
	UWidgetAnimation* Animation,
	const EDesign2UMGPrototypeGeneratedTransitionStage Stage)
{
	if (!BoundUserWidget || !Animation)
	{
		return;
	}

	ActiveGeneratedTransitionAnimation = Animation;
	GeneratedTransitionStage = Stage;
	FWidgetAnimationDynamicEvent FinishedEvent;
	FinishedEvent.BindDynamic(this, &UDesign2UMGPrototypeNavigationUserWidgetExtension::HandleGeneratedTransitionAnimationFinished);
	BoundUserWidget->BindToAnimationFinished(Animation, FinishedEvent);
	BoundUserWidget->PlayAnimation(Animation, 0.0f, 1, EUMGSequencePlayMode::Forward, 1.0f, false);
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::ClearGeneratedTransitionPlayback(const bool bResetVisualState)
{
	if (BoundUserWidget && ActiveGeneratedTransitionAnimation)
	{
		FWidgetAnimationDynamicEvent FinishedEvent;
		FinishedEvent.BindDynamic(this, &UDesign2UMGPrototypeNavigationUserWidgetExtension::HandleGeneratedTransitionAnimationFinished);
		BoundUserWidget->UnbindFromAnimationFinished(ActiveGeneratedTransitionAnimation, FinishedEvent);
		BoundUserWidget->StopAnimation(ActiveGeneratedTransitionAnimation);
	}

	ActiveGeneratedTransitionAnimation = nullptr;
	PendingIncomingAnimation = nullptr;
	GeneratedTransitionStage = EDesign2UMGPrototypeGeneratedTransitionStage::None;

	if (bResetVisualState && BoundSwitcher)
	{
		BoundSwitcher->SetRenderOpacity(1.0f);
		BoundSwitcher->SetRenderTranslation(FVector2D::ZeroVector);
	}
}

bool UDesign2UMGPrototypeNavigationUserWidgetExtension::TryPlayGeneratedTransition(
	const FUMGPrototypeNavigationBinding& Binding)
{
	if (Binding.Action == EUMGPrototypeNavigationAction::Overlay
		|| (Binding.Action == EUMGPrototypeNavigationAction::Back && OverlayStack.Num() > 0))
	{
		return false;
	}

	const UWorld* WidgetWorld = BoundUserWidget ? BoundUserWidget->GetWorld() : nullptr;
	if (!BoundUserWidget
		|| !BoundSwitcher
		|| !Binding.Transition.HasGeneratedAnimations()
		|| !WidgetWorld
		|| !WidgetWorld->IsGameWorld())
	{
		return false;
	}

	// Slide transitions depend on the runtime Switcher's actual size; keeping the ticker path avoids
	// under-travel or overshoot when pre-baked import-time bounds meet DPI or container constraint changes.
	if (Binding.Transition.UsesSlide())
	{
		return false;
	}

	UWidgetAnimation* OutgoingAnimation =
		ResolveGeneratedTransitionAnimation(Binding.Transition.OutgoingAnimationName);
	UWidgetAnimation* IncomingAnimation =
		ResolveGeneratedTransitionAnimation(Binding.Transition.IncomingAnimationName);
	if (!OutgoingAnimation && !IncomingAnimation)
	{
		return false;
	}

	ClearGeneratedTransitionPlayback(true);
	ActiveBinding = Binding;
	PendingIncomingAnimation = IncomingAnimation;

	if (OutgoingAnimation)
	{
		StartGeneratedTransitionStage(OutgoingAnimation, EDesign2UMGPrototypeGeneratedTransitionStage::Outgoing);
		return true;
	}

	BoundSwitcher->SetActiveWidgetIndex(Binding.TargetPageIndex);
	if (IncomingAnimation)
	{
		StartGeneratedTransitionStage(IncomingAnimation, EDesign2UMGPrototypeGeneratedTransitionStage::Incoming);
	}
	return true;
}

void UDesign2UMGPrototypeNavigationUserWidgetExtension::HandleGeneratedTransitionAnimationFinished()
{
	if (!BoundSwitcher)
	{
		ClearGeneratedTransitionPlayback(false);
		return;
	}

	if (GeneratedTransitionStage == EDesign2UMGPrototypeGeneratedTransitionStage::Outgoing)
	{
		UWidgetAnimation* IncomingAnimation = PendingIncomingAnimation;
		PendingIncomingAnimation = nullptr;
		ClearGeneratedTransitionPlayback(false);
		if (ActiveBinding.TargetPageIndex >= 0 && ActiveBinding.TargetPageIndex < BoundSwitcher->GetNumWidgets())
		{
			BoundSwitcher->SetActiveWidgetIndex(ActiveBinding.TargetPageIndex);
		}

		if (IncomingAnimation)
		{
			StartGeneratedTransitionStage(IncomingAnimation, EDesign2UMGPrototypeGeneratedTransitionStage::Incoming);
			return;
		}

		BoundSwitcher->SetRenderOpacity(1.0f);
		BoundSwitcher->SetRenderTranslation(FVector2D::ZeroVector);
		return;
	}

	ClearGeneratedTransitionPlayback(true);
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

	UWidgetSwitcher* Switcher = nullptr;
	if (NavigationData.RequiresSwitcher())
	{
		Switcher = FindPrototypeSwitcher(const_cast<UWidgetTree*>(WidgetTree), NavigationData);
		if (!Switcher)
		{
			return false;
		}
	}

	if (NavigationData.HasOverlayBindings() && !FindPrototypeOverlayHost(const_cast<UWidgetTree*>(WidgetTree), NavigationData))
	{
		return false;
	}

	for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
	{
		if (!Binding.IsValid())
		{
			return false;
		}

		if (!Binding.UsesKeyboardTrigger()
			&& !FindPrototypeTriggerButton(const_cast<UWidgetTree*>(WidgetTree), Binding))
		{
			return false;
		}

		if (Binding.RequiresSwitcher() && !Switcher)
		{
			return false;
		}

		if (Binding.RequiresPageTarget()
			&& (!Switcher || Binding.TargetPageIndex < 0 || Binding.TargetPageIndex >= Switcher->GetNumWidgets()))
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

	UWidgetSwitcher* Switcher = nullptr;
	if (NavigationData.RequiresSwitcher())
	{
		Switcher = FindPrototypeSwitcher(const_cast<UWidgetTree*>(WidgetTree), NavigationData);
		if (!Switcher)
		{
			return false;
		}
	}

	if (NavigationData.HasOverlayBindings() && !FindPrototypeOverlayHost(const_cast<UWidgetTree*>(WidgetTree), NavigationData))
	{
		return false;
	}

	for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
	{
		if (!Binding.IsValid())
		{
			return false;
		}

		if (!Binding.UsesKeyboardTrigger()
			&& !FindPrototypeTriggerButton(const_cast<UWidgetTree*>(WidgetTree), Binding))
		{
			return false;
		}

		if (Binding.RequiresSwitcher() && !Switcher)
		{
			return false;
		}

		if (Binding.RequiresPageTarget()
			&& (!Switcher || Binding.TargetPageIndex < 0 || Binding.TargetPageIndex >= Switcher->GetNumWidgets()))
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
