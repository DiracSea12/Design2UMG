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
#include "K2Node_EnumEquality.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetInputLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{

const TCHAR NodeMarkerComment[] = TEXT("Design2UMG Prototype Navigation");
const TCHAR NavHistoryVariableName[] = TEXT("__d2u_nav_history");

void ClearGeneratedBindings(UWidgetBlueprint* WBP, TArray<FString>* OutPreservedMixedGraphs)
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
	// which return FEventReply and therefore cannot be ubergraph event nodes). A function graph whose
	// nodes are ALL marker nodes is plugin-generated content and is removed whole. A graph that also
	// contains unmarked nodes is user-modified: preserve it as a whole and let WireSimplePrototypeBindings
	// skip regenerating that event, so user logic can never be deleted by a reimport.
	TArray<UEdGraph*> GeneratedGraphs;
	for (UEdGraph* FunctionGraph : WBP->FunctionGraphs)
	{
		if (!FunctionGraph)
		{
			continue;
		}
		bool bHasMarkerNode = false;
		bool bHasUnmarkedNode = false;
		for (const UEdGraphNode* Node : FunctionGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			if (Node->NodeComment.Equals(NodeMarkerComment, ESearchCase::CaseSensitive))
			{
				bHasMarkerNode = true;
			}
			else
			{
				bHasUnmarkedNode = true;
			}
			if (bHasMarkerNode && bHasUnmarkedNode)
			{
				break;
			}
		}
		if (bHasMarkerNode && !bHasUnmarkedNode)
		{
			GeneratedGraphs.Add(FunctionGraph);
		}
		else if (bHasMarkerNode && bHasUnmarkedNode)
		{
			if (OutPreservedMixedGraphs)
			{
				OutPreservedMixedGraphs->Add(FunctionGraph->GetName());
			}
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Prototype graph '%s' contains user-authored nodes; preserving the whole graph and skipping keyboard regeneration for it"),
				*FunctionGraph->GetName());
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
		UFunction* StopAnimation = nullptr;
		UFunction* Delay = nullptr;
		UFunction* SetVisibility = nullptr;
		UFunction* GetVisibility = nullptr;
		UFunction* Not_PreBool = nullptr;
		UFunction* SetRenderTranslation = nullptr;
		UFunction* MakeVector2D = nullptr;
		UFunction* ArrayAdd = nullptr;
		UFunction* ArrayIsEmpty = nullptr;
		UFunction* ArrayLastIndex = nullptr;
		UFunction* ArrayGet = nullptr;
		UFunction* ArrayRemove = nullptr;
		UFunction* GetKeyEventKey = nullptr;
		UFunction* EqualKeyKey = nullptr;
		UFunction* EqualEqual_IntInt = nullptr;
		UFunction* BooleanAND = nullptr;
		UFunction* BooleanOR = nullptr;
		UFunction* UnhandledReply = nullptr;

		FEngineFunctions()
		{
			SetActiveWidgetIndex = UWidgetSwitcher::StaticClass()->FindFunctionByName(TEXT("SetActiveWidgetIndex"));
			GetActiveWidgetIndex = UWidgetSwitcher::StaticClass()->FindFunctionByName(TEXT("GetActiveWidgetIndex"));
			LaunchURL = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("LaunchURL"));
			PlayAnimation = UUserWidget::StaticClass()->FindFunctionByName(TEXT("PlayAnimation"));
			StopAnimation = UUserWidget::StaticClass()->FindFunctionByName(TEXT("StopAnimation"));
			Delay = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("Delay"));
			SetVisibility = UWidget::StaticClass()->FindFunctionByName(TEXT("SetVisibility"));
			GetVisibility = UWidget::StaticClass()->FindFunctionByName(TEXT("GetVisibility"));
			Not_PreBool = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Not_PreBool"));
			SetRenderTranslation = UWidget::StaticClass()->FindFunctionByName(TEXT("SetRenderTranslation"));
			MakeVector2D = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("MakeVector2D"));
			ArrayAdd = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_Add"));
			ArrayIsEmpty = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_IsEmpty"));
			ArrayLastIndex = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_LastIndex"));
			ArrayGet = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_Get"));
			ArrayRemove = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_Remove"));
			GetKeyEventKey = UKismetInputLibrary::StaticClass()->FindFunctionByName(TEXT("GetKey"));
			EqualKeyKey = UKismetInputLibrary::StaticClass()->FindFunctionByName(TEXT("EqualEqual_KeyKey"));
			EqualEqual_IntInt = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("EqualEqual_IntInt"));
			BooleanAND = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("BooleanAND"));
			BooleanOR = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("BooleanOR"));
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
			if (Child && FUMGWidgetUtils::MatchesWidgetBaseName(Child->GetName(), PageBaseName))
			{
				++MatchCount;
				Matched = Child;
			}
		}
		return MatchCount == 1 ? Matched : nullptr;
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
		/** Overlay bindings act on the target page BuildRoot parented under the overlay host. */
		FName OverlayPageWidgetName;
		/** Overlay-mirror twin of the trigger (dual-target/initial overlay pages only): the visible
		 *  button inside the open overlay gets its own bound-event chain; NAME_None when the page
		 *  has no mirror or the twin is not authorable. */
		FName MirrorTriggerWidgetName;
		/** Source-page context for keyboard gating: empty for legacy/manual data (no gate). */
		FString SourcePageId;
		/** Index of the source page among the switcher children when it is a switcher child. */
		int32 SourceSwitcherPageIndex = INDEX_NONE;
		/** Hosted page/mirror widget name under the overlay host when the source page is displayed there. */
		FName SourceHostedPageWidgetName;
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
		else if (Step.Kind == EUMGPrototypeNavigationStepKind::Overlay)
		{
			// BuildRoot parents every overlay-target page under the host at build time: overlay-only
			// pages by their base name, dual-target/initial pages as a mirror copy named by the base
			// name + OverlayMirrorSuffix (deterministic by construction). Try the base name first,
			// then the mirrored name; the suffix is non-numeric, so the two lookups cannot both hit,
			// and FindOverlayHostPage requires exactly one match either way.
			const FString PageBaseName = FUMGWidgetUtils::SanitizeWidgetName(
				Step.TargetPageId, TEXT("CanvasPanel")).ToString();
			const UWidget* OverlayPage = FindOverlayHostPage(OverlayHost, PageBaseName);
			if (!OverlayPage)
			{
				OverlayPage = FindOverlayHostPage(OverlayHost,
					FUMGWidgetUtils::SanitizeWidgetName(
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

			// Mirror-trigger resolution: when the trigger lives on a dual-target/initial overlay page,
			// BuildRoot also synthesized a mirror copy of that page under the overlay host, and while
			// the overlay is open the mirror's button is the VISIBLE one - it needs its own chain
			// (overlay-context semantics, authored in pass 3). The mirror is optional: no twin in the
			// tree (flow pages, overlay-only pages) or an unusable twin (no class property, or a
			// non-generated event already bound to it - user content, never masked) leaves the
			// original binding exactly as it is today.
			UButton* MirrorTriggerButton = FUMGWidgetUtils::FindUniqueTreeWidget<UButton>(WBP->WidgetTree,
				FUMGWidgetUtils::SanitizeWidgetName(
					Binding.TriggerWidgetId + Design2UMGPrototypeTokens::OverlayMirrorSuffix(),
					TEXT("Button")).ToString());
			if (MirrorTriggerButton
				&& FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, MirrorTriggerButton->GetFName())
				&& !FKismetEditorUtilities::FindBoundEventForComponent(WBP, Resolved.DelegateName, MirrorTriggerButton->GetFName()))
			{
				Resolved.MirrorTriggerWidgetName = MirrorTriggerButton->GetFName();
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
			// Keyboard: the key itself is the trigger, so there is no button to resolve. Resolve the
			// owning page now so each generated key branch can be gated to that page. A page that is
			// in the switcher is matched by active index; a page shown under the overlay host (plain
			// overlay target or the dual-target/initial mirror copy) is matched by its visibility.
			Resolved.SourcePageId = Binding.SourcePageId;
			if (!Binding.SourcePageId.IsEmpty())
			{
				const FString SourcePageBaseName = FUMGWidgetUtils::SanitizeWidgetName(
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
							FUMGWidgetUtils::SanitizeWidgetName(
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

	// Pass 2 - structural preparation, all in one place because each mutation needs the same
	// skeleton refresh (which recreates the class properties, so it must precede pass 3's caching):
	//   1. The overlay host and its hosted pages must be exposed as variables: UOverlay and the
	//      hosted page types (e.g. UCanvasPanel) default bIsVariable=false (unlike
	//      UButton/UWidgetSwitcher), so synthesized hosts/pages never become class properties
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

	// Keyboard wiring: one generated function-graph override per event kind. OnKeyDown/OnKeyUp are
	// BlueprintImplementableEvents returning FEventReply, so they cannot live in the ubergraph; the
	// engine-standard encoding is an override function graph (the editor's own path for such
	// functions), with one key-equality Branch per (binding, key code) guarding the step chain.
	/**
	 * Pure boolean used as a keyboard branch guard: whether the binding source page is
	 * currently the active switcher page and/or a visible overlay-hosted page. Switcher pages
	 * use the active index (visibility alone cannot distinguish hidden switcher children);
	 * overlay-only pages and dual-target page mirrors use the hosted page's own visibility.
	 * OutConditionPin stays null for a legitimately page-less (single-page/legacy) binding.
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
			// Legacy/manual data with a source page but neither tree home: only a true
			// single-page blueprint (no switcher, no overlay host) may run ungated.
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
