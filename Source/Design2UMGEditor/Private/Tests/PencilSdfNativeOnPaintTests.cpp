// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "EdGraphSchema_K2.h"
#include "Engine/TextureRenderTarget2D.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"
#include "WidgetBlueprint.h"
#include "Tests/PencilSdfNativePrototype.h"

#if WITH_AUTOMATION_WORKER
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilSdfNativeOnPaintContractTest,
    "Design2UMG.Resources.SdfNativeOnPaintContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilSdfNativeOnPaintContractTest::RunTest(const FString& Parameters)
{
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWidgetBlueprint::StaticClass(), TEXT("SdfNativePaint"));
    TStrongObjectPtr<UWidgetBlueprint> Blueprint(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), GetTransientPackage(), Name, BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
    if (!TestNotNull(TEXT("Native UserWidget blueprint creates"), Blueprint.Get())) return false;
    Blueprint->WidgetTree->RootWidget = Blueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());

    const auto AddObjectVariable = [&](FName VariableName, UClass* Class)
    {
        FEdGraphPinType Type;
        Type.PinCategory = UEdGraphSchema_K2::PC_Object;
        Type.PinSubCategoryObject = Class;
        return FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), VariableName, Type);
    };
    if (!TestTrue(TEXT("Instance RT member declares"), AddObjectVariable(TEXT("SdfTarget"), UTextureRenderTarget2D::StaticClass()))
        || !TestTrue(TEXT("Instance MID member declares"), AddObjectVariable(TEXT("SdfPass"), UMaterialInstanceDynamic::StaticClass()))) return false;

    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint.Get(), TEXT("OnPaint"),
        UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(Blueprint.Get(), Graph, false, UUserWidget::StaticClass());
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Return = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (auto* Candidate = Cast<UK2Node_FunctionEntry>(Node)) Entry = Candidate;
        if (auto* Candidate = Cast<UK2Node_FunctionResult>(Node)) Return = Candidate;
    }
    if (!TestNotNull(TEXT("Real OnPaint function entry exists"), Entry)) return false;
    const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(Graph->GetSchema());
    bool bConnected = true;
    const auto Link = [&](UEdGraphPin* A, UEdGraphPin* B)
    {
        const bool bOk = A && B && Schema->TryCreateConnection(A, B);
        bConnected &= bOk;
        return bOk;
    };
    const auto GetMember = [&](FName VariableName)
    {
        auto* Node = NewObject<UK2Node_VariableGet>(Graph);
        Node->VariableReference.SetSelfMember(VariableName);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->AllocateDefaultPins();
        return Node->FindPin(VariableName);
    };
    const auto Call = [&](FName FunctionName)
    {
        auto* Node = NewObject<UK2Node_CallFunction>(Graph);
        Node->SetFromFunction(UKismetRenderingLibrary::StaticClass()->FindFunctionByName(FunctionName));
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->AllocateDefaultPins();
        return Node;
    };
    auto* Self = NewObject<UK2Node_Self>(Graph);
    Graph->AddNode(Self, false, false);
    Self->CreateNewGuid();
    Self->AllocateDefaultPins();
    UK2Node_CallFunction* Clear = Call(TEXT("ClearRenderTarget2D"));
    UK2Node_CallFunction* Draw = Call(TEXT("DrawMaterialToRenderTarget"));
    UEdGraphPin* Target = GetMember(TEXT("SdfTarget"));
    UEdGraphPin* Material = GetMember(TEXT("SdfPass"));
    UEdGraphPin* World = Self->FindPin(UEdGraphSchema_K2::PN_Self);
    Link(Entry->FindPin(UEdGraphSchema_K2::PN_Then), Clear->GetExecPin());
    Link(Clear->GetThenPin(), Draw->GetExecPin());
    for (UK2Node_CallFunction* Node : {Clear, Draw})
    {
        Link(World, Node->FindPin(TEXT("WorldContextObject")));
        Link(Target, Node->FindPin(TEXT("TextureRenderTarget")));
    }
    Link(Material, Draw->FindPin(TEXT("Material")));
    if (Return)
    {
        Link(Draw->GetThenPin(), Return->FindPin(UEdGraphSchema_K2::PN_Execute));
        UEdGraphPin* ContextOut = Entry->FindPin(TEXT("Context"));
        UEdGraphPin* ContextIn = Return->FindPin(TEXT("Context"));
        if (ContextOut && ContextIn) Link(ContextOut, ContextIn);
    }
    if (!TestTrue(TEXT("Native const graph links without a runtime helper"), bConnected)) return false;
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    TestTrue(TEXT("Native OnPaint graph compiles"), Blueprint->Status != BS_Error);
    UFunction* Function = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->FindFunctionByName(TEXT("OnPaint")) : nullptr;
    if (TestNotNull(TEXT("Generated OnPaint exists"), Function))
    {
        TestTrue(TEXT("OnPaint is implemented by this generated native-widget blueprint"),
            Function->GetOuterUClass() == Blueprint->GeneratedClass);
        TestTrue(TEXT("Generated OnPaint retains the native const contract"), Function->HasAnyFunctionFlags(FUNC_Const));
    }
    AddInfo(TEXT("Compile-only prerequisite: no Retainer capture, runtime dispatch, SDF algorithm or nested ordering is asserted."));
    return Blueprint->Status != BS_Error;
}

#if PLATFORM_WINDOWS && ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 7
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilSdfNativeDynamicTest,
    "Design2UMG.Resources.SdfNativeDynamicNested",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilSdfNativeDynamicTest::RunTest(const FString& Parameters)
{
    return PencilSdfNativePrototype::Run(*this);
}
#endif
#endif
