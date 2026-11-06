// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeGraphWiring.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "Design2UMGLog.h"
#include "UMGSchemaSourceExtensions.h"
#include "Blueprint/UserWidget.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "WidgetBlueprint.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "UObject/UnrealType.h"

namespace Design2UMGPrototypeGraphWiring
{
const TCHAR NodeMarkerComment[] = TEXT("Design2UMG Prototype Navigation");
const TCHAR NavHistoryVariableName[] = TEXT("__d2u_nav_history");
const TCHAR PageOpenEventName[] = TEXT("__d2u_page_open");
const TCHAR AnimationCleanupEventName[] = TEXT("__d2u_cancel_animation_actions");

void ClearGeneratedBindings(UWidgetBlueprint* WBP, TArray<FString>* OutPreservedMixedGraphs)
{
	if (!WBP)
	{
		return;
	}

	const FString LifecycleMarker = FString(NodeMarkerComment) + TEXT("|LifecycleBridge");
	const auto IsGenerated = [&](const UEdGraphNode* Node)
	{
		return Node && (Node->NodeComment == NodeMarkerComment || Node->NodeComment == LifecycleMarker
			|| Node->NodeComment.StartsWith(FString(NodeMarkerComment) + TEXT("|Preset|")));
	};
	TArray<FString> MixedGraphs;
	for (UEdGraph* Graph : WBP->UbergraphPages)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!IsGenerated(Node)) continue;
			auto* Bridge = Node->NodeComment == LifecycleMarker ? Cast<UK2Node_ExecutionSequence>(Node) : nullptr;
			auto* Input = Bridge ? Bridge->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
			auto* Prior = Bridge ? Bridge->GetThenPinGivenIndex(0) : nullptr;
			auto* Action = Bridge ? Bridge->GetThenPinGivenIndex(1) : nullptr;
			bool bSafeBridge = Input && Prior && Action && Input->LinkedTo.Num() == 1 && Prior->LinkedTo.Num() <= 1
				&& Action->LinkedTo.Num() == 1 && Action->LinkedTo[0]->GetOwningNode()->NodeComment == NodeMarkerComment;
			if (bSafeBridge && !Prior->LinkedTo.IsEmpty()
				&& IsGenerated(Input->LinkedTo[0]->GetOwningNode()) != IsGenerated(Prior->LinkedTo[0]->GetOwningNode())) bSafeBridge = false;
			if (Bridge) for (const UEdGraphPin* Pin : Bridge->Pins)
				if (Pin && Pin != Input && Pin != Prior && Pin != Action && !Pin->LinkedTo.IsEmpty()) bSafeBridge = false;
			if (Bridge && !bSafeBridge) MixedGraphs.AddUnique(Graph->GetName());
			for (const UEdGraphPin* Pin : Node->Pins)
				if (Pin) for (const UEdGraphPin* Link : Pin->LinkedTo)
					if (Link && !IsGenerated(Link->GetOwningNode()) && !(bSafeBridge && (Pin == Input || Pin == Prior)))
						MixedGraphs.AddUnique(Graph->GetName());
		}
	}
	for (const UEdGraph* Graph : WBP->FunctionGraphs)
	{
		if (!Graph) continue;
		bool bGenerated = false, bOther = false;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			bGenerated |= Node->NodeComment == NodeMarkerComment;
			bOther |= Node->NodeComment != NodeMarkerComment;
		}
		if (bGenerated && bOther) MixedGraphs.AddUnique(Graph->GetName());
	}
	if (!MixedGraphs.IsEmpty())
	{
		if (OutPreservedMixedGraphs) OutPreservedMixedGraphs->Append(MixedGraphs);
		return;
	}

	for (UEdGraph* Ubergraph : WBP->UbergraphPages)
	{
		if (!Ubergraph)
		{
			continue;
		}

		TArray<UEdGraphNode*> RestoredBridges;
		for (UEdGraphNode* Node : Ubergraph->Nodes)
		{
			auto* Sequence = Cast<UK2Node_ExecutionSequence>(Node);
			if (!Sequence || Sequence->NodeComment != LifecycleMarker) continue;
			auto* Input = Sequence->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			auto* Original = Sequence->GetThenPinGivenIndex(0);
			auto* Generated = Sequence->GetThenPinGivenIndex(1);
			if (!Input || !Original || !Generated || Input->LinkedTo.Num() != 1
				|| Original->LinkedTo.Num() > 1 || Generated->LinkedTo.Num() != 1
				|| Generated->LinkedTo[0]->GetOwningNode()->NodeComment != NodeMarkerComment) continue;
			bool bExtraConnection = false;
			for (const UEdGraphPin* Pin : Sequence->Pins)
				if (Pin && Pin != Input && Pin != Original && Pin != Generated && !Pin->LinkedTo.IsEmpty()) bExtraConnection = true;
			if (bExtraConnection) continue;
			UEdGraphPin* Source = Input->LinkedTo[0];
			UEdGraphPin* Prior = Original->LinkedTo.IsEmpty() ? nullptr : Original->LinkedTo[0];
			Source->BreakLinkTo(Input);
			if (Prior) { Original->BreakLinkTo(Prior); Source->MakeLinkTo(Prior); }
			RestoredBridges.Add(Sequence);
		}
		for (UEdGraphNode* Node : RestoredBridges) FBlueprintEditorUtils::RemoveNode(WBP, Node, true);
		TArray<UEdGraphNode*> NodesToRemove;
		TSet<UEdGraphNode*> Protected;
		const auto IsOwned = [](const UEdGraphNode* Node)
		{
			return Node && (Node->NodeComment.Equals(NodeMarkerComment, ESearchCase::CaseSensitive)
				|| Node->NodeComment.StartsWith(FString(NodeMarkerComment) + TEXT("|Preset|"))
				|| Node->NodeComment == FString(NodeMarkerComment) + TEXT("|LifecycleBridge"));
		};
		for (UEdGraphNode* Node : Ubergraph->Nodes)
		{
			if (!IsOwned(Node)) continue;
			for (UEdGraphPin* Pin : Node->Pins)
				if (Pin) for (UEdGraphPin* Link : Pin->LinkedTo)
					if (Link && !IsOwned(Link->GetOwningNode())) Protected.Add(Node);
		}
		TArray<UEdGraphNode*> Pending = Protected.Array();
		while (!Pending.IsEmpty())
		{
			UEdGraphNode* Node = Pending.Pop();
			for (UEdGraphPin* Pin : Node->Pins)
				if (Pin) for (UEdGraphPin* Link : Pin->LinkedTo)
				{
					UEdGraphNode* Other = Link ? Link->GetOwningNode() : nullptr;
					if (IsOwned(Other) && !Protected.Contains(Other)) { Protected.Add(Other); Pending.Add(Other); }
				}
		}
		if (!Protected.IsEmpty())
		{
			if (OutPreservedMixedGraphs) OutPreservedMixedGraphs->AddUnique(Ubergraph->GetName());
			continue;
		}
		for (UEdGraphNode* Node : Ubergraph->Nodes)
		{
			if (IsOwned(Node) && !Protected.Contains(Node))
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

	// Keyboard overrides and synchronous animation lifecycle functions use generated function graphs. A graph whose
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

void WireSimplePrototypeBindings(
	UWidgetBlueprint* WBP,
	const FUMGPrototypeNavigationData& NavigationData,
	TArray<FString>& OutWarnings)
{
	if (!WBP || !WBP->WidgetTree)
	{
		return;
	}

	// Preserve mixed user/generated graphs rather than authoring duplicate bindings beside them.
	TArray<FString> PreservedGraphs;
	ClearGeneratedBindings(WBP, &PreservedGraphs);
	if (!PreservedGraphs.IsEmpty())
	{
		OutWarnings.Add(FString::Printf(TEXT("Prototype wiring was preserved because user connections exist in graph(s): %s"),
			*FString::Join(PreservedGraphs, TEXT(", "))));
		return;
	}

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

	const bool bNeedsAnimationLifecycle = NavigationData.ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
	{
		return (!Binding.PrimaryStep().Transition.WidgetAnimationName.IsEmpty() && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty())
			|| Binding.UsesPageOpenTrigger()
			|| ((Binding.UsesHoverTrigger() || Binding.UsesPressTrigger()) && Binding.Steps.ContainsByPredicate(
				[](const FUMGPrototypeNavigationStep& Step) { return Step.Kind == EUMGPrototypeNavigationStepKind::Navigate; }))
			|| ((Binding.UsesHoverTrigger() || Binding.UsesPressTrigger()) && !Binding.PrimaryStep().Transition.StateHostWidgetId.IsEmpty())
			|| ((Binding.UsesHoverTrigger() || Binding.UsesPressTrigger()) && Binding.HasOverlayAction());
	});
	// Refresh after cleanup so removed generated functions no longer reserve their names.
	if (bNeedsAnimationLifecycle) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	const FEngineFunctions Functions;
	FBindingPlan Plan = ResolveBindingPlan(WBP, NavigationData, Functions, OutWarnings);
	bool bPageOpenDispatcher = false;
	if (!Plan.PageOpenBindings.IsEmpty() || Plan.MouseBindings.ContainsByPredicate([](const FResolvedBinding& Binding)
	{
		return Binding.WidgetAnimationName != NAME_None || Binding.HoverMonitorEventName != NAME_None || Binding.bPressOverlay || Binding.bTemporaryState || Binding.bTemporaryNavigate;
	}))
	{
		bool bConflict = false;
		for (const FName DispatcherName : {FName(PageOpenEventName), FName(AnimationCleanupEventName)})
		{
			bConflict |= WBP->SkeletonGeneratedClass->FindFunctionByName(DispatcherName)
				|| FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, DispatcherName);
		}
		for (UEdGraph* EventGraph : WBP->UbergraphPages)
		{
			if (!EventGraph) continue;
			for (UEdGraphNode* Node : EventGraph->Nodes)
			{
				const auto* Lifecycle = Cast<UK2Node_Event>(Node);
				bConflict |= Lifecycle && (Lifecycle->EventReference.GetMemberName() == TEXT("Construct")
					|| Lifecycle->EventReference.GetMemberName() == TEXT("Destruct"))
					&& Lifecycle->FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo.Num() > 1;
			}
		}
		if (bConflict)
		{
			SkipBinding(OutWarnings, TEXT("Property animation lifecycle conflicts with user content; property animation actions not wired"));
		}
		else
		{
			for (const FName Name : {FName(PageOpenEventName), FName(AnimationCleanupEventName)})
			{
				UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(WBP, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
				FBlueprintEditorUtils::AddFunctionGraph<UClass>(WBP, Graph, true, nullptr);
				for (UEdGraphNode* Node : Graph->Nodes) Node->NodeComment = NodeMarkerComment;
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
			bPageOpenDispatcher = true;
		}
	}
	if (!bPageOpenDispatcher)
	{
		Plan.PageOpenBindings.Reset();
		Plan.MouseBindings.RemoveAll([](const FResolvedBinding& Resolved)
		{
			return Resolved.WidgetAnimationName != NAME_None || Resolved.HoverMonitorEventName != NAME_None || Resolved.bPressOverlay || Resolved.bTemporaryState || Resolved.bTemporaryNavigate;
		});
	}
	const FBindingGraphTargets Targets = PrepareBindingGraphTargets(WBP, NavigationData, Plan, OutWarnings);
	const int32 WiredCount = WireMouseBindings(WBP, Ubergraph, NavigationData, Functions, Plan, Targets, OutWarnings);
	const bool bKeyboardWired = WireKeyboardBindings(WBP, NavigationData, Functions, Plan, Targets, OutWarnings);

	// The class must be focusable for key events to reach the override at all; written on the class
	// defaults CDO before the final compile so it serializes (same pattern as the design preview
	// size). SetIsFocusable only flips the flag here: a CDO has no cached Slate widget to invalidate.
	if (bKeyboardWired && WBP->GeneratedClass)
	{
		if (UUserWidget* WidgetCDO = WBP->GeneratedClass->GetDefaultObject<UUserWidget>())
		{
			Design2UMGWidgetPropertyAccess::SetUserWidgetFocusable(*WidgetCDO, true);
		}
	}

	if (WiredCount > 0 || bKeyboardWired)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	}
}
}
