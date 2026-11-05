// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "Design2UMGPrototypeDragWiring.h"
#include "NativeTextAnimation.h"
#include "Animation/WidgetAnimation.h"
#include "K2Node_CreateDelegate.h"
#include "UMGSchemaSourceExtensions.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_SwitchInteger.h"
#include "Blueprint/UserWidget.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{
namespace
{

	bool AppendNativeRefreshBindings(UEdGraph* Graph, UWidgetBlueprint* WBP, bool bBind, FChainLayout& Layout)
	{
		UFunction* Function = UUserWidget::StaticClass()->FindFunctionByName(bBind ? TEXT("BindToAnimationFinished") : TEXT("UnbindFromAnimationFinished"));
		if (!Function) return false;
		for (UWidgetAnimation* Animation : WBP->Animations)
		{
			if (!Animation || !Animation->MovieScene) continue;
			const TArray<FName> Names = NativeTextAnimation::RefreshFunctions(WBP, Animation);
			if (Names.IsEmpty()) continue;
			auto* Property = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Animation->GetFName());
			if (!Property) return false;
			for (FName Name : Names)
			{
				auto* AnimationGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, Property));
				auto* Delegate = CreateMarkerNode<UK2Node_CreateDelegate>(Graph, [Name](UK2Node_CreateDelegate* Node) { Node->SetFunction(Name); });
				Layout.PlacePure(Delegate);
				auto* Call = CreateCallNode(Graph, Function);
				Layout.PlaceExec(Call);
				AnimationGet->GetValuePin()->MakeLinkTo(Call->FindPinChecked(TEXT("Animation")));
				if (!Graph->GetSchema()->TryCreateConnection(Delegate->GetDelegateOutPin(), Call->FindPinChecked(TEXT("Delegate")))) return false;
			}
		}
		return true;
	}

	bool AttachLifecycleEvent(UEdGraph* Graph, UK2Node_Event* Event, UEdGraphNode* Action)
	{
		auto* Then = Event ? Event->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output) : nullptr;
		auto* Execute = Action ? Action->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
		if (!Then || !Execute || Then->LinkedTo.Num() > 1) return false;
		auto* Bridge = CreateMarkerNode<UK2Node_ExecutionSequence>(Graph, [](UK2Node_ExecutionSequence*) {});
		Bridge->NodeComment = FString(NodeMarkerComment) + TEXT("|LifecycleBridge");
		if (!Then->LinkedTo.IsEmpty())
		{
			UEdGraphPin* Original = Then->LinkedTo[0];
			Then->BreakLinkTo(Original);
			Bridge->GetThenPinGivenIndex(0)->MakeLinkTo(Original);
		}
		Then->MakeLinkTo(Bridge->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		Bridge->GetThenPinGivenIndex(1)->MakeLinkTo(Execute);
		return true;
	}

	void AppendGeneratedIndexSet(UEdGraph* Graph, UWidgetBlueprint* WBP, FIntProperty* OwnerProperty,
		int32 Owner, FChainLayout& Layout)
	{
		if (!OwnerProperty) return;
		auto* Set = CreateMarkerNode<UK2Node_VariableSet>(Graph, [=](UK2Node_VariableSet* Node)
		{
			Node->SetFromProperty(OwnerProperty, true, WBP->SkeletonGeneratedClass);
		});
		Layout.PlaceExec(Set);
		Set->FindPinChecked(Set->GetVarName(), EGPD_Input)->DefaultValue = FString::FromInt(Owner);
	}

	void AppendOwnerGate(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		FIntProperty* OwnerProperty, int32 Expected, FChainLayout& Layout)
	{
		auto* Owner = Layout.PlacePure(CreateVariableGet(Graph, WBP, OwnerProperty));
		auto* Equal = Layout.PlacePure(CreateCallNode(Graph, Functions.EqualEqual_IntInt));
		Owner->GetValuePin()->MakeLinkTo(Equal->FindPinChecked(TEXT("A")));
		Equal->FindPinChecked(TEXT("B"))->DefaultValue = FString::FromInt(Expected);
		auto* Branch = CreateBranchNode(Graph);
		Layout.PlaceExec(Branch);
		Equal->GetReturnValuePin()->MakeLinkTo(Branch->GetConditionPin());
	}


	/** Follow panel parents and the owning native UserWidget across nested WidgetTrees. */
	bool IsWidgetInsideWidget(const UWidget* Widget, const UWidget* Ancestor)
	{
		for (const UWidget* Current = Widget; Current;)
		{
			if (Current == Ancestor) return true;
			if (const UWidget* Parent = Current->GetParent()) Current = Parent;
			else
			{
				const UWidgetTree* Tree = Current->GetTypedOuter<UWidgetTree>();
				Current = Tree ? Cast<UUserWidget>(Tree->GetOuter()) : nullptr;
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
		FChainLayout& Layout,
		bool bReverse = false, bool bDirectional = false, bool bRestoreState = false)
	{
		FObjectProperty* AnimationProperty = FindFProperty<FObjectProperty>(
			WBP->SkeletonGeneratedClass, AnimationPropertyName);
		if (!AnimationProperty)
		{
			return;
		}
		UK2Node_VariableGet* AnimationGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, AnimationProperty));
		UK2Node_CallFunction* PlayNode = CreateCallNode(Graph, bDirectional
			? (bReverse ? Functions.PlayAnimationReverse : Functions.PlayAnimationForward) : Functions.PlayAnimation);
		Layout.PlaceExec(PlayNode);
		// PlayAnimation runs on the UserWidget self; only the animation pin needs an explicit link.
		if (UEdGraphPin* AnimationPin = PlayNode->FindPin(TEXT("InAnimation"), EGPD_Input))
		{
			AnimationGet->GetValuePin()->MakeLinkTo(AnimationPin);
		}
		PlayNode->FindPinChecked(TEXT("bRestoreState"))->DefaultValue = bRestoreState ? TEXT("true") : TEXT("false");
	}


	bool AppendReversibleAnimation(UEdGraph* Graph, UWidgetBlueprint* WBP, const FResolvedBinding& Resolved,
		float TargetTime, FChainLayout& Layout)
	{
		auto* Animation = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Resolved.WidgetAnimationName);
		auto* Target = FindFProperty<FFloatProperty>(WBP->SkeletonGeneratedClass, Resolved.PlaybackTargetPropertyName);
		auto* Start = FindFProperty<FFloatProperty>(WBP->SkeletonGeneratedClass, Resolved.PlaybackStartPropertyName);
		return Animation && Target && Start
			&& WireAnimationTimeTarget(Graph, WBP, Animation, Target, Start, nullptr, TargetTime, 1.0f, Layout);
	}

	void AppendTimerCall(UEdGraph* Graph, UFunction* Function, FName EventName, FChainLayout& Layout, float Seconds = 0)
	{
		auto* Call = CreateCallNode(Graph, Function);
		Layout.PlaceExec(Call);
		Call->FindPinChecked(TEXT("FunctionName"))->DefaultValue = EventName.ToString();
		if (UEdGraphPin* Time = Call->FindPin(TEXT("Time"))) Time->DefaultValue = FString::SanitizeFloat(Seconds);
	}

	void AppendAnimationTimer(UEdGraph* Graph, const FEngineFunctions& Functions, FName EventName, float Seconds, FChainLayout& Layout)
	{
		AppendTimerCall(Graph, Functions.ClearTimer, EventName, Layout);
		AppendTimerCall(Graph, Seconds == 0 ? Functions.SetTimerForNextTick : Functions.SetTimer, EventName, Layout, Seconds);
	}


	bool AppendReversibleReturnWithCompletion(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FResolvedBinding& Resolved, FName EventName, FChainLayout& Layout)
	{
		auto* Animation = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Resolved.WidgetAnimationName);
		auto* Target = FindFProperty<FFloatProperty>(WBP->SkeletonGeneratedClass, Resolved.PlaybackTargetPropertyName);
		UFunction* IsPlaying = UUserWidget::StaticClass()->FindFunctionByName(TEXT("IsAnimationPlaying"));
		UFunction* GetTime = UUserWidget::StaticClass()->FindFunctionByName(TEXT("GetAnimationCurrentTime"));
		UFunction* Select = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("SelectFloat"));
		UFunction* AtZero = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("LessEqual_DoubleDouble"));
		if (!Animation || !Target || !IsPlaying || !GetTime || !Select || !AtZero || !Functions.ClearTimer
			|| !Functions.SetTimer || !Functions.SetTimerForNextTick || EventName.IsNone()) return false;
		auto* AnimationGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, Animation));
		auto* TargetGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, Target));
		auto* Playing = Layout.PlacePure(CreateCallNode(Graph, IsPlaying));
		auto* Current = Layout.PlacePure(CreateCallNode(Graph, GetTime));
		auto* Pick = Layout.PlacePure(CreateCallNode(Graph, Select));
		auto* Zero = Layout.PlacePure(CreateCallNode(Graph, AtZero));
		AnimationGet->GetValuePin()->MakeLinkTo(Playing->FindPinChecked(TEXT("InAnimation")));
		AnimationGet->GetValuePin()->MakeLinkTo(Current->FindPinChecked(TEXT("InAnimation")));
		if (!Graph->GetSchema()->TryCreateConnection(Current->GetReturnValuePin(), Pick->FindPinChecked(TEXT("A")))
			|| !Graph->GetSchema()->TryCreateConnection(TargetGet->GetValuePin(), Pick->FindPinChecked(TEXT("B")))) return false;
		Playing->GetReturnValuePin()->MakeLinkTo(Pick->FindPinChecked(TEXT("bPickA")));
		Pick->GetReturnValuePin()->MakeLinkTo(Zero->FindPinChecked(TEXT("A")));
		Zero->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("0.0");
		AppendTimerCall(Graph, Functions.ClearTimer, EventName, Layout);
		auto* Branch = CreateBranchNode(Graph);
		Layout.PlaceExec(Branch);
		Zero->GetReturnValuePin()->MakeLinkTo(Branch->GetConditionPin());
		if (!LinkExecChain(Layout.ExecChain)) return false;
		FChainLayout Immediate;
		Immediate.RowY = Layout.RowY + NodeRowStep;
		Immediate.NextColumn = Layout.NextColumn;
		AppendTimerCall(Graph, Functions.SetTimerForNextTick, EventName, Immediate);
		if (!LinkExecChain(Immediate.ExecChain)) return false;
		Branch->GetThenPin()->MakeLinkTo(Immediate.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		FChainLayout Reverse;
		Reverse.RowY = Layout.RowY + NodeRowStep * 2;
		Reverse.NextColumn = Layout.NextColumn;
		auto* Delay = CreateCallNode(Graph, Functions.SetTimer);
		Reverse.PlaceExec(Delay);
		Delay->FindPinChecked(TEXT("FunctionName"))->DefaultValue = EventName.ToString();
		if (!Graph->GetSchema()->TryCreateConnection(Pick->GetReturnValuePin(), Delay->FindPinChecked(TEXT("Time")))
			|| !AppendReversibleAnimation(Graph, WBP, Resolved, 0.0f, Reverse) || !LinkExecChain(Reverse.ExecChain)) return false;
		Branch->GetElsePin()->MakeLinkTo(Reverse.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		return true;
	}

	void AppendSourcePageGate(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FBindingGraphTargets& Targets, const FResolvedBinding& Resolved, FChainLayout& Layout)
	{
		if (Resolved.SourceOverlayPageIndex != INDEX_NONE)
		{
			auto* Stack = Layout.PlacePure(CreateVariableGet(Graph, WBP, Targets.OverlayStackProperty));
			auto* Empty = Layout.PlacePure(CreateArrayCallNode(Graph, Functions.ArrayIsEmpty));
			LinkArrayPin(Graph, Stack->GetValuePin(), Empty->FindPinChecked(TEXT("TargetArray")));
			auto* NotEmpty = Layout.PlacePure(CreateCallNode(Graph, Functions.Not_PreBool));
			Empty->GetReturnValuePin()->MakeLinkTo(NotEmpty->FindPinChecked(TEXT("A")));
			auto* HasOverlay = CreateBranchNode(Graph);
			Layout.PlaceExec(HasOverlay);
			NotEmpty->GetReturnValuePin()->MakeLinkTo(HasOverlay->GetConditionPin());
			auto* Last = Layout.PlacePure(CreateArrayCallNode(Graph, Functions.ArrayLastIndex));
			auto* Top = Layout.PlacePure(CreateArrayCallNode(Graph, Functions.ArrayGet));
			for (auto* Node : {Last, Top}) LinkArrayPin(Graph, Stack->GetValuePin(), Node->FindPinChecked(TEXT("TargetArray")));
			Last->GetReturnValuePin()->MakeLinkTo(Top->FindPinChecked(TEXT("Index")));
			auto* Equal = Layout.PlacePure(CreateCallNode(Graph, Functions.EqualEqual_IntInt));
			Top->FindPinChecked(TEXT("Item"), EGPD_Output)->MakeLinkTo(Equal->FindPinChecked(TEXT("A")));
			Equal->FindPinChecked(TEXT("B"))->DefaultValue = FString::FromInt(Resolved.SourceOverlayPageIndex);
			auto* Active = CreateBranchNode(Graph);
			Layout.PlaceExec(Active);
			Equal->GetReturnValuePin()->MakeLinkTo(Active->GetConditionPin());
			return;
		}
		if (!Targets.CurrentPageProperty) return;
		auto* Active = Layout.PlacePure(CreateVariableGet(Graph, WBP, Targets.CurrentPageProperty));
		auto* Equal = Layout.PlacePure(CreateCallNode(Graph, Functions.EqualEqual_IntInt));
		Active->GetValuePin()->MakeLinkTo(Equal->FindPinChecked(TEXT("A")));
		Equal->FindPinChecked(TEXT("B"))->DefaultValue = FString::FromInt(Resolved.SourcePageIndex);
		auto* Branch = CreateBranchNode(Graph);
		Layout.PlaceExec(Branch);
		Equal->GetReturnValuePin()->MakeLinkTo(Branch->GetConditionPin());
	}

	FName StateTimeoutDispatchName()
	{
		return BindingFunctionName(TEXT("state_timeouts"), 0);
	}

	void AppendStateTimeoutCall(UEdGraph* Graph, UWidgetBlueprint* WBP, FChainLayout& Layout)
	{
		if (FindOwnedFunctionGraph(WBP, StateTimeoutDispatchName()))
			AppendOwnedFunctionCall(Graph, WBP, Layout, StateTimeoutDispatchName());
	}


	UEdGraphPin* StateTimeoutActivePin(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FBindingPlan&, const FBindingGraphTargets& Targets, const FResolvedBinding& Resolved, FChainLayout& Layout)
	{
		const auto& Host = Targets.StateHosts[Resolved.StateHostIndex];
		auto* Current = Layout.PlacePure(CreateVariableGet(Graph, WBP, Host.CurrentStateProperty));
		auto* Selected = Layout.PlacePure(CreateCallNode(Graph, Functions.EqualEqual_IntInt));
		Current->GetValuePin()->MakeLinkTo(Selected->FindPinChecked(TEXT("A")));
		Selected->FindPinChecked(TEXT("B"))->DefaultValue = FString::FromInt(Resolved.SourceStateIndex);
		const FName VisibleName = BindingFunctionName(TEXT("state_visible"), 0);
		auto* Visible = Layout.PlacePure(CreateMarkerNode<UK2Node_CallFunction>(Graph, [&](UK2Node_CallFunction* Node)
			{ Node->FunctionReference.SetSelfMember(VisibleName, FindOwnedFunctionGraph(WBP, VisibleName)->GraphGuid); }));
		UWidget* Source = Host.StateWidgets[Resolved.SourceStateIndex];
		auto* Property = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Source->GetFName());
		auto* Get = Layout.PlacePure(CreateVariableGet(Graph, WBP, Property));
		Get->GetValuePin()->MakeLinkTo(Visible->FindPinChecked(TEXT("Widget")));
		auto* And = Layout.PlacePure(CreateCallNode(Graph, Functions.BooleanAND));
		Selected->GetReturnValuePin()->MakeLinkTo(And->FindPinChecked(TEXT("A")));
		Visible->GetReturnValuePin()->MakeLinkTo(And->FindPinChecked(TEXT("B")));
		return And->GetReturnValuePin();
	}

	void AppendPageOpenDispatch(UEdGraph* Graph, UWidgetBlueprint* WBP,
		const FUMGPrototypeNavigationData& Data, const FEngineFunctions& Functions,
		const FBindingPlan& Plan, const FBindingGraphTargets& Targets, FChainLayout& Layout)
	{
		if (Plan.PageOpenBindings.IsEmpty()) return;
		auto* Sequence = CreateMarkerNode<UK2Node_ExecutionSequence>(Graph, [](UK2Node_ExecutionSequence*) {});
		Layout.PlaceExec(Sequence);
		for (int32 Index = 0; Index < Plan.PageOpenBindings.Num(); ++Index)
		{
			if (Index >= 2) Sequence->AddInputPin();
			const auto& Resolved = Plan.PageOpenBindings[Index];
			if (Resolved.SourceOverlayPageIndex != INDEX_NONE || Resolved.StateHostIndex != INDEX_NONE) continue;
			FChainLayout Auto;
			Auto.NextColumn = Layout.NextColumn;
			Auto.RowY = Layout.RowY + (Index + 1) * NodeRowStep;
			AppendSourcePageGate(Graph, WBP, Functions, Targets, Resolved, Auto);
			AppendAnimationTimer(Graph, Functions, Resolved.PlaybackEventName,
				Data.ClickBindings[Resolved.BindingIndex].TriggerDelaySeconds, Auto);
			LinkExecChain(Auto.ExecChain);
			Sequence->GetThenPinGivenIndex(Index)->MakeLinkTo(Auto.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			Layout.NextColumn = FMath::Max(Layout.NextColumn, Auto.NextColumn);
		}
		FChainLayout States;
		AppendStateTimeoutCall(Graph, WBP, States);
		if (!States.ExecChain.IsEmpty())
		{
			if (Plan.PageOpenBindings.Num() >= 2) Sequence->AddInputPin();
			Sequence->GetThenPinGivenIndex(Plan.PageOpenBindings.Num())->MakeLinkTo(States.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		}
	}

	/**
	 * Stop the outgoing animation before resetting translation: its final evaluation
	 * can occur after the latent delay and would otherwise overwrite the reset.
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
		UK2Node_CallFunction* StopNode = CreateCallNode(Graph, Functions.StopAnimation);
		Layout.PlaceExec(StopNode);
		if (UEdGraphPin* AnimationPin = StopNode->FindPin(TEXT("InAnimation"), EGPD_Input))
		{
			AnimationGet->GetValuePin()->MakeLinkTo(AnimationPin);
		}
		return true;
	}


	bool AppendResetAnimationEvaluation(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FResolvedBinding& Resolved, FChainLayout& Layout)
	{
		auto* Animation = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Resolved.WidgetAnimationName);
		if (!Animation || !Functions.PlayAnimation || !Functions.SetAnimationCurrentTime
			|| !Functions.FlushAnimations || !Functions.StopAnimation) return false;
		AppendPlayAnimationCall(Graph, WBP, Functions, Resolved.WidgetAnimationName, Layout);
		auto* Get = Layout.PlacePure(CreateVariableGet(Graph, WBP, Animation));
		auto* Seek = CreateCallNode(Graph, Functions.SetAnimationCurrentTime);
		Layout.PlaceExec(Seek);
		Get->GetValuePin()->MakeLinkTo(Seek->FindPinChecked(TEXT("InAnimation")));
		Seek->FindPinChecked(TEXT("InTime"))->DefaultValue = TEXT("0.0");
		auto* Flush = CreateCallNode(Graph, Functions.FlushAnimations);
		Layout.PlaceExec(Flush);
		if (!AppendStopAnimationCall(Graph, WBP, Functions, Resolved.WidgetAnimationName, Layout)) return false;
		// Stop queues its final evaluation on the shared runner; finish it before restoring the live envelope.
		auto* StopFlush = CreateCallNode(Graph, Functions.FlushAnimations);
		Layout.PlaceExec(StopFlush);
		return true;
	}

	bool AppendResetReversibleAnimation(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FResolvedBinding& Resolved, FChainLayout& Layout)
	{
		auto* Target = FindFProperty<FFloatProperty>(WBP->SkeletonGeneratedClass, Resolved.PlaybackTargetPropertyName);
		auto* Start = FindFProperty<FFloatProperty>(WBP->SkeletonGeneratedClass, Resolved.PlaybackStartPropertyName);
		if (!Target || !Start || !AppendResetAnimationEvaluation(Graph, WBP, Functions, Resolved, Layout)) return false;
		for (FProperty* Property : {static_cast<FProperty*>(Target), static_cast<FProperty*>(Start)})
		{
			auto* Set = CreateMarkerNode<UK2Node_VariableSet>(Graph,
				[&](UK2Node_VariableSet* Node) { Node->SetFromProperty(Property, true, WBP->SkeletonGeneratedClass); });
			Layout.PlaceExec(Set);
			Set->FindPinChecked(Set->GetVarName(), EGPD_Input)->DefaultValue = TEXT("0.0");
		}
		return true;
	}

	bool AppendSetVisibilityCall(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		FObjectProperty* WidgetProperty, ESlateVisibility Visibility, FChainLayout& Layout);

	bool AppendOverlayOpen(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FBindingGraphTargets& Targets, const FResolvedBinding& Resolved, bool bMirrorTrigger, FChainLayout& Layout)
	{
		FObjectProperty* Page = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Resolved.OverlayPageWidgetName);
		if (!Page || !Targets.OverlayHostProperty) return false;
		if (!Targets.OverlayStackProperty || !Targets.OverlayZOrderProperty || !Functions.ArrayAdd || !Functions.ArrayRemoveItem
			|| !Functions.SlotAsCanvasSlot || !Functions.SetCanvasZOrder || !Functions.AddInt || Resolved.OverlayPageIndex == INDEX_NONE) return false;
		auto* Stack = Layout.PlacePure(CreateVariableGet(Graph, WBP, Targets.OverlayStackProperty));
		auto* RemoveExisting = CreateArrayCallNode(Graph, Functions.ArrayRemoveItem);
		Layout.PlaceExec(RemoveExisting);
		LinkArrayPin(Graph, Stack->GetValuePin(), RemoveExisting->FindPinChecked(TEXT("TargetArray")));
		RemoveExisting->FindPinChecked(TEXT("Item"))->DefaultValue = FString::FromInt(Resolved.OverlayPageIndex);
		auto* Push = CreateArrayCallNode(Graph, Functions.ArrayAdd);
		Layout.PlaceExec(Push);
		LinkArrayPin(Graph, Stack->GetValuePin(), Push->FindPinChecked(TEXT("TargetArray")));
		Push->FindPinChecked(TEXT("NewItem"))->DefaultValue = FString::FromInt(Resolved.OverlayPageIndex);
		auto* PageGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, Page));
		auto* CanvasSlot = Layout.PlacePure(CreateCallNode(Graph, Functions.SlotAsCanvasSlot));
		PageGet->GetValuePin()->MakeLinkTo(CanvasSlot->FindPinChecked(TEXT("Widget")));
		auto* CurrentZ = Layout.PlacePure(CreateVariableGet(Graph, WBP, Targets.OverlayZOrderProperty));
		auto* NextZ = Layout.PlacePure(CreateCallNode(Graph, Functions.AddInt));
		CurrentZ->GetValuePin()->MakeLinkTo(NextZ->FindPinChecked(TEXT("A")));
		NextZ->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("1");
		auto* SetNextZ = CreateMarkerNode<UK2Node_VariableSet>(Graph, [&](UK2Node_VariableSet* Node)
			{ Node->SetFromProperty(Targets.OverlayZOrderProperty, true, WBP->SkeletonGeneratedClass); });
		Layout.PlaceExec(SetNextZ);
		NextZ->GetReturnValuePin()->MakeLinkTo(SetNextZ->FindPinChecked(SetNextZ->GetVarName(), EGPD_Input));
		auto* ZOrder = CreateCallNode(Graph, Functions.SetCanvasZOrder);
		Layout.PlaceExec(ZOrder);
		CanvasSlot->GetReturnValuePin()->MakeLinkTo(ZOrder->FindPinChecked(UEdGraphSchema_K2::PN_Self));
		CurrentZ->GetValuePin()->MakeLinkTo(ZOrder->FindPinChecked(TEXT("InZOrder")));
		AppendGeneratedIndexSet(Graph, WBP, Targets.OverlayOwnerProperty, Resolved.BindingIndex * 2 + 1 + (bMirrorTrigger ? 1 : 0), Layout);
		if (!AppendSetVisibilityCall(Graph, WBP, Functions, Targets.OverlayHostProperty, ESlateVisibility::SelfHitTestInvisible, Layout)
			|| !AppendSetVisibilityCall(Graph, WBP, Functions, Page, ESlateVisibility::SelfHitTestInvisible, Layout)) return false;
		for (const auto& Timed : Targets.OverlayPageOpenBindings)
			if (Timed.SourceOverlayPageIndex == Resolved.OverlayPageIndex && Timed.StateHostIndex == INDEX_NONE)
				AppendAnimationTimer(Graph, Functions, Timed.PlaybackEventName, Timed.TriggerDelaySeconds, Layout);
		AppendStateTimeoutCall(Graph, WBP, Layout);
		return true;
	}

	/** Appends a native visibility setter for a generated widget variable. */
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
		UK2Node_CallFunction* VisibilityNode = CreateCallNode(Graph, Functions.SetVisibility);
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
		const FBindingGraphTargets& Targets,
		FChainLayout& Layout)
	{
		auto* OverlayStackProperty = Targets.OverlayStackProperty;
		for (const auto& Timed : Targets.OverlayPageOpenBindings)
		{
			if (Timed.StateHostIndex != INDEX_NONE)
				AppendOwnedFunctionCall(Graph, WBP, Layout, BindingFunctionName(TEXT("cancel_timed"), Timed.BindingIndex));
			else AppendTimerCall(Graph, Functions.ClearTimer, Timed.PlaybackEventName, Layout);
		}
		if (OverlayStackProperty && Functions.ArrayClear)
		{
			auto* Stack = Layout.PlacePure(CreateVariableGet(Graph, WBP, OverlayStackProperty));
			auto* Clear = CreateArrayCallNode(Graph, Functions.ArrayClear);
			Layout.PlaceExec(Clear);
			LinkArrayPin(Graph, Stack->GetValuePin(), Clear->FindPinChecked(TEXT("TargetArray")));
		}
		if (!AppendSetVisibilityCall(Graph, WBP, Functions,
			Targets.OverlayHostProperty, ESlateVisibility::Collapsed, Layout))
		{
			return false;
		}
		bool bValid = true;
		for (UWidget* HostedPage : Targets.OverlayHostPages)
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

	bool AppendStateSelection(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FStateHostGraphTargets& Host, int32 StateIndex, FChainLayout& Layout, bool bRunTimeouts = true)
	{
		if (!Host.CurrentStateProperty || !Host.StateWidgets.IsValidIndex(StateIndex)
			|| Host.StateWidgets.Num() != Host.StateVisibility.Num()) return false;
		for (int32 Index = 0; Index < Host.StateWidgets.Num(); ++Index)
		{
			UWidget* Widget = Host.StateWidgets[Index];
			auto* Property = Widget ? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Widget->GetFName()) : nullptr;
			const UEnum* VisibilityEnum = StaticEnum<ESlateVisibility>();
			const int64 Value = VisibilityEnum ? VisibilityEnum->GetValueByNameString(Host.StateVisibility[Index]) : INDEX_NONE;
			if (Index == StateIndex && Value == INDEX_NONE) return false;
			if (!AppendSetVisibilityCall(Graph, WBP, Functions, Property,
				Index == StateIndex ? static_cast<ESlateVisibility>(Value) : ESlateVisibility::Collapsed, Layout)) return false;
		}
		AppendGeneratedIndexSet(Graph, WBP, Host.CurrentStateProperty, StateIndex, Layout);
		if (bRunTimeouts) AppendStateTimeoutCall(Graph, WBP, Layout);
		return true;
	}

	bool AppendTemporaryStateVisibility(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FStateHostGraphTargets& Host, const FResolvedBinding& Resolved, bool bOpen, FChainLayout& Layout, bool bRunTimeouts = true)
	{
		if (!Functions.GetRenderOpacity || !Functions.SetRenderOpacity || !Host.SavedSourceOpacityProperty
			|| !Host.StateWidgets.IsValidIndex(Resolved.SourceStateIndex) || !Host.StateWidgets.IsValidIndex(Resolved.TargetStateIndex)
			|| !Host.OpacityWidgets.IsValidIndex(Resolved.SourceStateIndex)) return false;
		UWidget* Source = Host.StateWidgets[Resolved.SourceStateIndex];
		UWidget* Target = Host.StateWidgets[Resolved.TargetStateIndex];
		UWidget* OpacityOwner = Host.OpacityWidgets[Resolved.SourceStateIndex];
		auto* OwnerProperty = OpacityOwner ? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, OpacityOwner->GetFName()) : nullptr;
		auto* TargetProperty = Target ? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Target->GetFName()) : nullptr;
		if (!Source || !OwnerProperty || !TargetProperty) return false;
		auto* OwnerGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, OwnerProperty));
		FFloatProperty* ProducerOpacity = OpacityOwner != Source ? FindFProperty<FFloatProperty>(OpacityOwner->GetClass(), TEXT("SourceOpacity")) : nullptr;
		if (OpacityOwner != Source && !ProducerOpacity) return false;
		UEdGraphPin* CurrentOpacity = nullptr;
		if (bOpen)
		{
			if (ProducerOpacity)
			{
				auto* Get = CreateMarkerNode<UK2Node_VariableGet>(Graph, [&](UK2Node_VariableGet* Node)
					{ Node->SetFromProperty(ProducerOpacity, false, OpacityOwner->GetClass()); });
				Layout.PlacePure(Get);
				OwnerGet->GetValuePin()->MakeLinkTo(Get->FindPinChecked(UEdGraphSchema_K2::PN_Self));
				CurrentOpacity = Get->GetValuePin();
			}
			else
			{
				auto* Get = CreateCallNode(Graph, Functions.GetRenderOpacity);
				Layout.PlacePure(Get);
				OwnerGet->GetValuePin()->MakeLinkTo(Get->FindPinChecked(UEdGraphSchema_K2::PN_Self));
				CurrentOpacity = Get->GetReturnValuePin();
			}
			auto* Save = CreateMarkerNode<UK2Node_VariableSet>(Graph, [&](UK2Node_VariableSet* Node)
				{ Node->SetFromProperty(Host.SavedSourceOpacityProperty, true, WBP->SkeletonGeneratedClass); });
			Layout.PlaceExec(Save);
			CurrentOpacity->MakeLinkTo(Save->FindPinChecked(Save->GetVarName(), EGPD_Input));
		}
		UEdGraphPin* OpacityInput = nullptr;
		if (ProducerOpacity)
		{
			auto* Set = CreateMarkerNode<UK2Node_VariableSet>(Graph, [&](UK2Node_VariableSet* Node)
				{ Node->SetFromProperty(ProducerOpacity, false, OpacityOwner->GetClass()); });
			Layout.PlaceExec(Set);
			OwnerGet->GetValuePin()->MakeLinkTo(Set->FindPinChecked(UEdGraphSchema_K2::PN_Self));
			OpacityInput = Set->FindPinChecked(Set->GetVarName(), EGPD_Input);
		}
		else
		{
			auto* Set = CreateCallNode(Graph, Functions.SetRenderOpacity);
			Layout.PlaceExec(Set);
			OwnerGet->GetValuePin()->MakeLinkTo(Set->FindPinChecked(UEdGraphSchema_K2::PN_Self));
			OpacityInput = Set->FindPinChecked(TEXT("InOpacity"));
		}
		if (bOpen) OpacityInput->DefaultValue = TEXT("0.0");
		else
		{
			auto* Saved = Layout.PlacePure(CreateVariableGet(Graph, WBP, Host.SavedSourceOpacityProperty));
			Saved->GetValuePin()->MakeLinkTo(OpacityInput);
		}
		if (!AppendSetVisibilityCall(Graph, WBP, Functions, TargetProperty,
			bOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed, Layout)) return false;
		if (bRunTimeouts && bOpen) AppendStateTimeoutCall(Graph, WBP, Layout);
		return true;
	}


	bool AppendLayerOrders(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FUMGPrototypeNavigationBinding& Binding, const FResolvedBinding& Resolved, bool bTarget, FChainLayout& Layout)
	{
		const auto& Orders = Binding.Transition.LayerOrders;
		if (Orders.IsEmpty()) return true;
		if (Orders.Num() != Resolved.LayerWidgetNames.Num() || Orders.Num() != Resolved.LayerOriginalZOrders.Num()
			|| !Functions.SlotAsCanvasSlot || !Functions.SetCanvasZOrder) return false;
		for (int32 Index = 0; Index < Orders.Num(); ++Index)
		{
			auto* Property = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Resolved.LayerWidgetNames[Index]);
			if (!Property) return false;
			auto* Widget = Layout.PlacePure(CreateVariableGet(Graph, WBP, Property));
			auto* Slot = Layout.PlacePure(CreateCallNode(Graph, Functions.SlotAsCanvasSlot));
			Widget->GetValuePin()->MakeLinkTo(Slot->FindPinChecked(TEXT("Widget")));
			auto* Set = CreateCallNode(Graph, Functions.SetCanvasZOrder);
			Layout.PlaceExec(Set);
			Slot->GetReturnValuePin()->MakeLinkTo(Set->FindPinChecked(UEdGraphSchema_K2::PN_Self));
			Set->FindPinChecked(TEXT("InZOrder"))->DefaultValue = FString::FromInt(bTarget ? Orders[Index].EndIndex : Resolved.LayerOriginalZOrders[Index]);
		}
		return true;
	}


	bool AppendAvailableStateOwner(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		FIntProperty* OwnerProperty, int32 Owner, FChainLayout& Layout)
	{
		if (!OwnerProperty || !Functions.EqualEqual_IntInt || !Functions.BooleanOR) return false;
		auto* Get = Layout.PlacePure(CreateVariableGet(Graph, WBP, OwnerProperty));
		auto* Available = Layout.PlacePure(CreateCallNode(Graph, Functions.EqualEqual_IntInt));
		auto* Same = Layout.PlacePure(CreateCallNode(Graph, Functions.EqualEqual_IntInt));
		for (auto* Equal : {Available, Same}) Get->GetValuePin()->MakeLinkTo(Equal->FindPinChecked(TEXT("A")));
		Available->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("0");
		Same->FindPinChecked(TEXT("B"))->DefaultValue = FString::FromInt(Owner);
		auto* Either = Layout.PlacePure(CreateCallNode(Graph, Functions.BooleanOR));
		Available->GetReturnValuePin()->MakeLinkTo(Either->FindPinChecked(TEXT("A")));
		Same->GetReturnValuePin()->MakeLinkTo(Either->FindPinChecked(TEXT("B")));
		auto* Gate = CreateBranchNode(Graph);
		Layout.PlaceExec(Gate);
		Either->GetReturnValuePin()->MakeLinkTo(Gate->GetConditionPin());
		return true;
	}


	bool AppendTemporaryPageVisibility(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FBindingGraphTargets& Targets, const FResolvedBinding& Resolved, bool bShow, FChainLayout& Layout)
	{
		if (!Targets.PageHostPages.IsValidIndex(Resolved.TargetPageIndex) || !Functions.SlotAsCanvasSlot || !Functions.SetCanvasZOrder) return false;
		UWidget* Target = Targets.PageHostPages[Resolved.TargetPageIndex];
		auto* Property = Target ? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Target->GetFName()) : nullptr;
		if (!Property) return false;
		auto* Widget = Layout.PlacePure(CreateVariableGet(Graph, WBP, Property));
		auto* Slot = Layout.PlacePure(CreateCallNode(Graph, Functions.SlotAsCanvasSlot));
		Widget->GetValuePin()->MakeLinkTo(Slot->FindPinChecked(TEXT("Widget")));
		auto* SetZ = CreateCallNode(Graph, Functions.SetCanvasZOrder);
		Layout.PlaceExec(SetZ);
		Slot->GetReturnValuePin()->MakeLinkTo(SetZ->FindPinChecked(UEdGraphSchema_K2::PN_Self));
		SetZ->FindPinChecked(TEXT("InZOrder"))->DefaultValue = FString::FromInt(bShow ? Resolved.TemporaryPageZ : Resolved.TargetPageOriginalZ);
		return AppendSetVisibilityCall(Graph, WBP, Functions, Property,
			bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed, Layout);
	}

	bool AppendTemporaryPageStart(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FBindingGraphTargets& Targets, const FUMGPrototypeNavigationBinding& Binding,
		const FResolvedBinding& Resolved, FChainLayout& Layout)
	{
		if (!Targets.CurrentPageProperty || !Targets.TransitionOwnerProperty) return false;
		AppendOwnerGate(Graph, WBP, Functions, Targets.CurrentPageProperty, Resolved.SourcePageIndex, Layout);
		if (!AppendAvailableStateOwner(Graph, WBP, Functions, Targets.TransitionOwnerProperty, Resolved.BindingIndex + 1, Layout)) return false;
		if (Resolved.NavigationEventName != NAME_None) AppendTimerCall(Graph, Functions.ClearTimer, Resolved.NavigationEventName, Layout);
		AppendGeneratedIndexSet(Graph, WBP, Targets.TransitionOwnerProperty, Resolved.BindingIndex + 1, Layout);
		if (!AppendTemporaryPageVisibility(Graph, WBP, Functions, Targets, Resolved, true, Layout)
			|| !AppendLayerOrders(Graph, WBP, Functions, Binding, Resolved, true, Layout)) return false;
		AppendLivePresentationCall(Graph, WBP, Functions, Resolved, true, Layout);
		return Resolved.WidgetAnimationName.IsNone()
			|| AppendReversibleAnimation(Graph, WBP, Resolved, Binding.Transition.DurationSeconds, Layout);
	}

	bool AppendStateTransitionStart(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FBindingGraphTargets& Targets, const FUMGPrototypeNavigationBinding& Binding,
		const FResolvedBinding& Resolved, FChainLayout& Layout)
	{
		if (!Targets.StateHosts.IsValidIndex(Resolved.StateHostIndex) || !Functions.EqualEqual_IntInt) return false;
		const auto& Host = Targets.StateHosts[Resolved.StateHostIndex];
		AppendOwnerGate(Graph, WBP, Functions, Host.CurrentStateProperty, Resolved.SourceStateIndex, Layout);
		const bool bTemporaryDissolve = Resolved.bTemporaryState && Binding.Transition.Type == TEXT("DISSOLVE");
		const bool bTemporaryLive = Resolved.bTemporaryState && !Resolved.LivePresentations.IsEmpty();
		if (bTemporaryDissolve || bTemporaryLive)
		{
			if (!AppendAvailableStateOwner(Graph, WBP, Functions, Host.TransitionOwnerProperty, Resolved.BindingIndex + 1, Layout)) return false;
		}
		else AppendOwnerGate(Graph, WBP, Functions, Host.TransitionOwnerProperty, 0, Layout);
		if (!AppendLayerOrders(Graph, WBP, Functions, Binding, Resolved, true, Layout)) return false;
		if (Binding.Transition.bStateInstant)
		{
			if (Resolved.bTemporaryState)
			{
				AppendGeneratedIndexSet(Graph, WBP, Host.TransitionOwnerProperty, Resolved.BindingIndex + 1, Layout);
				return AppendTemporaryStateVisibility(Graph, WBP, Functions, Host, Resolved, true, Layout);
			}
			return AppendStateSelection(Graph, WBP, Functions, Host, Resolved.TargetStateIndex, Layout);
		}
		if (bTemporaryLive) AppendGeneratedIndexSet(Graph, WBP, Host.TransitionOwnerProperty, Resolved.BindingIndex + 1, Layout);
		AppendLivePresentationCall(Graph, WBP, Functions, Resolved, true, Layout);
		if (Binding.UsesHoverTrigger() || Binding.UsesPressTrigger())
		{
			if (bTemporaryDissolve)
			{
				AppendTimerCall(Graph, Functions.ClearTimer, Resolved.StateCompletionEventName, Layout);
				AppendGeneratedIndexSet(Graph, WBP, Host.TransitionOwnerProperty, Resolved.BindingIndex + 1, Layout);
				UWidget* Target = Host.StateWidgets.IsValidIndex(Resolved.TargetStateIndex) ? Host.StateWidgets[Resolved.TargetStateIndex] : nullptr;
				auto* Property = Target ? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Target->GetFName()) : nullptr;
				if (!AppendSetVisibilityCall(Graph, WBP, Functions, Property, ESlateVisibility::HitTestInvisible, Layout)) return false;
				AppendStateTimeoutCall(Graph, WBP, Layout);
			}
			return AppendReversibleAnimation(Graph, WBP, Resolved, Binding.Transition.DurationSeconds, Layout);
		}
		AppendGeneratedIndexSet(Graph, WBP, Host.TransitionOwnerProperty, Resolved.BindingIndex + 1, Layout);
		if (Binding.Transition.Type == TEXT("DISSOLVE"))
		{
			UWidget* Target = Host.StateWidgets.IsValidIndex(Resolved.TargetStateIndex) ? Host.StateWidgets[Resolved.TargetStateIndex] : nullptr;
			auto* Property = Target ? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Target->GetFName()) : nullptr;
			if (!AppendSetVisibilityCall(Graph, WBP, Functions, Property, ESlateVisibility::HitTestInvisible, Layout)) return false;
		}
		if (!AppendStopAnimationCall(Graph, WBP, Functions, Resolved.WidgetAnimationName, Layout)) return false;
		AppendPlayAnimationCall(Graph, WBP, Functions, Resolved.WidgetAnimationName, Layout, false, false, true);
		AppendAnimationTimer(Graph, Functions, Resolved.StateCompletionEventName, Binding.Transition.DurationSeconds, Layout);
		return true;
	}

	bool AppendPageTransitionStart(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FBindingGraphTargets& Targets, const FUMGPrototypeNavigationBinding& Binding,
		const FResolvedBinding& Resolved, FChainLayout& Layout)
	{
		const bool bPageTransition = Binding.Transition.Type != TEXT("SMART_ANIMATE");
		if (bPageTransition)
		{
			if (!Targets.TransitionOwnerProperty || !Functions.EqualEqual_IntInt
				|| !Targets.PageHostPages.IsValidIndex(Resolved.SourcePageIndex)
				|| !Targets.PageHostPages.IsValidIndex(Resolved.TargetPageIndex)
				|| Resolved.SourcePageIndex == Resolved.TargetPageIndex) return false;
			AppendOwnerGate(Graph, WBP, Functions, Targets.TransitionOwnerProperty, 0, Layout);
			AppendGeneratedIndexSet(Graph, WBP, Targets.TransitionOwnerProperty, Resolved.BindingIndex + 1, Layout);
			if (!AppendTemporaryPageVisibility(Graph, WBP, Functions, Targets, Resolved, true, Layout)) return false;
		}
		AppendLivePresentationCall(Graph, WBP, Functions, Resolved, true, Layout);
		if (!AppendLayerOrders(Graph, WBP, Functions, Binding, Resolved, true, Layout)
			|| !AppendStopAnimationCall(Graph, WBP, Functions, Resolved.WidgetAnimationName, Layout)) return false;
		AppendPlayAnimationCall(Graph, WBP, Functions, Resolved.WidgetAnimationName, Layout, false, false, true);
		AppendAnimationTimer(Graph, Functions, Resolved.NavigationEventName, Binding.Transition.DurationSeconds, Layout);
		return true;
	}

	bool AuthorHoverOverlayMonitor(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FBindingGraphTargets& Targets, const FResolvedBinding& Resolved, FName TriggerName, FName EventName, int32 RowY)
	{
		FObjectProperty* Trigger = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, TriggerName);
		if (!Trigger || !Targets.OverlayOwnerProperty || EventName.IsNone()) return false;
		auto* Event = CreateMarkerNode<UK2Node_CustomEvent>(Graph, [EventName](UK2Node_CustomEvent* Node) { Node->CustomFunctionName = EventName; });
		FChainLayout Chain;
		Chain.RowY = RowY;
		Chain.PlaceExec(Event);
		auto* Owner = Chain.PlacePure(CreateVariableGet(Graph, WBP, Targets.OverlayOwnerProperty));
		auto* Equal = Chain.PlacePure(CreateCallNode(Graph, Functions.EqualEqual_IntInt));
		Owner->GetValuePin()->MakeLinkTo(Equal->FindPinChecked(TEXT("A")));
		Equal->FindPinChecked(TEXT("B"))->DefaultValue = FString::FromInt(Resolved.BindingIndex * 2 + 1
			+ (TriggerName == Resolved.MirrorTriggerWidgetName ? 1 : 0));
		auto* OwnerBranch = CreateBranchNode(Graph);
		Chain.PlaceExec(OwnerBranch);
		Equal->GetReturnValuePin()->MakeLinkTo(OwnerBranch->GetConditionPin());
		auto* TriggerGet = Chain.PlacePure(CreateVariableGet(Graph, WBP, Trigger));
		auto* Geometry = CreateCallNode(Graph, Functions.GetCachedGeometry);
		Chain.PlacePure(Geometry);
		TriggerGet->GetValuePin()->MakeLinkTo(Geometry->FindPinChecked(UEdGraphSchema_K2::PN_Self));
		auto* Mouse = CreateCallNode(Graph, Functions.GetMousePositionOnPlatform);
		Chain.PlaceExec(Mouse);
		auto* Contains = Chain.PlacePure(CreateCallNode(Graph, Functions.IsUnderLocation));
		Geometry->GetReturnValuePin()->MakeLinkTo(Contains->FindPinChecked(TEXT("Geometry")));
		Mouse->GetReturnValuePin()->MakeLinkTo(Contains->FindPinChecked(TEXT("AbsoluteCoordinate")));
		auto* Inside = CreateBranchNode(Graph);
		Chain.PlaceExec(Inside);
		Contains->GetReturnValuePin()->MakeLinkTo(Inside->GetConditionPin());
		AppendTimerCall(Graph, Functions.SetTimerForNextTick, EventName, Chain);
		if (!LinkExecChain(Chain.ExecChain)) return false;
		FChainLayout Close;
		Close.NextColumn = Chain.NextColumn;
		Close.RowY = RowY + NodeRowStep;
		if (Resolved.WidgetAnimationName != NAME_None) AppendStopAnimationCall(Graph, WBP, Functions, Resolved.WidgetAnimationName, Close);
		AppendGeneratedIndexSet(Graph, WBP, Targets.OverlayOwnerProperty, 0, Close);
		if (!AppendCloseOverlayTail(Graph, WBP, Functions, Targets, Close)
			|| !LinkExecChain(Close.ExecChain)) return false;
		Inside->GetElsePin()->MakeLinkTo(Close.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		return true;
	}

	bool AuthorPressOverlayRelease(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
		const FBindingGraphTargets& Targets, const FResolvedBinding& Resolved, FName TriggerName, int32 RowY)
	{
		auto* Property = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, TriggerName);
		auto* Signature = FindFProperty<FMulticastDelegateProperty>(UButton::StaticClass(), GET_MEMBER_NAME_CHECKED(UButton, OnReleased));
		if (!Property || !Signature || !Targets.OverlayOwnerProperty || !Functions.EqualEqual_IntInt) return false;
		auto* Event = CreateMarkerNode<UK2Node_ComponentBoundEvent>(Graph, [&](UK2Node_ComponentBoundEvent* Node)
		{
			Node->InitializeComponentBoundEventParams(Property, Signature);
		});
		FChainLayout Chain;
		Chain.RowY = RowY;
		Chain.PlaceExec(Event);
		AppendOwnerGate(Graph, WBP, Functions, Targets.OverlayOwnerProperty,
			Resolved.BindingIndex * 2 + 1 + (TriggerName == Resolved.MirrorTriggerWidgetName ? 1 : 0), Chain);
		if (Resolved.WidgetAnimationName != NAME_None) AppendStopAnimationCall(Graph, WBP, Functions, Resolved.WidgetAnimationName, Chain);
		AppendGeneratedIndexSet(Graph, WBP, Targets.OverlayOwnerProperty, 0, Chain);
		return AppendCloseOverlayTail(Graph, WBP, Functions, Targets, Chain)
			&& LinkExecChain(Chain.ExecChain);
	}


}




