// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "PencilShaderRuntimeBindings.h"
#include "DesignWidgetBlueprintUtils.h"
#include "Misc/EngineVersionComparison.h"
#include "PencilShaderSdf.h"
#include "PencilShaderMaterials.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "UObject/UnrealType.h"
#include "Design2UMGGenerationManifest.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/NamedSlot.h"
#include "Components/NamedSlotInterface.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Self.h"
#include "Materials/Material.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "Design2UMGWidgetMetadata.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/RetainerBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/WidgetSwitcher.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WidgetBlueprint.h"

namespace
{
const FString Marker = TEXT("Design2UMG Shader Runtime");
const FName VariableOwner(TEXT("Design2UMGShaderRuntime"));
struct FTarget
{
    UWidget* Widget = nullptr;
    bool Mouse = false;
    bool Sdf = false;
    // Retainers use their padding in logical units; fills use fractions of their original local paint size.
    FMargin CaptureInsets = FMargin(0);
    bool ObjectMip = false;
    bool FillObject = false;
    UMaterialInterface* Material = nullptr;
    FString NativeRole;
};

TArray<FTarget> Targets(UWidgetBlueprint* Blueprint, FString& Error)
{
    TArray<FTarget> Out;
    if (!Blueprint || !Blueprint->WidgetTree)
        return Out;
    Blueprint->WidgetTree->ForEachWidget(
        [&](UWidget* Widget)
        {
            UMaterialInterface* Material = nullptr;
            if (auto Border = Cast<UBorder>(Widget))
                Material = Cast<UMaterialInterface>(Border->Background.GetResourceObject());
            else if (auto Image = Cast<UImage>(Widget))
                Material =
                    Cast<UMaterialInterface>(Design2UMGWidgetPropertyAccess::GetBrush(*Image).GetResourceObject());
            else if (auto Retainer = Cast<URetainerBox>(Widget))
                Material = const_cast<UMaterialInterface*>(
                    Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*Retainer));
            if (!Material)
                return;
            TArray<FMaterialParameterInfo> Parameters;
            TArray<FGuid> Ids;
            Material->GetAllVectorParameterInfo(Parameters, Ids);
            const auto Has = [&](FName Name)
            { return Parameters.ContainsByPredicate([&](const auto& P) { return P.Name == Name; }); };
            if (!Has(TEXT("D2UResolution")) || !Has(TEXT("D2UDrawRect")))
                return;
            FMargin Insets(0);
            FLinearColor Draw, Size;
            if (auto Retainer = Cast<URetainerBox>(Widget))
            {
                if (Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("D2UDrawRect")), Draw) &&
                    Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("D2UResolution")), Size) &&
                    (Draw.R < 0 || Draw.G < 0 || Draw.B > Size.R || Draw.A > Size.G))
                {
                    auto Padded = Cast<UBorder>(Retainer->GetContent());
                    if (!Padded || !Padded->GetContent())
                    {
                        Error = TEXT("Shader expanded capture is missing its padding border");
                        return;
                    }
                    Insets = Padded->GetPadding();
                }
            }
            else if (Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("D2UDrawRect")), Draw) &&
                     Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("D2UResolution")), Size) &&
                     (Draw.R < 0 || Draw.G < 0 || Draw.B > Size.R || Draw.A > Size.G))
            {
                // A fill's expanded image uses a render transform; its local size still describes the original paint
                // box.
                Insets = FMargin(-Draw.R / Size.R, -Draw.G / Size.G, (Draw.B + Draw.R - Size.R) / Size.R,
                                 (Draw.A + Draw.G - Size.G) / Size.G);
            }
            TArray<FMaterialParameterInfo> TextureParameters;
            Material->GetAllTextureParameterInfo(TextureParameters, Ids);
            const bool Sdf = TextureParameters.ContainsByPredicate([](const auto& Parameter)
                                                                   { return Parameter.Name == TEXT("D2USdf"); });
        const bool ObjectMip = TextureParameters.ContainsByPredicate([](const auto& Parameter) {
            return Parameter.Name == TEXT("D2UObjectMip");
        });
        Out.Add({Widget, Has(TEXT("D2UMouse")), Sdf, Insets, ObjectMip, Has(TEXT("D2UFillObject")), Material});
        });
    return Out;
}
bool Owned(const UEdGraphNode* Node)
{
    return Node && Node->NodeComment == Marker;
}

void Clear(UWidgetBlueprint* Blueprint)
{
    // The root sequence's first output is the user's original Tick chain.
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        TArray<UEdGraphNode*> Remove;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Owned(Node))
                continue;
            if (auto Sequence = Cast<UK2Node_ExecutionSequence>(Node))
            {
                UEdGraphPin* Input = Sequence->GetExecPin();
                if (Input && Input->LinkedTo.Num() == 1 && Cast<UK2Node_Event>(Input->LinkedTo[0]->GetOwningNode()))
                {
                    UEdGraphPin* Upstream = Input->LinkedTo[0];
                    const auto UserLinks = Sequence->GetThenPinGivenIndex(0)->LinkedTo;
                    Upstream->BreakLinkTo(Input);
                    for (UEdGraphPin* Pin : UserLinks)
                        if (!Owned(Pin->GetOwningNode()))
                            Upstream->MakeLinkTo(Pin);
                }
            }
            if (!Cast<UK2Node_Event>(Node))
                Remove.Add(Node);
        }
        for (UEdGraphNode* Node : Remove)
            FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
        Remove.Reset();
        for (UEdGraphNode* Node : Graph->Nodes)
            if (Owned(Node) && Cast<UK2Node_Event>(Node))
            {
                bool UserLinks = false;
                for (const auto* Pin : Node->Pins)
                    for (const auto* Link : Pin->LinkedTo)
                        UserLinks |= !Owned(Link->GetOwningNode());
                if (UserLinks)
                    Node->NodeComment.Reset();
                else
                    Remove.Add(Node);
            }
        for (UEdGraphNode* Node : Remove)
            FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
    }
    TArray<FName> RemoveVariables;
    for (auto& Variable : Blueprint->NewVariables)
        if (Variable.HasMetaData(VariableOwner))
        {
            bool UserReference = false;
            TArray<UEdGraph*> Graphs;
            Blueprint->GetAllGraphs(Graphs);
            for (const auto* Graph : Graphs)
                for (const UEdGraphNode* Node : Graph->Nodes)
                    if (const auto* Reference = Cast<UK2Node_Variable>(Node))
                        UserReference |=
                            !Owned(Node) && Reference->VariableReference.GetMemberName() == Variable.VarName;
            if (UserReference)
                Variable.RemoveMetaData(VariableOwner);
            else
                RemoveVariables.Add(Variable.VarName);
        }
    FBlueprintEditorUtils::BulkRemoveMemberVariables(Blueprint, RemoveVariables);
}

class FGraph
{
  public:
    UEdGraph* Graph;
    FString& Error;
    int32 Count = 0;
    FGraph(UEdGraph* InGraph, FString& InError) : Graph(InGraph), Error(InError) {}
    template <class T, class F> T* Node(F Init)
    {
        FGraphNodeCreator<T> Creator(*Graph);
        T* N = Creator.CreateNode(false);
        Init(N);
        Creator.Finalize();
        N->NodeComment = Marker;
        N->NodePosX = (Count % 8) * 280;
        N->NodePosY = 1200 + (Count / 8) * 220;
        ++Count;
        return N;
    }
    template <class T> T* Node()
    {
        return Node<T>([](T*) {});
    }
    UEdGraphPin* Pin(UEdGraphNode* N, FName Name)
    {
        UEdGraphPin* P = N ? N->FindPin(Name) : nullptr;
        if (!P && Error.IsEmpty())
            Error = TEXT("Shader graph pin is missing: ") + Name.ToString();
        return P;
    }
    void Link(UEdGraphPin* A, UEdGraphPin* B)
    {
        const int32 Before = Graph->Nodes.Num();
        if ((!A || !B || !Graph->GetSchema()->TryCreateConnection(A, B)) && Error.IsEmpty())
            Error = TEXT("Shader graph connection failed");
        for (int32 I = Before; I < Graph->Nodes.Num(); ++I)
            Graph->Nodes[I]->NodeComment = Marker;
    }
    void Default(UEdGraphNode* N, FName Name, const FString& Value)
    {
        if (auto P = Pin(N, Name))
        {
            const FString Invalid = Graph->GetSchema()->IsPinDefaultValid(P, Value, nullptr, FText::GetEmpty());
            if (!Invalid.IsEmpty())
            {
                if (Error.IsEmpty())
                    Error = Invalid;
                return;
            }
            Graph->GetSchema()->TrySetDefaultValue(*P, Value);
        }
    }
    UK2Node_CallFunction* Call(UClass* Class, FName Function)
    {
        UFunction* Fn = Class->FindFunctionByName(Function);
        if (!Fn)
        {
            if (Error.IsEmpty())
                Error = TEXT("Shader graph native function is missing: ") + Function.ToString();
            return nullptr;
        }
        return Node<UK2Node_CallFunction>([&](auto* N) { N->SetFromFunction(Fn); });
    }
    UK2Node_CallFunction* Math(FName Function)
    {
        return Call(UKismetMathLibrary::StaticClass(), Function);
    }
    UEdGraphPin* Return(UEdGraphNode* N)
    {
        return Pin(N, UEdGraphSchema_K2::PN_ReturnValue);
    }
    UEdGraphPin* Get(FName Name)
    {
        auto N = Node<UK2Node_VariableGet>([&](auto* V) { V->VariableReference.SetSelfMember(Name); });
        return Pin(N, Name);
    }
    UEdGraphPin* Binary(FName Function, UEdGraphPin* A, UEdGraphPin* B)
    {
        auto N = Math(Function);
        Link(A, Pin(N, TEXT("A")));
        Link(B, Pin(N, TEXT("B")));
        return Return(N);
    }
    UK2Node_CallFunction* Break(UEdGraphPin* Vector)
    {
        auto N = Math(TEXT("BreakVector2D"));
        Link(Vector, Pin(N, TEXT("InVec")));
        return N;
    }
    UEdGraphPin* Vector(double X, double Y)
    {
        auto N = Math(TEXT("MakeVector2D"));
        Default(N, TEXT("X"), FString::SanitizeFloat(X));
        Default(N, TEXT("Y"), FString::SanitizeFloat(Y));
        return Return(N);
    }
    UEdGraphPin* Color(UEdGraphPin* R, UEdGraphPin* G, UEdGraphPin* B = nullptr, UEdGraphPin* A = nullptr)
    {
        auto N = Math(TEXT("MakeColor"));
        for (auto Pair : {TPair<FName, UEdGraphPin*>(TEXT("R"), R), {TEXT("G"), G}, {TEXT("B"), B}, {TEXT("A"), A}})
            if (Pair.Value)
                Link(Pair.Value, Pin(N, Pair.Key));
            else
                Default(N, Pair.Key, TEXT("0"));
        return Return(N);
    }
    UEdGraphPin* Parameter(UEdGraphPin* Exec, UEdGraphPin* MID, FName Name, UEdGraphPin* Value)
    {
        auto N = Call(UMaterialInstanceDynamic::StaticClass(), TEXT("SetVectorParameterValue"));
        Link(Exec, N ? N->GetExecPin() : nullptr);
        Link(MID, Pin(N, UEdGraphSchema_K2::PN_Self));
        Default(N, TEXT("ParameterName"), Name.ToString());
        Link(Value, Pin(N, TEXT("Value")));
        return N ? N->GetThenPin() : nullptr;
    }
    UEdGraphPin* Self()
    {
        auto* N = Node<UK2Node_Self>();
        return Pin(N, UEdGraphSchema_K2::PN_Self);
    }
    UK2Node_CallFunction* Execute(UClass* Class, FName Function, UEdGraphPin*& Exec)
    {
        auto* N = Call(Class, Function);
        if (N && N->GetExecPin())
        {
            Link(Exec, N->GetExecPin());
            Exec = N->GetThenPin();
        }
        return N;
    }
    UEdGraphPin* Cast(UClass* Class, UEdGraphPin* Value)
    {
        auto* N = Node<UK2Node_DynamicCast>([&](auto* CastNode) { CastNode->TargetType = Class; });
        N->SetPurity(true);
        Link(Value, N->GetCastSourcePin());
        return N->GetCastResultPin();
    }
    UEdGraphPin* Part(UEdGraphPin* Value, FName Component)
    {
        auto* N = Math(TEXT("BreakColor"));
        Link(Value, Pin(N, TEXT("InColor")));
        return Pin(N, Component);
    }
    UEdGraphPin* Literal(double Value)
    {
        auto* N = Call(UKismetSystemLibrary::StaticClass(), TEXT("MakeLiteralDouble"));
        Default(N, TEXT("Value"), FString::SanitizeFloat(Value));
        return Return(N);
    }
    UEdGraphPin* Select(UEdGraphPin* Condition, UEdGraphPin* A, UEdGraphPin* B)
    {
        auto* N = Math(TEXT("SelectFloat"));
        Link(Condition, Pin(N, TEXT("bPickA")));
        Link(A, Pin(N, TEXT("A")));
        Link(B, Pin(N, TEXT("B")));
        return Return(N);
    }
    UEdGraphPin* ReadVector(UEdGraphPin* MID, FName Name, UEdGraphPin*& Exec)
    {
        auto* N = Execute(UMaterialInstanceDynamic::StaticClass(), TEXT("K2_GetVectorParameterValue"), Exec);
        Link(MID, Pin(N, UEdGraphSchema_K2::PN_Self));
        Default(N, TEXT("ParameterName"), Name.ToString());
        return Return(N);
    }
    UEdGraphPin* ReadTexture(UEdGraphPin* MID, FName Name, UEdGraphPin*& Exec)
    {
        auto* N = Execute(UMaterialInstanceDynamic::StaticClass(), TEXT("K2_GetTextureParameterValue"), Exec);
        Link(MID, Pin(N, UEdGraphSchema_K2::PN_Self));
        Default(N, TEXT("ParameterName"), Name.ToString());
        return Return(N);
    }
    void Texture(UEdGraphPin* MID, FName Name, UEdGraphPin* Value, UEdGraphPin*& Exec)
    {
        auto* N = Execute(UMaterialInstanceDynamic::StaticClass(), TEXT("SetTextureParameterValue"), Exec);
        Link(MID, Pin(N, UEdGraphSchema_K2::PN_Self));
        Default(N, TEXT("ParameterName"), Name.ToString());
        Link(Value, Pin(N, TEXT("Value")));
    }
    void Scalar(UEdGraphPin* MID, FName Name, double Value, UEdGraphPin*& Exec)
    {
        auto* N = Execute(UMaterialInstanceDynamic::StaticClass(), TEXT("SetScalarParameterValue"), Exec);
        Link(MID, Pin(N, UEdGraphSchema_K2::PN_Self));
        Default(N, TEXT("ParameterName"), Name.ToString());
        Default(N, TEXT("Value"), FString::SanitizeFloat(Value));
    }
    void SetObject(FName Name, UEdGraphPin* Value, UEdGraphPin*& Exec)
    {
        auto* N = Node<UK2Node_VariableSet>([&](auto* Variable) { Variable->VariableReference.SetSelfMember(Name); });
        Link(Exec, N->GetExecPin());
        if (Value)
            Link(Value, Pin(N, Name));
        Exec = N->GetThenPin();
    }
    UEdGraphPin* ObjectMember(UEdGraphPin* Object, UClass* Class, FName Name)
    {
        auto* N = Node<UK2Node_VariableGet>([&](auto* Variable)
                                            { Variable->VariableReference.SetExternalMember(Name, Class); });
        Link(Object, Pin(N, UEdGraphSchema_K2::PN_Self));
        return Pin(N, Name);
    }
    UEdGraphPin* Local(UWidgetBlueprint* Blueprint, FName Name, UEdGraphPin* Value, UEdGraphPin*& Exec)
    {
        auto* Variable = FBlueprintEditorUtils::FindLocalVariable(Blueprint, Graph, Name);
        if (!Variable)
        {
            FEdGraphPinType Type;
            Type.PinCategory = UEdGraphSchema_K2::PC_Object;
            Type.PinSubCategoryObject = UTextureRenderTarget2D::StaticClass();
            if (!FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, Name, Type))
            {
                Error = TEXT("Shader paint local variable creation failed");
                return nullptr;
            }
            Variable = FBlueprintEditorUtils::FindLocalVariable(Blueprint, Graph, Name);
        }
        auto* SetLocal = Node<UK2Node_VariableSet>(
            [&](auto* N) { N->VariableReference.SetLocalMember(Name, Graph->GetName(), Variable->VarGuid); });
        Link(Exec, SetLocal->GetExecPin());
        Link(Value, Pin(SetLocal, Name));
        Exec = SetLocal->GetThenPin();
        auto* GetLocal = Node<UK2Node_VariableGet>(
            [&](auto* N) { N->VariableReference.SetLocalMember(Name, Graph->GetName(), Variable->VarGuid); });
        return Pin(GetLocal, Name);
    }
    UEdGraphPin* Event(FName Name)
    {
        for (UEdGraphNode* Existing : Graph->Nodes)
            if (auto* Entry = ::Cast<UK2Node_Event>(Existing))
                if (Entry->EventReference.GetMemberName() == Name)
                {
                    UEdGraphPin* Start = Entry->GetThenPin();
                    const auto Links = Start->LinkedTo;
                    Start->BreakAllPinLinks();
                    auto* Sequence = Node<UK2Node_ExecutionSequence>();
                    Link(Start, Sequence->GetExecPin());
                    for (UEdGraphPin* Original : Links)
                        Link(Sequence->GetThenPinGivenIndex(0), Original);
                    return Sequence->GetThenPinGivenIndex(1);
                }
        auto* Entry = Node<UK2Node_Event>([&](auto* N) {
            N->EventReference.SetExternalMember(Name, UUserWidget::StaticClass());
            N->bOverrideFunction = true;
        });
        return Entry->GetThenPin();
    }

    void Draw(FName Pass, UEdGraphPin* Target, UEdGraphPin*& Exec)
    {
        auto* Clear = Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ClearRenderTarget2D"), Exec);
        Link(Self(), Pin(Clear, TEXT("WorldContextObject")));
        Link(Target, Pin(Clear, TEXT("TextureRenderTarget")));
        Default(Clear, TEXT("ClearColor"), TEXT("(R=0,G=0,B=0,A=1)"));
        auto* Draw = Execute(UKismetRenderingLibrary::StaticClass(), TEXT("DrawMaterialToRenderTarget"), Exec);
        Link(Self(), Pin(Draw, TEXT("WorldContextObject")));
        Link(Target, Pin(Draw, TEXT("TextureRenderTarget")));
        Link(Get(Pass), Pin(Draw, TEXT("Material")));
    }
};

