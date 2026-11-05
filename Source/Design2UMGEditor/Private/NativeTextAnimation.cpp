// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "NativeTextAnimation.h"
#include "Animation/WidgetAnimation.h"
#include "Components/TextBlock.h"
#include "Components/RetainerBox.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Design2UMGWidgetMetadata.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_SetFieldsInStruct.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MovieScene.h"
#include "MovieSceneEventUtils.h"
#include "Sections/MovieSceneEventRepeaterSection.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "WidgetBlueprint.h"

namespace NativeTextAnimation
{
namespace
{
const TCHAR* RefreshMarker = TEXT("Design2UMG.NativeTextRefresh");

FName MemberName(const UWidgetAnimation* Animation, const TCHAR* Kind, const UWidget* Owner, const TCHAR* Channel)
{
    return *FString::Printf(TEXT("%s_%s_%s_%s"), *Animation->GetName(), Kind, *Owner->GetName(), Channel);
}

FGuid RootBinding(UWidgetBlueprint* Blueprint, UWidgetAnimation* Animation)
{
    if (const auto* Root = Animation->AnimationBindings.FindByPredicate([](const auto& Binding) { return Binding.bIsRootWidget; }))
        return Root->AnimationGuid;
    FWidgetAnimationBinding Binding;
    Binding.AnimationGuid = Animation->MovieScene->AddPossessable(Blueprint->GetName(), Blueprint->SkeletonGeneratedClass);
    Binding.bIsRootWidget = true;
    Animation->AnimationBindings.Add(Binding);
    return Binding.AnimationGuid;
}

void AttachRefresh(UWidgetBlueprint* Blueprint, UWidgetAnimation* Animation, UK2Node_FunctionEntry* Entry)
{
    Design2UMGWidgetMetadata::PackageMetadata(Entry->GetGraph()).SetValue(Entry->GetGraph(), RefreshMarker, TEXT("1"));
    auto* Events = Animation->MovieScene->AddTrack<UMovieSceneEventTrack>();
    Events->EventPosition = EFireEventsAtPosition::AtEndOfEvaluation;
    auto* Repeater = NewObject<UMovieSceneEventRepeaterSection>(Events);
    Repeater->SetRange(Animation->MovieScene->GetPlaybackRange());
    Events->AddSection(*Repeater);
    FMovieSceneEventUtils::BindEventSectionToBlueprint(Repeater, Blueprint);
    FMovieSceneEventUtils::SetEndpoint(&Repeater->Event, Repeater, Entry, nullptr);
}

template<class T> T* Node(UEdGraph* Graph)
{
    auto* Result = NewObject<T>(Graph); Graph->AddNode(Result, false, false);
    Result->CreateNewGuid(); return Result;
}

UK2Node_CallFunction* Call(UEdGraph* Graph, UFunction* Function)
{
    auto* Result = Node<UK2Node_CallFunction>(Graph);
    Result->SetFromFunction(Function); Result->AllocateDefaultPins(); return Result;
}

UK2Node_VariableGet* Member(UEdGraph* Graph, FName Name, UClass* Owner = nullptr)
{
    auto* Result = Node<UK2Node_VariableGet>(Graph);
    if (Owner) Result->VariableReference.SetExternalMember(Name, Owner);
    else Result->VariableReference.SetSelfMember(Name);
    Result->AllocateDefaultPins(); return Result;
}

}

bool FRefreshBuilder::AddMember(UWidgetAnimation* Animation, const FChannel& Channel,
    UMGPrototypeAnimationBuilder::FChannelWriter WriteChannel, FString& Error)
{
    if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Channel.Member) != INDEX_NONE)
    {
        Error = TEXT("Native refresh member conflicts with an existing member: ") + Channel.Member.ToString();
        return false;
    }
    // AddMemberVariable would regenerate the skeleton per member; Build regenerates it once for all of them.
    FBPVariableDescription Variable;
    Variable.VarName = Channel.Member;
    Variable.VarGuid = FGuid::NewGuid();
    Variable.FriendlyName = FName::NameToDisplayString(Channel.Member.ToString(), false);
    Variable.VarType.PinCategory = UEdGraphSchema_K2::PC_Real;
    Variable.VarType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    Variable.PropertyFlags |= CPF_Edit | CPF_BlueprintVisible | CPF_DisableEditOnInstance;
    Variable.Category = UEdGraphSchema_K2::VR_DefaultCategory;
    Variable.DefaultValue = FString::SanitizeFloat(Channel.Start);
    Blueprint->Modify();
    Blueprint->NewVariables.Add(MoveTemp(Variable));
    UMGPrototypeAnimationBuilder::AddFloatTrack(Animation, RootBinding(Blueprint, Animation), Channel.Member,
        Channel.Member.ToString(), Channel.Start, Channel.End, WriteChannel);
    return true;
}

