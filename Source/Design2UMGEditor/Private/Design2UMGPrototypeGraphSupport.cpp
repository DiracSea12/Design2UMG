// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "Design2UMGLog.h"
#include "UMGSchemaSourceExtensions.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ContentWidget.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetInputLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{
void ConfigureNativeFunctionOwner(UEdGraphNode* Node)
{
	if (auto* Call = Cast<UK2Node_CallFunction>(Node))
	{
		if (UFunction* Function = Call->GetTargetFunction())
		{
			UClass* Owner = Function->GetOwnerClass();
			if (Owner->HasAnyClassFlags(CLASS_Native))
			{
				// Resolving inherited native calls through self repeatedly searches the Blueprint's event graph for a function GUID during package reload.
				Call->FunctionReference.SetExternalMember(Function->GetFName(), Owner);
			}
		}
	}
}

FEngineFunctions::FEngineFunctions()
{
	GetCachedGeometry = UWidget::StaticClass()->FindFunctionByName(TEXT("GetCachedGeometry"));
	GetMousePositionOnPlatform = UWidgetLayoutLibrary::StaticClass()->FindFunctionByName(TEXT("GetMousePositionOnPlatform"));
	IsUnderLocation = USlateBlueprintLibrary::StaticClass()->FindFunctionByName(TEXT("IsUnderLocation"));
	GetRenderOpacity = UWidget::StaticClass()->FindFunctionByName(TEXT("GetRenderOpacity"));
	SetRenderOpacity = UWidget::StaticClass()->FindFunctionByName(TEXT("SetRenderOpacity"));
	LaunchURL = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("LaunchURL"));
	PlayAnimation = UUserWidget::StaticClass()->FindFunctionByName(TEXT("PlayAnimation"));
	PlayAnimationForward = UUserWidget::StaticClass()->FindFunctionByName(TEXT("PlayAnimationForward"));
	PlayAnimationReverse = UUserWidget::StaticClass()->FindFunctionByName(TEXT("PlayAnimationReverse"));
	StopAnimation = UUserWidget::StaticClass()->FindFunctionByName(TEXT("StopAnimation"));
	SetAnimationCurrentTime = UUserWidget::StaticClass()->FindFunctionByName(TEXT("SetAnimationCurrentTime"));
	FlushAnimations = UUserWidget::StaticClass()->FindFunctionByName(TEXT("FlushAnimations"));
	Delay = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("Delay"));
	SetTimer = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("K2_SetTimer"));
	SetTimerForNextTick = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("K2_SetTimerForNextTick"));
	ClearTimer = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("K2_ClearTimer"));
	SetVisibility = UWidget::StaticClass()->FindFunctionByName(TEXT("SetVisibility"));
	GetVisibility = UWidget::StaticClass()->FindFunctionByName(TEXT("GetVisibility"));
	SetContent = UContentWidget::StaticClass()->FindFunctionByName(TEXT("SetContent"));
	GetContent = UContentWidget::StaticClass()->FindFunctionByName(TEXT("GetContent"));
	GetLocalSize = USlateBlueprintLibrary::StaticClass()->FindFunctionByName(TEXT("GetLocalSize"));
	BreakVector2D = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("BreakVector2D"));
	SetWidthOverride = USizeBox::StaticClass()->FindFunctionByName(TEXT("SetWidthOverride"));
	SetHeightOverride = USizeBox::StaticClass()->FindFunctionByName(TEXT("SetHeightOverride"));
	ClearWidthOverride = USizeBox::StaticClass()->FindFunctionByName(TEXT("ClearWidthOverride"));
	ClearHeightOverride = USizeBox::StaticClass()->FindFunctionByName(TEXT("ClearHeightOverride"));
	EqualEqual_ObjectObject = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("EqualEqual_ObjectObject"));
	Not_PreBool = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Not_PreBool"));
	SetRenderTranslation = UWidget::StaticClass()->FindFunctionByName(TEXT("SetRenderTranslation"));
	MakeVector2D = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("MakeVector2D"));
	ArrayAdd = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_Add"));
	ArrayClear = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_Clear"));
	ArrayRemoveItem = UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_RemoveItem"));
	SlotAsCanvasSlot = UWidgetLayoutLibrary::StaticClass()->FindFunctionByName(TEXT("SlotAsCanvasSlot"));
	SetCanvasZOrder = UCanvasPanelSlot::StaticClass()->FindFunctionByName(TEXT("SetZOrder"));
	AddInt = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Add_IntInt"));
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
 * Use UK2Node_CallArrayFunction and schema connections to resolve wildcard array pins.
 * Raw MakeLinkTo leaves their types unresolved and breaks Blueprint compilation.
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

UEdGraph* FindOwnedFunctionGraph(UWidgetBlueprint* WBP, FName Name)
{
	for (UEdGraph* Graph : WBP->FunctionGraphs)
	{
		if (Graph->GetFName() != Name || Graph->Nodes.IsEmpty()) continue;
		for (const UEdGraphNode* Node : Graph->Nodes)
			if (Node->NodeComment != NodeMarkerComment) return nullptr;
		return Graph;
	}
	return nullptr;
}