const FName SurfaceSlot(TEXT("Surface"));
const TArray<FName> TargetNames = {TEXT("Coords0"), TEXT("Coords1"), TEXT("Distance"), TEXT("Blur"), TEXT("Sdf")};
TArray<FName> PassNames()
{
    TArray<FName> Names = {TEXT("Init")};
    for (int32 I = 0; I < PencilShaderSdf::JumpMaterialCount; ++I)
    {
        Names.Add(*FString::Printf(TEXT("Jump%d"), I));
    }
    Names.Append({TEXT("DistancePass"), TEXT("BlurX"), TEXT("BlurY"), TEXT("Gradient")});
    return Names;
}

UEdGraphPin* Surface(FGraph& Builder, UClass* Class, UEdGraphPin*& Exec)
{
    auto* Content = Builder.Execute(UContentWidget::StaticClass(), TEXT("GetContent"), Exec);
    Builder.Link(Builder.Get(SurfaceSlot), Builder.Pin(Content, UEdGraphSchema_K2::PN_Self));
    return Builder.Cast(Class, Builder.Return(Content));
}

UEdGraphPin* BindParameters(FGraph& Builder, const FTarget& Target, UEdGraphPin* MID, UEdGraphPin* Geometry,
                            UEdGraphPin* CaptureSize, UEdGraphPin*& Exec)
{
    const FMargin& Insets = Target.CaptureInsets;
    const bool Retainer = Target.Widget->IsA<URetainerBox>();
    UEdGraphPin* LogicalSize = CaptureSize;
    UEdGraphPin* DrawSize = CaptureSize;
    UEdGraphPin* Origin = Builder.Vector(Insets.Left, Insets.Top);
    if (Insets != FMargin(0))
    {
        if (Retainer)
            LogicalSize = Builder.Binary(TEXT("Subtract_Vector2DVector2D"), CaptureSize,
                                         Builder.Vector(Insets.Left + Insets.Right, Insets.Top + Insets.Bottom));
        else
        {
            DrawSize = Builder.Binary(TEXT("Multiply_Vector2DVector2D"), CaptureSize,
                                      Builder.Vector(1 + Insets.Left + Insets.Right, 1 + Insets.Top + Insets.Bottom));
            Origin =
                Builder.Binary(TEXT("Multiply_Vector2DVector2D"), CaptureSize, Builder.Vector(Insets.Left, Insets.Top));
        }
    }
    auto* Dimensions = Builder.Break(LogicalSize);
    if (Target.Mouse)
    {
        // RG is the public position; B holds the last real-time sample, A marks initialization.
        // Reading the previous resolution before replacement also preserves Y through a resize.
        auto* PreviousMouse = Builder.ReadVector(MID, TEXT("D2UMouse"), Exec);
        auto* PreviousSize = Builder.ReadVector(MID, TEXT("D2UResolution"), Exec);
        auto* Initialized =
            Builder.Binary(TEXT("Greater_DoubleDouble"), Builder.Part(PreviousMouse, TEXT("A")), Builder.Literal(0));
        auto* Clock = Builder.Call(UGameplayStatics::StaticClass(), TEXT("GetRealTimeSeconds"));
        Builder.Link(Builder.Self(), Builder.Pin(Clock, TEXT("WorldContextObject")));
        auto* Delta = Builder.Call(UGameplayStatics::StaticClass(), TEXT("GetWorldDeltaSeconds"));
        Builder.Link(Builder.Self(), Builder.Pin(Delta, TEXT("WorldContextObject")));
        auto* Elapsed = Builder.Select(Initialized,
                                       Builder.Binary(TEXT("Subtract_DoubleDouble"), Builder.Return(Clock),
                                                      Builder.Part(PreviousMouse, TEXT("B"))),
                                       Builder.Return(Delta));
        auto* Cursor = Builder.Execute(UWidgetLayoutLibrary::StaticClass(), TEXT("GetMousePositionOnPlatform"), Exec);
        auto* Local = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("AbsoluteToLocal"));
        Builder.Link(Geometry, Builder.Pin(Local, TEXT("Geometry")));
        Builder.Link(Builder.Return(Cursor), Builder.Pin(Local, TEXT("AbsoluteCoordinate")));
        UEdGraphPin* NodeCursor = Builder.Return(Local);
        if (Insets != FMargin(0))
        {
            if (!Retainer)
                NodeCursor =
                    Builder.Binary(TEXT("Multiply_Vector2DVector2D"), NodeCursor,
                                   Builder.Vector(1 + Insets.Left + Insets.Right, 1 + Insets.Top + Insets.Bottom));
            NodeCursor = Builder.Binary(TEXT("Subtract_Vector2DVector2D"), NodeCursor, Origin);
        }
        auto* PreviousX = Builder.Select(
            Initialized, Builder.Part(PreviousMouse, TEXT("R")),
            Builder.Binary(TEXT("Multiply_DoubleDouble"), Builder.Pin(Dimensions, TEXT("X")), Builder.Literal(0.5)));
        auto* PreviousY = Builder.Select(
            Initialized,
            Builder.Binary(TEXT("Subtract_DoubleDouble"), Builder.Part(PreviousSize, TEXT("G")),
                           Builder.Part(PreviousMouse, TEXT("G"))),
            Builder.Binary(TEXT("Multiply_DoubleDouble"), Builder.Pin(Dimensions, TEXT("Y")), Builder.Literal(0.5)));
        auto* Previous = Builder.Math(TEXT("MakeVector2D"));
        Builder.Link(PreviousX, Builder.Pin(Previous, TEXT("X")));
        Builder.Link(PreviousY, Builder.Pin(Previous, TEXT("Y")));
        auto* Rate = Builder.Binary(TEXT("Multiply_DoubleDouble"), Elapsed, Builder.Literal(-5));
        auto* Exponential = Builder.Math(TEXT("Exp"));
        Builder.Link(Rate, Builder.Pin(Exponential, TEXT("A")));
        auto* Difference = Builder.Binary(TEXT("Subtract_Vector2DVector2D"), Builder.Return(Previous), NodeCursor);
        auto* Weighted = Builder.Math(TEXT("Multiply_Vector2DFloat"));
        Builder.Link(Difference, Builder.Pin(Weighted, TEXT("A")));
        Builder.Link(Builder.Return(Exponential), Builder.Pin(Weighted, TEXT("B")));
        auto* Position =
            Builder.Break(Builder.Binary(TEXT("Add_Vector2DVector2D"), NodeCursor, Builder.Return(Weighted)));
        auto* UpY = Builder.Binary(TEXT("Subtract_DoubleDouble"), Builder.Pin(Dimensions, TEXT("Y")),
                                   Builder.Pin(Position, TEXT("Y")));
        Exec = Builder.Parameter(
            Exec, MID, TEXT("D2UMouse"),
            Builder.Color(Builder.Pin(Position, TEXT("X")), UpY, Builder.Return(Clock), Builder.Literal(1)));
    }
    Exec = Builder.Parameter(Exec, MID, TEXT("D2UResolution"),
                             Builder.Color(Builder.Pin(Dimensions, TEXT("X")), Builder.Pin(Dimensions, TEXT("Y"))));
    auto* DrawDimensions = Builder.Break(DrawSize);
    auto* Offset = Builder.Break(Origin);
    Exec = Builder.Parameter(
        Exec, MID, TEXT("D2UDrawRect"),
        Builder.Color(
            Builder.Binary(TEXT("Multiply_DoubleDouble"), Builder.Pin(Offset, TEXT("X")), Builder.Literal(-1)),
            Builder.Binary(TEXT("Multiply_DoubleDouble"), Builder.Pin(Offset, TEXT("Y")), Builder.Literal(-1)),
            Builder.Pin(DrawDimensions, TEXT("X")), Builder.Pin(DrawDimensions, TEXT("Y"))));
    return DrawSize;
}

void WireSdfPaint(FGraph& Builder, UWidgetBlueprint* Blueprint, const FTarget& Target, UEdGraphPin* MID,
                  UEdGraphPin* Geometry, UEdGraphPin* CaptureSize, UEdGraphPin*& Exec)
{
    // Fill SDFs describe their shape mask, not the captured lower paint.
    const bool Effect = Target.Widget->IsA<URetainerBox>() && !Target.FillObject;
    UEdGraphPin* Alpha = Builder.ReadTexture(MID, Effect ? TEXT("D2UObject") : TEXT("GeometryMask"), Exec);
    if (Target.FillObject)
        CaptureSize = Builder.Binary(TEXT("Subtract_Vector2DVector2D"), CaptureSize,
            Builder.Vector(Target.CaptureInsets.Left + Target.CaptureInsets.Right,
                           Target.CaptureInsets.Top + Target.CaptureInsets.Bottom));
    auto* Dimensions = Builder.Break(CaptureSize);
    UEdGraphPin* PhysicalWidth = nullptr;
    if (Effect)
    {
        auto* Capture = Builder.Cast(UTextureRenderTarget2D::StaticClass(), Alpha);
        PhysicalWidth = Builder.ObjectMember(Capture, UTextureRenderTarget2D::StaticClass(), TEXT("SizeX"));
    }
    else
    {
        auto* LocalSize = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("GetLocalSize"));
        Builder.Link(Geometry, Builder.Pin(LocalSize, TEXT("Geometry")));
        UEdGraphPin* LocalExtent = Builder.Return(LocalSize);
        if (Target.FillObject)
            LocalExtent = Builder.Binary(TEXT("Subtract_Vector2DVector2D"), LocalExtent,
                Builder.Vector(Target.CaptureInsets.Left + Target.CaptureInsets.Right,
                               Target.CaptureInsets.Top + Target.CaptureInsets.Bottom));
        auto* LocalDimensions = Builder.Break(LocalExtent);
        auto* Horizontal = Builder.Math(TEXT("MakeVector2D"));
        Builder.Link(Builder.Pin(LocalDimensions, TEXT("X")), Builder.Pin(Horizontal, TEXT("X")));
        auto* Start = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("LocalToAbsolute"));
        auto* End = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("LocalToAbsolute"));
        Builder.Link(Geometry, Builder.Pin(Start, TEXT("Geometry")));
        Builder.Link(Geometry, Builder.Pin(End, TEXT("Geometry")));
        Builder.Link(Builder.Vector(0, 0), Builder.Pin(Start, TEXT("LocalCoordinate")));
        Builder.Link(Builder.Return(Horizontal), Builder.Pin(End, TEXT("LocalCoordinate")));
        auto* Distance = Builder.Math(TEXT("Distance2D"));
        Builder.Link(Builder.Return(Start), Builder.Pin(Distance, TEXT("V1")));
        Builder.Link(Builder.Return(End), Builder.Pin(Distance, TEXT("V2")));
        PhysicalWidth = Builder.Return(Distance);
    }
    auto* Density = Builder.Binary(TEXT("Divide_DoubleDouble"), PhysicalWidth, Builder.Pin(Dimensions, TEXT("X")));
    auto* LogicalMaximum =
        Builder.Binary(TEXT("FMax"), Builder.Pin(Dimensions, TEXT("X")), Builder.Pin(Dimensions, TEXT("Y")));
    auto* Scale = Builder.Binary(TEXT("FMin"), Density,
                                 Builder.Binary(TEXT("Divide_DoubleDouble"),
                                                Builder.Literal(PencilShaderSdf::MaximumDimension), LogicalMaximum));
    const auto SdfDimension = [&](FName Axis)
    {
        auto* Ceil = Builder.Math(TEXT("FCeil"));
        Builder.Link(Builder.Binary(TEXT("Multiply_DoubleDouble"), Builder.Pin(Dimensions, Axis), Scale),
                     Builder.Pin(Ceil, TEXT("A")));
        return Builder.Return(Ceil);
    };
    auto* Width = SdfDimension(TEXT("X"));
    auto* Height = SdfDimension(TEXT("Y"));
    auto* VisibleScale = Builder.Node<UK2Node_IfThenElse>();
    Builder.Link(Exec, VisibleScale->GetExecPin());
    Builder.Link(Builder.Binary(TEXT("Greater_DoubleDouble"), Scale, Builder.Literal(0)),
                 VisibleScale->GetConditionPin());
    Exec = VisibleScale->GetThenPin();
    auto* Frame = Builder.Color(Width, Height, Builder.Binary(TEXT("Divide_DoubleDouble"), Builder.Literal(1), Scale));
    auto* SdfTarget = Builder.Get(TEXT("Sdf"));
    auto* WidthChanged = Builder.Binary(TEXT("NotEqual_IntInt"),
        Builder.ObjectMember(SdfTarget, UTextureRenderTarget2D::StaticClass(), TEXT("SizeX")), Width);
    auto* HeightChanged = Builder.Binary(TEXT("NotEqual_IntInt"),
        Builder.ObjectMember(SdfTarget, UTextureRenderTarget2D::StaticClass(), TEXT("SizeY")), Height);
    auto* ResizeNeeded = Builder.Node<UK2Node_IfThenElse>();
    Builder.Link(Exec, ResizeNeeded->GetExecPin());
    Builder.Link(Builder.Binary(TEXT("BooleanOR"), WidthChanged, HeightChanged), ResizeNeeded->GetConditionPin());
    Exec = ResizeNeeded->GetThenPin();
    for (FName Name : TargetNames)
    {
        // Rebuild on size changes so Canvas sees the new resource extent before its draw is queued.
        auto* Release = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ReleaseRenderTarget2D"), Exec);
        Builder.Link(Builder.Get(Name), Builder.Pin(Release, TEXT("TextureRenderTarget")));
        auto* Resize = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ResizeRenderTarget2D"), Exec);
        Builder.Link(Builder.Get(Name), Builder.Pin(Resize, TEXT("TextureRenderTarget")));
        Builder.Link(Width, Builder.Pin(Resize, TEXT("Width")));
        Builder.Link(Height, Builder.Pin(Resize, TEXT("Height")));
    }
    auto* Resized = Builder.Node<UK2Node_ExecutionSequence>();
    Builder.Link(Exec, Resized->GetExecPin());
    Builder.Link(ResizeNeeded->GetElsePin(), Resized->GetExecPin());
    Exec = Resized->GetThenPinGivenIndex(0);
    for (FName Name : PassNames())
    {
        Exec = Builder.Parameter(Exec, Builder.Get(Name), TEXT("Frame"), Frame);
    }
    for (FName Name : {FName(TEXT("Init")), FName(TEXT("DistancePass"))})
    {
        Builder.Texture(Builder.Get(Name), TEXT("Alpha"), Alpha, Exec);
    }
    Builder.Draw(TEXT("Init"), Builder.Get(TEXT("Coords0")), Exec);
    auto* Current = Builder.Local(Blueprint, TEXT("CurrentCoords"), Builder.Get(TEXT("Coords0")), Exec);
    auto* Maximum = Builder.Binary(TEXT("Max"), Width, Height);
    for (int32 I = 0; I < PencilShaderSdf::JumpMaterialCount; ++I)
    {
        const int32 Jump = PencilShaderSdf::MaximumDimension >> I;
        auto* Branch = Builder.Node<UK2Node_IfThenElse>();
        Builder.Link(Exec, Branch->GetExecPin());
        Builder.Link(Builder.Binary(TEXT("Greater_DoubleDouble"), Maximum, Builder.Literal(Jump / 2.0)),
                     Branch->GetConditionPin());
        Exec = Branch->GetThenPin();
        const FName Pass(*FString::Printf(TEXT("Jump%d"), I));
        Builder.Texture(Builder.Get(Pass), TEXT("Input"), Current, Exec);
        auto* Choose = Builder.Math(TEXT("SelectObject"));
        Builder.Link(Builder.Get(TEXT("Coords1")), Builder.Pin(Choose, TEXT("A")));
        Builder.Link(Builder.Get(TEXT("Coords0")), Builder.Pin(Choose, TEXT("B")));
        Builder.Link(Builder.Binary(TEXT("EqualEqual_ObjectObject"), Current, Builder.Get(TEXT("Coords0"))),
                     Builder.Pin(Choose, TEXT("bSelectA")));
        auto* Output = Builder.Cast(UTextureRenderTarget2D::StaticClass(), Builder.Return(Choose));
        Builder.Draw(Pass, Output, Exec);
        Builder.Local(Blueprint, TEXT("CurrentCoords"), Output, Exec);
        auto* Join = Builder.Node<UK2Node_ExecutionSequence>();
        Builder.Link(Exec, Join->GetExecPin());
        Builder.Link(Branch->GetElsePin(), Join->GetExecPin());
        Exec = Join->GetThenPinGivenIndex(0);
    }
    Builder.Texture(Builder.Get(TEXT("DistancePass")), TEXT("Input"), Current, Exec);
    Builder.Draw(TEXT("DistancePass"), Builder.Get(TEXT("Distance")), Exec);
    Builder.Draw(TEXT("BlurX"), Builder.Get(TEXT("Blur")), Exec);
    Builder.Draw(TEXT("BlurY"), Builder.Get(TEXT("Distance")), Exec);
    Builder.Draw(TEXT("Gradient"), Builder.Get(TEXT("Sdf")), Exec);
    Builder.Texture(MID, TEXT("D2USdf"), Builder.Get(TEXT("Sdf")), Exec);
    auto* Complete = Builder.Node<UK2Node_ExecutionSequence>();
    Builder.Link(Exec, Complete->GetExecPin());
    Builder.Link(VisibleScale->GetElsePin(), Complete->GetExecPin());
    Exec = Complete->GetThenPinGivenIndex(0);
}

