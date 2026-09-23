// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "UMGSchemaSourceExtensions.h"
#include "Blueprint/UserWidget.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "InputCoreTypes.h"
#include "K2Node_CallFunction.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{
namespace
{
	/**
	 * Resolves a Figma key code (uppercased by the parser) to an FKey name. General rule first:
	 * direct match, then first-letter capitalization (covers ENTER->Enter, ESCAPE->Escape,
	 * LEFT->Left); aliases only cover the known cross-name differences.
	 */
	FName ResolveKeyCodeAsFKeyName(const FString& KeyCode)
	{
		if (KeyCode.IsEmpty())
		{
			return NAME_None;
		}
		if (FKey(FName(*KeyCode)).IsValid())
		{
			return FName(*KeyCode);
		}
		const FString Capitalized = KeyCode.Left(1).ToUpper() + KeyCode.RightChop(1).ToLower();
		if (FKey(FName(*Capitalized)).IsValid())
		{
			return FName(*Capitalized);
		}
		static const TMap<FString, FName> KeyNameAliases{
			{ TEXT("SPACE"), FName(TEXT("SpaceBar")) },
			{ TEXT("BACKSPACE"), FName(TEXT("BackSpace")) },
		};
		const FName* Aliased = KeyNameAliases.Find(KeyCode);
		if (Aliased && FKey(*Aliased).IsValid())
		{
			return *Aliased;
		}
		return NAME_None;
	}
}


