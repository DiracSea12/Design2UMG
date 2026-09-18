// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "UMGSchemaSourceExtensions.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/WidgetSwitcher.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{
namespace
{
	/** True when Widget is Ancestor or sits in its subtree; both are tree widgets (same WidgetTree). */
	bool IsWidgetInsideWidget(const UWidget* Widget, const UWidget* Ancestor)
	{
		for (const UWidget* Current = Widget; Current; Current = Current->GetParent())
		{
			if (Current == Ancestor)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Appends a PlayAnimation call fed by the animation's class property (animations become named
	 * object properties on the compiled widget class, same VariableGet recipe as widget variables).
	 */
	void AppendPlayAnimationCall(
		UEdGraph* Graph,
		UWidgetBlueprint* WBP,
		const FEngineFunctions& Functions,
		const FName AnimationPropertyName,
		FChainLayout& Layout)
	{
		FObjectProperty* AnimationProperty = FindFProperty<FObjectProperty>(
			WBP->SkeletonGeneratedClass, AnimationPropertyName);
		if (!AnimationProperty)
		{
			return;
		}
		UK2Node_VariableGet* AnimationGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, AnimationProperty));
		UK2Node_CallFunction* PlayNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
			[&Functions](UK2Node_CallFunction* Node)
			{
				Node->SetFromFunction(Functions.PlayAnimation);
			});
		Layout.PlaceExec(PlayNode);
		// PlayAnimation runs on the UserWidget self; only the animation pin needs an explicit link.
		if (UEdGraphPin* AnimationPin = PlayNode->FindPin(TEXT("InAnimation"), EGPD_Input))
		{
			AnimationGet->GetValuePin()->MakeLinkTo(AnimationPin);
		}
	}

	/**
	 * Appends UUserWidget::StopAnimation(out) fed by the animation's class property, immediately
	 * before the outgoing-only translation reset. PlayAnimation(out) and the Delay share the same
	 * duration and start on the same exec tick, and the animation only starts advancing on the next
	 * frame while the latent manager advances on the click frame already - so the animation's final
	 * evaluation (the off-screen key) lands after the reset and re-overwrites the translation.
	 * StopAnimation parks the animation state so no further evaluation can fight the reset (stopping
	 * an already finished animation is a no-op).
	 */
	bool AppendStopAnimationCall(
		UEdGraph* Graph,
		UWidgetBlueprint* WBP,
		const FEngineFunctions& Functions,
		const FName AnimationPropertyName,
		FChainLayout& Layout)
	{
		if (!Functions.StopAnimation)
		{
			return false;
		}
		FObjectProperty* AnimationProperty = FindFProperty<FObjectProperty>(
			WBP->SkeletonGeneratedClass, AnimationPropertyName);
		if (!AnimationProperty)
		{
			return false;
		}
		UK2Node_VariableGet* AnimationGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, AnimationProperty));
		UK2Node_CallFunction* StopNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
			[&Functions](UK2Node_CallFunction* Node)
			{
				Node->SetFromFunction(Functions.StopAnimation);
			});
		Layout.PlaceExec(StopNode);
		if (UEdGraphPin* AnimationPin = StopNode->FindPin(TEXT("InAnimation"), EGPD_Input))
		{
			AnimationGet->GetValuePin()->MakeLinkTo(AnimationPin);
		}
		return true;
	}

	/**
	 * Appends a UWidget::SetVisibility call on a widget variable get. The overlay encoding is
	 * host + page visibility: BuildRoot parents the overlay-target pages under the host (Collapsed),
	 * so opening shows the host then the page (both SelfHitTestInvisible) and Back collapses the host
	 * plus every hosted page (a later different overlay then opens clean).
	 */
	bool AppendSetVisibilityCall(
		UEdGraph* Graph,
		UWidgetBlueprint* WBP,
		const FEngineFunctions& Functions,
		FObjectProperty* WidgetProperty,
		ESlateVisibility Visibility,
		FChainLayout& Layout)
	{
		if (!Functions.SetVisibility || !WidgetProperty)
		{
			return false;
		}
		UK2Node_VariableGet* WidgetGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, WidgetProperty));
		UK2Node_CallFunction* VisibilityNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
			[&Functions](UK2Node_CallFunction* Node)
			{
				Node->SetFromFunction(Functions.SetVisibility);
			});
		Layout.PlaceExec(VisibilityNode);
		if (UEdGraphPin* SelfPin = VisibilityNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input))
		{
			WidgetGet->GetValuePin()->MakeLinkTo(SelfPin);
		}
		if (UEdGraphPin* VisibilityPin = VisibilityNode->FindPin(TEXT("InVisibility"), EGPD_Input))
		{
			// Enum byte pins store the enumerant NAME as their literal default (the schema rejects
			// numeric strings: "'4' is not a valid enumerant of ESlateVisibility").
			const UEnum* VisibilityEnum = StaticEnum<ESlateVisibility>();
			VisibilityPin->DefaultValue = VisibilityEnum
				? VisibilityEnum->GetNameStringByValue(static_cast<int64>(Visibility))
				: TEXT("");
		}
		return true;
	}

	/**
	 * Overlay-exit sequence shared by the Back TRUE branch and the mirror-trigger Navigate chain:
	 * collapse the host, then every hosted page (a later different overlay then opens clean).
	 */
	bool AppendOverlayExitCollapse(
		UEdGraph* Graph,
		UWidgetBlueprint* WBP,
		const FEngineFunctions& Functions,
		FObjectProperty* OverlayHostProperty,
		const TArray<UWidget*>& OverlayHostPages,
		FChainLayout& Layout)
	{
		if (!AppendSetVisibilityCall(Graph, WBP, Functions,
			OverlayHostProperty, ESlateVisibility::Collapsed, Layout))
		{
			return false;
		}
		bool bValid = true;
		for (UWidget* HostedPage : OverlayHostPages)
		{
			if (FObjectProperty* PageProperty = FindFProperty<FObjectProperty>(
				WBP->SkeletonGeneratedClass, HostedPage->GetFName()))
			{
				bValid = AppendSetVisibilityCall(Graph, WBP, Functions,
					PageProperty, ESlateVisibility::Collapsed, Layout) && bValid;
			}
		}
		return bValid;
	}

	/**
	 * Appends UWidget::SetRenderTranslation(MakeVector2D(0,0)) on the switcher: the outgoing-only
	 * slide chain leaves the switcher translated off-screen after the switch (no incoming animation
	 * exists to restore the transform), so the chain resets it explicitly. MakeVector2D's untouched
	 * X/Y literals carry the zeros; a raw struct-pin literal would depend on the pin's export format.
	 */
	bool AppendSetRenderTranslationReset(
		UEdGraph* Graph,
		UWidgetBlueprint* WBP,
		const FEngineFunctions& Functions,
		FObjectProperty* SwitcherProperty,
		FChainLayout& Layout)
	{
		if (!Functions.SetRenderTranslation || !Functions.MakeVector2D || !SwitcherProperty)
		{
			return false;
		}
		UK2Node_VariableGet* SwitcherGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, SwitcherProperty));
		UK2Node_CallFunction* TranslationNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
			[&Functions](UK2Node_CallFunction* Node)
			{
				Node->SetFromFunction(Functions.SetRenderTranslation);
			});
		Layout.PlaceExec(TranslationNode);
		if (UEdGraphPin* SelfPin = TranslationNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input))
		{
			SwitcherGet->GetValuePin()->MakeLinkTo(SelfPin);
		}
		if (UEdGraphPin* TranslationPin = TranslationNode->FindPin(TEXT("Translation"), EGPD_Input))
		{
			UK2Node_CallFunction* MakeVectorNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
				[&Functions](UK2Node_CallFunction* Node)
				{
					Node->SetFromFunction(Functions.MakeVector2D);
				});
			Layout.PlacePure(MakeVectorNode);
			if (UEdGraphPin* XPin = MakeVectorNode->FindPin(TEXT("X"), EGPD_Input))
			{
				XPin->DefaultValue = TEXT("0.0");
			}
			if (UEdGraphPin* YPin = MakeVectorNode->FindPin(TEXT("Y"), EGPD_Input))
			{
				YPin->DefaultValue = TEXT("0.0");
			}
			if (UEdGraphPin* ReturnValuePin = MakeVectorNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output))
			{
				ReturnValuePin->MakeLinkTo(TranslationPin);
			}
		}
		return true;
	}
}