bool FRefreshBuilder::AddFontTracks(UWidgetAnimation* Animation, UTextBlock* Source, UTextBlock* Destination,
    bool bSize, bool bTracking, UMGPrototypeAnimationBuilder::FChannelWriter WriteChannel, FString& Error)
{
    if (!bSize && !bTracking) return true;
    const FName Tracking = bTracking ? MemberName(Animation, TEXT("font"), Source, TEXT("tracking")) : NAME_None;
    if (bTracking && !AddMember(Animation, {Tracking, NAME_None, float(Source->GetFont().LetterSpacing),
        float(Destination->GetFont().LetterSpacing)}, WriteChannel, Error)) return false;
    if (bSize) UMGPrototypeAnimationBuilder::AddFloatTrack(Animation, UMGPrototypeAnimationBuilder::BindWidget(Animation, Source),
        TEXT("Size"), TEXT("Font.Size"), Source->GetFont().Size, Destination->GetFont().Size, WriteChannel);
    Source->bIsVariable = true;
    Clips.FindOrAdd(Animation).Fonts.Add({Source, Tracking});
    return true;
}

bool FRefreshBuilder::AddPaint(UWidgetAnimation* Animation, UWidget* Owner, UFunction* Material, int32 TargetIndex,
    TArray<FChannel> Channels, UMGPrototypeAnimationBuilder::FChannelWriter WriteChannel, FString& Error)
{
    for (const FChannel& Channel : Channels)
        if (!AddMember(Animation, Channel, WriteChannel, Error)) return false;
    Owner->bIsVariable = true;
    Clips.FindOrAdd(Animation).Paints.Add({Owner, Material, TargetIndex, MoveTemp(Channels)});
    return true;
}

bool FRefreshBuilder::AddPaintTrack(UWidgetAnimation* Animation, URetainerBox* Retainer, int32 TargetIndex,
    UMGPrototypeAnimationBuilder::FChannelWriter WriteChannel, FString& Error)
{
    return AddPaint(Animation, Retainer, URetainerBox::StaticClass()->FindFunctionByName(TEXT("GetEffectMaterial")), TargetIndex,
        {{MemberName(Animation, TEXT("paint"), Retainer, TEXT("progress")), TEXT("PaintProgress"), 0, 1}}, WriteChannel, Error);
}

bool FRefreshBuilder::AddSurfacePaintTrack(UWidgetAnimation* Animation, UWidget* Surface, int32 TargetIndex,
    const FVector2D& SourceSize, const FVector2D& DestinationSize,
    UMGPrototypeAnimationBuilder::FChannelWriter WriteChannel, FString& Error)
{
    UClass* OwnerClass = Surface->IsA<URetainerBox>() ? URetainerBox::StaticClass()
        : Surface->IsA<UImage>() ? UImage::StaticClass() : Surface->IsA<UBorder>() ? UBorder::StaticClass() : nullptr;
    if (!OwnerClass) { Error = TEXT("Native sized paint requires an Image, Border or Retainer producer"); return false; }
    return AddPaint(Animation, Surface, OwnerClass->FindFunctionByName(
        OwnerClass == URetainerBox::StaticClass() ? TEXT("GetEffectMaterial") : TEXT("GetDynamicMaterial")), TargetIndex,
        {{MemberName(Animation, TEXT("paint"), Surface, TEXT("progress")), TEXT("PaintProgress"), 0, 1},
         {MemberName(Animation, TEXT("paint"), Surface, TEXT("width")), TEXT("SurfaceWidth"), float(SourceSize.X), float(DestinationSize.X)},
         {MemberName(Animation, TEXT("paint"), Surface, TEXT("height")), TEXT("SurfaceHeight"), float(SourceSize.Y), float(DestinationSize.Y)}},
        WriteChannel, Error);
}