bool WireKeyboardBindings(UWidgetBlueprint* WBP,
	const FUMGPrototypeNavigationData& NavigationData, const FEngineFunctions& Functions,
	const FBindingPlan& Plan, const FBindingGraphTargets& Targets, TArray<FString>& OutWarnings)
{
	UWidgetSwitcher* Switcher = Plan.Switcher;
	UOverlay* OverlayHost = Plan.OverlayHost;
	const auto& KeyboardBindings = Plan.KeyboardBindings;
	FObjectProperty* SwitcherProperty = Targets.SwitcherProperty;
	FArrayProperty* HistoryProperty = Targets.HistoryProperty;
	// Keyboard wiring: one generated function-graph override per event kind. OnKeyDown/OnKeyUp are
	// BlueprintImplementableEvents returning FEventReply, so they cannot live in the ubergraph; the
	// engine-standard encoding is an override function graph (the editor's own path for such
	// functions), with one key-equality Branch per (binding, key code) guarding the step chain.
	/**
	 * Build the keyboard guard for the active switcher page or visible overlay page.
	 * Page-less bindings leave OutConditionPin null.
	 */
	const auto BuildPageContextCondition = [&](UEdGraph* Graph, const FResolvedBinding& Resolved,
		FChainLayout& Layout, UEdGraphPin*& OutConditionPin) -> bool
	{
		OutConditionPin = nullptr;
		if (Resolved.SourcePageId.IsEmpty())
		{
			return true;
		}
		const bool bNeedsSwitcherGate = Resolved.SourceSwitcherPageIndex != INDEX_NONE;
		const bool bNeedsOverlayGate = Resolved.SourceHostedPageWidgetName != NAME_None;
		if (!bNeedsSwitcherGate && !bNeedsOverlayGate)
		{
			// A binding without a resolved tree home is ungated only in a single-page Blueprint.
			return Switcher == nullptr && OverlayHost == nullptr;
		}
		if (!Functions.BooleanAND)
		{
			return false;
		}

		UEdGraphPin* SwitcherConditionPin = nullptr;
		if (bNeedsSwitcherGate)
		{
			if (!SwitcherProperty || !Functions.GetActiveWidgetIndex || !Functions.EqualEqual_IntInt)
			{
				return false;
			}
			UK2Node_VariableGet* SwitcherGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, SwitcherProperty));
			UK2Node_CallFunction* ActiveIndexNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
				[&Functions](UK2Node_CallFunction* Node)
				{
					Node->SetFromFunction(Functions.GetActiveWidgetIndex);
				});
			Layout.PlacePure(ActiveIndexNode); // BlueprintGetter => BlueprintPure
			UEdGraphPin* IndexSelfPin = ActiveIndexNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
			if (!IndexSelfPin)
			{
				return false;
			}
			SwitcherGet->GetValuePin()->MakeLinkTo(IndexSelfPin);

			UK2Node_CallFunction* IndexEqualsNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
				[&Functions](UK2Node_CallFunction* Node)
				{
					Node->SetFromFunction(Functions.EqualEqual_IntInt);
				});
			Layout.PlacePure(IndexEqualsNode); // BlueprintPure
			UEdGraphPin* IndexAPin = IndexEqualsNode->FindPin(TEXT("A"), EGPD_Input);
			UEdGraphPin* IndexBPin = IndexEqualsNode->FindPin(TEXT("B"), EGPD_Input);
			UEdGraphPin* ActiveIndexPin = ActiveIndexNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
			if (!IndexAPin || !IndexBPin || !ActiveIndexPin)
			{
				return false;
			}
			ActiveIndexPin->MakeLinkTo(IndexAPin);
			IndexBPin->DefaultValue = FString::FromInt(Resolved.SourceSwitcherPageIndex);
			SwitcherConditionPin = IndexEqualsNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
			if (!SwitcherConditionPin)
			{
				return false;
			}
		}

		UEdGraphPin* OverlayConditionPin = nullptr;
		if (bNeedsOverlayGate)
		{
			if (!Functions.GetVisibility || !Functions.Not_PreBool || !WBP->SkeletonGeneratedClass)
			{
				return false;
			}
			FObjectProperty* HostedPageProperty = FindFProperty<FObjectProperty>(
				WBP->SkeletonGeneratedClass, Resolved.SourceHostedPageWidgetName);
			if (!HostedPageProperty)
			{
				return false;
			}
			UK2Node_VariableGet* HostedPageGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, HostedPageProperty));
			UK2Node_CallFunction* GetVisibilityNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
				[&Functions](UK2Node_CallFunction* Node)
				{
					Node->SetFromFunction(Functions.GetVisibility);
				});
			Layout.PlacePure(GetVisibilityNode); // BlueprintGetter => BlueprintPure
			UEdGraphPin* VisibilitySelfPin = GetVisibilityNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
			if (!VisibilitySelfPin)
			{
				return false;
			}
			HostedPageGet->GetValuePin()->MakeLinkTo(VisibilitySelfPin);

			UK2Node_EnumEquality* VisibilityIsCollapsed = CreateMarkerNode<UK2Node_EnumEquality>(Graph,
				[](UK2Node_EnumEquality*) {});
			Layout.PlacePure(VisibilityIsCollapsed);
			UEdGraphPin* VisibilityPin = GetVisibilityNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
			if (!VisibilityPin
				|| !Graph->GetSchema()->TryCreateConnection(VisibilityPin, VisibilityIsCollapsed->GetInput1Pin()))
			{
				return false;
			}
			const UEnum* VisibilityEnum = StaticEnum<ESlateVisibility>();
			UEdGraphPin* ComparePin = VisibilityIsCollapsed->GetInput2Pin();
			if (!ComparePin || !VisibilityEnum)
			{
				return false;
			}
			ComparePin->DefaultValue = VisibilityEnum->GetNameStringByValue(
				static_cast<int64>(ESlateVisibility::Collapsed));

			UK2Node_CallFunction* NotCollapsedNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
				[&Functions](UK2Node_CallFunction* Node)
				{
					Node->SetFromFunction(Functions.Not_PreBool);
				});
			Layout.PlacePure(NotCollapsedNode); // BlueprintPure
			UEdGraphPin* CollapsedPin = VisibilityIsCollapsed->GetReturnValuePin();
			UEdGraphPin* NotInputPin = NotCollapsedNode->FindPin(TEXT("A"), EGPD_Input);
			if (!CollapsedPin || !NotInputPin
				|| !Graph->GetSchema()->TryCreateConnection(CollapsedPin, NotInputPin))
			{
				return false;
			}
			OverlayConditionPin = NotCollapsedNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
			if (!OverlayConditionPin)
			{
				return false;
			}
		}

		if (SwitcherConditionPin && OverlayConditionPin)
		{
			if (!Functions.BooleanOR)
			{
				return false;
			}
			UK2Node_CallFunction* OrNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
				[&Functions](UK2Node_CallFunction* Node)
				{
					Node->SetFromFunction(Functions.BooleanOR);
				});
			Layout.PlacePure(OrNode); // BlueprintPure
			UEdGraphPin* A = OrNode->FindPin(TEXT("A"), EGPD_Input);
			UEdGraphPin* B = OrNode->FindPin(TEXT("B"), EGPD_Input);
			UEdGraphPin* OrReturn = OrNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
			if (!A || !B || !OrReturn)
			{
				return false;
			}
			SwitcherConditionPin->MakeLinkTo(A);
			OverlayConditionPin->MakeLinkTo(B);
			OutConditionPin = OrReturn;
		}
		else
		{
			OutConditionPin = SwitcherConditionPin ? SwitcherConditionPin : OverlayConditionPin;
		}
		return OutConditionPin != nullptr;
	};

	bool bKeyboardWired = false;
	struct FEventKind { bool bKeyDown; FName EventName; };
	const FEventKind EventKinds[] = {
		{ true, FName(TEXT("OnKeyDown")) },
		{ false, FName(TEXT("OnKeyUp")) },
	};
	for (const FEventKind& EventKind : EventKinds)
	{
		TArray<const FResolvedBinding*> KindBindings;
		for (const FResolvedBinding& Resolved : KeyboardBindings)
		{
			const FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings[Resolved.BindingIndex];
			if (EventKind.bKeyDown ? Binding.UsesKeyDownTrigger() : Binding.UsesKeyUpTrigger())
			{
				KindBindings.Add(&Resolved);
			}
		}
		if (KindBindings.Num() == 0)
		{
			continue;
		}

		// A surviving same-named function graph after the clear step is user content; do not
		// mask it with generated wiring.
		const bool bUserOverrideExists = WBP->FunctionGraphs.ContainsByPredicate(
			[&EventKind](const UEdGraph* Graph)
			{
				return Graph && Graph->GetFName() == EventKind.EventName;
			});
		if (bUserOverrideExists)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("keyboard event %s: a same-named override graph already exists; keyboard wiring skipped to preserve it"),
				*EventKind.EventName.ToString()));
			continue;
		}
		if (!Functions.GetKeyEventKey || !Functions.EqualKeyKey)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("keyboard event %s: UKismetInputLibrary key functions not found"),
				*EventKind.EventName.ToString()));
			continue;
		}

		UEdGraph* KeyGraph = FBlueprintEditorUtils::CreateNewGraph(
			WBP, EventKind.EventName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph(
			WBP, KeyGraph, /*bIsUserCreated=*/false, UUserWidget::StaticClass());

		UK2Node_FunctionEntry* EntryNode = nullptr;
		UK2Node_FunctionResult* ResultNode = nullptr;
		for (UEdGraphNode* Node : KeyGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			Node->NodeComment = NodeMarkerComment; // the whole graph is generated content
			if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
			{
				EntryNode = Entry;
			}
			else if (UK2Node_FunctionResult* Result = Cast<UK2Node_FunctionResult>(Node))
			{
				ResultNode = Result;
			}
		}
		UEdGraphPin* EntryThenPin = EntryNode
			? EntryNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output)
			: nullptr;
		UEdGraphPin* ResultExecPin = ResultNode
			? ResultNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input)
			: nullptr;
		if (!EntryNode || !ResultNode || !EntryThenPin || !ResultExecPin)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("keyboard event %s: override graph terminators missing (engine contract changed)"),
				*EventKind.EventName.ToString()));
			FBlueprintEditorUtils::RemoveGraph(WBP, KeyGraph, EGraphRemoveFlags::MarkTransient);
			continue;
		}
		// Insert the key branches between the entry and the (default) return.
		EntryThenPin->BreakAllPinLinks();

		// The override returns FEventReply; feed it the engine's Unhandled literal so the
		// compiled override compiles without the "return value must not be empty" warning.
		if (Functions.UnhandledReply)
		{
			if (UEdGraphPin* ReplyPin = ResultNode->FindPin(TEXT("ReturnValue"), EGPD_Input))
			{
				UK2Node_CallFunction* UnhandledNode = CreateMarkerNode<UK2Node_CallFunction>(KeyGraph,
					[&Functions](UK2Node_CallFunction* Node)
					{
						Node->SetFromFunction(Functions.UnhandledReply);
					});
				UnhandledNode->NodePosX = -NodeColumnStep;
				UnhandledNode->NodePosY = -NodeRowStep;
				if (UEdGraphPin* UnhandledValuePin = UnhandledNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
				{
					UnhandledValuePin->MakeLinkTo(ReplyPin);
				}
			}
		}

		UEdGraphPin* PreviousElsePin = nullptr;
		bool bAnyBranchWired = false;
		int32 KeyRow = 0;
		for (const FResolvedBinding* ResolvedPtr : KindBindings)
		{
			const FResolvedBinding& Resolved = *ResolvedPtr;
			const FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings[Resolved.BindingIndex];
			const FUMGPrototypeNavigationStep& Step = Binding.Steps[0];
			for (const FString& KeyCode : Binding.TriggerKeyCodes)
			{
				const FName KeyName = ResolveKeyCodeAsFKeyName(KeyCode);
				if (KeyName == NAME_None)
				{
					SkipBinding(OutWarnings, FString::Printf(
						TEXT("trigger '%s': key code '%s' has no matching FKey; key branch skipped"),
						*Binding.TriggerWidgetId, *KeyCode));
					continue;
				}

				FChainLayout Layout;
				Layout.RowY = KeyRow * NodeRowStep;
				Layout.NextColumn = 1; // the function entry sits at column 0

				UK2Node_CallFunction* GetKeyNode = CreateMarkerNode<UK2Node_CallFunction>(KeyGraph,
					[&Functions](UK2Node_CallFunction* Node)
					{
						Node->SetFromFunction(Functions.GetKeyEventKey);
					});
				Layout.PlacePure(GetKeyNode); // BlueprintPure
				if (UEdGraphPin* InputPin = GetKeyNode->FindPin(TEXT("Input"), EGPD_Input))
				{
					if (UEdGraphPin* KeyEventPin = EntryNode->FindPin(TEXT("InKeyEvent"), EGPD_Output))
					{
						KeyEventPin->MakeLinkTo(InputPin);
					}
				}

				UK2Node_CallFunction* KeyEqualsNode = CreateMarkerNode<UK2Node_CallFunction>(KeyGraph,
					[&Functions](UK2Node_CallFunction* Node)
					{
						Node->SetFromFunction(Functions.EqualKeyKey);
					});
				Layout.PlacePure(KeyEqualsNode); // BlueprintPure
				if (UEdGraphPin* APin = KeyEqualsNode->FindPin(TEXT("A"), EGPD_Input))
				{
					if (UEdGraphPin* GetKeyPin = GetKeyNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
					{
						GetKeyPin->MakeLinkTo(APin);
					}
				}
				if (UEdGraphPin* BPin = KeyEqualsNode->FindPin(TEXT("B"), EGPD_Input))
				{
					BPin->DefaultValue = KeyName.ToString();
				}

				// The branch fires only when the key matches AND the binding's own page is
				// active/visible: page A and page B may bind the same key to different actions,
				// and a hidden page's keyboard action must never run.
				UEdGraphPin* BranchConditionPin = KeyEqualsNode->FindPin(TEXT("ReturnValue"), EGPD_Output);
				UEdGraphPin* PageConditionPin = nullptr;
				if (!BranchConditionPin
					|| !BuildPageContextCondition(KeyGraph, Resolved, Layout, PageConditionPin))
				{
					SkipBinding(OutWarnings, FString::Printf(
						TEXT("trigger '%s': keyboard page-context guard unavailable; key branch skipped"),
						*Binding.TriggerWidgetId));
					continue;
				}
				if (PageConditionPin)
				{
					UK2Node_CallFunction* AndNode = CreateMarkerNode<UK2Node_CallFunction>(KeyGraph,
						[&Functions](UK2Node_CallFunction* Node)
						{
							Node->SetFromFunction(Functions.BooleanAND);
						});
					Layout.PlacePure(AndNode); // BlueprintPure
					UEdGraphPin* AndAPin = AndNode->FindPin(TEXT("A"), EGPD_Input);
					UEdGraphPin* AndBPin = AndNode->FindPin(TEXT("B"), EGPD_Input);
					UEdGraphPin* AndReturnPin = AndNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
					if (!AndAPin || !AndBPin || !AndReturnPin)
					{
						SkipBinding(OutWarnings, FString::Printf(
							TEXT("trigger '%s': keyboard page-context AND pins missing"),
							*Binding.TriggerWidgetId));
						continue;
					}
					BranchConditionPin->MakeLinkTo(AndAPin);
					PageConditionPin->MakeLinkTo(AndBPin);
					BranchConditionPin = AndReturnPin;
				}

				UK2Node_IfThenElse* BranchNode = CreateMarkerNode<UK2Node_IfThenElse>(KeyGraph,
					[](UK2Node_IfThenElse*) {});
				Layout.PlaceExec(BranchNode);
				if (UEdGraphPin* ConditionPin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input))
				{
					BranchConditionPin->MakeLinkTo(ConditionPin);
				}

				bool bTailValid = false;
				if (Step.Kind == EUMGPrototypeNavigationStepKind::Navigate)
				{
					bTailValid = AppendNavigateTail(KeyGraph, WBP, Functions,
						SwitcherProperty, HistoryProperty, Resolved.TargetPageIndex, Layout);
				}
				else
				{
					bTailValid = AppendOpenLinkTail(KeyGraph, Functions, Step, Layout);
				}
				if (!bTailValid || !LinkExecChain(Layout.ExecChain))
				{
					SkipBinding(OutWarnings, FString::Printf(
						TEXT("trigger '%s': keyboard chain nodes missing engine pins"),
						*Binding.TriggerWidgetId));
					continue;
				}

				// Daisy-chain: previous branch's false and this branch's true both feed forward.
				if (UEdGraphPin* BranchExecPin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input))
				{
					if (PreviousElsePin)
					{
						PreviousElsePin->MakeLinkTo(BranchExecPin);
					}
					else
					{
						EntryThenPin->MakeLinkTo(BranchExecPin);
					}
				}
				if (UEdGraphPin* ThenPin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output))
				{
					if (UEdGraphPin* TailExecPin = Layout.ExecChain[1]->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input))
					{
						ThenPin->MakeLinkTo(TailExecPin);
					}
				}
				if (UEdGraphPin* LastThenPin = Layout.ExecChain.Last()->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output))
				{
					LastThenPin->MakeLinkTo(ResultExecPin);
				}
				PreviousElsePin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output);
				bAnyBranchWired = true;
				++KeyRow;
			}
		}

		if (!bAnyBranchWired)
		{
			// No key branch survived: drop the graph instead of shipping an empty override that
			// would silently shadow the native event.
			FBlueprintEditorUtils::RemoveGraph(WBP, KeyGraph, EGraphRemoveFlags::MarkTransient);
			continue;
		}
		if (PreviousElsePin)
		{
			// Last non-matching key falls through to the default (unhandled) return.
			PreviousElsePin->MakeLinkTo(ResultExecPin);
		}
		bKeyboardWired = true;
	}

	return bKeyboardWired;
}
}
