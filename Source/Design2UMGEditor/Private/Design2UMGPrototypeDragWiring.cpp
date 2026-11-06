// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Design2UMGPrototypeDragWiring.h"
#include "UMGSchemaSourceExtensions.h"
#include "Blueprint/UserWidget.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Application/SlateApplication.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "Misc/Crc.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGPrototypeGraphWiring
{
namespace
{
namespace DragWiringPrivate
{
const FName DragMetadata(TEXT("Design2UMGGeneratedDrag"));
FName DragName(int32 Index, const TCHAR* Suffix)
{
    return FName(*FString::Printf(TEXT("__d2u_drag_%03d_%s"), Index, Suffix));
}
FName MonitorName(int32 Index) { return DragName(Index, TEXT("monitor")); }
FName ReleaseName(int32 Index) { return DragName(Index, TEXT("release")); }

bool Connect(UEdGraph* Graph, UEdGraphPin* A, UEdGraphPin* B)
{
    return A && B && Graph->GetSchema()->TryCreateConnection(A, B);
}
UK2Node_CallFunction* Call(UEdGraph* Graph, FChainLayout& Layout, UFunction* Function)
{
    if (!Function) return nullptr;
    auto* Node = CreateCallNode(Graph, Function);
    if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure)) Layout.PlacePure(Node);
    else Layout.PlaceExec(Node);
    return Node;
}
UK2Node_VariableSet* Set(UEdGraph* Graph, UWidgetBlueprint* WBP, const FProperty* Property, FChainLayout& Layout)
{
    auto* Node = CreateMarkerNode<UK2Node_VariableSet>(Graph,
        [&](UK2Node_VariableSet* N) { N->SetFromProperty(Property, true, WBP->SkeletonGeneratedClass); });
    Layout.PlaceExec(Node);
    return Node;
}
UEdGraphPin* SetInput(UK2Node_VariableSet* Node)
{
    return Node ? Node->FindPin(Node->GetVarName(), EGPD_Input) : nullptr;
}
bool SetLiteral(UEdGraph* Graph, UWidgetBlueprint* WBP, const FProperty* Property, const FString& Value, FChainLayout& Layout)
{
    if (!Property) return false;
    auto* Node = Set(Graph, WBP, Property, Layout);
    if (!SetInput(Node)) return false;
    SetInput(Node)->DefaultValue = Value;
    return true;
}
bool Timer(UEdGraph* Graph, FChainLayout& Layout, UFunction* Function, FName Event, float Seconds = 0.0f)
{
    auto* Node = Call(Graph, Layout, Function);
    if (!Node || !Node->FindPin(TEXT("FunctionName"), EGPD_Input)) return false;
    Node->FindPinChecked(TEXT("FunctionName"), EGPD_Input)->DefaultValue = Event.ToString();
    if (auto* Time = Node->FindPin(TEXT("Time"), EGPD_Input)) Time->DefaultValue = FString::SanitizeFloat(Seconds);
    return true;
}
UK2Node_IfThenElse* Branch(UEdGraph* Graph, FChainLayout& Layout, UEdGraphPin* Condition)
{
    auto* Node = CreateBranchNode(Graph);
    Layout.PlaceExec(Node);
    if (!Connect(Graph, Condition, Node->GetConditionPin())) return nullptr;
    return Node;
}
bool ConnectExec(UEdGraphPin* From, const FChainLayout& Chain)
{
    if (!From || Chain.ExecChain.IsEmpty()) return false;
    auto* To = Chain.ExecChain[0]->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
    if (!To) return false;
    From->MakeLinkTo(To);
    return true;
}
bool OwnerGate(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
    const FIntProperty* Owner, int32 Value, FChainLayout& Layout)
{
    if (!Owner || !Functions.EqualEqual_IntInt) return false;
    auto* Get = Layout.PlacePure(CreateVariableGet(Graph, WBP, Owner));
    auto* Equal = Call(Graph, Layout, Functions.EqualEqual_IntInt);
    if (!Equal || !Connect(Graph, Get->GetValuePin(), Equal->FindPin(TEXT("A")))) return false;
    Equal->FindPinChecked(TEXT("B"))->DefaultValue = FString::FromInt(Value);
    return Branch(Graph, Layout, Equal->GetReturnValuePin()) != nullptr;
}
struct FDragProperties
{
    FStructProperty* Screen = nullptr;
    FStructProperty* Local = nullptr;
    FBoolProperty* Active = nullptr;
    FBoolProperty* Accepted = nullptr;
    FBoolProperty* ForceCancel = nullptr;
    FBoolProperty* ClipInitialized = nullptr;
    FIntProperty* Selected = nullptr;
    FFloatProperty* Progress = nullptr;
    FFloatProperty* Span = nullptr;
    FFloatProperty* Threshold = nullptr;
    FFloatProperty* Trigger = nullptr;
    FFloatProperty* AnimationTarget = nullptr;
    FFloatProperty* AnimationStart = nullptr;
    FObjectProperty* Button = nullptr;
    FObjectProperty* Page = nullptr;
    FObjectProperty* Animation = nullptr;
};
FDragProperties Resolve(UWidgetBlueprint* WBP, int32 LeaderIndex, const FString& StableIdentity,
    const FResolvedBinding& Resolved, const FBindingGraphTargets& Targets)
{
    FDragProperties P;
    UClass* Class = WBP->SkeletonGeneratedClass;
    const int32 I = LeaderIndex;
    P.Screen = FindFProperty<FStructProperty>(Class, DragName(I, TEXT("screen")));
    P.Local = FindFProperty<FStructProperty>(Class, DragName(I, TEXT("local")));
    P.Active = FindFProperty<FBoolProperty>(Class, DragName(I, TEXT("active")));
    P.Accepted = FindFProperty<FBoolProperty>(Class, GetDragAcceptedName(I));
    P.ForceCancel = FindFProperty<FBoolProperty>(Class, DragName(I, TEXT("force_cancel")));
    P.ClipInitialized = FindFProperty<FBoolProperty>(Class, DragName(I, TEXT("clip_initialized")));
    P.Selected = FindFProperty<FIntProperty>(Class, GetDragSelectedName(I));
    P.Progress = FindFProperty<FFloatProperty>(Class, DragName(I, TEXT("progress")));
    P.Span = FindFProperty<FFloatProperty>(Class, GetDragSpanName(StableIdentity));
    P.Threshold = FindFProperty<FFloatProperty>(Class, GetDragCommitThresholdName(StableIdentity));
    P.Trigger = FindFProperty<FFloatProperty>(Class, GetDragSlateThresholdName(StableIdentity));
    P.AnimationTarget = FindFProperty<FFloatProperty>(Class, Resolved.PlaybackTargetPropertyName);
    P.AnimationStart = FindFProperty<FFloatProperty>(Class, Resolved.PlaybackStartPropertyName);
    P.Button = FindFProperty<FObjectProperty>(Class, Resolved.TriggerWidgetName);
    if (Targets.PageHostPages.IsValidIndex(Resolved.SourcePageIndex))
        P.Page = FindFProperty<FObjectProperty>(Class, Targets.PageHostPages[Resolved.SourcePageIndex]->GetFName());
    P.Animation = FindFProperty<FObjectProperty>(Class, Resolved.WidgetAnimationName);
    return P;
}

bool PropertiesValid(const FDragProperties& P)
{
    return P.Screen && P.Local && P.Screen->Struct == TBaseStructure<FVector2D>::Get()
        && P.Local->Struct == TBaseStructure<FVector2D>::Get() && P.Active && P.Accepted && P.ForceCancel && P.ClipInitialized && P.Selected && P.Progress
        && P.Span && P.Threshold && P.Trigger && P.AnimationTarget && P.AnimationStart && P.Button && P.Page && P.Animation;
}

UFunction* Math(const TCHAR* Name) { return UKismetMathLibrary::StaticClass()->FindFunctionByName(Name); }
UFunction* Slate(const TCHAR* Name) { return USlateBlueprintLibrary::StaticClass()->FindFunctionByName(Name); }

bool ScheduleSampleNext(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
    const FDragProperties& P, const FBindingGraphTargets& Targets, int32 LeaderIndex,
    bool bRelease, FChainLayout& Chain, bool bAccepted)
{
    if (bRelease && !bAccepted)
        return SetLiteral(Graph, WBP, Targets.TransitionOwnerProperty, TEXT("0"), Chain);
    return Timer(Graph, Chain, Functions.SetTimerForNextTick,
        bRelease ? ReleaseName(LeaderIndex) : MonitorName(LeaderIndex));
}

// The caller samples the pointer once, including on release.
bool AppendSample(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
    const FDragProperties& P, const FBindingGraphTargets& Targets, const FUMGPrototypeNavigationBinding& Binding,
    const FResolvedBinding& Resolved, int32 LeaderIndex, FVector2D DirectionUnit, bool bDirectional,
    bool bRelease, UEdGraphPin* MousePosition, FChainLayout& Sample)
{
    if (!MousePosition) return false;
    auto* Page = Sample.PlacePure(CreateVariableGet(Graph, WBP, P.Page));
    auto* Button = Sample.PlacePure(CreateVariableGet(Graph, WBP, P.Button));
    auto* PageGeometry = Call(Graph, Sample, Functions.GetCachedGeometry);
    auto* ButtonGeometry = Call(Graph, Sample, Functions.GetCachedGeometry);
    auto* Local = Call(Graph, Sample, Slate(TEXT("AbsoluteToLocal")));
    auto* Origin = Sample.PlacePure(CreateVariableGet(Graph, WBP, P.Local));
    auto* Size = Call(Graph, Sample, Slate(TEXT("GetLocalSize")));
    auto* SizeParts = Call(Graph, Sample, Math(TEXT("BreakVector2D")));
    auto* Span = Sample.PlacePure(CreateVariableGet(Graph, WBP, P.Span));
    auto* HasSpan = Call(Graph, Sample, Math(TEXT("Greater_DoubleDouble")));
    auto* PickSpan = Call(Graph, Sample, Math(TEXT("SelectFloat")));
    if (!PageGeometry || !ButtonGeometry || !Local || !Size || !SizeParts || !HasSpan || !PickSpan
        || !Connect(Graph, Page->GetValuePin(), PageGeometry->FindPin(UEdGraphSchema_K2::PN_Self))
        || !Connect(Graph, Button->GetValuePin(), ButtonGeometry->FindPin(UEdGraphSchema_K2::PN_Self))
        || !Connect(Graph, PageGeometry->GetReturnValuePin(), Local->FindPin(TEXT("Geometry")))
        || !Connect(Graph, MousePosition, Local->FindPin(TEXT("AbsoluteCoordinate")))
        || !Connect(Graph, ButtonGeometry->GetReturnValuePin(), Size->FindPin(TEXT("Geometry")))
        || !Connect(Graph, Size->GetReturnValuePin(), SizeParts->FindPin(TEXT("InVec")))
        || !Connect(Graph, Span->GetValuePin(), HasSpan->FindPin(TEXT("A")))
        || !Connect(Graph, Span->GetValuePin(), PickSpan->FindPin(TEXT("A")))
        || !Connect(Graph, SizeParts->FindPin(TEXT("X"), EGPD_Output), PickSpan->FindPin(TEXT("B")))
        || !Connect(Graph, HasSpan->GetReturnValuePin(), PickSpan->FindPin(TEXT("bPickA")))) return false;
    HasSpan->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("0.0");
    UEdGraphPin* Distance = nullptr;
    if (bDirectional)
    {
        auto* Delta = Call(Graph, Sample, Math(TEXT("Subtract_Vector2DVector2D")));
        auto* Dot = Call(Graph, Sample, Math(TEXT("DotProduct2D")));
        if (!Delta || !Dot || !Connect(Graph, Local->GetReturnValuePin(), Delta->FindPin(TEXT("A")))
            || !Connect(Graph, Origin->GetValuePin(), Delta->FindPin(TEXT("B")))
            || !Connect(Graph, Delta->GetReturnValuePin(), Dot->FindPin(TEXT("A")))) return false;
        Dot->FindPinChecked(TEXT("B"))->DefaultValue = FString::Printf(TEXT("(X=%.17g,Y=%.17g)"), DirectionUnit.X, DirectionUnit.Y);
        Distance = Dot->GetReturnValuePin();
    }
    else
    {
        auto* LocalDistance = Call(Graph, Sample, Math(TEXT("Distance2D")));
        if (!LocalDistance || !Connect(Graph, Local->GetReturnValuePin(), LocalDistance->FindPin(TEXT("V1")))
            || !Connect(Graph, Origin->GetValuePin(), LocalDistance->FindPin(TEXT("V2")))) return false;
        Distance = LocalDistance->GetReturnValuePin();
    }
    auto* Divide = Call(Graph, Sample, Math(TEXT("Divide_DoubleDouble")));
    auto* Clamp = Call(Graph, Sample, Math(TEXT("FClamp")));
    auto* Previous = Sample.PlacePure(CreateVariableGet(Graph, WBP, P.Progress));
    auto* Changed = Call(Graph, Sample, Math(TEXT("NotEqual_DoubleDouble")));
    if (!Divide || !Clamp || !Changed || !Connect(Graph, Distance, Divide->FindPin(TEXT("A")))
        || !Connect(Graph, PickSpan->GetReturnValuePin(), Divide->FindPin(TEXT("B")))
        || !Connect(Graph, Divide->GetReturnValuePin(), Clamp->FindPin(TEXT("Value")))
        || !Connect(Graph, Clamp->GetReturnValuePin(), Changed->FindPin(TEXT("A")))
        || !Connect(Graph, Previous->GetValuePin(), Changed->FindPin(TEXT("B")))) return false;
    Clamp->FindPinChecked(TEXT("Min"))->DefaultValue = TEXT("0.0");
    Clamp->FindPinChecked(TEXT("Max"))->DefaultValue = TEXT("1.0");
    auto* ChangedBranch = Branch(Graph, Sample, Changed->GetReturnValuePin());
    if (!ChangedBranch || !LinkExecChain(Sample.ExecChain)) return false;
    FChainLayout Same; Same.RowY = Sample.RowY + NodeRowStep; Same.NextColumn = Sample.NextColumn;
    if (!ScheduleSampleNext(Graph, WBP, Functions, P, Targets, LeaderIndex, bRelease, Same, true)
        || !LinkExecChain(Same.ExecChain) || !ConnectExec(ChangedBranch->GetElsePin(), Same)) return false;

    FChainLayout ChangedPath; ChangedPath.RowY = Sample.RowY + NodeRowStep * 2; ChangedPath.NextColumn = Sample.NextColumn;
    AppendLivePresentationCall(Graph, WBP, Functions, Resolved, true, ChangedPath);
    if (!AppendPrototypeLayerOrders(Graph, WBP, Functions, Binding, Resolved, true, ChangedPath)) return false;
    auto* SetProgress = Set(Graph, WBP, P.Progress, ChangedPath);
    auto* Multiply = Call(Graph, ChangedPath, Math(TEXT("Multiply_DoubleDouble")));
    if (!Multiply || !Connect(Graph, Clamp->GetReturnValuePin(), SetInput(SetProgress))
        || !Connect(Graph, Clamp->GetReturnValuePin(), Multiply->FindPin(TEXT("A")))) return false;
    Multiply->FindPinChecked(TEXT("B"))->DefaultValue = FString::SanitizeFloat(Binding.PrimaryStep().Transition.DurationSeconds);
    auto* SetTarget = Set(Graph, WBP, P.AnimationTarget, ChangedPath);
    if (!Connect(Graph, Multiply->GetReturnValuePin(), SetInput(SetTarget))) return false;
    auto* Target = ChangedPath.PlacePure(CreateVariableGet(Graph, WBP, P.AnimationTarget));
    auto* Animation = ChangedPath.PlacePure(CreateVariableGet(Graph, WBP, P.Animation));
    auto* Initialized = ChangedPath.PlacePure(CreateVariableGet(Graph, WBP, P.ClipInitialized));
    auto* InitializedBranch = Branch(Graph, ChangedPath, Initialized->GetValuePin());
    if (!InitializedBranch || !LinkExecChain(ChangedPath.ExecChain)
        || !ConnectExec(ChangedBranch->GetThenPin(), ChangedPath)) return false;

    FChainLayout First; First.RowY = ChangedPath.RowY + NodeRowStep; First.NextColumn = ChangedPath.NextColumn;
    if ((Binding.PrimaryStep().Transition.UsesSlide() || Binding.PrimaryStep().Transition.UsesFadeOnly())
        && !AppendPrototypeTemporaryPageVisibility(Graph, WBP, Functions, Targets, Resolved, true, First)) return false;
    auto* Play = Call(Graph, First, Functions.PlayAnimation);
    if (!Play || !Connect(Graph, Animation->GetValuePin(), Play->FindPin(TEXT("InAnimation")))
        || !Connect(Graph, Target->GetValuePin(), Play->FindPin(TEXT("StartAtTime")))) return false;
    Play->FindPinChecked(TEXT("NumLoopsToPlay"))->DefaultValue = TEXT("0");
    Play->FindPinChecked(TEXT("PlaybackSpeed"))->DefaultValue = TEXT("0.0");
    Play->FindPinChecked(TEXT("bRestoreState"))->DefaultValue = TEXT("false");
    if (!SetLiteral(Graph, WBP, P.ClipInitialized, TEXT("true"), First)
        || !ScheduleSampleNext(Graph, WBP, Functions, P, Targets, LeaderIndex, bRelease, First, true)
        || !LinkExecChain(First.ExecChain) || !ConnectExec(InitializedBranch->GetElsePin(), First)) return false;

    FChainLayout Seek; Seek.RowY = ChangedPath.RowY + NodeRowStep * 2; Seek.NextColumn = ChangedPath.NextColumn;
    auto* SetTime = Call(Graph, Seek, Functions.SetAnimationCurrentTime);
    if (!SetTime || !Connect(Graph, Animation->GetValuePin(), SetTime->FindPin(TEXT("InAnimation")))
        || !Connect(Graph, Target->GetValuePin(), SetTime->FindPin(TEXT("InTime")))) return false;
    return ScheduleSampleNext(Graph, WBP, Functions, P, Targets, LeaderIndex, bRelease, Seek, true)
        && LinkExecChain(Seek.ExecChain) && ConnectExec(InitializedBranch->GetThenPin(), Seek);
}
} // namespace DragWiringPrivate
} // namespace