void WireSdfLifecycle(FGraph& Builder, const FTarget& Target, const PencilShaderSdf::FPassMaterials& Programs)
{
    const auto Event = [&](FName Name)
    {
        return Builder.Node<UK2Node_Event>(
            [&](auto* N)
            {
                N->EventReference.SetExternalMember(Name, UUserWidget::StaticClass());
                N->bOverrideFunction = true;
            });
    };
    UEdGraphPin* Exec = Event(TEXT("PreConstruct"))->GetThenPin();
    for (int32 I = 0; I < TargetNames.Num(); ++I)
    {
        auto* Release = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ReleaseRenderTarget2D"), Exec);
        Builder.Link(Builder.Get(TargetNames[I]), Builder.Pin(Release, TEXT("TextureRenderTarget")));
        auto* Create = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("CreateRenderTarget2D"), Exec);
        Builder.Link(Builder.Self(), Builder.Pin(Create, TEXT("WorldContextObject")));
        Builder.Default(Create, TEXT("Width"), TEXT("1"));
        Builder.Default(Create, TEXT("Height"), TEXT("1"));
        Builder.Default(Create, TEXT("Format"), (I == 2 || I == 3) ? TEXT("RTF_R16f") : TEXT("RTF_RGBA16f"));
        Builder.Default(Create, TEXT("ClearColor"), TEXT("(R=0,G=0,B=0,A=1)"));
        Builder.SetObject(TargetNames[I], Builder.Return(Create), Exec);
    }
    const TArray<FName> Names = PassNames();
    for (int32 I = 0; I < Names.Num(); ++I)
    {
        UMaterial* Material = I == 0                                    ? Programs.Init
                              : I <= PencilShaderSdf::JumpMaterialCount ? Programs.Jump
                              : I == Names.Num() - 4                    ? Programs.Distance
                              : I == Names.Num() - 1                    ? Programs.Gradient
                                                                        : Programs.Blur;
        auto* Create =
            Builder.Execute(UKismetMaterialLibrary::StaticClass(), TEXT("CreateDynamicMaterialInstance"), Exec);
        Builder.Link(Builder.Self(), Builder.Pin(Create, TEXT("WorldContextObject")));
        Builder.Pin(Create, TEXT("Parent"))->DefaultObject = Material;
        Builder.SetObject(Names[I], Builder.Return(Create), Exec);
        if (I > 0 && I <= PencilShaderSdf::JumpMaterialCount)
        {
            Builder.Scalar(Builder.Get(Names[I]), TEXT("Jump"), PencilShaderSdf::MaximumDimension >> (I - 1), Exec);
        }
    }
    Builder.Texture(Builder.Get(TEXT("BlurX")), TEXT("Input"), Builder.Get(TEXT("Distance")), Exec);
    Builder.Texture(Builder.Get(TEXT("BlurY")), TEXT("Input"), Builder.Get(TEXT("Blur")), Exec);
    Builder.Texture(Builder.Get(TEXT("Gradient")), TEXT("Input"), Builder.Get(TEXT("Distance")), Exec);
    Exec = Builder.Parameter(Exec, Builder.Get(TEXT("BlurX")), TEXT("Axis"),
                             Builder.Color(Builder.Literal(1), Builder.Literal(0)));
    Exec = Builder.Parameter(Exec, Builder.Get(TEXT("BlurY")), TEXT("Axis"),
                             Builder.Color(Builder.Literal(0), Builder.Literal(1)));
    auto* Widget = Surface(Builder, Target.Widget->GetClass(), Exec);
    auto* GetMID = Builder.Execute(
        Target.Widget->GetClass(),
        Target.Widget->IsA<URetainerBox>() ? TEXT("GetEffectMaterial") : TEXT("GetDynamicMaterial"), Exec);
    Builder.Link(Widget, Builder.Pin(GetMID, UEdGraphSchema_K2::PN_Self));
    Builder.Texture(Builder.Return(GetMID), TEXT("D2USdf"), Builder.Get(TEXT("Sdf")), Exec);
    Exec = Event(TEXT("Destruct"))->GetThenPin();
    for (FName Name : TargetNames)
    {
        auto* Release = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ReleaseRenderTarget2D"), Exec);
        Builder.Link(Builder.Get(Name), Builder.Pin(Release, TEXT("TextureRenderTarget")));
        Builder.SetObject(Name, nullptr, Exec);
    }
    for (FName Name : Names)
    {
        Builder.SetObject(Name, nullptr, Exec);
    }
}

void WireOutputProperties(FGraph& Builder, UEdGraphPin* Source, UEdGraphPin*& Exec)
{
    auto* Output = Builder.Get(TEXT("ShaderOutput"));
    for (const auto& Property : {
             TPair<FName, FName>(TEXT("RenderTransform"), TEXT("InTransform")),
             TPair<FName, FName>(TEXT("RenderTransformPivot"), TEXT("Pivot"))})
    {
        auto* Set = Builder.Execute(UWidget::StaticClass(),
            Property.Key == TEXT("RenderTransform") ? TEXT("SetRenderTransform") : TEXT("SetRenderTransformPivot"), Exec);
        Builder.Link(Output, Builder.Pin(Set, UEdGraphSchema_K2::PN_Self));
        Builder.Link(Builder.ObjectMember(Source, UWidget::StaticClass(), Property.Key), Builder.Pin(Set, Property.Value));
    }
    auto* Visible = Builder.Call(UWidget::StaticClass(), TEXT("IsVisible"));
    Builder.Link(Source, Builder.Pin(Visible, UEdGraphSchema_K2::PN_Self));
    auto* Opacity = Builder.Execute(UWidget::StaticClass(), TEXT("SetRenderOpacity"), Exec);
    Builder.Link(Output, Builder.Pin(Opacity, UEdGraphSchema_K2::PN_Self));
    Builder.Link(Builder.Select(Builder.Return(Visible),
        Builder.ObjectMember(Source, UWidget::StaticClass(), TEXT("RenderOpacity")), Builder.Literal(0)),
        Builder.Pin(Opacity, TEXT("InOpacity")));
}

void WireObjectMipLifecycle(FGraph& Builder, UMaterial* CopyMaterial)
{
    UEdGraphPin* Exec = Builder.Event(TEXT("PreConstruct"));
    auto* Release = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ReleaseRenderTarget2D"), Exec);
    Builder.Link(Builder.Get(TEXT("ObjectMip")), Builder.Pin(Release, TEXT("TextureRenderTarget")));
    auto* Create = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("CreateRenderTarget2D"), Exec);
    Builder.Link(Builder.Self(), Builder.Pin(Create, TEXT("WorldContextObject")));
    Builder.Default(Create, TEXT("Width"), TEXT("1"));
    Builder.Default(Create, TEXT("Height"), TEXT("1"));
    Builder.Default(Create, TEXT("Format"), TEXT("RTF_RGBA16f"));
    Builder.Default(Create, TEXT("ClearColor"), TEXT("(R=0,G=0,B=0,A=1)"));
    Builder.Default(Create, TEXT("bAutoGenerateMipMaps"), TEXT("true"));
    Builder.SetObject(TEXT("ObjectMip"), Builder.Return(Create), Exec);
    auto* Copy = Builder.Execute(UKismetMaterialLibrary::StaticClass(), TEXT("CreateDynamicMaterialInstance"), Exec);
    Builder.Link(Builder.Self(), Builder.Pin(Copy, TEXT("WorldContextObject")));
    Builder.Pin(Copy, TEXT("Parent"))->DefaultObject = CopyMaterial;
    Builder.SetObject(TEXT("ObjectCopy"), Builder.Return(Copy), Exec);

    Exec = Builder.Event(TEXT("Destruct"));
    Release = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ReleaseRenderTarget2D"), Exec);
    Builder.Link(Builder.Get(TEXT("ObjectMip")), Builder.Pin(Release, TEXT("TextureRenderTarget")));
    Builder.SetObject(TEXT("ObjectMip"), nullptr, Exec);
    Builder.SetObject(TEXT("ObjectCopy"), nullptr, Exec);
}

void WireObjectMipPaint(FGraph& Builder, UEdGraphPin* CaptureMID, UEdGraphPin* OutputMID, UEdGraphPin*& Exec)
{
    auto* Source = Builder.ReadTexture(CaptureMID, TEXT("D2UObject"), Exec);
    auto* Capture = Builder.Cast(UTextureRenderTarget2D::StaticClass(), Source);
    auto* Valid = Builder.Call(UKismetSystemLibrary::StaticClass(), TEXT("IsValid"));
    Builder.Link(Capture, Builder.Pin(Valid, TEXT("Object")));
    auto* Branch = Builder.Node<UK2Node_IfThenElse>();
    Builder.Link(Exec, Branch->GetExecPin());
    Builder.Link(Builder.Return(Valid), Branch->GetConditionPin());
    Exec = Branch->GetThenPin();
    auto* Target = Builder.Get(TEXT("ObjectMip"));
    auto* Width = Builder.ObjectMember(Capture, UTextureRenderTarget2D::StaticClass(), TEXT("SizeX"));
    auto* Height = Builder.ObjectMember(Capture, UTextureRenderTarget2D::StaticClass(), TEXT("SizeY"));
    auto* Changed = Builder.Binary(TEXT("BooleanOR"),
        Builder.Binary(TEXT("NotEqual_IntInt"), Width,
            Builder.ObjectMember(Target, UTextureRenderTarget2D::StaticClass(), TEXT("SizeX"))),
        Builder.Binary(TEXT("NotEqual_IntInt"), Height,
            Builder.ObjectMember(Target, UTextureRenderTarget2D::StaticClass(), TEXT("SizeY"))));
    auto* RecreateNeeded = Builder.Node<UK2Node_IfThenElse>();
    Builder.Link(Exec, RecreateNeeded->GetExecPin());
    Builder.Link(Changed, RecreateNeeded->GetConditionPin());
    Exec = RecreateNeeded->GetThenPin();
    auto* Release = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ReleaseRenderTarget2D"), Exec);
    Builder.Link(Target, Builder.Pin(Release, TEXT("TextureRenderTarget")));
    auto* Create = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("CreateRenderTarget2D"), Exec);
    Builder.Link(Builder.Self(), Builder.Pin(Create, TEXT("WorldContextObject")));
    Builder.Link(Width, Builder.Pin(Create, TEXT("Width")));
    Builder.Link(Height, Builder.Pin(Create, TEXT("Height")));
    Builder.Default(Create, TEXT("Format"), TEXT("RTF_RGBA16f"));
    Builder.Default(Create, TEXT("ClearColor"), TEXT("(R=0,G=0,B=0,A=1)"));
    Builder.Default(Create, TEXT("bAutoGenerateMipMaps"), TEXT("true"));
    // OnPaint is const; use the native UObject setter for the instance-owned target.
    auto* Store = Builder.Execute(UKismetSystemLibrary::StaticClass(), TEXT("SetObjectPropertyByName"), Exec);
    Builder.Link(Builder.Self(), Builder.Pin(Store, TEXT("Object")));
    Builder.Default(Store, TEXT("PropertyName"), TEXT("ObjectMip"));
    Builder.Link(Builder.Return(Create), Builder.Pin(Store, TEXT("Value")));
    auto* Ready = Builder.Node<UK2Node_ExecutionSequence>();
    Builder.Link(Exec, Ready->GetExecPin());
    Builder.Link(RecreateNeeded->GetElsePin(), Ready->GetExecPin());
    Exec = Ready->GetThenPinGivenIndex(0);
    auto* CurrentTarget = Builder.Get(TEXT("ObjectMip"));
    Builder.Texture(Builder.Get(TEXT("ObjectCopy")), TEXT("Source"), Source, Exec);
    Builder.Draw(TEXT("ObjectCopy"), CurrentTarget, Exec);
    Builder.Texture(OutputMID, TEXT("D2UObject"), Source, Exec);
    Builder.Texture(OutputMID, TEXT("D2UObjectMip"), CurrentTarget, Exec);
    auto* Complete = Builder.Node<UK2Node_ExecutionSequence>();
    Builder.Link(Exec, Complete->GetExecPin());
    Builder.Link(Branch->GetElsePin(), Complete->GetExecPin());
    Exec = Complete->GetThenPinGivenIndex(0);
}

FString SurfaceIdentity(const FTarget& Target)
{
    // Classes depend on the paint contract, not the material's static colors or image identity.
    const FMargin& Insets = Target.CaptureInsets;
    const FString Identity = FString::Printf(TEXT("%s|%d|%d|%.17g|%.17g|%.17g|%.17g"), *Target.Widget->GetClass()->GetPathName(),
                           Target.Mouse, Target.Sdf, double(Insets.Left), double(Insets.Top), double(Insets.Right),
                           double(Insets.Bottom));
    return Identity + Target.NativeRole + (Target.ObjectMip ? TEXT("|object-mip") : TEXT("")) +
        (Target.FillObject ? TEXT("|fill-object") : TEXT(""));
}