void FRefreshBuilder::Build(TFunctionRef<void(UWidgetAnimation*, const FString&)> OnFailed)
{
    if (Clips.IsEmpty()) return;
    // Refresh graphs read the new members and widget variables from the skeleton.
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    for (const auto& Pair : Clips)
    {
        // A clip removed during generation has already dropped its members.
        UWidgetAnimation* Animation = Pair.Key.Get();
        if (Animation && Blueprint->Animations.Contains(Animation) && !BuildClip(Animation, Pair.Value))
            OnFailed(Animation, TEXT("Native refresh function could not connect its native font and paint members"));
    }
    Clips.Reset();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
}

bool FRefreshBuilder::BuildClip(UWidgetAnimation* Animation, const FClipRefresh& Clip) const
{
    auto* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, *(Animation->GetName() + TEXT("_native_refresh")),
        UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    // AddFunctionGraph would regenerate the skeleton per clip; Build regenerates it once after all clips.
    FBlueprintEditorUtils::CreateFunctionGraph(Blueprint, Graph, false, static_cast<UClass*>(nullptr));
    Blueprint->FunctionGraphs.Add(Graph);
    TArray<UK2Node_FunctionEntry*> Entries;
    Graph->GetNodesOfClass(Entries);
    UK2Node_FunctionEntry* Entry = Entries[0];
    Entry->MetaData.bCallInEditor = true;
    AttachRefresh(Blueprint, Animation, Entry);
    const auto* Schema = GetDefault<UEdGraphSchema_K2>();
    bool Linked = true;
    const auto Link = [&](UEdGraphPin* From, UEdGraphPin* To)
    {
        Linked &= From && To && Schema->TryCreateConnection(From, To);
    };
    TArray<UEdGraphPin*> Exec{Entry->FindPin(UEdGraphSchema_K2::PN_Then)};
    const auto Then = [&](UEdGraphNode* Next)
    {
        for (UEdGraphPin* Pin : Exec) Link(Pin, Next->FindPin(UEdGraphSchema_K2::PN_Execute));
        Exec = {Next->FindPin(UEdGraphSchema_K2::PN_Then)};
    };
    for (const FFontRefresh& Font : Clip.Fonts)
    {
        auto* Label = Member(Graph, Font.Text->GetFName());
        auto* Info = Member(Graph, TEXT("Font"), UTextBlock::StaticClass());
        Link(Label->GetValuePin(), Info->FindPin(UEdGraphSchema_K2::PN_Self));
        UEdGraphPin* Value = Info->GetValuePin();
        if (!Font.Tracking.IsNone())
        {
            auto* Round = Call(Graph, UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Round")));
            // Font tracking is integral; MovieScene evaluates the continuous value on the source curve.
            Link(Member(Graph, Font.Tracking)->GetValuePin(), Round->FindPin(TEXT("A")));
            auto* Fields = Node<UK2Node_SetFieldsInStruct>(Graph);
            Fields->StructType = FSlateFontInfo::StaticStruct();
            Fields->AllocateDefaultPins();
            Fields->RestoreAllPins();
            if (UEdGraphPin* TrackingField = Fields->FindPin(TEXT("LetterSpacing")))
                Fields->RemoveFieldPins(TrackingField, UK2Node_SetFieldsInStruct::AllOtherPins);
            Then(Fields);
            Link(Value, Fields->FindPin(TEXT("StructRef")));
            Link(Round->GetReturnValuePin(), Fields->FindPin(TEXT("LetterSpacing")));
            Value = Fields->FindPin(TEXT("StructOut"));
        }
        auto* Set = Call(Graph, UTextBlock::StaticClass()->FindFunctionByName(TEXT("SetFont")));
        Then(Set);
        Link(Label->GetValuePin(), Set->FindPin(UEdGraphSchema_K2::PN_Self));
        Link(Value, Set->FindPin(TEXT("InFontInfo")));
    }
    for (const FPaintRefresh& Paint : Clip.Paints)
    {
        auto* Material = Call(Graph, Paint.Material);
        Link(Member(Graph, Paint.Owner->GetFName())->GetValuePin(), Material->FindPin(UEdGraphSchema_K2::PN_Self));
        if (Material->FindPin(UEdGraphSchema_K2::PN_Execute)) Then(Material);
        // A released Slate widget has no dynamic material, e.g. when destruction stops the clip.
        auto* Valid = Call(Graph, UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid)));
        Link(Material->GetReturnValuePin(), Valid->FindPin(TEXT("Object")));
        auto* Branch = Node<UK2Node_IfThenElse>(Graph);
        Branch->AllocateDefaultPins();
        Link(Valid->GetReturnValuePin(), Branch->GetConditionPin());
        Then(Branch);
        UEdGraphPin* Skip = Branch->GetElsePin();
        const auto SetParameter = [&](FName Parameter)
        {
            auto* Set = Call(Graph, UMaterialInstanceDynamic::StaticClass()->FindFunctionByName(TEXT("SetScalarParameterValue")));
            Then(Set);
            Link(Material->GetReturnValuePin(), Set->FindPin(UEdGraphSchema_K2::PN_Self));
            Schema->TrySetDefaultValue(*Set->FindPin(TEXT("ParameterName")), Parameter.ToString());
            return Set->FindPin(TEXT("Value"));
        };
        Schema->TrySetDefaultValue(*SetParameter(TEXT("PaintTarget")), FString::FromInt(Paint.TargetIndex));
        for (const FChannel& Channel : Paint.Channels)
            Link(Member(Graph, Channel.Member)->GetValuePin(), SetParameter(Channel.Parameter));
        Exec.Add(Skip);
    }
    return Linked;
}

