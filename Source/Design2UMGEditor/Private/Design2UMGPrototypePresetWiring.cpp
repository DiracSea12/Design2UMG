// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGAnimationPresets.h"
#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ScopeExit.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MovieScene.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{
namespace
{
	const TCHAR* PresetMarkerPrefix = TEXT("Design2UMG Prototype Navigation|Preset|");
	const FName PresetVariableMetadata(TEXT("Design2UMGGeneratedPresetTime"));

	FString MarkerFor(FName Target, bool bEntrance)
	{
		return FString(PresetMarkerPrefix) + Target.ToString() + (bEntrance ? TEXT("|entrance") : TEXT("|feedback"));
	}
	FName TimeVariable(FName Target, const TCHAR* Suffix)
	{
		return FName(*(TEXT("__d2u_preset_") + Target.ToString() + TEXT("_") + Suffix));
	}

	bool IsOwnedNode(const UEdGraphNode* Node)
	{
		return Node && (Node->NodeComment == NodeMarkerComment || Node->NodeComment.StartsWith(PresetMarkerPrefix)
			|| Node->NodeComment == FString(NodeMarkerComment) + TEXT("|LifecycleBridge"));
	}

	bool EventCanBeExtended(const UEdGraphNode* Event)
	{
		if (!Event) return true;
		if (!IsOwnedNode(Event)) return false;
		const UEdGraphPin* Then = Event->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (!Then) return false;
		for (const UEdGraphPin* Link : Then->LinkedTo)
			if (!Link || !IsOwnedNode(Link->GetOwningNode())) return false;
		return true;
	}

	UK2Node_Event* FindConstruct(UWidgetBlueprint* Blueprint)
	{
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
			if (Graph) for (UEdGraphNode* Node : Graph->Nodes)
				if (auto* Event = Cast<UK2Node_Event>(Node))
					if (Event->EventReference.GetMemberName() == TEXT("Construct")) return Event;
		return nullptr;
	}