bool SaveSurfaceBlueprint(UWidgetBlueprint* Blueprint, const FTarget& Target, bool NewAsset, FString& Error)
{
    UPackage* Package = Blueprint->GetOutermost();
    const FString File =
        FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(Package, Blueprint, *File, Args))
    {
        Error = TEXT("Shader surface blueprint save failed: ") + File;
        return false;
    }
    FDesign2UMGGenerationManifestPayload Manifest;
    Manifest.bWholeAssetOwned = true;
    Manifest.Provider = TEXT("pencil-shader");
    Manifest.DocumentScope = SurfaceIdentity(Target);
    UWidget* Root = Blueprint->WidgetTree->RootWidget;
    Manifest.SourceRootNodeId = Manifest.DocumentScope + TEXT("/") + Root->GetName();
    Manifest.SourceRootName = Root->GetName();
    Manifest.TopLevelGenerated.Add({Manifest.SourceRootNodeId, Root->GetName()});
    Manifest.D2UOwnedNames.Add(Root->GetName());
    if (Design2UMGGenerationManifest::WriteSidecarForAsset(Package->GetName(), Manifest) != EDesign2UMGFileState::Saved)
    {
        Error = TEXT("Shader surface blueprint ownership record failed: ") + File;
        return false;
    }
    if (NewAsset)
        FAssetRegistryModule::AssetCreated(Blueprint);
    return true;
}

UWidgetBlueprint* PrepareSurfaceBlueprint(const FTarget& Target, const TCHAR* OwnershipError,
    const TCHAR* CreationError, bool& NewAsset, FString& Error)
{
    const FString Identity = SurfaceIdentity(Target);
    const FString Name = TEXT("D2U_ShaderSurface_") + FMD5::HashAnsiString(*Identity);
    const FString PackageName = TEXT("/Game/Design2UMG/GeneratedShaderWidgets/") + Name;
    auto* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn);
    NewAsset = Blueprint == nullptr;
    if (Blueprint)
    {
        FDesign2UMGGenerationManifestSidecar Manifest;
        if (Blueprint->GetOutermost()->IsDirty() ||
            !Design2UMGGenerationManifest::TryLoadSidecarForAsset(PackageName, Manifest, Error) ||
            Manifest.GeneratedAssetHash != Design2UMGGenerationManifest::ComputeSavedAssetHash(PackageName))
        {
            Error = FString(OwnershipError) + PackageName;
            return nullptr;
        }
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
        }
        TArray<FName> Variables;
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
            Variables.Add(Variable.VarName);
        FBlueprintEditorUtils::BulkRemoveMemberVariables(Blueprint, Variables);
    }
    else
    {
        Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
            UUserWidget::StaticClass(), CreatePackage(*PackageName), *Name, BPTYPE_Normal,
            UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
    }
    if (!Blueprint)
    {
        Error = FString(CreationError) + PackageName;
        return nullptr;
    }
    return Blueprint;
}

UWidgetBlueprint* BuildSurfaceBlueprint(const FTarget& Target, UMaterial* CopyMaterial, FString& Error)
{
    bool NewAsset = false;
    auto* Blueprint = PrepareSurfaceBlueprint(Target,
        TEXT("Generated shader surface has user changes or no ownership record: "),
        TEXT("Shader surface blueprint creation failed: "), NewAsset, Error);
    if (!Blueprint) return nullptr;
    auto* Slot = Cast<UNamedSlot>(Blueprint->WidgetTree->RootWidget);
    if (!Slot)
    {
        Slot = Blueprint->WidgetTree->ConstructWidget<UNamedSlot>(UNamedSlot::StaticClass(), SurfaceSlot);
        Blueprint->WidgetTree->RootWidget = Slot;
    }
    Slot->bIsVariable = true;
    Slot->bExposeOnInstanceOnly = true;
    PencilShaderSdf::FPassMaterials Programs;
    if (Target.Sdf)
    {
        if (!PencilShaderSdf::BuildMaterials(Programs, Error))
            return nullptr;
        TArray<FName> Names = TargetNames;
        Names.Append(PassNames());
        for (FName Variable : Names)
        {
            FEdGraphPinType Type;
            Type.PinCategory = UEdGraphSchema_K2::PC_Object;
            Type.PinSubCategoryObject = TargetNames.Contains(Variable) ? UTextureRenderTarget2D::StaticClass()
                                                                       : UMaterialInstanceDynamic::StaticClass();
            if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Variable, Type))
            {
                Error = TEXT("Shader surface state creation failed: ") + Variable.ToString();
                return nullptr;
            }
        }
    }
    if (Target.ObjectMip)
    {
        for (FName MemberName : {FName(TEXT("ObjectMip")), FName(TEXT("ObjectCopy")), FName(TEXT("ShaderOutput"))})
        {
            FEdGraphPinType Type;
            Type.PinCategory = UEdGraphSchema_K2::PC_Object;
            Type.PinSubCategoryObject = MemberName == TEXT("ObjectMip") ? UTextureRenderTarget2D::StaticClass()
                : MemberName == TEXT("ShaderOutput") ? UImage::StaticClass() : UMaterialInstanceDynamic::StaticClass();
            if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, MemberName, Type))
            {
                Error = TEXT("Object mip surface variable creation failed: ") + MemberName.ToString();
                return nullptr;
            }
        }
    }
    auto* Paint = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("OnPaint"), UEdGraph::StaticClass(),
                                                        UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Paint, false, UUserWidget::StaticClass());
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    for (UEdGraphNode* N : Paint->Nodes)
    {
        if (auto* E = Cast<UK2Node_FunctionEntry>(N))
            Entry = E;
        if (auto* R = Cast<UK2Node_FunctionResult>(N))
            Result = R;
    }
    if (!Entry || !Result)
    {
        Error = TEXT("Shader OnPaint has no native entry/result");
        return nullptr;
    }
    FGraph Builder(Paint, Error);
    UEdGraphPin* Exec = Builder.Pin(Entry, UEdGraphSchema_K2::PN_Then);
    auto* Widget = Surface(Builder, Target.Widget->GetClass(), Exec);
    auto* Geometry = Builder.Call(UWidget::StaticClass(), TEXT("GetCachedGeometry"));
    Builder.Link(Widget, Builder.Pin(Geometry, UEdGraphSchema_K2::PN_Self));
    auto* Size = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("GetLocalSize"));
    Builder.Link(Builder.Return(Geometry), Builder.Pin(Size, TEXT("Geometry")));
    auto* Dimensions = Builder.Break(Builder.Return(Size));
    const bool Retainer = Target.Widget->IsA<URetainerBox>();
    auto* Positive = Builder.Binary(
        TEXT("BooleanAND"),
        Builder.Binary(TEXT("Greater_DoubleDouble"), Builder.Pin(Dimensions, TEXT("X")),
                       Builder.Literal(Retainer ? Target.CaptureInsets.Left + Target.CaptureInsets.Right : 0)),
        Builder.Binary(TEXT("Greater_DoubleDouble"), Builder.Pin(Dimensions, TEXT("Y")),
                       Builder.Literal(Retainer ? Target.CaptureInsets.Top + Target.CaptureInsets.Bottom : 0)));
    auto* GetMID = Builder.Execute(
        Target.Widget->GetClass(),
        Target.Widget->IsA<URetainerBox>() ? TEXT("GetEffectMaterial") : TEXT("GetDynamicMaterial"), Exec);
    Builder.Link(Widget, Builder.Pin(GetMID, UEdGraphSchema_K2::PN_Self));
    UEdGraphPin* CaptureMID = Builder.Return(GetMID);
    UEdGraphPin* OutputMID = CaptureMID;
    if (Target.ObjectMip)
    {
        WireOutputProperties(Builder, Widget, Exec);
        auto* OutputMaterial = Builder.Execute(UImage::StaticClass(), TEXT("GetDynamicMaterial"), Exec);
        Builder.Link(Builder.Get(TEXT("ShaderOutput")), Builder.Pin(OutputMaterial, UEdGraphSchema_K2::PN_Self));
        OutputMID = Builder.Return(OutputMaterial);
    }
    auto* Valid = Builder.Call(UKismetSystemLibrary::StaticClass(), TEXT("IsValid"));
    Builder.Link(OutputMID, Builder.Pin(Valid, TEXT("Object")));
    auto* Branch = Builder.Node<UK2Node_IfThenElse>();
    Builder.Link(Exec, Branch->GetExecPin());
    Builder.Link(Builder.Binary(TEXT("BooleanAND"), Positive, Builder.Return(Valid)), Branch->GetConditionPin());
    Builder.Link(Branch->GetElsePin(), Builder.Pin(Result, UEdGraphSchema_K2::PN_Execute));
    Exec = Branch->GetThenPin();
    auto* PaintSize =
        BindParameters(Builder, Target, OutputMID, Builder.Return(Geometry), Builder.Return(Size), Exec);
    if (Target.ObjectMip)
        WireObjectMipPaint(Builder, CaptureMID, OutputMID, Exec);
    if (Target.Sdf)
    {
        WireSdfPaint(Builder, Blueprint, Target, OutputMID, Builder.Return(Geometry), PaintSize, Exec);
    }
    Builder.Link(Exec, Builder.Pin(Result, UEdGraphSchema_K2::PN_Execute));
    if (Entry->FindPin(TEXT("Context")) && Result->FindPin(TEXT("Context")))
    {
        Builder.Link(Entry->FindPin(TEXT("Context")), Result->FindPin(TEXT("Context")));
    }
    if (Target.Sdf || Target.ObjectMip)
    {
        auto* Events = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("EventGraph"), UEdGraph::StaticClass(),
                                                             UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Events);
        FGraph Lifecycle(Events, Error);
        if (Target.Sdf)
            WireSdfLifecycle(Lifecycle, Target, Programs);
        if (Target.ObjectMip)
            WireObjectMipLifecycle(Lifecycle, CopyMaterial);
    }
    if (!Error.IsEmpty())
        return nullptr;
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status != BS_UpToDate)
    {
        Error = TEXT("Native shader surface blueprint has compilation errors or warnings: ") + Blueprint->GetOutermost()->GetName();
        return nullptr;
    }
    return SaveSurfaceBlueprint(Blueprint, Target, NewAsset, Error) ? Blueprint : nullptr;
}
} // namespace

