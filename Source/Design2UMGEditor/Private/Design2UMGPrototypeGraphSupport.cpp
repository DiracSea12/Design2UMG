// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "Design2UMGLog.h"
#include "UMGSchemaSourceExtensions.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/WidgetSwitcher.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetInputLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{
FEngineFunctions::FEngineFunctions()
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
}