TArray<FName> RefreshFunctions(UWidgetBlueprint* Blueprint, UWidgetAnimation* Animation)
{
    TArray<FName> Result;
    const UMovieScene* Scene=Animation->MovieScene;
    for (UMovieSceneTrack* Track : Scene->GetTracks())
        for (UMovieSceneSection* Section : Track->GetAllSections())
            if (auto* Repeater=Cast<UMovieSceneEventRepeaterSection>(Section))
                if (auto* Endpoint=Cast<UK2Node_FunctionEntry>(Repeater->Event.WeakEndpoint.Get()))
                {
                    auto* Graph=Endpoint->GetGraph();
                    if (Graph->GetOuter()==Blueprint && Design2UMGWidgetMetadata::PackageMetadata(Graph).GetValue(Graph,RefreshMarker)==TEXT("1"))
                        Result.AddUnique(Graph->GetFName());
                }
    return Result;
}

void RemoveGeneratedEndpoints(UWidgetBlueprint* Blueprint, UWidgetAnimation* Animation)
{
    const TArray<FName> Names=RefreshFunctions(Blueprint,Animation);
    const TArray<UEdGraph*> Graphs = Blueprint->FunctionGraphs;
    for (UEdGraph* Graph : Graphs)
        if (Names.Contains(Graph->GetFName())) FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
    // Root-bound tracks evaluate exactly the members this clip generated.
    TArray<FName> Members;
    for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
        if (Binding.bIsRootWidget)
            for (UMovieSceneTrack* Track : Animation->MovieScene->FindBinding(Binding.AnimationGuid)->GetTracks())
                Members.Add(CastChecked<UMovieScenePropertyTrack>(Track)->GetPropertyName());
    FBlueprintEditorUtils::BulkRemoveMemberVariables(Blueprint, Members);
}
}
