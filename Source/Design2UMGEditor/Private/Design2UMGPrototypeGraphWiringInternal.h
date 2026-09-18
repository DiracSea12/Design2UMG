// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "Design2UMGPrototypeGraphWiring.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

class UOverlay;
class UWidget;
class UWidgetSwitcher;
class UK2Node_VariableGet;
class UK2Node_CallArrayFunction;
class FProperty;
class FObjectProperty;
class FArrayProperty;
struct FUMGPrototypeNavigationStep;

namespace Design2UMGPrototypeGraphWiring
{
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

struct FBindingPlan
{
	UWidgetSwitcher* Switcher = nullptr;
	UOverlay* OverlayHost = nullptr;
	TArray<FResolvedBinding> MouseBindings;
	TArray<FResolvedBinding> KeyboardBindings;
	FString SwitcherBaseName;
};

// Created only after all skeleton-refreshing mutations are complete.
struct FBindingGraphTargets
{
	FArrayProperty* HistoryProperty = nullptr;
	FObjectProperty* SwitcherProperty = nullptr;
	FObjectProperty* OverlayHostProperty = nullptr;
	TArray<UWidget*> OverlayHostPages;
};

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

	FEngineFunctions();
};

constexpr int32 NodeColumnStep = 320;
constexpr int32 NodeRowStep = 220;

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

void SkipBinding(TArray<FString>& OutWarnings, const FString& Reason);

bool LinkExecChain(const TArray<UEdGraphNode*>& ExecChain);

UK2Node_VariableGet* CreateVariableGet(UEdGraph* Graph, UWidgetBlueprint* WBP, const FProperty* Property);

UK2Node_CallArrayFunction* CreateArrayCallNode(UEdGraph* Graph, UFunction* Function);

bool LinkArrayPin(UEdGraph* Graph, UEdGraphPin* SourcePin, UEdGraphPin* TargetPin);

bool AppendNavigateTail(
	UEdGraph* Graph,
	UWidgetBlueprint* WBP,
	const FEngineFunctions& Functions,
	FObjectProperty* SwitcherProperty,
	FArrayProperty* HistoryProperty,
	int32 TargetPageIndex,
	FChainLayout& Layout);

bool AppendOpenLinkTail(
	UEdGraph* Graph,
	const FEngineFunctions& Functions,
	const FUMGPrototypeNavigationStep& Step,
	FChainLayout& Layout);

FBindingPlan ResolveBindingPlan(UWidgetBlueprint* WBP,
	const FUMGPrototypeNavigationData& NavigationData, const FEngineFunctions& Functions,
	TArray<FString>& OutWarnings);

FBindingGraphTargets PrepareBindingGraphTargets(UWidgetBlueprint* WBP,
	const FUMGPrototypeNavigationData& NavigationData, FBindingPlan& Plan,
	TArray<FString>& OutWarnings);

int32 WireMouseBindings(UWidgetBlueprint* WBP, UEdGraph* Ubergraph,
	const FUMGPrototypeNavigationData& NavigationData, const FEngineFunctions& Functions,
	const FBindingPlan& Plan, const FBindingGraphTargets& Targets, TArray<FString>& OutWarnings);

bool WireKeyboardBindings(UWidgetBlueprint* WBP,
	const FUMGPrototypeNavigationData& NavigationData, const FEngineFunctions& Functions,
	const FBindingPlan& Plan, const FBindingGraphTargets& Targets, TArray<FString>& OutWarnings);
}