	bool RemovePresetGraph(UWidgetBlueprint* Blueprint, const FString& Marker, FString& Error)
	{
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (!Graph) continue;
			TArray<UEdGraphNode*> Remove;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node || Node->NodeComment != Marker) continue;
				for (UEdGraphPin* Pin : Node->Pins)
					if (Pin) for (UEdGraphPin* Link : Pin->LinkedTo)
						if (Link && !IsOwnedNode(Link->GetOwningNode()))
						{
							Error = TEXT("Preset-generated nodes have user-authored connections; preserve them and refuse replacement.");
							return false;
						}
				Remove.Add(Node);
			}
			for (UEdGraphNode* Node : Remove)
			{
				auto* Sequence = Cast<UK2Node_ExecutionSequence>(Node);
				if (!Sequence) continue;
				UEdGraphPin* Input = Sequence->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
				UEdGraphPin* First = Sequence->GetThenPinGivenIndex(0);
				if (!Input || !First || Input->LinkedTo.Num() != 1 || First->LinkedTo.Num() != 1) continue;
				UEdGraphPin* Source = Input->LinkedTo[0];
				UEdGraphPin* Original = First->LinkedTo[0];
				if (Source->GetOwningNode()->NodeComment == Marker || Original->GetOwningNode()->NodeComment == Marker) continue;
				Source->BreakLinkTo(Input);
				First->BreakLinkTo(Original);
				Source->MakeLinkTo(Original);
			}
			for (UEdGraphNode* Node : Remove) FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
		}
		return true;
	}

	bool AttachEvent(UEdGraph* Graph, UEdGraphNode* Event, UEdGraphNode* Action)
	{
		UEdGraphPin* Then = Event->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* ActionExec = Action->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		if (!Then || !ActionExec || Then->LinkedTo.Num() > 1) return false;
		if (Then->LinkedTo.IsEmpty()) { Then->MakeLinkTo(ActionExec); return true; }
		UEdGraphPin* Original = Then->LinkedTo[0];
		Then->BreakLinkTo(Original);
		auto* Sequence = CreateMarkerNode<UK2Node_ExecutionSequence>(Graph, [](UK2Node_ExecutionSequence*) {});
		Then->MakeLinkTo(Sequence->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		Sequence->GetThenPinGivenIndex(0)->MakeLinkTo(Original);
		Sequence->GetThenPinGivenIndex(1)->MakeLinkTo(ActionExec);
		return true;
	}

	bool MakeTimeVariable(UWidgetBlueprint* Blueprint, FName Name)
	{
		const int32 Existing = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Name);
		if (Existing != INDEX_NONE)
		{
			FString Value;
			return FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, Name, nullptr, PresetVariableMetadata, Value) && Value == TEXT("1");
		}
		if (FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass, Name)) return false;
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Real;
		Type.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, TEXT("0.0"))) return false;
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Name, nullptr, PresetVariableMetadata, TEXT("1"));
		return true;
	}

	UK2Node_VariableSet* SetVariable(UEdGraph* Graph, UWidgetBlueprint* Blueprint, const FProperty* Property, FChainLayout& Chain)
	{
		auto* Set = CreateMarkerNode<UK2Node_VariableSet>(Graph, [&](UK2Node_VariableSet* Node)
			{ Node->SetFromProperty(Property, true, Blueprint->SkeletonGeneratedClass); });
		Chain.PlaceExec(Set);
		return Set;
	}

	bool Connect(UEdGraph* Graph, UEdGraphPin* From, UEdGraphPin* To)
	{
		return From && To && Graph->GetSchema()->TryCreateConnection(From, To);
	}

	UK2Node_CallFunction* Call(UEdGraph* Graph, FChainLayout& Chain, UFunction* Function)
	{
		if (!Function) return nullptr;
		auto* Node = CreateMarkerNode<UK2Node_CallFunction>(Graph, [Function](UK2Node_CallFunction* NewNode) { NewNode->SetFromFunction(Function); });
		if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure)) Chain.PlacePure(Node); else Chain.PlaceExec(Node);
		return Node;
	}

}

