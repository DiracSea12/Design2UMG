// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "Design2UMGPrototypeGraphWiring.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"

class UWidget;
class UCanvasPanel;
class UK2Node_VariableGet;
class UK2Node_FunctionEntry;
class UK2Node_CallArrayFunction;
class FProperty;
class FObjectProperty;
class FArrayProperty;
class FIntProperty;
class FFloatProperty;
struct FUMGPrototypeNavigationStep;
struct FUMGPrototypeNavigationBinding;

namespace Design2UMGPrototypeGraphWiring
{
struct FLivePresentationBinding
{
	FName WidgetName;
	FName OriginalSocketName;
	FName PresentationSocketName;
};

struct FResolvedBinding
{
	int32 BindingIndex = INDEX_NONE;
	FName DelegateName;
	FName TriggerWidgetName;
	/** Overlay bindings act on the target page BuildRoot parented under the overlay host. */
	FName OverlayPageWidgetName;
	int32 OverlayPageIndex = INDEX_NONE;
	bool bTemporaryNavigate = false;
	bool bDrag = false;
	int32 DragLeaderIndex = INDEX_NONE;
	FName DragAcceptedPropertyName;
	int32 TargetPageOriginalZ = 0;
	int32 TemporaryPageZ = 0;
	FName PlaybackTargetPropertyName;
	FName PlaybackStartPropertyName;
	TArray<FName> LayerWidgetNames;
	TArray<int32> LayerOriginalZOrders;
	/** Overlay-mirror twin of the trigger (dual-target/initial overlay pages only): the visible
	 *  button inside the open overlay gets its own bound-event chain; NAME_None when the page
	 *  has no mirror or the twin is not authorable. */
	FName MirrorTriggerWidgetName;
	/**
	 * Source page used for keyboard gating; empty means no page gate.
	 */
	FString SourcePageId;
	/** Index of the source page among the page host children. */
	int32 SourcePageIndex = INDEX_NONE;
	int32 SourceOverlayPageIndex = INDEX_NONE;
	float TriggerDelaySeconds = 0.0f;
	/** Hosted page/mirror widget name under the overlay host when the source page is displayed there. */
	FName SourceHostedPageWidgetName;
	int32 TargetPageIndex = INDEX_NONE;
	FName WidgetAnimationName;
	FName PlaybackEventName;
	FName NavigationEventName;
	FName HoverMonitorEventName;
	FName MirrorHoverMonitorEventName;
	bool bPressOverlay = false;
	bool bTemporaryState = false;
	int32 StateHostIndex = INDEX_NONE;
	int32 SourceStateIndex = INDEX_NONE;
	int32 TargetStateIndex = INDEX_NONE;
	FName StateCompletionEventName;
	FName LiveReturnEventName;
	FName LayoutOriginalWidgetName;
	FName LayoutPresentationWidgetName;
	float LayoutOriginalOpacity = 1.0f;
	TArray<FLivePresentationBinding> LivePresentations;
};

struct FBindingPlan
{
	UCanvasPanel* PageHost = nullptr;
	UCanvasPanel* OverlayHost = nullptr;
	TArray<FResolvedBinding> MouseBindings;
	TArray<FResolvedBinding> KeyboardBindings;
	FString PageHostBaseName;
	TArray<FResolvedBinding> PageOpenBindings;
};

// Created only after all skeleton-refreshing mutations are complete.
struct FStateHostGraphTargets
{
	FIntProperty* CurrentStateProperty = nullptr;
	FIntProperty* TransitionOwnerProperty = nullptr;
	FFloatProperty* SavedSourceOpacityProperty = nullptr;
	TArray<UWidget*> StateWidgets;
	TArray<UWidget*> OpacityWidgets;
	TArray<FString> StateVisibility;
};

struct FBindingGraphTargets
{
	FArrayProperty* HistoryProperty = nullptr;
	FIntProperty* CurrentPageProperty = nullptr;
	TArray<UWidget*> PageHostPages;
	TArray<FString> PageDisplayVisibility;
	FObjectProperty* OverlayHostProperty = nullptr;
	TArray<UWidget*> OverlayHostPages;
	FArrayProperty* OverlayStackProperty = nullptr;
	FIntProperty* OverlayZOrderProperty = nullptr;
	FIntProperty* OverlayOwnerProperty = nullptr;
	FIntProperty* TransitionOwnerProperty = nullptr;
	TArray<FStateHostGraphTargets> StateHosts;
	TArray<FResolvedBinding> OverlayPageOpenBindings;
};

/** Every engine function the wiring calls, resolved once per run. */
struct FEngineFunctions
{
	UFunction* LaunchURL = nullptr;
	UFunction* PlayAnimation = nullptr;
	UFunction* PlayAnimationForward = nullptr;
	UFunction* PlayAnimationReverse = nullptr;
	UFunction* StopAnimation = nullptr;
	UFunction* SetAnimationCurrentTime = nullptr;
	UFunction* FlushAnimations = nullptr;
	UFunction* Delay = nullptr;
	UFunction* SetTimer = nullptr;
	UFunction* SetTimerForNextTick = nullptr;
	UFunction* ClearTimer = nullptr;
	UFunction* SetVisibility = nullptr;
	UFunction* GetVisibility = nullptr;
	UFunction* SetContent = nullptr;
	UFunction* GetContent = nullptr;
	UFunction* GetLocalSize = nullptr;
	UFunction* BreakVector2D = nullptr;
	UFunction* SetWidthOverride = nullptr;
	UFunction* SetHeightOverride = nullptr;
	UFunction* ClearWidthOverride = nullptr;
	UFunction* ClearHeightOverride = nullptr;
	UFunction* EqualEqual_ObjectObject = nullptr;
	UFunction* GetCachedGeometry = nullptr;
	UFunction* GetMousePositionOnPlatform = nullptr;
	UFunction* IsUnderLocation = nullptr;
	UFunction* GetRenderOpacity = nullptr;
	UFunction* SetRenderOpacity = nullptr;
	UFunction* Not_PreBool = nullptr;
	UFunction* SetRenderTranslation = nullptr;
	UFunction* MakeVector2D = nullptr;
	UFunction* ArrayAdd = nullptr;
	UFunction* ArrayClear = nullptr;
	UFunction* ArrayRemoveItem = nullptr;
	UFunction* SlotAsCanvasSlot = nullptr;
	UFunction* SetCanvasZOrder = nullptr;
	UFunction* AddInt = nullptr;
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

void ConfigureNativeFunctionOwner(UEdGraphNode* Node);

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
	ConfigureNativeFunctionOwner(Node);
	Node->AllocateDefaultPins();
	Node->NodeComment = NodeMarkerComment;
	return Node;
}

inline UK2Node_CallFunction* CreateCallNode(UEdGraph* Graph, UFunction* Function)
{
	return CreateMarkerNode<UK2Node_CallFunction>(Graph, [Function](UK2Node_CallFunction* Node) { Node->SetFromFunction(Function); });
}

inline UK2Node_IfThenElse* CreateBranchNode(UEdGraph* Graph)
{
	return CreateMarkerNode<UK2Node_IfThenElse>(Graph, [](UK2Node_IfThenElse*) {});
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

bool WireAnimationTimeTarget(UEdGraph* Graph, UWidgetBlueprint* Blueprint, FObjectProperty* AnimationProperty,
	FFloatProperty* TargetProperty, FFloatProperty* StartProperty, UEdGraphPin* DynamicTarget,
	float TargetTime, float Speed, FChainLayout& Chain);

void SkipBinding(TArray<FString>& OutWarnings, const FString& Reason);

bool LinkExecChain(const TArray<UEdGraphNode*>& ExecChain);

UK2Node_VariableGet* CreateVariableGet(UEdGraph* Graph, UWidgetBlueprint* WBP, const FProperty* Property);

UK2Node_CallArrayFunction* CreateArrayCallNode(UEdGraph* Graph, UFunction* Function);

bool LinkArrayPin(UEdGraph* Graph, UEdGraphPin* SourcePin, UEdGraphPin* TargetPin);

extern const TCHAR PageOpenEventName[];
extern const TCHAR AnimationCleanupEventName[];
UEdGraph* FindOwnedFunctionGraph(UWidgetBlueprint* WBP, FName Name);
UK2Node_FunctionEntry* FindOwnedFunctionEntry(UWidgetBlueprint* WBP, FName Name);
FName BindingFunctionName(const TCHAR* Role, int32 Index);
TArray<FName> GetMouseFunctionNames(const FUMGPrototypeNavigationData& Navigation, const FBindingPlan& Plan);
void AppendOwnedFunctionCall(UEdGraph* Graph, UWidgetBlueprint* WBP, FChainLayout& Layout, FName FunctionName);
void AppendPageOpenCall(UEdGraph* Graph, UWidgetBlueprint* WBP, FChainLayout& Layout);
void AppendAnimationCleanupCall(UEdGraph* Graph, UWidgetBlueprint* WBP, FChainLayout& Layout);

bool AppendPageSelection(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
	const FBindingGraphTargets& Targets, int32 TargetPageIndex, FChainLayout& Layout);

void AppendLivePresentationCall(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
	const FResolvedBinding& Resolved, bool bEnter, FChainLayout& Layout, bool bRefreshTimeouts = true);

bool AppendPrototypeTemporaryPageVisibility(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
	const FBindingGraphTargets& Targets, const FResolvedBinding& Resolved, bool bShow, FChainLayout& Layout);
bool AppendPrototypeLayerOrders(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
	const FUMGPrototypeNavigationBinding& Binding, const FResolvedBinding& Resolved, bool bTarget, FChainLayout& Layout);
bool AppendCloseOverlayTail(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
	const FBindingGraphTargets& Targets, FChainLayout& Layout);

bool AppendNavigateTail(
	UEdGraph* Graph,
	UWidgetBlueprint* WBP,
	const FEngineFunctions& Functions,
	const FBindingGraphTargets& Targets,
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