namespace
{
bool ReplaceInTree(UWidgetBlueprint* Blueprint, UWidget* Original, UWidget* Replacement, FString& Error)
{
    UPanelWidget* Parent = Original->GetParent();
    // Named-slot bindings are also serialized ownership, not just the live panel child.
    if (!Parent || Parent->IsA<UNamedSlot>())
    {
        INamedSlotInterface* Host = nullptr;
        FName SlotName;
        Blueprint->WidgetTree->ForEachWidget(
            [&](UWidget* Widget)
            {
                if (Host)
                    return;
                if (auto* Candidate = Cast<INamedSlotInterface>(Widget))
                {
                    TArray<FName> Names;
                    Candidate->GetSlotNames(Names);
                    for (FName Name : Names)
                        if (Candidate->GetContentForSlot(Name) == Original)
                        {
                            Host = Candidate;
                            SlotName = Name;
                            break;
                        }
                }
            });
        if (Host)
        {
            Host->SetContentForSlot(SlotName, Replacement);
            if (Host->GetContentForSlot(SlotName) == Replacement)
                return true;
            Error = TEXT("Shader surface could not retain its named-slot binding");
            return false;
        }
    }
    if (Parent && Parent->ReplaceChildAt(Parent->GetChildIndex(Original), Replacement))
        return true;
    if (!Parent && Blueprint->WidgetTree->RootWidget == Original)
    {
        Blueprint->WidgetTree->RootWidget = Replacement;
        return true;
    }
    Error = TEXT("Shader surface could not retain its panel, root or named-slot placement");
    return false;
}


const FName NativeProducerRole(TEXT("D2UNativeProducer"));
const FName BackgroundBoundaryRole(TEXT("D2UBackgroundBoundary"));
bool HasNativeRole(const UUserWidget* Widget, FName Role)
{
    return FindFProperty<FObjectPropertyBase>(Widget->GetClass(), Role) != nullptr;
}
UWidgetBlueprint* CreateBoundaryBlueprint(const FTarget& Target, bool& NewAsset, FString& Error)
{
    auto* Blueprint = PrepareSurfaceBlueprint(Target,
        TEXT("Generated paint-boundary asset has local edits: "),
        TEXT("Native paint-boundary blueprint could not be created: "), NewAsset, Error);
    if (!Blueprint) return nullptr;
    Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, TEXT("WidgetTree"), RF_Transactional);
    return Blueprint;
}
bool BoundaryObject(UWidgetBlueprint* Blueprint, FName Name, UClass* Class, FString& Error)
{
    FEdGraphPinType Type;
    Type.PinCategory = UEdGraphSchema_K2::PC_Object;
    Type.PinSubCategoryObject = Class;
    if (FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type)) return true;
    Error = TEXT("Paint-boundary variable could not be created: ") + Name.ToString();
    return false;
}
bool CompileBoundaryState(UWidgetBlueprint* Blueprint, FString& Error)
{
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
    if (Blueprint->Status == BS_UpToDate && Blueprint->SkeletonGeneratedClass) return true;
    Error = TEXT("Native paint-boundary declarations failed to compile: ") + Blueprint->GetPathName();
    return false;
}
bool FinishBoundary(UWidgetBlueprint* Blueprint, const FTarget& Target, bool NewAsset, FString& Error)
{
    return Error.IsEmpty() && CompileBoundaryState(Blueprint, Error)
        && SaveSurfaceBlueprint(Blueprint, Target, NewAsset, Error);
}
UEdGraph* BoundaryEvents(UWidgetBlueprint* Blueprint)
{
    auto* Events = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("EventGraph"),
        UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Events);
    return Events;
}
UEdGraph* BoundaryPaint(UWidgetBlueprint* Blueprint, UK2Node_FunctionEntry*& Entry, UK2Node_FunctionResult*& Result, FString& Error)
{
    auto* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("OnPaint"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, false, UUserWidget::StaticClass());
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (auto* E = Cast<UK2Node_FunctionEntry>(Node)) Entry = E;
        if (auto* R = Cast<UK2Node_FunctionResult>(Node)) Result = R;
    }
    if (!Entry || !Result) Error = TEXT("Paint boundary has no native OnPaint signature");
    return Graph;
}
void CompleteBoundaryPaint(FGraph& Builder, UK2Node_FunctionResult* Result, UEdGraphPin* Exec)
{
    Builder.Link(Exec, Builder.Pin(Result, UEdGraphSchema_K2::PN_Execute));
}
UEdGraphPin* CurrentPaintFrame(FGraph& Builder)
{
    auto* Count = Builder.Call(UKismetSystemLibrary::StaticClass(), TEXT("GetFrameCount"));
    UEdGraphPin* Remaining = Builder.Return(Count);
    UEdGraphPin* Words[4];
    // Four exact 16-bit components preserve every int64 frame bit in the native MID.
    for (int32 Index = 0; Index < 4; ++Index)
    {
        auto* Word = Builder.Math(TEXT("Percent_Int64Int64"));
        Builder.Link(Remaining, Builder.Pin(Word, TEXT("A")));
        Builder.Default(Word, TEXT("B"), TEXT("65536"));
        auto* Component = Builder.Math(TEXT("Conv_Int64ToDouble"));
        Builder.Link(Builder.Return(Word), Builder.Pin(Component, TEXT("InInt")));
        Words[Index] = Builder.Return(Component);
        if (Index < 3)
        {
            auto* Next = Builder.Math(TEXT("Divide_Int64Int64"));
            Builder.Link(Remaining, Builder.Pin(Next, TEXT("A")));
            Builder.Default(Next, TEXT("B"), TEXT("65536"));
            Remaining = Builder.Return(Next);
        }
    }
    return Builder.Color(Words[0], Words[1], Words[2], Words[3]);
}
UWidgetBlueprint* BuildProducer(const FTarget& Target, UMaterial* Opacity, FString& Error)
{
    bool NewAsset = false;
    auto* Blueprint = CreateBoundaryBlueprint(Target, NewAsset, Error);
    if (!Blueprint || !BoundaryObject(Blueprint, NativeProducerRole, UWidget::StaticClass(), Error)) return nullptr;
    FEdGraphPinType Float;
    Float.PinCategory = UEdGraphSchema_K2::PC_Real;
    Float.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("SourceOpacity"), Float, TEXT("1"))
        || !BoundaryObject(Blueprint, TEXT("PaintState"), UMaterialInstanceDynamic::StaticClass(), Error))
    {
        Error = TEXT("Native producer opacity/frame declaration failed");
        return nullptr;
    }
    auto* Root = Blueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
    auto* Capture = Blueprint->WidgetTree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass(), TEXT("NativeCapture"));
    auto* Padding = Blueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CapturePadding"));
    auto* Content = Blueprint->WidgetTree->ConstructWidget<UNamedSlot>(UNamedSlot::StaticClass(), SurfaceSlot);
    Capture->bIsVariable = true;
    Capture->SetEffectMaterial(Opacity);
    Capture->SetTextureParameter(TEXT("Texture"));
    Capture->SetRetainRendering(true);
    Capture->SetRenderingPhase(0, 1);
    Padding->SetBrushColor(FLinearColor::Transparent);
    Padding->SetPadding(Target.CaptureInsets);
    Padding->SetHorizontalAlignment(HAlign_Fill);
    Padding->SetVerticalAlignment(VAlign_Fill);
    Padding->SetContent(Content);
    Capture->SetContent(Padding);
    auto* Slot = Root->AddChildToOverlay(Capture);
    Slot->SetHorizontalAlignment(HAlign_Fill);
    Slot->SetVerticalAlignment(VAlign_Fill);
    Slot->SetPadding(FMargin(-Target.CaptureInsets.Left, -Target.CaptureInsets.Top,
        -Target.CaptureInsets.Right, -Target.CaptureInsets.Bottom));
    Blueprint->WidgetTree->RootWidget = Root;
    if (!CompileBoundaryState(Blueprint, Error)) return nullptr;
    FGraph Lifecycle(BoundaryEvents(Blueprint), Error);
    auto* Exec = Lifecycle.Event(TEXT("PreConstruct"));
    auto* MID = Lifecycle.Execute(URetainerBox::StaticClass(), TEXT("GetEffectMaterial"), Exec);
    Lifecycle.Link(Lifecycle.Get(TEXT("NativeCapture")), Lifecycle.Pin(MID, UEdGraphSchema_K2::PN_Self));
    auto* Scalar = Lifecycle.Execute(UMaterialInstanceDynamic::StaticClass(), TEXT("SetScalarParameterValue"), Exec);
    Lifecycle.Link(Lifecycle.Return(MID), Lifecycle.Pin(Scalar, UEdGraphSchema_K2::PN_Self));
    Lifecycle.Default(Scalar, TEXT("ParameterName"), TEXT("D2UGroupOpacity"));
    Lifecycle.Link(Lifecycle.Get(TEXT("SourceOpacity")), Lifecycle.Pin(Scalar, TEXT("Value")));
    Lifecycle.SetObject(TEXT("PaintState"), Lifecycle.Return(MID), Exec);
    Exec = Lifecycle.Parameter(Exec, Lifecycle.Get(TEXT("PaintState")), TEXT("D2UPaintFrame"),
        Lifecycle.Color(Lifecycle.Literal(-1), Lifecycle.Literal(-1), Lifecycle.Literal(-1), Lifecycle.Literal(-1)));
    auto* Destruct = Lifecycle.Event(TEXT("Destruct"));
    Lifecycle.SetObject(TEXT("PaintState"), nullptr, Destruct);
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    auto* Paint = BoundaryPaint(Blueprint, Entry, Result, Error);
    if (!Error.IsEmpty()) return nullptr;
    FGraph Painted(Paint, Error);
    auto* PaintedExec = Painted.Parameter(Painted.Pin(Entry, UEdGraphSchema_K2::PN_Then),
        Painted.Get(TEXT("PaintState")), TEXT("D2UPaintFrame"), CurrentPaintFrame(Painted));
    CompleteBoundaryPaint(Painted, Result, PaintedExec);
    return FinishBoundary(Blueprint, Target, NewAsset, Error) ? Blueprint : nullptr;
}
UEdGraphPin* PaintGeometry(FGraph& Builder, UEdGraphPin* Widget)
{
    auto* Geometry = Builder.Call(UWidget::StaticClass(), TEXT("GetPaintSpaceGeometry"));
    Builder.Link(Widget, Builder.Pin(Geometry, UEdGraphSchema_K2::PN_Self));
    return Builder.Return(Geometry);
}
UEdGraphPin* GeometrySize(FGraph& Builder, UEdGraphPin* Geometry)
{
    auto* Size = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("GetLocalSize"));
    Builder.Link(Geometry, Builder.Pin(Size, TEXT("Geometry")));
    return Builder.Return(Size);
}
UEdGraphPin* ProducerCapture(FGraph& Builder, UEdGraphPin* Producer)
{
    return Builder.ObjectMember(Producer, Cast<UClass>(Producer->PinType.PinSubCategoryObject.Get()), TEXT("NativeCapture"));
}
UEdGraphPin* ProducerTexture(FGraph& Builder, UEdGraphPin* Producer, UEdGraphPin*& Exec)
{
    auto* MID = Builder.Execute(URetainerBox::StaticClass(), TEXT("GetEffectMaterial"), Exec);
    Builder.Link(ProducerCapture(Builder, Producer), Builder.Pin(MID, UEdGraphSchema_K2::PN_Self));
    return Builder.ReadTexture(Builder.Return(MID), TEXT("Texture"), Exec);
}
struct FPaintBoundary { UWidget* Widget = nullptr; bool Producer = false; };
using FPaintChain = TArray<FPaintBoundary>;
TPair<int32, int32> SeparatePaintSpaces(const FPaintChain& Canvas, const FPaintChain& Texture)
{
    int32 C = Canvas.Num(), T = Texture.Num();
    while (C && T && Canvas[C - 1].Widget == Texture[T - 1].Widget) { --C; --T; }
    return {C, T};
}
FName SpaceName(FName Input, bool Canvas, int32 Index)
{
    return *FString::Printf(TEXT("%s%sSpace%d"), *Input.ToString(), Canvas ? TEXT("Canvas") : TEXT("Texture"), Index);
}
FString PaintSpaceIdentity(const FPaintChain& Canvas, const FPaintChain& Texture)
{
    const auto Counts = SeparatePaintSpaces(Canvas, Texture);
    FString Result = TEXT("|space:");
    for (int32 Index = 0; Index < Counts.Key; ++Index)
        Result += Canvas[Index].Producer ? TEXT("p(") + Canvas[Index].Widget->GetClass()->GetPathName() + TEXT(")") : TEXT("r");
    Result += TEXT(":");
    for (int32 Index = 0; Index < Counts.Value; ++Index)
        Result += Texture[Index].Producer ? TEXT("p(") + Texture[Index].Widget->GetClass()->GetPathName() + TEXT(")") : TEXT("r");
    return Result;
}
UEdGraphPin* BoundaryGeometry(FGraph& Builder, FName Name, const FPaintBoundary& Boundary)
{
    auto* Widget = Builder.Get(Name);
    return PaintGeometry(Builder, Boundary.Producer
        ? ProducerCapture(Builder, Builder.Cast(Boundary.Widget->GetClass(), Widget)) : Widget);
}
UEdGraphPin* ChangePaintSpace(FGraph& Builder, UEdGraphPin* Point, UEdGraphPin* Geometry, bool ToOuter)
{
    // Retainer children use scale/translation; the finished target receives the full transform.
    auto* Origin = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("LocalToAbsolute"));
    Builder.Link(Geometry, Builder.Pin(Origin, TEXT("Geometry")));
    Builder.Link(Builder.Vector(0, 0), Builder.Pin(Origin, TEXT("LocalCoordinate")));
    auto* Scale = Builder.Math(TEXT("MakeVector2D"));
    for (int32 Axis = 0; Axis < 2; ++Axis)
    {
        auto* Unit = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("LocalToAbsolute"));
        Builder.Link(Geometry, Builder.Pin(Unit, TEXT("Geometry")));
        Builder.Link(Builder.Vector(Axis == 0 ? 1 : 0, Axis == 1 ? 1 : 0), Builder.Pin(Unit, TEXT("LocalCoordinate")));
        auto* Length = Builder.Math(TEXT("VSize2D"));
        Builder.Link(Builder.Binary(TEXT("Subtract_Vector2DVector2D"), Builder.Return(Unit), Builder.Return(Origin)), Builder.Pin(Length, TEXT("A")));
        auto* Value = Builder.Return(Length);
        Builder.Link(Builder.Select(Builder.Binary(TEXT("Greater_DoubleDouble"), Value, Builder.Literal(0)), Value, Builder.Literal(1)),
            Builder.Pin(Scale, Axis == 0 ? TEXT("X") : TEXT("Y")));
    }
    if (ToOuter)
    {
        auto* Local = Builder.Binary(TEXT("Divide_Vector2DVector2D"),
            Builder.Binary(TEXT("Subtract_Vector2DVector2D"), Point, Builder.Return(Origin)), Builder.Return(Scale));
        auto* Absolute = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("LocalToAbsolute"));
        Builder.Link(Geometry, Builder.Pin(Absolute, TEXT("Geometry")));
        Builder.Link(Local, Builder.Pin(Absolute, TEXT("LocalCoordinate")));
        return Builder.Return(Absolute);
    }
    auto* Local = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("AbsoluteToLocal"));
    Builder.Link(Geometry, Builder.Pin(Local, TEXT("Geometry")));
    Builder.Link(Point, Builder.Pin(Local, TEXT("AbsoluteCoordinate")));
    return Builder.Binary(TEXT("Add_Vector2DVector2D"), Builder.Return(Origin),
        Builder.Binary(TEXT("Multiply_Vector2DVector2D"), Builder.Return(Local), Builder.Return(Scale)));
}

void Affine(FGraph& Builder, UEdGraphPin* MID, FName U, FName V,
    UEdGraphPin* CanvasGeometry, UEdGraphPin* TextureGeometry, FName Input,
    const FPaintChain& CanvasSpace, const FPaintChain& TextureSpace, UEdGraphPin*& Exec)
{
    auto* CanvasSize = GeometrySize(Builder, CanvasGeometry);
    auto* TextureDimensions = Builder.Break(GeometrySize(Builder, TextureGeometry));
    auto* SafeSize = Builder.Math(TEXT("MakeVector2D"));
    for (FName Axis : {FName(TEXT("X")), FName(TEXT("Y"))})
    {
        auto* Size = Builder.Pin(TextureDimensions, Axis);
        Builder.Link(Builder.Select(Builder.Binary(TEXT("Greater_DoubleDouble"), Size, Builder.Literal(0)), Size, Builder.Literal(1)), Builder.Pin(SafeSize, Axis));
    }
    UEdGraphPin* Points[3];
    const FVector2D Corners[] = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1)};
    const auto Counts = SeparatePaintSpaces(CanvasSpace, TextureSpace);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto* Absolute = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("LocalToAbsolute"));
        Builder.Link(CanvasGeometry, Builder.Pin(Absolute, TEXT("Geometry")));
        Builder.Link(Builder.Binary(TEXT("Multiply_Vector2DVector2D"), CanvasSize,
            Builder.Vector(Corners[Index].X, Corners[Index].Y)), Builder.Pin(Absolute, TEXT("LocalCoordinate")));
        auto* Point = Builder.Return(Absolute);
        for (int32 Space = 0; Space < Counts.Key; ++Space)
            Point = ChangePaintSpace(Builder, Point, BoundaryGeometry(Builder, SpaceName(Input, true, Space), CanvasSpace[Space]), true);
        for (int32 Space = Counts.Value - 1; Space >= 0; --Space)
            Point = ChangePaintSpace(Builder, Point, BoundaryGeometry(Builder, SpaceName(Input, false, Space), TextureSpace[Space]), false);
        auto* Local = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("AbsoluteToLocal"));
        Builder.Link(TextureGeometry, Builder.Pin(Local, TEXT("Geometry")));
        Builder.Link(Point, Builder.Pin(Local, TEXT("AbsoluteCoordinate")));
        Points[Index] = Builder.Binary(TEXT("Divide_Vector2DVector2D"), Builder.Return(Local), Builder.Return(SafeSize));
    }
    auto* Origin = Builder.Break(Points[0]);
    auto* X = Builder.Break(Builder.Binary(TEXT("Subtract_Vector2DVector2D"), Points[1], Points[0]));
    auto* Y = Builder.Break(Builder.Binary(TEXT("Subtract_Vector2DVector2D"), Points[2], Points[0]));
    Exec = Builder.Parameter(Exec, MID, U, Builder.Color(Builder.Pin(X, TEXT("X")), Builder.Pin(Y, TEXT("X")), Builder.Pin(Origin, TEXT("X"))));
    Exec = Builder.Parameter(Exec, MID, V, Builder.Color(Builder.Pin(X, TEXT("Y")), Builder.Pin(Y, TEXT("Y")), Builder.Pin(Origin, TEXT("Y"))));
}
void BoundaryTargets(FGraph& Builder, const TArray<FName>& Targets, UMaterial* Composite)
{
    auto* Exec = Builder.Event(TEXT("PreConstruct"));
    for (FName Name : Targets)
    {
        auto* Release = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ReleaseRenderTarget2D"), Exec);
        Builder.Link(Builder.Get(Name), Builder.Pin(Release, TEXT("TextureRenderTarget")));
        auto* Create = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("CreateRenderTarget2D"), Exec);
        Builder.Link(Builder.Self(), Builder.Pin(Create, TEXT("WorldContextObject")));
        Builder.Default(Create, TEXT("Width"), TEXT("1"));
        Builder.Default(Create, TEXT("Height"), TEXT("1"));
        Builder.Default(Create, TEXT("Format"), TEXT("RTF_RGBA16f"));
        Builder.Default(Create, TEXT("ClearColor"), TEXT("(R=0,G=0,B=0,A=1)"));
        Builder.Default(Create, TEXT("bAutoGenerateMipMaps"), TEXT("true"));
        Builder.SetObject(Name, Builder.Return(Create), Exec);
    }
    auto* MID = Builder.Execute(UKismetMaterialLibrary::StaticClass(), TEXT("CreateDynamicMaterialInstance"), Exec);
    Builder.Link(Builder.Self(), Builder.Pin(MID, TEXT("WorldContextObject")));
    Builder.Pin(MID, TEXT("Parent"))->DefaultObject = Composite;
    Builder.SetObject(TEXT("CompositeMID"), Builder.Return(MID), Exec);
    Exec = Builder.Event(TEXT("Destruct"));
    for (FName Name : Targets)
    {
        auto* Release = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ReleaseRenderTarget2D"), Exec);
        Builder.Link(Builder.Get(Name), Builder.Pin(Release, TEXT("TextureRenderTarget")));
        Builder.SetObject(Name, nullptr, Exec);
    }
    Builder.SetObject(TEXT("CompositeMID"), nullptr, Exec);
}
void SizeBoundaryTargets(FGraph& Builder, const TArray<FName>& Targets, UEdGraphPin* Geometry, UEdGraphPin*& Exec)
{
    auto* LocalSize = Builder.Break(GeometrySize(Builder, Geometry));
    auto* Origin = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("LocalToAbsolute"));
    Builder.Link(Geometry, Builder.Pin(Origin, TEXT("Geometry")));
    Builder.Link(Builder.Vector(0, 0), Builder.Pin(Origin, TEXT("LocalCoordinate")));
    UEdGraphPin* Dimensions[2];
    for (int32 Index = 0; Index < 2; ++Index)
    {
        auto* Local = Builder.Math(TEXT("MakeVector2D"));
        const FName Axis = Index == 0 ? FName(TEXT("X")) : FName(TEXT("Y"));
        Builder.Link(Builder.Pin(LocalSize, Axis), Builder.Pin(Local, Axis));
        auto* Absolute = Builder.Call(USlateBlueprintLibrary::StaticClass(), TEXT("LocalToAbsolute"));
        Builder.Link(Geometry, Builder.Pin(Absolute, TEXT("Geometry")));
        Builder.Link(Builder.Return(Local), Builder.Pin(Absolute, TEXT("LocalCoordinate")));
        auto* Length = Builder.Math(TEXT("VSize2D"));
        Builder.Link(Builder.Binary(TEXT("Subtract_Vector2DVector2D"), Builder.Return(Absolute), Builder.Return(Origin)), Builder.Pin(Length, TEXT("A")));
        Dimensions[Index] = Builder.Return(Length);
    }
    for (FName Name : Targets)
    {
        auto* Resize = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ResizeRenderTarget2D"), Exec);
        Builder.Link(Builder.Get(Name), Builder.Pin(Resize, TEXT("TextureRenderTarget")));
        for (int32 Index = 0; Index < 2; ++Index)
        {
            auto* Round = Builder.Math(TEXT("FCeil"));
            Builder.Link(Dimensions[Index], Builder.Pin(Round, TEXT("A")));
            Builder.Link(Builder.Return(Round), Builder.Pin(Resize, Index == 0 ? TEXT("Width") : TEXT("Height")));
        }
    }
}
void ClearBoundary(FGraph& Builder, UEdGraphPin* Target, UEdGraphPin*& Exec, bool EmptyDestination = false)
{
    auto* Clear = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("ClearRenderTarget2D"), Exec);
    Builder.Link(Builder.Self(), Builder.Pin(Clear, TEXT("WorldContextObject")));
    Builder.Link(Target, Builder.Pin(Clear, TEXT("TextureRenderTarget")));
    Builder.Default(Clear, TEXT("ClearColor"), EmptyDestination ? TEXT("(R=0,G=0,B=0,A=0)") : TEXT("(R=0,G=0,B=0,A=1)"));
}
void CompositeDraw(FGraph& Builder, UEdGraphPin* Target, UEdGraphPin*& Exec)
{
    ClearBoundary(Builder, Target, Exec);
    auto* Draw = Builder.Execute(UKismetRenderingLibrary::StaticClass(), TEXT("DrawMaterialToRenderTarget"), Exec);
    Builder.Link(Builder.Self(), Builder.Pin(Draw, TEXT("WorldContextObject")));
    Builder.Link(Target, Builder.Pin(Draw, TEXT("TextureRenderTarget")));
    Builder.Link(Builder.Get(TEXT("CompositeMID")), Builder.Pin(Draw, TEXT("Material")));
}
struct FBackgroundContext
{
    URetainerBox* Consumer = nullptr;
    UWidget* Content = nullptr;
    FTarget Shader;
    int32 BlendMode = 0;
    TArray<UWidget*> Prefix;
    UUserWidget* ObjectProducer = nullptr;
    UUserWidget* SourceProducer = nullptr;
    bool ObjectSampling = false;
    bool DirectBackground = false;
    FPaintChain PaintSpace;
};
FName PrefixName(int32 Index) { return *FString::Printf(TEXT("Prefix%d"), Index); }
bool PaintSpaceVariables(UWidgetBlueprint* Blueprint, FName Input, const FPaintChain& Canvas, const FPaintChain& Texture, FString& Error)
{
    const auto Counts = SeparatePaintSpaces(Canvas, Texture);
    for (int32 Index = 0; Index < Counts.Key; ++Index)
        if (!BoundaryObject(Blueprint, SpaceName(Input, true, Index), UWidget::StaticClass(), Error)) return false;
    for (int32 Index = 0; Index < Counts.Value; ++Index)
        if (!BoundaryObject(Blueprint, SpaceName(Input, false, Index), UWidget::StaticClass(), Error)) return false;
    return true;
}
void ProducerComposite(FGraph& Builder, UUserWidget* Producer, FName Variable, UEdGraphPin* Destination,
    UEdGraphPin* Geometry, UEdGraphPin* Output, int32 Mode, const FPaintChain& CanvasSpace,
    const FPaintChain& TextureSpace, UEdGraphPin*& Exec)
{
    auto* Instance = Builder.Cast(Producer->GetClass(), Builder.Get(Variable));
    auto* MID = Builder.Get(TEXT("CompositeMID"));
    Builder.Texture(MID, TEXT("D2USource"), ProducerTexture(Builder, Instance, Exec), Exec);
    Builder.Texture(MID, TEXT("D2UDestination"), Destination, Exec);
    Builder.Scalar(MID, TEXT("D2UBlendMode"), Mode, Exec);
    auto* PaintedFrame = Builder.ReadVector(Builder.ObjectMember(Instance, Producer->GetClass(), TEXT("PaintState")),
        TEXT("D2UPaintFrame"), Exec);
    auto* Scalar = Builder.Execute(UMaterialInstanceDynamic::StaticClass(), TEXT("SetScalarParameterValue"), Exec);
    Builder.Link(MID, Builder.Pin(Scalar, UEdGraphSchema_K2::PN_Self));
    Builder.Default(Scalar, TEXT("ParameterName"), TEXT("D2USourceOpacity"));
    auto* Visible = Builder.Call(UWidget::StaticClass(), TEXT("IsVisible"));
    Builder.Link(Builder.ObjectMember(Instance, Producer->GetClass(), NativeProducerRole), Builder.Pin(Visible, UEdGraphSchema_K2::PN_Self));
    auto* Current = Builder.Binary(TEXT("BooleanAND"), Builder.Return(Visible),
        Builder.Binary(TEXT("EqualEqual_LinearColorLinearColor"), PaintedFrame, CurrentPaintFrame(Builder)));
    Builder.Link(Builder.Select(Current, Builder.ObjectMember(Instance, Producer->GetClass(), TEXT("SourceOpacity")), Builder.Literal(0)), Builder.Pin(Scalar, TEXT("Value")));
    Affine(Builder, MID, TEXT("D2USourceU"), TEXT("D2USourceV"), Geometry,
        PaintGeometry(Builder, ProducerCapture(Builder, Instance)), Variable, CanvasSpace, TextureSpace, Exec);
    CompositeDraw(Builder, Output, Exec);
}