FName GetDragAcceptedName(int32 BindingIndex) { return DragWiringPrivate::DragName(BindingIndex, TEXT("accepted")); }
FName GetDragSelectedName(int32 BindingIndex) { return DragWiringPrivate::DragName(BindingIndex, TEXT("selected")); }
FString GetDragStableIdentity(const FUMGPrototypeNavigationBinding& Binding)
{
    return Binding.SourcePageId + TEXT("|") + Binding.PrimaryStep().Transition.InstanceScopeId + TEXT("|") + Binding.TriggerWidgetId;
}
static FName ConfigName(const FString& Identity, const TCHAR* Suffix)
{
    return FName(*FString::Printf(TEXT("__d2u_drag_cfg_%08x_%s"), FCrc::StrCrc32(*Identity), Suffix));
}
FName GetDragSpanName(const FString& Identity) { return ConfigName(Identity, TEXT("span")); }
FName GetDragCommitThresholdName(const FString& Identity) { return ConfigName(Identity, TEXT("commit_threshold")); }
FName GetDragSlateThresholdName(const FString& Identity) { return ConfigName(Identity, TEXT("slate_threshold")); }

bool PrepareDragVariables(UWidgetBlueprint* WBP, int32 BindingIndex, const FString& StableIdentity, TArray<FString>& OutWarnings)
{
    if (!WBP || !WBP->SkeletonGeneratedClass || StableIdentity.IsEmpty() || !FSlateApplication::IsInitialized())
    {
        OutWarnings.Add(TEXT("Drag requires an initialized Slate application and blueprint skeleton.")); return false;
    }
    struct FVar { FName Name; FEdGraphPinType Type; FString Default; bool bEditable; FString Owner; };
    FEdGraphPinType Real; Real.PinCategory = UEdGraphSchema_K2::PC_Real; Real.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    FEdGraphPinType Bool; Bool.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    FEdGraphPinType Int; Int.PinCategory = UEdGraphSchema_K2::PC_Int;
    FEdGraphPinType Vector; Vector.PinCategory = UEdGraphSchema_K2::PC_Struct; Vector.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
    const TArray<FVar> Vars = {
        {DragWiringPrivate::DragName(BindingIndex,TEXT("screen")), Vector, TEXT("(X=0,Y=0)"), false, FString::FromInt(BindingIndex)},
        {DragWiringPrivate::DragName(BindingIndex,TEXT("local")), Vector, TEXT("(X=0,Y=0)"), false, FString::FromInt(BindingIndex)},
        {DragWiringPrivate::DragName(BindingIndex,TEXT("active")), Bool, TEXT("false"), false, FString::FromInt(BindingIndex)},
        {GetDragAcceptedName(BindingIndex), Bool, TEXT("false"), false, FString::FromInt(BindingIndex)},
        {DragWiringPrivate::DragName(BindingIndex,TEXT("force_cancel")), Bool, TEXT("false"), false, FString::FromInt(BindingIndex)},
        {DragWiringPrivate::DragName(BindingIndex,TEXT("clip_initialized")), Bool, TEXT("false"), false, FString::FromInt(BindingIndex)},
        {GetDragSelectedName(BindingIndex), Int, TEXT("-1"), false, FString::FromInt(BindingIndex)},
        {DragWiringPrivate::DragName(BindingIndex,TEXT("progress")), Real, TEXT("0.0"), false, FString::FromInt(BindingIndex)},
        {GetDragSpanName(StableIdentity), Real, TEXT("0.0"), true, StableIdentity},
        {GetDragCommitThresholdName(StableIdentity), Real, TEXT("0.5"), true, StableIdentity},
        {GetDragSlateThresholdName(StableIdentity), Real, FString::SanitizeFloat(FSlateApplication::Get().GetDragTriggerDistance()), true, StableIdentity}
    };
    for (const FVar& Var : Vars)
    {
        const int32 Existing = FBlueprintEditorUtils::FindNewVariableIndex(WBP, Var.Name);
        FString Marker;
        if ((Existing != INDEX_NONE && (!FBlueprintEditorUtils::GetBlueprintVariableMetaData(WBP, Var.Name, nullptr, DragWiringPrivate::DragMetadata, Marker)
                || Marker != Var.Owner || !(WBP->NewVariables[Existing].VarType == Var.Type)))
            || (Existing == INDEX_NONE && FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, Var.Name)))
        {
            OutWarnings.Add(FString::Printf(TEXT("Drag variable '%s' conflicts with user content."), *Var.Name.ToString())); return false;
        }
    }
    TArray<FName> Created;
    for (const FVar& Var : Vars)
    {
        if (FBlueprintEditorUtils::FindNewVariableIndex(WBP, Var.Name) != INDEX_NONE) continue;
        if (!FBlueprintEditorUtils::AddMemberVariable(WBP, Var.Name, Var.Type, Var.Default))
        {
            for (FName Name : Created) FBlueprintEditorUtils::RemoveMemberVariable(WBP, Name);
            OutWarnings.Add(FString::Printf(TEXT("Could not create drag variable '%s'."), *Var.Name.ToString())); return false;
        }
        Created.Add(Var.Name);
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, Var.Name, nullptr, DragWiringPrivate::DragMetadata, Var.Owner);
        const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(WBP, Var.Name);
        if (Index != INDEX_NONE && Var.bEditable)
        {
            auto& Description = WBP->NewVariables[Index];
            Description.PropertyFlags &= ~CPF_DisableEditOnInstance;
            Description.Category = FText::FromString(TEXT("Design2UMG Drag | ") + StableIdentity);
            Description.FriendlyName = Var.Name == GetDragSpanName(StableIdentity) ? TEXT("Drag distance (0 = widget width)")
                : Var.Name == GetDragCommitThresholdName(StableIdentity) ? TEXT("Drag completion threshold")
                : TEXT("Drag start threshold (screen pixels)");
            FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, Var.Name, nullptr, TEXT("ClampMin"), TEXT("0"));
            if (Var.Name == GetDragCommitThresholdName(StableIdentity))
                FBlueprintEditorUtils::SetBlueprintVariableMetaData(WBP, Var.Name, nullptr, TEXT("ClampMax"), TEXT("1"));
        }
    }
    return true;
}

