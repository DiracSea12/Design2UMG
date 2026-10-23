// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignBrowserWidget.h"

#include "Dom/JsonObject.h"
#include "Blueprint/WidgetTree.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetTextLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "WidgetBlueprint.h"
#include "DesignBrowserURL.h"
#include "Design2UMGLog.h"
#include "UMGSchemaTypedPayloads.h"
#include "UObject/UnrealType.h"
#include "WebBrowser.h"


namespace
{
    const FName ScrollVariableOwner(TEXT("D2U_BrowserInitialScroll"));

    // These nodes belong only to this browser. Other handlers for its native events stay intact.
    struct FBrowserScrollGraph
    {
        UEdGraph* Graph;
        FString Marker;
        bool bValid = true;
        int32 NextColumn = 0;
        int32 Row = 0;

        template <typename T, typename F>
        T* Add(F Initialize)
        {
            T* Node = NewObject<T>(Graph);
            Initialize(Node);
            Graph->AddNode(Node, false, false);
            Node->CreateNewGuid();
            Node->PostPlacedNewNode();
            Node->AllocateDefaultPins();
            Node->NodeComment = Marker;
            Node->NodePosX = NextColumn++ * 240;
            Node->NodePosY = Row;
            return Node;
        }
        UEdGraphPin* Pin(UEdGraphNode* Node, FName Name)
        {
            UEdGraphPin* Result = Node ? Node->FindPin(Name) : nullptr;
            if (!Result)
            {
                UE_LOG(LogDesign2UMG, Error, TEXT("Browser scroll graph '%s': node %s is missing pin '%s'"),
                    *Marker, *GetNameSafe(Node), *Name.ToString());
                bValid = false;
            }
            return Result;
        }
        void Connect(UEdGraphPin* From, UEdGraphPin* To)
        {
            if (!From || !To || !Graph->GetSchema()->TryCreateConnection(From, To))
            {
                UE_LOG(LogDesign2UMG, Error, TEXT("Browser scroll graph '%s': connection %s.%s -> %s.%s failed"),
                    *Marker, From ? *From->GetOwningNode()->GetName() : TEXT("<none>"), From ? *From->PinName.ToString() : TEXT("<none>"),
                    To ? *To->GetOwningNode()->GetName() : TEXT("<none>"), To ? *To->PinName.ToString() : TEXT("<none>"));
                bValid = false;
            }
        }
        void Default(UEdGraphPin* PinValue, const FString& Value)
        {
            if (PinValue) Graph->GetSchema()->TrySetDefaultValue(*PinValue, Value);
            else bValid = false;
        }
        UK2Node_CallFunction* Call(UClass* Owner, FName Name)
        {
            UFunction* Function = Owner->FindFunctionByName(Name);
            check(Function);
            return Add<UK2Node_CallFunction>([&](auto* Node) { Node->SetFromFunction(Function); });
        }
        UK2Node_ComponentBoundEvent* Event(FName WidgetName, FName DelegateName)
        {
            const auto* Delegate = FindFProperty<FMulticastDelegateProperty>(UWebBrowser::StaticClass(), DelegateName);
            check(Delegate);
            // The widget property is emitted by the normal final Blueprint compile, after tree construction.
            return Add<UK2Node_ComponentBoundEvent>([&](auto* Node)
            {
                Node->ComponentPropertyName = WidgetName;
                Node->DelegatePropertyName = DelegateName;
                Node->DelegateOwnerClass = UWebBrowser::StaticClass();
                Node->EventReference.SetFromField<UFunction>(Delegate->SignatureFunction, false);
                Node->CustomFunctionName = FName(*FString::Printf(TEXT("BndEvt__%s_%s"), *WidgetName.ToString(), *Node->GetName()));
                Node->bOverrideFunction = false;
                Node->bInternalEvent = true;
            });
        }
        // The complete widget tree has not reached its normal Skeleton refresh yet. Declare the
        // real source types here; native reconstruction/final compilation resolves and validates the members.
        UEdGraphPin* Get(FName Name, const FEdGraphPinType& SourceType)
        {
            auto* Node = Add<UK2Node_VariableGet>([&](auto* Value) { Value->VariableReference.SetSelfMember(Name); });
            if (!Node->FindPin(Name)) Node->CreatePin(EGPD_Output, SourceType, Name);
            return Pin(Node, Name);
        }
        UK2Node_VariableSet* Set(FName Name, const FEdGraphPinType& SourceType)
        {
            auto* Node = Add<UK2Node_VariableSet>([&](auto* Value) { Value->VariableReference.SetSelfMember(Name); });
            if (!Node->FindPin(Name)) Node->CreatePin(EGPD_Input, SourceType, Name);
            return Node;
        }
        UEdGraphPin* If(UEdGraphPin* Exec, UEdGraphPin* Condition, bool bThen)
        {
            auto* Branch = Add<UK2Node_IfThenElse>([](auto*) {});
            Connect(Exec, Branch->GetExecPin());
            Connect(Condition, Branch->GetConditionPin());
            return bThen ? Branch->GetThenPin() : Branch->GetElsePin();
        }
    };

