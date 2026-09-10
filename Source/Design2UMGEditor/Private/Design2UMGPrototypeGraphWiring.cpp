// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeGraphWiring.h"

#include "Design2UMGLog.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetUtils.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/WidgetSwitcher.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{

const TCHAR NodeMarkerComment[] = TEXT("Design2UMG Prototype Navigation");

void ClearGeneratedBindings(UWidgetBlueprint* WBP)
{
	if (!WBP)
	{
		return;
	}

	for (UEdGraph* Ubergraph : WBP->UbergraphPages)
	{
		if (!Ubergraph)
		{
			continue;
		}

		TArray<UEdGraphNode*> NodesToRemove;
		for (UEdGraphNode* Node : Ubergraph->Nodes)
		{
			if (Node && Node->NodeComment.Equals(NodeMarkerComment, ESearchCase::CaseSensitive))
			{
				NodesToRemove.Add(Node);
			}
		}
		for (UEdGraphNode* Node : NodesToRemove)
		{
			// bDontRecompile: the caller re-authors nodes right after and the next compile
			// (SaveWidgetBlueprint's final one) picks the graph up; no extra structural compiles here.
			FBlueprintEditorUtils::RemoveNode(WBP, Node, /*bDontRecompile=*/true);
		}
	}
}

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
	const EUMGPrototypeNavigationStepKind Kind = Binding.Steps[0].Kind;
	return Kind == EUMGPrototypeNavigationStepKind::Navigate
		|| Kind == EUMGPrototypeNavigationStepKind::OpenLink;
}

namespace
{
	constexpr int32 NodeColumnStep = 320;
	constexpr int32 NodeRowStep = 220;
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
			if (Child && FUMGWidgetUtils::MatchesWidgetBaseName(Child->GetName(), PageBaseName))
			{
				++MatchCount;
				MatchedIndex = ChildIndex;
			}
		}
		return MatchCount == 1 ? MatchedIndex : INDEX_NONE;
	}

	/**
	 * Manual node creation mirroring FEdGraphSchemaAction_K2NewNode::CreateNode without the editor
	 * transaction/selection side effects: NewObject -> init params -> AddNode -> CreateNewGuid ->
	 * PostPlacedNewNode -> AllocateDefaultPins. Every node gets the marker comment.
	 */
	template <typename NodeType>
	NodeType* CreateMarkerNode(UEdGraph* Graph, TFunctionRef<void(NodeType*)> InitParams)
	{
		NodeType* Node = NewObject<NodeType>(Graph);
		InitParams(Node);
		Graph->AddNode(Node, /*bUserAction=*/false, /*bNotify=*/false);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		Node->NodeComment = NodeMarkerComment;
		return Node;
	}

	void SkipBinding(TArray<FString>& OutWarnings, const FString& Reason)
	{
		OutWarnings.Add(Reason);
		UE_LOG(LogDesign2UMG, Verbose, TEXT("PrototypeNavigation wiring skipped: %s"), *Reason);
	}
}

