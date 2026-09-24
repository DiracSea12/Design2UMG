// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTreeBuilder.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/WidgetSwitcher.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{
bool IsSimpleMouseActionBinding(const FUMGPrototypeNavigationBinding& Binding)
{
	if (!Binding.UsesClickTrigger() && !Binding.UsesHoverTrigger() && !Binding.UsesPressTrigger())
	{
		return false;
	}
	if (Binding.Steps.Num() != 1)
	{
		return false;
	}
	switch (Binding.Steps[0].Kind)
	{
	case EUMGPrototypeNavigationStepKind::Navigate:
	case EUMGPrototypeNavigationStepKind::OpenLink:
	case EUMGPrototypeNavigationStepKind::Overlay:
	case EUMGPrototypeNavigationStepKind::Back:
		return true;
	default:
		return false;
	}
}

namespace
{
	FName GetDelegateNameForTrigger(const FUMGPrototypeNavigationBinding& Binding)
	{
		if (Binding.UsesHoverTrigger())
		{
			return GET_MEMBER_NAME_CHECKED(UButton, OnHovered);
		}
		if (Binding.UsesPressTrigger())
		{
			return GET_MEMBER_NAME_CHECKED(UButton, OnPressed);
		}
		return GET_MEMBER_NAME_CHECKED(UButton, OnClicked);
	}

	bool IsSimpleKeyboardActionBinding(const FUMGPrototypeNavigationBinding& Binding)
	{
		if (!Binding.UsesKeyDownTrigger() && !Binding.UsesKeyUpTrigger())
		{
			return false;
		}
		if (Binding.Steps.Num() != 1)
		{
			return false;
		}
		const EUMGPrototypeNavigationStepKind Kind = Binding.Steps[0].Kind;
		return Kind == EUMGPrototypeNavigationStepKind::Navigate
			|| Kind == EUMGPrototypeNavigationStepKind::OpenLink;
	}

	/** Index of the page widget (by sanitized page id) among the switcher's children; INDEX_NONE unless the match is unique. */
	int32 FindSwitcherPageIndex(const UWidgetSwitcher* Switcher, const FString& PageBaseName)
	{
		if (!Switcher || PageBaseName.IsEmpty())
		{
			return INDEX_NONE;
		}

		int32 MatchCount = 0;
		int32 MatchedIndex = INDEX_NONE;
		for (int32 ChildIndex = 0; ChildIndex < Switcher->GetChildrenCount(); ++ChildIndex)
		{
			const UWidget* Child = Switcher->GetChildAt(ChildIndex);
			if (Child && FUMGWidgetTreeBuilder::MatchesWidgetBaseName(Child->GetName(), PageBaseName))
			{
				++MatchCount;
				MatchedIndex = ChildIndex;
			}
		}
		return MatchCount == 1 ? MatchedIndex : INDEX_NONE;
	}

	/**
	 * Unique direct child of the overlay host matching the page base name. BuildRoot parents every
	 * overlay-target page under the host at build time - overlay-only pages under their base name,
	 * dual-target/initial pages as a mirror copy under base name + OverlayMirrorSuffix - so the
	 * overlay binding's page and the Back reset list both resolve here.
	 */
	UWidget* FindOverlayHostPage(const UOverlay* OverlayHost, const FString& PageBaseName)
	{
		if (!OverlayHost || PageBaseName.IsEmpty())
		{
			return nullptr;
		}
		int32 MatchCount = 0;
		UWidget* Matched = nullptr;
		for (int32 ChildIndex = 0; ChildIndex < OverlayHost->GetChildrenCount(); ++ChildIndex)
		{
			UWidget* Child = OverlayHost->GetChildAt(ChildIndex);
			if (Child && FUMGWidgetTreeBuilder::MatchesWidgetBaseName(Child->GetName(), PageBaseName))
			{
				++MatchCount;
				Matched = Child;
			}
		}
		return MatchCount == 1 ? Matched : nullptr;
	}
}