    bool BindInitialScroll(UWebBrowser& Widget, const FUMGBrowserData& Data)
    {
        UWidgetBlueprint* Blueprint = Widget.GetTypedOuter<UWidgetBlueprint>();
        if (!Blueprint) return true; // A standalone template has no generated Blueprint execution graph.
        const FString Marker = TEXT("D2U Browser initial scroll: ") + Widget.GetName();
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            const auto Nodes = Graph->Nodes;
            for (UEdGraphNode* Node : Nodes)
            {
                if (Node->NodeComment == Marker) FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
            }
        }
        const FName Dispatched(*(TEXT("D2U_BrowserScrollDispatched_") + Widget.GetName()));
        const bool bRestore = Data.ScrollX.IsSet() || Data.ScrollY.IsSet();
        const int32 VariableIndex = Blueprint->NewVariables.IndexOfByPredicate(
            [&](const auto& Value) { return Value.VarName == Dispatched; });
        if (VariableIndex != INDEX_NONE)
        {
            const auto& Variable = Blueprint->NewVariables[VariableIndex];
            if (!Variable.HasMetaData(ScrollVariableOwner) || Variable.GetMetaData(ScrollVariableOwner) != Marker) return false;
        }
        if (!bRestore)
        {
            if (VariableIndex != INDEX_NONE) Blueprint->NewVariables.RemoveAt(VariableIndex);
            return true;
        }
        if (VariableIndex == INDEX_NONE)
        {
            if (FindFProperty<FProperty>(Blueprint->ParentClass, Dispatched)) return false;
            // AddMemberVariable compiles immediately. This tree is still being built; its normal save compiles it once complete.
            FBPVariableDescription Variable;
            Variable.VarName = Dispatched;
            Variable.VarGuid = FGuid::NewGuid();
            Variable.VarType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
            Variable.PropertyFlags = CPF_BlueprintVisible | CPF_Transient;
            Variable.DefaultValue = TEXT("false");
            Variable.SetMetaData(ScrollVariableOwner, Marker);
            Blueprint->NewVariables.Add(MoveTemp(Variable));
        }
        UEdGraph* Graph = Blueprint->UbergraphPages.IsEmpty() ? nullptr : Blueprint->UbergraphPages[0].Get();
        if (!Graph)
        {
            Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("EventGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            // AddUbergraphPage refreshes the Skeleton immediately; the normal save owns that refresh.
            Blueprint->UbergraphPages.Add(Graph);
        }
        const auto& StateType = Blueprint->NewVariables.FindByPredicate(
            [&](const auto& Value) { return Value.VarName == Dispatched; })->VarType;
        FEdGraphPinType BrowserType;
        BrowserType.PinCategory = UEdGraphSchema_K2::PC_Object;
        BrowserType.PinSubCategoryObject = Widget.GetClass();
        FBrowserScrollGraph Builder{Graph, Marker};
        for (const auto& Existing : Graph->Nodes) Builder.Row = FMath::Max(Builder.Row, Existing->NodePosY + 400);
        auto* Address = Builder.Event(Widget.GetFName(), GET_MEMBER_NAME_CHECKED(UWebBrowser, OnUrlChanged));
        auto* Text = Builder.Call(UKismetTextLibrary::StaticClass(), TEXT("Conv_TextToString"));
        Builder.Connect(Builder.Pin(Address, TEXT("Text")), Builder.Pin(Text, TEXT("InText")));
        UEdGraphPin* Exec = Builder.If(Builder.Pin(Address, UEdGraphSchema_K2::PN_Then), Builder.Get(Dispatched, StateType), false);
        auto SkipURL = [&](const FString& Ignored)
        {
            auto* Compare = Builder.Call(UKismetStringLibrary::StaticClass(), TEXT("NotEqual_StrStr"));
            Builder.Connect(Builder.Pin(Text, UEdGraphSchema_K2::PN_ReturnValue), Builder.Pin(Compare, TEXT("A")));
            Builder.Default(Builder.Pin(Compare, TEXT("B")), Ignored);
            Exec = Builder.If(Exec, Builder.Pin(Compare, UEdGraphSchema_K2::PN_ReturnValue), true);
        };
        SkipURL(TEXT(""));
        const FString InitialURL = DesignBrowserURL::Resolve(Data.InitialURL);
        if (!InitialURL.IsEmpty() && !InitialURL.Equals(TEXT("about:blank"), ESearchCase::IgnoreCase)) SkipURL(TEXT("about:blank"));
        auto* SetDispatched = Builder.Set(Dispatched, StateType);
        Builder.Default(Builder.Pin(SetDispatched, Dispatched), TEXT("true"));
        Builder.Connect(Exec, SetDispatched->GetExecPin());
        auto* Execute = Builder.Call(UWebBrowser::StaticClass(), TEXT("ExecuteJavascript"));
        Builder.Connect(SetDispatched->GetThenPin(), Builder.Pin(Execute, UEdGraphSchema_K2::PN_Execute));
        Builder.Connect(Builder.Get(Widget.GetFName(), BrowserType), Builder.Pin(Execute, UEdGraphSchema_K2::PN_Self));
        const FString X = Data.ScrollX.IsSet() ? FString::SanitizeFloat(Data.ScrollX.GetValue()) : TEXT("window.scrollX");
        const FString Y = Data.ScrollY.IsSet() ? FString::SanitizeFloat(Data.ScrollY.GetValue()) : TEXT("window.scrollY");
        const FString Script = FString::Printf(TEXT(R"JS((()=>{
let finished=false;
const cancel=()=>{finished=true;window.removeEventListener('load',restore);};
const restore=()=>requestAnimationFrame(()=>{
 if(finished)return;finished=true;window.removeEventListener('pagehide',cancel);
 window.scrollTo({left:%s,top:%s,behavior:'instant'});
});
window.addEventListener('pagehide',cancel,{once:true});
if(document.readyState==='complete')restore();else window.addEventListener('load',restore,{once:true});
})();)JS"), *X, *Y);
        Builder.Default(Builder.Pin(Execute, TEXT("ScriptText")), Script);