void WireSimplePrototypeBindings(
	UWidgetBlueprint* WBP,
	const FUMGPrototypeNavigationData& NavigationData,
	TArray<FString>& OutWarnings)
{
	if (!WBP || !WBP->WidgetTree)
	{
		return;
	}

	// Re-wiring is clear-then-author: a regenerate over the same blueprint must not accumulate nodes.
	ClearGeneratedBindings(WBP);

	if (NavigationData.ClickBindings.Num() == 0)
	{
		return;
	}

	UEdGraph* Ubergraph = WBP->UbergraphPages.Num() > 0 ? WBP->UbergraphPages[0] : nullptr;
	if (!Ubergraph)
	{
		UE_LOG(LogDesign2UMG, Verbose,
			TEXT("PrototypeNavigation wiring skipped for %s: blueprint has no ubergraph"),
			*WBP->GetName());
		return;
	}

	// Shared resolution for every Navigate binding: the switcher and its class property.
	const FString SwitcherBaseName = FUMGWidgetUtils::SanitizeWidgetName(
		NavigationData.SwitcherWidgetName, TEXT("WidgetSwitcher")).ToString();
	UWidgetSwitcher* Switcher = FUMGWidgetUtils::FindUniqueTreeWidget<UWidgetSwitcher>(
		WBP->WidgetTree, SwitcherBaseName);
	FObjectProperty* SwitcherProperty = Switcher
		? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Switcher->GetFName())
		: nullptr;

	// GET_MEMBER_NAME_CHECKED only resolves member variables; all call targets are UFUNCTIONs, so
	// their stable engine names are used directly.
	UFunction* SetActiveWidgetIndexFunction = UWidgetSwitcher::StaticClass()->FindFunctionByName(
		TEXT("SetActiveWidgetIndex"));
	UFunction* LaunchURLFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		TEXT("LaunchURL"));
	UFunction* PlayAnimationFunction = UUserWidget::StaticClass()->FindFunctionByName(
		TEXT("PlayAnimation"));
	UFunction* DelayFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		TEXT("Delay"));

	int32 WiredCount = 0;
	for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
	{
		if (!IsSimpleMouseActionBinding(Binding))
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s' (%s): binding is not a simple single-action Click/Hover/Press Navigate or OpenLink (Phase 2)"),
				*Binding.TriggerWidgetId, *Binding.TriggerType));
			continue;
		}
		const FUMGPrototypeNavigationStep& Step = Binding.Steps[0];
		const bool bNavigate = Step.Kind == EUMGPrototypeNavigationStepKind::Navigate;

		// Resolve the trigger button in the built tree, then read its class property; node params must
		// come from the built tree, never from the IR names, to stay immune to name drift.
		UButton* TriggerButton = FUMGWidgetUtils::FindUniqueTreeWidget<UButton>(WBP->WidgetTree,
			FUMGWidgetUtils::SanitizeWidgetName(Binding.TriggerWidgetId, TEXT("Button")).ToString());
		if (!TriggerButton)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s' (%s): no unique button widget in the built tree"),
				*Binding.TriggerWidgetId, *Binding.TriggerType));
			continue;
		}
		FObjectProperty* ButtonProperty = FindFProperty<FObjectProperty>(
			WBP->SkeletonGeneratedClass, TriggerButton->GetFName());
		if (!ButtonProperty)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s': button widget '%s' has no skeleton class property"),
				*Binding.TriggerWidgetId, *TriggerButton->GetName()));
			continue;
		}

		const FName DelegateName = GetDelegateNameForTrigger(Binding);
		FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(
			UButton::StaticClass(), DelegateName);
		if (!DelegateProperty)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s': delegate %s not found on UButton"),
				*Binding.TriggerWidgetId, *DelegateName.ToString()));
			continue;
		}
		if (FKismetEditorUtilities::FindBoundEventForComponent(WBP, DelegateName, TriggerButton->GetFName()))
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s': an event is already bound to %s on button '%s'"),
				*Binding.TriggerWidgetId, *DelegateName.ToString(), *TriggerButton->GetName()));
			continue;
		}

		// Navigate resolves the target index from the built switcher children, never from PageIds order.
		int32 TargetPageIndex = INDEX_NONE;
		if (bNavigate)
		{
			if (!Switcher || !SwitcherProperty)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': no unique switcher widget '%s' with a class property in the built tree"),
					*Binding.TriggerWidgetId, *SwitcherBaseName));
				continue;
			}
			TargetPageIndex = FindSwitcherPageIndex(Switcher,
				FUMGWidgetUtils::SanitizeWidgetName(Step.TargetPageId, TEXT("CanvasPanel")).ToString());
			if (TargetPageIndex == INDEX_NONE || !SetActiveWidgetIndexFunction)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': target page '%s' not uniquely locatable under switcher '%s'"),
					*Binding.TriggerWidgetId, *Step.TargetPageId, *Switcher->GetName()));
				continue;
			}
		}
		else if (!LaunchURLFunction)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s': UKismetSystemLibrary::LaunchURL not found"),
				*Binding.TriggerWidgetId));
			continue;
		}

		// Phase 2: a Navigate binding whose transition produced generated animations plays them
		// around the page switch (out -> wait one phase -> switch -> in). Missing engine functions or
		// unresolved animation class properties degrade to the Phase 1 direct wiring with a warning.
		bool bUseTransitionChain = false;
		FObjectProperty* OutgoingAnimationProperty = nullptr;
		FObjectProperty* IncomingAnimationProperty = nullptr;
		if (bNavigate && Binding.Transition.HasGeneratedAnimations())
		{
			if (!PlayAnimationFunction || !DelayFunction)
			{
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': PlayAnimation/Delay engine functions not found; wiring the switch without transition"),
					*Binding.TriggerWidgetId));
			}
			else
			{
				OutgoingAnimationProperty = Binding.Transition.OutgoingAnimationName.IsEmpty()
					? nullptr
					: FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, FName(*Binding.Transition.OutgoingAnimationName));
				IncomingAnimationProperty = Binding.Transition.IncomingAnimationName.IsEmpty()
					? nullptr
					: FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, FName(*Binding.Transition.IncomingAnimationName));
				if (!OutgoingAnimationProperty && !IncomingAnimationProperty)
				{
					SkipBinding(OutWarnings, FString::Printf(
						TEXT("trigger '%s': transition animations '%s'/'%s' have no skeleton class properties; wiring the switch without transition"),
						*Binding.TriggerWidgetId,
						*Binding.Transition.OutgoingAnimationName, *Binding.Transition.IncomingAnimationName));
				}
				else
				{
					bUseTransitionChain = true;
				}
			}
		}

		const int32 RowY = WiredCount * NodeRowStep;

		UK2Node_ComponentBoundEvent* EventNode = CreateMarkerNode<UK2Node_ComponentBoundEvent>(Ubergraph,
			[ButtonProperty, DelegateProperty](UK2Node_ComponentBoundEvent* Node)
			{
				Node->InitializeComponentBoundEventParams(ButtonProperty, DelegateProperty);
			});
		EventNode->NodePosX = 0;
		EventNode->NodePosY = RowY;

		// Exec chain after the bound event, linked front-to-back at the end; pure variable gets sit
		// between their producer and consumer, one column each (Phase 1 layout: event 0, get 1, call 2).
		TArray<UEdGraphNode*> ExecChain;
		ExecChain.Add(EventNode);
		int32 NextColumn = 1;
		const auto PlaceExecNode = [&ExecChain, &NextColumn, RowY](UEdGraphNode* Node)
		{
			Node->NodePosX = NextColumn++ * NodeColumnStep;
			Node->NodePosY = RowY;
			ExecChain.Add(Node);
		};
		const auto PlacePureGet = [&NextColumn, RowY](UK2Node_VariableGet* Node)
		{
			Node->NodePosX = NextColumn++ * NodeColumnStep;
			Node->NodePosY = RowY + 16;
		};

		// Authors a PlayAnimation call fed by the animation's class property (animations become named
		// object properties on the compiled widget class, same VariableGet recipe as widget variables).
		const auto AddPlayAnimationCall = [&](FObjectProperty* AnimationProperty)
		{
			UK2Node_VariableGet* AnimationGetNode = CreateMarkerNode<UK2Node_VariableGet>(Ubergraph,
				[AnimationProperty, WBP](UK2Node_VariableGet* Node)
				{
					Node->SetFromProperty(AnimationProperty, /*bSelfContext=*/true, WBP->SkeletonGeneratedClass);
				});
			PlacePureGet(AnimationGetNode);

			UK2Node_CallFunction* PlayNode = CreateMarkerNode<UK2Node_CallFunction>(Ubergraph,
				[PlayAnimationFunction](UK2Node_CallFunction* Node)
				{
					Node->SetFromFunction(PlayAnimationFunction);
				});
			PlaceExecNode(PlayNode);
			// PlayAnimation runs on the UserWidget self; only the animation pin needs an explicit link.
			if (UEdGraphPin* AnimationPin = PlayNode->FindPin(TEXT("InAnimation"), EGPD_Input))
			{
				AnimationGetNode->GetValuePin()->MakeLinkTo(AnimationPin);
			}
		};

		if (bUseTransitionChain)
		{
			if (OutgoingAnimationProperty)
			{
				AddPlayAnimationCall(OutgoingAnimationProperty);
			}

			UK2Node_CallFunction* DelayNode = CreateMarkerNode<UK2Node_CallFunction>(Ubergraph,
				[DelayFunction](UK2Node_CallFunction* Node)
				{
					Node->SetFromFunction(DelayFunction);
				});
			PlaceExecNode(DelayNode);
			if (UEdGraphPin* DurationPin = DelayNode->FindPin(TEXT("Duration"), EGPD_Input))
			{
				// Same single phase-duration rule the animation builder sampled its keys with, so the
				// latent wait ends exactly when the outgoing animation does.
				DurationPin->DefaultValue = FString::SanitizeFloat(
					UMGPrototypeAnimationBuilder::ResolvePhaseDurationSeconds(Binding.Transition));
			}
		}

		UK2Node_VariableGet* SwitcherGetNode = nullptr;
		if (bNavigate)
		{
			SwitcherGetNode = CreateMarkerNode<UK2Node_VariableGet>(Ubergraph,
				[SwitcherProperty, WBP](UK2Node_VariableGet* Node)
				{
					Node->SetFromProperty(SwitcherProperty, /*bSelfContext=*/true, WBP->SkeletonGeneratedClass);
				});
			PlacePureGet(SwitcherGetNode);
		}

		UK2Node_CallFunction* CallNode = CreateMarkerNode<UK2Node_CallFunction>(Ubergraph,
			[bNavigate, SetActiveWidgetIndexFunction, LaunchURLFunction](UK2Node_CallFunction* Node)
			{
				Node->SetFromFunction(bNavigate ? SetActiveWidgetIndexFunction : LaunchURLFunction);
			});
		PlaceExecNode(CallNode);

		if (bNavigate)
		{
			if (UEdGraphPin* IndexPin = CallNode->FindPin(TEXT("Index"), EGPD_Input))
			{
				IndexPin->DefaultValue = FString::FromInt(TargetPageIndex);
			}
			if (UEdGraphPin* SelfPin = CallNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
				SelfPin && SwitcherGetNode)
			{
				SwitcherGetNode->GetValuePin()->MakeLinkTo(SelfPin);
			}
		}
		else
		{
			if (UEdGraphPin* UrlPin = CallNode->FindPin(TEXT("URL"), EGPD_Input))
			{
				UrlPin->DefaultValue = Step.ExternalUrl;
			}
		}

		if (bUseTransitionChain && IncomingAnimationProperty)
		{
			AddPlayAnimationCall(IncomingAnimationProperty);
		}

		bool bExecChainLinked = true;
		for (int32 ChainIndex = 0; ChainIndex + 1 < ExecChain.Num(); ++ChainIndex)
		{
			UEdGraphPin* ThenPin = ExecChain[ChainIndex]->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			UEdGraphPin* ExecPin = ExecChain[ChainIndex + 1]->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			if (ThenPin && ExecPin)
			{
				ThenPin->MakeLinkTo(ExecPin);
			}
			else
			{
				bExecChainLinked = false;
			}
		}
		if (!bExecChainLinked)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s': generated nodes missing exec pins (engine contract changed)"),
				*Binding.TriggerWidgetId));
		}

		++WiredCount;
	}

	if (WiredCount > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	}
}

} // namespace Design2UMGPrototypeGraphWiring