bool WireAnimationTimeTarget(UEdGraph* Graph, UWidgetBlueprint* Blueprint, FObjectProperty* AnimationProperty,
		FFloatProperty* TargetProperty, FFloatProperty* StartProperty, UEdGraphPin* DynamicTarget,
		float TargetTime, float Speed, FChainLayout& Chain)
	{
		TSet<UEdGraphNode*> ExistingNodes;
		for (UEdGraphNode* Node : Graph->Nodes) ExistingNodes.Add(Node);
		ON_SCOPE_EXIT
		{
			for (UEdGraphNode* Node : Graph->Nodes)
				if (Node && !ExistingNodes.Contains(Node) && Node->NodeComment.IsEmpty()) Node->NodeComment = NodeMarkerComment;
		};
		UFunction* IsPlaying = UUserWidget::StaticClass()->FindFunctionByName(TEXT("IsAnimationPlaying"));
		UFunction* GetTime = UUserWidget::StaticClass()->FindFunctionByName(TEXT("GetAnimationCurrentTime"));
		UFunction* EndTime = UWidgetAnimation::StaticClass()->FindFunctionByName(TEXT("GetEndTime"));
		UFunction* Select = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("SelectFloat"));
		UFunction* Less = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Less_DoubleDouble"));
		UFunction* Subtract = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Subtract_DoubleDouble"));
		UFunction* Play = UUserWidget::StaticClass()->FindFunctionByName(TEXT("PlayAnimationTimeRange"));
		if (!IsPlaying || !GetTime || !EndTime || !Select || !Less || !Subtract || !Play) return false;
		auto* Animation = Chain.PlacePure(CreateVariableGet(Graph, Blueprint, AnimationProperty));
		auto* Playing = Call(Graph, Chain, IsPlaying);
		auto* Current = Call(Graph, Chain, GetTime);
		if (!Connect(Graph, Animation->GetValuePin(), Playing->FindPin(TEXT("InAnimation")))
			|| !Connect(Graph, Animation->GetValuePin(), Current->FindPin(TEXT("InAnimation")))) return false;
		auto* OldTarget = Chain.PlacePure(CreateVariableGet(Graph, Blueprint, TargetProperty));
		auto* PickStart = Call(Graph, Chain, Select);
		if (!Connect(Graph, Current->GetReturnValuePin(), PickStart->FindPin(TEXT("A")))
			|| !Connect(Graph, OldTarget->GetValuePin(), PickStart->FindPin(TEXT("B")))
			|| !Connect(Graph, Playing->GetReturnValuePin(), PickStart->FindPin(TEXT("bPickA")))) return false;
		auto* SetStart = SetVariable(Graph, Blueprint, StartProperty, Chain);
		if (!Connect(Graph, PickStart->GetReturnValuePin(), SetStart->FindPinChecked(SetStart->GetVarName(), EGPD_Input))) return false;
		auto* SetTarget = SetVariable(Graph, Blueprint, TargetProperty, Chain);
		if (DynamicTarget)
		{
			if (!Connect(Graph, DynamicTarget, SetTarget->FindPinChecked(SetTarget->GetVarName(), EGPD_Input))) return false;
		}
		else SetTarget->FindPinChecked(SetTarget->GetVarName(), EGPD_Input)->DefaultValue = FString::SanitizeFloat(TargetTime);
		auto* Start = Chain.PlacePure(CreateVariableGet(Graph, Blueprint, StartProperty));
		auto* Target = Chain.PlacePure(CreateVariableGet(Graph, Blueprint, TargetProperty));
		auto* LessNode = Call(Graph, Chain, Less);
		if (!Connect(Graph, Start->GetValuePin(), LessNode->FindPin(TEXT("A")))
			|| !Connect(Graph, Target->GetValuePin(), LessNode->FindPin(TEXT("B")))) return false;
		auto* Direction = CreateBranchNode(Graph);
		Chain.PlaceExec(Direction);
		if (!Connect(Graph, LessNode->GetReturnValuePin(), Direction->GetConditionPin()) || !LinkExecChain(Chain.ExecChain)) return false;

		auto MakePlay = [&](FChainLayout& Branch, bool bReverse) -> bool
		{
			UEdGraphPin* StartPin = Start->GetValuePin();
			if (bReverse)
			{
				auto* End = Call(Graph, Branch, EndTime);
				if (!Connect(Graph, Animation->GetValuePin(), End->FindPin(UEdGraphSchema_K2::PN_Self))) return false;
				auto* Delta = Call(Graph, Branch, Subtract);
				if (!Connect(Graph, End->GetReturnValuePin(), Delta->FindPin(TEXT("A")))
					|| !Connect(Graph, Start->GetValuePin(), Delta->FindPin(TEXT("B")))) return false;
				StartPin = Delta->GetReturnValuePin();
			}
			auto* PlayNode = Call(Graph, Branch, Play);
			if (!Connect(Graph, Animation->GetValuePin(), PlayNode->FindPin(TEXT("InAnimation")))
				|| !Connect(Graph, StartPin, PlayNode->FindPin(TEXT("StartAtTime")))
				|| !Connect(Graph, Target->GetValuePin(), PlayNode->FindPin(TEXT("EndAtTime")))) return false;
			PlayNode->FindPinChecked(TEXT("PlayMode"))->DefaultValue = bReverse ? TEXT("Reverse") : TEXT("Forward");
			PlayNode->FindPinChecked(TEXT("PlaybackSpeed"))->DefaultValue = FString::SanitizeFloat(Speed);
			PlayNode->FindPinChecked(TEXT("bRestoreState"))->DefaultValue = TEXT("false");
			return LinkExecChain(Branch.ExecChain);
		};
		FChainLayout Forward, Reverse;
		Forward.RowY = Chain.RowY + NodeRowStep;
		Reverse.RowY = Chain.RowY + NodeRowStep * 2;
		Forward.NextColumn = Reverse.NextColumn = Chain.NextColumn;
		if (!MakePlay(Forward, false) || !MakePlay(Reverse, true)) return false;
		Direction->GetThenPin()->MakeLinkTo(Forward.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		Direction->GetElsePin()->MakeLinkTo(Reverse.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		return true;
	}

bool ValidateAnimationPresetBindings(UWidgetBlueprint* Blueprint,
	const Design2UMGAnimationPresets::FBindingPlan& Plan, FString& OutError)
{
	if (!Blueprint || !Blueprint->WidgetTree || Blueprint->UbergraphPages.IsEmpty() || !Blueprint->SkeletonGeneratedClass)
	{
		OutError = TEXT("Blueprint, widget tree or event graph is unavailable."); return false;
	}
	UWidget* Target = Blueprint->WidgetTree->FindWidget(Plan.TargetWidgetName);
	if (!Target) { OutError = TEXT("The selected target widget is unavailable."); return false; }
	const FName ReplacedAnimation = Plan.EntranceAnimationName.IsNone() ? Plan.FeedbackAnimationName : Plan.EntranceAnimationName;
	for (const UWidgetAnimation* Existing : Blueprint->Animations)
	{
		if (!Existing) continue;
		if (Existing->GetFName() == ReplacedAnimation)
		{
			if (!Design2UMGAnimationPresets::IsOwnedAnimation(Existing))
			{
				OutError = FString::Printf(TEXT("Animation '%s' belongs to the user and is preserved."), *Existing->GetName());
				return false;
			}
			continue;
		}
		if (!Existing->MovieScene) continue;
		const UMovieScene* Scene = Existing->MovieScene;
		for (const FWidgetAnimationBinding& Binding : Existing->AnimationBindings)
		{
			if (Binding.WidgetName != Plan.TargetWidgetName) continue;
			for (const FMovieSceneBinding& SceneBinding : Scene->GetBindings())
			{
				if (SceneBinding.GetObjectGuid() != Binding.AnimationGuid) continue;
				for (const UMovieSceneTrack* Track : SceneBinding.GetTracks())
				{
					const auto* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track);
					if (!PropertyTrack) continue;
					const FName Property = PropertyTrack->GetPropertyName();
					if ((Plan.bTransform && Property == TEXT("RenderTransform"))
						|| (Plan.bOpacity && Property == TEXT("RenderOpacity")))
					{
						OutError = FString::Printf(TEXT("Animation '%s' already controls %s on the selected widget; its track is preserved."),
							*Existing->GetName(), *Property.ToString());
						return false;
					}
				}
			}
		}
	}
	if (!Plan.EntranceAnimationName.IsNone())
	{
		if (!EventCanBeExtended(FindConstruct(Blueprint))) { OutError = TEXT("Construct contains user-authored wiring; it is preserved."); return false; }
		return true;
	}
	if (!Cast<UButton>(Target) || Plan.FeedbackAnimationName.IsNone())
	{
		OutError = TEXT("Button feedback requires a native Button and one feedback animation."); return false;
	}
	for (const FName Delegate : {GET_MEMBER_NAME_CHECKED(UButton, OnHovered), GET_MEMBER_NAME_CHECKED(UButton, OnUnhovered),
		GET_MEMBER_NAME_CHECKED(UButton, OnPressed), GET_MEMBER_NAME_CHECKED(UButton, OnReleased)})
	{
		if (!EventCanBeExtended(FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, Delegate, Plan.TargetWidgetName)))
		{
			OutError = FString::Printf(TEXT("Button event %s contains user-authored wiring; it is preserved."), *Delegate.ToString()); return false;
		}
	}
	for (const FName Name : {TimeVariable(Plan.TargetWidgetName, TEXT("target")), TimeVariable(Plan.TargetWidgetName, TEXT("start"))})
	{
		const int32 Existing = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Name);
		FString Value;
		if ((Existing != INDEX_NONE && (!FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, Name, nullptr, PresetVariableMetadata, Value) || Value != TEXT("1")))
			|| (Existing == INDEX_NONE && FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass, Name)))
		{
			OutError = TEXT("Preset state variable conflicts with user content."); return false;
		}
	}
	return true;
}