        return Builder.bValid;
    }
}

namespace DesignBrowserWidget
{
	bool ApplyTemplate(UWebBrowser& Widget, const FUMGBrowserData& Data)
	{
		const FString URL = DesignBrowserURL::Resolve(Data.InitialURL);
		FStrProperty* URLProperty = FindFProperty<FStrProperty>(UWebBrowser::StaticClass(), TEXT("InitialURL"));
		check(URLProperty);
		URLProperty->SetPropertyValue_InContainer(&Widget, URL);
		if (Data.bSupportsTransparency.IsSet())
		{
			FBoolProperty* TransparencyProperty = FindFProperty<FBoolProperty>(UWebBrowser::StaticClass(), TEXT("bSupportsTransparency"));
			check(TransparencyProperty);
			TransparencyProperty->SetPropertyValue_InContainer(&Widget, Data.bSupportsTransparency.GetValue());
		}
		return BindInitialScroll(Widget, Data);
	}

	TSharedPtr<FJsonObject> Serialize(const FUMGBrowserData& Data)
	{
		if (!Data.HasAny()) return nullptr;
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("InitialURL"), Data.InitialURL);
		if (!Data.DeviceId.IsEmpty()) Object->SetStringField(TEXT("DeviceId"), Data.DeviceId);
		if (Data.Zoom.IsSet()) Object->SetNumberField(TEXT("Zoom"), Data.Zoom.GetValue());
		if (Data.ScrollX.IsSet()) Object->SetNumberField(TEXT("ScrollX"), Data.ScrollX.GetValue());
		if (Data.ScrollY.IsSet()) Object->SetNumberField(TEXT("ScrollY"), Data.ScrollY.GetValue());
		if (Data.bSupportsTransparency.IsSet()) Object->SetBoolField(TEXT("SupportsTransparency"), Data.bSupportsTransparency.GetValue());
		return Object;
	}

	void Parse(const TSharedPtr<FJsonObject>& NodeObject, FUMGBrowserData& OutData)
	{
		const TSharedPtr<FJsonObject>* Object;
		if (!NodeObject->TryGetObjectField(TEXT("Browser"), Object)) return;
		(*Object)->TryGetStringField(TEXT("InitialURL"), OutData.InitialURL);
		(*Object)->TryGetStringField(TEXT("DeviceId"), OutData.DeviceId);
		auto ReadNumber = [&Object](const TCHAR* Key, TOptional<float>& Out)
		{
			double Value;
			if ((*Object)->TryGetNumberField(Key, Value) && FMath::IsFinite(Value)
				&& FMath::Abs(Value) <= MAX_flt) Out = static_cast<float>(Value);
		};
		ReadNumber(TEXT("Zoom"), OutData.Zoom);
		ReadNumber(TEXT("ScrollX"), OutData.ScrollX);
		ReadNumber(TEXT("ScrollY"), OutData.ScrollY);
		bool bTransparency;
		if ((*Object)->TryGetBoolField(TEXT("SupportsTransparency"), bTransparency)) OutData.bSupportsTransparency = bTransparency;
	}
}
