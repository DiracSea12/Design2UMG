// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "Design2UMGPrototypeDragWiring.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "PencilShaderRuntimeBindings.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTreeBuilder.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanelSlot.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Crc.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{
bool IsSimpleMouseActionBinding(const FUMGPrototypeNavigationBinding& Binding)
{
	if (!Binding.UsesClickTrigger() && !Binding.UsesHoverTrigger() && !Binding.UsesMouseEnterTrigger() && !Binding.UsesPressTrigger() && !Binding.UsesDragTrigger())
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
	case EUMGPrototypeNavigationStepKind::CloseOverlay:
	case EUMGPrototypeNavigationStepKind::Back:
	case EUMGPrototypeNavigationStepKind::AnimateWidget:
		return true;
	default:
		return false;
	}
}

namespace
{
	FName GetDelegateNameForTrigger(const FUMGPrototypeNavigationBinding& Binding)
	{
		if (Binding.UsesHoverTrigger() || Binding.UsesMouseEnterTrigger())
		{
			return GET_MEMBER_NAME_CHECKED(UButton, OnHovered);
		}
		if (Binding.UsesPressTrigger() || Binding.UsesDragTrigger())
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
			|| Kind == EUMGPrototypeNavigationStepKind::OpenLink
			|| Kind == EUMGPrototypeNavigationStepKind::CloseOverlay;
	}

	/** Index of the page widget (by sanitized page id) among the page host's children; INDEX_NONE unless the match is unique. */
	int32 FindPageIndex(const UCanvasPanel* PageHost, const FString& PageBaseName)
	{
		if (!PageHost || PageBaseName.IsEmpty())
		{
			return INDEX_NONE;
		}

		int32 MatchCount = 0;
		int32 MatchedIndex = INDEX_NONE;
		for (int32 ChildIndex = 0; ChildIndex < PageHost->GetChildrenCount(); ++ChildIndex)
		{
			const UWidget* Child = PageHost->GetChildAt(ChildIndex);
			if (Child && FUMGWidgetTreeBuilder::MatchesWidgetBaseName(Child->GetName(), PageBaseName))
			{
				++MatchCount;
				MatchedIndex = ChildIndex;
			}
		}
		return MatchCount == 1 ? MatchedIndex : INDEX_NONE;
	}

	/** Widget base name BuildRoot gave a page (plus an optional mirror suffix); empty for an unknown page. */
	FString PageWidgetBaseName(const FUMGPrototypeNavigationData& NavigationData, const FString& PageId, const TCHAR* Suffix = TEXT(""))
	{
		const FString* Name = NavigationData.PageWidgetNames.Find(PageId);
		return Name ? FUMGWidgetTreeBuilder::SanitizeWidgetName(*Name + Suffix, TEXT("CanvasPanel")).ToString() : FString();
	}

	/**
	 * Unique direct child of the overlay host matching the page base name. BuildRoot parents every
	 * overlay-target page under the host at build time - overlay-only pages under their base name,
	 * dual-target/initial pages as a mirror copy under base name + OverlayMirrorSuffix - so the
	 * overlay binding's page and the Back reset list both resolve here.
	 */
	UWidget* FindOverlayHostPage(const UCanvasPanel* OverlayHost, const FString& PageBaseName)
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
	const FString PageHostBaseName = FUMGWidgetTreeBuilder::SanitizeWidgetName(
		NavigationData.PageHostWidgetName, TEXT("CanvasPanel")).ToString();
	UCanvasPanel* PageHost = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UCanvasPanel>(
		WBP->WidgetTree, PageHostBaseName);
	UCanvasPanel* OverlayHost = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UCanvasPanel>(
		WBP->WidgetTree, FUMGWidgetTreeBuilder::SanitizeWidgetName(
			NavigationData.OverlayHostWidgetName, TEXT("CanvasPanel")).ToString());

	// Pass 1 - resolve every candidate binding without touching the blueprint, so the (potentially
	// skeleton-refreshing) history variable creation below cannot invalidate cached properties.
	TArray<FResolvedBinding> MouseBindings;
	TArray<FResolvedBinding> KeyboardBindings;
	TArray<FResolvedBinding> PageOpenBindings;