namespace
{
namespace DragWiringPrivate
{
struct FValidatedCandidate
{
    const FUMGPrototypeNavigationBinding* Binding = nullptr;
    const FResolvedBinding* Resolved = nullptr;
    FDragProperties Properties;
    FName CommitEventName;
    FName CancelEventName;
    FVector2D Unit = FVector2D::ZeroVector;
    bool bDirectional = false;
};

bool ResolveDirection(const FUMGPrototypeNavigationBinding& Binding, bool bMultiple,
    FVector2D& Unit, bool& bDirectional)
{
    const FString Direction = Binding.PrimaryStep().Transition.Direction.ToUpper();
    if (Binding.PrimaryStep().Transition.UsesSlide() && Direction.IsEmpty()) return false;
    if (Direction == TEXT("LEFT")) Unit = FVector2D(-1, 0);
    else if (Direction == TEXT("RIGHT")) Unit = FVector2D(1, 0);
    else if (Direction == TEXT("TOP")) Unit = FVector2D(0, -1);
    else if (Direction == TEXT("BOTTOM")) Unit = FVector2D(0, 1);
    else if (!Direction.IsEmpty()) return false;
    else if (bMultiple)
    {
        if (Binding.PrimaryStep().Transition.bHasDragMotionVector)
            Unit = Binding.PrimaryStep().Transition.DragMotionVector.GetSafeNormal();
        else
        {
            int32 Matches = 0;
            for (const auto& Target : Binding.PrimaryStep().Transition.WidgetTargets)
                if (Target.NodeId == Binding.TriggerWidgetId && !Target.Translation.IsNearlyZero())
                {
                    Unit = Target.Translation.GetSafeNormal();
                    ++Matches;
                }
            if (Matches != 1) return false;
        }
    }
    bDirectional = !Unit.IsNearlyZero();
    return true;
}

// Select a target once, when a pointer first crosses Slate's drag threshold.
bool WireGroupSample(UEdGraph* Graph, UWidgetBlueprint* WBP, const FEngineFunctions& Functions,
    const FBindingGraphTargets& Targets, TConstArrayView<FValidatedCandidate> Candidates,
    int32 LeaderIndex, bool bRelease, FChainLayout& Entry)
{
    const FDragProperties& Common = Candidates[0].Properties;
    auto* Mouse = Call(Graph, Entry, Functions.GetMousePositionOnPlatform);
    auto* ScreenOrigin = Entry.PlacePure(CreateVariableGet(Graph, WBP, Common.Screen));
    auto* ScreenDistance = Call(Graph, Entry, Math(TEXT("Distance2D")));
    auto* Threshold = Entry.PlacePure(CreateVariableGet(Graph, WBP, Common.Trigger));
    auto* Crossed = Call(Graph, Entry, Math(TEXT("GreaterEqual_DoubleDouble")));
    auto* OldAccepted = Entry.PlacePure(CreateVariableGet(Graph, WBP, Common.Accepted));
    auto* Either = Call(Graph, Entry, Math(TEXT("BooleanOR")));
    if (!Mouse || !ScreenDistance || !Crossed || !Either
        || !Connect(Graph, Mouse->GetReturnValuePin(), ScreenDistance->FindPin(TEXT("V1")))
        || !Connect(Graph, ScreenOrigin->GetValuePin(), ScreenDistance->FindPin(TEXT("V2")))
        || !Connect(Graph, ScreenDistance->GetReturnValuePin(), Crossed->FindPin(TEXT("A")))
        || !Connect(Graph, Threshold->GetValuePin(), Crossed->FindPin(TEXT("B")))
        || !Connect(Graph, Crossed->GetReturnValuePin(), Either->FindPin(TEXT("A")))
        || !Connect(Graph, OldAccepted->GetValuePin(), Either->FindPin(TEXT("B")))) return false;
    auto* SetAccepted = Set(Graph, WBP, Common.Accepted, Entry);
    if (!Connect(Graph, Either->GetReturnValuePin(), SetInput(SetAccepted))) return false;
    auto* Accepted = Entry.PlacePure(CreateVariableGet(Graph, WBP, Common.Accepted));
    auto* AcceptedBranch = Branch(Graph, Entry, Accepted->GetValuePin());
    if (!AcceptedBranch || !LinkExecChain(Entry.ExecChain)) return false;
    FChainLayout Before; Before.RowY = Entry.RowY + NodeRowStep; Before.NextColumn = Entry.NextColumn;
    if (!ScheduleSampleNext(Graph, WBP, Functions, Common, Targets, LeaderIndex, bRelease, Before, false)
        || !LinkExecChain(Before.ExecChain) || !ConnectExec(AcceptedBranch->GetElsePin(), Before)) return false;

    FChainLayout Select; Select.RowY = Entry.RowY + NodeRowStep * 2; Select.NextColumn = Entry.NextColumn;
    auto* Current = Select.PlacePure(CreateVariableGet(Graph, WBP, Common.Selected));
    auto* IsUnset = Call(Graph, Select, Functions.EqualEqual_IntInt);
    if (!IsUnset || !Connect(Graph, Current->GetValuePin(), IsUnset->FindPin(TEXT("A")))) return false;
    IsUnset->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("-1");
    UEdGraphPin* Chosen = nullptr;
    if (Candidates.Num() > 1)
    {
        auto* Page = Select.PlacePure(CreateVariableGet(Graph, WBP, Common.Page));
        auto* Geometry = Call(Graph, Select, Functions.GetCachedGeometry);
        auto* Local = Call(Graph, Select, Slate(TEXT("AbsoluteToLocal")));
        auto* Origin = Select.PlacePure(CreateVariableGet(Graph, WBP, Common.Local));
        auto* Delta = Call(Graph, Select, Math(TEXT("Subtract_Vector2DVector2D")));
        if (!Geometry || !Local || !Delta || !Connect(Graph, Page->GetValuePin(), Geometry->FindPin(UEdGraphSchema_K2::PN_Self))
            || !Connect(Graph, Geometry->GetReturnValuePin(), Local->FindPin(TEXT("Geometry")))
            || !Connect(Graph, Mouse->GetReturnValuePin(), Local->FindPin(TEXT("AbsoluteCoordinate")))
            || !Connect(Graph, Local->GetReturnValuePin(), Delta->FindPin(TEXT("A")))
            || !Connect(Graph, Origin->GetValuePin(), Delta->FindPin(TEXT("B")))) return false;
        TArray<UEdGraphPin*> Matches;
        for (const auto& Candidate : Candidates)
        {
            auto* Dot = Call(Graph, Select, Math(TEXT("DotProduct2D")));
            auto* Positive = Call(Graph, Select, Math(TEXT("Greater_DoubleDouble")));
            if (!Dot || !Positive || !Connect(Graph, Delta->GetReturnValuePin(), Dot->FindPin(TEXT("A")))
                || !Connect(Graph, Dot->GetReturnValuePin(), Positive->FindPin(TEXT("A")))) return false;
            Dot->FindPinChecked(TEXT("B"))->DefaultValue = FString::Printf(TEXT("(X=%.17g,Y=%.17g)"), Candidate.Unit.X, Candidate.Unit.Y);
            Positive->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("0.0");
            Matches.Add(Positive->GetReturnValuePin());
        }
        for (int32 I = 0; I < Matches.Num(); ++I)
        {
            UEdGraphPin* Unique = Matches[I];
            for (int32 J = 0; J < Matches.Num(); ++J)
            {
                if (I == J) continue;
                auto* Not = Call(Graph, Select, Math(TEXT("Not_PreBool")));
                auto* And = Call(Graph, Select, Math(TEXT("BooleanAND")));
                if (!Not || !And || !Connect(Graph, Matches[J], Not->FindPin(TEXT("A")))
                    || !Connect(Graph, Unique, And->FindPin(TEXT("A")))
                    || !Connect(Graph, Not->GetReturnValuePin(), And->FindPin(TEXT("B")))) return false;
                Unique = And->GetReturnValuePin();
            }
            auto* Pick = Call(Graph, Select, Math(TEXT("SelectInt")));
            if (!Pick || !Connect(Graph, Unique, Pick->FindPin(TEXT("bPickA")))) return false;
            Pick->FindPinChecked(TEXT("A"))->DefaultValue = FString::FromInt(I);
            if (Chosen) { if (!Connect(Graph, Chosen, Pick->FindPin(TEXT("B")))) return false; }
            else Pick->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("-1");
            Chosen = Pick->GetReturnValuePin();
        }
    }
    auto* PickCurrent = Call(Graph, Select, Math(TEXT("SelectInt")));
    if (!PickCurrent || !Connect(Graph, IsUnset->GetReturnValuePin(), PickCurrent->FindPin(TEXT("bPickA")))
        || !Connect(Graph, Current->GetValuePin(), PickCurrent->FindPin(TEXT("B")))) return false;
    if (Chosen) { if (!Connect(Graph, Chosen, PickCurrent->FindPin(TEXT("A")))) return false; }
    else PickCurrent->FindPinChecked(TEXT("A"))->DefaultValue = TEXT("0");
    auto* SetSelected = Set(Graph, WBP, Common.Selected, Select);
    if (!Connect(Graph, PickCurrent->GetReturnValuePin(), SetInput(SetSelected))) return false;
    auto* Selected = Select.PlacePure(CreateVariableGet(Graph, WBP, Common.Selected));
    auto* Switch = CreateMarkerNode<UK2Node_SwitchInteger>(Graph, [](UK2Node_SwitchInteger* Node) { Node->StartIndex = -1; });
    Select.PlaceExec(Switch);
    if (!Connect(Graph, Selected->GetValuePin(), Switch->GetSelectionPin())
        || !LinkExecChain(Select.ExecChain) || !ConnectExec(AcceptedBranch->GetThenPin(), Select)) return false;
    Switch->AddPinToSwitchNode();
    FChainLayout Ambiguous; Ambiguous.RowY = Select.RowY + NodeRowStep; Ambiguous.NextColumn = Select.NextColumn;
    auto* Report = Call(Graph, Ambiguous, UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
    if (!Report) return false;
    Report->FindPinChecked(TEXT("InString"))->DefaultValue = TEXT("Design2UMG: drag direction did not uniquely select a target; gesture cancelled.");
    Report->FindPinChecked(TEXT("bPrintToScreen"))->DefaultValue = TEXT("false");
    Report->FindPinChecked(TEXT("bPrintToLog"))->DefaultValue = TEXT("true");
    if (!SetLiteral(Graph, WBP, Common.Active, TEXT("false"), Ambiguous)
        || !SetLiteral(Graph, WBP, Targets.TransitionOwnerProperty, TEXT("0"), Ambiguous)
        || !LinkExecChain(Ambiguous.ExecChain)
        || !ConnectExec(Switch->FindPin(Switch->GetPinNameGivenIndex(-1), EGPD_Output), Ambiguous)) return false;
    for (int32 I = 0; I < Candidates.Num(); ++I)
    {
        Switch->AddPinToSwitchNode();
        FChainLayout Sample; Sample.RowY = Select.RowY + (I + 2) * NodeRowStep; Sample.NextColumn = Select.NextColumn;
        // Sample owns its first execution node, which is the current mouse-position call.
        if (!AppendSample(Graph, WBP, Functions, Candidates[I].Properties, Targets, *Candidates[I].Binding,
                *Candidates[I].Resolved, LeaderIndex, Candidates[I].Unit, Candidates[I].bDirectional, bRelease, Mouse->GetReturnValuePin(), Sample)
            || !ConnectExec(Switch->FindPin(Switch->GetPinNameGivenIndex(I), EGPD_Output), Sample)) return false;
    }
    return true;
}
} // namespace DragWiringPrivate
} // namespace

bool AppendDragCleanup(UEdGraph* Graph, UWidgetBlueprint* WBP, TConstArrayView<FDragCandidate> Candidates,
    const FEngineFunctions& Functions, FChainLayout& Layout)
{
    if (!Graph || !WBP || !WBP->SkeletonGeneratedClass || !Functions.ClearTimer || Candidates.IsEmpty()
        || !Candidates[0].Resolved) return false;
    const int32 Leader = Candidates[0].Resolved->BindingIndex;
    auto* Active = FindFProperty<FBoolProperty>(WBP->SkeletonGeneratedClass, DragWiringPrivate::DragName(Leader, TEXT("active")));
    if (!Active || !DragWiringPrivate::Timer(Graph, Layout, Functions.ClearTimer, DragWiringPrivate::MonitorName(Leader))
        || !DragWiringPrivate::Timer(Graph, Layout, Functions.ClearTimer, DragWiringPrivate::ReleaseName(Leader))) return false;
    for (const auto& Candidate : Candidates)
        for (FName Name : {Candidate.CommitEventName, Candidate.CancelEventName})
            if (!Name.IsNone() && !DragWiringPrivate::Timer(Graph, Layout, Functions.ClearTimer, Name)) return false;
    return DragWiringPrivate::SetLiteral(Graph, WBP, Active, TEXT("false"), Layout);
}

bool WireDragBinding(UEdGraph* Graph, UWidgetBlueprint* WBP, TConstArrayView<FDragCandidate> Candidates,
    const FBindingGraphTargets& Targets, const FEngineFunctions& Functions, TArray<FString>& OutWarnings)
{
    if (!Graph || !WBP || !WBP->SkeletonGeneratedClass || !WBP->WidgetTree || Candidates.IsEmpty()
        || !Candidates[0].Binding || !Candidates[0].Resolved || !Targets.CurrentPageProperty
        || !Targets.TransitionOwnerProperty || !Functions.EqualEqual_IntInt || !Functions.GetCachedGeometry
        || !Functions.GetMousePositionOnPlatform || !Functions.SetTimer || !Functions.SetTimerForNextTick
        || !Functions.ClearTimer || !Functions.PlayAnimation || !Functions.SetAnimationCurrentTime)
    {
        OutWarnings.Add(TEXT("Drag requires native mouse, animation, timer and page ownership functions.")); return false;
    }
    const auto& Leader = *Candidates[0].Resolved;
    const int32 LeaderIndex = Leader.BindingIndex;
    TArray<DragWiringPrivate::FValidatedCandidate> Valid;
    for (const FDragCandidate& Candidate : Candidates)
    {
        if (!Candidate.Binding || !Candidate.Resolved || !Candidate.Binding->UsesDragTrigger()
            || !(Candidate.Binding->PrimaryStep().Transition.Type.Equals(TEXT("SMART_ANIMATE"), ESearchCase::IgnoreCase)
                || Candidate.Binding->PrimaryStep().Transition.UsesSlide() || Candidate.Binding->PrimaryStep().Transition.UsesFadeOnly())
            || !Candidate.Binding->PrimaryStep().Transition.Diagnostic.IsEmpty()
            || Candidate.Binding->PrimaryStep().Transition.DurationSeconds <= KINDA_SMALL_NUMBER
            || Candidate.CommitEventName.IsNone() || Candidate.CancelEventName.IsNone()
            || Candidate.Resolved->TriggerWidgetName != Leader.TriggerWidgetName
            || Candidate.Resolved->SourcePageIndex != Leader.SourcePageIndex
            || GetDragStableIdentity(*Candidate.Binding) != GetDragStableIdentity(*Candidates[0].Binding)
            || Candidate.Resolved->StateHostIndex != Leader.StateHostIndex
            || Candidate.Resolved->SourceStateIndex != Leader.SourceStateIndex)
        {
            OutWarnings.Add(TEXT("Drag group requires compatible source, a supported native transition and completion callbacks.")); return false;
        }
        DragWiringPrivate::FValidatedCandidate Item;
        Item.Binding = Candidate.Binding; Item.Resolved = Candidate.Resolved;
        Item.CommitEventName = Candidate.CommitEventName; Item.CancelEventName = Candidate.CancelEventName;
        Item.Properties = DragWiringPrivate::Resolve(WBP, LeaderIndex, GetDragStableIdentity(*Candidates[0].Binding), *Candidate.Resolved, Targets);
        int32 ClipMatches = 0;
        const UWidgetAnimation* Clip = nullptr;
        for (const UWidgetAnimation* Animation : WBP->Animations)
            if (Animation && Animation->GetFName() == Candidate.Resolved->WidgetAnimationName && Animation->MovieScene)
            {
                ++ClipMatches;
                Clip = Animation;
            }
        const bool bTwoPages = Candidate.Binding->PrimaryStep().Transition.UsesSlide() || Candidate.Binding->PrimaryStep().Transition.UsesFadeOnly();
        bool bValidPages = !bTwoPages || (Candidate.Binding->Steps.Num() == 1
            && Candidate.Binding->Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate
            && Candidate.Resolved->StateHostIndex == INDEX_NONE
            && Targets.PageHostPages.IsValidIndex(Candidate.Resolved->SourcePageIndex)
            && Targets.PageHostPages.IsValidIndex(Candidate.Resolved->TargetPageIndex)
            && Targets.PageHostPages[Candidate.Resolved->SourcePageIndex]
            && Targets.PageHostPages[Candidate.Resolved->TargetPageIndex]
            && Candidate.Resolved->SourcePageIndex != Candidate.Resolved->TargetPageIndex);
        if (bTwoPages && bValidPages && ClipMatches == 1)
        {
            const FName Source = Targets.PageHostPages[Candidate.Resolved->SourcePageIndex]->GetFName();
            const FName Target = Targets.PageHostPages[Candidate.Resolved->TargetPageIndex]->GetFName();
            bValidPages = Clip->AnimationBindings.ContainsByPredicate([&](const auto& Entry) { return Entry.WidgetName == Source; })
                && Clip->AnimationBindings.ContainsByPredicate([&](const auto& Entry) { return Entry.WidgetName == Target; });
        }
        if (!DragWiringPrivate::PropertiesValid(Item.Properties) || ClipMatches != 1 || !bValidPages
            || !DragWiringPrivate::ResolveDirection(*Candidate.Binding, Candidates.Num() > 1, Item.Unit, Item.bDirectional))
        {
            OutWarnings.Add(TEXT("Drag group lacks a unique native clip, valid pages, animation state or a reliable source direction.")); return false;
        }
        for (const auto& Prior : Valid)
            if ((Prior.Unit - Item.Unit).IsNearlyZero())
            {
                OutWarnings.Add(TEXT("Drag targets have indistinguishable source directions; no target was chosen.")); return false;
            }
        Valid.Add(MoveTemp(Item));
    }
    if (Leader.StateHostIndex != INDEX_NONE && (!Targets.StateHosts.IsValidIndex(Leader.StateHostIndex)
        || !Targets.StateHosts[Leader.StateHostIndex].CurrentStateProperty))
    {
        OutWarnings.Add(TEXT("Drag source state is unavailable.")); return false;
    }
    const DragWiringPrivate::FDragProperties& P = Valid[0].Properties;
    for (FName Delegate : {GET_MEMBER_NAME_CHECKED(UButton, OnPressed), GET_MEMBER_NAME_CHECKED(UButton, OnReleased)})
        if (FKismetEditorUtilities::FindBoundEventForComponent(WBP, Delegate, Leader.TriggerWidgetName))
        {
            OutWarnings.Add(FString::Printf(TEXT("Drag event '%s' already has a binding; existing wiring is preserved."), *Delegate.ToString())); return false;
        }
    for (FName Name : {DragWiringPrivate::MonitorName(LeaderIndex), DragWiringPrivate::ReleaseName(LeaderIndex)})
    {
        bool bConflict = WBP->SkeletonGeneratedClass->FindFunctionByName(Name)
            || FindFProperty<FProperty>(WBP->SkeletonGeneratedClass, Name);
        for (const UEdGraph* ExistingGraph : WBP->UbergraphPages)
            if (ExistingGraph) for (const UEdGraphNode* ExistingNode : ExistingGraph->Nodes)
                if (const auto* Event = Cast<UK2Node_CustomEvent>(ExistingNode)) bConflict |= Event->CustomFunctionName == Name;
        if (bConflict)
        {
            OutWarnings.Add(FString::Printf(TEXT("Drag event '%s' conflicts with existing content."), *Name.ToString())); return false;
        }
    }
    for (int32 I = 0; I < Valid.Num(); ++I)
        for (int32 J = I + 1; J < Valid.Num(); ++J)
            if (FVector2D::DotProduct(Valid[I].Unit, Valid[J].Unit) > -1.0 + KINDA_SMALL_NUMBER)
                OutWarnings.AddUnique(TEXT("Some drag directions may match multiple source targets; ambiguous movement is cancelled."));
    TSet<UEdGraphNode*> Existing;
    for (UEdGraphNode* Node : Graph->Nodes) Existing.Add(Node);
    bool bSucceeded = false;
    ON_SCOPE_EXIT
    {
        TArray<UEdGraphNode*> Added;
        for (UEdGraphNode* Node : Graph->Nodes) if (Node && !Existing.Contains(Node)) Added.Add(Node);
        if (!bSucceeded) { for (UEdGraphNode* Node : Added) FBlueprintEditorUtils::RemoveNode(WBP, Node, true); }
        else for (UEdGraphNode* Node : Added) if (Node->NodeComment.IsEmpty()) Node->NodeComment = NodeMarkerComment;
    };
    const int32 Owner = LeaderIndex + 1;
    auto CreateButtonEvent = [&](FName Delegate) -> UK2Node_ComponentBoundEvent*
    {
        auto* Signature = FindFProperty<FMulticastDelegateProperty>(UButton::StaticClass(), Delegate);
        if (!Signature) return nullptr;
        return CreateMarkerNode<UK2Node_ComponentBoundEvent>(Graph,
            [&](UK2Node_ComponentBoundEvent* Node) { Node->InitializeComponentBoundEventParams(P.Button, Signature); });
    };
    auto GateSource = [&](FChainLayout& Chain) -> bool
    {
        if (!DragWiringPrivate::OwnerGate(Graph, WBP, Functions, Targets.CurrentPageProperty, Leader.SourcePageIndex, Chain)) return false;
        if (Leader.StateHostIndex != INDEX_NONE)
            if (!DragWiringPrivate::OwnerGate(Graph, WBP, Functions, Targets.StateHosts[Leader.StateHostIndex].CurrentStateProperty,
                Leader.SourceStateIndex, Chain)) return false;
        return true;
    };
    auto* Press = CreateButtonEvent(GET_MEMBER_NAME_CHECKED(UButton, OnPressed));
    auto* Release = CreateButtonEvent(GET_MEMBER_NAME_CHECKED(UButton, OnReleased));
    auto* Monitor = CreateMarkerNode<UK2Node_CustomEvent>(Graph,
        [&](UK2Node_CustomEvent* Node) { Node->CustomFunctionName = DragWiringPrivate::MonitorName(LeaderIndex); });
    auto* Released = CreateMarkerNode<UK2Node_CustomEvent>(Graph,
        [&](UK2Node_CustomEvent* Node) { Node->CustomFunctionName = DragWiringPrivate::ReleaseName(LeaderIndex); });
    if (!Press || !Release || !Monitor || !Released) return false;
    FChainLayout Start; Start.RowY = 0; Start.PlaceExec(Press);
    if (!GateSource(Start) || !DragWiringPrivate::OwnerGate(Graph, WBP, Functions, Targets.TransitionOwnerProperty, 0, Start)) return false;
    if (!DragWiringPrivate::Timer(Graph, Start, Functions.ClearTimer, DragWiringPrivate::MonitorName(LeaderIndex))
        || !DragWiringPrivate::Timer(Graph, Start, Functions.ClearTimer, DragWiringPrivate::ReleaseName(LeaderIndex))) return false;
    for (const auto& Candidate : Valid)
        if (!DragWiringPrivate::Timer(Graph, Start, Functions.ClearTimer, Candidate.CommitEventName)
            || !DragWiringPrivate::Timer(Graph, Start, Functions.ClearTimer, Candidate.CancelEventName)) return false;
    if (!DragWiringPrivate::SetLiteral(Graph, WBP, P.Active, TEXT("true"), Start)
        || !DragWiringPrivate::SetLiteral(Graph, WBP, P.Accepted, TEXT("false"), Start)
        || !DragWiringPrivate::SetLiteral(Graph, WBP, P.ForceCancel, TEXT("false"), Start)
        || !DragWiringPrivate::SetLiteral(Graph, WBP, P.ClipInitialized, TEXT("false"), Start)
        || !DragWiringPrivate::SetLiteral(Graph, WBP, P.Selected, TEXT("-1"), Start)
        || !DragWiringPrivate::SetLiteral(Graph, WBP, P.Progress, TEXT("0.0"), Start)
        || !DragWiringPrivate::SetLiteral(Graph, WBP, Targets.TransitionOwnerProperty, FString::FromInt(Owner), Start)) return false;
    for (const auto& Candidate : Valid)
        if (!DragWiringPrivate::SetLiteral(Graph, WBP, Candidate.Properties.AnimationTarget, TEXT("0.0"), Start)
            || !DragWiringPrivate::SetLiteral(Graph, WBP, Candidate.Properties.AnimationStart, TEXT("0.0"), Start)) return false;
    auto* Mouse = DragWiringPrivate::Call(Graph, Start, Functions.GetMousePositionOnPlatform);
    auto* SaveScreen = DragWiringPrivate::Set(Graph, WBP, P.Screen, Start);
    auto* Page = Start.PlacePure(CreateVariableGet(Graph, WBP, P.Page));
    auto* Geometry = DragWiringPrivate::Call(Graph, Start, Functions.GetCachedGeometry);
    auto* Local = DragWiringPrivate::Call(Graph, Start, DragWiringPrivate::Slate(TEXT("AbsoluteToLocal")));
    auto* SaveLocal = DragWiringPrivate::Set(Graph, WBP, P.Local, Start);
    if (!Mouse || !Geometry || !Local || !DragWiringPrivate::Connect(Graph, Mouse->GetReturnValuePin(), DragWiringPrivate::SetInput(SaveScreen))
        || !DragWiringPrivate::Connect(Graph, Page->GetValuePin(), Geometry->FindPin(UEdGraphSchema_K2::PN_Self))
        || !DragWiringPrivate::Connect(Graph, Geometry->GetReturnValuePin(), Local->FindPin(TEXT("Geometry")))
        || !DragWiringPrivate::Connect(Graph, Mouse->GetReturnValuePin(), Local->FindPin(TEXT("AbsoluteCoordinate")))
        || !DragWiringPrivate::Connect(Graph, Local->GetReturnValuePin(), DragWiringPrivate::SetInput(SaveLocal))
        || !DragWiringPrivate::Timer(Graph, Start, Functions.SetTimerForNextTick, DragWiringPrivate::MonitorName(LeaderIndex))
        || !LinkExecChain(Start.ExecChain)) return false;
    FChainLayout Tick; Tick.RowY = NodeRowStep * 4; Tick.PlaceExec(Monitor);
    if (!GateSource(Tick) || !DragWiringPrivate::OwnerGate(Graph, WBP, Functions, Targets.TransitionOwnerProperty, Owner, Tick)) return false;
    auto* Active = Tick.PlacePure(CreateVariableGet(Graph, WBP, P.Active));
    if (!DragWiringPrivate::Branch(Graph, Tick, Active->GetValuePin())
        || !DragWiringPrivate::WireGroupSample(Graph, WBP, Functions, Targets, Valid, LeaderIndex, false, Tick)) return false;
    FChainLayout Up; Up.RowY = NodeRowStep * 10; Up.PlaceExec(Release);
    if (!DragWiringPrivate::OwnerGate(Graph, WBP, Functions, Targets.TransitionOwnerProperty, Owner, Up)) return false;
    auto* WasActive = Up.PlacePure(CreateVariableGet(Graph, WBP, P.Active));
    if (!DragWiringPrivate::Branch(Graph, Up, WasActive->GetValuePin()) || !DragWiringPrivate::SetLiteral(Graph, WBP, P.Active, TEXT("false"), Up)
        || !DragWiringPrivate::Timer(Graph, Up, Functions.ClearTimer, DragWiringPrivate::MonitorName(LeaderIndex))) return false;
    auto* ButtonGet = Up.PlacePure(CreateVariableGet(Graph, WBP, P.Button));
    auto* HasCapture = DragWiringPrivate::Call(Graph, Up, UWidget::StaticClass()->FindFunctionByName(TEXT("HasMouseCapture")));
    if (!HasCapture || !DragWiringPrivate::Connect(Graph, ButtonGet->GetValuePin(), HasCapture->FindPin(UEdGraphSchema_K2::PN_Self))) return false;
    auto* CaptureBranch = DragWiringPrivate::Branch(Graph, Up, HasCapture->GetReturnValuePin());
    if (!CaptureBranch || !DragWiringPrivate::WireGroupSample(Graph, WBP, Functions, Targets, Valid, LeaderIndex, true, Up)) return false;
    FChainLayout Lost; Lost.RowY = Up.RowY + NodeRowStep; Lost.NextColumn = Up.NextColumn;
    if (!DragWiringPrivate::SetLiteral(Graph, WBP, P.ForceCancel, TEXT("true"), Lost)) return false;
    auto* AcceptedOnLoss = Lost.PlacePure(CreateVariableGet(Graph, WBP, P.Accepted));
    auto* LostBranch = DragWiringPrivate::Branch(Graph, Lost, AcceptedOnLoss->GetValuePin());
    if (!LostBranch || !LinkExecChain(Lost.ExecChain) || !DragWiringPrivate::ConnectExec(CaptureBranch->GetElsePin(), Lost)) return false;
    FChainLayout LostDrag; LostDrag.RowY = Lost.RowY + NodeRowStep; LostDrag.NextColumn = Lost.NextColumn;
    if (!DragWiringPrivate::Timer(Graph, LostDrag, Functions.SetTimerForNextTick, DragWiringPrivate::ReleaseName(LeaderIndex))
        || !LinkExecChain(LostDrag.ExecChain) || !DragWiringPrivate::ConnectExec(LostBranch->GetThenPin(), LostDrag)) return false;
    FChainLayout LostClick; LostClick.RowY = Lost.RowY + NodeRowStep * 2; LostClick.NextColumn = Lost.NextColumn;
    if (!DragWiringPrivate::SetLiteral(Graph, WBP, Targets.TransitionOwnerProperty, TEXT("0"), LostClick)
        || !LinkExecChain(LostClick.ExecChain) || !DragWiringPrivate::ConnectExec(LostBranch->GetElsePin(), LostClick)) return false;

    FChainLayout Decision; Decision.RowY = NodeRowStep * 16; Decision.PlaceExec(Released);
    if (!DragWiringPrivate::OwnerGate(Graph, WBP, Functions, Targets.TransitionOwnerProperty, Owner, Decision)) return false;
    auto* Selected = Decision.PlacePure(CreateVariableGet(Graph, WBP, P.Selected));
    auto* Switch = CreateMarkerNode<UK2Node_SwitchInteger>(Graph, [](UK2Node_SwitchInteger* Node) { Node->StartIndex = 0; });
    Decision.PlaceExec(Switch);
    if (!DragWiringPrivate::Connect(Graph, Selected->GetValuePin(), Switch->GetSelectionPin()) || !LinkExecChain(Decision.ExecChain)) return false;
    for (int32 I = 0; I < Valid.Num(); ++I)
    {
        Switch->AddPinToSwitchNode();
        const auto& Candidate = Valid[I];
        FChainLayout Pick; Pick.RowY = Decision.RowY + (I + 1) * NodeRowStep; Pick.NextColumn = Decision.NextColumn;
        auto* Progress = Pick.PlacePure(CreateVariableGet(Graph, WBP, P.Progress));
        auto* Limit = Pick.PlacePure(CreateVariableGet(Graph, WBP, P.Threshold));
        auto* Meets = DragWiringPrivate::Call(Graph, Pick, DragWiringPrivate::Math(TEXT("GreaterEqual_DoubleDouble")));
        if (!Meets || !DragWiringPrivate::Connect(Graph, Progress->GetValuePin(), Meets->FindPin(TEXT("A")))
            || !DragWiringPrivate::Connect(Graph, Limit->GetValuePin(), Meets->FindPin(TEXT("B")))) return false;
        auto* ForceCancel = Pick.PlacePure(CreateVariableGet(Graph, WBP, P.ForceCancel));
        auto* NotCancelled = DragWiringPrivate::Call(Graph, Pick, DragWiringPrivate::Math(TEXT("Not_PreBool")));
        auto* CanCommit = DragWiringPrivate::Call(Graph, Pick, DragWiringPrivate::Math(TEXT("BooleanAND")));
        if (!NotCancelled || !CanCommit || !DragWiringPrivate::Connect(Graph, ForceCancel->GetValuePin(), NotCancelled->FindPin(TEXT("A")))
            || !DragWiringPrivate::Connect(Graph, Meets->GetReturnValuePin(), CanCommit->FindPin(TEXT("A")))
            || !DragWiringPrivate::Connect(Graph, NotCancelled->GetReturnValuePin(), CanCommit->FindPin(TEXT("B")))) return false;
        auto* Commit = DragWiringPrivate::Branch(Graph, Pick, CanCommit->GetReturnValuePin());
        if (!Commit || !LinkExecChain(Pick.ExecChain)
            || !DragWiringPrivate::ConnectExec(Switch->FindPin(Switch->GetPinNameGivenIndex(I), EGPD_Output), Pick)) return false;
        auto Finish = [&](bool bCommit) -> bool
        {
            FChainLayout Path; Path.RowY = Pick.RowY + (bCommit ? NodeRowStep : NodeRowStep * 2); Path.NextColumn = Pick.NextColumn;
            const FName Event = bCommit ? Candidate.CommitEventName : Candidate.CancelEventName;
            if (!DragWiringPrivate::Timer(Graph, Path, Functions.ClearTimer, Event)) return false;
            UEdGraphPin* RemainingFraction = Progress->GetValuePin();
            if (bCommit)
            {
                auto* Subtract = DragWiringPrivate::Call(Graph, Path, DragWiringPrivate::Math(TEXT("Subtract_DoubleDouble")));
                if (!Subtract || !DragWiringPrivate::Connect(Graph, RemainingFraction, Subtract->FindPin(TEXT("B")))) return false;
                Subtract->FindPinChecked(TEXT("A"))->DefaultValue = TEXT("1.0");
                RemainingFraction = Subtract->GetReturnValuePin();
            }
            auto* Remaining = DragWiringPrivate::Call(Graph, Path, DragWiringPrivate::Math(TEXT("Multiply_DoubleDouble")));
            auto* IsZero = DragWiringPrivate::Call(Graph, Path, DragWiringPrivate::Math(TEXT("LessEqual_DoubleDouble")));
            if (!Remaining || !IsZero || !DragWiringPrivate::Connect(Graph, RemainingFraction, Remaining->FindPin(TEXT("A")))
                || !DragWiringPrivate::Connect(Graph, Remaining->GetReturnValuePin(), IsZero->FindPin(TEXT("A")))) return false;
            Remaining->FindPinChecked(TEXT("B"))->DefaultValue = FString::SanitizeFloat(Candidate.Binding->PrimaryStep().Transition.DurationSeconds);
            IsZero->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("0.0");
            auto* ZeroBranch = DragWiringPrivate::Branch(Graph, Path, IsZero->GetReturnValuePin());
            if (!ZeroBranch || !LinkExecChain(Path.ExecChain)) return false;

            FChainLayout ImmediateFinish; ImmediateFinish.RowY = Path.RowY + NodeRowStep; ImmediateFinish.NextColumn = Path.NextColumn;
            if (!DragWiringPrivate::Timer(Graph, ImmediateFinish, Functions.SetTimerForNextTick, Event)
                || !LinkExecChain(ImmediateFinish.ExecChain)
                || !DragWiringPrivate::ConnectExec(ZeroBranch->GetThenPin(), ImmediateFinish)) return false;

            FChainLayout ContinueAnimation; ContinueAnimation.RowY = Path.RowY + NodeRowStep * 2; ContinueAnimation.NextColumn = Path.NextColumn;
            auto* Delay = DragWiringPrivate::Call(Graph, ContinueAnimation, Functions.SetTimer);
            if (!Delay || !Delay->FindPin(TEXT("FunctionName"), EGPD_Input)
                || !DragWiringPrivate::Connect(Graph, Remaining->GetReturnValuePin(), Delay->FindPin(TEXT("Time"), EGPD_Input))) return false;
            Delay->FindPinChecked(TEXT("FunctionName"), EGPD_Input)->DefaultValue = Event.ToString();
            if (!WireAnimationTimeTarget(Graph, WBP, Candidate.Properties.Animation,
                    Candidate.Properties.AnimationTarget, Candidate.Properties.AnimationStart, nullptr,
                    bCommit ? Candidate.Binding->PrimaryStep().Transition.DurationSeconds : 0.0f, 1.0f, ContinueAnimation)
                || !LinkExecChain(ContinueAnimation.ExecChain)
                || !DragWiringPrivate::ConnectExec(ZeroBranch->GetElsePin(), ContinueAnimation)) return false;
            if (bCommit) return DragWiringPrivate::ConnectExec(Commit->GetThenPin(), Path);
            FChainLayout NeedsRewind; NeedsRewind.RowY = Pick.RowY + NodeRowStep * 3; NeedsRewind.NextColumn = Pick.NextColumn;
            auto* Initialized = NeedsRewind.PlacePure(CreateVariableGet(Graph, WBP, P.ClipInitialized));
            auto* AboveZero = DragWiringPrivate::Call(Graph, NeedsRewind, DragWiringPrivate::Math(TEXT("Greater_DoubleDouble")));
            auto* And = DragWiringPrivate::Call(Graph, NeedsRewind, DragWiringPrivate::Math(TEXT("BooleanAND")));
            if (!AboveZero || !And || !DragWiringPrivate::Connect(Graph, Progress->GetValuePin(), AboveZero->FindPin(TEXT("A")))
                || !DragWiringPrivate::Connect(Graph, Initialized->GetValuePin(), And->FindPin(TEXT("A")))
                || !DragWiringPrivate::Connect(Graph, AboveZero->GetReturnValuePin(), And->FindPin(TEXT("B")))) return false;
            AboveZero->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("0.0");
            auto* RewindBranch = DragWiringPrivate::Branch(Graph, NeedsRewind, And->GetReturnValuePin());
            if (!RewindBranch || !LinkExecChain(NeedsRewind.ExecChain) || !DragWiringPrivate::ConnectExec(Commit->GetElsePin(), NeedsRewind)
                || !DragWiringPrivate::ConnectExec(RewindBranch->GetThenPin(), Path)) return false;
            FChainLayout Immediate; Immediate.RowY = Pick.RowY + NodeRowStep * 4; Immediate.NextColumn = NeedsRewind.NextColumn;
            return DragWiringPrivate::Timer(Graph, Immediate, Functions.SetTimerForNextTick, Event)
                && LinkExecChain(Immediate.ExecChain) && DragWiringPrivate::ConnectExec(RewindBranch->GetElsePin(), Immediate);
        };
        if (!Finish(true) || !Finish(false)) return false;
    }
    bSucceeded = true;
    return true;
}
} // namespace Design2UMGPrototypeGraphWiring