int32 WireMouseBindings(UWidgetBlueprint* WBP, UEdGraph* Ubergraph,
	const FUMGPrototypeNavigationData& NavigationData, const FEngineFunctions& Functions,
	const FBindingPlan& Plan, const FBindingGraphTargets& Targets, TArray<FString>& OutWarnings)
{
	UWidgetSwitcher* Switcher = Plan.Switcher;
	const FString& SwitcherBaseName = Plan.SwitcherBaseName;
	UOverlay* OverlayHost = Plan.OverlayHost;
	const auto& MouseBindings = Plan.MouseBindings;
	FObjectProperty* SwitcherProperty = Targets.SwitcherProperty;
	FObjectProperty* OverlayHostProperty = Targets.OverlayHostProperty;
	FArrayProperty* HistoryProperty = Targets.HistoryProperty;
	const auto& OverlayHostPages = Targets.OverlayHostPages;
	int32 WiredCount = 0;

	// One bound-event + action chain for one trigger of a resolved binding. A trigger whose widget
	// physically sits inside the overlay host (the dual-target/initial mirror copy, or a plain
	// overlay-target page) reuses the same authoring with overlay-context semantics: Navigate
	// prepends the overlay-exit collapse (Figma semantics: navigate replaces the current screen,
	// overlay included) and skips the transition animations (the overlay layer is closing, not
	// animating the switcher underneath); Back/Overlay/OpenLink are the same chain. The copy type
	// is deliberately not the discriminator: an overlay-only page has no mirror twin at all.
	const auto AuthorTriggerChain = [&](const FResolvedBinding& Resolved, const FName TriggerWidgetName,
		const bool bMirrorTrigger, FChainLayout& Layout) -> bool
	{
		const FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings[Resolved.BindingIndex];
		const FUMGPrototypeNavigationStep& Step = Binding.Steps[0];
		const UWidget* TriggerWidget = WBP->WidgetTree ? WBP->WidgetTree->FindWidget(TriggerWidgetName) : nullptr;
		const bool bTriggerInsideOverlayHost = OverlayHost && TriggerWidget
			&& IsWidgetInsideWidget(TriggerWidget, OverlayHost);

		FObjectProperty* ButtonProperty = FindFProperty<FObjectProperty>(
			WBP->SkeletonGeneratedClass, TriggerWidgetName);
		FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(
			UButton::StaticClass(), Resolved.DelegateName);
		if (!ButtonProperty || !DelegateProperty)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s'%s: button/delegate property missing after skeleton refresh"),
				*Binding.TriggerWidgetId, bMirrorTrigger ? TEXT(" (mirror)") : TEXT("")));
			return false;
		}

		UK2Node_ComponentBoundEvent* EventNode = CreateMarkerNode<UK2Node_ComponentBoundEvent>(Ubergraph,
			[ButtonProperty, DelegateProperty](UK2Node_ComponentBoundEvent* Node)
			{
				Node->InitializeComponentBoundEventParams(ButtonProperty, DelegateProperty);
			});
		Layout.PlaceExec(EventNode);

		bool bChainValid = true;
		switch (Step.Kind)
		{
		case EUMGPrototypeNavigationStepKind::Overlay:
		{
			// Host + page visibility: show the host, then the hosted target page - the plain page for
			// an overlay-only target, the mirror copy for a dual-target/initial page (both layers must
			// switch to SelfHitTestInvisible; a visible page under a collapsed host stays hidden). The
			// chain never touches the switcher or the history: the overlay is a layered display over
			// the current page.
			FObjectProperty* OverlayPageProperty = FindFProperty<FObjectProperty>(
				WBP->SkeletonGeneratedClass, Resolved.OverlayPageWidgetName);
			if (!OverlayPageProperty)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': overlay page '%s' has no skeleton class property after refresh"),
					*Binding.TriggerWidgetId, *Resolved.OverlayPageWidgetName.ToString()));
				bChainValid = false;
				break;
			}
			bChainValid = AppendSetVisibilityCall(Ubergraph, WBP, Functions,
				OverlayHostProperty, ESlateVisibility::SelfHitTestInvisible, Layout)
				&& AppendSetVisibilityCall(Ubergraph, WBP, Functions,
					OverlayPageProperty, ESlateVisibility::SelfHitTestInvisible, Layout)
				&& LinkExecChain(Layout.ExecChain);
			break;
		}

		case EUMGPrototypeNavigationStepKind::Back:
		{
			// Back first asks whether an overlay is open: GetVisibility() is the widget's own UMG
			// property, so this is correct even before the Slate tree caches the widget. Any
			// non-Collapsed host counts as open (the generated open chain uses SelfHitTestInvisible);
			// TRUE (overlay open): collapse the host and every hosted page, then STOP - no history
			// pop and the active page is unchanged, so A->B->open overlay->Back stays on B. FALSE:
			// the history pop chain runs (empty-history guard unchanged). Without a host the pop
			// chain runs directly off the event.
			UK2Node_IfThenElse* HostBranchNode = nullptr;
			if (OverlayHost && OverlayHostProperty)
			{
				if (!Functions.GetVisibility || !Functions.Not_PreBool)
				{
					bChainValid = false;
					SkipBinding(OutWarnings, FString::Printf(
						TEXT("trigger '%s': UWidget::GetVisibility/Not_PreBool not found for Back wiring"),
						*Binding.TriggerWidgetId));
					break;
				}
				UK2Node_VariableGet* HostGet = Layout.PlacePure(CreateVariableGet(Ubergraph, WBP, OverlayHostProperty));
				UK2Node_CallFunction* GetVisibilityNode = CreateMarkerNode<UK2Node_CallFunction>(Ubergraph,
					[&Functions](UK2Node_CallFunction* Node)
					{
						Node->SetFromFunction(Functions.GetVisibility);
					});
				Layout.PlacePure(GetVisibilityNode);
				if (UEdGraphPin* SelfPin = GetVisibilityNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input))
				{
					HostGet->GetValuePin()->MakeLinkTo(SelfPin);
				}
				else
				{
					bChainValid = false;
				}

				UK2Node_EnumEquality* VisibilityIsCollapsed = CreateMarkerNode<UK2Node_EnumEquality>(Ubergraph,
					[](UK2Node_EnumEquality*) {});
				Layout.PlacePure(VisibilityIsCollapsed);
				if (bChainValid)
				{
					if (UEdGraphPin* VisibilityPin = GetVisibilityNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output))
					{
						// TryCreateConnection (not MakeLinkTo): the enum-equality inputs are wildcards
						// until a connection propagates ESlateVisibility to both A and B.
						bChainValid = Ubergraph->GetSchema()->TryCreateConnection(
							VisibilityPin, VisibilityIsCollapsed->GetInput1Pin());
					}
					else
					{
						bChainValid = false;
					}
				}
				if (bChainValid)
				{
					UEdGraphPin* ComparePin = VisibilityIsCollapsed->GetInput2Pin();
					const UEnum* VisibilityEnum = StaticEnum<ESlateVisibility>();
					if (ComparePin && VisibilityEnum)
					{
						ComparePin->DefaultValue = VisibilityEnum->GetNameStringByValue(
							static_cast<int64>(ESlateVisibility::Collapsed));
					}
					else
					{
						bChainValid = false;
					}
				}

				UK2Node_CallFunction* NotNode = CreateMarkerNode<UK2Node_CallFunction>(Ubergraph,
					[&Functions](UK2Node_CallFunction* Node)
					{
						Node->SetFromFunction(Functions.Not_PreBool);
					});
				Layout.PlacePure(NotNode);
				if (bChainValid)
				{
					UEdGraphPin* EqualityPin = VisibilityIsCollapsed->GetReturnValuePin();
					UEdGraphPin* NotInputPin = NotNode->FindPin(TEXT("A"), EGPD_Input);
					bChainValid = EqualityPin && NotInputPin
						&& Ubergraph->GetSchema()->TryCreateConnection(EqualityPin, NotInputPin);
				}

				HostBranchNode = CreateMarkerNode<UK2Node_IfThenElse>(Ubergraph,
					[](UK2Node_IfThenElse*) {});
				Layout.PlaceExec(HostBranchNode);
				if (bChainValid)
				{
					UEdGraphPin* ConditionPin = HostBranchNode->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input);
					UEdGraphPin* NotReturnPin = NotNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
					bChainValid = ConditionPin && NotReturnPin
						&& Ubergraph->GetSchema()->TryCreateConnection(NotReturnPin, ConditionPin);
				}
			}
			if (bChainValid && (!Switcher || !SwitcherProperty || !Functions.ArrayIsEmpty
				|| !Functions.ArrayLastIndex || !Functions.ArrayGet || !Functions.ArrayRemove))
			{
				bChainValid = false;
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': switcher or array engine functions unavailable for Back wiring"),
					*Binding.TriggerWidgetId));
			}
			if (bChainValid && HostBranchNode)
			{
				// TRUE (overlay open): the collapse chain ends the branch - the history pop below
				// stays unreached (shared overlay-exit sequence).
				FChainLayout CollapseLayout;
				CollapseLayout.RowY = Layout.RowY;
				CollapseLayout.NextColumn = Layout.NextColumn;
				bChainValid = AppendOverlayExitCollapse(Ubergraph, WBP, Functions,
					OverlayHostProperty, OverlayHostPages, CollapseLayout);
				Layout.NextColumn = FMath::Max(Layout.NextColumn, CollapseLayout.NextColumn);
				if (bChainValid)
				{
					bChainValid = LinkExecChain(CollapseLayout.ExecChain);
				}
				if (bChainValid)
				{
					UEdGraphPin* ThenPin = HostBranchNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
					UEdGraphPin* CollapseExecPin = CollapseLayout.ExecChain[0]->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
					if (ThenPin && CollapseExecPin)
					{
						ThenPin->MakeLinkTo(CollapseExecPin);
					}
					else
					{
						bChainValid = false;
					}
				}
			}
			if (bChainValid)
			{
				UK2Node_VariableGet* HistoryGet = Layout.PlacePure(
					CreateVariableGet(Ubergraph, WBP, HistoryProperty));
				UK2Node_CallArrayFunction* IsEmptyNode = CreateArrayCallNode(Ubergraph, Functions.ArrayIsEmpty);
				Layout.PlacePure(IsEmptyNode); // BlueprintPure
				if (UEdGraphPin* TargetArrayPin = IsEmptyNode->FindPin(TEXT("TargetArray"), EGPD_Input))
				{
					LinkArrayPin(Ubergraph, HistoryGet->GetValuePin(), TargetArrayPin);
				}

				// The empty-history Branch is the entry of the false side: it hangs off the host
				// branch's FALSE pin (overlay closed) or directly off the event when no host exists.
				// It is placed manually (like the pop chain below) so the linear exec chain does not
				// chain it behind the host branch's TRUE path.
				UK2Node_IfThenElse* BranchNode = CreateMarkerNode<UK2Node_IfThenElse>(Ubergraph,
					[](UK2Node_IfThenElse*) {});
				BranchNode->NodePosX = Layout.NextColumn++ * NodeColumnStep;
				BranchNode->NodePosY = Layout.RowY;
				if (UEdGraphPin* ConditionPin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input))
				{
					if (UEdGraphPin* IsEmptyPin = IsEmptyNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
					{
						IsEmptyPin->MakeLinkTo(ConditionPin);
					}
				}

				UK2Node_CallArrayFunction* LastIndexNode = CreateArrayCallNode(Ubergraph, Functions.ArrayLastIndex);
				Layout.PlacePure(LastIndexNode); // BlueprintPure
				if (UEdGraphPin* TargetArrayPin = LastIndexNode->FindPin(TEXT("TargetArray"), EGPD_Input))
				{
					LinkArrayPin(Ubergraph, HistoryGet->GetValuePin(), TargetArrayPin);
				}

				// The history stores VALUES (the page that was left), not positions in this array:
				// the pop must read history[last index] through Array_Get. Feeding LastIndex's
				// return straight into SetActiveWidgetIndex only works while every entry equals
				// its own index (single-step history); [0,2] would otherwise jump to page 1.
				UK2Node_CallArrayFunction* LastValueNode = CreateArrayCallNode(Ubergraph, Functions.ArrayGet);
				Layout.PlacePure(LastValueNode); // BlueprintPure
				if (UEdGraphPin* TargetArrayPin = LastValueNode->FindPin(TEXT("TargetArray"), EGPD_Input))
				{
					LinkArrayPin(Ubergraph, HistoryGet->GetValuePin(), TargetArrayPin);
				}
				if (UEdGraphPin* GetIndexPin = LastValueNode->FindPin(TEXT("Index"), EGPD_Input))
				{
					if (UEdGraphPin* LastIndexPin = LastIndexNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
					{
						LastIndexPin->MakeLinkTo(GetIndexPin);
					}
				}

				// False path (history non-empty): jump to the recorded page, then drop that entry.
				// The true path (empty history) is an intentional no-op end.
				TArray<UEdGraphNode*> PopChain;
				const auto PlacePopExec = [&Layout, &PopChain](UEdGraphNode* Node)
				{
					Node->NodePosX = Layout.NextColumn++ * NodeColumnStep;
					Node->NodePosY = Layout.RowY;
					PopChain.Add(Node);
				};

				UK2Node_VariableGet* SwitcherGet = Layout.PlacePure(
					CreateVariableGet(Ubergraph, WBP, SwitcherProperty));
				UK2Node_CallFunction* SwitchNode = CreateMarkerNode<UK2Node_CallFunction>(Ubergraph,
					[&Functions](UK2Node_CallFunction* Node)
					{
						Node->SetFromFunction(Functions.SetActiveWidgetIndex);
					});
				PlacePopExec(SwitchNode);
				if (UEdGraphPin* IndexPin = SwitchNode->FindPin(TEXT("Index"), EGPD_Input))
				{
					// Array_Get returns its value through the "Item" out-param (BlueprintInternalUseOnly
					// array helper; the engine's own Get node uses the same UK2Node_CallArrayFunction).
					if (UEdGraphPin* LastValuePin = LastValueNode->FindPin(TEXT("Item"), EGPD_Output))
					{
						LastValuePin->MakeLinkTo(IndexPin);
					}
				}
				if (UEdGraphPin* SelfPin = SwitchNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input))
				{
					SwitcherGet->GetValuePin()->MakeLinkTo(SelfPin);
				}

				UK2Node_CallArrayFunction* RemoveNode = CreateArrayCallNode(Ubergraph, Functions.ArrayRemove);
				PlacePopExec(RemoveNode);
				if (UEdGraphPin* TargetArrayPin = RemoveNode->FindPin(TEXT("TargetArray"), EGPD_Input))
				{
					LinkArrayPin(Ubergraph, HistoryGet->GetValuePin(), TargetArrayPin);
				}
				if (UEdGraphPin* IndexToRemovePin = RemoveNode->FindPin(TEXT("IndexToRemove"), EGPD_Input))
				{
					if (UEdGraphPin* LastIndexPin = LastIndexNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
					{
						LastIndexPin->MakeLinkTo(IndexToRemovePin);
					}
				}

				// The main exec chain ends at the host visibility branch (or at the event when no
				// host exists); the empty-history Branch hangs off the FALSE side as wired below.
				bChainValid = LinkExecChain(Layout.ExecChain);
				if (bChainValid)
				{
					UEdGraphPin* HistoryEntryExecPin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
					UEdGraphPin* EntrySourcePin = HostBranchNode
						? HostBranchNode->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output)
						: Layout.ExecChain[0]->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
					if (HistoryEntryExecPin && EntrySourcePin)
					{
						EntrySourcePin->MakeLinkTo(HistoryEntryExecPin);
					}
					else
					{
						bChainValid = false;
					}
				}
				if (bChainValid)
				{
					UEdGraphPin* ElsePin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output);
					UEdGraphPin* PopExecPin = PopChain[0]->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
					if (ElsePin && PopExecPin)
					{
						ElsePin->MakeLinkTo(PopExecPin);
						bChainValid = LinkExecChain(PopChain);
					}
					else
					{
						bChainValid = false;
					}
				}
			}
			break;
		}

		case EUMGPrototypeNavigationStepKind::Navigate:
		{
			if (!Switcher || !SwitcherProperty)
			{
				bChainValid = false;
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': no unique switcher widget '%s' with a class property in the built tree"),
					*Binding.TriggerWidgetId, *SwitcherBaseName));
				break;
			}
			if (bTriggerInsideOverlayHost)
			{
				// Overlay-context Navigate (fired from inside the open overlay): exit the
				// overlay first - the shared Back TRUE-branch collapse - then the standard
				// navigate tail. Figma semantics: navigate replaces the current screen, overlay
				// included. No transition animations: the overlay layer is closing, not
				// animating the switcher underneath.
				bChainValid = AppendOverlayExitCollapse(Ubergraph, WBP, Functions,
					OverlayHostProperty, OverlayHostPages, Layout)
					&& AppendNavigateTail(Ubergraph, WBP, Functions,
						SwitcherProperty, HistoryProperty, Resolved.TargetPageIndex, Layout)
					&& LinkExecChain(Layout.ExecChain);
				break;
			}
			// Chain shape by which transition animations resolved (the animation builder generates
			// out-only for MOVE_OUT, in-only for MOVE_IN, both otherwise):
			// - both: out -> delay -> switch -> in (bidirectional/fade keep the classic chain);
			// - out only: out -> delay -> switch -> stop(out) -> reset translation. The stop runs
			//   immediately before the reset because the outgoing animation can still have its
			//   final evaluation pending when the latent delay resumes (the animation only starts
			//   advancing on the next frame while the latent manager advances on the click frame);
			//   without the stop, that residual evaluation re-overwrites the reset with the final
			//   off-screen key. Stopping an already finished animation is a no-op;
			// - in only: switch -> in (switch first; a pre-switch delay would be dead time).
			const bool bHasOutgoing = Resolved.bTransitionChain && Resolved.OutgoingAnimationName != NAME_None;
			const bool bHasIncoming = Resolved.bTransitionChain && Resolved.IncomingAnimationName != NAME_None;
			if (bHasOutgoing)
			{
				AppendPlayAnimationCall(Ubergraph, WBP, Functions, Resolved.OutgoingAnimationName, Layout);
			}
			if (bHasOutgoing)
			{
				UK2Node_CallFunction* DelayNode = CreateMarkerNode<UK2Node_CallFunction>(Ubergraph,
					[&Functions](UK2Node_CallFunction* Node)
					{
						Node->SetFromFunction(Functions.Delay);
					});
				Layout.PlaceExec(DelayNode);
				if (UEdGraphPin* DurationPin = DelayNode->FindPin(TEXT("Duration"), EGPD_Input))
				{
					// Same single phase-duration rule the animation builder sampled its keys with,
					// so the latent wait ends exactly when the outgoing animation does.
					DurationPin->DefaultValue = FString::SanitizeFloat(Resolved.DelaySeconds);
				}
			}
			bChainValid = AppendNavigateTail(Ubergraph, WBP, Functions,
				SwitcherProperty, HistoryProperty, Resolved.TargetPageIndex, Layout);
			if (bChainValid && bHasIncoming)
			{
				AppendPlayAnimationCall(Ubergraph, WBP, Functions, Resolved.IncomingAnimationName, Layout);
			}
			if (bChainValid && bHasOutgoing && !bHasIncoming)
			{
				bChainValid = AppendStopAnimationCall(Ubergraph, WBP, Functions,
					Resolved.OutgoingAnimationName, Layout)
					&& AppendSetRenderTranslationReset(Ubergraph, WBP, Functions, SwitcherProperty, Layout);
			}
			if (bChainValid)
			{
				bChainValid = LinkExecChain(Layout.ExecChain);
			}
			break;
		}

		case EUMGPrototypeNavigationStepKind::OpenLink:
			bChainValid = AppendOpenLinkTail(Ubergraph, Functions, Step, Layout)
				&& LinkExecChain(Layout.ExecChain);
			break;

		default:
			bChainValid = false;
			break;
		}

		return bChainValid;
	};

	for (const FResolvedBinding& Resolved : MouseBindings)
	{
		const FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings[Resolved.BindingIndex];
		FChainLayout Layout;
		Layout.RowY = WiredCount * NodeRowStep;
		if (!AuthorTriggerChain(Resolved, Resolved.TriggerWidgetName, /*bMirrorTrigger=*/false, Layout))
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s': generated nodes missing engine pins (engine contract changed)"),
				*Binding.TriggerWidgetId));
		}
		++WiredCount;

		// The mirror twin (resolved in pass 1) gets its own chain on its own row: same authored set,
		// same marker lifecycle - cleared and re-authored together on regeneration.
		if (Resolved.MirrorTriggerWidgetName != NAME_None)
		{
			FChainLayout MirrorLayout;
			MirrorLayout.RowY = WiredCount * NodeRowStep;
			if (!AuthorTriggerChain(Resolved, Resolved.MirrorTriggerWidgetName, /*bMirrorTrigger=*/true, MirrorLayout))
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': mirror trigger '%s' generated nodes missing engine pins (engine contract changed)"),
					*Binding.TriggerWidgetId, *Resolved.MirrorTriggerWidgetName.ToString()));
			}
			++WiredCount;
		}
	}

	return WiredCount;
}
}