UWidgetBlueprint* BuildBackgroundBefore(const FBackgroundContext& Context, const TArray<UUserWidget*>& Prefix,
    UMaterial* Composite, TMap<FString, UWidgetBlueprint*>& Classes,
    const TMap<UUserWidget*, FPaintChain>& Spaces, FString& Error)
{
    FTarget Target;
    Target.Widget = Context.Content;
    Target.CaptureInsets = Context.Shader.CaptureInsets;
    Target.NativeRole = TEXT("|background-before|native-affine-prefix");
    for (auto* Producer : Prefix)
        Target.NativeRole += TEXT("|") + Producer->GetClass()->GetPathName() + PaintSpaceIdentity(Context.PaintSpace, Spaces.FindChecked(Producer));
    if (Context.Shader.Widget) Target.NativeRole += TEXT("|shader:") + Context.Shader.Widget->GetClass()->GetPathName();
    if (Context.ObjectProducer) Target.NativeRole += TEXT("|object:") + Context.ObjectProducer->GetClass()->GetPathName()
        + PaintSpaceIdentity(Context.PaintSpace, Spaces.FindChecked(Context.ObjectProducer));
    Target.NativeRole += Context.DirectBackground ? TEXT("|direct") : TEXT("|consumer");
    const FString Key = SurfaceIdentity(Target);
    if (auto* Existing = Classes.FindRef(Key)) return Existing;
    bool NewAsset = false;
    auto* Blueprint = CreateBoundaryBlueprint(Target, NewAsset, Error);
    if (!Blueprint || !BoundaryObject(Blueprint, BackgroundBoundaryRole, UWidget::StaticClass(), Error)) return nullptr;
    Blueprint->WidgetTree->RootWidget = Blueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MarkerRoot"));
    TArray<FName> Targets = {TEXT("BackgroundA"), TEXT("BackgroundB")};
    if (Context.ObjectProducer) Targets.Add(TEXT("ObjectMip"));
    for (FName Name : Targets)
        if (!BoundaryObject(Blueprint, Name, UTextureRenderTarget2D::StaticClass(), Error)) return nullptr;
    if (!BoundaryObject(Blueprint, TEXT("Background"), UTextureRenderTarget2D::StaticClass(), Error)
        || !BoundaryObject(Blueprint, TEXT("CompositeMID"), UMaterialInstanceDynamic::StaticClass(), Error)) return nullptr;
    for (int32 Index = 0; Index < Prefix.Num(); ++Index)
        if (!BoundaryObject(Blueprint, PrefixName(Index), UUserWidget::StaticClass(), Error)
            || !PaintSpaceVariables(Blueprint, PrefixName(Index), Context.PaintSpace, Spaces.FindChecked(Prefix[Index]), Error)) return nullptr;
    if (Context.Shader.Widget && !BoundaryObject(Blueprint, TEXT("Shader"), UWidget::StaticClass(), Error)) return nullptr;
    if (Context.ObjectProducer && (!BoundaryObject(Blueprint, TEXT("ObjectProducer"), UUserWidget::StaticClass(), Error)
        || !PaintSpaceVariables(Blueprint, TEXT("ObjectProducer"), Context.PaintSpace, Spaces.FindChecked(Context.ObjectProducer), Error))) return nullptr;
    if (!CompileBoundaryState(Blueprint, Error)) return nullptr;
    FGraph Lifecycle(BoundaryEvents(Blueprint), Error);
    BoundaryTargets(Lifecycle, Targets, Composite);
    auto* Alias = Lifecycle.Event(TEXT("PreConstruct"));
    // Every prefix writes exactly once; the final ping-pong object is stable for this class.
    Lifecycle.SetObject(TEXT("Background"), Lifecycle.Get(Prefix.Num() % 2 ? TEXT("BackgroundA") : TEXT("BackgroundB")), Alias);
    Alias = Lifecycle.Event(TEXT("Destruct"));
    Lifecycle.SetObject(TEXT("Background"), nullptr, Alias);
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    auto* Paint = BoundaryPaint(Blueprint, Entry, Result, Error);
    if (!Error.IsEmpty()) return nullptr;
    FGraph Builder(Paint, Error);
    auto* Exec = Builder.Pin(Entry, UEdGraphSchema_K2::PN_Then);
    auto* Geometry = PaintGeometry(Builder, Context.DirectBackground ? Builder.Self() : Builder.Get(BackgroundBoundaryRole));
    SizeBoundaryTargets(Builder, Targets, Geometry, Exec);
    ClearBoundary(Builder, Builder.Get(TEXT("BackgroundA")), Exec, true);
    // A clear updates only the base level; draw the empty destination once so even
    // an empty background has this paint's complete automatic mip chain.
    Builder.Texture(Builder.Get(TEXT("CompositeMID")), TEXT("D2UDestination"), Builder.Get(TEXT("BackgroundA")), Exec);
    Builder.Scalar(Builder.Get(TEXT("CompositeMID")), TEXT("D2USourceOpacity"), 0, Exec);
    CompositeDraw(Builder, Builder.Get(TEXT("BackgroundB")), Exec);
    FName Previous(TEXT("BackgroundB"));
    for (int32 Index = 0; Index < Prefix.Num(); ++Index)
    {
        const FName Next = Previous == TEXT("BackgroundA") ? FName(TEXT("BackgroundB")) : FName(TEXT("BackgroundA"));
        ProducerComposite(Builder, Prefix[Index], PrefixName(Index), Builder.Get(Previous), Geometry, Builder.Get(Next), 0,
            Context.PaintSpace, Spaces.FindChecked(Prefix[Index]), Exec);
        Previous = Next;
    }
    if (Context.Shader.Widget)
    {
        auto* Shader = Builder.Cast(Context.Shader.Widget->GetClass(), Builder.Get(TEXT("Shader")));
        auto* Material = Builder.Execute(Context.Shader.Widget->GetClass(),
            Context.Shader.Widget->IsA<URetainerBox>() ? TEXT("GetEffectMaterial") : TEXT("GetDynamicMaterial"), Exec);
        Builder.Link(Shader, Builder.Pin(Material, UEdGraphSchema_K2::PN_Self));
        auto* MID = Builder.Return(Material);
        Builder.Texture(MID, TEXT("D2UBackground"), Builder.Get(Previous), Exec);
        Builder.Texture(MID, TEXT("D2UBackgroundMip"), Builder.Get(Previous), Exec);
        auto* Logical = Builder.Break(GeometrySize(Builder, Geometry));
        const auto Row = [&](FName Axis, double Before, double After)
        {
            UEdGraphPin* Scale;
            UEdGraphPin* Offset;
            if (Context.Shader.Widget->IsA<URetainerBox>())
            {
                auto* Size = Builder.Pin(Logical, Axis);
                Scale = Builder.Binary(TEXT("Divide_DoubleDouble"),
                    Builder.Binary(TEXT("Subtract_DoubleDouble"), Size, Builder.Literal(Before + After)), Size);
                Offset = Builder.Binary(TEXT("Divide_DoubleDouble"), Builder.Literal(Before), Size);
            }
            else
            {
                Scale = Builder.Literal(1 / (1 + Before + After));
                Offset = Builder.Literal(Before / (1 + Before + After));
            }
            return Axis == TEXT("X") ? Builder.Color(Scale, Builder.Literal(0), Offset)
                                     : Builder.Color(Builder.Literal(0), Scale, Offset);
        };
        const FMargin& Insets = Context.Shader.CaptureInsets;
        Exec = Builder.Parameter(Exec, MID, TEXT("D2UBackgroundU"), Row(TEXT("X"), Insets.Left, Insets.Right));
        Exec = Builder.Parameter(Exec, MID, TEXT("D2UBackgroundV"), Row(TEXT("Y"), Insets.Top, Insets.Bottom));
        if (Context.ObjectProducer)
        {
            const FName Empty = Previous == TEXT("BackgroundA") ? FName(TEXT("BackgroundB")) : FName(TEXT("BackgroundA"));
            ClearBoundary(Builder, Builder.Get(Empty), Exec, true);
            ProducerComposite(Builder, Context.ObjectProducer, TEXT("ObjectProducer"), Builder.Get(Empty),
                Geometry, Builder.Get(TEXT("ObjectMip")), 0, Context.PaintSpace, Spaces.FindChecked(Context.ObjectProducer), Exec);
            Builder.Texture(MID, TEXT("D2UObject"), Builder.Get(TEXT("ObjectMip")), Exec);
            Builder.Texture(MID, TEXT("D2UObjectMip"), Builder.Get(TEXT("ObjectMip")), Exec);
        }
    }
    CompleteBoundaryPaint(Builder, Result, Exec);
    if (!FinishBoundary(Blueprint, Target, NewAsset, Error)) return nullptr;
    Classes.Add(Key, Blueprint);
    return Blueprint;
}
UWidgetBlueprint* BuildBackgroundAfter(const FBackgroundContext& Context, UWidgetBlueprint* Before,
    UMaterial* Composite, TMap<FString, UWidgetBlueprint*>& Classes,
    const TMap<UUserWidget*, FPaintChain>& Spaces, FString& Error)
{
    FTarget Target;
    Target.Widget = Context.Content;
    Target.NativeRole = TEXT("|background-after|") + Before->GetPathName() + TEXT("|")
        + Context.SourceProducer->GetClass()->GetPathName() + TEXT("|mode:") + FString::FromInt(Context.BlendMode)
        + PaintSpaceIdentity(Context.PaintSpace, Spaces.FindChecked(Context.SourceProducer));
    const FString Key = SurfaceIdentity(Target);
    if (auto* Existing = Classes.FindRef(Key)) return Existing;
    bool NewAsset = false;
    auto* Blueprint = CreateBoundaryBlueprint(Target, NewAsset, Error);
    if (!Blueprint || !BoundaryObject(Blueprint, BackgroundBoundaryRole, UWidget::StaticClass(), Error)
        || !BoundaryObject(Blueprint, TEXT("Before"), UUserWidget::StaticClass(), Error)
        || !BoundaryObject(Blueprint, TEXT("Source"), UUserWidget::StaticClass(), Error)
        || !BoundaryObject(Blueprint, TEXT("Consumer"), URetainerBox::StaticClass(), Error)
        || !BoundaryObject(Blueprint, TEXT("Composition"), UTextureRenderTarget2D::StaticClass(), Error)
        || !BoundaryObject(Blueprint, TEXT("CompositeMID"), UMaterialInstanceDynamic::StaticClass(), Error)
        || !PaintSpaceVariables(Blueprint, TEXT("Source"), Context.PaintSpace, Spaces.FindChecked(Context.SourceProducer), Error)) return nullptr;
    Blueprint->WidgetTree->RootWidget = Blueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MarkerRoot"));
    if (!CompileBoundaryState(Blueprint, Error)) return nullptr;
    FGraph Lifecycle(BoundaryEvents(Blueprint), Error);
    BoundaryTargets(Lifecycle, {TEXT("Composition")}, Composite);
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    auto* Paint = BoundaryPaint(Blueprint, Entry, Result, Error);
    if (!Error.IsEmpty()) return nullptr;
    FGraph Builder(Paint, Error);
    auto* Exec = Builder.Pin(Entry, UEdGraphSchema_K2::PN_Then);
    auto* Previous = Builder.Cast(Before->GeneratedClass, Builder.Get(TEXT("Before")));
    auto* Geometry = PaintGeometry(Builder, Builder.Get(TEXT("Consumer")));
    auto* Background = Builder.ObjectMember(Previous, Before->GeneratedClass, TEXT("Background"));
    SizeBoundaryTargets(Builder, {TEXT("Composition")}, Geometry, Exec);
    ProducerComposite(Builder, Context.SourceProducer, TEXT("Source"), Background, Geometry,
        Builder.Get(TEXT("Composition")), Context.BlendMode, Context.PaintSpace, Spaces.FindChecked(Context.SourceProducer), Exec);
    auto* Material = Builder.Execute(URetainerBox::StaticClass(), TEXT("GetEffectMaterial"), Exec);
    Builder.Link(Builder.Get(TEXT("Consumer")), Builder.Pin(Material, UEdGraphSchema_K2::PN_Self));
    auto* MID = Builder.Return(Material);
    Builder.Texture(MID, TEXT("D2UDestination"), Background, Exec);
    Builder.Texture(MID, TEXT("D2UComposition"), Builder.Get(TEXT("Composition")), Exec);
    CompleteBoundaryPaint(Builder, Result, Exec);
    if (!FinishBoundary(Blueprint, Target, NewAsset, Error)) return nullptr;
    Classes.Add(Key, Blueprint);
    return Blueprint;
}