void AppendLivePresentationCall(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
	const FResolvedBinding& Resolved, bool bEnter, FChainLayout& Layout, bool bRefreshTimeouts)
{
	if (Resolved.LivePresentations.IsEmpty()) return;
	if (bEnter && !Resolved.LiveReturnEventName.IsNone())
		AppendTimerCall(Graph, Functions.ClearTimer, Resolved.LiveReturnEventName, Layout);
	AppendOwnedFunctionCall(Graph, WBP, Layout,
		BindingFunctionName(bEnter ? TEXT("live_enter") : TEXT("live_exit"), Resolved.BindingIndex));
	if (bRefreshTimeouts) AppendStateTimeoutCall(Graph, WBP, Layout);
}

bool AppendPrototypeTemporaryPageVisibility(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
	const FBindingGraphTargets& Targets, const FResolvedBinding& Resolved, bool bShow, FChainLayout& Layout)
{
	return AppendTemporaryPageVisibility(Graph, WBP, Functions, Targets, Resolved, bShow, Layout);
}

bool AppendPrototypeLayerOrders(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
	const FUMGPrototypeNavigationBinding& Binding, const FResolvedBinding& Resolved, bool bTarget, FChainLayout& Layout)
{
	return AppendLayerOrders(Graph, WBP, Functions, Binding, Resolved, bTarget, Layout);
}

