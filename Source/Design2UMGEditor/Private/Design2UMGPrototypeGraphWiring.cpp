// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeGraphWiring.h"

#include "Design2UMGLog.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetUtils.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/PanelWidget.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/WidgetSwitcher.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "InputCoreTypes.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetInputLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{

const TCHAR NodeMarkerComment[] = TEXT("Design2UMG Prototype Navigation");
const TCHAR NavHistoryVariableName[] = TEXT("__d2u_nav_history");

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

	// Keyboard wiring lives in generated function graphs (UUserWidget::OnKeyDown/OnKeyUp overrides,
	// which return FEventReply and therefore cannot be ubergraph event nodes). A function graph
	// carrying marker nodes is plugin-generated content and is removed whole.
	TArray<UEdGraph*> GeneratedGraphs;
	for (UEdGraph* FunctionGraph : WBP->FunctionGraphs)
	{
		if (!FunctionGraph)
		{
			continue;
		}
		for (const UEdGraphNode* Node : FunctionGraph->Nodes)
		{
			if (Node && Node->NodeComment.Equals(NodeMarkerComment, ESearchCase::CaseSensitive))
			{
				GeneratedGraphs.Add(FunctionGraph);
				break;
			}
		}
	}
	for (UEdGraph* Graph : GeneratedGraphs)
	{
		// MarkTransient without Recompile, same rationale as the node removal above.
		FBlueprintEditorUtils::RemoveGraph(WBP, Graph, EGraphRemoveFlags::MarkTransient);
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

	/** Front-to-back then->exec linking of an exec chain; false when an engine pin contract breaks. */
	bool LinkExecChain(const TArray<UEdGraphNode*>& ExecChain)
	{
		for (int32 ChainIndex = 0; ChainIndex + 1 < ExecChain.Num(); ++ChainIndex)
		{
			UEdGraphPin* ThenPin = ExecChain[ChainIndex]->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			UEdGraphPin* ExecPin = ExecChain[ChainIndex + 1]->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			if (!ThenPin || !ExecPin)
			{
				return false;
			}
			ThenPin->MakeLinkTo(ExecPin);
		}
		return true;
	}

	/** Column/row placement + bookkeeping for one binding's exec chain and its pure nodes. */
	struct FChainLayout
	{
		TArray<UEdGraphNode*> ExecChain;
		int32 NextColumn = 0;
		int32 RowY = 0;

		UEdGraphNode* PlaceExec(UEdGraphNode* Node)
		{
			Node->NodePosX = NextColumn++ * NodeColumnStep;
			Node->NodePosY = RowY;
			ExecChain.Add(Node);
			return Node;
		}
		template <typename NodeType>
		NodeType* PlacePure(NodeType* Node)
		{
			Node->NodePosX = NextColumn++ * NodeColumnStep;
			Node->NodePosY = RowY + 16;
			return Node;
		}
	};

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

	/** Every engine function the wiring calls, resolved once per run. */
	struct FEngineFunctions
	{
		UFunction* SetActiveWidgetIndex = nullptr;
		UFunction* GetActiveWidgetIndex = nullptr;
		UFunction* LaunchURL = nullptr;
		UFunction* PlayAnimation = nullptr;
		UFunction* Delay = nullptr;
		UFunction* SetVisibility = nullptr;
		UFunction* ArrayAdd = nullptr;
		UFunction* ArrayIsEmpty = nullptr;
		UFunction* ArrayLastIndex = nullptr;
		UFunction* ArrayRemove = nullptr;
		UFunction* GetKeyEventKey = nullptr;
		UFunction* EqualKeyKey = nullptr;
		UFunction* UnhandledReply = nullptr;

		FEngineFunctions()
		{
			SetActiveWidgetIndex = UWidgetSwitcher::StaticClass()->FindFunctionByName(TEXT("SetActiveWidgetIndex"));
			GetActiveWidgetIndex = UWidgetSwitcher::StaticClass()->FindFunctionByName(TEXT("GetActiveWidgetIndex"));
			LaunchURL = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("LaunchURL"));
			PlayAnimation = UUserWidget::StaticClass()->FindFunctionByName(TEXT("PlayAnimation"));
			Delay = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("Delay"));
			SetVisibility = UWidget::StaticClass()->FindFunctionByName(TEXT("SetVisibility"));
			ArrayAdd = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_Add"));
			ArrayIsEmpty = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_IsEmpty"));
			ArrayLastIndex = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_LastIndex"));
			ArrayRemove = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_Remove"));
			GetKeyEventKey = UKismetInputLibrary::StaticClass()->FindFunctionByName(TEXT("GetKey"));
			EqualKeyKey = UKismetInputLibrary::StaticClass()->FindFunctionByName(TEXT("EqualEqual_KeyKey"));
			UnhandledReply = UWidgetBlueprintLibrary::StaticClass()->FindFunctionByName(TEXT("Unhandled"));
		}
	};

	/** Variable get against a skeleton class property; every widget/animation/history read uses it. */
	UK2Node_VariableGet* CreateVariableGet(UEdGraph* Graph, UWidgetBlueprint* WBP, const FProperty* Property)
	{
		return CreateMarkerNode<UK2Node_VariableGet>(Graph,
			[Property, WBP](UK2Node_VariableGet* Node)
			{
				Node->SetFromProperty(Property, /*bSelfContext=*/true, WBP->SkeletonGeneratedClass);
			});
	}

	/**
	 * Array library calls are authored with the engine's dedicated UK2Node_CallArrayFunction and
	 * connected through the schema: the TargetArray pins are wildcards (schema-level ArrayParam
	 * meta, independent of node class), and only TryCreateConnection ->
	 * PinConnectionListChanged -> PropagateArrayTypeInfo resolves them. A raw MakeLinkTo would
	 * leave the pin untyped ("Target Array type is undetermined") and break compilation.
	 */
	UK2Node_CallArrayFunction* CreateArrayCallNode(UEdGraph* Graph, UFunction* Function)
	{
		return CreateMarkerNode<UK2Node_CallArrayFunction>(Graph,
			[Function](UK2Node_CallArrayFunction* Node)
			{
				Node->SetFromFunction(Function);
			});
	}

	bool LinkArrayPin(UEdGraph* Graph, UEdGraphPin* SourcePin, UEdGraphPin* TargetPin)
	{
		return SourcePin && TargetPin && Graph->GetSchema()->TryCreateConnection(SourcePin, TargetPin);
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
	 * Appends the shared Navigate tail: pure GetActiveWidgetIndex(switcher) feeding
	 * Array_Add(history, index) -> SetActiveWidgetIndex(target). The push reads the switcher
	 * immediately before the switch, so the pushed value is always the page actually being left.
	 * GetActiveWidgetIndex is a BlueprintGetter and therefore compiled BlueprintPure (no exec pins);
	 * the array calls use the engine's UK2Node_CallArrayFunction with schema-side connection (see
	 * CreateArrayCallNode).
	 */
	bool AppendNavigateTail(
		UEdGraph* Graph,
		UWidgetBlueprint* WBP,
		const FEngineFunctions& Functions,
		FObjectProperty* SwitcherProperty,
		FArrayProperty* HistoryProperty,
		int32 TargetPageIndex,
		FChainLayout& Layout)
	{
		if (!Functions.ArrayAdd || !Functions.SetActiveWidgetIndex || !Functions.GetActiveWidgetIndex)
		{
			return false;
		}

		UK2Node_VariableGet* SwitcherForIndexGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, SwitcherProperty));
		UK2Node_CallFunction* ActiveIndexNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
			[&Functions](UK2Node_CallFunction* Node)
			{
				Node->SetFromFunction(Functions.GetActiveWidgetIndex);
			});
		Layout.PlacePure(ActiveIndexNode); // BlueprintGetter => BlueprintPure, no exec pins
		if (UEdGraphPin* SelfPin = ActiveIndexNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input))
		{
			SwitcherForIndexGet->GetValuePin()->MakeLinkTo(SelfPin);
		}

		UK2Node_VariableGet* HistoryGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, HistoryProperty));
		UK2Node_CallArrayFunction* PushNode = CreateArrayCallNode(Graph, Functions.ArrayAdd);
		Layout.PlaceExec(PushNode);
		if (UEdGraphPin* TargetArrayPin = PushNode->FindPin(TEXT("TargetArray"), EGPD_Input))
		{
			LinkArrayPin(Graph, HistoryGet->GetValuePin(), TargetArrayPin);
		}
		if (UEdGraphPin* NewItemPin = PushNode->FindPin(TEXT("NewItem"), EGPD_Input))
		{
			if (UEdGraphPin* ActiveIndexPin = ActiveIndexNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
			{
				ActiveIndexPin->MakeLinkTo(NewItemPin);
			}
		}

		UK2Node_VariableGet* SwitcherForSwitchGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, SwitcherProperty));
		UK2Node_CallFunction* SwitchNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
			[&Functions](UK2Node_CallFunction* Node)
			{
				Node->SetFromFunction(Functions.SetActiveWidgetIndex);
			});
		Layout.PlaceExec(SwitchNode);
		if (UEdGraphPin* IndexPin = SwitchNode->FindPin(TEXT("Index"), EGPD_Input))
		{
			IndexPin->DefaultValue = FString::FromInt(TargetPageIndex);
		}
		if (UEdGraphPin* SelfPin = SwitchNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input))
		{
			SwitcherForSwitchGet->GetValuePin()->MakeLinkTo(SelfPin);
		}
		return true;
	}

	/** Appends the shared OpenLink tail: LaunchURL(url literal). */
	bool AppendOpenLinkTail(
		UEdGraph* Graph,
		const FEngineFunctions& Functions,
		const FUMGPrototypeNavigationStep& Step,
		FChainLayout& Layout)
	{
		if (!Functions.LaunchURL)
		{
			return false;
		}
		UK2Node_CallFunction* CallNode = CreateMarkerNode<UK2Node_CallFunction>(Graph,
			[&Functions](UK2Node_CallFunction* Node)
			{
				Node->SetFromFunction(Functions.LaunchURL);
			});
		Layout.PlaceExec(CallNode);
		if (UEdGraphPin* UrlPin = CallNode->FindPin(TEXT("URL"), EGPD_Input))
		{
			UrlPin->DefaultValue = Step.ExternalUrl;
		}
		return true;
	}

	/**
	 * Appends a UWidget::SetVisibility call on a widget variable get. The old runtime's overlay
	 * semantics (reparenting the overlay page into the host) cannot be expressed with standard
	 * nodes; showing/collapsing the host is the decided encoding. Back always collapses the host
	 * (simplification: the old runtime popped the overlay before history only when one was open,
	 * which standard nodes cannot observe).
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

	const FEngineFunctions Functions;

	// Shared tree resolution: tree widgets are stable across the skeleton refresh below, so they are
	// resolved once; class properties are read after any refresh instead of cached across it.
	const FString SwitcherBaseName = FUMGWidgetUtils::SanitizeWidgetName(
		NavigationData.SwitcherWidgetName, TEXT("WidgetSwitcher")).ToString();
	UWidgetSwitcher* Switcher = FUMGWidgetUtils::FindUniqueTreeWidget<UWidgetSwitcher>(
		WBP->WidgetTree, SwitcherBaseName);
	UOverlay* OverlayHost = FUMGWidgetUtils::FindUniqueTreeWidget<UOverlay>(
		WBP->WidgetTree, FUMGWidgetUtils::SanitizeWidgetName(
			NavigationData.OverlayHostWidgetName, TEXT("Overlay")).ToString());

	// Pass 1 - resolve every candidate binding without touching the blueprint, so the (potentially
	// skeleton-refreshing) history variable creation below cannot invalidate cached properties.
	struct FResolvedBinding
	{
		int32 BindingIndex = INDEX_NONE;
		FName DelegateName;
		FName TriggerWidgetName;
		FName OutgoingAnimationName;
		FName IncomingAnimationName;
		int32 TargetPageIndex = INDEX_NONE;
		float DelaySeconds = 0.0f;
		bool bTransitionChain = false;
	};
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
				TEXT("trigger '%s' (%s): binding is not a simple single-action mouse Navigate/OpenLink/Overlay/Back or keyboard Navigate/OpenLink (Phase 3)"),
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
				FUMGWidgetUtils::SanitizeWidgetName(Step.TargetPageId, TEXT("CanvasPanel")).ToString());
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

		if (bMouse)
		{
			// Resolve the trigger button in the built tree; node params must come from the built
			// tree, never from the IR names, to stay immune to name drift.
			UButton* TriggerButton = FUMGWidgetUtils::FindUniqueTreeWidget<UButton>(WBP->WidgetTree,
				FUMGWidgetUtils::SanitizeWidgetName(Binding.TriggerWidgetId, TEXT("Button")).ToString());
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

			// Phase 2: a Navigate binding whose transition produced generated animations plays them
			// around the page switch; missing engine functions or unresolved animation class
			// properties degrade to the direct wiring with a warning.
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
			// Keyboard: the key itself is the trigger, so there is no button to resolve; key codes
			// resolve to FKey names at authoring time below.
			KeyboardBindings.Add(Resolved);
		}
	}

	// Pass 2 - structural preparation, all in one place because each mutation needs the same
	// skeleton refresh (which recreates the class properties, so it must precede pass 3's caching):
	//   1. The overlay host must be exposed as a variable: UOverlay defaults bIsVariable=false
	//      (unlike UButton/UWidgetSwitcher), so synthesized hosts never become class properties
	//      otherwise.
	//   2. The generated TArray<int32> history member variable, needed by any resolved Navigate
	//      (push) or Back (pop) wiring. AddMemberVariable refuses to duplicate an existing name,
	//      which is exactly the regenerate case: the previous run's variable is reused. A
	//      same-named variable that is not a TArray<int32> (user content) disables history wiring.
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

	bool bNeedsSkeletonRefresh = false;
	if (OverlayHost
		&& MouseBindings.ContainsByPredicate(ReachesOverlayHost)
		&& !FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, OverlayHost->GetFName()))
	{
		OverlayHost->bIsVariable = true;
		bNeedsSkeletonRefresh = true;
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

	int32 WiredCount = 0;
	for (const FResolvedBinding& Resolved : MouseBindings)
	{
		const FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings[Resolved.BindingIndex];
		const FUMGPrototypeNavigationStep& Step = Binding.Steps[0];
		const int32 RowY = WiredCount * NodeRowStep;

		FObjectProperty* ButtonProperty = FindFProperty<FObjectProperty>(
			WBP->SkeletonGeneratedClass, Resolved.TriggerWidgetName);
		FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(
			UButton::StaticClass(), Resolved.DelegateName);
		if (!ButtonProperty || !DelegateProperty)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s': button/delegate property missing after skeleton refresh"),
				*Binding.TriggerWidgetId));
			continue;
		}

		UK2Node_ComponentBoundEvent* EventNode = CreateMarkerNode<UK2Node_ComponentBoundEvent>(Ubergraph,
			[ButtonProperty, DelegateProperty](UK2Node_ComponentBoundEvent* Node)
			{
				Node->InitializeComponentBoundEventParams(ButtonProperty, DelegateProperty);
			});
		FChainLayout Layout;
		Layout.RowY = RowY;
		Layout.PlaceExec(EventNode);

		bool bChainValid = true;
		switch (Step.Kind)
		{
		case EUMGPrototypeNavigationStepKind::Overlay:
			bChainValid = AppendSetVisibilityCall(Ubergraph, WBP, Functions,
				OverlayHostProperty, ESlateVisibility::SelfHitTestInvisible, Layout)
				&& LinkExecChain(Layout.ExecChain);
			break;

		case EUMGPrototypeNavigationStepKind::Back:
		{
			// Overlay host first: Back always collapses it before touching history (see
			// AppendSetVisibilityCall for the simplification note).
			if (OverlayHost && OverlayHostProperty)
			{
				bChainValid = AppendSetVisibilityCall(Ubergraph, WBP, Functions,
					OverlayHostProperty, ESlateVisibility::Collapsed, Layout);
			}
			if (bChainValid && (!Switcher || !SwitcherProperty || !Functions.ArrayIsEmpty
				|| !Functions.ArrayLastIndex || !Functions.ArrayRemove))
			{
				bChainValid = false;
				SkipBinding(OutWarnings, FString::Printf(
					TEXT("trigger '%s': switcher or array engine functions unavailable for Back wiring"),
					*Binding.TriggerWidgetId));
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

				UK2Node_IfThenElse* BranchNode = CreateMarkerNode<UK2Node_IfThenElse>(Ubergraph,
					[](UK2Node_IfThenElse*) {});
				Layout.PlaceExec(BranchNode);
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
					if (UEdGraphPin* LastIndexPin = LastIndexNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
					{
						LastIndexPin->MakeLinkTo(IndexPin);
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

				bChainValid = LinkExecChain(Layout.ExecChain);
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
			if (Resolved.bTransitionChain && Resolved.OutgoingAnimationName != NAME_None)
			{
				AppendPlayAnimationCall(Ubergraph, WBP, Functions, Resolved.OutgoingAnimationName, Layout);
			}
			if (Resolved.bTransitionChain)
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
			if (bChainValid && Resolved.bTransitionChain && Resolved.IncomingAnimationName != NAME_None)
			{
				AppendPlayAnimationCall(Ubergraph, WBP, Functions, Resolved.IncomingAnimationName, Layout);
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

		if (!bChainValid)
		{
			SkipBinding(OutWarnings, FString::Printf(
				TEXT("trigger '%s': generated nodes missing engine pins (engine contract changed)"),
				*Binding.TriggerWidgetId));
		}
		++WiredCount;
	}

	// Keyboard wiring: one generated function-graph override per event kind. OnKeyDown/OnKeyUp are
	// BlueprintImplementableEvents returning FEventReply, so they cannot live in the ubergraph; the
	// engine-standard encoding is an override function graph (the editor's own path for such
	// functions), with one key-equality Branch per (binding, key code) guarding the step chain.
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
				TEXT("keyboard event %s: a non-generated override graph already exists; keyboard wiring skipped"),
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

				UK2Node_IfThenElse* BranchNode = CreateMarkerNode<UK2Node_IfThenElse>(KeyGraph,
					[](UK2Node_IfThenElse*) {});
				Layout.PlaceExec(BranchNode);
				if (UEdGraphPin* ConditionPin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input))
				{
					if (UEdGraphPin* EqualsPin = KeyEqualsNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
					{
						EqualsPin->MakeLinkTo(ConditionPin);
					}
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

	// The class must be focusable for key events to reach the override at all; written on the class
	// defaults CDO before the final compile so it serializes (same pattern as the design preview
	// size). SetIsFocusable only flips the flag here: a CDO has no cached Slate widget to invalidate.
	if (bKeyboardWired && WBP->GeneratedClass)
	{
		if (UUserWidget* WidgetCDO = WBP->GeneratedClass->GetDefaultObject<UUserWidget>())
		{
			WidgetCDO->SetIsFocusable(true);
		}
	}

	if (WiredCount > 0 || bKeyboardWired)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	}
}

} // namespace Design2UMGPrototypeGraphWiring