	for (int32 BindingIndex = 0; BindingIndex < NavigationData.ClickBindings.Num(); ++BindingIndex)
	{
		const FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings[BindingIndex];
		const bool bMouse = IsSimpleMouseActionBinding(Binding);
		const bool bKeyboard = !bMouse && IsSimpleKeyboardActionBinding(Binding);
		const bool bPageOpen = Binding.UsesPageOpenTrigger() && Binding.Steps.Num() == 1
			&& (Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate
				|| Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::AnimateWidget
				|| Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Overlay
				|| Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::CloseOverlay);
		if (!bMouse && !bKeyboard && !bPageOpen)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s' (%s): binding is not a simple single-action mouse Navigate/OpenLink/Overlay/Back or keyboard Navigate/OpenLink"),
				*Binding.TriggerWidgetId, *Binding.TriggerType));
			continue;
		}
		const FUMGPrototypeNavigationStep& Step = Binding.Steps[0];
		const bool bNavigate = Step.Kind == EUMGPrototypeNavigationStepKind::Navigate;
		if (Binding.UsesDragTrigger() && (Step.Transition.WidgetAnimationName.IsEmpty()
			|| (Step.Kind != EUMGPrototypeNavigationStepKind::Navigate && Step.Kind != EUMGPrototypeNavigationStepKind::AnimateWidget)))
		{
			SkipBinding(OutWarnings, FString::Printf(TEXT("trigger '%s' -> '%s': drag animation unavailable (%s); action not executed"),
				*Binding.TriggerWidgetId, *Step.TargetPageId, Step.Transition.Diagnostic.IsEmpty()
					? TEXT("drag requires a resolved native animation and a page or state target") : *Step.Transition.Diagnostic));
			continue;
		}
		if ((bNavigate || Step.Kind == EUMGPrototypeNavigationStepKind::Overlay)
			&& Step.Transition.IsAnimated() && Step.Transition.Type != TEXT("SMART_ANIMATE")
			&& Step.Transition.WidgetAnimationName.IsEmpty())
		{
			SkipBinding(OutWarnings, FString::Printf(TEXT("trigger '%s': unsupported navigation transition '%s'; action not executed"),
				*Binding.TriggerWidgetId, *Step.Transition.Type));
			continue;
		}
		if ((Step.Transition.Type == TEXT("SMART_ANIMATE") || !Step.Transition.WidgetAnimationName.IsEmpty())
			&& ((!bMouse && !bPageOpen) || (!bNavigate && Step.Kind != EUMGPrototypeNavigationStepKind::AnimateWidget
				&& (Step.Kind != EUMGPrototypeNavigationStepKind::Overlay || Step.Transition.Type == TEXT("SMART_ANIMATE")))))
		{
			SkipBinding(OutWarnings, FString::Printf(TEXT("trigger '%s': property animation supports only mouse/page-entry CHANGE_TO and page Navigate; Overlay and other actions not executed"), *Binding.TriggerWidgetId));
			continue;
		}
		if (!Step.Transition.Diagnostic.IsEmpty()
			|| ((Step.Transition.Type == TEXT("SMART_ANIMATE") || Step.Kind == EUMGPrototypeNavigationStepKind::AnimateWidget)
				&& Step.Transition.WidgetAnimationName.IsEmpty() && !Step.Transition.bStateInstant))
		{
			SkipBinding(OutWarnings, FString::Printf(TEXT("trigger '%s': animation unavailable (%s); action not executed"),
				*Binding.TriggerWidgetId, *Step.Transition.Diagnostic));
			continue;
		}

		FResolvedBinding Resolved;
		Resolved.BindingIndex = BindingIndex;
		Resolved.bDrag = Binding.UsesDragTrigger();
		if (Resolved.bDrag)
		{
			const FName Commit(*FString::Printf(TEXT("__d2u_drag_%03d_commit"), BindingIndex));
			const FName Cancel(*FString::Printf(TEXT("__d2u_drag_%03d_cancel"), BindingIndex));
			if (WBP->SkeletonGeneratedClass->FindFunctionByName(Commit) || WBP->SkeletonGeneratedClass->FindFunctionByName(Cancel)
				|| FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, Commit) || FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, Cancel))
			{
				SkipBinding(OutWarnings, TEXT("drag completion event conflicts with user content"));
				continue;
			}
		}
		Resolved.bTemporaryNavigate = bNavigate && (Binding.UsesHoverTrigger() || Binding.UsesPressTrigger());
		if (!Step.Transition.StateHostWidgetId.IsEmpty())
		{
			Resolved.bTemporaryState = Binding.UsesHoverTrigger() || Binding.UsesPressTrigger();
			if (Resolved.bTemporaryState && Step.Transition.bStateInstant
				&& (!Functions.GetRenderOpacity || !Functions.SetRenderOpacity))
			{
				SkipBinding(OutWarnings, TEXT("temporary state requires native opacity functions"));
				continue;
			}
			if (Resolved.bTemporaryState && !Step.Transition.bStateInstant
				&& Step.Transition.Type != TEXT("SMART_ANIMATE") && Step.Transition.Type != TEXT("DISSOLVE"))
			{
				SkipBinding(OutWarnings, TEXT("temporary state transition requires reversible state lifecycle; action not executed"));
				continue;
			}
			for (int32 HostIndex = 0; HostIndex < NavigationData.StateHosts.Num(); ++HostIndex)
			{
				const auto& Host = NavigationData.StateHosts[HostIndex];
				if (Host.HostWidgetId != Step.Transition.StateHostWidgetId) continue;
				if (Resolved.StateHostIndex != INDEX_NONE) { Resolved.StateHostIndex = INDEX_NONE; break; }
				Resolved.StateHostIndex = HostIndex;
				Resolved.SourceStateIndex = Host.States.IndexOfByPredicate([&](const auto& State) { return State.StateId == Step.Transition.SourceStateId; });
				Resolved.TargetStateIndex = Host.States.IndexOfByPredicate([&](const auto& State) { return State.StateId == Step.Transition.TargetStateId; });
			}
			if (Resolved.StateHostIndex == INDEX_NONE || Resolved.SourceStateIndex == INDEX_NONE || Resolved.TargetStateIndex == INDEX_NONE
				|| Resolved.SourceStateIndex == Resolved.TargetStateIndex)
			{
				SkipBinding(OutWarnings, TEXT("state transition requires one host and unique source/target states"));
				continue;
			}
			if (!Step.Transition.bStateInstant && (!Resolved.bTemporaryState || Step.Transition.Type == TEXT("DISSOLVE")) && !Binding.UsesDragTrigger())
			{
				if (!Functions.SetTimer || !Functions.SetTimerForNextTick || !Functions.ClearTimer)
				{
					SkipBinding(OutWarnings, TEXT("state transition requires native timer functions"));
					continue;
				}
				Resolved.StateCompletionEventName = FName(*FString::Printf(TEXT("__d2u_state_%03d_complete"), BindingIndex));
				if (WBP->SkeletonGeneratedClass->FindFunctionByName(Resolved.StateCompletionEventName))
				{
					SkipBinding(OutWarnings, TEXT("state completion callback conflicts with user content"));
					continue;
				}
			}
		}
		if (!Step.Transition.WidgetAnimationName.IsEmpty())
		{
			if (!Functions.PlayAnimationForward || !Functions.PlayAnimationReverse || !Functions.StopAnimation || !FindFProperty<FObjectProperty>(
				WBP->SkeletonGeneratedClass, FName(*Step.Transition.WidgetAnimationName)))
			{
				SkipBinding(OutWarnings, FString::Printf(TEXT("trigger '%s': native animation function/property unavailable; action not executed"), *Binding.TriggerWidgetId));
				continue;
			}
			Resolved.WidgetAnimationName = FName(*Step.Transition.WidgetAnimationName);
			if (bPageOpen || bNavigate)
			{
				if (!Functions.PlayAnimation || !Functions.SetTimer || !Functions.SetTimerForNextTick || !Functions.ClearTimer)
				{
					SkipBinding(OutWarnings, TEXT("Native property animation timer functions unavailable; action not executed"));
					continue;
				}
				if (bPageOpen) Resolved.PlaybackEventName = FName(*FString::Printf(TEXT("__d2u_anim_%03d_play"), BindingIndex));
				if (bNavigate && !Resolved.bDrag) Resolved.NavigationEventName = FName(*FString::Printf(TEXT("__d2u_anim_%03d_navigate"), BindingIndex));
				if ((Resolved.PlaybackEventName != NAME_None && WBP->SkeletonGeneratedClass->FindFunctionByName(Resolved.PlaybackEventName))
					|| (Resolved.NavigationEventName != NAME_None && WBP->SkeletonGeneratedClass->FindFunctionByName(Resolved.NavigationEventName)))
				{
					SkipBinding(OutWarnings, TEXT("Native property animation callback conflicts with user content; action not executed"));
					continue;
				}
			}
		}

		if (Resolved.bDrag && !bNavigate)
		{
			Resolved.SourcePageIndex = PageHost ? FindPageIndex(PageHost,
				PageWidgetBaseName(NavigationData, Binding.SourcePageId)) : INDEX_NONE;
			if (Resolved.SourcePageIndex == INDEX_NONE)
			{
				SkipBinding(OutWarnings, TEXT("drag requires a uniquely mapped source page for stable pointer geometry"));
				continue;
			}
		}
		if (bNavigate)
		{
			if (!PageHost)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': no unique page host '%s' in the built tree"),
					*Binding.TriggerWidgetId, *PageHostBaseName));
				continue;
			}
			Resolved.TargetPageIndex = FindPageIndex(PageHost,
				PageWidgetBaseName(NavigationData, Step.TargetPageId));
			if (Resolved.TargetPageIndex == INDEX_NONE)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': target page '%s' not uniquely locatable under page host '%s'"),
					*Binding.TriggerWidgetId, *Step.TargetPageId, *PageHost->GetName()));
				continue;
			}
			if (Resolved.WidgetAnimationName != NAME_None || Resolved.bTemporaryNavigate)
			{
				Resolved.SourcePageIndex = FindPageIndex(PageHost,
					PageWidgetBaseName(NavigationData, Binding.SourcePageId));
				if (Resolved.SourcePageIndex == INDEX_NONE || Resolved.SourcePageIndex == Resolved.TargetPageIndex)
				{
					SkipBinding(OutWarnings, TEXT("Native transition source page is missing or equals its target; action not executed"));
					continue;
				}
			}
			if (Resolved.bTemporaryNavigate || (Resolved.WidgetAnimationName != NAME_None && Step.Transition.Type != TEXT("SMART_ANIMATE")))
			{
				auto* Slot = Cast<UCanvasPanelSlot>(PageHost->GetChildAt(Resolved.TargetPageIndex)->Slot);
				if (!Slot) { SkipBinding(OutWarnings, TEXT("temporary navigation requires a native Canvas page slot")); continue; }
				Resolved.TargetPageOriginalZ = Slot->GetZOrder();
				const bool bSourceOnTop = Step.Transition.Type == TEXT("MOVE_OUT") || Step.Transition.Type == TEXT("SLIDE_OUT");
				for (int32 ChildIndex = 0; ChildIndex < PageHost->GetChildrenCount(); ++ChildIndex)
					if (auto* ChildSlot = Cast<UCanvasPanelSlot>(PageHost->GetChildAt(ChildIndex)->Slot))
						Resolved.TemporaryPageZ = bSourceOnTop
							? FMath::Min(Resolved.TemporaryPageZ, ChildSlot->GetZOrder() - 1)
							: FMath::Max(Resolved.TemporaryPageZ, ChildSlot->GetZOrder() + 1);
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
			const FString PageBaseName = PageWidgetBaseName(NavigationData, Step.TargetPageId);
			const UWidget* OverlayPage = FindOverlayHostPage(OverlayHost, PageBaseName);
			if (!OverlayPage)
			{
				OverlayPage = FindOverlayHostPage(OverlayHost,
					PageWidgetBaseName(NavigationData, Step.TargetPageId, Design2UMGPrototypeTokens::OverlayMirrorSuffix()));
			}
			if (!OverlayPage)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': overlay target page '%s' not uniquely locatable under overlay host '%s'"),
					*Binding.TriggerWidgetId, *Step.TargetPageId, *OverlayHost->GetName()));
				continue;
			}
			Resolved.OverlayPageWidgetName = OverlayPage->GetFName();
			Resolved.OverlayPageIndex = OverlayHost->GetChildIndex(OverlayPage);
		}

		if (bPageOpen)
		{
			if (Step.Kind == EUMGPrototypeNavigationStepKind::Overlay
				|| Step.Kind == EUMGPrototypeNavigationStepKind::CloseOverlay || Step.Transition.bStateInstant)
			{
				if (!Functions.SetTimer || !Functions.SetTimerForNextTick || !Functions.ClearTimer)
				{
					SkipBinding(OutWarnings, TEXT("AFTER_TIMEOUT action requires native timer functions"));
					continue;
				}
				if (Resolved.PlaybackEventName.IsNone()) Resolved.PlaybackEventName = FName(*FString::Printf(TEXT("__d2u_anim_%03d_play"), BindingIndex));
				if (WBP->SkeletonGeneratedClass->FindFunctionByName(Resolved.PlaybackEventName))
				{
					SkipBinding(OutWarnings, TEXT("AFTER_TIMEOUT callback conflicts with user content"));
					continue;
				}
			}
			else if (Step.Transition.WidgetAnimationName.IsEmpty())
			{
				SkipBinding(OutWarnings, TEXT("AFTER_TIMEOUT requires a resolved native property animation"));
				continue;
			}
			Resolved.SourcePageIndex = PageHost ? FindPageIndex(PageHost,
				PageWidgetBaseName(NavigationData, Binding.SourcePageId)) : INDEX_NONE;
			if (Resolved.SourcePageIndex == INDEX_NONE || Step.Kind == EUMGPrototypeNavigationStepKind::CloseOverlay)
			{
				const FString SourceName = PageWidgetBaseName(NavigationData, Binding.SourcePageId);
				const UWidget* SourceOverlay = FindOverlayHostPage(OverlayHost, SourceName);
				if (!SourceOverlay)
					SourceOverlay = FindOverlayHostPage(OverlayHost, PageWidgetBaseName(NavigationData, Binding.SourcePageId, Design2UMGPrototypeTokens::OverlayMirrorSuffix()));
				if (SourceOverlay) Resolved.SourceOverlayPageIndex = OverlayHost->GetChildIndex(SourceOverlay);
			}
			if ((PageHost && Resolved.SourcePageIndex == INDEX_NONE && Resolved.SourceOverlayPageIndex == INDEX_NONE)
				|| (Step.Kind == EUMGPrototypeNavigationStepKind::CloseOverlay && Resolved.SourceOverlayPageIndex == INDEX_NONE))
			{
				SkipBinding(OutWarnings, TEXT("AFTER_TIMEOUT source page is not uniquely present in its host"));
				continue;
			}
			Resolved.TriggerDelaySeconds = Binding.TriggerDelaySeconds;
			PageOpenBindings.Add(Resolved);
		}
		else if (bMouse)
		{
			// Resolve the trigger button in the built tree; node params must come from the built
			// tree, never from the IR names, to stay immune to name drift.
			UButton* TriggerButton = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UButton>(WBP->WidgetTree,
				FUMGWidgetTreeBuilder::SanitizeWidgetName(Binding.TriggerWidgetName, TEXT("Button")).ToString());
			if (!TriggerButton)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s' (%s): no unique button widget in the built tree"),
					*Binding.TriggerWidgetId, *Binding.TriggerType));
				continue;
			}
			Resolved.TriggerWidgetName = TriggerButton->GetFName();
			if (bNavigate && Resolved.WidgetAnimationName != NAME_None)
			{
				bool bInsideOverlay = false;
				for (const UWidget* Parent = TriggerButton; Parent; Parent = Parent->GetParent())
					bInsideOverlay |= Parent == OverlayHost;
				if (bInsideOverlay)
				{
					SkipBinding(OutWarnings, FString::Printf(TEXT("trigger '%s': property Navigate from an overlay is not implemented; action not executed"), *Binding.TriggerWidgetId));
					continue;
				}
			}
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
			const bool bReversiblePropertyAnimation = ((Resolved.WidgetAnimationName != NAME_None || Step.Transition.bStateInstant)
				&& Step.Kind == EUMGPrototypeNavigationStepKind::AnimateWidget) || Resolved.bTemporaryNavigate || Resolved.bDrag;
			const bool bHoverOverlay = Binding.UsesHoverTrigger() && Step.Kind == EUMGPrototypeNavigationStepKind::Overlay;
			const bool bPressOverlay = Binding.UsesPressTrigger() && Step.Kind == EUMGPrototypeNavigationStepKind::Overlay;
			Resolved.bPressOverlay = bPressOverlay;
			const FName ReverseDelegate = bReversiblePropertyAnimation && Binding.UsesHoverTrigger()
				? GET_MEMBER_NAME_CHECKED(UButton, OnUnhovered)
				: ((bReversiblePropertyAnimation && (Binding.UsesPressTrigger() || Resolved.bDrag)) || bPressOverlay)
					? GET_MEMBER_NAME_CHECKED(UButton, OnReleased) : NAME_None;
			if (ReverseDelegate != NAME_None
				&& FKismetEditorUtilities::FindBoundEventForComponent(WBP, ReverseDelegate, TriggerButton->GetFName()))
			{
				SkipBinding(OutWarnings, FString::Printf(TEXT("trigger '%s': %s is already bound; preserving user content and refusing the complete reversible animation"), *Binding.TriggerWidgetId, *ReverseDelegate.ToString()));
				continue;
			}
			if (bHoverOverlay)
			{
				if (!Functions.GetCachedGeometry || !Functions.GetMousePositionOnPlatform || !Functions.IsUnderLocation
					|| !Functions.EqualEqual_IntInt || !Functions.SetTimerForNextTick || !Functions.ClearTimer)
				{
					SkipBinding(OutWarnings, TEXT("hover overlay requires native pointer geometry and timer functions"));
					continue;
				}
				Resolved.HoverMonitorEventName = FName(*FString::Printf(TEXT("__d2u_overlay_%03d_monitor"), BindingIndex));
				if (WBP->SkeletonGeneratedClass->FindFunctionByName(Resolved.HoverMonitorEventName))
				{
					SkipBinding(OutWarnings, TEXT("hover overlay callback conflicts with user content"));
					continue;
				}
			}

			// Wire visible overlay mirrors as separate triggers with overlay-context behavior.
			// Missing mirrors, missing class properties and user-bound events leave the original binding intact.
			UButton* MirrorTriggerButton = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UButton>(WBP->WidgetTree,
				FUMGWidgetTreeBuilder::SanitizeWidgetName(
					Binding.TriggerWidgetName + Design2UMGPrototypeTokens::OverlayMirrorSuffix(),
					TEXT("Button")).ToString());
			if (MirrorTriggerButton
				&& FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, MirrorTriggerButton->GetFName())
				&& !FKismetEditorUtilities::FindBoundEventForComponent(WBP, Resolved.DelegateName, MirrorTriggerButton->GetFName())
				&& (ReverseDelegate == NAME_None || !FKismetEditorUtilities::FindBoundEventForComponent(WBP, ReverseDelegate, MirrorTriggerButton->GetFName())))
			{
				if (Resolved.WidgetAnimationName != NAME_None || Resolved.StateHostIndex != INDEX_NONE)
					SkipBinding(OutWarnings, FString::Printf(TEXT("trigger '%s': property animation on an overlay mirror is not implemented; mirror action not executed"), *Binding.TriggerWidgetId));
				else
					Resolved.MirrorTriggerWidgetName = MirrorTriggerButton->GetFName();
			}

			if (bHoverOverlay && !Resolved.MirrorTriggerWidgetName.IsNone())
			{
				const FName MirrorMonitor(*FString::Printf(TEXT("__d2u_overlay_%03d_mirror_monitor"), BindingIndex));
				if (WBP->SkeletonGeneratedClass->FindFunctionByName(MirrorMonitor))
				{
					SkipBinding(OutWarnings, TEXT("hover overlay mirror callback conflicts with user content; original trigger remains wired"));
					Resolved.MirrorTriggerWidgetName = NAME_None;
				}
				else Resolved.MirrorHoverMonitorEventName = MirrorMonitor;
			}

			MouseBindings.Add(Resolved);
		}
		else
		{
			// Keyboard: the key itself is the trigger, so there is no button to resolve. Resolve the
			// owning page now so each generated key branch can be gated to that page. A page that is
			// in the page host is matched by active index; a page shown under the overlay host (plain
			// overlay target or the dual-target/initial mirror copy) is matched by its visibility.
			Resolved.SourcePageId = Binding.SourcePageId;
			if (!Binding.SourcePageId.IsEmpty())
			{
				const FString SourcePageBaseName = PageWidgetBaseName(NavigationData, Binding.SourcePageId);
				if (PageHost)
				{
					Resolved.SourcePageIndex = FindPageIndex(PageHost, SourcePageBaseName);
				}
				if (OverlayHost)
				{
					const UWidget* HostedPage = FindOverlayHostPage(OverlayHost, SourcePageBaseName);
					if (!HostedPage)
					{
						HostedPage = FindOverlayHostPage(OverlayHost,
							PageWidgetBaseName(NavigationData, Binding.SourcePageId, Design2UMGPrototypeTokens::OverlayMirrorSuffix()));
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

	const auto AnimatedProperties = [WBP](FName Name)
	{
		TSet<FString> Result;
		for (const UWidgetAnimation* Animation : WBP->Animations)
		{
			if (!Animation || Animation->GetFName() != Name || !Animation->MovieScene) continue;
			const UMovieScene* Scene = Animation->MovieScene;
			for (const auto& WidgetBinding : Animation->AnimationBindings)
				for (const FMovieSceneBinding& SceneBinding : Scene->GetBindings())
				{
					if (SceneBinding.GetObjectGuid() != WidgetBinding.AnimationGuid) continue;
					for (const UMovieSceneTrack* Track : SceneBinding.GetTracks())
						if (const auto* Property = Cast<UMovieScenePropertyTrack>(Track))
							Result.Add(WidgetBinding.WidgetName.ToString() + TEXT("|") + Property->GetPropertyName().ToString());
				}
		}
		return Result;
	};
	TSet<int32> ConflictingBindings;
	for (int32 A = 0; A < MouseBindings.Num(); ++A)
		for (int32 B = A + 1; B < MouseBindings.Num(); ++B)
			if (MouseBindings[A].TriggerWidgetName == MouseBindings[B].TriggerWidgetName
				&& ((MouseBindings[A].bDrag && NavigationData.ClickBindings[MouseBindings[B].BindingIndex].UsesPressTrigger())
					|| (MouseBindings[B].bDrag && NavigationData.ClickBindings[MouseBindings[A].BindingIndex].UsesPressTrigger())))
			{ ConflictingBindings.Add(A); ConflictingBindings.Add(B); }
	for (int32 A = 0; A < MouseBindings.Num(); ++A)
		for (int32 B = A + 1; B < MouseBindings.Num(); ++B)
		{
			const auto& Left = MouseBindings[A];
			const auto& Right = MouseBindings[B];
			if (Left.TriggerWidgetName != Right.TriggerWidgetName || Left.WidgetAnimationName.IsNone() || Right.WidgetAnimationName.IsNone()) continue;
			const auto& LeftBinding = NavigationData.ClickBindings[Left.BindingIndex];
			const auto& RightBinding = NavigationData.ClickBindings[Right.BindingIndex];
			if (!((LeftBinding.UsesHoverTrigger() && (RightBinding.UsesPressTrigger() || Right.bDrag))
				|| ((LeftBinding.UsesPressTrigger() || Left.bDrag) && RightBinding.UsesHoverTrigger()))) continue;
			const TSet<FString> LeftProperties = AnimatedProperties(Left.WidgetAnimationName);
			const TSet<FString> RightProperties = AnimatedProperties(Right.WidgetAnimationName);
			for (const FString& Property : LeftProperties)
				if (RightProperties.Contains(Property)) { ConflictingBindings.Add(A); ConflictingBindings.Add(B); break; }
		}
	for (int32 Index = MouseBindings.Num() - 1; Index >= 0; --Index)
		if (ConflictingBindings.Contains(Index))
		{
			SkipBinding(OutWarnings, FString::Printf(TEXT("trigger '%s': concurrent source interactions conflict on the native button event or animated property; actions were not wired"),
				*NavigationData.ClickBindings[MouseBindings[Index].BindingIndex].TriggerWidgetId));
			MouseBindings.RemoveAt(Index);
		}

	return {PageHost, OverlayHost, MoveTemp(MouseBindings), MoveTemp(KeyboardBindings), PageHostBaseName, MoveTemp(PageOpenBindings)};
}

FBindingGraphTargets PrepareBindingGraphTargets(UWidgetBlueprint* WBP,
	const FUMGPrototypeNavigationData& NavigationData, FBindingPlan& Plan,
	TArray<FString>& OutWarnings)
{
	UCanvasPanel* PageHost = Plan.PageHost;
	UCanvasPanel* OverlayHost = Plan.OverlayHost;
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
		return Resolved.SourceOverlayPageIndex != INDEX_NONE
			|| Kind == EUMGPrototypeNavigationStepKind::Overlay
			|| Kind == EUMGPrototypeNavigationStepKind::Back
			|| Kind == EUMGPrototypeNavigationStepKind::CloseOverlay;
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
	TArray<FStateHostGraphTargets> StateHostTargets;
	TArray<FName> StateVariableNames;
	TArray<FName> StateOwnerNames;
	TArray<FName> StateOpacityNames;
	StateHostTargets.SetNum(NavigationData.StateHosts.Num());
	StateVariableNames.SetNum(NavigationData.StateHosts.Num());
	StateOwnerNames.SetNum(NavigationData.StateHosts.Num());
	StateOpacityNames.SetNum(NavigationData.StateHosts.Num());
	const FName StateMetadata(TEXT("Design2UMGGeneratedStateHost"));
	const TMap<UWidget*, UWidget*> ProducerPlacements = PencilShaderRuntimeBindings::ProducerPlacements(WBP);
	for (int32 HostIndex = 0; HostIndex < NavigationData.StateHosts.Num(); ++HostIndex)
	{
		const auto& Host = NavigationData.StateHosts[HostIndex];
		auto& Targets = StateHostTargets[HostIndex];
		const int32 Initial = Host.States.IndexOfByPredicate([&](const auto& State) { return State.StateId == Host.InitialStateId; });
		if (Initial == INDEX_NONE || Host.HostWidgetId.IsEmpty()) continue;
		bool bComplete = true;
		UPanelWidget* StateParent = nullptr;
		for (const auto& State : Host.States)
		{
			UWidget* Widget = State.WidgetName.IsEmpty() ? nullptr : WBP->WidgetTree->FindWidget(FName(*State.WidgetName));
			UWidget* OpacityWidget = Widget ? ProducerPlacements.FindRef(Widget) : nullptr;
			if (!OpacityWidget) OpacityWidget = Widget;
			if (!Widget || !OpacityWidget->GetParent() || (StateParent && OpacityWidget->GetParent() != StateParent)
				|| (OpacityWidget != Widget && !FindFProperty<FFloatProperty>(OpacityWidget->GetClass(), TEXT("SourceOpacity")))) { bComplete = false; break; }
			StateParent = OpacityWidget->GetParent();
			Targets.StateWidgets.Add(Widget);
			Targets.OpacityWidgets.Add(OpacityWidget);
			Targets.StateVisibility.Add(State.Visibility);
			if (!FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, OpacityWidget->GetFName()))
			{
				OpacityWidget->bIsVariable = true;
				bNeedsSkeletonRefresh = true;
			}
			if (!FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Widget->GetFName()))
			{
				Widget->bIsVariable = true;
				bNeedsSkeletonRefresh = true;
			}
		}
		if (!bComplete) { Targets.StateWidgets.Reset(); Targets.OpacityWidgets.Reset(); Targets.StateVisibility.Reset(); continue; }
		const FName Name(*FString::Printf(TEXT("__d2u_state_%08x"), FCrc::StrCrc32(*Host.HostWidgetId)));
		const int32 Existing = FBlueprintEditorUtils::FindNewVariableIndex(WBP, Name);
		if (Existing == INDEX_NONE && !FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, Name))
		{
			const FEdGraphPinType Type(UEdGraphSchema_K2::PC_Int, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
			if (!FBlueprintEditorUtils::AddMemberVariable(WBP, Name, Type, FString::FromInt(Initial))) continue;
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, Name, nullptr, StateMetadata, Host.HostWidgetId);
			bNeedsSkeletonRefresh = true;
		}
		else
		{
			FString Owner;
			if (!FBlueprintEditorUtils::GetBlueprintVariableMetaData(WBP, Name, nullptr, StateMetadata, Owner) || Owner != Host.HostWidgetId) continue;
			if (WBP->NewVariables.IsValidIndex(Existing)) WBP->NewVariables[Existing].DefaultValue = FString::FromInt(Initial);
		}
		StateVariableNames[HostIndex] = Name;
		const FName OwnerName(*FString::Printf(TEXT("__d2u_state_owner_%08x"), FCrc::StrCrc32(*Host.HostWidgetId)));
		const int32 ExistingOwner = FBlueprintEditorUtils::FindNewVariableIndex(WBP, OwnerName);
		if (ExistingOwner == INDEX_NONE && !FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, OwnerName))
		{
			const FEdGraphPinType Type(UEdGraphSchema_K2::PC_Int, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
			if (!FBlueprintEditorUtils::AddMemberVariable(WBP, OwnerName, Type)) continue;
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, OwnerName, nullptr, StateMetadata, Host.HostWidgetId);
			bNeedsSkeletonRefresh = true;
		}
		else
		{
			FString Owner;
			if (!FBlueprintEditorUtils::GetBlueprintVariableMetaData(WBP, OwnerName, nullptr, StateMetadata, Owner) || Owner != Host.HostWidgetId) continue;
		}
		StateOwnerNames[HostIndex] = OwnerName;
		const bool bNeedsOpacity = NavigationData.ClickBindings.ContainsByPredicate([&](const auto& Binding)
		{
			return Binding.PrimaryStep().Transition.StateHostWidgetId == Host.HostWidgetId && Binding.PrimaryStep().Transition.bStateInstant
				&& (Binding.UsesHoverTrigger() || Binding.UsesPressTrigger());
		});
		if (bNeedsOpacity)
		{
			const FName OpacityName(*FString::Printf(TEXT("__d2u_state_opacity_%08x"), FCrc::StrCrc32(*Host.HostWidgetId)));
			const int32 ExistingOpacity = FBlueprintEditorUtils::FindNewVariableIndex(WBP, OpacityName);
			if (ExistingOpacity == INDEX_NONE && !FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, OpacityName))
			{
				FEdGraphPinType Type;
				Type.PinCategory = UEdGraphSchema_K2::PC_Real;
				Type.PinSubCategory = UEdGraphSchema_K2::PC_Float;
				if (!FBlueprintEditorUtils::AddMemberVariable(WBP, OpacityName, Type, TEXT("1.0"))) continue;
				FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, OpacityName, nullptr, StateMetadata, Host.HostWidgetId);
				bNeedsSkeletonRefresh = true;
			}
			else
			{
				FString Owner;
				if (!FBlueprintEditorUtils::GetBlueprintVariableMetaData(WBP, OpacityName, nullptr, StateMetadata, Owner) || Owner != Host.HostWidgetId) continue;
			}
			StateOpacityNames[HostIndex] = OpacityName;
		}
	}
	TArray<UWidget*> PageHostPages;
	if (PageHost)
	{
		for (int32 Index = 0; Index < PageHost->GetChildrenCount(); ++Index)
		{
			if (UWidget* Page = PageHost->GetChildAt(Index))
			{
				PageHostPages.Add(Page);
				if (!FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Page->GetFName()))
				{
					Page->bIsVariable = true;
					bNeedsSkeletonRefresh = true;
				}
			}
		}
	}
	const FName CurrentPageName(TEXT("__d2u_current_page"));
	const FName GeneratedPageMetadata(TEXT("Design2UMGGeneratedCurrentPage"));
	bool bCurrentPageAvailable = false;
	if (PageHost)
	{
		if (FBlueprintEditorUtils::FindNewVariableIndex(WBP, CurrentPageName) == INDEX_NONE
			&& !FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, CurrentPageName))
		{
			const FEdGraphPinType PageType(UEdGraphSchema_K2::PC_Int, NAME_None, nullptr,
				EPinContainerType::None, false, FEdGraphTerminalType());
			if (FBlueprintEditorUtils::AddMemberVariable(WBP, CurrentPageName, PageType,
				FString::FromInt(NavigationData.InitialPageIndex)))
			{
				FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, CurrentPageName, nullptr, GeneratedPageMetadata, TEXT("1"));
				bNeedsSkeletonRefresh = true;
				bCurrentPageAvailable = true;
			}
		}
		else
		{
			FString Metadata;
			bCurrentPageAvailable = FBlueprintEditorUtils::GetBlueprintVariableMetaData(WBP, CurrentPageName, nullptr, GeneratedPageMetadata, Metadata)
				&& Metadata == TEXT("1") && FindFProperty<FIntProperty>(WBP->SkeletonGeneratedClass, CurrentPageName);
			const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(WBP, CurrentPageName);
			if (bCurrentPageAvailable && WBP->NewVariables.IsValidIndex(VariableIndex))
			{
				WBP->NewVariables[VariableIndex].DefaultValue = FString::FromInt(NavigationData.InitialPageIndex);
			}
		}
	}
	const FName OverlayOwnerName(TEXT("__d2u_overlay_owner"));
	const FName GeneratedOwnerMetadata(TEXT("Design2UMGGeneratedOverlayOwner"));
	const FName TransitionOwnerName(TEXT("__d2u_transition_owner"));
	const FName GeneratedTransitionMetadata(TEXT("Design2UMGGeneratedTransitionOwner"));
	const auto NeedsTransitionOwner = [&NavigationData](const FResolvedBinding& Resolved)
	{
		const auto& Binding = NavigationData.ClickBindings[Resolved.BindingIndex];
		return Resolved.bTemporaryNavigate || Resolved.bDrag || (Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate
			&& Binding.PrimaryStep().Transition.Type != TEXT("SMART_ANIMATE") && Resolved.WidgetAnimationName != NAME_None);
	};
	const bool bNeedsTransitionOwner = MouseBindings.ContainsByPredicate(NeedsTransitionOwner)
		|| Plan.PageOpenBindings.ContainsByPredicate(NeedsTransitionOwner);
	bool bTransitionOwnerAvailable = false;
	if (bNeedsTransitionOwner)
	{
		if (FBlueprintEditorUtils::FindNewVariableIndex(WBP, TransitionOwnerName) == INDEX_NONE
			&& !FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, TransitionOwnerName))
		{
			const FEdGraphPinType OwnerType(UEdGraphSchema_K2::PC_Int, NAME_None, nullptr,
				EPinContainerType::None, false, FEdGraphTerminalType());
			if (FBlueprintEditorUtils::AddMemberVariable(WBP, TransitionOwnerName, OwnerType))
			{
				FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, TransitionOwnerName, nullptr, GeneratedTransitionMetadata, TEXT("1"));
				bNeedsSkeletonRefresh = true;
				bTransitionOwnerAvailable = true;
			}
		}
		else
		{
			FString Metadata;
			bTransitionOwnerAvailable = FBlueprintEditorUtils::GetBlueprintVariableMetaData(WBP, TransitionOwnerName, nullptr, GeneratedTransitionMetadata, Metadata)
				&& Metadata == TEXT("1") && FindFProperty<FIntProperty>(WBP->SkeletonGeneratedClass, TransitionOwnerName);
		}
	}
	const bool bNeedsOverlayOwner = MouseBindings.ContainsByPredicate([&](const FResolvedBinding& Resolved)
	{
		const auto& Binding = NavigationData.ClickBindings[Resolved.BindingIndex];
		return (Binding.UsesHoverTrigger() || Binding.UsesPressTrigger())
			&& Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Overlay;
	});
	bool bOwnerAvailable = false;
	if (bNeedsOverlayOwner)
	{
		if (FBlueprintEditorUtils::FindNewVariableIndex(WBP, OverlayOwnerName) == INDEX_NONE
			&& !FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, OverlayOwnerName))
		{
			const FEdGraphPinType OwnerType(UEdGraphSchema_K2::PC_Int, NAME_None, nullptr,
				EPinContainerType::None, false, FEdGraphTerminalType());
			if (FBlueprintEditorUtils::AddMemberVariable(WBP, OverlayOwnerName, OwnerType))
			{
				FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, OverlayOwnerName, nullptr, GeneratedOwnerMetadata, TEXT("1"));
				bNeedsSkeletonRefresh = true;
				bOwnerAvailable = true;
			}
		}
		else
		{
			FString Metadata;
			bOwnerAvailable = FBlueprintEditorUtils::GetBlueprintVariableMetaData(WBP, OverlayOwnerName, nullptr, GeneratedOwnerMetadata, Metadata)
				&& Metadata == TEXT("1") && FindFProperty<FIntProperty>(WBP->SkeletonGeneratedClass, OverlayOwnerName);
		}
	}
	if (OverlayHost && (MouseBindings.ContainsByPredicate(ReachesOverlayHost)
		|| Plan.PageOpenBindings.ContainsByPredicate(ReachesOverlayHost)
		|| KeyboardBindings.ContainsByPredicate(ReachesOverlayHost)
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
	const FName OverlayStackName(TEXT("__d2u_overlay_stack"));
	const FName OverlayStackMetadata(TEXT("Design2UMGGeneratedOverlayStack"));
	bool bOverlayStackAvailable = false;
	if (OverlayHost)
	{
		const int32 Existing = FBlueprintEditorUtils::FindNewVariableIndex(WBP, OverlayStackName);
		if (Existing == INDEX_NONE && !FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, OverlayStackName))
		{
			const FEdGraphPinType Type(UEdGraphSchema_K2::PC_Int, NAME_None, nullptr,
				EPinContainerType::Array, false, FEdGraphTerminalType());
			if (FBlueprintEditorUtils::AddMemberVariable(WBP, OverlayStackName, Type))
			{
				FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, OverlayStackName, nullptr, OverlayStackMetadata, TEXT("1"));
				bNeedsSkeletonRefresh = true;
				bOverlayStackAvailable = true;
			}
		}
		else
		{
			FString Marker;
			bOverlayStackAvailable = FBlueprintEditorUtils::GetBlueprintVariableMetaData(WBP, OverlayStackName, nullptr, OverlayStackMetadata, Marker)
				&& Marker == TEXT("1");
		}
	}
	const FName OverlayZOrderName(TEXT("__d2u_overlay_zorder"));
	const FName OverlayZOrderMetadata(TEXT("Design2UMGGeneratedOverlayZOrder"));
	if (OverlayHost && FBlueprintEditorUtils::FindNewVariableIndex(WBP, OverlayZOrderName) == INDEX_NONE
		&& !FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, OverlayZOrderName))
	{
		const FEdGraphPinType Type(UEdGraphSchema_K2::PC_Int, NAME_None, nullptr,
			EPinContainerType::None, false, FEdGraphTerminalType());
		if (FBlueprintEditorUtils::AddMemberVariable(WBP, OverlayZOrderName, Type))
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, OverlayZOrderName, nullptr, OverlayZOrderMetadata, TEXT("1"));
			bNeedsSkeletonRefresh = true;
		}
	}
	const bool bAnyHistoryUser = MouseBindings.ContainsByPredicate(NeedsHistory)
		|| KeyboardBindings.ContainsByPredicate(NeedsHistory) || Plan.PageOpenBindings.ContainsByPredicate(NeedsHistory);
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
	const auto ResolveLayerWidgets = [&](TArray<FResolvedBinding>& Bindings)
	{
		for (int32 Index = Bindings.Num() - 1; Index >= 0; --Index)
		{
			auto& Resolved = Bindings[Index];
			const auto& Orders = NavigationData.ClickBindings[Resolved.BindingIndex].PrimaryStep().Transition.LayerOrders;
			bool bValid = true;
			FString LayerFailure;
			for (const auto& Order : Orders)
			{
				UWidget* Widget = Order.WidgetName.IsEmpty() ? nullptr : WBP->WidgetTree->FindWidget(FName(*Order.WidgetName));
				UWidget* Parent = Order.ParentWidgetName.IsEmpty() ? nullptr : WBP->WidgetTree->FindWidget(FName(*Order.ParentWidgetName));
				if (!Widget || !Parent || Widget->GetParent() != Parent || !Cast<UCanvasPanelSlot>(Widget->Slot)
					|| Order.StartIndex == INDEX_NONE || Order.EndIndex == INDEX_NONE)
				{
					LayerFailure = FString::Printf(TEXT("node='%s' mapped='%s' parent='%s' actual_parent='%s' slot='%s' indices=%d/%d"),
						*Order.NodeId, *Order.WidgetName, *Order.ParentWidgetName,
						Widget && Widget->GetParent() ? *Widget->GetParent()->GetName() : TEXT("<none>"),
						Widget && Widget->Slot ? *Widget->Slot->GetClass()->GetName() : TEXT("<none>"), Order.StartIndex, Order.EndIndex);
					bValid = false; break;
				}
				Resolved.LayerWidgetNames.Add(Widget->GetFName());
				Resolved.LayerOriginalZOrders.Add(CastChecked<UCanvasPanelSlot>(Widget->Slot)->GetZOrder());
				if (!FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Widget->GetFName()))
				{
					Widget->bIsVariable = true;
					bNeedsSkeletonRefresh = true;
				}
			}
			if (!bValid)
			{
				SkipBinding(OutWarnings, FString::Printf(TEXT("layer order requires uniquely mapped native Canvas children: %s"), *LayerFailure));
				Bindings.RemoveAt(Index);
			}
		}
	};
	ResolveLayerWidgets(MouseBindings);
	ResolveLayerWidgets(Plan.PageOpenBindings);
	TMap<FName, int32> DragLeaders;
	TSet<FName> InvalidDragGroups;
	for (auto& Resolved : MouseBindings)
	{
		if (!Resolved.bDrag) continue;
		if (const int32* Leader = DragLeaders.Find(Resolved.TriggerWidgetName)) Resolved.DragLeaderIndex = *Leader;
		else
		{
			Resolved.DragLeaderIndex = Resolved.BindingIndex;
			DragLeaders.Add(Resolved.TriggerWidgetName, Resolved.BindingIndex);
			const auto& Binding = NavigationData.ClickBindings[Resolved.BindingIndex];
			const FString Identity = GetDragStableIdentity(Binding);
			if (!PrepareDragVariables(WBP, Resolved.BindingIndex, Identity, OutWarnings)) InvalidDragGroups.Add(Resolved.TriggerWidgetName);
			else bNeedsSkeletonRefresh = true;
		}
	}
	MouseBindings.RemoveAll([&](const FResolvedBinding& Resolved)
	{
		return Resolved.bDrag && InvalidDragGroups.Contains(Resolved.TriggerWidgetName);
	});
	for (auto& Resolved : MouseBindings)
	{
		if (!NavigationData.ClickBindings[Resolved.BindingIndex].UsesClickTrigger()) continue;
		if (const int32* Leader = DragLeaders.Find(Resolved.TriggerWidgetName))
			if (!InvalidDragGroups.Contains(Resolved.TriggerWidgetName)) Resolved.DragAcceptedPropertyName = GetDragAcceptedName(*Leader);
	}
	const auto PrepareLiveBindings = [&](TArray<FResolvedBinding>& Bindings)
	{
		for (int32 Index = Bindings.Num() - 1; Index >= 0; --Index)
		{
			auto& Resolved = Bindings[Index];
			const auto& Records = NavigationData.ClickBindings[Resolved.BindingIndex].PrimaryStep().Transition.LivePresentations;
			TSet<UWidget*> LiveWidgets;
			bool bValid = true;
			for (const auto& Live : Records)
			{
				UWidget* Widget = WBP->WidgetTree->FindWidget(FName(*Live.WidgetName));
				auto* Original = Cast<USizeBox>(WBP->WidgetTree->FindWidget(FName(*Live.OriginalSocketWidgetName)));
				auto* Presentation = Cast<USizeBox>(WBP->WidgetTree->FindWidget(FName(*Live.PresentationSocketWidgetName)));
				if (!Widget || !Original || !Presentation || Original == Presentation
					|| Original->GetContent() != Widget || Presentation->GetContent() || LiveWidgets.Contains(Widget))
				{
					bValid = false; break;
				}
				LiveWidgets.Add(Widget);
				Resolved.LivePresentations.Add({Widget->GetFName(), Original->GetFName(), Presentation->GetFName()});
				for (UWidget* Item : {Widget, static_cast<UWidget*>(Original), static_cast<UWidget*>(Presentation)})
					if (!FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Item->GetFName()))
					{
						Item->bIsVariable = true;
						bNeedsSkeletonRefresh = true;
					}
			}
			if (!bValid)
			{
				SkipBinding(OutWarnings, TEXT("live presentation requires one native subtree in its original socket and one empty presentation socket"));
				Bindings.RemoveAt(Index); continue;
			}
			const auto& Transition = NavigationData.ClickBindings[Resolved.BindingIndex].PrimaryStep().Transition;
			if (!Records.IsEmpty() && Transition.bFixedViewportLayout)
			{
				Resolved.LayoutOriginalWidgetName = FName(*Transition.LayoutOriginalWidgetName);
				Resolved.LayoutPresentationWidgetName = FName(*Transition.LayoutPresentationWidgetName);
				Resolved.LayoutOriginalOpacity = Transition.LayoutOriginalOpacity;
				for (const FName Name : {Resolved.LayoutOriginalWidgetName, Resolved.LayoutPresentationWidgetName})
				{
					UWidget* Widget = WBP->WidgetTree->FindWidget(Name);
					if (!Widget) { bValid = false; break; }
					if (!FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Name))
					{ Widget->bIsVariable = true; bNeedsSkeletonRefresh = true; }
				}
				if (!bValid) { SkipBinding(OutWarnings, TEXT("live layout presentation has unresolved native paint owners")); Bindings.RemoveAt(Index); continue; }
			}
			if (!Records.IsEmpty() && Resolved.bTemporaryState)
				Resolved.LiveReturnEventName = BindingFunctionName(TEXT("live_return"), Resolved.BindingIndex);
		}
	};
	PrepareLiveBindings(MouseBindings);
	PrepareLiveBindings(Plan.PageOpenBindings);
	const FName SourceTimeMetadata(TEXT("Design2UMGGeneratedSourceAnimationTime"));
	for (FResolvedBinding& Resolved : MouseBindings)
	{
		const auto& Binding = NavigationData.ClickBindings[Resolved.BindingIndex];
		if ((!Binding.UsesHoverTrigger() && !Binding.UsesPressTrigger() && !Resolved.bDrag) || Resolved.WidgetAnimationName.IsNone()
			|| (Binding.Steps[0].Kind != EUMGPrototypeNavigationStepKind::AnimateWidget && !Resolved.bTemporaryNavigate && !Resolved.bDrag)) continue;
		Resolved.PlaybackTargetPropertyName = FName(*FString::Printf(TEXT("__d2u_source_%03d_target"), Resolved.BindingIndex));
		Resolved.PlaybackStartPropertyName = FName(*FString::Printf(TEXT("__d2u_source_%03d_start"), Resolved.BindingIndex));
		for (const FName Name : {Resolved.PlaybackTargetPropertyName, Resolved.PlaybackStartPropertyName})
		{
			const int32 Existing = FBlueprintEditorUtils::FindNewVariableIndex(WBP, Name);
			if (Existing == INDEX_NONE && !FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, Name))
			{
				FEdGraphPinType Type;
				Type.PinCategory = UEdGraphSchema_K2::PC_Real;
				Type.PinSubCategory = UEdGraphSchema_K2::PC_Float;
				if (FBlueprintEditorUtils::AddMemberVariable(WBP, Name, Type, TEXT("0.0")))
				{
					FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, Name, nullptr, SourceTimeMetadata, TEXT("1"));
					bNeedsSkeletonRefresh = true;
				}
			}
		}
	}
    const TArray<FName> FunctionNames = GetMouseFunctionNames(NavigationData, Plan);
    for (FName Name : FunctionNames)
    {
        if (WBP->SkeletonGeneratedClass->FindFunctionByName(Name)
            || FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, Name)
            || WBP->FunctionGraphs.ContainsByPredicate([&](const UEdGraph* Graph) { return Graph->GetFName() == Name; }))
        {
            SkipBinding(OutWarnings, FString::Printf(TEXT("Synchronous prototype function '%s' conflicts with user content; bindings not wired"), *Name.ToString()));
            Plan.MouseBindings.Reset();
            Plan.PageOpenBindings.Reset();
            return FBindingGraphTargets();
        }
    }
    for (FName Name : FunctionNames)
    {
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(WBP, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        // Register the synchronous bodies before the unified skeleton refresh below.
        FBlueprintEditorUtils::CreateFunctionGraph<UClass>(WBP, Graph, true, nullptr);
        WBP->FunctionGraphs.Add(Graph);
        FBlueprintEditorUtils::ValidateBlueprintChildVariables(WBP, Graph->GetFName());
        if (Name == BindingFunctionName(TEXT("state_visible"), 0))
        {
            auto* Entry = CastChecked<UK2Node_FunctionEntry>(Graph->Nodes[0]);
            Entry->SetExtraFlags(Entry->GetExtraFlags() | FUNC_BlueprintPure);
            const FEdGraphPinType WidgetType(UEdGraphSchema_K2::PC_Object, NAME_None, UWidget::StaticClass(), EPinContainerType::None, false, FEdGraphTerminalType());
            Entry->CreateUserDefinedPin(TEXT("Widget"), WidgetType, EGPD_Output);
            FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*Graph);
            auto* Result = ResultCreator.CreateNode(false);
            ResultCreator.Finalize();
            Result->NodeComment = NodeMarkerComment;
            const FEdGraphPinType BoolType(UEdGraphSchema_K2::PC_Boolean, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
            Result->CreateUserDefinedPin(UEdGraphSchema_K2::PN_ReturnValue, BoolType, EGPD_Input, false);
            Result->ReconstructNode();
        }
        for (UEdGraphNode* Node : Graph->Nodes) Node->NodeComment = NodeMarkerComment;
    }
    if (!FunctionNames.IsEmpty()) bNeedsSkeletonRefresh = true;
	if (bNeedsSkeletonRefresh)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	}
	for (int32 Index = MouseBindings.Num() - 1; Index >= 0; --Index)
	{
		const auto& Resolved = MouseBindings[Index];
		if (Resolved.PlaybackTargetPropertyName.IsNone()) continue;
		bool bValid = true;
		for (const FName Name : {Resolved.PlaybackTargetPropertyName, Resolved.PlaybackStartPropertyName})
		{
			FString Marker;
			bValid &= FBlueprintEditorUtils::GetBlueprintVariableMetaData(WBP, Name, nullptr, SourceTimeMetadata, Marker)
				&& Marker == TEXT("1") && FindFProperty<FFloatProperty>(WBP->SkeletonGeneratedClass, Name);
		}
		if (!bValid)
		{
			SkipBinding(OutWarnings, TEXT("source animation time state conflicts with user content"));
			MouseBindings.RemoveAt(Index);
		}
	}
	for (int32 HostIndex = 0; HostIndex < StateHostTargets.Num(); ++HostIndex)
	{
		if (!StateVariableNames[HostIndex].IsNone())
			StateHostTargets[HostIndex].CurrentStateProperty = FindFProperty<FIntProperty>(WBP->SkeletonGeneratedClass, StateVariableNames[HostIndex]);
		if (!StateOwnerNames[HostIndex].IsNone())
			StateHostTargets[HostIndex].TransitionOwnerProperty = FindFProperty<FIntProperty>(WBP->SkeletonGeneratedClass, StateOwnerNames[HostIndex]);
		if (!StateOpacityNames[HostIndex].IsNone())
			StateHostTargets[HostIndex].SavedSourceOpacityProperty = FindFProperty<FFloatProperty>(WBP->SkeletonGeneratedClass, StateOpacityNames[HostIndex]);
	}
	const auto DropUnavailableStates = [&](TArray<FResolvedBinding>& Bindings)
	{
		for (int32 Index = Bindings.Num() - 1; Index >= 0; --Index)
		{
			const int32 HostIndex = Bindings[Index].StateHostIndex;
			if (HostIndex != INDEX_NONE && (!StateHostTargets.IsValidIndex(HostIndex)
				|| !StateHostTargets[HostIndex].CurrentStateProperty || !StateHostTargets[HostIndex].TransitionOwnerProperty
				|| (Bindings[Index].bTemporaryState && NavigationData.ClickBindings[Bindings[Index].BindingIndex].PrimaryStep().Transition.bStateInstant
					&& !StateHostTargets[HostIndex].SavedSourceOpacityProperty)
				|| !StateHostTargets[HostIndex].StateWidgets.IsValidIndex(Bindings[Index].SourceStateIndex)
				|| !StateHostTargets[HostIndex].StateWidgets.IsValidIndex(Bindings[Index].TargetStateIndex)))
			{
				SkipBinding(OutWarnings, TEXT("state host has unresolved widgets or conflicts with user state"));
				Bindings.RemoveAt(Index);
			}
		}
	};
	DropUnavailableStates(MouseBindings);
	DropUnavailableStates(Plan.PageOpenBindings);
	FIntProperty* OverlayOwnerProperty = bOwnerAvailable
		? FindFProperty<FIntProperty>(WBP->SkeletonGeneratedClass, OverlayOwnerName) : nullptr;
	if (bNeedsOverlayOwner && !OverlayOwnerProperty)
	{
		for (int32 Index = MouseBindings.Num() - 1; Index >= 0; --Index)
		{
			const auto& Binding = NavigationData.ClickBindings[MouseBindings[Index].BindingIndex];
			if ((Binding.UsesHoverTrigger() || Binding.UsesPressTrigger())
				&& Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Overlay)
			{
				SkipBinding(OutWarnings, FString::Printf(TEXT("trigger '%s': overlay owner variable unavailable or conflicts with user content"), *Binding.TriggerWidgetId));
				MouseBindings.RemoveAt(Index);
			}
		}
	}
	FIntProperty* TransitionOwnerProperty = bTransitionOwnerAvailable
		? FindFProperty<FIntProperty>(WBP->SkeletonGeneratedClass, TransitionOwnerName) : nullptr;
	if (bNeedsTransitionOwner && !TransitionOwnerProperty)
	{
		const auto DropTransition = [&](TArray<FResolvedBinding>& Bindings)
		{
			for (int32 Index = Bindings.Num() - 1; Index >= 0; --Index)
			{
				if (NeedsTransitionOwner(Bindings[Index]))
				{
					SkipBinding(OutWarnings, TEXT("page transition owner conflicts with user content"));
					Bindings.RemoveAt(Index);
				}
			}
		};
		DropTransition(MouseBindings);
		DropTransition(Plan.PageOpenBindings);
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
			DropHistoryBindings(Plan.PageOpenBindings);
		}
	}

	// Pass 3 - author. Class properties are read now, after the possible skeleton refresh above.
	FIntProperty* CurrentPageProperty = bCurrentPageAvailable
		? FindFProperty<FIntProperty>(WBP->SkeletonGeneratedClass, CurrentPageName) : nullptr;
	if (PageHost && !CurrentPageProperty)
	{
		SkipBinding(OutWarnings, TEXT("page index variable unavailable or conflicts with user content; navigation wiring dropped"));
		Plan.MouseBindings.Reset();
		Plan.KeyboardBindings.Reset();
		Plan.PageOpenBindings.Reset();
	}
	FObjectProperty* OverlayHostProperty = OverlayHost
		? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, OverlayHost->GetFName())
		: nullptr;

	FArrayProperty* OverlayStackProperty = bOverlayStackAvailable
		? FindFProperty<FArrayProperty>(WBP->SkeletonGeneratedClass, OverlayStackName) : nullptr;
	FString OverlayZOrderMarker;
	FIntProperty* OverlayZOrderProperty = FBlueprintEditorUtils::GetBlueprintVariableMetaData(
		WBP, OverlayZOrderName, nullptr, OverlayZOrderMetadata, OverlayZOrderMarker) && OverlayZOrderMarker == TEXT("1")
		? FindFProperty<FIntProperty>(WBP->SkeletonGeneratedClass, OverlayZOrderName) : nullptr;
	if (OverlayHost && (!OverlayStackProperty || !CastField<FIntProperty>(OverlayStackProperty->Inner) || !OverlayZOrderProperty))
	{
		SkipBinding(OutWarnings, TEXT("overlay stack conflicts with user content; overlay actions are not wired"));
		auto DropOverlay = [&](TArray<FResolvedBinding>& Bindings)
		{
			Bindings.RemoveAll([&](const FResolvedBinding& Resolved) { return ReachesOverlayHost(Resolved); });
		};
		DropOverlay(Plan.MouseBindings);
		DropOverlay(Plan.KeyboardBindings);
		DropOverlay(Plan.PageOpenBindings);
		OverlayStackProperty = nullptr;
	}
	TArray<FResolvedBinding> OverlayPageOpenBindings;
	for (const auto& Resolved : Plan.PageOpenBindings)
		if (Resolved.SourceOverlayPageIndex != INDEX_NONE) OverlayPageOpenBindings.Add(Resolved);
	return {HistoryProperty, CurrentPageProperty, MoveTemp(PageHostPages), NavigationData.PageDisplayVisibility,
		OverlayHostProperty, MoveTemp(OverlayHostPages), OverlayStackProperty, OverlayZOrderProperty, OverlayOwnerProperty, TransitionOwnerProperty, MoveTemp(StateHostTargets), MoveTemp(OverlayPageOpenBindings)};
}
}