bool AppendCloseOverlayTail(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
	const FBindingGraphTargets& Targets, FChainLayout& Layout)
{
	if (!Targets.OverlayHostProperty || !Targets.OverlayStackProperty) return true;
	if (!Functions.ArrayIsEmpty || !Functions.ArrayLastIndex || !Functions.ArrayGet || !Functions.ArrayRemove) return false;
	auto* Stack = Layout.PlacePure(CreateVariableGet(Graph, WBP, Targets.OverlayStackProperty));
	auto* Empty = Layout.PlacePure(CreateArrayCallNode(Graph, Functions.ArrayIsEmpty));
	auto* Last = Layout.PlacePure(CreateArrayCallNode(Graph, Functions.ArrayLastIndex));
	auto* Value = Layout.PlacePure(CreateArrayCallNode(Graph, Functions.ArrayGet));
	for (auto* Node : {Empty, Last, Value}) LinkArrayPin(Graph, Stack->GetValuePin(), Node->FindPinChecked(TEXT("TargetArray")));
	Last->GetReturnValuePin()->MakeLinkTo(Value->FindPinChecked(TEXT("Index")));
	auto* Branch = CreateBranchNode(Graph);
	Layout.PlaceExec(Branch);
	Empty->GetReturnValuePin()->MakeLinkTo(Branch->FindPinChecked(UEdGraphSchema_K2::PN_Condition));
	auto* Switch = CreateMarkerNode<UK2Node_SwitchInteger>(Graph, [](UK2Node_SwitchInteger* Node) { Node->StartIndex = 0; });
	Switch->NodePosX = Layout.NextColumn++ * NodeColumnStep;
	Switch->NodePosY = Layout.RowY;
	Branch->FindPinChecked(UEdGraphSchema_K2::PN_Else)->MakeLinkTo(Switch->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
	Value->FindPinChecked(TEXT("Item"), EGPD_Output)->MakeLinkTo(Switch->GetSelectionPin());
	for (int32 Index = 0; Index < Targets.OverlayHostPages.Num(); ++Index)
	{
		Switch->AddPinToSwitchNode();
		FChainLayout Case;
		Case.NextColumn = Layout.NextColumn;
		Case.RowY = Layout.RowY + (Index + 1) * NodeRowStep;
		auto* Page = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Targets.OverlayHostPages[Index]->GetFName());
		for (const auto& Timed : Targets.OverlayPageOpenBindings)
			if (Timed.SourceOverlayPageIndex == Index)
			{
				if (Timed.StateHostIndex != INDEX_NONE)
					AppendOwnedFunctionCall(Graph, WBP, Case, BindingFunctionName(TEXT("cancel_timed"), Timed.BindingIndex));
				else AppendTimerCall(Graph, Functions.ClearTimer, Timed.PlaybackEventName, Case);
			}
		if (!AppendSetVisibilityCall(Graph, WBP, Functions, Page, ESlateVisibility::Collapsed, Case)) return false;
		auto* Remove = CreateArrayCallNode(Graph, Functions.ArrayRemove);
		Case.PlaceExec(Remove);
		LinkArrayPin(Graph, Stack->GetValuePin(), Remove->FindPinChecked(TEXT("TargetArray")));
		Last->GetReturnValuePin()->MakeLinkTo(Remove->FindPinChecked(TEXT("IndexToRemove")));
		AppendGeneratedIndexSet(Graph, WBP, Targets.OverlayOwnerProperty, 0, Case);
		auto* After = CreateBranchNode(Graph);
		Case.PlaceExec(After);
		Empty->GetReturnValuePin()->MakeLinkTo(After->FindPinChecked(UEdGraphSchema_K2::PN_Condition));
		FChainLayout Hide;
		Hide.NextColumn = Case.NextColumn;
		Hide.RowY = Case.RowY;
		if (!AppendSetVisibilityCall(Graph, WBP, Functions, Targets.OverlayHostProperty, ESlateVisibility::Collapsed, Hide)
			|| !LinkExecChain(Case.ExecChain) || !LinkExecChain(Hide.ExecChain)) return false;
		After->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(Hide.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		Switch->FindPinChecked(Switch->GetPinNameGivenIndex(Index), EGPD_Output)->MakeLinkTo(Case.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		Layout.NextColumn = FMath::Max(Layout.NextColumn, Hide.NextColumn);
	}
	return true;
}

static bool HasMouseCancellation(const FUMGPrototypeNavigationBinding& Binding, const FResolvedBinding& Resolved)
{
    return !Resolved.HoverMonitorEventName.IsNone() || !Resolved.MirrorHoverMonitorEventName.IsNone()
        || !Binding.Transition.LayerOrders.IsEmpty() || Resolved.bTemporaryNavigate
        || (!Resolved.WidgetAnimationName.IsNone() && Resolved.PlaybackTargetPropertyName.IsNone()
            && (Resolved.NavigationEventName.IsNone() || Resolved.bTemporaryNavigate));
}

TArray<FName> GetMouseFunctionNames(const FUMGPrototypeNavigationData& Navigation, const FBindingPlan& Plan)
{
    TArray<FName> Names;
    if (Plan.PageOpenBindings.ContainsByPredicate([](const FResolvedBinding& Binding) { return Binding.StateHostIndex != INDEX_NONE; }))
        { Names.Add(StateTimeoutDispatchName()); Names.Add(BindingFunctionName(TEXT("state_visible"), 0)); }
    const auto Add = [&](FName Name) { if (!Name.IsNone()) Names.AddUnique(Name); };
    const auto AddLive = [&](const FResolvedBinding& Binding)
    {
        if (Binding.LivePresentations.IsEmpty()) return;
        Add(BindingFunctionName(TEXT("live_enter"), Binding.BindingIndex));
        Add(BindingFunctionName(TEXT("live_exit"), Binding.BindingIndex));
        Add(Binding.LiveReturnEventName);
    };
    TArray<FResolvedBinding> Timed = Plan.PageOpenBindings;
    for (const auto& Binding : Plan.MouseBindings)
        if (!Binding.NavigationEventName.IsNone() || !Binding.StateCompletionEventName.IsNone()) Timed.Add(Binding);
    for (const auto& Binding : Timed)
    {
        AddLive(Binding);
        Add(BindingFunctionName(TEXT("cancel_timed"), Binding.BindingIndex));
        Add(Binding.NavigationEventName); Add(Binding.StateCompletionEventName); Add(Binding.PlaybackEventName);
        if (!Binding.bTemporaryNavigate && !Binding.NavigationEventName.IsNone()
            && Navigation.ClickBindings[Binding.BindingIndex].Transition.Type != TEXT("SMART_ANIMATE"))
            Add(BindingFunctionName(TEXT("restore_page"), Binding.BindingIndex));
    }
    for (const auto& Binding : Plan.MouseBindings)
    {
        AddLive(Binding);
        const auto& Source = Navigation.ClickBindings[Binding.BindingIndex];
        if (Binding.bDrag)
        {
            Add(BindingFunctionName(TEXT("cancel_drag"), Binding.DragLeaderIndex));
            Add(BindingFunctionName(TEXT("restore_drag"), Binding.DragLeaderIndex));
            continue;
        }
        Add(BindingFunctionName(TEXT("trigger"), Binding.BindingIndex));
        if (!Binding.MirrorTriggerWidgetName.IsNone()) Add(BindingFunctionName(TEXT("mirror_trigger"), Binding.BindingIndex));
        if (HasMouseCancellation(Source, Binding)) Add(BindingFunctionName(TEXT("cancel_mouse"), Binding.BindingIndex));
        if (!Binding.PlaybackTargetPropertyName.IsNone()) Add(BindingFunctionName(TEXT("restore_animation"), Binding.BindingIndex));
        if (Binding.bTemporaryState && Source.Transition.bStateInstant) Add(BindingFunctionName(TEXT("restore_instant"), Binding.BindingIndex));
        if ((Source.UsesHoverTrigger() || Source.UsesPressTrigger())
            && (((!Binding.WidgetAnimationName.IsNone() || (Binding.bTemporaryState && Source.Transition.bStateInstant))
                && Source.Steps[0].Kind == EUMGPrototypeNavigationStepKind::AnimateWidget) || Binding.bTemporaryNavigate))
            Add(BindingFunctionName(TEXT("release"), Binding.BindingIndex));
    }
    if (!Timed.IsEmpty() || Plan.MouseBindings.ContainsByPredicate([](const FResolvedBinding& Binding)
        { return !Binding.WidgetAnimationName.IsNone() || !Binding.HoverMonitorEventName.IsNone()
            || Binding.bPressOverlay || Binding.bTemporaryState || Binding.bTemporaryNavigate; }))
    {
        for (int32 Index = 0; Index < Navigation.StateHosts.Num(); ++Index) Add(BindingFunctionName(TEXT("restore_host"), Index));
        if (Plan.PageHost) Add(BindingFunctionName(TEXT("restore_current_page"), 0));
    }
    return Names;
}

int32 WireMouseBindings(UWidgetBlueprint* WBP, UEdGraph* Ubergraph,
	const FUMGPrototypeNavigationData& NavigationData, const FEngineFunctions& Functions,
	const FBindingPlan& Plan, const FBindingGraphTargets& Targets, TArray<FString>& OutWarnings)
{
	TMap<UEdGraph*, TSet<UEdGraphNode*>> ExistingNodes;
    const auto WatchGraph = [&](UEdGraph* Graph)
    {
        auto& Nodes = ExistingNodes.FindOrAdd(Graph);
        for (UEdGraphNode* Node : Graph->Nodes) Nodes.Add(Node);
    };
    WatchGraph(Ubergraph);
    for (FName Name : GetMouseFunctionNames(NavigationData, Plan))
        if (UEdGraph* Graph = FindOwnedFunctionGraph(WBP, Name)) WatchGraph(Graph);
	ON_SCOPE_EXIT
	{
		for (const auto& Pair : ExistingNodes)
            for (UEdGraphNode* Node : Pair.Key->Nodes)
                if (Node && !Pair.Value.Contains(Node) && Node->NodeComment.IsEmpty()) Node->NodeComment = NodeMarkerComment;
	};
	UCanvasPanel* PageHost = Plan.PageHost;
	const FString& PageHostBaseName = Plan.PageHostBaseName;
	UCanvasPanel* OverlayHost = Plan.OverlayHost;
	const auto& MouseBindings = Plan.MouseBindings;
	FObjectProperty* OverlayHostProperty = Targets.OverlayHostProperty;
	FArrayProperty* HistoryProperty = Targets.HistoryProperty;
	const auto& OverlayHostPages = Targets.OverlayHostPages;
	TMap<int32, TArray<FDragCandidate>> DragGroups;
	for (const FResolvedBinding& Resolved : MouseBindings)
	{
		if (!Resolved.bDrag || Resolved.DragLeaderIndex == INDEX_NONE) continue;
		FDragCandidate Candidate;
		Candidate.Binding = &NavigationData.ClickBindings[Resolved.BindingIndex];
		Candidate.Resolved = &Resolved;
		Candidate.CommitEventName = FName(*FString::Printf(TEXT("__d2u_drag_%03d_commit"), Resolved.BindingIndex));
		Candidate.CancelEventName = FName(*FString::Printf(TEXT("__d2u_drag_%03d_cancel"), Resolved.BindingIndex));
		DragGroups.FindOrAdd(Resolved.DragLeaderIndex).Add(Candidate);
	}
	TArray<FResolvedBinding> LiveBindings = Plan.PageOpenBindings;
	LiveBindings.Append(Plan.MouseBindings);
	for (const auto& Resolved : LiveBindings)
	{
		if (Resolved.LivePresentations.IsEmpty()) continue;
		for (bool bEnter : {true, false})
		{
			const FName Name = BindingFunctionName(bEnter ? TEXT("live_enter") : TEXT("live_exit"), Resolved.BindingIndex);
			auto* Entry = FindOwnedFunctionEntry(WBP, Name);
			UEdGraph* Graph = Entry->GetGraph();
			FChainLayout Chain; Chain.PlaceExec(Entry);
			auto* Sequence = CreateMarkerNode<UK2Node_ExecutionSequence>(Graph, [](UK2Node_ExecutionSequence*) {});
			const bool bLocalEndpoints = !NavigationData.ClickBindings[Resolved.BindingIndex].Transition.LocalEndpoints.IsEmpty();
			const auto AppendEnvelope = [&](FChainLayout& Envelope)
			{
				if (!Resolved.LayoutOriginalWidgetName.IsNone())
				{
					auto* OriginalProperty = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Resolved.LayoutOriginalWidgetName);
					auto* PresentationProperty = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Resolved.LayoutPresentationWidgetName);
					auto* Original = Envelope.PlacePure(CreateVariableGet(Graph, WBP, OriginalProperty));
					FFloatProperty* Opacity = FindFProperty<FFloatProperty>(OriginalProperty->PropertyClass, TEXT("SourceOpacity"));
					UEdGraphPin* Value;
					if (Opacity)
					{
						auto* Set = CreateMarkerNode<UK2Node_VariableSet>(Graph,
							[&](UK2Node_VariableSet* Node) { Node->SetFromProperty(Opacity, false, OriginalProperty->PropertyClass); });
						Envelope.PlaceExec(Set); Original->GetValuePin()->MakeLinkTo(Set->FindPinChecked(UEdGraphSchema_K2::PN_Self));
						Value = Set->FindPinChecked(Set->GetVarName(), EGPD_Input);
					}
					else
					{
						auto* Set = CreateCallNode(Graph, Functions.SetRenderOpacity);
						Envelope.PlaceExec(Set); Original->GetValuePin()->MakeLinkTo(Set->FindPinChecked(UEdGraphSchema_K2::PN_Self));
						Value = Set->FindPinChecked(TEXT("InOpacity"));
					}
					Value->DefaultValue = FString::SanitizeFloat(bEnter ? 0.0f : Resolved.LayoutOriginalOpacity);
					AppendSetVisibilityCall(Graph, WBP, Functions, PresentationProperty,
						bEnter ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed, Envelope);
				}
			};
			if (bEnter || !bLocalEndpoints) AppendEnvelope(Chain);
			Chain.PlaceExec(Sequence); LinkExecChain(Chain.ExecChain);
			for (int32 Index = 0; Index < Resolved.LivePresentations.Num(); ++Index)
			{
				if (Index >= 2) Sequence->AddInputPin();
				const auto& Live = Resolved.LivePresentations[Index];
				auto* WidgetProperty = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Live.WidgetName);
				auto* FromProperty = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, bEnter ? Live.OriginalSocketName : Live.PresentationSocketName);
				auto* ToProperty = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, bEnter ? Live.PresentationSocketName : Live.OriginalSocketName);
				FChainLayout Move;
				auto* Widget = Move.PlacePure(CreateVariableGet(Graph, WBP, WidgetProperty));
				auto* From = Move.PlacePure(CreateVariableGet(Graph, WBP, FromProperty));
				auto* To = Move.PlacePure(CreateVariableGet(Graph, WBP, ToProperty));
				auto* Content = Move.PlacePure(CreateCallNode(Graph, Functions.GetContent));
				From->GetValuePin()->MakeLinkTo(Content->FindPinChecked(UEdGraphSchema_K2::PN_Self));
				auto* Equal = Move.PlacePure(CreateCallNode(Graph, Functions.EqualEqual_ObjectObject));
				Content->GetReturnValuePin()->MakeLinkTo(Equal->FindPinChecked(TEXT("A")));
				Widget->GetValuePin()->MakeLinkTo(Equal->FindPinChecked(TEXT("B")));
				auto* Owned = CreateBranchNode(Graph);
				Move.PlaceExec(Owned); Equal->GetReturnValuePin()->MakeLinkTo(Owned->GetConditionPin());
				auto* Original = Move.PlacePure(CreateVariableGet(Graph, WBP,
					FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Live.OriginalSocketName)));
				if (bEnter)
				{
					auto* Geometry = Move.PlacePure(CreateCallNode(Graph, Functions.GetCachedGeometry));
					Original->GetValuePin()->MakeLinkTo(Geometry->FindPinChecked(UEdGraphSchema_K2::PN_Self));
					auto* Size = Move.PlacePure(CreateCallNode(Graph, Functions.GetLocalSize));
					Geometry->GetReturnValuePin()->MakeLinkTo(Size->FindPinChecked(TEXT("Geometry")));
					auto* Split = Move.PlacePure(CreateCallNode(Graph, Functions.BreakVector2D));
					Size->GetReturnValuePin()->MakeLinkTo(Split->FindPinChecked(TEXT("InVec")));
					for (bool bWidth : {true, false})
					{
						auto* Reserve = CreateCallNode(Graph, bWidth ? Functions.SetWidthOverride : Functions.SetHeightOverride);
						Move.PlaceExec(Reserve); Original->GetValuePin()->MakeLinkTo(Reserve->FindPinChecked(UEdGraphSchema_K2::PN_Self));
						Graph->GetSchema()->TryCreateConnection(Split->FindPinChecked(bWidth ? TEXT("X") : TEXT("Y")),
							Reserve->FindPinChecked(bWidth ? TEXT("InWidthOverride") : TEXT("InHeightOverride")));
					}
				}
				if (!bEnter && bLocalEndpoints)
				{
					if (!AppendResetAnimationEvaluation(Graph, WBP, Functions, Resolved, Move))
						SkipBinding(OutWarnings, TEXT("local endpoint source evaluation could not restore before live return"));
					AppendEnvelope(Move);
				}
				auto* Set = CreateCallNode(Graph, Functions.SetContent);
				Move.PlaceExec(Set); To->GetValuePin()->MakeLinkTo(Set->FindPinChecked(UEdGraphSchema_K2::PN_Self));
				Widget->GetValuePin()->MakeLinkTo(Set->FindPinChecked(TEXT("Content")));
				if (!bEnter)
					for (UFunction* Function : {Functions.ClearWidthOverride, Functions.ClearHeightOverride})
					{
						auto* Clear = CreateCallNode(Graph, Function);
						Move.PlaceExec(Clear); Original->GetValuePin()->MakeLinkTo(Clear->FindPinChecked(UEdGraphSchema_K2::PN_Self));
					}
				LinkExecChain(Move.ExecChain);
				Sequence->GetThenPinGivenIndex(Index)->MakeLinkTo(Move.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			}
		}
	}
	for (const auto& Resolved : LiveBindings)
	{
		if (Resolved.LiveReturnEventName.IsNone()) continue;
		auto* Entry = FindOwnedFunctionEntry(WBP, Resolved.LiveReturnEventName);
		UEdGraph* Graph = Entry->GetGraph();
		FChainLayout Finish; Finish.PlaceExec(Entry);
		FIntProperty* Owner = Targets.StateHosts.IsValidIndex(Resolved.StateHostIndex)
			? Targets.StateHosts[Resolved.StateHostIndex].TransitionOwnerProperty : Targets.TransitionOwnerProperty;
		AppendOwnerGate(Graph, WBP, Functions, Owner, Resolved.BindingIndex + 1, Finish);
		AppendResetReversibleAnimation(Graph, WBP, Functions, Resolved, Finish);
		AppendLivePresentationCall(Graph, WBP, Functions, Resolved, false, Finish, false);
		AppendGeneratedIndexSet(Graph, WBP, Owner, 0, Finish);
		AppendStateTimeoutCall(Graph, WBP, Finish);
		LinkExecChain(Finish.ExecChain);
	}
	int32 WiredCount = 0;
	TArray<FResolvedBinding> TimedAnimations = Plan.PageOpenBindings;
	for (const auto& Resolved : MouseBindings)
		if (Resolved.NavigationEventName != NAME_None || Resolved.StateCompletionEventName != NAME_None) TimedAnimations.Add(Resolved);
	if (!TimedAnimations.IsEmpty() || MouseBindings.ContainsByPredicate([](const FResolvedBinding& Resolved)
	{
		return Resolved.WidgetAnimationName != NAME_None || Resolved.HoverMonitorEventName != NAME_None || Resolved.bPressOverlay || Resolved.bTemporaryState || Resolved.bTemporaryNavigate;
	}))
	{
		UK2Node_Event* Construct = nullptr;
		UK2Node_Event* Destruct = nullptr;
		UEdGraph* DispatchGraph = FindOwnedFunctionGraph(WBP, PageOpenEventName);
		UEdGraph* CancelGraph = FindOwnedFunctionGraph(WBP, AnimationCleanupEventName);
        WatchGraph(DispatchGraph); WatchGraph(CancelGraph);
		UK2Node_FunctionEntry* Dispatcher = nullptr;
		UK2Node_FunctionEntry* CleanupEvent = nullptr;
		for (UEdGraphNode* Node : DispatchGraph->Nodes)
			if (auto* Entry = Cast<UK2Node_FunctionEntry>(Node)) Dispatcher = Entry;
		for (UEdGraphNode* Node : CancelGraph->Nodes)
			if (auto* Entry = Cast<UK2Node_FunctionEntry>(Node)) CleanupEvent = Entry;
		for (UEdGraph* EventGraph : WBP->UbergraphPages)
		{
			if (!EventGraph) continue;
			for (UEdGraphNode* Node : EventGraph->Nodes)
			{
				auto* Event = Cast<UK2Node_Event>(Node);
				if (Event && Event->EventReference.GetMemberName() == TEXT("Construct")) Construct = Event;
				if (Event && Event->EventReference.GetMemberName() == TEXT("Destruct")) Destruct = Event;
			}
		}
		const FName VisibleName = BindingFunctionName(TEXT("state_visible"), 0);
		if (auto* Entry = FindOwnedFunctionEntry(WBP, VisibleName))
		{
			UEdGraph* Graph = Entry->GetGraph();
			UK2Node_FunctionResult* RootResult = nullptr;
			for (UEdGraphNode* Node : Graph->Nodes) if (auto* Result = Cast<UK2Node_FunctionResult>(Node)) RootResult = Result;
			RootResult->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue)->DefaultValue = TEXT("true");
			const auto NewResult = [&]()
			{
				// Post-placement inherits the primary result's declared return signature.
				FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*Graph);
				auto* Result = ResultCreator.CreateNode(false);
				ResultCreator.Finalize();
				Result->NodeComment = NodeMarkerComment;
				return Result;
			};
			FChainLayout Chain;
			Chain.PlaceExec(Entry);
			auto* Valid = Chain.PlacePure(CreateCallNode(Graph, UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("IsValid"))));
			Entry->FindPinChecked(TEXT("Widget"))->MakeLinkTo(Valid->FindPinChecked(TEXT("Object")));
			auto* HasWidget = CreateBranchNode(Graph);
			Chain.PlaceExec(HasWidget); Valid->GetReturnValuePin()->MakeLinkTo(HasWidget->GetConditionPin());
			LinkExecChain(Chain.ExecChain);
			HasWidget->GetElsePin()->MakeLinkTo(RootResult->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			auto* Visible = Chain.PlacePure(CreateCallNode(Graph, UWidget::StaticClass()->FindFunctionByName(TEXT("IsVisible"))));
			Entry->FindPinChecked(TEXT("Widget"))->MakeLinkTo(Visible->FindPinChecked(UEdGraphSchema_K2::PN_Self));
			auto* CheckVisible = CreateBranchNode(Graph);
			Visible->GetReturnValuePin()->MakeLinkTo(CheckVisible->GetConditionPin());
			HasWidget->GetThenPin()->MakeLinkTo(CheckVisible->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			auto* Hidden = NewResult(); Hidden->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue)->DefaultValue = TEXT("false");
			CheckVisible->GetElsePin()->MakeLinkTo(Hidden->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			auto* Parent = Chain.PlacePure(CreateCallNode(Graph, UWidget::StaticClass()->FindFunctionByName(TEXT("GetParent"))));
			Entry->FindPinChecked(TEXT("Widget"))->MakeLinkTo(Parent->FindPinChecked(UEdGraphSchema_K2::PN_Self));
			auto* ParentValid = Chain.PlacePure(CreateCallNode(Graph, UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("IsValid"))));
			Parent->GetReturnValuePin()->MakeLinkTo(ParentValid->FindPinChecked(TEXT("Object")));
			auto* Tree = Chain.PlacePure(CreateCallNode(Graph, UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("GetOuterObject"))));
			Entry->FindPinChecked(TEXT("Widget"))->MakeLinkTo(Tree->FindPinChecked(TEXT("Object")));
			auto* Owner = Chain.PlacePure(CreateCallNode(Graph, UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("GetOuterObject"))));
			Tree->GetReturnValuePin()->MakeLinkTo(Owner->FindPinChecked(TEXT("Object")));
			auto* OwnerCast = Chain.PlacePure(CreateMarkerNode<UK2Node_DynamicCast>(Graph,
				[](UK2Node_DynamicCast* Node) { Node->TargetType = UUserWidget::StaticClass(); }));
			OwnerCast->SetPurity(true);
			Graph->GetSchema()->TryCreateConnection(Owner->GetReturnValuePin(), OwnerCast->GetCastSourcePin());
			auto* Pick = Chain.PlacePure(CreateCallNode(Graph, UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("SelectObject"))));
			Parent->GetReturnValuePin()->MakeLinkTo(Pick->FindPinChecked(TEXT("A"))); OwnerCast->GetCastResultPin()->MakeLinkTo(Pick->FindPinChecked(TEXT("B")));
			ParentValid->GetReturnValuePin()->MakeLinkTo(Pick->FindPinChecked(TEXT("bSelectA")));
			auto* WidgetCast = Chain.PlacePure(CreateMarkerNode<UK2Node_DynamicCast>(Graph,
				[](UK2Node_DynamicCast* Node) { Node->TargetType = UWidget::StaticClass(); }));
			WidgetCast->SetPurity(true);
			Graph->GetSchema()->TryCreateConnection(Pick->GetReturnValuePin(), WidgetCast->GetCastSourcePin());
			auto* Recurse = Chain.PlacePure(CreateMarkerNode<UK2Node_CallFunction>(Graph,
				[&](UK2Node_CallFunction* Node) { Node->FunctionReference.SetSelfMember(VisibleName, Graph->GraphGuid); }));
			WidgetCast->GetCastResultPin()->MakeLinkTo(Recurse->FindPinChecked(TEXT("Widget")));
			auto* ParentResult = NewResult(); Recurse->GetReturnValuePin()->MakeLinkTo(ParentResult->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue));
			CheckVisible->GetThenPin()->MakeLinkTo(ParentResult->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		}
		if (auto* StateDispatcher = FindOwnedFunctionEntry(WBP, StateTimeoutDispatchName()))
		{
			UEdGraph* StateGraph = StateDispatcher->GetGraph();
			WatchGraph(StateGraph);
			FChainLayout Dispatch;
			Dispatch.PlaceExec(StateDispatcher);
			auto* Sequence = CreateMarkerNode<UK2Node_ExecutionSequence>(StateGraph, [](UK2Node_ExecutionSequence*) {});
			Dispatch.PlaceExec(Sequence);
			LinkExecChain(Dispatch.ExecChain);
			int32 CaseIndex = 0;
			for (const auto& Timed : Plan.PageOpenBindings)
			{
				if (Timed.StateHostIndex == INDEX_NONE) continue;
				if (CaseIndex >= 2) Sequence->AddInputPin();
				FChainLayout Case;
				AppendSourcePageGate(StateGraph, WBP, Functions, Targets, Timed, Case);
				auto* Active = CreateBranchNode(StateGraph);
				Case.PlaceExec(Active);
				StateTimeoutActivePin(StateGraph, WBP, Functions, Plan, Targets, Timed, Case)->MakeLinkTo(Active->GetConditionPin());
				LinkExecChain(Case.ExecChain);
				Sequence->GetThenPinGivenIndex(CaseIndex++)->MakeLinkTo(Case.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
				FChainLayout Enter;
				AppendOwnerGate(StateGraph, WBP, Functions, Targets.StateHosts[Timed.StateHostIndex].TransitionOwnerProperty, 0, Enter);
				auto* Waiting = Enter.PlacePure(CreateCallNode(StateGraph, UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("K2_IsTimerActive"))));
				Waiting->FindPinChecked(TEXT("FunctionName"))->DefaultValue = Timed.PlaybackEventName.ToString();
				auto* NotWaiting = Enter.PlacePure(CreateCallNode(StateGraph, Functions.Not_PreBool));
				Waiting->GetReturnValuePin()->MakeLinkTo(NotWaiting->FindPinChecked(TEXT("A")));
				auto* Start = CreateBranchNode(StateGraph);
				Enter.PlaceExec(Start); NotWaiting->GetReturnValuePin()->MakeLinkTo(Start->GetConditionPin());
				AppendAnimationTimer(StateGraph, Functions, Timed.PlaybackEventName,
					NavigationData.ClickBindings[Timed.BindingIndex].TriggerDelaySeconds, Enter);
				LinkExecChain(Enter.ExecChain);
				Active->GetThenPin()->MakeLinkTo(Enter.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
				FChainLayout Exit;
				AppendOwnedFunctionCall(StateGraph, WBP, Exit, BindingFunctionName(TEXT("cancel_timed"), Timed.BindingIndex));
				Active->GetElsePin()->MakeLinkTo(Exit.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			}
		}
		if (Dispatcher)
		{
			FChainLayout Dispatch;
			Dispatch.RowY = -NodeRowStep * 4;
			Dispatch.PlaceExec(Dispatcher);
			AppendAnimationCleanupCall(DispatchGraph, WBP, Dispatch);
			AppendPageOpenDispatch(DispatchGraph, WBP, NavigationData, Functions, Plan, Targets, Dispatch);
			LinkExecChain(Dispatch.ExecChain);
			++WiredCount;
		}
		if (!Construct)
		{
			Construct = CreateMarkerNode<UK2Node_Event>(Ubergraph, [](UK2Node_Event* Node)
			{
				Node->EventReference.SetExternalMember(TEXT("Construct"), UUserWidget::StaticClass());
				Node->bOverrideFunction = true;
			});
			Construct->SetEnabledState(ENodeEnabledState::Enabled, false);
		}
		FChainLayout Auto;
		Auto.RowY = -NodeRowStep * 2;
		if (!AppendNativeRefreshBindings(Construct->GetGraph(), WBP, false, Auto)
			|| !AppendNativeRefreshBindings(Construct->GetGraph(), WBP, true, Auto))
			SkipBinding(OutWarnings, TEXT("Animation refresh binding could not be generated"));
		AppendPageOpenCall(Construct->GetGraph(), WBP, Auto);
		if (Auto.ExecChain.IsEmpty() || !LinkExecChain(Auto.ExecChain)
			|| !AttachLifecycleEvent(Construct->GetGraph(), Construct, Auto.ExecChain[0]))
			SkipBinding(OutWarnings, TEXT("Construct event cannot be extended without changing existing user wiring"));
		if (!Destruct)
		{
			Destruct = CreateMarkerNode<UK2Node_Event>(Ubergraph, [](UK2Node_Event* Node)
			{
				Node->EventReference.SetExternalMember(TEXT("Destruct"), UUserWidget::StaticClass());
				Node->bOverrideFunction = true;
			});
			Destruct->SetEnabledState(ENodeEnabledState::Enabled, false);
		}
		FChainLayout Cleanup;
		Cleanup.RowY = -NodeRowStep;
		AppendAnimationCleanupCall(Destruct->GetGraph(), WBP, Cleanup);
		if (!AppendNativeRefreshBindings(Destruct->GetGraph(), WBP, false, Cleanup))
			SkipBinding(OutWarnings, TEXT("Animation refresh unbinding could not be generated"));
		if (Cleanup.ExecChain.IsEmpty() || !LinkExecChain(Cleanup.ExecChain)
			|| !AttachLifecycleEvent(Destruct->GetGraph(), Destruct, Cleanup.ExecChain[0]))
			SkipBinding(OutWarnings, TEXT("Destruct event cannot be extended without changing existing user wiring"));
		FChainLayout Cancel;
		Cancel.RowY = -NodeRowStep * 3;
		Cancel.PlaceExec(CleanupEvent);
		for (const auto& Resolved : LiveBindings)
		{
			if (!Resolved.LiveReturnEventName.IsNone()) AppendTimerCall(CancelGraph, Functions.ClearTimer, Resolved.LiveReturnEventName, Cancel);
		}

        for (const auto& Resolved : TimedAnimations)
        {
            const FName Name = BindingFunctionName(TEXT("cancel_timed"), Resolved.BindingIndex);
            AppendOwnedFunctionCall(CancelGraph, WBP, Cancel, Name);
            auto* Entry = FindOwnedFunctionEntry(WBP, Name);
            UEdGraph* TimedCancelGraph = Entry->GetGraph();
            FChainLayout TimedCancel;
            TimedCancel.PlaceExec(Entry);
			if (Resolved.PlaybackEventName != NAME_None) AppendTimerCall(TimedCancelGraph, Functions.ClearTimer, Resolved.PlaybackEventName, TimedCancel);
			if (Resolved.NavigationEventName != NAME_None) AppendTimerCall(TimedCancelGraph, Functions.ClearTimer, Resolved.NavigationEventName, TimedCancel);
			if (Resolved.StateCompletionEventName != NAME_None) AppendTimerCall(TimedCancelGraph, Functions.ClearTimer, Resolved.StateCompletionEventName, TimedCancel);
			if (!AppendLayerOrders(TimedCancelGraph, WBP, Functions, NavigationData.ClickBindings[Resolved.BindingIndex], Resolved, false, TimedCancel))
				SkipBinding(OutWarnings, TEXT("source layer order could not reset on cancellation"));
			AppendStopAnimationCall(TimedCancelGraph, WBP, Functions, Resolved.WidgetAnimationName, TimedCancel);
			if (Resolved.PlaybackEventName != NAME_None && Targets.StateHosts.IsValidIndex(Resolved.StateHostIndex))
			{
				const auto& Host = Targets.StateHosts[Resolved.StateHostIndex];
				AppendOwnerGate(TimedCancelGraph, WBP, Functions, Host.TransitionOwnerProperty, Resolved.BindingIndex + 1, TimedCancel);
				AppendLivePresentationCall(TimedCancelGraph, WBP, Functions, Resolved, false, TimedCancel, false);
				AppendGeneratedIndexSet(TimedCancelGraph, WBP, Host.TransitionOwnerProperty, 0, TimedCancel);
			}
            LinkExecChain(TimedCancel.ExecChain);
			if (Resolved.NavigationEventName != NAME_None)
			{
				auto* Event = FindOwnedFunctionEntry(WBP, Resolved.NavigationEventName);
                UEdGraph* CallbackGraph = Event->GetGraph();
				FChainLayout Finish;
				Finish.RowY = (WiredCount++ + 1) * NodeRowStep;
				Finish.PlaceExec(Event);
				const auto& FinishBinding = NavigationData.ClickBindings[Resolved.BindingIndex];
				if (Resolved.bTemporaryNavigate)
				{
					AppendOwnerGate(CallbackGraph, WBP, Functions, Targets.TransitionOwnerProperty, Resolved.BindingIndex + 1, Finish);
					AppendLivePresentationCall(CallbackGraph, WBP, Functions, Resolved, false, Finish, false);
					if (!AppendTemporaryPageVisibility(CallbackGraph, WBP, Functions, Targets, Resolved, false, Finish))
						SkipBinding(OutWarnings, TEXT("temporary target page could not be hidden"));
					AppendGeneratedIndexSet(CallbackGraph, WBP, Targets.TransitionOwnerProperty, 0, Finish);
					AppendStateTimeoutCall(CallbackGraph, WBP, Finish);
					if (!LinkExecChain(Finish.ExecChain)) SkipBinding(OutWarnings, TEXT("temporary page completion could not be wired"));
					continue;
				}
				if (FinishBinding.Transition.Type != TEXT("SMART_ANIMATE"))
				{
					AppendOwnerGate(CallbackGraph, WBP, Functions, Targets.TransitionOwnerProperty, Resolved.BindingIndex + 1, Finish);
					AppendStopAnimationCall(CallbackGraph, WBP, Functions, Resolved.WidgetAnimationName, Finish);
					if (!AppendTemporaryPageVisibility(CallbackGraph, WBP, Functions, Targets, Resolved, false, Finish))
						SkipBinding(OutWarnings, TEXT("transition target page could not restore its slot"));
					AppendGeneratedIndexSet(CallbackGraph, WBP, Targets.TransitionOwnerProperty, 0, Finish);
				}
				AppendNavigateTail(CallbackGraph, WBP, Functions, Targets, Resolved.TargetPageIndex, Finish);
				LinkExecChain(Finish.ExecChain);
			}
			if (Resolved.StateCompletionEventName != NAME_None && Targets.StateHosts.IsValidIndex(Resolved.StateHostIndex))
			{
				const auto& StateHost = Targets.StateHosts[Resolved.StateHostIndex];
				auto* Event = FindOwnedFunctionEntry(WBP, Resolved.StateCompletionEventName);
                UEdGraph* CallbackGraph = Event->GetGraph();
				FChainLayout Finish;
				Finish.RowY = (WiredCount++ + 1) * NodeRowStep;
				Finish.PlaceExec(Event);
				AppendOwnerGate(CallbackGraph, WBP, Functions, StateHost.TransitionOwnerProperty, Resolved.BindingIndex + 1, Finish);
				AppendOwnerGate(CallbackGraph, WBP, Functions, StateHost.CurrentStateProperty, Resolved.SourceStateIndex, Finish);
				const auto& StateBinding = NavigationData.ClickBindings[Resolved.BindingIndex];
				if (Resolved.bTemporaryState && StateBinding.Transition.Type == TEXT("DISSOLVE"))
				{
					UWidget* Target = StateHost.StateWidgets.IsValidIndex(Resolved.TargetStateIndex) ? StateHost.StateWidgets[Resolved.TargetStateIndex] : nullptr;
					auto* Property = Target ? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Target->GetFName()) : nullptr;
					if (!AppendSetVisibilityCall(CallbackGraph, WBP, Functions, Property, ESlateVisibility::Collapsed, Finish))
						SkipBinding(OutWarnings, TEXT("temporary dissolve target could not be hidden"));
					AppendGeneratedIndexSet(CallbackGraph, WBP, StateHost.TransitionOwnerProperty, 0, Finish);
					AppendStateTimeoutCall(CallbackGraph, WBP, Finish);
					if (!LinkExecChain(Finish.ExecChain)) SkipBinding(OutWarnings, TEXT("temporary dissolve completion could not be wired"));
					continue;
				}
				if (!AppendLayerOrders(CallbackGraph, WBP, Functions, NavigationData.ClickBindings[Resolved.BindingIndex], Resolved, false, Finish))
					SkipBinding(OutWarnings, TEXT("source layer order could not reset after transition"));
				AppendStopAnimationCall(CallbackGraph, WBP, Functions, Resolved.WidgetAnimationName, Finish);
				AppendGeneratedIndexSet(CallbackGraph, WBP, StateHost.TransitionOwnerProperty, 0, Finish);
				AppendLivePresentationCall(CallbackGraph, WBP, Functions, Resolved, false, Finish, false);
				if (!AppendStateSelection(CallbackGraph, WBP, Functions, StateHost, Resolved.TargetStateIndex, Finish)
					|| !LinkExecChain(Finish.ExecChain)) SkipBinding(OutWarnings, TEXT("state completion could not be wired"));
			}
			if (Resolved.PlaybackEventName != NAME_None)
			{
				auto* Event = FindOwnedFunctionEntry(WBP, Resolved.PlaybackEventName);
                UEdGraph* CallbackGraph = Event->GetGraph();
				FChainLayout Play;
				Play.RowY = (WiredCount++ + 1) * NodeRowStep;
				Play.PlaceExec(Event);
				AppendSourcePageGate(CallbackGraph, WBP, Functions, Targets, Resolved, Play);
				const auto& TimedBinding = NavigationData.ClickBindings[Resolved.BindingIndex];
				if (Resolved.StateHostIndex != INDEX_NONE)
				{
					auto* Active = CreateBranchNode(CallbackGraph);
					Play.PlaceExec(Active);
					StateTimeoutActivePin(CallbackGraph, WBP, Functions, Plan, Targets, Resolved, Play)->MakeLinkTo(Active->GetConditionPin());
				}
				if (TimedBinding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Overlay)
				{
					if (!AppendOverlayOpen(CallbackGraph, WBP, Functions, Targets, Resolved, false, Play))
					{
						SkipBinding(OutWarnings, TEXT("AFTER_TIMEOUT overlay target is unavailable"));
						continue;
					}
					if (Resolved.WidgetAnimationName != NAME_None)
					{
						AppendStopAnimationCall(CallbackGraph, WBP, Functions, Resolved.WidgetAnimationName, Play);
						AppendPlayAnimationCall(CallbackGraph, WBP, Functions, Resolved.WidgetAnimationName, Play, false, false, true);
					}
				}
				else if (TimedBinding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::CloseOverlay)
				{
					if (!AppendCloseOverlayTail(CallbackGraph, WBP, Functions, Targets, Play))
					{
						SkipBinding(OutWarnings, TEXT("AFTER_TIMEOUT overlay close could not be wired"));
						continue;
					}
				}
				else if (Resolved.StateHostIndex != INDEX_NONE)
				{
					if (!AppendStateTransitionStart(CallbackGraph, WBP, Functions, Targets, TimedBinding, Resolved, Play))
					{
						SkipBinding(OutWarnings, TEXT("timed state transition could not be wired"));
						continue;
					}
				}
				else if (Resolved.NavigationEventName != NAME_None)
				{
					if (!AppendPageTransitionStart(CallbackGraph, WBP, Functions, Targets, TimedBinding, Resolved, Play))
					{
						SkipBinding(OutWarnings, TEXT("timed page transition could not be wired"));
						continue;
					}
				}
				else
				{
					if (!AppendLayerOrders(CallbackGraph, WBP, Functions, TimedBinding, Resolved, true, Play))
					{
						SkipBinding(OutWarnings, TEXT("timed animation layer order could not be wired"));
						continue;
					}
					AppendPlayAnimationCall(CallbackGraph, WBP, Functions, Resolved.WidgetAnimationName, Play);
				}
				LinkExecChain(Play.ExecChain);
			}
		}
        for (const auto& Pair : DragGroups)
        {
            const FName Name = BindingFunctionName(TEXT("cancel_drag"), Pair.Key);
            AppendOwnedFunctionCall(CancelGraph, WBP, Cancel, Name);
            auto* Entry = FindOwnedFunctionEntry(WBP, Name);
            FChainLayout DragTimerCleanup;
            DragTimerCleanup.PlaceExec(Entry);
            if (!AppendDragCleanup(Entry->GetGraph(), WBP, Pair.Value, Functions, DragTimerCleanup) || !LinkExecChain(DragTimerCleanup.ExecChain))
                SkipBinding(OutWarnings, TEXT("drag timer cleanup could not be wired"));
        }
		for (const auto& Resolved : MouseBindings)
		{
            if (Resolved.bDrag || !HasMouseCancellation(NavigationData.ClickBindings[Resolved.BindingIndex], Resolved)) continue;
            const FName Name = BindingFunctionName(TEXT("cancel_mouse"), Resolved.BindingIndex);
            AppendOwnedFunctionCall(CancelGraph, WBP, Cancel, Name);
            auto* Entry = FindOwnedFunctionEntry(WBP, Name);
            UEdGraph* MouseCancelGraph = Entry->GetGraph();
            FChainLayout MouseCancel;
            MouseCancel.PlaceExec(Entry);
			if (Resolved.HoverMonitorEventName != NAME_None) AppendTimerCall(MouseCancelGraph, Functions.ClearTimer, Resolved.HoverMonitorEventName, MouseCancel);
			if (Resolved.MirrorHoverMonitorEventName != NAME_None) AppendTimerCall(MouseCancelGraph, Functions.ClearTimer, Resolved.MirrorHoverMonitorEventName, MouseCancel);
			if (!AppendLayerOrders(MouseCancelGraph, WBP, Functions, NavigationData.ClickBindings[Resolved.BindingIndex], Resolved, false, MouseCancel))
				SkipBinding(OutWarnings, TEXT("source layer order could not reset on cancellation"));
			if (Resolved.bTemporaryNavigate && !AppendTemporaryPageVisibility(MouseCancelGraph, WBP, Functions, Targets, Resolved, false, MouseCancel))
				SkipBinding(OutWarnings, TEXT("temporary target page could not reset on cancellation"));
			if (Resolved.WidgetAnimationName != NAME_None && Resolved.PlaybackTargetPropertyName.IsNone()
				&& (Resolved.NavigationEventName == NAME_None || Resolved.bTemporaryNavigate))
				AppendStopAnimationCall(MouseCancelGraph, WBP, Functions, Resolved.WidgetAnimationName, MouseCancel);
            LinkExecChain(MouseCancel.ExecChain);
		}
		if (Targets.OverlayOwnerProperty)
		{
			AppendGeneratedIndexSet(CancelGraph, WBP, Targets.OverlayOwnerProperty, 0, Cancel);
			AppendOverlayExitCollapse(CancelGraph, WBP, Functions, Targets, Cancel);
		}
		TArray<FResolvedBinding> TemporaryInstantStates;
		TArray<FResolvedBinding> ReversibleAnimations;
		TArray<FResolvedBinding> TimedPageTransitions;
		for (const auto& Resolved : TimedAnimations)
			if (!Resolved.bTemporaryNavigate && Resolved.NavigationEventName != NAME_None
				&& NavigationData.ClickBindings[Resolved.BindingIndex].Transition.Type != TEXT("SMART_ANIMATE"))
				TimedPageTransitions.Add(Resolved);
		for (const auto& Resolved : MouseBindings)
		{
			if (Resolved.bTemporaryState && NavigationData.ClickBindings[Resolved.BindingIndex].Transition.bStateInstant)
				TemporaryInstantStates.Add(Resolved);
			if (!Resolved.bDrag && !Resolved.PlaybackTargetPropertyName.IsNone()) ReversibleAnimations.Add(Resolved);
		}
		const int32 CleanupBranches = DragGroups.Num() + ReversibleAnimations.Num() + TimedPageTransitions.Num() + TemporaryInstantStates.Num()
			+ Targets.StateHosts.Num() + (Targets.TransitionOwnerProperty ? 1 : 0)
			+ (LiveBindings.ContainsByPredicate([](const FResolvedBinding& Binding) { return !Binding.LivePresentations.IsEmpty(); }) ? 1 : 0);
		if (CleanupBranches > 0)
		{
			auto* Sequence = CreateMarkerNode<UK2Node_ExecutionSequence>(CancelGraph, [](UK2Node_ExecutionSequence*) {});
			Cancel.PlaceExec(Sequence);
			for (int32 Index = 2; Index < CleanupBranches; ++Index) Sequence->AddInputPin();
			int32 DragBranch = 0;
			for (const auto& Pair : DragGroups)
			{
				auto* SelectedProperty = FindFProperty<FIntProperty>(WBP->SkeletonGeneratedClass, GetDragSelectedName(Pair.Key));
				if (!SelectedProperty) { ++DragBranch; continue; }
				const FName FunctionName = BindingFunctionName(TEXT("restore_drag"), Pair.Key);
                FChainLayout Invoke;
                AppendOwnedFunctionCall(CancelGraph, WBP, Invoke, FunctionName);
                auto* Entry = FindOwnedFunctionEntry(WBP, FunctionName);
                UEdGraph* RestoreGraph = Entry->GetGraph();
                FChainLayout DragCleanup;
                DragCleanup.PlaceExec(Entry);
				DragCleanup.RowY = Cancel.RowY + (DragBranch + 1) * NodeRowStep;
				DragCleanup.NextColumn = Cancel.NextColumn;
				AppendOwnerGate(RestoreGraph, WBP, Functions, Targets.TransitionOwnerProperty, Pair.Key + 1, DragCleanup);
				auto* Selected = DragCleanup.PlacePure(CreateVariableGet(RestoreGraph, WBP, SelectedProperty));
				auto* Switch = CreateMarkerNode<UK2Node_SwitchInteger>(RestoreGraph, [](UK2Node_SwitchInteger* Node) { Node->StartIndex = 0; });
				DragCleanup.PlaceExec(Switch);
				Selected->GetValuePin()->MakeLinkTo(Switch->GetSelectionPin());
				for (int32 CandidateIndex = 0; CandidateIndex < Pair.Value.Num(); ++CandidateIndex)
				{
					Switch->AddPinToSwitchNode();
					const auto& Candidate = Pair.Value[CandidateIndex];
					FChainLayout Restore;
					Restore.RowY = DragCleanup.RowY + (CandidateIndex + 1) * NodeRowStep;
					Restore.NextColumn = DragCleanup.NextColumn;
					bool bValid = true;
					if (Candidate.Binding->Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate
						&& Candidate.Binding->Transition.Type != TEXT("SMART_ANIMATE"))
						bValid = AppendTemporaryPageVisibility(RestoreGraph, WBP, Functions, Targets, *Candidate.Resolved, false, Restore);
					if (!bValid || !AppendResetReversibleAnimation(RestoreGraph, WBP, Functions, *Candidate.Resolved, Restore)
						|| !AppendLayerOrders(RestoreGraph, WBP, Functions, *Candidate.Binding, *Candidate.Resolved, false, Restore)
						|| !LinkExecChain(Restore.ExecChain))
					{
						SkipBinding(OutWarnings, TEXT("selected drag animation could not reset on cancellation"));
						continue;
					}
					Switch->FindPinChecked(Switch->GetPinNameGivenIndex(CandidateIndex), EGPD_Output)->MakeLinkTo(
						Restore.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
				}
				if (LinkExecChain(DragCleanup.ExecChain)) Sequence->GetThenPinGivenIndex(DragBranch)->MakeLinkTo(
					Invoke.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
				++DragBranch;
			}
			for (int32 Index = 0; Index < ReversibleAnimations.Num(); ++Index)
			{
				const auto& Resolved = ReversibleAnimations[Index];
				auto* Target = FindFProperty<FFloatProperty>(WBP->SkeletonGeneratedClass, Resolved.PlaybackTargetPropertyName);
				auto* Animation = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Resolved.WidgetAnimationName);
				UFunction* IsPlaying = UUserWidget::StaticClass()->FindFunctionByName(TEXT("IsAnimationPlaying"));
				UFunction* NotEqual = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("NotEqual_DoubleDouble"));
				if (!Target || !Animation || !IsPlaying || !NotEqual || !Functions.BooleanOR) continue;
				const FName FunctionName = BindingFunctionName(TEXT("restore_animation"), Resolved.BindingIndex);
                FChainLayout Invoke;
                AppendOwnedFunctionCall(CancelGraph, WBP, Invoke, FunctionName);
                auto* Entry = FindOwnedFunctionEntry(WBP, FunctionName);
                UEdGraph* RestoreGraph = Entry->GetGraph();
                FChainLayout Restore;
                Restore.PlaceExec(Entry);
				Restore.RowY = Cancel.RowY + (DragGroups.Num() + Index + 1) * NodeRowStep;
				Restore.NextColumn = Cancel.NextColumn;
				auto* TargetGet = Restore.PlacePure(CreateVariableGet(RestoreGraph, WBP, Target));
				auto* AnimationGet = Restore.PlacePure(CreateVariableGet(RestoreGraph, WBP, Animation));
				auto* Playing = Restore.PlacePure(CreateCallNode(RestoreGraph, IsPlaying));
				auto* Changed = Restore.PlacePure(CreateCallNode(RestoreGraph, NotEqual));
				auto* Needed = Restore.PlacePure(CreateCallNode(RestoreGraph, Functions.BooleanOR));
				AnimationGet->GetValuePin()->MakeLinkTo(Playing->FindPinChecked(TEXT("InAnimation")));
				if (!RestoreGraph->GetSchema()->TryCreateConnection(TargetGet->GetValuePin(), Changed->FindPinChecked(TEXT("A")))) continue;
				Changed->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("0.0");
				Changed->GetReturnValuePin()->MakeLinkTo(Needed->FindPinChecked(TEXT("A")));
				Playing->GetReturnValuePin()->MakeLinkTo(Needed->FindPinChecked(TEXT("B")));
				auto* Gate = CreateBranchNode(RestoreGraph);
				Restore.PlaceExec(Gate);
				Needed->GetReturnValuePin()->MakeLinkTo(Gate->GetConditionPin());
				if (!AppendResetReversibleAnimation(RestoreGraph, WBP, Functions, Resolved, Restore)
					|| !LinkExecChain(Restore.ExecChain)) continue;
				Sequence->GetThenPinGivenIndex(DragGroups.Num() + Index)->MakeLinkTo(
					Invoke.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			}
			for (int32 Index = 0; Index < TimedPageTransitions.Num(); ++Index)
			{
				const auto& Resolved = TimedPageTransitions[Index];
				const FName FunctionName = BindingFunctionName(TEXT("restore_page"), Resolved.BindingIndex);
                FChainLayout Invoke;
                AppendOwnedFunctionCall(CancelGraph, WBP, Invoke, FunctionName);
                auto* Entry = FindOwnedFunctionEntry(WBP, FunctionName);
                UEdGraph* RestoreGraph = Entry->GetGraph();
                FChainLayout Restore;
                Restore.PlaceExec(Entry);
				Restore.RowY = Cancel.RowY + (DragGroups.Num() + ReversibleAnimations.Num() + Index + 1) * NodeRowStep;
				Restore.NextColumn = Cancel.NextColumn;
				AppendOwnerGate(RestoreGraph, WBP, Functions, Targets.TransitionOwnerProperty, Resolved.BindingIndex + 1, Restore);
				if (!AppendTemporaryPageVisibility(RestoreGraph, WBP, Functions, Targets, Resolved, false, Restore)
					|| !LinkExecChain(Restore.ExecChain))
				{
					SkipBinding(OutWarnings, TEXT("interrupted transition target page could not restore its slot"));
					continue;
				}
				Sequence->GetThenPinGivenIndex(DragGroups.Num() + ReversibleAnimations.Num() + Index)->MakeLinkTo(
					Invoke.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			}
			for (int32 Index = 0; Index < TemporaryInstantStates.Num(); ++Index)
			{
				const auto& Resolved = TemporaryInstantStates[Index];
				if (!Targets.StateHosts.IsValidIndex(Resolved.StateHostIndex)) continue;
				const auto& Host = Targets.StateHosts[Resolved.StateHostIndex];
				const FName FunctionName = BindingFunctionName(TEXT("restore_instant"), Resolved.BindingIndex);
                FChainLayout Invoke;
                AppendOwnedFunctionCall(CancelGraph, WBP, Invoke, FunctionName);
                auto* Entry = FindOwnedFunctionEntry(WBP, FunctionName);
                UEdGraph* RestoreGraph = Entry->GetGraph();
                FChainLayout RestoreTemporary;
                RestoreTemporary.PlaceExec(Entry);
				RestoreTemporary.RowY = Cancel.RowY + (Index + 1) * NodeRowStep;
				RestoreTemporary.NextColumn = Cancel.NextColumn;
				AppendOwnerGate(RestoreGraph, WBP, Functions, Host.TransitionOwnerProperty, Resolved.BindingIndex + 1, RestoreTemporary);
				if (!AppendTemporaryStateVisibility(RestoreGraph, WBP, Functions, Host, Resolved, false, RestoreTemporary, false)
					|| !LinkExecChain(RestoreTemporary.ExecChain)) continue;
				Sequence->GetThenPinGivenIndex(DragGroups.Num() + ReversibleAnimations.Num() + TimedPageTransitions.Num() + Index)->MakeLinkTo(Invoke.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			}
			for (int32 HostIndex = 0; HostIndex < Targets.StateHosts.Num(); ++HostIndex)
			{
				const auto& Host = Targets.StateHosts[HostIndex];
				if (!Host.CurrentStateProperty || !Host.TransitionOwnerProperty) continue;
				const FName FunctionName = BindingFunctionName(TEXT("restore_host"), HostIndex);
                FChainLayout Invoke;
                AppendOwnedFunctionCall(CancelGraph, WBP, Invoke, FunctionName);
                auto* Entry = FindOwnedFunctionEntry(WBP, FunctionName);
                UEdGraph* RestoreGraph = Entry->GetGraph();
                FChainLayout HostCleanup;
                HostCleanup.PlaceExec(Entry);
				HostCleanup.RowY = Cancel.RowY + (HostIndex + 1) * NodeRowStep;
				HostCleanup.NextColumn = Cancel.NextColumn;
				AppendGeneratedIndexSet(RestoreGraph, WBP, Host.TransitionOwnerProperty, 0, HostCleanup);
				auto* Current = HostCleanup.PlacePure(CreateVariableGet(RestoreGraph, WBP, Host.CurrentStateProperty));
				auto* Restore = CreateMarkerNode<UK2Node_SwitchInteger>(RestoreGraph, [](UK2Node_SwitchInteger* Node) { Node->StartIndex = 0; });
				HostCleanup.PlaceExec(Restore);
				Current->GetValuePin()->MakeLinkTo(Restore->GetSelectionPin());
				for (int32 StateIndex = 0; StateIndex < Host.StateWidgets.Num(); ++StateIndex)
				{
					Restore->AddPinToSwitchNode();
					FChainLayout State;
					State.RowY = HostCleanup.RowY + (StateIndex + 1) * NodeRowStep;
					State.NextColumn = HostCleanup.NextColumn;
					if (!AppendStateSelection(RestoreGraph, WBP, Functions, Host, StateIndex, State, false) || !LinkExecChain(State.ExecChain))
					{
						SkipBinding(OutWarnings, TEXT("state cleanup could not restore the current state"));
						continue;
					}
					if (UEdGraphPin* Case = Restore->FindPin(Restore->GetPinNameGivenIndex(StateIndex), EGPD_Output))
						Case->MakeLinkTo(State.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
				}
				if (LinkExecChain(HostCleanup.ExecChain)) Sequence->GetThenPinGivenIndex(DragGroups.Num() + ReversibleAnimations.Num() + TimedPageTransitions.Num() + TemporaryInstantStates.Num() + HostIndex)->MakeLinkTo(
					Invoke.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			}
			if (Targets.TransitionOwnerProperty)
			{
				const FName FunctionName = BindingFunctionName(TEXT("restore_current_page"), 0);
                FChainLayout Invoke;
                AppendOwnedFunctionCall(CancelGraph, WBP, Invoke, FunctionName);
                auto* Entry = FindOwnedFunctionEntry(WBP, FunctionName);
                UEdGraph* RestoreGraph = Entry->GetGraph();
                FChainLayout PageCleanup;
                PageCleanup.PlaceExec(Entry);
				PageCleanup.RowY = Cancel.RowY + (Targets.StateHosts.Num() + 1) * NodeRowStep;
				PageCleanup.NextColumn = Cancel.NextColumn;
				AppendGeneratedIndexSet(RestoreGraph, WBP, Targets.TransitionOwnerProperty, 0, PageCleanup);
				auto* Current = PageCleanup.PlacePure(CreateVariableGet(RestoreGraph, WBP, Targets.CurrentPageProperty));
				auto* Restore = CreateMarkerNode<UK2Node_SwitchInteger>(RestoreGraph, [](UK2Node_SwitchInteger* Node) { Node->StartIndex = 0; });
				PageCleanup.PlaceExec(Restore);
				Current->GetValuePin()->MakeLinkTo(Restore->GetSelectionPin());
				for (int32 PageIndex = 0; PageIndex < Targets.PageHostPages.Num(); ++PageIndex)
				{
					Restore->AddPinToSwitchNode();
					FChainLayout Page;
					Page.RowY = PageCleanup.RowY + (PageIndex + 1) * NodeRowStep;
					Page.NextColumn = PageCleanup.NextColumn;
					if (!AppendPageSelection(RestoreGraph, WBP, Functions, Targets, PageIndex, Page) || !LinkExecChain(Page.ExecChain))
					{
						SkipBinding(OutWarnings, TEXT("page transition cleanup could not restore the current page"));
						continue;
					}
					if (UEdGraphPin* Case = Restore->FindPin(Restore->GetPinNameGivenIndex(PageIndex), EGPD_Output))
						Case->MakeLinkTo(Page.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
				}
				if (LinkExecChain(PageCleanup.ExecChain)) Sequence->GetThenPinGivenIndex(DragGroups.Num() + ReversibleAnimations.Num() + TimedPageTransitions.Num() + TemporaryInstantStates.Num() + Targets.StateHosts.Num())->MakeLinkTo(
					Invoke.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			}
			if (LiveBindings.ContainsByPredicate([](const FResolvedBinding& Binding) { return !Binding.LivePresentations.IsEmpty(); }))
			{
				FChainLayout ReturnLive;
				for (const auto& Resolved : LiveBindings)
					AppendLivePresentationCall(CancelGraph, WBP, Functions, Resolved, false, ReturnLive, false);
				LinkExecChain(ReturnLive.ExecChain);
				Sequence->GetThenPinGivenIndex(CleanupBranches - 1)->MakeLinkTo(ReturnLive.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
			}
		}
		LinkExecChain(Cancel.ExecChain);
		for (const auto& Resolved : MouseBindings)
		{
			if (Resolved.HoverMonitorEventName != NAME_None
				&& !AuthorHoverOverlayMonitor(Ubergraph, WBP, Functions, Targets, Resolved,
					Resolved.TriggerWidgetName, Resolved.HoverMonitorEventName, (WiredCount++ + 1) * NodeRowStep))
			{
				SkipBinding(OutWarnings, TEXT("hover overlay monitor could not be wired"));
			}
			if (Resolved.MirrorTriggerWidgetName != NAME_None && Resolved.MirrorHoverMonitorEventName != NAME_None
				&& !AuthorHoverOverlayMonitor(Ubergraph, WBP, Functions, Targets, Resolved,
					Resolved.MirrorTriggerWidgetName, Resolved.MirrorHoverMonitorEventName, (WiredCount++ + 1) * NodeRowStep))
			{
				SkipBinding(OutWarnings, TEXT("mirror hover overlay monitor could not be wired"));
			}
		}
	}

	// Overlay-context Navigate closes the overlay and switches pages without animating
	// The drag driver owns input and chooses one candidate; these callbacks own state/page commits.
	for (const auto& Pair : DragGroups)
	{
		for (const FDragCandidate& Candidate : Pair.Value)
		{
			const auto& Resolved = *Candidate.Resolved;
			const auto& Binding = *Candidate.Binding;
			for (const bool bCommit : {false, true})
			{
				const FName Name = bCommit ? Candidate.CommitEventName : Candidate.CancelEventName;
				auto* Event = CreateMarkerNode<UK2Node_CustomEvent>(Ubergraph,
					[Name](UK2Node_CustomEvent* Node) { Node->CustomFunctionName = Name; });
				FChainLayout Finish;
				Finish.RowY = (WiredCount++ + 1) * NodeRowStep;
				Finish.PlaceExec(Event);
				AppendOwnerGate(Ubergraph, WBP, Functions, Targets.TransitionOwnerProperty, Pair.Key + 1, Finish);
				bool bValid = true;
				if (Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate && Binding.Transition.Type != TEXT("SMART_ANIMATE"))
					bValid = AppendTemporaryPageVisibility(Ubergraph, WBP, Functions, Targets, Resolved, false, Finish);
				bValid = bValid && (bCommit
					? AppendResetReversibleAnimation(Ubergraph, WBP, Functions, Resolved, Finish)
					: AppendStopAnimationCall(Ubergraph, WBP, Functions, Resolved.WidgetAnimationName, Finish))
					&& AppendLayerOrders(Ubergraph, WBP, Functions, Binding, Resolved, false, Finish);
				AppendLivePresentationCall(Ubergraph, WBP, Functions, Resolved, false, Finish, !bCommit);
				AppendGeneratedIndexSet(Ubergraph, WBP, Targets.TransitionOwnerProperty, 0, Finish);
				if (bCommit && Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate)
					bValid = bValid && AppendNavigateTail(Ubergraph, WBP, Functions, Targets, Resolved.TargetPageIndex, Finish);
				else if (bCommit && Targets.StateHosts.IsValidIndex(Resolved.StateHostIndex))
				{
					const auto& Host = Targets.StateHosts[Resolved.StateHostIndex];
					AppendOwnerGate(Ubergraph, WBP, Functions, Host.CurrentStateProperty, Resolved.SourceStateIndex, Finish);
					bValid = bValid && AppendStateSelection(Ubergraph, WBP, Functions, Host, Resolved.TargetStateIndex, Finish);
				}
				else if (bCommit) bValid = false;
				if (!bValid || !LinkExecChain(Finish.ExecChain))
					SkipBinding(OutWarnings, TEXT("drag completion could not restore or commit its source state"));
			}
		}
	}
	for (const auto& Pair : DragGroups)
	{
		bool bSameSource = !Pair.Value.IsEmpty();
		if (bSameSource)
		{
			const auto& First = *Pair.Value[0].Resolved;
			for (const auto& Candidate : Pair.Value)
				bSameSource &= Candidate.Resolved->SourcePageIndex == First.SourcePageIndex
					&& Candidate.Resolved->StateHostIndex == First.StateHostIndex
					&& Candidate.Resolved->SourceStateIndex == First.SourceStateIndex;
		}
		if (!bSameSource || !WireDragBinding(Ubergraph, WBP, Pair.Value, Targets, Functions, OutWarnings))
			SkipBinding(OutWarnings, TEXT("drag candidates could not share one source input"));
		else ++WiredCount;
	}

	// the underlying page host. Back, Overlay and OpenLink use their normal action chains.
	const auto AuthorTriggerChain = [&](const FResolvedBinding& Resolved, const FName TriggerWidgetName,
		const bool bMirrorTrigger, FChainLayout& EventLayout) -> bool
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
        EventLayout.PlaceExec(EventNode);
        const FName FunctionName = BindingFunctionName(bMirrorTrigger ? TEXT("mirror_trigger") : TEXT("trigger"), Resolved.BindingIndex);
        AppendOwnedFunctionCall(Ubergraph, WBP, EventLayout, FunctionName);
        if (!LinkExecChain(EventLayout.ExecChain)) return false;
        auto* Entry = FindOwnedFunctionEntry(WBP, FunctionName);
        UEdGraph* BodyGraph = Entry->GetGraph();
        FChainLayout Layout;
        Layout.PlaceExec(Entry);
		if (!Resolved.DragAcceptedPropertyName.IsNone())
		{
			auto* Accepted = FindFProperty<FBoolProperty>(WBP->SkeletonGeneratedClass, Resolved.DragAcceptedPropertyName);
			if (!Accepted || !Functions.Not_PreBool) return false;
			auto* Get = Layout.PlacePure(CreateVariableGet(BodyGraph, WBP, Accepted));
			auto* Not = Layout.PlacePure(CreateCallNode(BodyGraph, Functions.Not_PreBool));
			Get->GetValuePin()->MakeLinkTo(Not->FindPinChecked(TEXT("A")));
			auto* Gate = CreateBranchNode(BodyGraph);
			Layout.PlaceExec(Gate);
			Not->GetReturnValuePin()->MakeLinkTo(Gate->GetConditionPin());
		}

		bool bChainValid = true;
		switch (Step.Kind)
		{
		case EUMGPrototypeNavigationStepKind::AnimateWidget:
			if (Resolved.StateHostIndex != INDEX_NONE)
				bChainValid = AppendStateTransitionStart(BodyGraph, WBP, Functions, Targets, Binding, Resolved, Layout);
			else if (Binding.UsesHoverTrigger() || Binding.UsesPressTrigger())
				bChainValid = AppendLayerOrders(BodyGraph, WBP, Functions, Binding, Resolved, true, Layout)
					&& AppendReversibleAnimation(BodyGraph, WBP, Resolved, Binding.Transition.DurationSeconds, Layout);
			else
			{
				bChainValid = AppendLayerOrders(BodyGraph, WBP, Functions, Binding, Resolved, true, Layout);
				if (bChainValid) AppendPlayAnimationCall(BodyGraph, WBP, Functions, Resolved.WidgetAnimationName, Layout, false, true);
			}
			bChainValid = bChainValid && LinkExecChain(Layout.ExecChain);
			break;
		case EUMGPrototypeNavigationStepKind::Overlay:
		{
			// Host + page visibility: show the host, then the hosted target page - the plain page for
			// an overlay-only target, the mirror copy for a dual-target/initial page (both layers must
			// switch to SelfHitTestInvisible; a visible page under a collapsed host stays hidden). The
			// chain never touches the page host or the history: the overlay is a layered display over
			// the current page.
			bChainValid = AppendOverlayOpen(BodyGraph, WBP, Functions, Targets, Resolved, bMirrorTrigger, Layout);
			if (bChainValid && Resolved.WidgetAnimationName != NAME_None)
			{
				bChainValid = AppendStopAnimationCall(BodyGraph, WBP, Functions, Resolved.WidgetAnimationName, Layout);
				if (bChainValid) AppendPlayAnimationCall(BodyGraph, WBP, Functions, Resolved.WidgetAnimationName, Layout, false, false, true);
			}
			const FName Monitor = bMirrorTrigger ? Resolved.MirrorHoverMonitorEventName : Resolved.HoverMonitorEventName;
			if (bChainValid && Monitor != NAME_None) AppendAnimationTimer(BodyGraph, Functions, Monitor, 0.0f, Layout);
			bChainValid = bChainValid && LinkExecChain(Layout.ExecChain);
			break;
		}

		case EUMGPrototypeNavigationStepKind::Back:
		{
			// Back closes an open overlay without changing the active page or history.
			// When no overlay is open, pop history if it contains a page.
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
				UK2Node_VariableGet* HostGet = Layout.PlacePure(CreateVariableGet(BodyGraph, WBP, OverlayHostProperty));
				UK2Node_CallFunction* GetVisibilityNode = CreateCallNode(BodyGraph, Functions.GetVisibility);
				Layout.PlacePure(GetVisibilityNode);
				if (UEdGraphPin* SelfPin = GetVisibilityNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input))
				{
					HostGet->GetValuePin()->MakeLinkTo(SelfPin);
				}
				else
				{
					bChainValid = false;
				}

				UK2Node_EnumEquality* VisibilityIsCollapsed = CreateMarkerNode<UK2Node_EnumEquality>(BodyGraph,
					[](UK2Node_EnumEquality*) {});
				Layout.PlacePure(VisibilityIsCollapsed);
				if (bChainValid)
				{
					if (UEdGraphPin* VisibilityPin = GetVisibilityNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output))
					{
						// TryCreateConnection (not MakeLinkTo): the enum-equality inputs are wildcards
						// until a connection propagates ESlateVisibility to both A and B.
						bChainValid = BodyGraph->GetSchema()->TryCreateConnection(
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

				UK2Node_CallFunction* NotNode = CreateCallNode(BodyGraph, Functions.Not_PreBool);
				Layout.PlacePure(NotNode);
				if (bChainValid)
				{
					UEdGraphPin* EqualityPin = VisibilityIsCollapsed->GetReturnValuePin();
					UEdGraphPin* NotInputPin = NotNode->FindPin(TEXT("A"), EGPD_Input);
					bChainValid = EqualityPin && NotInputPin
						&& BodyGraph->GetSchema()->TryCreateConnection(EqualityPin, NotInputPin);
				}

				HostBranchNode = CreateBranchNode(BodyGraph);
				Layout.PlaceExec(HostBranchNode);
				if (bChainValid)
				{
					UEdGraphPin* ConditionPin = HostBranchNode->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input);
					UEdGraphPin* NotReturnPin = NotNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
					bChainValid = ConditionPin && NotReturnPin
						&& BodyGraph->GetSchema()->TryCreateConnection(NotReturnPin, ConditionPin);
				}
			}
			if (bChainValid && (!PageHost || !Targets.CurrentPageProperty || !Functions.ArrayIsEmpty
				|| !Functions.ArrayLastIndex || !Functions.ArrayGet || !Functions.ArrayRemove))
			{
				bChainValid = false;
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': page host or array engine functions unavailable for Back wiring"),
					*Binding.TriggerWidgetId));
			}
			if (bChainValid && HostBranchNode)
			{
				// TRUE (overlay open): the collapse chain ends the branch - the history pop below
				// stays unreached (shared overlay-exit sequence).
				FChainLayout CollapseLayout;
				CollapseLayout.RowY = Layout.RowY;
				CollapseLayout.NextColumn = Layout.NextColumn;
				AppendGeneratedIndexSet(BodyGraph, WBP, Targets.OverlayOwnerProperty, 0, CollapseLayout);
				bChainValid = AppendOverlayExitCollapse(BodyGraph, WBP, Functions,
					Targets, CollapseLayout);
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
					CreateVariableGet(BodyGraph, WBP, HistoryProperty));
				UK2Node_CallArrayFunction* IsEmptyNode = CreateArrayCallNode(BodyGraph, Functions.ArrayIsEmpty);
				Layout.PlacePure(IsEmptyNode); // BlueprintPure
				if (UEdGraphPin* TargetArrayPin = IsEmptyNode->FindPin(TEXT("TargetArray"), EGPD_Input))
				{
					LinkArrayPin(BodyGraph, HistoryGet->GetValuePin(), TargetArrayPin);
				}

				// The empty-history Branch is the entry of the false side: it hangs off the host
				// branch's FALSE pin (overlay closed) or directly off the event when no host exists.
				// It is placed manually (like the pop chain below) so the linear exec chain does not
				// chain it behind the host branch's TRUE path.
				UK2Node_IfThenElse* BranchNode = CreateBranchNode(BodyGraph);
				BranchNode->NodePosX = Layout.NextColumn++ * NodeColumnStep;
				BranchNode->NodePosY = Layout.RowY;
				if (UEdGraphPin* ConditionPin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input))
				{
					if (UEdGraphPin* IsEmptyPin = IsEmptyNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
					{
						IsEmptyPin->MakeLinkTo(ConditionPin);
					}
				}

				UK2Node_CallArrayFunction* LastIndexNode = CreateArrayCallNode(BodyGraph, Functions.ArrayLastIndex);
				Layout.PlacePure(LastIndexNode); // BlueprintPure
				if (UEdGraphPin* TargetArrayPin = LastIndexNode->FindPin(TEXT("TargetArray"), EGPD_Input))
				{
					LinkArrayPin(BodyGraph, HistoryGet->GetValuePin(), TargetArrayPin);
				}

				// The history stores VALUES (the page that was left), not positions in this array:
				// the pop reads history[last index] through Array_Get to preserve multi-step history.
				UK2Node_CallArrayFunction* LastValueNode = CreateArrayCallNode(BodyGraph, Functions.ArrayGet);
				Layout.PlacePure(LastValueNode); // BlueprintPure
				if (UEdGraphPin* TargetArrayPin = LastValueNode->FindPin(TEXT("TargetArray"), EGPD_Input))
				{
					LinkArrayPin(BodyGraph, HistoryGet->GetValuePin(), TargetArrayPin);
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

				auto* SwitchNode = CreateMarkerNode<UK2Node_SwitchInteger>(BodyGraph,
					[](UK2Node_SwitchInteger* Node) { Node->StartIndex = 0; });
				PlacePopExec(SwitchNode);
				if (UEdGraphPin* LastValuePin = LastValueNode->FindPin(TEXT("Item"), EGPD_Output))
				{
					LastValuePin->MakeLinkTo(SwitchNode->GetSelectionPin());
				}
				for (int32 PageIndex = 0; PageIndex < Targets.PageHostPages.Num(); ++PageIndex)
				{
					SwitchNode->AddPinToSwitchNode();
					FChainLayout PageChain;
					PageChain.NextColumn = Layout.NextColumn;
					PageChain.RowY = Layout.RowY + (PageIndex + 1) * NodeRowStep;
					if (!AppendPageSelection(BodyGraph, WBP, Functions, Targets, PageIndex, PageChain)) { bChainValid = false; break; }
					auto* RemoveNode = CreateArrayCallNode(BodyGraph, Functions.ArrayRemove);
					PageChain.PlaceExec(RemoveNode);
					LinkArrayPin(BodyGraph, HistoryGet->GetValuePin(), RemoveNode->FindPinChecked(TEXT("TargetArray")));
					LastIndexNode->GetReturnValuePin()->MakeLinkTo(RemoveNode->FindPinChecked(TEXT("IndexToRemove")));
					AppendPageOpenCall(BodyGraph, WBP, PageChain);
					if (!LinkExecChain(PageChain.ExecChain)) { bChainValid = false; break; }
					UEdGraphPin* CasePin = SwitchNode->FindPin(SwitchNode->GetPinNameGivenIndex(PageIndex), EGPD_Output);
					if (!CasePin) { bChainValid = false; break; }
					CasePin->MakeLinkTo(PageChain.ExecChain[0]->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
					Layout.NextColumn = FMath::Max(Layout.NextColumn, PageChain.NextColumn);
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

		case EUMGPrototypeNavigationStepKind::CloseOverlay:
		{
			bChainValid = AppendCloseOverlayTail(BodyGraph, WBP, Functions, Targets, Layout) && LinkExecChain(Layout.ExecChain);
			break;
		}

		case EUMGPrototypeNavigationStepKind::Navigate:
		{
			if (!PageHost || !Targets.CurrentPageProperty)
			{
				bChainValid = false;
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': page host '%s' or current page state is unavailable"),
					*Binding.TriggerWidgetId, *PageHostBaseName));
				break;
			}
			if (Resolved.bTemporaryNavigate)
			{
				bChainValid = AppendTemporaryPageStart(BodyGraph, WBP, Functions, Targets, Binding, Resolved, Layout)
					&& LinkExecChain(Layout.ExecChain);
				break;
			}
			if (bTriggerInsideOverlayHost)
			{
				// Overlay-context Navigate (fired from inside the open overlay): exit the
				// overlay first - the shared Back TRUE-branch collapse - then the standard
				// navigate tail. Figma semantics: navigate replaces the current screen, overlay
				// included. No transition animations: the overlay layer is closing, not
				// animating the page host underneath.
				AppendGeneratedIndexSet(BodyGraph, WBP, Targets.OverlayOwnerProperty, 0, Layout);
				bChainValid = AppendOverlayExitCollapse(BodyGraph, WBP, Functions,
					Targets, Layout)
					&& AppendNavigateTail(BodyGraph, WBP, Functions, Targets, Resolved.TargetPageIndex, Layout)
					&& LinkExecChain(Layout.ExecChain);
				break;
			}
			if (Resolved.WidgetAnimationName != NAME_None)
			{
				AppendSourcePageGate(BodyGraph, WBP, Functions, Targets, Resolved, Layout);
				bChainValid = AppendPageTransitionStart(BodyGraph, WBP, Functions, Targets, Binding, Resolved, Layout)
					&& LinkExecChain(Layout.ExecChain);
				break;
			}
			bChainValid = AppendNavigateTail(BodyGraph, WBP, Functions, Targets, Resolved.TargetPageIndex, Layout);
			if (bChainValid)
			{
				bChainValid = LinkExecChain(Layout.ExecChain);
			}
			break;
		}

		case EUMGPrototypeNavigationStepKind::OpenLink:
			bChainValid = AppendOpenLinkTail(BodyGraph, Functions, Step, Layout)
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
		if (Resolved.bDrag) continue;
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
		if ((Binding.UsesHoverTrigger() || Binding.UsesPressTrigger())
			&& (((Resolved.WidgetAnimationName != NAME_None || (Resolved.bTemporaryState && Binding.Transition.bStateInstant))
				&& Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::AnimateWidget) || Resolved.bTemporaryNavigate))
		{
			const FName Delegate = Binding.UsesHoverTrigger()
				? GET_MEMBER_NAME_CHECKED(UButton, OnUnhovered) : GET_MEMBER_NAME_CHECKED(UButton, OnReleased);
			if (!FKismetEditorUtilities::FindBoundEventForComponent(WBP, Delegate, Resolved.TriggerWidgetName))
			{
				auto* Property = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Resolved.TriggerWidgetName);
				auto* Signature = FindFProperty<FMulticastDelegateProperty>(UButton::StaticClass(), Delegate);
				auto* Event = CreateMarkerNode<UK2Node_ComponentBoundEvent>(Ubergraph, [&](UK2Node_ComponentBoundEvent* Node)
				{
					Node->InitializeComponentBoundEventParams(Property, Signature);
				});
				FChainLayout Reverse;
				Reverse.RowY = WiredCount++ * NodeRowStep;
				FChainLayout Invoke;
                Invoke.PlaceExec(Event);
                const FName FunctionName = BindingFunctionName(TEXT("release"), Resolved.BindingIndex);
                AppendOwnedFunctionCall(Ubergraph, WBP, Invoke, FunctionName);
                LinkExecChain(Invoke.ExecChain);
                auto* Entry = FindOwnedFunctionEntry(WBP, FunctionName);
                UEdGraph* BodyGraph = Entry->GetGraph();
                Reverse.PlaceExec(Entry);
				if (Resolved.bTemporaryNavigate)
				{
					AppendOwnerGate(BodyGraph, WBP, Functions, Targets.TransitionOwnerProperty, Resolved.BindingIndex + 1, Reverse);
					if (Resolved.WidgetAnimationName.IsNone())
					{
						if (!AppendTemporaryPageVisibility(BodyGraph, WBP, Functions, Targets, Resolved, false, Reverse))
							SkipBinding(OutWarnings, TEXT("temporary target page could not be hidden"));
						AppendGeneratedIndexSet(BodyGraph, WBP, Targets.TransitionOwnerProperty, 0, Reverse);
					}
					else
					{
						if (!AppendLayerOrders(BodyGraph, WBP, Functions, Binding, Resolved, false, Reverse)
							|| !AppendReversibleReturnWithCompletion(BodyGraph, WBP, Functions, Resolved, Resolved.NavigationEventName, Reverse))
							SkipBinding(OutWarnings, TEXT("temporary navigation could not reverse"));
					}
				}
				else if (Resolved.bTemporaryState && Binding.Transition.bStateInstant && Targets.StateHosts.IsValidIndex(Resolved.StateHostIndex))
				{
					const auto& Host = Targets.StateHosts[Resolved.StateHostIndex];
					AppendOwnerGate(BodyGraph, WBP, Functions, Host.TransitionOwnerProperty, Resolved.BindingIndex + 1, Reverse);
					if (!AppendTemporaryStateVisibility(BodyGraph, WBP, Functions, Host, Resolved, false, Reverse))
						SkipBinding(OutWarnings, TEXT("temporary state could not restore its source opacity"));
					AppendGeneratedIndexSet(BodyGraph, WBP, Host.TransitionOwnerProperty, 0, Reverse);
					AppendStateTimeoutCall(BodyGraph, WBP, Reverse);
				}
				else
				{
					if (Resolved.bTemporaryState && (Binding.Transition.Type == TEXT("DISSOLVE") || !Resolved.LivePresentations.IsEmpty()))
					{
						const auto& Host = Targets.StateHosts[Resolved.StateHostIndex];
						AppendOwnerGate(BodyGraph, WBP, Functions, Host.TransitionOwnerProperty, Resolved.BindingIndex + 1, Reverse);
					}
					const bool bTemporaryDissolve = Resolved.bTemporaryState && Binding.Transition.Type == TEXT("DISSOLVE");
					const FName Completion = !Resolved.LiveReturnEventName.IsNone() ? Resolved.LiveReturnEventName
						: (bTemporaryDissolve ? Resolved.StateCompletionEventName : NAME_None);
					const bool bLayerRestored = AppendLayerOrders(BodyGraph, WBP, Functions, Binding, Resolved, false, Reverse);
					const bool bReversed = bLayerRestored && (Completion.IsNone()
						? AppendReversibleAnimation(BodyGraph, WBP, Resolved, 0.0f, Reverse)
						: AppendReversibleReturnWithCompletion(BodyGraph, WBP, Functions, Resolved, Completion, Reverse));
					if (!bReversed) SkipBinding(OutWarnings, TEXT("reversible animation could not restore its source state"));
				}
				LinkExecChain(Reverse.ExecChain);
			}
		}
		if (Binding.UsesPressTrigger() && Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Overlay
			&& !AuthorPressOverlayRelease(Ubergraph, WBP, Functions, Targets, Resolved,
				Resolved.TriggerWidgetName, (WiredCount++ + 1) * NodeRowStep))
		{
			SkipBinding(OutWarnings, TEXT("press overlay release could not be wired"));
		}

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
			if (Binding.UsesPressTrigger() && Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Overlay
				&& !AuthorPressOverlayRelease(Ubergraph, WBP, Functions, Targets, Resolved,
					Resolved.MirrorTriggerWidgetName, (WiredCount++ + 1) * NodeRowStep))
			{
				SkipBinding(OutWarnings, TEXT("mirror press overlay release could not be wired"));
			}
			++WiredCount;
		}
	}

	return WiredCount;
}
}