FBindingPlan ResolveBindingPlan(UWidgetBlueprint* WBP,
	const FUMGPrototypeNavigationData& NavigationData, const FEngineFunctions& Functions,
	TArray<FString>& OutWarnings)
{
	// Shared tree resolution: tree widgets are stable across the skeleton refresh below, so they are
	// resolved once; class properties are read after any refresh instead of cached across it.
	const FString SwitcherBaseName = FUMGWidgetTreeBuilder::SanitizeWidgetName(
		NavigationData.SwitcherWidgetName, TEXT("WidgetSwitcher")).ToString();
	UWidgetSwitcher* Switcher = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UWidgetSwitcher>(
		WBP->WidgetTree, SwitcherBaseName);
	UOverlay* OverlayHost = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UOverlay>(
		WBP->WidgetTree, FUMGWidgetTreeBuilder::SanitizeWidgetName(
			NavigationData.OverlayHostWidgetName, TEXT("Overlay")).ToString());

	// Pass 1 - resolve every candidate binding without touching the blueprint, so the (potentially
	// skeleton-refreshing) history variable creation below cannot invalidate cached properties.
	TArray<FResolvedBinding> MouseBindings;
	TArray<FResolvedBinding> KeyboardBindings;

	for (int32 BindingIndex = 0; BindingIndex < NavigationData.ClickBindings.Num(); ++BindingIndex)
	{
		const FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings[BindingIndex];
		const bool bMouse = IsSimpleMouseActionBinding(Binding);
		const bool bKeyboard = !bMouse && IsSimpleKeyboardActionBinding(Binding);
		if (!bMouse && !bKeyboard)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s' (%s): binding is not a simple single-action mouse Navigate/OpenLink/Overlay/Back or keyboard Navigate/OpenLink"),
				*Binding.TriggerWidgetId, *Binding.TriggerType));
			continue;
		}
		const FUMGPrototypeNavigationStep& Step = Binding.Steps[0];
		const bool bNavigate = Step.Kind == EUMGPrototypeNavigationStepKind::Navigate;

		FResolvedBinding Resolved;
		Resolved.BindingIndex = BindingIndex;

		if (bNavigate)
		{
			if (!Switcher)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': no unique switcher widget '%s' in the built tree"),
					*Binding.TriggerWidgetId, *SwitcherBaseName));
				continue;
			}
			Resolved.TargetPageIndex = FindSwitcherPageIndex(Switcher,
				FUMGWidgetTreeBuilder::SanitizeWidgetName(Step.TargetPageId, TEXT("CanvasPanel")).ToString());
			if (Resolved.TargetPageIndex == INDEX_NONE || !Functions.SetActiveWidgetIndex)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': target page '%s' not uniquely locatable under switcher '%s'"),
					*Binding.TriggerWidgetId, *Step.TargetPageId, *Switcher->GetName()));
				continue;
			}
		}
		else if (Step.Kind == EUMGPrototypeNavigationStepKind::OpenLink && !Functions.LaunchURL)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s': UKismetSystemLibrary::LaunchURL not found"),
				*Binding.TriggerWidgetId));
			continue;
		}
		else if (Step.Kind == EUMGPrototypeNavigationStepKind::Overlay && !OverlayHost)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s': no unique overlay host widget '%s' in the built tree"),
				*Binding.TriggerWidgetId, *NavigationData.OverlayHostWidgetName));
			continue;
		}
		else if (Step.Kind == EUMGPrototypeNavigationStepKind::Overlay)
		{
			// BuildRoot parents every overlay-target page under the host at build time: overlay-only
			// pages by their base name, dual-target/initial pages as a mirror copy named by the base
			// name + OverlayMirrorSuffix (deterministic by construction). Try the base name first,
			// then the mirrored name; the suffix is non-numeric, so the two lookups cannot both hit,
			// and FindOverlayHostPage requires exactly one match either way.
			const FString PageBaseName = FUMGWidgetTreeBuilder::SanitizeWidgetName(
				Step.TargetPageId, TEXT("CanvasPanel")).ToString();
			const UWidget* OverlayPage = FindOverlayHostPage(OverlayHost, PageBaseName);
			if (!OverlayPage)
			{
				OverlayPage = FindOverlayHostPage(OverlayHost,
					FUMGWidgetTreeBuilder::SanitizeWidgetName(
						Step.TargetPageId + Design2UMGPrototypeTokens::OverlayMirrorSuffix(),
						TEXT("CanvasPanel")).ToString());
			}
			if (!OverlayPage)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': overlay target page '%s' not uniquely locatable under overlay host '%s'"),
					*Binding.TriggerWidgetId, *Step.TargetPageId, *OverlayHost->GetName()));
				continue;
			}
			Resolved.OverlayPageWidgetName = OverlayPage->GetFName();
		}

		if (bMouse)
		{
			// Resolve the trigger button in the built tree; node params must come from the built
			// tree, never from the IR names, to stay immune to name drift.
			UButton* TriggerButton = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UButton>(WBP->WidgetTree,
				FUMGWidgetTreeBuilder::SanitizeWidgetName(Binding.TriggerWidgetId, TEXT("Button")).ToString());
			if (!TriggerButton)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s' (%s): no unique button widget in the built tree"),
					*Binding.TriggerWidgetId, *Binding.TriggerType));
				continue;
			}
			Resolved.TriggerWidgetName = TriggerButton->GetFName();
			if (!FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, TriggerButton->GetFName()))
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': button widget '%s' has no skeleton class property"),
					*Binding.TriggerWidgetId, *TriggerButton->GetName()));
				continue;
			}

			Resolved.DelegateName = GetDelegateNameForTrigger(Binding);
			FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(
				UButton::StaticClass(), Resolved.DelegateName);
			if (!DelegateProperty)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': delegate %s not found on UButton"),
					*Binding.TriggerWidgetId, *Resolved.DelegateName.ToString()));
				continue;
			}
			if (FKismetEditorUtilities::FindBoundEventForComponent(WBP, Resolved.DelegateName, TriggerButton->GetFName()))
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': an event is already bound to %s on button '%s'"),
					*Binding.TriggerWidgetId, *Resolved.DelegateName.ToString(), *TriggerButton->GetName()));
				continue;
			}

			// Wire visible overlay mirrors as separate triggers with overlay-context behavior.
			// Missing mirrors, missing class properties and user-bound events leave the original binding intact.
			UButton* MirrorTriggerButton = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UButton>(WBP->WidgetTree,
				FUMGWidgetTreeBuilder::SanitizeWidgetName(
					Binding.TriggerWidgetId + Design2UMGPrototypeTokens::OverlayMirrorSuffix(),
					TEXT("Button")).ToString());
			if (MirrorTriggerButton
				&& FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, MirrorTriggerButton->GetFName())
				&& !FKismetEditorUtilities::FindBoundEventForComponent(WBP, Resolved.DelegateName, MirrorTriggerButton->GetFName()))
			{
				Resolved.MirrorTriggerWidgetName = MirrorTriggerButton->GetFName();
			}

			// Play available Navigate transitions around the page switch. Missing engine functions
			// or animation properties produce a warning and direct navigation.
			if (bNavigate && Binding.Transition.HasGeneratedAnimations())
			{
				if (!Functions.PlayAnimation || !Functions.Delay)
				{
					SkipBinding(OutWarnings, FString::Printf(
						TEXT("trigger '%s': PlayAnimation/Delay engine functions not found; wiring the switch without transition"),
						*Binding.TriggerWidgetId));
				}
				else
				{
					const FObjectProperty* OutgoingProperty = Binding.Transition.OutgoingAnimationName.IsEmpty()
						? nullptr
						: FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, FName(*Binding.Transition.OutgoingAnimationName));
					const FObjectProperty* IncomingProperty = Binding.Transition.IncomingAnimationName.IsEmpty()
						? nullptr
						: FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, FName(*Binding.Transition.IncomingAnimationName));
					if (!OutgoingProperty && !IncomingProperty)
					{
						SkipBinding(OutWarnings, FString::Printf(
							TEXT("trigger '%s': transition animations '%s'/'%s' have no skeleton class properties; wiring the switch without transition"),
							*Binding.TriggerWidgetId,
							*Binding.Transition.OutgoingAnimationName, *Binding.Transition.IncomingAnimationName));
					}
					else
					{
						Resolved.bTransitionChain = true;
						Resolved.DelaySeconds = UMGPrototypeAnimationBuilder::ResolvePhaseDurationSeconds(Binding.Transition);
						if (OutgoingProperty)
						{
							Resolved.OutgoingAnimationName = OutgoingProperty->GetFName();
						}
						if (IncomingProperty)
						{
							Resolved.IncomingAnimationName = IncomingProperty->GetFName();
						}
					}
				}
			}
			MouseBindings.Add(Resolved);
		}
		else
		{
			// Keyboard: the key itself is the trigger, so there is no button to resolve. Resolve the
			// owning page now so each generated key branch can be gated to that page. A page that is
			// in the switcher is matched by active index; a page shown under the overlay host (plain
			// overlay target or the dual-target/initial mirror copy) is matched by its visibility.
			Resolved.SourcePageId = Binding.SourcePageId;
			if (!Binding.SourcePageId.IsEmpty())
			{
				const FString SourcePageBaseName = FUMGWidgetTreeBuilder::SanitizeWidgetName(
					Binding.SourcePageId, TEXT("CanvasPanel")).ToString();
				if (Switcher)
				{
					Resolved.SourceSwitcherPageIndex = FindSwitcherPageIndex(Switcher, SourcePageBaseName);
				}
				if (OverlayHost)
				{
					const UWidget* HostedPage = FindOverlayHostPage(OverlayHost, SourcePageBaseName);
					if (!HostedPage)
					{
						HostedPage = FindOverlayHostPage(OverlayHost,
							FUMGWidgetTreeBuilder::SanitizeWidgetName(
								Binding.SourcePageId + Design2UMGPrototypeTokens::OverlayMirrorSuffix(),
								TEXT("CanvasPanel")).ToString());
					}
					if (HostedPage)
					{
						Resolved.SourceHostedPageWidgetName = HostedPage->GetFName();
					}
				}
			}
			KeyboardBindings.Add(Resolved);
		}
	}

	return {Switcher, OverlayHost, MoveTemp(MouseBindings), MoveTemp(KeyboardBindings), SwitcherBaseName};
}

