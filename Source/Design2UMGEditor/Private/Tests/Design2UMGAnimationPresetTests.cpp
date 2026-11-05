// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Design2UMGAnimationPresets.h"
#include "Design2UMGPrototypeGraphWiring.h"
#include "Design2UMGWidgetMetadata.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"
#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/UMGSequenceTickManager.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MovieScene.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/LatentActionManager.h"
#include "TimerManager.h"
#include "Misc/ScopeExit.h"
#include "Misc/EngineVersionComparison.h"
#include "WidgetBlueprint.h"

namespace
{
	UWidgetBlueprint* MakePresetBlueprint(bool bEditable = false)
	{
		auto* Blueprint = bEditable ? Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(), GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(),
				UWidgetBlueprint::StaticClass(), TEXT("PresetLifecycle")), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()))
			: NewObject<UWidgetBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
		if (!Blueprint->WidgetTree) Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, TEXT("WidgetTree"), RF_Transient);
		auto* Root = Blueprint->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ScreenRoot"));
		Blueprint->WidgetTree->RootWidget = Root;
		auto* Button = Blueprint->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("MotionButton"));
		Root->AddChildToCanvas(Button)->SetSize(FVector2D(120, 60));
		Button->SetRenderTransform(FWidgetTransform(FVector2D(17, 23), FVector2D(0.8, 1.2), FVector2D(2, 3), 9));
		Button->SetRenderOpacity(0.6f);
		return Blueprint;
	}

	UMovieScene2DTransformSection* FindTransform(UWidgetAnimation* Animation)
	{
		const UMovieScene* Scene = Animation->MovieScene;
		for (const auto& Binding : Scene->GetBindings())
			for (UMovieSceneTrack* Track : Binding.GetTracks())
				if (auto* Transform = Cast<UMovieScene2DTransformTrack>(Track))
					return Cast<UMovieScene2DTransformSection>(Transform->GetAllSections()[0]);
		return nullptr;
	}

	bool CheckFeedbackPlayback(FAutomationTestBase& Test, UWidgetBlueprint* Blueprint, const Design2UMGAnimationPresets::FSettings& Settings)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		const TStrongObjectPtr<UUserWidget> Instance(CreateWidget<UUserWidget>(World, Blueprint->GeneratedClass.Get()));
		if (!Test.TestNotNull(TEXT("Applied preset instantiates as a native user widget"), Instance.Get())) return false;
		const auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Native button preset playback")))
			.ClientSize(FVector2D(300, 200)).FocusWhenFirstShown(false)[Instance->TakeWidget()];
		FSlateApplication::Get().AddWindow(Window, true);
		ON_SCOPE_EXIT { FSlateApplication::Get().RequestDestroyWindow(Window); };
		Window->SlatePrepass();
		FSlateApplication::Get().Tick();
		auto* Button = Cast<UButton>(Instance->WidgetTree->FindWidget(Settings.TargetWidgetName));
		if (!Test.TestNotNull(TEXT("Playback uses the generated native Button"), Button)) return false;
		const auto Control = Button->GetCachedWidget();
		if (!Test.TestTrue(TEXT("Native Button has a live Slate control"), Control.IsValid())) return false;
		const FGeometry Geometry = Control->GetCachedGeometry();
		const FVector2D Center = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5);
		const FVector2D Outside = Geometry.LocalToAbsolute(FVector2D(-20, -20));
		const TSet<FKey> Pressed{EKeys::LeftMouseButton};
		const FPointerEvent Move(0, FSlateApplication::CursorPointerIndex, Center, Center, {}, FKey(), 0, FModifierKeysState());
		const FPointerEvent Down(0, FSlateApplication::CursorPointerIndex, Center, Center, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
		const FPointerEvent Up(0, FSlateApplication::CursorPointerIndex, Center, Center, {}, EKeys::LeftMouseButton, 0, FModifierKeysState());
		const FPointerEvent Leave(0, FSlateApplication::CursorPointerIndex, Outside, Center, {}, FKey(), 0, FModifierKeysState());
		const FPointerEvent OutsideUp(0, FSlateApplication::CursorPointerIndex, Outside, Outside, {}, EKeys::LeftMouseButton, 0, FModifierKeysState());
		const FWidgetTransform Idle = Button->GetRenderTransform();
		const auto IsIdle = [&]
		{
			const auto Current = Button->GetRenderTransform();
			return Current.Translation.Equals(Idle.Translation, 0.001) && Current.Scale.Equals(Idle.Scale, 0.001)
				&& Current.Shear.Equals(Idle.Shear, 0.001) && FMath::IsNearlyEqual(Current.Angle, Idle.Angle);
		};
		const auto Tick = [&](float Seconds)
		{
			++GFrameCounter;
			UUMGSequenceTickManager::Get(Instance.Get())->OnWidgetTicked(Instance.Get());
			FSlateApplication::Get().OnPreTick().Broadcast(Seconds);
			World->GetLatentActionManager().BeginFrame();
			World->GetLatentActionManager().ProcessLatentActions(Instance.Get(), Seconds);
			World->GetTimerManager().Tick(Seconds);
			FSlateApplication::Get().OnPostTick().Broadcast(Seconds);
		};
		Control->OnMouseEnter(Geometry, Move);
		Tick(0); Tick(Settings.DurationSeconds * 0.35f);
		Test.TestTrue(TEXT("Actual hover moves toward its preset pose"), Button->GetRenderTransform().Scale.X > Idle.Scale.X);
		FVector2D Before = Button->GetRenderTransform().Scale;
		Control->OnMouseButtonDown(Geometry, Down);
		Tick(0);
		Test.TestTrue(TEXT("Press during hover does not jump to a different pose"), Button->GetRenderTransform().Scale.Equals(Before, 0.001));
		Tick(Settings.DurationSeconds * 0.3f);
		Before = Button->GetRenderTransform().Scale;
		Control->OnMouseButtonUp(Geometry, Up);
		Tick(0);
		Test.TestTrue(TEXT("Release during press keeps the current pose"), Button->GetRenderTransform().Scale.Equals(Before, 0.001));
		Tick(Settings.DurationSeconds * 0.2f);
		Before = Button->GetRenderTransform().Scale;
		Control->OnMouseLeave(Leave);
		Tick(0);
		Test.TestTrue(TEXT("Leave during release reverses continuously"), Button->GetRenderTransform().Scale.Equals(Before, 0.001));
		for (int32 Frame = 0; Frame < 5; ++Frame) Tick(Settings.DurationSeconds);
		Test.TestTrue(TEXT("Unhover returns to the authored transform"), IsIdle());
		Control->OnMouseEnter(Geometry, Move);
		Control->OnMouseButtonDown(Geometry, Down);
		Control->OnMouseLeave(Leave);
		for (int32 Frame = 0; Frame < 5; ++Frame) Tick(Settings.DurationSeconds);
		Test.TestTrue(TEXT("Held press takes priority after leaving"), Button->IsPressed()
			&& Button->GetRenderTransform().Scale.Equals(Idle.Scale * Settings.PressScale, 0.001));
		Control->OnMouseButtonUp(Geometry, OutsideUp);
		for (int32 Frame = 0; Frame < 5; ++Frame) Tick(Settings.DurationSeconds);
		Test.TestTrue(TEXT("Release after leaving restores the authored transform"), IsIdle());
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeAnimationPresetLifecycleTest,
	"Design2UMG.WidgetBlueprint.AnimationPresets.ApplyUndoAndReimport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeAnimationPresetLifecycleTest::RunTest(const FString&)
{
	using namespace Design2UMGAnimationPresets;
	UWidgetBlueprint* Blueprint = MakePresetBlueprint(true);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	FSettings Settings(EPreset::ButtonScale);
	Settings.TargetWidgetName = TEXT("MotionButton");
	RecordSourceTargets(Blueprint, {{Settings.TargetWidgetName, TEXT("instance::button")}});
	const int32 OriginalNodeCount = Blueprint->UbergraphPages[0]->Nodes.Num();
	TArray<FString> Warnings;
	const bool bApplied = Apply(Blueprint, Settings, Warnings);
	if (!TestTrue(*FString::Printf(TEXT("Preset applies through the real compiler: %s"), *FString::Join(Warnings, TEXT("; "))), bApplied)) return false;
	if (!CheckFeedbackPlayback(*this, Blueprint, Settings)) return false;
	TestEqual(TEXT("One owned animation after apply"), Blueprint->Animations.Num(), 1);
	TestTrue(TEXT("Apply can be undone"), GEditor->UndoTransaction(false));
	TestEqual(TEXT("Undo restores original animation list"), Blueprint->Animations.Num(), 0);
	TestEqual(TEXT("Undo restores original graph"), Blueprint->UbergraphPages[0]->Nodes.Num(), OriginalNodeCount);
	TestTrue(TEXT("Undo restores the editor settings too"), Design2UMGWidgetMetadata::PackageMetadata(Blueprint)
		.GetValue(Blueprint, TEXT("Design2UMG.AnimationPresets")).IsEmpty());
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Warnings.Reset();
	const bool bReapplied = Apply(Blueprint, Settings, Warnings);
	if (!TestTrue(*FString::Printf(TEXT("Preset can be reapplied after undo: %s"), *FString::Join(Warnings, TEXT("; "))), bReapplied)) return false;
	UWidgetAnimation* Animation = Blueprint->Animations[0];
	auto* Section = FindTransform(Animation);
	Section->Scale[0].AddLinearKey(100, 1.73f);
	const int32 EditedKeyCount = Section->Scale[0].GetNumKeys();
	Design2UMGPrototypeGraphWiring::ClearGeneratedBindings(Blueprint);
	UWidget* Button = Blueprint->WidgetTree->FindWidget(Settings.TargetWidgetName);
	Button->Rename(TEXT("RenamedButton"), Blueprint->WidgetTree);
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
	Blueprint->OnVariableRenamed(Settings.TargetWidgetName, TEXT("RenamedButton"));
#endif
	RecordSourceTargets(Blueprint, {{FName(TEXT("RenamedButton")), TEXT("instance::button")}});
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Warnings.Reset();
	Restore(Blueprint, Warnings);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	TestTrue(TEXT("Reimport restore has no diagnostics"), Warnings.IsEmpty());
	TestTrue(TEXT("Reimport preserves the edited native clip"), Blueprint->Animations.Num() == 1 && Blueprint->Animations[0] == Animation);
	TestEqual(TEXT("Reimport preserves hand-edited keys"), FindTransform(Animation)->Scale[0].GetNumKeys(), EditedKeyCount);
	TestEqual(TEXT("Stable source identity rebinds a renamed widget"), Animation->AnimationBindings[0].WidgetName, FName(TEXT("RenamedButton")));

	UEdGraph* Graph = Blueprint->UbergraphPages[0];
	auto* Event = NewObject<UK2Node_CustomEvent>(Graph);
	Event->CustomFunctionName = TEXT("UserBrokenCall");
	Graph->AddNode(Event); Event->CreateNewGuid(); Event->AllocateDefaultPins();
	auto* Call = NewObject<UK2Node_CallFunction>(Graph);
	Call->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)));
	Graph->AddNode(Call); Call->CreateNewGuid(); Call->AllocateDefaultPins();
	GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Event->FindPinChecked(UEdGraphSchema_K2::PN_Then),
		Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
	Call->FunctionReference.SetExternalMember(TEXT("PresetMissingFunction"), UKismetSystemLibrary::StaticClass());
	AddExpectedError(TEXT("PresetMissingFunction"), EAutomationExpectedErrorFlags::Contains, 0);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	const int32 BeforeFailureNodes = Graph->Nodes.Num();
	const FString BeforeFailureSettings = Design2UMGWidgetMetadata::PackageMetadata(Blueprint).GetValue(Blueprint, TEXT("Design2UMG.AnimationPresets"));
	Settings.TargetWidgetName = TEXT("RenamedButton");
	Settings.Scale = 1.12f;
	Warnings.Reset();
	TestFalse(TEXT("A real compiler failure rejects the preset"), Apply(Blueprint, Settings, Warnings));
	TestTrue(TEXT("Failure restores the original clip object"), Blueprint->Animations.Num() == 1 && Blueprint->Animations[0] == Animation);
	TestEqual(TEXT("Failure restores the edited keys"), FindTransform(Animation)->Scale[0].GetNumKeys(), EditedKeyCount);
	TestEqual(TEXT("Failure restores the original graph"), Graph->Nodes.Num(), BeforeFailureNodes);
	TestTrue(TEXT("Failure preserves the actual user nodes"), Graph->Nodes.Contains(Event) && Graph->Nodes.Contains(Call));
	TestEqual(TEXT("Failure preserves the original metadata"), Design2UMGWidgetMetadata::PackageMetadata(Blueprint)
		.GetValue(Blueprint, TEXT("Design2UMG.AnimationPresets")), BeforeFailureSettings);
	Graph->RemoveNode(Call); Graph->RemoveNode(Event);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	TestTrue(TEXT("Removing the original broken user call leaves the restored Blueprint compilable"),
		Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeAnimationPresetTracksTest,
	"Design2UMG.WidgetBlueprint.AnimationPresets.NativeTracks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeAnimationPresetTracksTest::RunTest(const FString&)
{
	using namespace Design2UMGAnimationPresets;
	UWidgetBlueprint* Blueprint = MakePresetBlueprint();
	auto* Button = Blueprint->WidgetTree->FindWidget(TEXT("MotionButton"));
	const FWidgetTransform Original = Button->GetRenderTransform();
	for (uint8 Index = 0; Index <= static_cast<uint8>(EPreset::ButtonPop); ++Index)
	{
		FSettings Settings(static_cast<EPreset>(Index));
		Settings.TargetWidgetName = IsScreenPreset(Settings.Preset) ? TEXT("ScreenRoot") : TEXT("MotionButton");
		Settings.DurationSeconds = 0.2f;
		FBindingPlan Plan;
		FString Error;
		if (!TestTrue(*GetPresetLabel(Settings.Preset).ToString(), BuildAnimations(Blueprint, Settings, Plan, Error))) return false;
		UWidgetAnimation* Animation = Blueprint->Animations.Last();
		TestTrue(TEXT("Preset uses the standard engine animation class"), Animation->GetClass() == UWidgetAnimation::StaticClass());
		TestEqual(TEXT("One actual widget binding"), Animation->AnimationBindings.Num(), 1);
		TestEqual(TEXT("Binding names the selected widget"), Animation->AnimationBindings[0].WidgetName, Settings.TargetWidgetName);
		if (!IsScreenPreset(Settings.Preset))
		{
			TestTrue(TEXT("Button has one shared feedback timeline"), !Plan.FeedbackAnimationName.IsNone());
			TestTrue(TEXT("Feedback pose times are ordered"), Plan.IdleTime == 0 && Plan.HoverTime > 0 && Plan.PressTime > Plan.HoverTime);
			if (auto* Section = FindTransform(Animation))
			{
				float Value = 0;
				Section->Translation[0].Evaluate(FFrameTime(0), Value);
				TestTrue(TEXT("Authored idle translation is preserved"), FMath::IsNearlyEqual(Value, static_cast<float>(Original.Translation.X)));
				Section->Scale[0].Evaluate(FFrameTime(0), Value);
				TestTrue(TEXT("Authored idle scale is preserved"), FMath::IsNearlyEqual(Value, static_cast<float>(Original.Scale.X)));
			}
		}
		TestTrue(TEXT("Generating a preset does not mutate its template transform"), Button->GetRenderTransform() == Original);
	}
	TestEqual(TEXT("Repeated choices replace only their screen or button animation"), Blueprint->Animations.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeAnimationSpringTracksTest,
	"Design2UMG.WidgetBlueprint.AnimationPresets.SourceSpringTracks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeAnimationSpringTracksTest::RunTest(const FString&)
{
	UWidgetBlueprint* Blueprint = MakePresetBlueprint();
	FUMGWidgetNode Root;
	Root.Id = TEXT("root"); Root.Name = TEXT("ScreenRoot"); Root.Type = TEXT("CanvasPanel");
	FUMGWidgetNode& Child = Root.Children.AddDefaulted_GetRef();
	Child.Id = TEXT("surface"); Child.Name = TEXT("MotionButton"); Child.Type = TEXT("Button");
	Child.MutableSource().SourceNodeId = TEXT("surface");
	FUMGPrototypeNavigationData Navigation;
	auto& Binding = Navigation.ClickBindings.AddDefaulted_GetRef();
	Binding.TriggerWidgetId = TEXT("trigger"); Binding.TriggerWidgetName = TEXT("MotionButton"); Binding.TriggerType = TEXT("ON_CLICK");
	auto& Step = Binding.Steps.AddDefaulted_GetRef();
	Step.Kind = EUMGPrototypeNavigationStepKind::AnimateWidget; Step.TargetPageId = TEXT("state");
	auto& Transition = Binding.Transition;
	Transition.DurationSeconds = 0.5f;
	Transition.bSpring = true; Transition.SpringMass = 1; Transition.SpringStiffness = 100; Transition.SpringDamping = 10;
	auto& Target = Transition.WidgetTargets.AddDefaulted_GetRef();
	Target.NodeId = TEXT("surface"); Target.Translation = FVector2D(20, 0);
	UMGPrototypeAnimationBuilder::GenerateNavigationAnimations(Blueprint, Root, Navigation);
	if (!TestEqual(TEXT("One native source spring animation"), Blueprint->Animations.Num(), 1)) return false;
	auto* Animation = Blueprint->Animations[0].Get();
	auto* Section = FindTransform(Animation);
	if (!TestNotNull(TEXT("Spring uses an ordinary transform section"), Section)) return false;
	TestTrue(TEXT("The source response is represented by multiple native keys"), Section->Translation[0].GetNumKeys() > 2);
	float Value = 0;
	Section->Translation[0].Evaluate(FFrameTime(0), Value);
	TestTrue(TEXT("Spring starts at the authored translation"), FMath::IsNearlyEqual(Value, 17.0f));
	const FFrameNumber End(static_cast<int32>(FMath::RoundToInt64(Animation->MovieScene->GetTickResolution().AsDecimal() * 0.5)));
	Section->Translation[0].Evaluate(FFrameTime(End), Value);
	TestTrue(TEXT("A still-overshooting source response is not clamped or normalized at its end"), Value > 37.0f);
	return true;
}

#endif
