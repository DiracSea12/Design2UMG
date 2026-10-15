// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeGraphWiring.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "Design2UMGLog.h"
#include "UMGSchemaSourceExtensions.h"
#include "Blueprint/UserWidget.h"
#include "Kismet2/BlueprintEditorUtils.h"
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

	FBindingPlan Plan = ResolveBindingPlan(WBP, NavigationData, Functions, OutWarnings);
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