bool WireAnimationPresetBindings(UWidgetBlueprint* Blueprint,
	const Design2UMGAnimationPresets::FBindingPlan& Plan, TArray<FString>& OutWarnings)
{
	FString Error;
	if (!ValidateAnimationPresetBindings(Blueprint, Plan, Error)) { OutWarnings.Add(Error); return false; }
	const bool bEntrance = !Plan.EntranceAnimationName.IsNone();
	const FString Marker = MarkerFor(Plan.TargetWidgetName, bEntrance);
	if (!RemovePresetGraph(Blueprint, Marker, Error)) { OutWarnings.Add(Error); return false; }
	UWidget* Widget = Blueprint->WidgetTree->FindWidget(Plan.TargetWidgetName);
	Widget->bIsVariable = true;
	if (!bEntrance && (!MakeTimeVariable(Blueprint, TimeVariable(Plan.TargetWidgetName, TEXT("target")))
		|| !MakeTimeVariable(Blueprint, TimeVariable(Plan.TargetWidgetName, TEXT("start")))))
	{
		OutWarnings.Add(TEXT("Preset time state could not be created without overwriting user content.")); return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	UEdGraph* Graph = Blueprint->UbergraphPages[0];
	TSet<UEdGraphNode*> Existing;
	for (UEdGraphNode* Node : Graph->Nodes) Existing.Add(Node);
	FObjectProperty* Animation = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass,
		bEntrance ? Plan.EntranceAnimationName : Plan.FeedbackAnimationName);
	if (!Animation) { OutWarnings.Add(TEXT("Generated preset animation is unavailable.")); return false; }
	bool bValid = true;
	if (bEntrance)
	{
		auto* Event = FindConstruct(Blueprint);
		if (!Event) Event = CreateMarkerNode<UK2Node_Event>(Graph, [](UK2Node_Event* Node)
		{
			Node->EventReference.SetExternalMember(TEXT("Construct"), UUserWidget::StaticClass());
			Node->bOverrideFunction = true;
		});
		FChainLayout Chain;
		auto* Get = Chain.PlacePure(CreateVariableGet(Graph, Blueprint, Animation));
		auto* Play = Call(Graph, Chain, UUserWidget::StaticClass()->FindFunctionByName(TEXT("PlayAnimation")));
		bValid = Play && Connect(Graph, Get->GetValuePin(), Play->FindPin(TEXT("InAnimation")))
			&& AttachEvent(Graph, Event, Play);
	}
	else
	{
		auto* TargetState = FindFProperty<FFloatProperty>(Blueprint->SkeletonGeneratedClass, TimeVariable(Plan.TargetWidgetName, TEXT("target")));
		auto* StartState = FindFProperty<FFloatProperty>(Blueprint->SkeletonGeneratedClass, TimeVariable(Plan.TargetWidgetName, TEXT("start")));
		auto* ButtonProperty = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, Plan.TargetWidgetName);
		if (!TargetState || !StartState || !ButtonProperty) { OutWarnings.Add(TEXT("Preset state or Button property is unavailable.")); return false; }
		const FName Delegates[] = {GET_MEMBER_NAME_CHECKED(UButton, OnHovered), GET_MEMBER_NAME_CHECKED(UButton, OnUnhovered),
			GET_MEMBER_NAME_CHECKED(UButton, OnPressed), GET_MEMBER_NAME_CHECKED(UButton, OnReleased)};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Delegates) && bValid; ++Index)
		{
			auto* Signature = FindFProperty<FMulticastDelegateProperty>(UButton::StaticClass(), Delegates[Index]);
			auto* Event = const_cast<UK2Node_ComponentBoundEvent*>(
				FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, Delegates[Index], Plan.TargetWidgetName));
			if (!Signature) { bValid = false; break; }
			if (!Event) Event = CreateMarkerNode<UK2Node_ComponentBoundEvent>(Graph, [&](UK2Node_ComponentBoundEvent* Node)
				{ Node->InitializeComponentBoundEventParams(ButtonProperty, Signature); });
			FChainLayout Chain;
			Chain.RowY = Index * NodeRowStep * 4;
			UEdGraphPin* DynamicTarget = nullptr;
			UK2Node_IfThenElse* PressGate = nullptr;
			if (Index == 0 || Index == 1 || Index == 3)
			{
				auto* Button = Chain.PlacePure(CreateVariableGet(Graph, Blueprint, ButtonProperty));
				UFunction* Query = Index == 3 ? UWidget::StaticClass()->FindFunctionByName(TEXT("IsHovered"))
					: UButton::StaticClass()->FindFunctionByName(TEXT("IsPressed"));
				auto* QueryNode = Call(Graph, Chain, Query);
				if (!QueryNode || !Connect(Graph, Button->GetValuePin(), QueryNode->FindPin(UEdGraphSchema_K2::PN_Self))) { bValid = false; break; }
				if (Index == 3)
				{
					auto* Select = Call(Graph, Chain, UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("SelectFloat")));
					if (!Select || !Connect(Graph, QueryNode->GetReturnValuePin(), Select->FindPin(TEXT("bPickA")))) { bValid = false; break; }
					Select->FindPinChecked(TEXT("A"))->DefaultValue = FString::SanitizeFloat(Plan.HoverTime);
					Select->FindPinChecked(TEXT("B"))->DefaultValue = FString::SanitizeFloat(Plan.IdleTime);
					DynamicTarget = Select->GetReturnValuePin();
				}
				else
				{
					PressGate = CreateBranchNode(Graph);
					Chain.PlaceExec(PressGate);
					if (!Connect(Graph, QueryNode->GetReturnValuePin(), PressGate->GetConditionPin())) { bValid = false; break; }
				}
			}
			const float Time = Index == 0 ? Plan.HoverTime : Index == 1 ? Plan.IdleTime : Plan.PressTime;
			const float Speed = Index >= 2 ? Plan.PressPlaybackSpeed : Plan.HoverPlaybackSpeed;
			FChainLayout Play;
			Play.RowY = Chain.RowY + NodeRowStep;
			Play.NextColumn = Chain.NextColumn;
			if (!WireAnimationTimeTarget(Graph, Blueprint, Animation, TargetState, StartState, DynamicTarget, Time, Speed, Play)) { bValid = false; break; }
			if (Chain.ExecChain.IsEmpty()) bValid = AttachEvent(Graph, Event, Play.ExecChain[0]);
			else
			{
				bValid = LinkExecChain(Chain.ExecChain) && AttachEvent(Graph, Event, Chain.ExecChain[0]);
				UEdGraphPin* Exit = PressGate ? PressGate->GetElsePin() : Chain.ExecChain.Last()->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
				if (bValid && Exit) Exit->MakeLinkTo(Play.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
				else bValid = false;
			}
		}
	}
	for (UEdGraphNode* Node : Graph->Nodes)
		if (Node && !Existing.Contains(Node)) Node->NodeComment = Marker;
	if (!bValid) OutWarnings.Add(TEXT("Preset graph could not be wired to the required native animation functions."));
	return bValid;
}
} // namespace Design2UMGPrototypeGraphWiring