bool BindProducerVisibility(UWidgetBlueprint* Blueprint, UUserWidget* Wrapper, FString& Error,
    UWidget* Placement = nullptr, FName SourceMember = NativeProducerRole)
{
    if (!Placement) Placement = Wrapper;
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
    // AddFunctionGraph synchronously compiles the skeleton, including all source widgets.
    // An empty map is initialized by that compiler; an existing map retains its GUIDs.
    if (!Blueprint->WidgetVariableNameToGuidMap.IsEmpty())
        DesignWidgetBlueprintUtils::EnsureWidgetVariableGuids(Blueprint);
#endif
    const FName Name(*FString(TEXT("D2UVisibility_") + Placement->GetName()));
    auto* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, true, static_cast<UFunction*>(nullptr));
    FGraph Builder(Graph, Error);
    TArray<UK2Node_FunctionEntry*> Entries;
    Graph->GetNodesOfClass(Entries);
    if (Entries.Num() != 1) { Error = TEXT("Producer visibility getter has no function entry"); return false; }
    Entries[0]->AddExtraFlags(FUNC_BlueprintPure);
    auto* Result = Builder.Node<UK2Node_FunctionResult>();
    Result->FunctionReference.SetSelfMember(Name);
    FEdGraphPinType Type;
    Type.PinCategory = UEdGraphSchema_K2::PC_Byte;
    Type.PinSubCategoryObject = StaticEnum<ESlateVisibility>();
    Result->CreateUserDefinedPin(TEXT("ReturnValue"), Type, EGPD_Input);
    auto* GetWrapper = Builder.Node<UK2Node_VariableGet>([&](auto* Node) { Node->VariableReference.SetSelfMember(Wrapper->GetFName()); });
    auto* Producer = GetWrapper->FindPin(Wrapper->GetFName());
    FEdGraphPinType WidgetType;
    WidgetType.PinCategory = UEdGraphSchema_K2::PC_Object;
    WidgetType.PinSubCategoryObject = Wrapper->GetClass();
    // Keep the getter output typed from the known template class while assembling
    // its graph for the parent blueprint's final compile.
    if (!Producer) Producer = GetWrapper->CreatePin(EGPD_Output, WidgetType, Wrapper->GetFName());
    else Producer->PinType = WidgetType;
    auto* Source = Builder.ObjectMember(Producer, Wrapper->GetClass(), SourceMember);
    auto* Visibility = Builder.Call(UWidget::StaticClass(), TEXT("GetVisibility"));
    Builder.Link(Source, Builder.Pin(Visibility, UEdGraphSchema_K2::PN_Self));
    Builder.Link(Builder.Return(Visibility), Builder.Pin(Result, TEXT("ReturnValue")));
    Builder.Link(Builder.Pin(Entries[0], UEdGraphSchema_K2::PN_Then), Builder.Pin(Result, UEdGraphSchema_K2::PN_Execute));
    FDelegateEditorBinding Binding;
    Binding.ObjectName = Placement->GetName();
    Binding.PropertyName = TEXT("Visibility");
    Binding.FunctionName = Name;
    Binding.MemberGuid = Graph->GraphGuid;
    Binding.Kind = EBindingKind::Function;
    Blueprint->Bindings.Add(Binding);
    return Error.IsEmpty();
}
UUserWidget* WrapProducer(UWidgetBlueprint* Blueprint, UWidget* Source, const FMargin& Insets,
    UMaterial* Opacity, TMap<FString, UWidgetBlueprint*>& Classes, TArray<FString>* GeneratedNames, FString& Error)
{
    FTarget Target;
    Target.Widget = Source;
    Target.CaptureInsets = Insets;
    Target.NativeRole = TEXT("|native-prefix-producer|opacity-once|paint-frame");
    const FString Key = SurfaceIdentity(Target);
    auto* Class = Classes.FindRef(Key);
    if (!Class)
    {
        Class = BuildProducer(Target, Opacity, Error);
        if (!Class) return nullptr;
        Classes.Add(Key, Class);
    }
    auto* Wrapper = NewObject<UUserWidget>(Blueprint->WidgetTree, Class->GeneratedClass,
        MakeUniqueObjectName(Blueprint->WidgetTree, UUserWidget::StaticClass(), *FString(TEXT("D2U_Producer_") + Source->GetName())), RF_Transactional);
    Wrapper->bIsVariable = true;
    Wrapper->ForceVolatile(true);
    Wrapper->SetVisibility(Source->GetVisibility());
    Wrapper->SetRenderTransform(Source->GetRenderTransform());
    Wrapper->SetRenderTransformPivot(Source->GetRenderTransformPivot());
    auto* SourceProperty = FindFProperty<FObjectPropertyBase>(Wrapper->GetClass(), NativeProducerRole);
    auto* OpacityProperty = FindFProperty<FFloatProperty>(Wrapper->GetClass(), TEXT("SourceOpacity"));
    if (!SourceProperty || !OpacityProperty) { Error = TEXT("Native producer lost its source/opacity contract"); return nullptr; }
    SourceProperty->SetObjectPropertyValue_InContainer(Wrapper, Source);
    OpacityProperty->SetPropertyValue_InContainer(Wrapper, Source->GetRenderOpacity());
    if (!ReplaceInTree(Blueprint, Source, Wrapper, Error)) return nullptr;
    Source->Slot = nullptr;
    Source->SetRenderOpacity(1);
    Source->SetRenderTransform(FWidgetTransform());
    Source->SetRenderTransformPivot(FVector2D::ZeroVector);
    Wrapper->SetContentForSlot(SurfaceSlot, Source);
    if (Wrapper->GetContentForSlot(SurfaceSlot) != Source || !BindProducerVisibility(Blueprint, Wrapper, Error))
    {
        if (Error.IsEmpty()) Error = TEXT("Native producer lost its original named-slot content");
        return nullptr;
    }
    if (GeneratedNames) GeneratedNames->AddUnique(Wrapper->GetName());
    return Wrapper;
}
UMaterialInterface* NativeMaterial(UWidget* Widget)
{
    if (auto* Image = Cast<UImage>(Widget)) return Cast<UMaterialInterface>(Design2UMGWidgetPropertyAccess::GetBrush(*Image).GetResourceObject());
    if (auto* Border = Cast<UBorder>(Widget)) return Cast<UMaterialInterface>(Border->Background.GetResourceObject());
    if (auto* Retainer = Cast<URetainerBox>(Widget))
        return const_cast<UMaterialInterface*>(Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*Retainer));
    return nullptr;
}
bool IsBackgroundConsumer(URetainerBox* Retainer)
{
    auto* Material = NativeMaterial(Retainer);
    if (!Material) return false;
    TArray<FMaterialParameterInfo> Parameters;
    TArray<FGuid> Ids;
    Material->GetAllTextureParameterInfo(Parameters, Ids);
    return Parameters.ContainsByPredicate([](const auto& P) { return P.Name == TEXT("D2UComposition"); });
}
bool HasBackgroundInput(UMaterialInterface* Material)
{
    if (!Material) return false;
    TArray<FMaterialParameterInfo> Parameters;
    TArray<FGuid> Ids;
    Material->GetAllTextureParameterInfo(Parameters, Ids);
    return Parameters.ContainsByPredicate([](const auto& P) { return P.Name == TEXT("D2UBackground") || P.Name == TEXT("D2UBackgroundMip"); });
}
TArray<UWidget*> PaintChildren(UPanelWidget* Parent)
{
    TArray<UWidget*> Children;
    for (int32 Index = 0; Index < Parent->GetChildrenCount(); ++Index) Children.Add(Parent->GetChildAt(Index));
    if (Parent->IsA<UCanvasPanel>())
        Children.StableSort([](const UWidget& A, const UWidget& B)
        { return CastChecked<UCanvasPanelSlot>(A.Slot)->GetZOrder() < CastChecked<UCanvasPanelSlot>(B.Slot)->GetZOrder(); });
    else if (Parent->IsA<UGridPanel>())
        Children.StableSort([](const UWidget& A, const UWidget& B)
        { return CastChecked<UGridSlot>(A.Slot)->GetLayer() < CastChecked<UGridSlot>(B.Slot)->GetLayer(); });
    return Children;
}
void CollectBackgroundPrefix(UWidget* Widget, const TMap<UWidget*, FMargin>& Insets,
    const TMap<UWidget*, UWidget*>& Parents, TArray<UWidget*>& Prefix)
{
    auto* Parent = Parents.FindRef(Widget);
    if (!Parent) return;
    CollectBackgroundPrefix(Parent, Insets, Parents, Prefix);
    auto* Panel = Cast<UPanelWidget>(Parent);
    if (!Panel || Panel->IsA<UWidgetSwitcher>()) return;
    for (auto* Sibling : PaintChildren(Panel))
    {
        if (Sibling == Widget) break;
        if (Insets.Contains(Sibling)) Prefix.Add(Sibling);
    }
}
bool SetBoundaryObject(UUserWidget* Widget, FName Name, UObject* Value, FString& Error)
{
    auto* Property = FindFProperty<FObjectPropertyBase>(Widget->GetClass(), Name);
    if (!Property || (Value && !Value->IsA(Property->PropertyClass)))
    {
        Error = TEXT("Native paint-boundary reference is missing or has the wrong type: ") + Name.ToString();
        return false;
    }
    Property->SetObjectPropertyValue_InContainer(Widget, Value);
    return true;
}
UUserWidget* BoundaryInstance(UWidgetBlueprint* Blueprint, UWidgetBlueprint* Class, const FString& Prefix, TArray<FString>* GeneratedNames)
{
    auto* Widget = NewObject<UUserWidget>(Blueprint->WidgetTree, Class->GeneratedClass,
        MakeUniqueObjectName(Blueprint->WidgetTree, UUserWidget::StaticClass(), *Prefix), RF_Transactional);
    Widget->bIsVariable = true;
    Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
    Widget->ForceVolatile(true);
    if (GeneratedNames) GeneratedNames->AddUnique(Widget->GetName());
    return Widget;
}
FPaintChain NativePaintSpace(UWidget* Widget, bool IncludeOwnCapture,
    const TMap<UWidget*, UWidget*>& Parents, const TMap<UWidget*, UUserWidget*>& Captures)
{
    FPaintChain Chain;
    if (IncludeOwnCapture) if (auto* Own = Captures.FindRef(Widget)) Chain.Add({Own, true});
    for (auto* Parent = Parents.FindRef(Widget); Parent; Parent = Parents.FindRef(Parent))
    {
        if (Parent->IsA<URetainerBox>()) Chain.Add({Parent, false});
        if (auto* Capture = Captures.FindRef(Parent)) Chain.Add({Capture, true});
    }
    return Chain;
}
bool SetPaintSpace(UUserWidget* Boundary, FName Input, const FPaintChain& Canvas, const FPaintChain& Texture, FString& Error)
{
    const auto Counts = SeparatePaintSpaces(Canvas, Texture);
    for (int32 Index = 0; Index < Counts.Key; ++Index)
        if (!SetBoundaryObject(Boundary, SpaceName(Input, true, Index), Canvas[Index].Widget, Error)) return false;
    for (int32 Index = 0; Index < Counts.Value; ++Index)
        if (!SetBoundaryObject(Boundary, SpaceName(Input, false, Index), Texture[Index].Widget, Error)) return false;
    return true;
}
void FillOverlay(UOverlay* Overlay, UWidget* Widget)
{
    auto* Slot = Overlay->AddChildToOverlay(Widget);
    Slot->SetHorizontalAlignment(HAlign_Fill);
    Slot->SetVerticalAlignment(VAlign_Fill);
}

