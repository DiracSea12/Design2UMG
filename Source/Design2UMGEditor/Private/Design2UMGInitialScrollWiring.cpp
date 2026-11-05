// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGInitialScrollWiring.h"
#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "Design2UMGWidgetMetadata.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ScrollBox.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "WidgetBlueprint.h"

namespace Design2UMGInitialScrollWiring
{
namespace
{
	const TCHAR* OffsetKey = TEXT("D2UInitialScrollOffset");

	// Distinct from the prototype marker: prototype rewiring neither removes nor bridges these nodes.
	const FString& Marker()
	{
		static const FString Value = FString(Design2UMGPrototypeGraphWiring::NodeMarkerComment) + TEXT("|InitialScroll");
		return Value;
	}

	template <typename NodeType>
	NodeType* Mark(NodeType* Node)
	{
		Node->NodeComment = Marker();
		return Node;
	}
}

void SetInitialOffset(UScrollBox* ScrollBox, float Offset)
{
	ScrollBox->bIsVariable = true;
	Design2UMGWidgetMetadata::PackageMetadata(ScrollBox).SetValue(ScrollBox, OffsetKey, *LexToString(Offset));
}

void Wire(UWidgetBlueprint* WBP)
{
	using namespace Design2UMGPrototypeGraphWiring;
	UEdGraph* Graph = WBP->UbergraphPages.IsEmpty() ? nullptr : WBP->UbergraphPages[0];
	if (!Graph) return;

	// Drop the previous generation; a user PreConstruct is spliced back onto its own chain.
	UK2Node_Event* PreConstruct = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
		if (auto* Event = Cast<UK2Node_Event>(Node); Event && Event->EventReference.GetMemberName() == TEXT("PreConstruct")) PreConstruct = Event;
	UEdGraphPin* UserChain = nullptr;
	if (PreConstruct && PreConstruct->NodeComment != Marker())
	{
		UEdGraphPin* Pin = PreConstruct->FindPinChecked(UEdGraphSchema_K2::PN_Then);
		while (Pin->LinkedTo.Num() == 1 && Pin->LinkedTo[0]->GetOwningNode()->NodeComment == Marker())
			Pin = Pin->LinkedTo[0]->GetOwningNode()->FindPinChecked(UEdGraphSchema_K2::PN_Then);
		UserChain = Pin->LinkedTo.IsEmpty() ? nullptr : Pin->LinkedTo[0];
	}
	for (UEdGraphNode* Node : TArray<UEdGraphNode*>(Graph->Nodes))
	{
		if (Node->NodeComment != Marker()) continue;
		if (Node == PreConstruct) PreConstruct = nullptr;
		FBlueprintEditorUtils::RemoveNode(WBP, Node, true);
	}

	TArray<TPair<const FObjectProperty*, FString>> Offsets;
	WBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		const FString Offset = Cast<UScrollBox>(Widget)
			? Design2UMGWidgetMetadata::PackageMetadata(Widget).GetValue(Widget, OffsetKey) : FString();
		if (!Offset.IsEmpty())
			Offsets.Emplace(FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Widget->GetFName()), Offset);
	});
	if (Offsets.IsEmpty())
	{
		if (UserChain) PreConstruct->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(UserChain);
		return;
	}

	if (!PreConstruct)
	{
		PreConstruct = Mark(CreateMarkerNode<UK2Node_Event>(Graph, [](UK2Node_Event* Node)
		{
			Node->EventReference.SetExternalMember(TEXT("PreConstruct"), UUserWidget::StaticClass());
			Node->bOverrideFunction = true;
		}));
	}
	UEdGraphPin* Then = PreConstruct->FindPinChecked(UEdGraphSchema_K2::PN_Then);
	Then->BreakAllPinLinks();
	FChainLayout Layout;
	Layout.RowY = -NodeRowStep * 6;
	UFunction* SetScrollOffset = UScrollBox::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UScrollBox, SetScrollOffset));
	for (const auto& [Property, Offset] : Offsets)
	{
		auto* Get = Mark(Layout.PlacePure(CreateVariableGet(Graph, WBP, Property)));
		auto* Set = Mark(CreateCallNode(Graph, SetScrollOffset));
		Layout.PlaceExec(Set);
		Get->GetValuePin()->MakeLinkTo(Set->FindPinChecked(UEdGraphSchema_K2::PN_Self));
		Set->FindPinChecked(TEXT("NewScrollOffset"))->DefaultValue = Offset;
		Then->MakeLinkTo(Set->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		Then = Set->FindPinChecked(UEdGraphSchema_K2::PN_Then);
	}
	if (UserChain) Then->MakeLinkTo(UserChain);
}
}