UK2Node_FunctionEntry* FindOwnedFunctionEntry(UWidgetBlueprint* WBP, FName Name)
{
    if (UEdGraph* Graph = FindOwnedFunctionGraph(WBP, Name))
        for (UEdGraphNode* Node : Graph->Nodes)
            if (auto* Entry = Cast<UK2Node_FunctionEntry>(Node)) return Entry;
    return nullptr;
}

FName BindingFunctionName(const TCHAR* Role, int32 Index)
{
    return FName(*FString::Printf(TEXT("__d2u_%s_%03d"), Role, Index));
}

void AppendOwnedFunctionCall(UEdGraph* Graph, UWidgetBlueprint* WBP, FChainLayout& Layout, FName FunctionName)
{
	UEdGraph* FunctionGraph = FindOwnedFunctionGraph(WBP, FunctionName);
	UFunction* Function = FunctionGraph ? WBP->SkeletonGeneratedClass->FindFunctionByName(FunctionName) : nullptr;
	if (!Function) return;
	auto* Call = CreateMarkerNode<UK2Node_CallFunction>(Graph, [&](UK2Node_CallFunction* Node)
	{
		Node->SetFromFunction(Function);
		Node->FunctionReference.SetSelfMember(FunctionName, FunctionGraph->GraphGuid);
	});
	Layout.PlaceExec(Call);
}

void AppendPageOpenCall(UEdGraph* Graph, UWidgetBlueprint* WBP, FChainLayout& Layout)
{
	AppendOwnedFunctionCall(Graph, WBP, Layout, PageOpenEventName);
}

void AppendAnimationCleanupCall(UEdGraph* Graph, UWidgetBlueprint* WBP, FChainLayout& Layout)
{
	AppendOwnedFunctionCall(Graph, WBP, Layout, AnimationCleanupEventName);
}

bool AppendPageSelection(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
	const FBindingGraphTargets& Targets, int32 TargetPageIndex, FChainLayout& Layout)
{
	if (!Targets.CurrentPageProperty || !Functions.SetVisibility || !Targets.PageHostPages.IsValidIndex(TargetPageIndex)
		|| Targets.PageDisplayVisibility.Num() != Targets.PageHostPages.Num()) return false;
	for (int32 Index = 0; Index < Targets.PageHostPages.Num(); ++Index)
	{
		UWidget* Page = Targets.PageHostPages[Index];
		FObjectProperty* Property = Page ? FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Page->GetFName()) : nullptr;
		if (!Property) return false;
		auto* PageGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, Property));
		auto* SetVisibility = CreateMarkerNode<UK2Node_CallFunction>(Graph, [&](UK2Node_CallFunction* Node) { Node->SetFromFunction(Functions.SetVisibility); });
		Layout.PlaceExec(SetVisibility);
		PageGet->GetValuePin()->MakeLinkTo(SetVisibility->FindPinChecked(UEdGraphSchema_K2::PN_Self));
		SetVisibility->FindPinChecked(TEXT("InVisibility"))->DefaultValue = Index == TargetPageIndex
			? Targets.PageDisplayVisibility[Index] : TEXT("Collapsed");
	}
	auto* SetPage = CreateMarkerNode<UK2Node_VariableSet>(Graph, [&](UK2Node_VariableSet* Node)
	{
		Node->SetFromProperty(Targets.CurrentPageProperty, true, WBP->SkeletonGeneratedClass);
	});
	Layout.PlaceExec(SetPage);
	SetPage->FindPinChecked(SetPage->GetVarName(), EGPD_Input)->DefaultValue = FString::FromInt(TargetPageIndex);
	return true;
}

bool AppendNavigateTail(
	UEdGraph* Graph,
	UWidgetBlueprint* WBP,
	const FEngineFunctions& Functions,
	const FBindingGraphTargets& Targets,
	int32 TargetPageIndex,
	FChainLayout& Layout)
{
	if (!Functions.ArrayAdd || !Targets.CurrentPageProperty || !Targets.HistoryProperty)
	{
		return false;
	}
	AppendAnimationCleanupCall(Graph, WBP, Layout);

	UK2Node_VariableGet* CurrentPage = Layout.PlacePure(CreateVariableGet(Graph, WBP, Targets.CurrentPageProperty));
	UK2Node_VariableGet* HistoryGet = Layout.PlacePure(CreateVariableGet(Graph, WBP, Targets.HistoryProperty));
	UK2Node_CallArrayFunction* PushNode = CreateArrayCallNode(Graph, Functions.ArrayAdd);
	Layout.PlaceExec(PushNode);
	if (UEdGraphPin* TargetArrayPin = PushNode->FindPin(TEXT("TargetArray"), EGPD_Input))
	{
		LinkArrayPin(Graph, HistoryGet->GetValuePin(), TargetArrayPin);
	}
	if (UEdGraphPin* NewItemPin = PushNode->FindPin(TEXT("NewItem"), EGPD_Input))
	{
		CurrentPage->GetValuePin()->MakeLinkTo(NewItemPin);
	}
	if (!AppendPageSelection(Graph, WBP, Functions, Targets, TargetPageIndex, Layout)) return false;
	AppendPageOpenCall(Graph, WBP, Layout);
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