bool PrepareBackground(UWidgetBlueprint* Blueprint, TArray<FTarget>& Bindings, TArray<FString>* GeneratedNames, FString& Error)
{
    bool HasBoundaries = false;
    TSet<UWidget*> PreparedShaders;
    TArray<UWidget*> Widgets;
    TMap<UWidget*, UWidget*> Parents;
    Blueprint->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        Widgets.Add(Widget);
        Parents.Add(Widget, Widget->GetParent());
        if (auto* User = Cast<UUserWidget>(Widget))
        {
            HasBoundaries |= HasNativeRole(User, BackgroundBoundaryRole);
            if (HasNativeRole(User, BackgroundBoundaryRole))
                if (auto* Shader = FindFProperty<FObjectPropertyBase>(User->GetClass(), TEXT("Shader")))
                    PreparedShaders.Add(Cast<UWidget>(Shader->GetObjectPropertyValue_InContainer(User)));
        }
    });
    for (auto* Widget : Widgets)
        if (auto* Host = Cast<INamedSlotInterface>(Widget))
        {
            TArray<FName> Slots;
            Host->GetSlotNames(Slots);
            for (FName Slot : Slots)
                if (auto* Content = Host->GetContentForSlot(Slot))
                    if (!Parents.FindRef(Content)) Parents.Add(Content, Widget);
        }
    // Roles keep their native instances and references on a second Prepare. Ordinary
    // shader surfaces inside their slots are rebuilt by the existing surface path.
    if (HasBoundaries)
    {
        for (auto& Binding : Bindings) if (PreparedShaders.Contains(Binding.Widget)) Binding.ObjectMip = false;
        return true;
    }
    TMap<UWidget*, FMargin> Insets;
    TArray<FBackgroundContext> Contexts;
    for (auto* Widget : Widgets)
    {
        TOptional<FMargin> Capture;
        if (!Design2UMGWidgetMetadata::ConsumeBackgroundCaptureInsets(Widget, Capture, Error)) return false;
        if (Capture.IsSet()) Insets.Add(Widget, Capture.GetValue());
        auto* Consumer = Cast<URetainerBox>(Widget);
        if (!Consumer || !IsBackgroundConsumer(Consumer)) continue;
        FBackgroundContext Context;
        Context.Consumer = Consumer;
        Context.Content = Consumer->GetContent();
        if (!Context.Content) { Error = TEXT("Background consumer has no native source content"); return false; }
        float Mode = 0;
        if (!NativeMaterial(Consumer)->GetScalarParameterValue(FMaterialParameterInfo(TEXT("D2UBlendMode")), Mode)
            || Mode != FMath::RoundToFloat(Mode) || Mode < 0 || Mode > 17)
        {
            Error = TEXT("Background consumer has no valid blend-mode contract");
            return false;
        }
        Context.BlendMode = int32(Mode);
        for (auto& Binding : Bindings)
            if (Binding.Widget == Context.Content)
            {
                Context.Shader = Binding;
                TArray<FMaterialParameterInfo> Parameters;
                TArray<FGuid> Ids;
                Binding.Material->GetAllTextureParameterInfo(Parameters, Ids);
                Context.ObjectSampling = Parameters.ContainsByPredicate([](const auto& P)
                    { return P.Name == TEXT("D2UObject") || P.Name == TEXT("D2UObjectMip"); });
                Binding.ObjectMip = false;
                break;
            }
        Contexts.Add(MoveTemp(Context));
    }
    TSet<UWidget*> ConsumerSources;
    for (const auto& Context : Contexts) ConsumerSources.Add(Context.Content);
    for (auto* Widget : Widgets)
    {
        auto* Retainer = Cast<URetainerBox>(Widget);
        auto* Material = NativeMaterial(Widget);
        if (!Retainer || ConsumerSources.Contains(Widget) || IsBackgroundConsumer(Retainer) || !HasBackgroundInput(Material)) continue;
        FBackgroundContext Context;
        Context.Consumer = Retainer;
        Context.Content = Retainer;
        Context.Shader.Widget = Retainer;
        Context.Shader.Material = Material;
        Context.DirectBackground = true;
        Contexts.Add(MoveTemp(Context));
    }
    if (Contexts.IsEmpty()) return true;
    for (auto& Context : Contexts) CollectBackgroundPrefix(Context.Consumer, Insets, Parents, Context.Prefix);
    UMaterial *Composite = nullptr, *Consumer = nullptr, *Identity = nullptr, *Opacity = nullptr;
    if (!PencilShaderMaterials::PrepareBackgroundMaterials(Composite, Consumer, Identity, Opacity, Error)) return false;
    TMap<FString, UWidgetBlueprint*> ProducerClasses;
    TMap<UWidget*, UWidget*> Placements;
    TMap<UWidget*, UUserWidget*> Producers;
    TMap<UWidget*, UUserWidget*> CaptureOwners;
    TMap<UUserWidget*, UWidget*> CapturedSources;
    // The local object prefix must actually paint before the shader that samples it.
    for (auto& Context : Contexts)
    {
        if (!Context.Shader.Widget || !Context.ObjectSampling) continue;
        auto* Source = Cast<URetainerBox>(Context.Shader.Widget);
        if (!Source || !Source->GetContent()) { Error = TEXT("Context object fill has no native local prefix"); return false; }
        UWidget* Local = Source->GetContent();
        auto* Carrier = Blueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
            MakeUniqueObjectName(Blueprint->WidgetTree, UBorder::StaticClass(), *FString(TEXT("D2U_ShaderCarrier_") + Source->GetName())));
        Carrier->SetPadding(FMargin(0));
        Carrier->SetBrushColor(FLinearColor::Transparent);
        Carrier->SetVisibility(ESlateVisibility::HitTestInvisible);
        // The original local producer sizes the shared fill overlay. This visible,
        // transparent child keeps the shader Retainer's native effect quad active.
        Source->SetContent(Carrier);
        Source->SetTextureParameter(TEXT("D2UNativeEmptyCapture"));
        if (GeneratedNames) GeneratedNames->AddUnique(Carrier->GetName());
        const float GroupOpacity = Source->GetRenderOpacity();
        Source->SetRenderOpacity(1);
        auto* Group = Blueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),
            MakeUniqueObjectName(Blueprint->WidgetTree, UOverlay::StaticClass(), *FString(TEXT("D2U_Object_") + Context.Consumer->GetName())));
        Group->SetRenderTransform(Context.Consumer->GetRenderTransform());
        Group->SetRenderTransformPivot(Context.Consumer->GetRenderTransformPivot());
        Group->SetVisibility(Context.Consumer->GetVisibility());
        Group->SetRenderOpacity(GroupOpacity);
        Parents.Add(Group, Parents.FindRef(Context.Consumer));
        Parents.Add(Context.Consumer, Group);
        Parents.Add(Local, Group);
        if (!ReplaceInTree(Blueprint, Context.Consumer, Group, Error)) return false;
        Context.Consumer->Slot = nullptr;
        Context.Consumer->SetRenderTransform(FWidgetTransform());
        Context.Consumer->SetRenderTransformPivot(FVector2D::ZeroVector);
        FillOverlay(Group, Local);
        FillOverlay(Group, Context.Consumer);
        Context.ObjectProducer = WrapProducer(Blueprint, Local, FMargin(0), Opacity, ProducerClasses, GeneratedNames, Error);
        if (!Context.ObjectProducer) return false;
        CaptureOwners.Add(Local, Context.ObjectProducer);
        CapturedSources.Add(Context.ObjectProducer, Local);
        Placements.Add(Context.Consumer, Group);
        Insets.FindOrAdd(Context.Consumer);
        if (GeneratedNames) GeneratedNames->AddUnique(Group->GetName());
    }
    for (const auto& Pair : Insets)
    {
        UWidget* Placement = Placements.FindRef(Pair.Key);
        if (!Placement) Placement = Pair.Key;
        auto* Producer = WrapProducer(Blueprint, Placement, Pair.Value, Opacity, ProducerClasses, GeneratedNames, Error);
        if (!Producer) return false;
        if (Placement != Pair.Key && !SetBoundaryObject(Producer, NativeProducerRole, Pair.Key, Error)) return false;
        Producers.Add(Pair.Key, Producer);
        CaptureOwners.Add(Placement, Producer);
        CapturedSources.Add(Producer, Placement);
    }
    for (auto& Context : Contexts)
    {
        if (Context.DirectBackground) continue;
        Context.SourceProducer = Producers.FindRef(Context.Content);
        if (!Context.SourceProducer)
            Context.SourceProducer = WrapProducer(Blueprint, Context.Content, FMargin(0), Opacity, ProducerClasses, GeneratedNames, Error);
        if (!Context.SourceProducer) return false;
        CaptureOwners.Add(Context.Content, Context.SourceProducer);
        CapturedSources.Add(Context.SourceProducer, Context.Content);
    }
    TMap<UUserWidget*, FPaintChain> Spaces;
    for (const auto& Pair : CapturedSources) Spaces.Add(Pair.Key, NativePaintSpace(Pair.Value, false, Parents, CaptureOwners));
    for (auto& Context : Contexts) Context.PaintSpace = NativePaintSpace(Context.Consumer, true, Parents, CaptureOwners);

    for (auto& Context : Contexts)
    {
        TArray<UUserWidget*> Prefix;
        for (auto* Source : Context.Prefix)
        {
            auto* Producer = Producers.FindRef(Source);
            if (!Producer) { Error = TEXT("Background prefix lost its native producer"); return false; }
            Prefix.Add(Producer);
        }
        if (Context.ObjectProducer) Prefix.Add(Context.ObjectProducer);
        auto* BeforeClass = BuildBackgroundBefore(Context, Prefix, Composite, ProducerClasses, Spaces, Error);
        if (!BeforeClass) return false;
        auto* Before = BoundaryInstance(Blueprint, BeforeClass, TEXT("D2U_BackgroundBefore_") + Context.Consumer->GetName(), GeneratedNames);
        for (int32 Index = 0; Index < Prefix.Num(); ++Index)
            if (!SetBoundaryObject(Before, PrefixName(Index), Prefix[Index], Error)
                || !SetPaintSpace(Before, PrefixName(Index), Context.PaintSpace, Spaces.FindChecked(Prefix[Index]), Error)) return false;
        if (!SetBoundaryObject(Before, BackgroundBoundaryRole, Context.Consumer, Error)) return false;
        if (Context.Shader.Widget && !SetBoundaryObject(Before, TEXT("Shader"), Context.Shader.Widget, Error)) return false;
        if (Context.ObjectProducer && (!SetBoundaryObject(Before, TEXT("ObjectProducer"), Context.ObjectProducer, Error)
            || !SetPaintSpace(Before, TEXT("ObjectProducer"), Context.PaintSpace, Spaces.FindChecked(Context.ObjectProducer), Error))) return false;
        auto* Layers = Blueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),
            MakeUniqueObjectName(Blueprint->WidgetTree, UOverlay::StaticClass(), *FString(TEXT("D2U_BackgroundLayers_") + Context.Consumer->GetName())));
        if (Context.DirectBackground)
        {
            Layers->bIsVariable = true;
            Layers->SetVisibility(Context.Content->GetVisibility());
            Layers->SetRenderTransform(Context.Content->GetRenderTransform());
            Layers->SetRenderTransformPivot(Context.Content->GetRenderTransformPivot());
            if (!ReplaceInTree(Blueprint, Context.Content, Layers, Error)) return false;
            Context.Content->Slot = nullptr;
            Context.Content->SetRenderTransform(FWidgetTransform());
            Context.Content->SetRenderTransformPivot(FVector2D::ZeroVector);
            FillOverlay(Layers, Before);
            FillOverlay(Layers, Context.Content);
            if (!BindProducerVisibility(Blueprint, Before, Error, Layers, BackgroundBoundaryRole)) return false;
        }
        else
        {
            auto* AfterClass = BuildBackgroundAfter(Context, BeforeClass, Composite, ProducerClasses, Spaces, Error);
            if (!AfterClass) return false;
            auto* After = BoundaryInstance(Blueprint, AfterClass, TEXT("D2U_BackgroundAfter_") + Context.Consumer->GetName(), GeneratedNames);
            if (!SetBoundaryObject(After, BackgroundBoundaryRole, Context.Consumer, Error)
                || !SetBoundaryObject(After, TEXT("Before"), Before, Error)
                || !SetBoundaryObject(After, TEXT("Source"), Context.SourceProducer, Error)
                || !SetBoundaryObject(After, TEXT("Consumer"), Context.Consumer, Error)
                || !SetPaintSpace(After, TEXT("Source"), Context.PaintSpace, Spaces.FindChecked(Context.SourceProducer), Error)) return false;
            Context.Consumer->SetContent(nullptr);
            FillOverlay(Layers, Before);
            FillOverlay(Layers, Context.SourceProducer);
            FillOverlay(Layers, After);
            Context.Consumer->SetContent(Layers);
        }
        if (GeneratedNames) GeneratedNames->AddUnique(Layers->GetName());
    }
    return Error.IsEmpty();
}

bool UnwrapSurfaces(UWidgetBlueprint* Blueprint, TArray<FString>* GeneratedNames, FString& Error)
{
    TArray<UUserWidget*> Wrappers;
    Blueprint->WidgetTree->ForEachWidget(
        [&](UWidget* Widget)
        {
            if (auto* UserWidget = Cast<UUserWidget>(Widget))
            {
                if (UserWidget->GetClass()->GetPathName().StartsWith(TEXT("/Game/Design2UMG/GeneratedShaderWidgets/"))
                    && !HasNativeRole(UserWidget, NativeProducerRole) && !HasNativeRole(UserWidget, BackgroundBoundaryRole))
                    Wrappers.Add(UserWidget);
            }
        });
    for (int32 I = Wrappers.Num() - 1; I >= 0; --I)
    {
        UUserWidget* Wrapper = Wrappers[I];
        UWidget* Original = Wrapper->GetContentForSlot(SurfaceSlot);
        if (!Original)
        {
            Error = TEXT("Generated shader surface lost its original native content");
            return false;
        }
        Wrapper->SetContentForSlot(SurfaceSlot, nullptr);
        UWidget* Placement = Wrapper;
        UImage* Output = nullptr;
        auto* OutputProperty = FindFProperty<FObjectPropertyBase>(Wrapper->GetClass(), TEXT("ShaderOutput"));
        if (OutputProperty)
        {
            Output = Cast<UImage>(OutputProperty->GetObjectPropertyValue_InContainer(Wrapper));
            auto* Composition = Cast<UOverlay>(Wrapper->GetParent());
            auto* Capture = Cast<URetainerBox>(Original);
            if (!Output || !Capture || !Composition || Composition->GetChildrenCount() != 2 ||
                Composition->GetChildAt(0) != Wrapper || Composition->GetChildAt(1) != Output)
            {
                Error = TEXT("Object mip surface lost its source/output composition");
                return false;
            }
            Capture->SetEffectMaterial(Cast<UMaterialInterface>(
                Design2UMGWidgetPropertyAccess::GetBrush(*Output).GetResourceObject()));
            OutputProperty->SetObjectPropertyValue_InContainer(Wrapper, nullptr);
            Placement = Composition;
        }
        if (!ReplaceInTree(Blueprint, Placement, Original, Error))
            return false;
        if (Placement != Wrapper)
        {
            CastChecked<UOverlay>(Placement)->ClearChildren();
            Placement->Slot = nullptr;
            if (GeneratedNames)
            {
                GeneratedNames->Remove(Placement->GetName());
                GeneratedNames->Remove(Output->GetName());
            }
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
            Blueprint->OnVariableRemoved(Output->GetFName());
            Blueprint->OnVariableRemoved(Placement->GetFName());
#endif
            Output->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
            Output->MarkAsGarbage();
            Placement->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
            Placement->MarkAsGarbage();
        }
        Wrapper->Slot = nullptr;
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
        Blueprint->OnVariableRemoved(Wrapper->GetFName());
#endif
        if (GeneratedNames)
            GeneratedNames->Remove(Wrapper->GetName());
        Wrapper->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
        Wrapper->MarkAsGarbage();
    }
    return true;
}
} // namespace

bool PencilShaderRuntimeBindings::Prepare(UWidgetBlueprint* Blueprint, FString& Error, TArray<FString>* GeneratedNames)
{
    Error.Reset();
    if (!Blueprint || !Blueprint->WidgetTree)
    {
        Error = TEXT("Shader runtime requires a widget tree");
        return false;
    }
    Clear(Blueprint);
    if (!UnwrapSurfaces(Blueprint, GeneratedNames, Error))
        return false;
    auto Bindings = Targets(Blueprint, Error);
    if (!Error.IsEmpty())
        return false;
    if (!PrepareBackground(Blueprint, Bindings, GeneratedNames, Error))
        return false;
    UMaterial* CaptureOutput = nullptr;
    UMaterial* CopyMaterial = nullptr;
    if (Bindings.ContainsByPredicate([](const FTarget& Target) { return Target.ObjectMip; }) &&
        !PencilShaderMaterials::PrepareObjectCapture(CaptureOutput, CopyMaterial, Error))
        return false;
    // Repeated surfaces in one import share their generated native class without recompiling it per instance.
    TMap<FString, UWidgetBlueprint*> BuiltSurfaces;
    for (const auto& Binding : Bindings)
    {
        const FString Identity = SurfaceIdentity(Binding);
        UWidgetBlueprint* SurfaceBlueprint = BuiltSurfaces.FindRef(Identity);
        if (!SurfaceBlueprint)
        {
            SurfaceBlueprint = BuildSurfaceBlueprint(Binding, CopyMaterial, Error);
            if (SurfaceBlueprint)
                BuiltSurfaces.Add(Identity, SurfaceBlueprint);
        }
        if (!SurfaceBlueprint)
            return false;
        UWidget* Original = Binding.Widget;
        const FName Name = MakeUniqueObjectName(Blueprint->WidgetTree, UUserWidget::StaticClass(),
                                                *FString(TEXT("D2U_Shader_") + Original->GetName()));
        // Blueprint templates retain slot bindings; native instance initialization creates their inner trees.
        auto* Wrapper = NewObject<UUserWidget>(Blueprint->WidgetTree, SurfaceBlueprint->GeneratedClass, Name,
                                               RF_Transactional);
        if (Binding.ObjectMip)
        {
            auto* Composition = Blueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),
                MakeUniqueObjectName(Blueprint->WidgetTree, UOverlay::StaticClass(),
                    *FString(TEXT("D2U_ObjectMip_") + Original->GetName())));
            auto* Output = Blueprint->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
                MakeUniqueObjectName(Blueprint->WidgetTree, UImage::StaticClass(),
                    *FString(TEXT("D2U_ShaderOutput_") + Original->GetName())));
            FSlateBrush Brush;
            Brush.SetResourceObject(Binding.Material);
            Brush.ImageSize = FVector2D::ZeroVector;
            Brush.DrawAs = ESlateBrushDrawType::Image;
            Output->SetBrush(Brush);
            Output->SetVisibility(ESlateVisibility::HitTestInvisible);
            Output->SetRenderTransform(Original->GetRenderTransform());
            Output->SetRenderTransformPivot(Original->GetRenderTransformPivot());
            Output->SetRenderOpacity(Original->GetRenderOpacity());
            auto* OutputProperty = FindFProperty<FObjectPropertyBase>(Wrapper->GetClass(), TEXT("ShaderOutput"));
            if (!OutputProperty)
            {
                Error = TEXT("Object mip surface has no output image reference");
                return false;
            }
            OutputProperty->SetObjectPropertyValue_InContainer(Wrapper, Output);
            if (!ReplaceInTree(Blueprint, Original, Composition, Error))
                return false;
            auto* SourceSlot = Composition->AddChildToOverlay(Wrapper);
            SourceSlot->SetHorizontalAlignment(HAlign_Fill);
            SourceSlot->SetVerticalAlignment(VAlign_Fill);
            auto* OutputSlot = Composition->AddChildToOverlay(Output);
            OutputSlot->SetHorizontalAlignment(HAlign_Fill);
            OutputSlot->SetVerticalAlignment(VAlign_Fill);
            CastChecked<URetainerBox>(Original)->SetEffectMaterial(CaptureOutput);
            if (GeneratedNames)
            {
                GeneratedNames->AddUnique(Composition->GetName());
                GeneratedNames->AddUnique(Output->GetName());
            }
        }
        else if (!ReplaceInTree(Blueprint, Original, Wrapper, Error))
            return false;
        Original->Slot = nullptr;
        Wrapper->SetContentForSlot(SurfaceSlot, Original);
        if (Wrapper->GetContentForSlot(SurfaceSlot) != Original)
        {
            Error = TEXT("Shader surface did not retain its original native content");
            return false;
        }
        if (GeneratedNames)
            GeneratedNames->AddUnique(Wrapper->GetName());
    }
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
    // Include boundaries, layers and object-mip composition added after the last visibility graph.
    if (!Blueprint->WidgetVariableNameToGuidMap.IsEmpty())
        DesignWidgetBlueprintUtils::EnsureWidgetVariableGuids(Blueprint);
#endif
    if (GeneratedNames)
        GeneratedNames->Sort();
    return true;
}