FBindingGraphTargets PrepareBindingGraphTargets(UWidgetBlueprint* WBP,
	const FUMGPrototypeNavigationData& NavigationData, FBindingPlan& Plan,
	TArray<FString>& OutWarnings)
{
	UWidgetSwitcher* Switcher = Plan.Switcher;
	UOverlay* OverlayHost = Plan.OverlayHost;
	auto& MouseBindings = Plan.MouseBindings;
	auto& KeyboardBindings = Plan.KeyboardBindings;
	// Expose overlay hosts/pages and create the TArray<int32> navigation history before
	// refreshing the skeleton and caching class properties. Reuse a matching history member;
	// a same-named member of another type disables history wiring.
	const auto BindingKind = [&NavigationData](const FResolvedBinding& Resolved)
	{
		return NavigationData.ClickBindings[Resolved.BindingIndex].Steps[0].Kind;
	};
	const auto NeedsHistory = [&BindingKind](const FResolvedBinding& Resolved)
	{
		const EUMGPrototypeNavigationStepKind Kind = BindingKind(Resolved);
		return Kind == EUMGPrototypeNavigationStepKind::Navigate
			|| Kind == EUMGPrototypeNavigationStepKind::Back;
	};
	const auto ReachesOverlayHost = [&BindingKind](const FResolvedBinding& Resolved)
	{
		const EUMGPrototypeNavigationStepKind Kind = BindingKind(Resolved);
		return Kind == EUMGPrototypeNavigationStepKind::Overlay
			|| Kind == EUMGPrototypeNavigationStepKind::Back;
	};

	// The hosted pages are the Back reset list; the overlay chains address them by class property.
	TArray<UWidget*> OverlayHostPages;
	if (OverlayHost)
	{
		for (int32 ChildIndex = 0; ChildIndex < OverlayHost->GetChildrenCount(); ++ChildIndex)
		{
			if (UWidget* HostedPage = OverlayHost->GetChildAt(ChildIndex))
			{
				OverlayHostPages.Add(HostedPage);
			}
		}
	}

	const auto NeedsOverlayHostedPageVariable = [](const FResolvedBinding& Resolved)
	{
		return Resolved.SourceHostedPageWidgetName != NAME_None;
	};
	bool bNeedsSkeletonRefresh = false;
	if (OverlayHost && (MouseBindings.ContainsByPredicate(ReachesOverlayHost)
		|| KeyboardBindings.ContainsByPredicate(NeedsOverlayHostedPageVariable)))
	{
		TArray<UWidget*> OverlayVariableTargets;
		if (!FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, OverlayHost->GetFName()))
		{
			OverlayVariableTargets.Add(OverlayHost);
		}
		for (UWidget* HostedPage : OverlayHostPages)
		{
			if (!FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, HostedPage->GetFName()))
			{
				OverlayVariableTargets.Add(HostedPage);
			}
		}
		for (UWidget* Widget : OverlayVariableTargets)
		{
			Widget->bIsVariable = true;
			bNeedsSkeletonRefresh = true;
		}
	}

	const FName HistoryName(NavHistoryVariableName);
	const bool bAnyHistoryUser = MouseBindings.ContainsByPredicate(NeedsHistory)
		|| KeyboardBindings.ContainsByPredicate(NeedsHistory);
	if (bAnyHistoryUser
		&& !FindFProperty<FArrayProperty>(WBP->SkeletonGeneratedClass, HistoryName))
	{
		const FEdGraphPinType HistoryType(
			UEdGraphSchema_K2::PC_Int, NAME_None, nullptr,
			EPinContainerType::Array, /*bIsReference=*/false, FEdGraphTerminalType());
		if (FBlueprintEditorUtils::AddMemberVariable(WBP, HistoryName, HistoryType))
		{
			bNeedsSkeletonRefresh = true;
		}
	}
	if (bNeedsSkeletonRefresh)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	}

	FArrayProperty* HistoryProperty = nullptr;
	if (bAnyHistoryUser)
	{
		if (FArrayProperty* ResolvedHistory = FindFProperty<FArrayProperty>(
				WBP->SkeletonGeneratedClass, HistoryName))
		{
			HistoryProperty = CastField<FIntProperty>(ResolvedHistory->Inner) ? ResolvedHistory : nullptr;
		}
		if (!HistoryProperty)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("history variable '%s' could not be created/resolved as TArray<int32>; Navigate/Back wiring dropped"),
				NavHistoryVariableName));
			const auto DropHistoryBindings = [&](TArray<FResolvedBinding>& Bindings)
			{
				for (int32 Index = Bindings.Num() - 1; Index >= 0; --Index)
				{
					if (NeedsHistory(Bindings[Index]))
					{
						SkipBinding(OutWarnings, FString::Printf(
							TEXT("trigger '%s': history variable unavailable; Navigate/Back wiring dropped"),
							*NavigationData.ClickBindings[Bindings[Index].BindingIndex].TriggerWidgetId));
						Bindings.RemoveAt(Index);
					}
				}
			};
			DropHistoryBindings(MouseBindings);
			DropHistoryBindings(KeyboardBindings);
		}
	}

	// Pass 3 - author. Class properties are read now, after the possible skeleton refresh above.
	FObjectProperty* SwitcherProperty = Switcher
		? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Switcher->GetFName())
		: nullptr;
	FObjectProperty* OverlayHostProperty = OverlayHost
		? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, OverlayHost->GetFName())
		: nullptr;

	return {HistoryProperty, SwitcherProperty, OverlayHostProperty, MoveTemp(OverlayHostPages)};
}
}
