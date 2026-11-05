// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DesignImportPlanning.h"
#include "Design2UMGLocalFigmaImport.h"
#include "Design2UMGPrototypeGraphWiring.h"
#include "Design2UMGGeneratedNameSuffixes.h"
#include "FigmaParser.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGTextPaintOwner.h"
#include "UMGWidgetTreeBuilder.h"
#include "UMGGeneratorRootHandoff.h"
#include "UMGJsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

#include "Animation/UMGSequenceTickManager.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Animation/MovieSceneMarginTrack.h"
#include "MovieSceneBinding.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/RetainerBox.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/CanvasPanel.h"
#include "Components/ContentWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "CoreGlobals.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "Engine/LatentActionManager.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GenericPlatform/ICursor.h"
#include "Input/Events.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Layout/WidgetPath.h"
#include "Types/ReflectionMetadata.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/Crc.h"
#include "MovieScene.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/SCanvas.h"

namespace
{
	struct FRealWidgetMatch
	{
		const FUMGWidgetNode* Node = nullptr;
		UWidget* Widget = nullptr;
	};

	void FindRealWidgets(const FUMGWidgetNode& Node, UWidget* Widget, const FString& Id,
		TArray<FRealWidgetMatch>& Matches)
	{
		if (!Widget) return;
		if (auto* Producer = Cast<UUserWidget>(Widget))
		{
			const auto* Role = FindFProperty<FObjectPropertyBase>(Producer->GetClass(), TEXT("D2UNativeProducer"));
			if (Role)
			{
				auto* Source = Cast<UWidget>(Role->GetObjectPropertyValue_InContainer(Producer));
				if (Source) FindRealWidgets(Node, Source, Id, Matches);
				return;
			}
		}
		if (!FUMGWidgetTreeBuilder::MatchesWidgetBaseName(Widget->GetName(),
			FUMGWidgetTreeBuilder::SanitizeWidgetName(Node.Name, Node.Type).ToString())) return;
		if (Node.Id == Id) Matches.Add({&Node, Widget});
		auto* Panel = Cast<UPanelWidget>(Widget);
		if (!Panel || Panel->GetChildrenCount() != Node.Children.Num()) return;
		for (int32 Index = 0; Index < Node.Children.Num(); ++Index)
			FindRealWidgets(Node.Children[Index], Panel->GetChildAt(Index), Id, Matches);
	}

	FRealWidgetMatch RealVisualOwner(const FRealWidgetMatch& Match)
	{
		if (auto* Producer = Cast<UUserWidget>(Match.Widget))
			if (const auto* Role = FindFProperty<FObjectPropertyBase>(Producer->GetClass(), TEXT("D2UNativeProducer")))
				return RealVisualOwner({Match.Node, Cast<UWidget>(Role->GetObjectPropertyValue_InContainer(Producer))});
		if (!Match.Widget || !Match.Node) return {};
		if (Match.Node->Type == TEXT("Overlay") && !Match.Node->Children.IsEmpty()
			&& Match.Node->Children[0].Id == Match.Node->Id + TEXT("__layout_content"))
			return RealVisualOwner({&Match.Node->Children[0], CastChecked<UPanelWidget>(Match.Widget)->GetChildAt(0)});
		if (Match.Node->SourceVisualChildIndex != INDEX_NONE)
		{
			const int32 Index = Match.Node->SourceVisualChildIndex;
			return RealVisualOwner({&Match.Node->Children[Index], CastChecked<UPanelWidget>(Match.Widget)->GetChildAt(Index)});
		}
		auto* Button = Cast<UButton>(Match.Widget);
		return Button && Match.Node->Children.Num() == 1 && Button->GetChildrenCount() == 1
			&& Match.Node->Children[0].Id == Match.Node->Id + Design2UMGGeneratedNameSuffixes::Content()
			? RealVisualOwner({&Match.Node->Children[0], Button->GetChildAt(0)}) : Match;
	}
	FRealWidgetMatch RealTextPaintOwner(const FRealWidgetMatch& Match)
	{
		const auto* Owner = UMGTextPaintOwner::Find(*Match.Node);
		TArray<FRealWidgetMatch> Matches;
		if (Owner) FindRealWidgets(*Match.Node, Match.Widget, Owner->Id, Matches);
		return Matches.Num() == 1 ? Matches[0] : FRealWidgetMatch{};
	}

	UTextBlock* RealTextOwner(const FRealWidgetMatch& Match)
	{
		FRealWidgetMatch Glyph = Match;
		while (Glyph.Widget && Glyph.Node)
		{
			Glyph = RealVisualOwner(Glyph);
			if (!Glyph.Widget || !Glyph.Node) return nullptr;
			if (Glyph.Node->Type == TEXT("TextBlock")) return Cast<UTextBlock>(Glyph.Widget);
			const auto Paint = RealTextPaintOwner(Glyph);
			if (Paint.Widget) Glyph = Paint;
			const bool bContentContainer = (Glyph.Node->Type == TEXT("SizeBox") && Glyph.Widget->IsA<USizeBox>())
				|| (Glyph.Node->Type == TEXT("ScaleBox") && Glyph.Widget->IsA<UScaleBox>())
				|| (Glyph.Node->Type == TEXT("Border") && Glyph.Widget->IsA<UBorder>())
				|| (Glyph.Node->Type == TEXT("Button") && Glyph.Widget->IsA<UButton>())
				|| (Glyph.Node->Type == TEXT("RetainerBox") && Glyph.Widget->IsA<URetainerBox>());
			auto* Panel = Cast<UPanelWidget>(Glyph.Widget);
			if (!bContentContainer || !Panel || Glyph.Node->Children.Num() != 1 || Panel->GetChildrenCount() != 1) return nullptr;
			Glyph = {&Glyph.Node->Children[0], Panel->GetChildAt(0)};
		}
		return nullptr;
	}

	FRealWidgetMatch RealSurfacePaintOwner(const FRealWidgetMatch& Match)
	{
		const auto Visual = RealVisualOwner(Match);
		if (!Visual.Node || !Visual.Widget) return {};
		if (Visual.Node->Type == TEXT("Overlay") && Visual.Node->Children.Num() == 2
			&& Visual.Node->Children[0].Effect.SurfacePaints.IsSet())
			return RealVisualOwner({&Visual.Node->Children[0], CastChecked<UPanelWidget>(Visual.Widget)->GetChildAt(0)});
		TArray<FRealWidgetMatch> Matches;
		FindRealWidgets(*Visual.Node, Visual.Widget, Visual.Node->Id + TEXT("__surface_paint"), Matches);
		return Matches.Num() == 1 && Matches[0].Node->Effect.SurfacePaints.IsSet() ? Matches[0] : FRealWidgetMatch{};
	}

	UWidget* RealColorOwner(const FRealWidgetMatch& Match)
	{
		return RealVisualOwner(Match).Widget;
	}

	template <typename TTrack>
	bool HasBoundTrack(const UWidgetAnimation* Animation, const UWidget* Widget)
	{
		if (!Animation || !Animation->MovieScene || !Widget) return false;
		const UMovieScene* Scene = Animation->MovieScene;
		int32 Count = 0;
		for (const auto& WidgetBinding : Animation->AnimationBindings)
		{
			if (WidgetBinding.WidgetName != Widget->GetFName()) continue;
			for (const FMovieSceneBinding& Binding : Scene->GetBindings())
				if (Binding.GetObjectGuid() == WidgetBinding.AnimationGuid)
					for (const UMovieSceneTrack* Track : Binding.GetTracks()) Count += Cast<TTrack>(Track) ? 1 : 0;
		}
		return Count == 1;
	}

	template <typename TTrack>
	UWidget* ResolveNativeTrackOwner(UWidgetTree* Tree, UWidgetAnimation* Animation, UWidget* Source)
	{
		UWidget* Owner = HasBoundTrack<TTrack>(Animation, Source) ? Source : nullptr;
		bool bAmbiguous = false;
		Tree->ForEachWidget([&](UWidget* Widget)
		{
			auto* Producer = Cast<UUserWidget>(Widget);
			const auto* Role = Producer ? FindFProperty<FObjectPropertyBase>(Producer->GetClass(), TEXT("D2UNativeProducer")) : nullptr;
			if (!Role || Role->GetObjectPropertyValue_InContainer(Producer) != Source || !HasBoundTrack<TTrack>(Animation, Producer)) return;
			if (Owner) bAmbiguous = true; else Owner = Producer;
		});
		return bAmbiguous ? nullptr : Owner;
	}

	UWidget* ResolveNativeLayoutOwner(UWidgetTree* Tree, UWidget* Source)
	{
		if (Cast<UCanvasPanelSlot>(Source->Slot)) return Source;
		UWidget* Found = nullptr;
		bool bAmbiguous = false;
		Tree->ForEachWidget([&](UWidget* Widget)
		{
			auto* Producer = Cast<UUserWidget>(Widget);
			const auto* Role = Producer ? FindFProperty<FObjectPropertyBase>(Producer->GetClass(), TEXT("D2UNativeProducer")) : nullptr;
			if (!Role || Role->GetObjectPropertyValue_InContainer(Producer) != Source || !Cast<UCanvasPanelSlot>(Producer->Slot)) return;
			if (Found) bAmbiguous = true; else Found = Producer;
		});
		return bAmbiguous ? nullptr : Found;
	}

	float NativeOpacity(UWidget* Widget)
	{
		if (auto* Producer = Cast<UUserWidget>(Widget))
			if (const auto* Role = FindFProperty<FObjectPropertyBase>(Producer->GetClass(), TEXT("D2UNativeProducer")))
				if (Role->GetObjectPropertyValue_InContainer(Producer))
					if (const auto* Opacity = FindFProperty<FFloatProperty>(Producer->GetClass(), TEXT("SourceOpacity")))
						return Opacity->GetPropertyValue_InContainer(Producer);
		return Widget->GetRenderOpacity();
	}

	FWidgetPath FindHitPoint(const TSharedRef<SWindow>& Window, const TSharedPtr<SWidget>& Control,
		int32 UserIndex, FVector2D& OutPoint)
	{
		if (!Control) return FWidgetPath();
		const auto& Geometry = Control->GetCachedGeometry();
		const FVector2D Size = Geometry.GetLocalSize();
		const TArray<TSharedRef<SWindow>> Windows{Window};
		for (const double Y : {0.5, 0.1, 0.9, 0.25, 0.75})
			for (const double X : {0.5, 0.1, 0.9, 0.25, 0.75})
			{
				const FVector2D Point = Geometry.LocalToAbsolute(FVector2D(Size.X * X, Size.Y * Y));
				const FWidgetPath Path = FSlateApplication::Get().LocateWindowUnderMouse(Point, Windows, false, UserIndex);
				if (Path.IsValid() && Path.GetWindow() == Window && Path.ContainsWidget(Control.Get()))
				{
					OutPoint = Point;
					return Path;
				}
			}
		return FWidgetPath();
	}

	bool ReadSolidColor(const UWidget* Widget, FLinearColor& Color)
	{
		if (const auto* Border = Cast<UBorder>(Widget))
		{
			Color = Border->GetBrushColor() * Border->Background.TintColor.GetSpecifiedColor();
			return true;
		}
		if (const auto* Image = Cast<UImage>(Widget))
		{
			Color = Image->GetBrush().TintColor.GetSpecifiedColor() * Image->GetColorAndOpacity();
			return true;
		}
		if (const auto* Text = Cast<UTextBlock>(Widget))
		{
			Color = Text->GetColorAndOpacity().GetSpecifiedColor();
			return true;
		}
		return false;
	}

	struct FObservedAnimationTarget
	{
		FUMGPrototypeWidgetAnimationTarget Source;
		UWidget* Widget = nullptr;
		UWidget* ColorWidget = nullptr;
		FVector2D InitialTranslation;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeWidgetAnimationRealSourcePlaybackTest,
	"Design2UMG.Manual.NativeWidgetAnimationRealSourcePlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeWidgetAnimationRealSourcePlaybackTest::RunTest(const FString&)
{
	FString SourcePath;
	FString AssetPath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationSource="), SourcePath)
		|| !FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationAsset="), AssetPath))
	{
		AddError(TEXT("Real-source playback requires explicit -D2UNativeAnimationSource=<absolute design.json> and -D2UNativeAnimationAsset=<saved WBP object path>"));
		return false;
	}
	if (!TestFalse(TEXT("Real source input path is absolute"), FPaths::IsRelative(SourcePath))
		|| !TestTrue(TEXT("Saved asset object path is explicit"), AssetPath.StartsWith(TEXT("/Game/")) && AssetPath.Contains(TEXT("."))))
		return false;
	if (!TestNull(TEXT("Real asset playback requires a fresh editor process"), FindObject<UWidgetBlueprint>(nullptr, *AssetPath))) return false;

	FString Json;
	FDesignDocument Document;
	if (!TestTrue(TEXT("Read the explicit real source JSON"), FFileHelper::LoadFileToString(Json, *SourcePath))
		|| !TestTrue(TEXT("Real source parses through the production Figma parser"), FFigmaParser::ParseDocument(Json, Document))) return false;
	FImportAnalysisInput Input;
	Input.bResolveImages = false;
	const FImportAnalysisResult Analysis = DesignImportPlanning::AnalyzeImport(Document, Input);
	if (!TestTrue(*FString::Printf(TEXT("Real source analysis succeeds: %s"), *Analysis.ErrorMessage), Analysis.ErrorMessage.IsEmpty())
		|| !TestTrue(TEXT("Production conversion has no error"), Analysis.Data.ConversionError.IsEmpty())) return false;
	for (const auto& Warning : Analysis.Data.Warnings) AddInfo(Warning);
	const auto& Navigation = Analysis.Data.PrototypeNavigation();
	if (!TestEqual(TEXT("This playback check requires one real source interaction"), Navigation.ClickBindings.Num(), 1)) return false;
	const auto& Binding = Navigation.ClickBindings[0];
	const auto& Transition = Binding.Transition;
	if (!TestEqual(TEXT("Real source explicitly requests click"), Binding.TriggerType, FString(TEXT("ON_CLICK")))
		|| !TestEqual(TEXT("Real click has one executable action"), Binding.Steps.Num(), 1)) return false;
	if (!TestTrue(TEXT("Real click is a widget property action"), Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::AnimateWidget)
		|| !TestEqual(TEXT("Real action explicitly uses SMART_ANIMATE"), Transition.Type, FString(TEXT("SMART_ANIMATE")))
		|| !TestTrue(*FString::Printf(TEXT("Real source animation is supported: %s"), *Transition.Diagnostic), Transition.Diagnostic.IsEmpty())
		|| !TestTrue(TEXT("Real source duration is positive"), Transition.IsAnimated())
		|| !TestTrue(TEXT("Production analysis resolved real property targets"), Transition.WidgetTargets.Num() > 0)) return false;

	auto* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!TestNotNull(TEXT("Existing real WBP reloads from disk without regeneration"), Blueprint)) return false;
	if (!TestNotNull(TEXT("Saved real WBP has a compiled generated class"), Blueprint->GeneratedClass.Get())
		|| !TestNotNull(TEXT("Saved real WBP retains its widget tree"), Blueprint->WidgetTree.Get())
		|| !TestEqual(TEXT("Single real interaction has one persisted animation"), Blueprint->Animations.Num(), 1)) return false;
	auto* PersistedAnimation = Blueprint->Animations[0].Get();
	if (!TestNotNull(TEXT("Persisted animation exists"), PersistedAnimation)
		|| !TestEqual(TEXT("Persisted animation is engine-native"), PersistedAnimation->GetClass(), UWidgetAnimation::StaticClass())
		|| !TestNotNull(TEXT("Persisted native timeline exists"), PersistedAnimation->MovieScene.Get())) return false;
	const float TimeTolerance = static_cast<float>(PersistedAnimation->MovieScene->GetTickResolution().AsInterval()) + KINDA_SMALL_NUMBER;
	if (!TestTrue(TEXT("Persisted duration equals the real source duration"), FMath::IsNearlyEqual(
		PersistedAnimation->GetEndTime() - PersistedAnimation->GetStartTime(), Transition.DurationSeconds, TimeTolerance))) return false;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Real playback uses the normal editor world"), World)
		|| !TestTrue(TEXT("Native Slate application is available"), FSlateApplication::IsInitialized())) return false;
	UUserWidget* Instance = CreateWidget<UUserWidget>(World, Blueprint->GeneratedClass.Get());
	if (!TestNotNull(TEXT("Saved real generated class instantiates"), Instance)) return false;
	Instance->AddToRoot();
	const auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Real source native animation playback")))
		.ClientSize(FVector2D(600, 180)).FocusWhenFirstShown(false).SupportsMaximize(false).SupportsMinimize(false)
		[SNew(SBox)[Instance->TakeWidget()]];
	FSlateApplication::Get().AddWindow(Window, true);
	ON_SCOPE_EXIT
	{
		FSlateApplication::Get().RequestDestroyWindow(Window);
		Instance->RemoveFromRoot();
	};
	Window->SlatePrepass();
	FSlateApplication::Get().Tick();
	FSlateApplication::Get().Tick();
	const auto* AnimationProperty = FindFProperty<FObjectProperty>(Blueprint->GeneratedClass, PersistedAnimation->GetFName());
	auto* Animation = AnimationProperty ? Cast<UWidgetAnimation>(AnimationProperty->GetObjectPropertyValue_InContainer(Instance)) : nullptr;
	if (!TestNotNull(TEXT("Actual instance owns the reloaded native animation"), Animation)
		|| !TestNotNull(TEXT("Actual instance timeline is available"), Animation->MovieScene.Get())) return false;
	if (!TestTrue(TEXT("Runtime native animation retains the real duration"), FMath::IsNearlyEqual(
		Animation->GetEndTime() - Animation->GetStartTime(), Transition.DurationSeconds, TimeTolerance))) return false;

	TArray<FObservedAnimationTarget> Targets;
	bool bHasMovement = false;
	bool bHasColor = false;
	for (const auto& SourceTarget : Transition.WidgetTargets)
	{
		// This single real-source check covers movement and solid paint, not five-property parity.
		if (!TestTrue(TEXT("Real check contains only movement and solid-color deltas"),
			SourceTarget.Scale.Equals(FVector2D(1, 1)) && FMath::IsNearlyZero(SourceTarget.Rotation) && !SourceTarget.bOpacity)) return false;
		TArray<FRealWidgetMatch> Matches;
		FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, SourceTarget.NodeId, Matches);
		if (!TestEqual(TEXT("Real source target resolves uniquely in the saved native tree"), Matches.Num(), 1)) return false;
		UWidget* Widget = Matches[0].Widget;
		UWidget* ColorWidget = SourceTarget.bColor ? RealColorOwner(Matches[0]) : nullptr;
		if (!TestTrue(TEXT("Animated target is an engine UMG widget"), Widget->GetClass()->GetOutermost()->GetName().StartsWith(TEXT("/Script/UMG")))) return false;
		if (!SourceTarget.Translation.IsNearlyZero())
		{
			Widget = ResolveNativeTrackOwner<UMovieScene2DTransformTrack>(Instance->WidgetTree, Animation, Widget);
			if (!TestNotNull(TEXT("Real movement binds its unique actual native owner"), Widget)) return false;
		}
		if (SourceTarget.bColor
			&& !TestTrue(TEXT("Real color binds its actual native paint owner"), HasBoundTrack<UMovieSceneColorTrack>(Animation, ColorWidget))) return false;
		Targets.Add({SourceTarget, Widget, ColorWidget, Widget->GetRenderTransform().Translation});
		bHasMovement |= !SourceTarget.Translation.IsNearlyZero();
		bHasColor |= SourceTarget.bColor;
		if (SourceTarget.bColor)
		{
			FLinearColor Color;
			if (!TestTrue(TEXT("Actual target exposes a native solid paint"), ReadSolidColor(ColorWidget, Color))
				|| !TestTrue(TEXT("Actual initial solid color equals the real source endpoint"), Color.Equals(SourceTarget.StartColor, 0.01f))) return false;
		}
	}
	if (!TestTrue(TEXT("Real source has a nonzero movement endpoint"), bHasMovement)
		|| !TestTrue(TEXT("Real source has a solid-color endpoint change"), bHasColor)) return false;

	auto* Button = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UButton>(Instance->WidgetTree,
		FUMGWidgetTreeBuilder::SanitizeWidgetName(Binding.TriggerWidgetName, TEXT("Button")).ToString());
	if (!TestNotNull(TEXT("Real source click resolves to its actual saved native button"), Button)) return false;
	if (!TestNotNull(TEXT("Saved blueprint contains that button's native click event"),
		FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnClicked"), Button->GetFName()))) return false;
	const auto Control = Button->GetCachedWidget();
	if (!TestTrue(TEXT("Actual native button has live Slate geometry"), Control.IsValid())) return false;
	TSharedPtr<FSlateVirtualUserHandle> UserHandle;
	int32 VirtualIndex = 0;
	do { UserHandle = FSlateApplication::Get().FindOrCreateVirtualUser(VirtualIndex++); } while (!UserHandle.IsUnique());
	const int32 UserIndex = UserHandle->GetUserIndex();
	const auto User = FSlateApplication::Get().GetUser(UserIndex);
	FVector2D Center;
	const FWidgetPath Path = FindHitPoint(Window, Control, UserIndex, Center);
	if (!TestTrue(TEXT("Native hit test reaches the real button in this test window"), Path.IsValid()
		&& Path.GetWindow() == Window && Path.ContainsWidget(Control.Get()))) return false;
	const FPointerEvent Move(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, FKey(), 0, FModifierKeysState());
	FSlateApplication::Get().RoutePointerMoveEvent(Path, Move, true);
	const TSet<FKey> Pressed{EKeys::LeftMouseButton};
	const FPointerEvent Down(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
	const FPointerEvent Up(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, EKeys::LeftMouseButton, 0, FModifierKeysState());
	if (!TestFalse(TEXT("Real click animation has not already started"), Instance->IsAnimationPlaying(Animation))) return false;
	const bool bDown = FSlateApplication::Get().RoutePointerDownEvent(Path, Down).IsEventHandled();
	const auto& CapturePath = User->GetCaptorPath(FSlateApplication::CursorPointerIndex);
	TestTrue(TEXT("Native pointer capture stays in the test window"), CapturePath.IsValid() && CapturePath.GetWindow() == Window);
	const bool bUp = FSlateApplication::Get().RoutePointerUpEvent(Path, Up).IsEventHandled();
	if (!TestTrue(TEXT("Actual native pointer click was handled and released"), bDown && bUp && !User->HasCapture(FSlateApplication::CursorPointerIndex))
		|| !TestTrue(TEXT("Real pointer click starts the persisted native animation"), Instance->IsAnimationPlaying(Animation))) return false;

	const auto Tick = [&](float Seconds)
	{
		++GFrameCounter;
		UUMGSequenceTickManager::Get(Instance)->OnWidgetTicked(Instance);
		FSlateApplication::Get().OnPreTick().Broadcast(Seconds);
		World->GetLatentActionManager().BeginFrame();
		World->GetLatentActionManager().ProcessLatentActions(Instance, Seconds);
		World->GetTimerManager().Tick(Seconds);
		FSlateApplication::Get().OnPostTick().Broadcast(Seconds);
	};
	Tick(Transition.DurationSeconds * 0.25f);
	Tick(Transition.DurationSeconds * 0.25f);
	if (!TestTrue(TEXT("Native animation is still playing at an intermediate frame"), Instance->IsAnimationPlaying(Animation))) return false;
	TestTrue(TEXT("Native playback time is between real source endpoints"), Instance->GetAnimationCurrentTime(Animation) > 0
		&& Instance->GetAnimationCurrentTime(Animation) < Transition.DurationSeconds);
	for (const auto& Target : Targets)
	{
		if (!Target.Source.Translation.IsNearlyZero())
		{
			const FVector2D Value = Target.Widget->GetRenderTransform().Translation;
			TestTrue(TEXT("Real native movement has a frame distinct from both endpoints"),
				!Value.Equals(Target.InitialTranslation, 0.01) && !Value.Equals(Target.InitialTranslation + Target.Source.Translation, 0.01));
		}
		if (Target.Source.bColor)
		{
			FLinearColor Value;
			if (!TestTrue(TEXT("Read actual intermediate native paint"), ReadSolidColor(Target.ColorWidget, Value))) return false;
			TestTrue(TEXT("Real native solid color has a frame distinct from both endpoints"),
				!Value.Equals(Target.Source.StartColor, 0.01f) && !Value.Equals(Target.Source.EndColor, 0.01f));
		}
	}
	if (!Transition.bSpring)
	{
		Tick(Transition.DurationSeconds * 0.49f);
		for (const auto& Target : Targets)
		{
			if (!Target.Source.Translation.IsNearlyZero())
				TestTrue(TEXT("Native movement approaches the real endpoint before state commit"),
					Target.Widget->GetRenderTransform().Translation.Equals(Target.InitialTranslation + Target.Source.Translation,
						FMath::Max(0.02, Target.Source.Translation.Size() * 0.01)));
			if (Target.Source.bColor)
			{
				FLinearColor Value;
				if (!TestTrue(TEXT("Read endpoint native paint"), ReadSolidColor(Target.ColorWidget, Value))) return false;
				TestTrue(TEXT("Native paint approaches the real endpoint before state commit"), Value.Equals(Target.Source.EndColor, 0.02f));
			}
		}
	}
	for (int32 Frame = 0; Frame < 4; ++Frame) Tick(Transition.DurationSeconds * 0.25f);
	TestFalse(TEXT("Real native animation completes"), Instance->IsAnimationPlaying(Animation));
	if (!Transition.StateHostWidgetId.IsEmpty())
	{
		TArray<FRealWidgetMatch> SourceStates, TargetStates;
		FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, Transition.SourceStateWidgetId, SourceStates);
		FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, Transition.TargetStateWidgetId, TargetStates);
		if (!TestEqual(TEXT("Source state resolves uniquely"), SourceStates.Num(), 1)
			|| !TestEqual(TEXT("Target state resolves uniquely"), TargetStates.Num(), 1)) return false;
		TestEqual(TEXT("Source state is hidden after commit"), SourceStates[0].Widget->GetVisibility(), ESlateVisibility::Collapsed);
		TestTrue(TEXT("Target state is displayed after commit"), TargetStates[0].Widget->GetVisibility() != ESlateVisibility::Collapsed
			&& TargetStates[0].Widget->GetVisibility() != ESlateVisibility::Hidden);
		for (const auto& Target : Targets)
		{
			if (!Target.Source.Translation.IsNearlyZero())
				TestTrue(TEXT("Source movement is restored for later entry"), Target.Widget->GetRenderTransform().Translation.Equals(Target.InitialTranslation, 0.01));
			if (Target.Source.bColor)
			{
				FLinearColor Value;
				if (!TestTrue(TEXT("Read restored source paint"), ReadSolidColor(Target.ColorWidget, Value))) return false;
				TestTrue(TEXT("Source paint is restored for later entry"), Value.Equals(Target.Source.StartColor, 0.01f));
			}
		}
	}
	else
	{
		for (const auto& Target : Targets)
		{
			if (!Target.Source.Translation.IsNearlyZero())
				TestTrue(TEXT("Actual native movement reaches the real source endpoint"), Target.Widget->GetRenderTransform().Translation.Equals(Target.InitialTranslation + Target.Source.Translation, 0.01));
			if (Target.Source.bColor)
			{
				FLinearColor Value;
				if (!TestTrue(TEXT("Read actual final native paint"), ReadSolidColor(Target.ColorWidget, Value))) return false;
				TestTrue(TEXT("Actual native solid color reaches the real source endpoint"), Value.Equals(Target.Source.EndColor, 0.01f));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("Real-source playback input=%s asset=%s duration=%.9g targets=%d"),
		*SourcePath, *AssetPath, Transition.DurationSeconds, Targets.Num()));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeWidgetAnimationRealSourceAnalysisTest,
	"Design2UMG.Manual.NativeWidgetAnimationRealSourceAnalysis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeWidgetAnimationRealSourceAnalysisTest::RunTest(const FString&)
{
	FString SourcePath, TriggerId, TriggerType, TargetNodeId, DestinationPageId;
	if (!FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationSource="), SourcePath)
		|| !TestFalse(TEXT("Analysis source path is absolute"), FPaths::IsRelative(SourcePath)))
	{
		AddError(TEXT("Real-source analysis requires -D2UNativeAnimationSource=<absolute design.json>"));
		return false;
	}
	FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationTrigger="), TriggerId);
	FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationTriggerType="), TriggerType);
	FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationNode="), TargetNodeId);
	FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationDestination="), DestinationPageId);
	FDesignDocument Document;
	FString LoadError;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(SourcePath, Document, LoadError, TargetNodeId);
	if (!TestTrue(*FString::Printf(TEXT("Load real source for analysis: %s"), *LoadError), bLoaded)) return false;
	FImportAnalysisInput Input;
	Input.bResolveImages = false;
	const FImportAnalysisResult Analysis = DesignImportPlanning::AnalyzeImport(Document, Input);
	AddInfo(FString::Printf(TEXT("Real source analysis: source=%s error=%s conversionError=%s"),
		*SourcePath, *Analysis.ErrorMessage, *Analysis.Data.ConversionError));
	for (const auto& Warning : Analysis.Data.Warnings) AddInfo(Warning);
	const auto& Navigation = Analysis.Data.PrototypeNavigation();
	AddInfo(FString::Printf(TEXT("Analysis navigation: bindings=%d pages=%d requiresHost=%d valid=%d"),
		Navigation.ClickBindings.Num(), Navigation.PageIds.Num(), Navigation.RequiresPageHost(), Navigation.IsValid()));
	int32 Matches = 0;
	for (int32 Index = 0; Index < Navigation.ClickBindings.Num(); ++Index)
	{
		const auto& Binding = Navigation.ClickBindings[Index];
		const bool bMatches = !TriggerId.IsEmpty() && Binding.TriggerWidgetId == TriggerId
			&& (TriggerType.IsEmpty() || Binding.TriggerType.Equals(TriggerType, ESearchCase::IgnoreCase))
			&& (DestinationPageId.IsEmpty() || (Binding.Steps.Num() == 1 && Binding.Steps[0].TargetPageId == DestinationPageId));
		if (bMatches) ++Matches;
		AddInfo(FString::Printf(TEXT("Analysis binding[%d]: trigger=%s type=%s sourcePage=%s destination=%s steps=%d targets=%d diagnostic=%s requestedMatch=%d"),
			Index, *Binding.TriggerWidgetId, *Binding.TriggerType, *Binding.SourcePageId,
			Binding.Steps.Num() == 1 ? *Binding.Steps[0].TargetPageId : TEXT(""), Binding.Steps.Num(),
			Binding.Transition.WidgetTargets.Num(), *Binding.Transition.Diagnostic, bMatches));
	}
	if (!TriggerId.IsEmpty())
		AddInfo(FString::Printf(TEXT("Requested analysis interaction: trigger=%s type=%s destination=%s matches=%d; rejected actions are reported in conversion warnings"),
			*TriggerId, *TriggerType, *DestinationPageId, Matches));
	TestTrue(TEXT("Real source analysis succeeds"), Analysis.ErrorMessage.IsEmpty());
	TestTrue(TEXT("Real source conversion succeeds"), Analysis.Data.ConversionError.IsEmpty());
	FString DiagnosticPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("D2UDiagnosticPaintStages="), DiagnosticPath))
	{
		FString GameOutput, AssetName;
		if (!TestFalse(TEXT("Paint-stage output path is absolute"), FPaths::IsRelative(DiagnosticPath))
			|| !FParse::Value(FCommandLine::Get(), TEXT("D2UDiagnosticGameOutput="), GameOutput)
			|| !FParse::Value(FCommandLine::Get(), TEXT("D2UDiagnosticAssetName="), AssetName)
			|| !TestTrue(TEXT("Paint-stage preparation has explicit Game output and asset name"),
				GameOutput.StartsWith(TEXT("/Game/")) && !AssetName.IsEmpty())
			|| !Analysis.ErrorMessage.IsEmpty() || !Analysis.Data.ConversionError.IsEmpty()) return false;
		auto Evidence = MakeShared<FJsonObject>();
		Evidence->SetStringField(TEXT("source"), SourcePath);
		Evidence->SetStringField(TEXT("game_output"), GameOutput);
		Evidence->SetStringField(TEXT("asset_name"), AssetName);
		Evidence->SetStringField(TEXT("document_stage"), TEXT("After production LoadLocalFigmaDocument, including existing image blend bakes and URL resolution"));
		TArray<TSharedPtr<FJsonValue>> Fills;
		TFunction<void(const TSharedPtr<FDesignNode>&, const FString&)> ReadFills;
		ReadFills = [&](const TSharedPtr<FDesignNode>& Node, const FString& Scope)
		{
			if (!Node.IsValid()) return;
			for (int32 Index = 0; Index < Node->Visual.Fills.Num(); ++Index)
			{
				const auto& Fill = Node->Visual.Fills[Index];
				// Include image identities after baking, even when the bake made their blend normal.
				if ((Fill.BlendMode.IsEmpty() || Fill.BlendMode.Equals(TEXT("NORMAL"), ESearchCase::IgnoreCase))
					&& Fill.Type != EDesignFillType::Image) continue;
				auto Value = MakeShared<FJsonObject>();
				Value->SetStringField(TEXT("scope"), Scope);
				Value->SetStringField(TEXT("node_id"), Node->Id);
				Value->SetStringField(TEXT("node_name"), Node->Name);
				Value->SetNumberField(TEXT("fill_index"), Index);
				Value->SetNumberField(TEXT("fill_type"), static_cast<int32>(Fill.Type));
				Value->SetBoolField(TEXT("enabled"), Fill.bEnabled);
				Value->SetStringField(TEXT("blend_mode"), Fill.BlendMode);
				Value->SetStringField(TEXT("image_url"), Fill.Image.Url);
				Value->SetNumberField(TEXT("opacity"), Fill.Opacity);
				Value->SetNumberField(TEXT("image_opacity"), Fill.Image.Opacity);
				Fills.Add(MakeShared<FJsonValueObject>(Value));
			}
			for (const auto& Child : Node->Children) ReadFills(Child, Scope);
		};
		for (const auto& Node : Document.Children) ReadFills(Node, TEXT("document"));
		for (const auto& Node : Document.SourceDocument().PrototypePages) ReadFills(Node, TEXT("prototype_pages"));
		Evidence->SetArrayField(TEXT("typed_non_normal_and_image_fills"), Fills);
		Evidence->SetStringField(TEXT("converted_data_json"), FUMGJsonSerializer::Serialize(Analysis.Data));
		const auto Prepared = UMGGeneratorRootHandoff::PrepareRootForBuild(Analysis.Data, GameOutput, AssetName);
		Evidence->SetStringField(TEXT("preparation_error"), Prepared.ErrorMessage);
		Evidence->SetNumberField(TEXT("prepared_width"), Prepared.DesignSize.X);
		Evidence->SetNumberField(TEXT("prepared_height"), Prepared.DesignSize.Y);
		Evidence->SetObjectField(TEXT("prepared_root"), FUMGJsonSerializer::SerializeWidgetNode(Prepared.Root));
		FString Json;
		FJsonSerializer::Serialize(Evidence, TJsonWriterFactory<>::Create(&Json));
		TestTrue(TEXT("Write paint-stage analysis"), FFileHelper::SaveStringToFile(Json, *DiagnosticPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
		TestTrue(*FString::Printf(TEXT("Paint-stage production resource preparation succeeds: %s"), *Prepared.ErrorMessage), Prepared.ErrorMessage.IsEmpty());
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeWidgetAnimationRealInteractionPlaybackTest,
	"Design2UMG.Manual.NativeWidgetAnimationRealInteractionPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativeWidgetAnimationRealInteractionPlaybackTest::RunTest(const FString&)
{
	FString SourcePath, AssetPath, TriggerId, TriggerType, PreTriggerId, ReturnTriggerId, TargetNodeId, DestinationPageId;
	FString NestedTimeoutTriggerId, NestedReturnTimeoutTriggerId;
	if (!FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationSource="), SourcePath)
		|| !FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationAsset="), AssetPath)
		|| !FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationTrigger="), TriggerId)
		|| !FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationTriggerType="), TriggerType))
	{
		AddError(TEXT("Real playback requires source, saved asset, trigger ID and trigger type"));
		return false;
	}
	FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationPreTrigger="), PreTriggerId);
	FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationReturnTrigger="), ReturnTriggerId);
	FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationNode="), TargetNodeId);
	FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationDestination="), DestinationPageId);
	FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationNestedTimeoutTrigger="), NestedTimeoutTriggerId);
	FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationNestedReturnTimeoutTrigger="), NestedReturnTimeoutTriggerId);
	if (!TestTrue(TEXT("Nested timer observation requires both explicit triggers"),
		NestedTimeoutTriggerId.IsEmpty() == NestedReturnTimeoutTriggerId.IsEmpty())) return false;
	if (!TestFalse(TEXT("Real source path is absolute"), FPaths::IsRelative(SourcePath))
		|| !TestTrue(TEXT("Saved asset path is explicit"), AssetPath.StartsWith(TEXT("/Game/")) && AssetPath.Contains(TEXT(".")))
		|| !TestNull(TEXT("Playback requires a fresh editor process"), FindObject<UWidgetBlueprint>(nullptr, *AssetPath))) return false;
	FDesignDocument Document;
	FString LoadError;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(SourcePath, Document, LoadError, TargetNodeId);
	if (!TestTrue(*FString::Printf(TEXT("Load real source with its resources: %s"), *LoadError), bLoaded)) return false;
	FImportAnalysisInput Input;
	Input.bResolveImages = false;
	const FImportAnalysisResult Analysis = DesignImportPlanning::AnalyzeImport(Document, Input);
	if (!TestTrue(TEXT("Real source analysis succeeds"), Analysis.ErrorMessage.IsEmpty())
		|| !TestTrue(TEXT("Real source conversion succeeds"), Analysis.Data.ConversionError.IsEmpty())) return false;
	for (const auto& Warning : Analysis.Data.Warnings) AddInfo(Warning);
	const auto& Navigation = Analysis.Data.PrototypeNavigation();
	int32 BindingIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Navigation.ClickBindings.Num(); ++Index)
	{
		const auto& Candidate = Navigation.ClickBindings[Index];
		if (Candidate.TriggerWidgetId != TriggerId || !Candidate.TriggerType.Equals(TriggerType, ESearchCase::IgnoreCase)) continue;
		if (!DestinationPageId.IsEmpty() && (Candidate.Steps.Num() != 1 || Candidate.Steps[0].TargetPageId != DestinationPageId)) continue;
		if (!TestTrue(TEXT("Requested interaction is unique"), BindingIndex == INDEX_NONE)) return false;
		BindingIndex = Index;
	}
	if (!TestTrue(TEXT("Requested interaction exists in production analysis"), BindingIndex != INDEX_NONE)) return false;
	const auto& Binding = Navigation.ClickBindings[BindingIndex];
	const auto& Transition = Binding.Transition;
	const bool bOfficialDissolve = Transition.Type == TEXT("DISSOLVE") && Transition.SourceTransitionType == TEXT("SMART_ANIMATE")
		&& !Transition.DissolveReason.IsEmpty();
	const bool bWhileHoverState = Binding.UsesHoverTrigger() && Binding.Steps.Num() == 1
		&& Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::AnimateWidget && !Transition.StateHostWidgetId.IsEmpty();
	if (!TestTrue(TEXT("Requested trigger is click, drag, hover, mouse entry or page timeout"), Binding.UsesClickTrigger()
		|| Binding.UsesDragTrigger() || Binding.UsesHoverTrigger() || Binding.UsesMouseEnterTrigger() || Binding.UsesPageOpenTrigger())
		|| !TestTrue(TEXT("Requested interaction has one native animation action"), Binding.Steps.Num() == 1
			&& (Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::AnimateWidget
				|| Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate))
		|| !TestTrue(*FString::Printf(TEXT("Real animation admitted: %s"), *Transition.Diagnostic), Transition.Diagnostic.IsEmpty())
		|| !TestTrue(TEXT("Real source retains Smart Animate semantics or its explicit official dissolve representation"),
			Transition.Type == TEXT("SMART_ANIMATE") || bOfficialDissolve)
		|| !TestTrue(TEXT("Real action has an animated representation"), Transition.IsAnimated()
			&& (bOfficialDissolve || !Transition.WidgetTargets.IsEmpty() || !Transition.LayerOrders.IsEmpty() || Transition.bFixedViewportLayout))) return false;
	if (!NestedTimeoutTriggerId.IsEmpty() && !TestTrue(TEXT("Nested timer observation requires a real while-hover state"), bWhileHoverState)) return false;
	const FUMGPrototypeNavigationBinding* PreBinding = nullptr;
	if (Navigation.RequiresPageHost())
	{
		if (!TestTrue(TEXT("Initial page is available"), Navigation.PageIds.IsValidIndex(Navigation.InitialPageIndex))) return false;
		if (Navigation.PageIds[Navigation.InitialPageIndex] != Binding.SourcePageId)
		{
			for (const auto& Candidate : Navigation.ClickBindings)
			{
				if (Candidate.TriggerWidgetId != PreTriggerId || !Candidate.UsesClickTrigger() || Candidate.Steps.Num() != 1
					|| Candidate.SourcePageId != Navigation.PageIds[Navigation.InitialPageIndex]
					|| Candidate.Steps[0].Kind != EUMGPrototypeNavigationStepKind::Navigate
					|| Candidate.Steps[0].TargetPageId != Binding.SourcePageId || !Candidate.Transition.Type.IsEmpty()) continue;
				if (!TestNull(TEXT("Preliminary real navigation is unique"), PreBinding)) return false;
				PreBinding = &Candidate;
			}
			if (!TestNotNull(TEXT("Explicit preliminary click reaches the interaction page"), PreBinding)) return false;
		}
	}
	UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!TestNotNull(TEXT("Reload real saved blueprint"), Blueprint)
		|| !TestNotNull(TEXT("Saved blueprint is compiled"), Blueprint->GeneratedClass.Get())) return false;
	const FName AnimationName(*FString::Printf(TEXT("__d2u_nav_anim_%03d_%s"), BindingIndex,
		bOfficialDissolve && Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate ? TEXT("pages") : TEXT("properties")));
	UWidgetAnimation* Persisted = nullptr;
	for (UWidgetAnimation* Candidate : Blueprint->Animations)
		if (Candidate && Candidate->GetFName() == AnimationName) Persisted = Candidate;
	if (!TestNotNull(TEXT("Requested native animation is saved"), Persisted)
		|| !TestNotNull(TEXT("Requested native timeline is saved"), Persisted->MovieScene.Get())) return false;
	const float Tolerance = static_cast<float>(Persisted->MovieScene->GetTickResolution().AsInterval()) + KINDA_SMALL_NUMBER;
	if (!TestTrue(TEXT("Native duration matches source"), FMath::IsNearlyEqual(
		Persisted->GetEndTime() - Persisted->GetStartTime(), Transition.DurationSeconds, Tolerance))) return false;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Playback uses an editor world"), World)
		|| !TestTrue(TEXT("Slate application is available"), FSlateApplication::IsInitialized())) return false;
	UUserWidget* Instance = CreateWidget<UUserWidget>(World, Blueprint->GeneratedClass.Get());
	if (!TestNotNull(TEXT("Saved generated class instantiates"), Instance)) return false;
	Instance->AddToRoot();
	const FVector2D WindowSize = Transition.bFixedViewportLayout ? FVector2D(Analysis.Data.RootWidget.Bounds.W, Analysis.Data.RootWidget.Bounds.H)
		: FVector2D(FMath::Max(600.0f, Analysis.Data.RootWidget.Bounds.W), FMath::Max(180.0f, Analysis.Data.RootWidget.Bounds.H));
	const TSharedRef<SWidget> NativeSlate = Instance->TakeWidget();
	TSharedRef<SWidget> Content = SNew(SBox)[NativeSlate];
	if (Transition.bFixedViewportLayout)
		Content = SNew(SCanvas) + SCanvas::Slot().Position(FVector2D::ZeroVector).Size(WindowSize)[NativeSlate];
	const auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Real interaction playback")))
		.ClientSize(WindowSize)
		.FocusWhenFirstShown(false).SupportsMaximize(false).SupportsMinimize(false)
		[Content];
	FSlateApplication::Get().AddWindow(Window, true);
	ON_SCOPE_EXIT { FSlateApplication::Get().RequestDestroyWindow(Window); Instance->RemoveFromRoot(); };
	Window->SlatePrepass();
	FSlateApplication::Get().Tick();
	FSlateApplication::Get().Tick();
	if (PreBinding)
	{
		auto* Button = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UButton>(Instance->WidgetTree,
			FUMGWidgetTreeBuilder::SanitizeWidgetName(PreBinding->TriggerWidgetName, TEXT("Button")).ToString());
		if (!TestNotNull(TEXT("Preliminary real click has a native button"), Button)) return false;
		const auto Control = Button->GetCachedWidget();
		if (!TestTrue(TEXT("Preliminary real button has Slate geometry"), Control.IsValid())) return false;
		TSharedPtr<FSlateVirtualUserHandle> UserHandle;
		int32 VirtualIndex = 0;
		do { UserHandle = FSlateApplication::Get().FindOrCreateVirtualUser(VirtualIndex++); } while (!UserHandle.IsUnique());
		const int32 UserIndex = UserHandle->GetUserIndex();
		FVector2D Center;
		const FWidgetPath Path = FindHitPoint(Window, Control, UserIndex, Center);
		if (!TestTrue(TEXT("Native hit test reaches the preliminary button"), Path.IsValid() && Path.ContainsWidget(Control.Get()))) return false;
		const FPointerEvent Move(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, FKey(), 0, FModifierKeysState());
		FSlateApplication::Get().RoutePointerMoveEvent(Path, Move, true);
		const TSet<FKey> Pressed{EKeys::LeftMouseButton};
		const FPointerEvent Down(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
		const FPointerEvent Up(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, EKeys::LeftMouseButton, 0, FModifierKeysState());
		const bool bDown = FSlateApplication::Get().RoutePointerDownEvent(Path, Down).IsEventHandled();
		const bool bUp = FSlateApplication::Get().RoutePointerUpEvent(Path, Up).IsEventHandled();
		if (!TestTrue(TEXT("Preliminary real click is handled"), bDown && bUp)) return false;
		Window->SlatePrepass();
		FSlateApplication::Get().Tick();
		FSlateApplication::Get().Tick();
		auto* Host = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UCanvasPanel>(Instance->WidgetTree,
			FUMGWidgetTreeBuilder::SanitizeWidgetName(Navigation.PageHostWidgetName, TEXT("CanvasPanel")).ToString());
		if (!TestNotNull(TEXT("Preliminary navigation has a native page host"), Host)) return false;
		const FString PageName = FUMGWidgetTreeBuilder::SanitizeWidgetName(Binding.SourcePageId, TEXT("CanvasPanel")).ToString();
		bool bPageVisible = false;
		for (UWidget* Page : Host->GetAllChildren())
			bPageVisible |= Page && Page->GetVisibility() != ESlateVisibility::Collapsed && Page->GetVisibility() != ESlateVisibility::Hidden
				&& FUMGWidgetTreeBuilder::MatchesWidgetBaseName(Page->GetName(), PageName);
		if (!TestTrue(TEXT("Preliminary navigation reaches the source page"), bPageVisible)) return false;
	}
	const auto* Property = FindFProperty<FObjectProperty>(Blueprint->GeneratedClass, AnimationName);
	auto* Animation = Property ? Cast<UWidgetAnimation>(Property->GetObjectPropertyValue_InContainer(Instance)) : nullptr;
	if (!TestNotNull(TEXT("Instance owns requested animation"), Animation)) return false;
	struct FTarget
	{
		FUMGPrototypeWidgetAnimationTarget Source;
		UWidget* Widget;
		UWidget* OpacityWidget;
		UWidget* ColorWidget;
		FWidgetTransform Initial;
		float Opacity;
		FLinearColor Color;
	};
	TArray<FTarget> Targets;
	for (int32 Index = 0; !bOfficialDissolve && !Transition.bFixedViewportLayout && Index < Transition.WidgetTargets.Num(); ++Index)
	{
		const auto& Source = Transition.WidgetTargets[Index];
		TArray<FRealWidgetMatch> Matches;
		FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, Source.NodeId, Matches);
		if (!TestEqual(TEXT("Real source target resolves uniquely in the saved tree"), Matches.Num(), 1)) return false;
		UWidget* Widget = Matches[0].Widget;
		UWidget* ColorWidget = Source.bColor ? RealColorOwner(Matches[0]) : nullptr;
		if (!TestTrue(TEXT("Animation uses an engine widget"), Widget->GetClass()->GetOutermost()->GetName().StartsWith(TEXT("/Script/UMG")))) return false;
		const bool bTransform = !Source.Translation.IsNearlyZero() || !Source.Scale.Equals(FVector2D(1, 1)) || !FMath::IsNearlyZero(Source.Rotation);
		UWidget* TransformWidget = bTransform ? ResolveNativeTrackOwner<UMovieScene2DTransformTrack>(Instance->WidgetTree, Animation, Widget) : Widget;
		UWidget* OpacityWidget = Source.bOpacity ? ResolveNativeTrackOwner<UMovieSceneFloatTrack>(Instance->WidgetTree, Animation, Widget) : Widget;
		if (!TestNotNull(TEXT("Transform uses its unique native property owner"), TransformWidget)
			|| !TestNotNull(TEXT("Opacity uses its unique native property owner"), OpacityWidget)) return false;
		if (Source.bColor && !TestTrue(TEXT("Color track binds the real paint owner"), HasBoundTrack<UMovieSceneColorTrack>(Animation, ColorWidget))) return false;
		FLinearColor Color = FLinearColor::Transparent;
		if (Source.bColor && !TestTrue(TEXT("Read initial native color"), ReadSolidColor(ColorWidget, Color))) return false;
		if (Source.bColor && !TestTrue(TEXT("Initial color matches the source"), Color.Equals(Source.StartColor, 0.01f))) return false;
		if (Source.bOpacity && !TestTrue(TEXT("Initial opacity matches the source"), FMath::IsNearlyEqual(NativeOpacity(OpacityWidget), Source.StartOpacity, 0.01f))) return false;
		Targets.Add({Source, TransformWidget, OpacityWidget, ColorWidget, TransformWidget->GetRenderTransform(), NativeOpacity(OpacityWidget), Color});
	}
	UWidget* DissolveSource = nullptr;
	UWidget* DissolveDestination = nullptr;
	UWidget* DissolveSourceOpacity = nullptr;
	UWidget* DissolveDestinationOpacity = nullptr;
	float SourceOpacityBefore = 0, DestinationOpacityBefore = 0;
	FWidgetTransform SourceTransformBefore, DestinationTransformBefore;
	const auto VisibleInTree = [](UWidget* Widget)
	{
		for (UWidget* Current = Widget; Current;)
		{
			if (Current->GetVisibility() == ESlateVisibility::Collapsed || Current->GetVisibility() == ESlateVisibility::Hidden) return false;
			if (Current->GetParent()) Current = Current->GetParent();
			else
			{
				const auto* Tree = Current->GetTypedOuter<UWidgetTree>();
				Current = Tree ? Cast<UUserWidget>(Tree->GetOuter()) : nullptr;
			}
		}
		return Widget && Widget->GetCachedWidget().IsValid()
			&& Widget->GetCachedWidget()->GetVisibility().IsVisible();
	};
	if (bOfficialDissolve)
	{
		const FString SourceId = Transition.SourceStateWidgetId.IsEmpty() ? Binding.SourcePageId : Transition.SourceStateWidgetId;
		const FString DestinationId = Transition.TargetStateWidgetId.IsEmpty() ? Binding.Steps[0].TargetPageId : Transition.TargetStateWidgetId;
		TArray<FRealWidgetMatch> SourceMatches, DestinationMatches;
		FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, SourceId, SourceMatches);
		FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, DestinationId, DestinationMatches);
		if (!TestEqual(TEXT("Official dissolve retains one native source endpoint"), SourceMatches.Num(), 1)
			|| !TestEqual(TEXT("Official dissolve retains one native destination endpoint"), DestinationMatches.Num(), 1)) return false;
		DissolveSource = SourceMatches[0].Widget; DissolveDestination = DestinationMatches[0].Widget;
		DissolveSourceOpacity = ResolveNativeTrackOwner<UMovieSceneFloatTrack>(Instance->WidgetTree, Animation, DissolveSource);
		DissolveDestinationOpacity = ResolveNativeTrackOwner<UMovieSceneFloatTrack>(Instance->WidgetTree, Animation, DissolveDestination);
		if (!TestNotNull(TEXT("Official dissolve has a native source opacity track"), DissolveSourceOpacity)
			|| !TestNotNull(TEXT("Official dissolve has a native destination opacity track"), DissolveDestinationOpacity)
			|| !TestTrue(TEXT("Official dissolve uses two distinct native endpoint objects"), DissolveSource != DissolveDestination)) return false;
		SourceOpacityBefore = NativeOpacity(DissolveSourceOpacity); DestinationOpacityBefore = NativeOpacity(DissolveDestinationOpacity);
		SourceTransformBefore = DissolveSource->GetRenderTransform(); DestinationTransformBefore = DissolveDestination->GetRenderTransform();
		if (!TestTrue(TEXT("Official dissolve begins with its source displayed"), VisibleInTree(DissolveSource))
			|| !TestFalse(TEXT("Official dissolve begins with its destination hidden"), VisibleInTree(DissolveDestination))) return false;
		AddInfo(FString::Printf(TEXT("Official dissolve source transition=%s representation=%s reason=%s"),
			*Transition.SourceTransitionType, *Transition.Type, *Transition.DissolveReason));
	}
	const auto CheckDissolveRestored = [&]()
	{
		if (!bOfficialDissolve) return;
		TestTrue(TEXT("Official dissolve restores the original source opacity"), FMath::IsNearlyEqual(NativeOpacity(DissolveSourceOpacity), SourceOpacityBefore, 0.01f));
		TestTrue(TEXT("Official dissolve restores the destination's normal opacity"), FMath::IsNearlyEqual(NativeOpacity(DissolveDestinationOpacity), DestinationOpacityBefore, 0.01f));
		TestTrue(TEXT("Official dissolve preserves both endpoint transforms"),
			DissolveSource->GetRenderTransform() == SourceTransformBefore && DissolveDestination->GetRenderTransform() == DestinationTransformBefore);
	};
	const auto LogDissolve = [&](const TCHAR* Stage)
	{
		if (!bOfficialDissolve) return;
		AddInfo(FString::Printf(TEXT("Dissolve %s: animation=%s time=%.6f playing=%d source=%s visibility=%d owner=%s opacity=%.6f target=%s visibility=%d owner=%s opacity=%.6f"),
			Stage, *Animation->GetName(), Instance->GetAnimationCurrentTime(Animation), Instance->IsAnimationPlaying(Animation),
			*DissolveSource->GetName(), static_cast<int32>(DissolveSource->GetVisibility()), *DissolveSourceOpacity->GetName(), NativeOpacity(DissolveSourceOpacity),
			*DissolveDestination->GetName(), static_cast<int32>(DissolveDestination->GetVisibility()), *DissolveDestinationOpacity->GetName(), NativeOpacity(DissolveDestinationOpacity)));
		AddInfo(FString::Printf(TEXT("Dissolve native carriers: sourceVisibility=%d sourceSlateVisible=%d sourceParent=%s targetVisibility=%d targetSlateVisible=%d targetParent=%s"),
			static_cast<int32>(DissolveSourceOpacity->GetVisibility()), DissolveSourceOpacity->GetCachedWidget().IsValid() && DissolveSourceOpacity->GetCachedWidget()->GetVisibility().IsVisible(),
			*GetNameSafe(DissolveSourceOpacity->GetParent()), static_cast<int32>(DissolveDestinationOpacity->GetVisibility()),
			DissolveDestinationOpacity->GetCachedWidget().IsValid() && DissolveDestinationOpacity->GetCachedWidget()->GetVisibility().IsVisible(), *GetNameSafe(DissolveDestinationOpacity->GetParent())));
	};
	if (bOfficialDissolve)
	{
		LogDissolve(TEXT("initial"));
		const UMovieScene* Scene = Animation->MovieScene;
		for (const auto& ObjectBinding : Animation->AnimationBindings)
			for (const auto& SceneBinding : Scene->GetBindings())
				if (ObjectBinding.AnimationGuid == SceneBinding.GetObjectGuid())
					for (const auto* Track : SceneBinding.GetTracks())
					{
						const auto* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track);
						AddInfo(FString::Printf(TEXT("Dissolve binding: widget=%s slot=%s root=%d track=%s property=%s path=%s"),
							*ObjectBinding.WidgetName.ToString(), *ObjectBinding.SlotWidgetName.ToString(), ObjectBinding.bIsRootWidget,
							*Track->GetClass()->GetName(), PropertyTrack ? *PropertyTrack->GetPropertyName().ToString() : TEXT("none"),
							PropertyTrack ? *PropertyTrack->GetPropertyPath().ToString() : TEXT("none")));
					}
	}
	struct FObservedLayout
	{
		UWidget* Presented = nullptr;
		UWidget* Source = nullptr;
		UWidget* Destination = nullptr;
		UWidget* OpacityOwner = nullptr;
		bool bStartPresent = true, bEndPresent = true;
		float InitialOpacity = 1, StartOpacity = 1, EndOpacity = 1;
		FVector2D FirstRelativePosition;
		bool bRelativeSample = false;
		UCanvasPanelSlot* Slot = nullptr;
		FMargin InitialOffsets;
		FVector2D SourcePosition, SourceSize, NearEndPosition, NearEndSize;
		bool bNearEnd = false;
		UTextBlock* Text = nullptr;
		UTextBlock* DestinationText = nullptr;
		FSlateFontInfo InitialFont;
		bool bFontSize = false, bTracking = false;
		UMaterialInstanceDynamic* Paint = nullptr;
		float InitialProgress = 0;
	};
	TArray<FObservedLayout> Layout;
	UWidget* LayoutPresentation = nullptr;
	UWidget* LayoutSource = nullptr;
	UWidget* LayoutDestination = nullptr;
	UWidget* LayoutOriginalOpacity = nullptr;
	float LayoutOriginalAlpha = 0;
	const auto PaintLayout = [&]()
	{
		if (!Transition.bFixedViewportLayout) return;
		FSlateApplication::Get().ForceRedrawWindow(Window);
		FlushRenderingCommands();
	};
	if (Transition.bFixedViewportLayout)
	{
		const auto Find = [&](const FString& Id) -> FRealWidgetMatch
		{
			TArray<FRealWidgetMatch> Matches;
			FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, Id, Matches);
			TestEqual(*FString::Printf(TEXT("Measured layout identity resolves uniquely: %s"), *Id), Matches.Num(), 1);
			return Matches.Num() == 1 ? Matches[0] : FRealWidgetMatch{};
		};
		LayoutPresentation = Find(Transition.LayoutPresentationWidgetId).Widget;
		LayoutSource = Find(Transition.LayoutSourceWidgetId).Widget;
		LayoutDestination = Find(Transition.LayoutDestinationWidgetId).Widget;
		UWidget* Original = Find(Transition.LayoutOriginalWidgetId).Widget;
		if (!TestNotNull(TEXT("Saved layout presentation exists"), LayoutPresentation)
			|| !TestNotNull(TEXT("Native source layout is retained"), LayoutSource)
			|| !TestNotNull(TEXT("Native destination layout is retained"), LayoutDestination)
			|| !TestEqual(TEXT("Presentation is initially collapsed"), LayoutPresentation->GetVisibility(), ESlateVisibility::Collapsed)) return false;
		LayoutOriginalOpacity = Original ? ResolveNativeTrackOwner<UMovieSceneFloatTrack>(Instance->WidgetTree, Animation, Original) : nullptr;
		if (!TestNotNull(TEXT("Original layout has a native temporary opacity owner"), LayoutOriginalOpacity)) return false;
		LayoutOriginalAlpha = NativeOpacity(LayoutOriginalOpacity);
		PaintLayout();
		if (!TestTrue(TEXT("Native layout playback receives the fixed design viewport"), FVector2D(Instance->GetCachedGeometry().GetLocalSize()).Equals(WindowSize, 0.01))) return false;
		for (const auto& Mapping : Transition.LayoutTargets)
		{
			const auto* Change = Transition.WidgetTargets.FindByPredicate([&](const FUMGPrototypeWidgetAnimationTarget& Target) { return Target.NodeId == Mapping.NodeId; });
			const bool bStartPresent = !Change || Change->bStartPresent, bEndPresent = !Change || Change->bEndPresent;
			if (bStartPresent && bEndPresent && Mapping.DestinationWidgetId.IsEmpty()) continue;
			const auto Source = bStartPresent ? Find(Mapping.NodeId) : FRealWidgetMatch{};
			const auto Destination = bEndPresent ? Find(Mapping.DestinationWidgetId) : FRealWidgetMatch{};
			const auto Presented = Find(Mapping.PresentationWidgetId);
			if ((bStartPresent && !Source.Widget) || (bEndPresent && !Destination.Widget) || !Presented.Widget) return false;
			if (!TestTrue(TEXT("Measured presentation uses an engine widget"), Presented.Widget->GetClass()->GetOutermost()->GetName().StartsWith(TEXT("/Script/UMG")))) return false;
			FObservedLayout Item;
			Item.Presented = Presented.Widget; Item.Source = Source.Widget; Item.Destination = Destination.Widget;
			Item.bStartPresent = bStartPresent; Item.bEndPresent = bEndPresent;
			const auto& InitialGeometry = Source.Widget ? Source.Widget->GetCachedGeometry() : Presented.Widget->GetCachedGeometry();
			Item.SourcePosition = FVector2D(InitialGeometry.GetAccumulatedLayoutTransform().GetTranslation());
			Item.SourceSize = InitialGeometry.GetLocalSize();
			if (UWidget* Owner = ResolveNativeLayoutOwner(Instance->WidgetTree, Presented.Widget))
			{
				if (bEndPresent && !TestTrue(TEXT("Presentation Canvas slot is bound by the native margin track"), HasBoundTrack<UMovieSceneMarginTrack>(Animation, Owner))) return false;
				Item.Slot = CastChecked<UCanvasPanelSlot>(Owner->Slot); Item.InitialOffsets = Item.Slot->GetOffsets();
			}
			if (!bStartPresent || !bEndPresent)
			{
				Item.Slot = Cast<UCanvasPanelSlot>(Presented.Widget->Slot);
				Item.OpacityOwner = ResolveNativeTrackOwner<UMovieSceneFloatTrack>(Instance->WidgetTree, Animation, Presented.Widget);
				if (!TestTrue(TEXT("One-sided flow retains its native Canvas parent and opacity track"), Item.Slot
					&& Presented.Widget->GetParent() && Item.OpacityOwner
					&& HasBoundTrack<UMovieSceneFloatTrack>(Animation, Item.OpacityOwner))) return false;
				Item.InitialOffsets = Item.Slot->GetOffsets();
				Item.InitialOpacity = NativeOpacity(Item.OpacityOwner); Item.StartOpacity = Change->StartOpacity; Item.EndOpacity = Change->EndOpacity;
			}
			if (Change && bStartPresent && bEndPresent)
			{
				FRealWidgetMatch PropertyOwner = Presented;
				if (const auto* LivePresentation = Transition.LivePresentations.FindByPredicate([&](const FUMGPrototypeLivePresentation& Live)
					{ return Live.PresentationSocketWidgetId == Mapping.PresentationWidgetId; }))
				{
					PropertyOwner = Find(LivePresentation->WidgetId);
					if (!TestNotNull(TEXT("Live presentation retains its real content property owner"), PropertyOwner.Widget)) return false;
				}
				Item.bFontSize = Change->bFontSize; Item.bTracking = Change->bLetterSpacing;
				if (Item.bFontSize || Item.bTracking)
				{
					Item.Text = RealTextOwner(PropertyOwner); Item.DestinationText = RealTextOwner(Destination);
					if (!TestNotNull(TEXT("Presentation has a live native font owner"), Item.Text)
						|| !TestNotNull(TEXT("Destination has a native font endpoint"), Item.DestinationText)) return false;
					Item.InitialFont = Item.Text->GetFont();
				}
				if (Change->bTextPaintChange)
					if (auto* Retainer = Cast<URetainerBox>(RealTextPaintOwner(PropertyOwner).Widget)) Item.Paint = Retainer->GetEffectMaterial();
				if (Change->bSurfacePaintChange)
					if (auto* Image = Cast<UImage>(RealSurfacePaintOwner(PropertyOwner).Widget)) Item.Paint = Image->GetDynamicMaterial();
				if (Change->bTextPaintChange || Change->bSurfacePaintChange)
				{
					if (!TestNotNull(TEXT("Presentation has its own native dynamic paint material"), Item.Paint)) return false;
					Item.InitialProgress = Item.Paint->K2_GetScalarParameterValue(TEXT("PaintProgress"));
				}
			}
			Layout.Add(Item);
		}
		if (!TestTrue(TEXT("Real layout has saved presentation correspondences"), !Layout.IsEmpty())) return false;
	}
	const auto DescribeLayoutWidget = [](UWidget* Widget)
	{
		if (!Widget) return FString(TEXT("<null>"));
		FString SlotValues;
		if (const auto* Canvas = Cast<UCanvasPanelSlot>(Widget->Slot))
		{
			const FMargin Value = Canvas->GetOffsets();
			SlotValues = FString::Printf(TEXT(" offsets=(%.9g,%.9g,%.9g,%.9g)"), Value.Left, Value.Top, Value.Right, Value.Bottom);
		}
		if (Widget->Slot)
			if (const auto* Padding = FindFProperty<FStructProperty>(Widget->Slot->GetClass(), TEXT("Padding"));
				Padding)
			{
				FString Value;
				Padding->ExportTextItem_Direct(Value, Padding->ContainerPtrToValuePtr<void>(Widget->Slot.Get()), nullptr, Widget->Slot.Get(), PPF_None);
				SlotValues += TEXT(" padding=") + Value;
			}
		return FString::Printf(TEXT("widget='%s' class='%s' parent='%s' parentClass='%s' slot='%s' slotClass='%s'%s geometrySize=%s"),
			*GetPathNameSafe(Widget), *GetNameSafe(Widget->GetClass()), *GetPathNameSafe(Widget->GetParent()),
			*GetNameSafe(Widget->GetParent() ? Widget->GetParent()->GetClass() : nullptr), *GetPathNameSafe(Widget->Slot.Get()),
			*GetNameSafe(Widget->Slot ? Widget->Slot->GetClass() : nullptr), *SlotValues, *Widget->GetCachedGeometry().GetLocalSize().ToString());
	};
	const auto CheckLayoutRestored = [&]()
	{
		if (!Transition.bFixedViewportLayout) return;
		if (LayoutPresentation->GetVisibility() != ESlateVisibility::Collapsed
			|| !FMath::IsNearlyEqual(NativeOpacity(LayoutOriginalOpacity), LayoutOriginalAlpha, 0.01f))
		{
			const auto* PageProperty = FindFProperty<FIntProperty>(Instance->GetClass(), TEXT("__d2u_current_page"));
			AddInfo(FString::Printf(TEXT("Layout envelope restore failure: time=%.9g playing=%d currentPage=%d presentation='%s' visibility=%d alpha=%.9g original='%s' visibility=%d alpha=%.9g expectedAlpha=%.9g"),
				Instance->GetAnimationCurrentTime(Animation), Instance->IsAnimationPlaying(Animation), PageProperty ? PageProperty->GetPropertyValue_InContainer(Instance) : INDEX_NONE,
				*GetPathNameSafe(LayoutPresentation), static_cast<int32>(LayoutPresentation->GetVisibility()), NativeOpacity(LayoutPresentation),
				*GetPathNameSafe(LayoutOriginalOpacity), static_cast<int32>(LayoutOriginalOpacity->GetVisibility()), NativeOpacity(LayoutOriginalOpacity), LayoutOriginalAlpha));
		}
		TestEqual(TEXT("Stopped presentation returns to its collapsed baseline"), LayoutPresentation->GetVisibility(), ESlateVisibility::Collapsed);
		TestTrue(TEXT("Original native content opacity restores"), FMath::IsNearlyEqual(NativeOpacity(LayoutOriginalOpacity), LayoutOriginalAlpha, 0.01f));
		for (const auto& Item : Layout)
		{
			if (!Item.bStartPresent || !Item.bEndPresent)
				TestTrue(TEXT("One-sided flow opacity restores for re-entry"), FMath::IsNearlyEqual(NativeOpacity(Item.OpacityOwner), Item.InitialOpacity, 0.01f));
			if (Item.Slot && Item.Slot->GetOffsets() != Item.InitialOffsets)
			{
				const FMargin Actual = Item.Slot->GetOffsets();
				AddInfo(FString::Printf(TEXT("Layout offsets restore failure: presented={%s} source={%s} destination={%s} cachedSlot='%s' cachedSlotParent='%s' currentPresentedSlot='%s' initial=(%.9g,%.9g,%.9g,%.9g) actual=(%.9g,%.9g,%.9g,%.9g)"),
					*DescribeLayoutWidget(Item.Presented), *DescribeLayoutWidget(Item.Source), *DescribeLayoutWidget(Item.Destination),
					*GetPathNameSafe(Item.Slot), *GetPathNameSafe(Item.Slot->Parent), *GetPathNameSafe(Item.Presented->Slot.Get()),
					Item.InitialOffsets.Left, Item.InitialOffsets.Top, Item.InitialOffsets.Right, Item.InitialOffsets.Bottom,
					Actual.Left, Actual.Top, Actual.Right, Actual.Bottom));
			}
			if (Item.Slot) TestTrue(TEXT("Presentation Canvas offsets restore for re-entry"), Item.Slot->GetOffsets() == Item.InitialOffsets);
			if (Item.Text) TestTrue(TEXT("Presentation font and tracking restore for re-entry"), FMath::IsNearlyEqual(Item.Text->GetFont().Size, Item.InitialFont.Size, 0.01f)
				&& Item.Text->GetFont().LetterSpacing == Item.InitialFont.LetterSpacing);
			if (Item.Paint) TestTrue(TEXT("Presentation paint progress restores for re-entry"), FMath::IsNearlyEqual(Item.Paint->K2_GetScalarParameterValue(TEXT("PaintProgress")), Item.InitialProgress, 0.01f));
		}
	};
	const auto SampleLayout = [&](bool bNearEnd)
	{
		if (!Transition.bFixedViewportLayout) return false;
		PaintLayout();
		TestTrue(TEXT("Native layout presentation actually participates in painting"), VisibleInTree(LayoutPresentation));
		TestTrue(TEXT("Original content is hidden while its native presentation animates"), FMath::IsNearlyZero(NativeOpacity(LayoutOriginalOpacity), 0.001f));
		bool bChanged = false;
		for (auto& Item : Layout)
		{
			const FGeometry& Geometry = Item.Presented->GetCachedGeometry();
			const FVector2D Position(Geometry.GetAccumulatedLayoutTransform().GetTranslation()), Size(Geometry.GetLocalSize());
			if (!Item.bStartPresent || !Item.bEndPresent)
			{
				const float Alpha = NativeOpacity(Item.OpacityOwner);
				const auto ParentGeometry = Item.Presented->GetParent()->GetCachedGeometry();
				const FVector2D Relative = Position - FVector2D(ParentGeometry.GetAccumulatedLayoutTransform().GetTranslation());
				AddInfo(FString::Printf(TEXT("Flow presence: widget=%s startPresent=%d endPresent=%d nearEnd=%d alpha=%.6f initial=%.6f endpoints=%.6f/%.6f visible=%d relative=%s size=%s"),
					*Item.Presented->GetName(), Item.bStartPresent, Item.bEndPresent, bNearEnd, Alpha, Item.InitialOpacity,
					Item.StartOpacity, Item.EndOpacity, VisibleInTree(Item.Presented), *Relative.ToString(), *Size.ToString()));
				TestTrue(TEXT("One-sided flow has visible positive native geometry while animating"), VisibleInTree(Item.Presented) && Size.X > 0 && Size.Y > 0);
				if (!bNearEnd)
				{
					TestTrue(TEXT("One-sided flow evaluates an intermediate native opacity"), Alpha > FMath::Min(Item.StartOpacity, Item.EndOpacity)
						&& Alpha < FMath::Max(Item.StartOpacity, Item.EndOpacity));
					Item.FirstRelativePosition = Relative; Item.bRelativeSample = true;
				}
				else
				{
					TestTrue(TEXT("One-sided flow approaches its actual opacity endpoint"), FMath::IsNearlyEqual(Alpha, Item.EndOpacity,
						FMath::Max(0.02f, FMath::Abs(Item.EndOpacity - Item.StartOpacity) * 0.03f)));
					TestTrue(TEXT("One-sided flow keeps its native pose relative to the moving parent"), Item.bRelativeSample && Relative.Equals(Item.FirstRelativePosition, 0.5));
				}
				bChanged |= !FMath::IsNearlyEqual(Alpha, Item.InitialOpacity, 0.001f);
			}
			if (Item.Slot) bChanged |= Item.Slot->GetOffsets() != Item.InitialOffsets;
			bChanged |= !Position.Equals(Item.SourcePosition, 0.02) || !Size.Equals(Item.SourceSize, 0.02);
			if (Item.Text)
			{
				const auto& Font = Item.Text->GetFont(); const auto& End = Item.DestinationText->GetFont();
				AddInfo(FString::Printf(TEXT("Layout font: widget=%s nearEnd=%d size=%.6f source=%.6f destination=%.6f tracking=%d"),
					*Item.Text->GetName(), bNearEnd, Font.Size, Item.InitialFont.Size, End.Size, Font.LetterSpacing));
				if (Item.bFontSize)
				{
					TestFalse(TEXT("Native presentation font size changes during layout playback"), FMath::IsNearlyEqual(Font.Size, Item.InitialFont.Size, 0.01f));
					bChanged |= !FMath::IsNearlyEqual(Font.Size, Item.InitialFont.Size, 0.01f);
					if (bNearEnd) TestTrue(TEXT("Presentation font approaches its actual native destination"), FMath::IsNearlyEqual(Font.Size, End.Size,
						FMath::Max(0.02f, FMath::Abs(End.Size - Item.InitialFont.Size) * 0.02f)));
				}
				if (Item.bTracking)
				{
					TestNotEqual(TEXT("Native presentation tracking changes during layout playback"), Font.LetterSpacing, Item.InitialFont.LetterSpacing);
					bChanged |= Font.LetterSpacing != Item.InitialFont.LetterSpacing;
					if (bNearEnd) TestTrue(TEXT("Presentation tracking approaches its native destination"), FMath::Abs(Font.LetterSpacing - End.LetterSpacing)
						<= FMath::Max(1, FMath::RoundToInt(FMath::Abs(End.LetterSpacing - Item.InitialFont.LetterSpacing) * 0.02f)));
				}
			}
			if (Item.Paint)
			{
				const float Progress = Item.Paint->K2_GetScalarParameterValue(TEXT("PaintProgress"));
				AddInfo(FString::Printf(TEXT("Layout paint: widget=%s nearEnd=%d progress=%.6f source=%.6f position=%s size=%s"),
					*Item.Presented->GetName(), bNearEnd, Progress, Item.InitialProgress, *Position.ToString(), *Size.ToString()));
				TestFalse(TEXT("Presentation paint parameters actually update"), FMath::IsNearlyEqual(Progress, Item.InitialProgress, 0.001f));
				bChanged |= !FMath::IsNearlyEqual(Progress, Item.InitialProgress, 0.001f);
				if (bNearEnd) TestTrue(TEXT("Presentation paint approaches the destination stack"), FMath::IsNearlyEqual(Progress, 1.0f, 0.03f));
			}
			if (bNearEnd) { Item.NearEndPosition = Position; Item.NearEndSize = Size; Item.bNearEnd = true; }
		}
		return bChanged;
	};
	const auto CheckLayoutDestination = [&]()
	{
		if (!Transition.bFixedViewportLayout) return;
		PaintLayout();
		if (bWhileHoverState)
		{
			TestTrue(TEXT("While-hover layout keeps its source input and painted presentation"), VisibleInTree(LayoutSource) && VisibleInTree(LayoutPresentation));
			return;
		}
		TestTrue(TEXT("Completed layout displays the retained native destination"), !VisibleInTree(LayoutSource) && VisibleInTree(LayoutDestination));
		for (const auto& Item : Layout)
		{
			if (!Item.bNearEnd) continue;
			if (!Item.bEndPresent)
			{
				TestFalse(TEXT("Committed outgoing flow is absent from the active source tree"), VisibleInTree(Item.Source));
				continue;
			}
			const FGeometry& Geometry = Item.Destination->GetCachedGeometry();
			const FVector2D Position(Geometry.GetAccumulatedLayoutTransform().GetTranslation()), Size(Geometry.GetLocalSize());
			TestTrue(*FString::Printf(TEXT("Animated Slate position approaches the actual native endpoint: %s actual=%s destination=%s"),
				*Item.Presented->GetName(), *Item.NearEndPosition.ToString(), *Position.ToString()), Item.NearEndPosition.Equals(Position, FMath::Max(0.5, (Position - Item.SourcePosition).Size() * 0.02)));
			if (!Item.NearEndSize.Equals(Size, FMath::Max(0.5, (Size - Item.SourceSize).Size() * 0.02)))
				AddInfo(FString::Printf(TEXT("Layout endpoint size failure: sampledNearEnd=%s source={%s} presented={%s} destination={%s} presentedParent={%s} destinationParent={%s}"),
					*Item.NearEndSize.ToString(), *DescribeLayoutWidget(Item.Source), *DescribeLayoutWidget(Item.Presented), *DescribeLayoutWidget(Item.Destination),
					*DescribeLayoutWidget(Item.Presented->GetParent()), *DescribeLayoutWidget(Item.Destination->GetParent())));
			TestTrue(*FString::Printf(TEXT("Animated Slate size approaches the actual native endpoint: %s actual=%s destination=%s"),
				*Item.Presented->GetName(), *Item.NearEndSize.ToString(), *Size.ToString()), Item.NearEndSize.Equals(Size, FMath::Max(0.5, (Size - Item.SourceSize).Size() * 0.02)));
		}
		CheckLayoutRestored();
	};
	TArray<const FUMGPrototypeNavigationBinding*> ReturnBindings;
	if (Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate)
		for (const auto& Candidate : Navigation.ClickBindings)
			if (Candidate.SourcePageId == Binding.Steps[0].TargetPageId && Candidate.UsesClickTrigger()
				&& (ReturnTriggerId.IsEmpty() || Candidate.TriggerWidgetId == ReturnTriggerId)
				&& Candidate.Steps.Num() == 1 && Candidate.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate
				&& Candidate.Steps[0].TargetPageId == Binding.SourcePageId) ReturnBindings.Add(&Candidate);
	if (!ReturnTriggerId.IsEmpty() && !TestEqual(TEXT("Requested real return action resolves uniquely"), ReturnBindings.Num(), 1)) return false;
	UWidget* SourcePage = nullptr;
	if (ReturnBindings.Num() == 1)
	{
		TArray<FRealWidgetMatch> Pages;
		FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, Binding.SourcePageId, Pages);
		if (!TestEqual(TEXT("Return source page resolves uniquely"), Pages.Num(), 1)) return false;
		SourcePage = Pages[0].Widget;
		SourcePage->SetToolTipText(FText::FromString(TEXT("Prototype state retained")));
	}
	struct FObservedLayerOrder
	{
		UCanvasPanelSlot* Slot = nullptr;
		int32 InitialZ = 0;
		int32 EndIndex = 0;
	};
	TArray<FObservedLayerOrder> LayerOrders;
	for (const auto& Order : Transition.LayerOrders)
	{
		// A fixed-viewport transition draws, and therefore reorders, its presentation copy.
		const auto* Presentation = Transition.bFixedViewportLayout ? Transition.LayoutTargets.FindByPredicate(
			[&](const FUMGPrototypeLayoutTarget& Item) { return Item.NodeId == Order.NodeId; }) : nullptr;
		TArray<FRealWidgetMatch> Matches;
		FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, Presentation ? Presentation->PresentationWidgetId : Order.NodeId, Matches);
		if (!TestEqual(TEXT("Reordered source child resolves uniquely"), Matches.Num(), 1)) return false;
		UWidget* LayoutOwner = ResolveNativeLayoutOwner(Instance->WidgetTree, Matches[0].Widget);
		auto* Slot = LayoutOwner ? Cast<UCanvasPanelSlot>(LayoutOwner->Slot) : nullptr;
		if (!TestNotNull(TEXT("Reordered child resolves its native Canvas slot"), Slot)) return false;
		LayerOrders.Add({Slot, Slot->GetZOrder(), Order.EndIndex});
	}
	TMap<FString, UWidget*> NestedWidgets;
	struct FNestedCanvasObservation
	{
		UWidget* Widget;
		UCanvasPanelSlot* Slot;
	};
	TArray<FNestedCanvasObservation> NestedTargetCanvas;
	struct FLiveObservation
	{
		UWidget* Widget;
		UContentWidget* Original;
		UContentWidget* Presentation;
		UPanelSlot* OriginalSlot;
		UPanelWidget* OriginalParent;
	};
	TArray<FLiveObservation> LiveObservations;
	if (!NestedTimeoutTriggerId.IsEmpty())
	{
		const auto CacheWidget = [&](const FString& Id) -> UWidget*
		{
			if (UWidget* const* Existing = NestedWidgets.Find(Id)) return *Existing;
			TArray<FRealWidgetMatch> Matches;
			FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, Id, Matches);
			if (!TestEqual(TEXT("Live lifecycle starts from a unique actual widget"), Matches.Num(), 1)) return nullptr;
			NestedWidgets.Add(Id, Matches[0].Widget); return Matches[0].Widget;
		};
		for (const auto& Host : Navigation.StateHosts)
			for (const auto& State : Host.States)
			{
				if (!CacheWidget(State.WidgetId)) return false;
				if (!Navigation.ClickBindings.ContainsByPredicate([&](const FUMGPrototypeNavigationBinding& Candidate)
					{ return Candidate.TriggerWidgetId == NestedTimeoutTriggerId && Candidate.Transition.TargetStateWidgetId == State.WidgetId; })) continue;
				TArray<FRealWidgetMatch> Matches;
				FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, State.WidgetId, Matches);
				if (!TestEqual(TEXT("Nested state geometry has one source model"), Matches.Num(), 1)) return false;
				TSet<UWidget*> Seen;
				TFunction<void(const FUMGWidgetNode&)> CollectCanvas = [&](const FUMGWidgetNode& Node)
				{
					if (!Node.Id.IsEmpty() && Node.Bounds.W > 0 && Node.Bounds.H > 0)
					{
						UWidget* Source = CacheWidget(Node.Id);
						UWidget* Widget = Source ? ResolveNativeLayoutOwner(Instance->WidgetTree, Source) : nullptr;
						auto* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
						if (Slot && !Slot->GetAutoSize() && Slot->GetAnchors().Minimum == Slot->GetAnchors().Maximum && !Seen.Contains(Widget))
						{
							Seen.Add(Widget);
							NestedTargetCanvas.Add({Widget, Slot});
						}
					}
					for (const auto& Child : Node.Children) CollectCanvas(Child);
				};
				CollectCanvas(*Matches[0].Node);
			}
		if (!TestFalse(TEXT("The nested transition retains live native presentation references"), Transition.LivePresentations.IsEmpty())) return false;
		for (const auto& Live : Transition.LivePresentations)
		{
			UWidget* Widget = CacheWidget(Live.WidgetId);
			auto* Original = Cast<UContentWidget>(CacheWidget(Live.OriginalSocketWidgetId));
			auto* Presentation = Cast<UContentWidget>(CacheWidget(Live.PresentationSocketWidgetId));
			UWidget* LiveContent = Original ? Original->GetContent() : nullptr;
			const auto* Role = LiveContent ? FindFProperty<FObjectPropertyBase>(LiveContent->GetClass(), TEXT("D2UNativeProducer")) : nullptr;
			const bool bSameNativeSource = LiveContent == Widget || (Role && Role->GetObjectPropertyValue_InContainer(LiveContent) == Widget);
			if (!TestTrue(TEXT("One live host starts in its original native socket"), Widget && Original && Presentation
				&& LiveContent && bSameNativeSource && !Presentation->GetContent())) return false;
			LiveObservations.Add({LiveContent, Original, Presentation, Original->Slot, Original->GetParent()});
		}
		for (const auto& Candidate : Navigation.ClickBindings)
			if (Candidate.TriggerWidgetId == NestedTimeoutTriggerId || Candidate.TriggerWidgetId == NestedReturnTimeoutTriggerId)
				for (const auto& Target : Candidate.Transition.WidgetTargets) if (!CacheWidget(Target.NodeId)) return false;
	}
	const auto ResolveStateWidget = [&](const FString& Id) -> UWidget*
	{
		if (!NestedTimeoutTriggerId.IsEmpty())
		{
			UWidget* const* Widget = NestedWidgets.Find(Id);
			TestTrue(TEXT("State lookup keeps the pre-input native object identity"), Widget && *Widget);
			return Widget ? *Widget : nullptr;
		}
		TArray<FRealWidgetMatch> Matches;
		FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, Id, Matches);
		if (!TestEqual(TEXT("State ID resolves uniquely in the actual tree"), Matches.Num(), 1)) return nullptr;
		return Matches[0].Widget;
	};
	const auto Tick = [&](float Seconds)
	{
		++GFrameCounter;
		UUMGSequenceTickManager::Get(Instance)->OnWidgetTicked(Instance);
		FSlateApplication::Get().OnPreTick().Broadcast(Seconds);
		World->GetLatentActionManager().BeginFrame();
		World->GetLatentActionManager().ProcessLatentActions(Instance, Seconds);
		World->GetTimerManager().Tick(Seconds);
		FSlateApplication::Get().OnPostTick().Broadcast(Seconds);
	};
	if (Binding.UsesDragTrigger())
	{
		auto& Slate = FSlateApplication::Get();
		const auto CursorUser = Slate.GetCursorUser();
		if (!TestTrue(TEXT("Slate cursor user is available"), CursorUser.IsValid())) return false;
		const auto PlatformCursor = Slate.GetPlatformCursor();
		if (!TestTrue(TEXT("Platform cursor is available"), PlatformCursor.IsValid())
			|| !TestFalse(TEXT("Cursor user is not already captured"), CursorUser->HasCapture(FSlateApplication::CursorPointerIndex))) return false;
		const FVector2D OriginalCursorPosition = CursorUser->GetCursorPosition();
		if (!TestTrue(TEXT("Test starts in platform cursor mode"), OriginalCursorPosition.Equals(PlatformCursor->GetPosition()))) return false;
		Slate.UsePlatformCursorForCursorUser(false);
		ON_SCOPE_EXIT
		{
			const auto& CapturePath = CursorUser->GetCaptorPath(FSlateApplication::CursorPointerIndex);
			if (CapturePath.IsValid() && CapturePath.GetWindow() == Window)
				CursorUser->ReleaseCapture(FSlateApplication::CursorPointerIndex);
			Slate.SetCursorPos(OriginalCursorPosition);
			Slate.UsePlatformCursorForCursorUser(true);
		};
		auto* Button = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UButton>(Instance->WidgetTree,
			FUMGWidgetTreeBuilder::SanitizeWidgetName(Binding.TriggerWidgetName, TEXT("Button")).ToString());
		if (!TestNotNull(TEXT("Real drag trigger has a native button"), Button)) return false;
		const auto Control = Button->GetCachedWidget();
		const int32 CursorUserIndex = CursorUser->GetUserIndex();
		FVector2D Start;
		const FWidgetPath Path = FindHitPoint(Window, Control, CursorUserIndex, Start);
		if (!Path.IsValid())
		{
			const FVector2D Center = Control->GetCachedGeometry().LocalToAbsolute(Control->GetCachedGeometry().GetLocalSize() * 0.5f);
			AddInfo(FString::Printf(TEXT("Drag hit window: visible=%d acceptsInput=%d containsCenter=%d center=%s position=%s size=%s gridOrigin=%s gridSize=%s"),
				Window->IsVisible(), Window->AcceptsInput(), Window->IsScreenspaceMouseWithin(Center), *Center.ToString(),
				*Window->GetPositionInScreen().ToString(), *Window->GetSizeInScreen().ToString(),
				*FVector2D(Window->GetHittestGrid().GetGridOrigin()).ToString(), *FVector2D(Window->GetHittestGrid().GetGridSize()).ToString()));
			const FWidgetPath Actual = Slate.LocateWindowUnderMouse(Center, {Window}, false, CursorUserIndex);
			for (int32 Index = 0; Index < Actual.Widgets.Num(); ++Index)
				AddInfo(FString::Printf(TEXT("Drag center hit: %s"), *FReflectionMetaData::GetWidgetDebugInfo(Actual.Widgets[Index].Widget.Get())));
			for (UWidget* Current = Button; Current; Current = Current->GetParent())
			{
				const auto& Geometry = Current->GetCachedGeometry();
				AddInfo(FString::Printf(TEXT("Drag hit ancestor: widget=%s class=%s visibility=%d position=%s size=%s slate=%s"),
					*Current->GetName(), *Current->GetClass()->GetName(), static_cast<int32>(Current->GetVisibility()),
					*FVector2D(Geometry.GetAccumulatedLayoutTransform().GetTranslation()).ToString(), *FVector2D(Geometry.GetLocalSize()).ToString(),
					Current->GetCachedWidget().IsValid() ? *Current->GetCachedWidget()->GetTypeAsString() : TEXT("none")));
			}
		}
		if (!TestTrue(TEXT("Native hit test reaches the drag trigger"), Path.IsValid())) return false;
		FVector2D Motion = Transition.bHasDragMotionVector ? Transition.DragMotionVector : FVector2D::ZeroVector;
		if (!Transition.bHasDragMotionVector) for (const auto& Target : Targets)
			if (Target.Source.NodeId == Binding.TriggerWidgetId) Motion = Target.Source.Translation;
		if (!TestTrue(TEXT("Dragged source node supplies a movement direction"), Motion.Normalize())) return false;
		const FGeometry& Geometry = Control->GetCachedGeometry();
		const double Width = Geometry.GetLocalSize().X;
		if (!TestTrue(TEXT("Drag trigger has a positive logical width"), Width > 0.0)) return false;
		const FVector2D Delta = Geometry.LocalToAbsolute(Motion * Width) - Geometry.LocalToAbsolute(FVector2D::ZeroVector);
		const auto* Progress = FindFProperty<FFloatProperty>(Instance->GetClass(),
			FName(*FString::Printf(TEXT("__d2u_drag_%03d_progress"), BindingIndex)));
		const auto* CurrentPage = FindFProperty<FIntProperty>(Instance->GetClass(), TEXT("__d2u_current_page"));
		if (!TestNotNull(TEXT("Native drag progress exists"), Progress)
			|| !TestNotNull(TEXT("Native current page exists"), CurrentPage)) return false;
		const auto* History = FindFProperty<FArrayProperty>(Instance->GetClass(), FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName));
		if (!TestNotNull(TEXT("Native navigation history exists"), History)) return false;
		const auto HistoryCount = [&]() { return FScriptArrayHelper(History, History->ContainerPtrToValuePtr<void>(Instance)).Num(); };
		const int32 InitialHistory = HistoryCount();
		const int32 InitialPage = CurrentPage->GetPropertyValue_InContainer(Instance);
		const int32 TargetPage = Navigation.PageIds.IndexOfByKey(Binding.Steps[0].TargetPageId);
		if (!TestTrue(TEXT("Drag target page is known"), TargetPage != INDEX_NONE && TargetPage != InitialPage)) return false;
		const TSet<FKey> Pressed{EKeys::LeftMouseButton};
		auto Down = [&]()
		{
			Slate.SetCursorPos(Start);
			const FPointerEvent Event(CursorUserIndex, FSlateApplication::CursorPointerIndex, Start, Start,
				Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
			return Slate.RoutePointerDownEvent(Path, Event).IsEventHandled();
		};
		auto Move = [&](const FVector2D& Previous, const FVector2D& Point)
		{
			Slate.SetCursorPos(Point);
			const FPointerEvent Event(CursorUserIndex, FSlateApplication::CursorPointerIndex, Point, Previous,
				Pressed, FKey(), 0, FModifierKeysState());
			Slate.RoutePointerMoveEvent(Path, Event, true);
			Tick(0.016f);
			Tick(0.016f);
		};
		auto Up = [&](const FVector2D& Point, bool bCommit)
		{
			Slate.SetCursorPos(Point);
			const FPointerEvent Event(CursorUserIndex, FSlateApplication::CursorPointerIndex, Point, Point,
				{}, EKeys::LeftMouseButton, 0, FModifierKeysState());
			Slate.RoutePointerUpEvent(Path, Event);
			bool bSampledEndpoint = false;
			for (int32 Index = 0; Index < (bCommit ? 1600 : 40); ++Index)
			{
				Tick(bCommit ? 0.001f : 0.04f);
				if (bCommit && CurrentPage->GetPropertyValue_InContainer(Instance) == TargetPage) break;
				if (bCommit && Transition.bFixedViewportLayout && !bSampledEndpoint
					&& Instance->GetAnimationCurrentTime(Animation) >= Transition.DurationSeconds * 0.99f)
				{
					TestTrue(TEXT("Released drag reaches its actual native layout endpoint"), SampleLayout(true));
					bSampledEndpoint = true;
				}
			}
			if (bCommit && Transition.bFixedViewportLayout) TestTrue(TEXT("Native drag endpoint was sampled before commit"), bSampledEndpoint);
		};
		if (!TestTrue(TEXT("Native drag press is handled"), Down())) return false;
		Move(Start, Start + Delta * 0.35);
		if (Transition.bFixedViewportLayout) TestTrue(TEXT("Dragging changes the actual native layout presentation"), SampleLayout(false));
		const float Forward = Progress->GetPropertyValue_InContainer(Instance);
		const float ForwardSourceAlpha = bOfficialDissolve ? NativeOpacity(DissolveSourceOpacity) : 0;
		const float ForwardDestinationAlpha = bOfficialDissolve ? NativeOpacity(DissolveDestinationOpacity) : 0;
		if (bOfficialDissolve)
		{
			TestTrue(TEXT("Dragging an official dissolve draws both native endpoint groups"), VisibleInTree(DissolveSource) && VisibleInTree(DissolveDestination));
			TestTrue(TEXT("Drag dissolve has intermediate opacity on both groups"), ForwardSourceAlpha > 0 && ForwardSourceAlpha < SourceOpacityBefore
				&& ForwardDestinationAlpha > 0 && ForwardDestinationAlpha < DestinationOpacityBefore);
		}
		Move(Start + Delta * 0.35, Start + Delta * 0.15);
		const float Backward = Progress->GetPropertyValue_InContainer(Instance);
		TestTrue(TEXT("Drag follows the pointer forward below the threshold"), Forward > 0.2f && Forward < 0.5f);
		TestTrue(TEXT("Dragging back reduces progress"), Backward < Forward && Backward > 0.05f);
		if (bOfficialDissolve && !Transition.bSpring)
			TestTrue(TEXT("Dragging back reverses the native dissolve opacity"), NativeOpacity(DissolveSourceOpacity) > ForwardSourceAlpha
				&& NativeOpacity(DissolveDestinationOpacity) < ForwardDestinationAlpha);
		Up(Start + Delta * 0.15, false);
		TestEqual(TEXT("Release below threshold stays on source page"), CurrentPage->GetPropertyValue_InContainer(Instance), InitialPage);
		TestEqual(TEXT("Cancelled drag preserves navigation history"), HistoryCount(), InitialHistory);
		for (const auto& Target : Targets)
			TestTrue(TEXT("Cancelled drag restores source transform"), Target.Widget->GetRenderTransform().Translation.Equals(Target.Initial.Translation, 0.02));
		if (bOfficialDissolve)
		{
			CheckDissolveRestored();
			TestTrue(TEXT("Cancelled dissolve returns to the original source group"), VisibleInTree(DissolveSource) && !VisibleInTree(DissolveDestination));
		}
		CheckLayoutRestored();
		Window->SlatePrepass();
		if (!TestTrue(TEXT("Second native drag press is handled"), Down())) return false;
		Move(Start, Start + Delta * 0.65);
		TestTrue(TEXT("Drag crosses the commit threshold"), Progress->GetPropertyValue_InContainer(Instance) > 0.5f);
		Up(Start + Delta * 0.65, true);
		TestEqual(TEXT("Release above threshold commits the target page"), CurrentPage->GetPropertyValue_InContainer(Instance), TargetPage);
		TestEqual(TEXT("Committed drag records source history once"), HistoryCount(), InitialHistory + 1);
		if (bOfficialDissolve)
		{
			CheckDissolveRestored();
			TestTrue(TEXT("Committed dissolve displays its actual native target"), !VisibleInTree(DissolveSource) && VisibleInTree(DissolveDestination));
		}
		CheckLayoutDestination();
		return !HasAnyErrors();
	}
	TSharedPtr<FSlateVirtualUserHandle> UserHandle;
	TSharedPtr<SWidget> TriggerControl;
	int32 UserIndex = INDEX_NONE;
	const bool bFauxHoverCursor = Binding.UsesHoverTrigger() || Binding.UsesMouseEnterTrigger();
	const auto HoverCursorUser = FSlateApplication::Get().GetCursorUser();
	const FVector2D SavedHoverCursor = HoverCursorUser->GetCursorPosition();
	if (bFauxHoverCursor)
	{
		if (!TestFalse(TEXT("Hover cursor starts without capture"), HoverCursorUser->HasCapture(FSlateApplication::CursorPointerIndex))
			|| !TestTrue(TEXT("Hover probe starts in platform cursor mode"), SavedHoverCursor.Equals(FSlateApplication::Get().GetPlatformCursor()->GetPosition()))) return false;
		FSlateApplication::Get().UsePlatformCursorForCursorUser(false);
	}
	ON_SCOPE_EXIT
	{
		if (bFauxHoverCursor)
		{
			FSlateApplication::Get().SetCursorPos(SavedHoverCursor);
			FSlateApplication::Get().UsePlatformCursorForCursorUser(true);
		}
	};
	const auto* HoverState = bWhileHoverState ? FindFProperty<FIntProperty>(Instance->GetClass(),
		FName(*FString::Printf(TEXT("__d2u_state_%08x"), FCrc::StrCrc32(*Transition.StateHostWidgetId)))) : nullptr;
	const int32 HoverStateBefore = HoverState ? HoverState->GetPropertyValue_InContainer(Instance) : INDEX_NONE;
	const FIntProperty* NestedState = nullptr;
	UWidget* NestedSource = nullptr;
	UWidget* NestedDestination = nullptr;
	float NestedCycleSeconds = 0;
	const auto LogHover = [&](const TCHAR* Stage)
	{
		if (!Binding.UsesHoverTrigger()) return;
		const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
		AddInfo(FString::Printf(TEXT("Hover %s: time=%.6f playing=%d forward=%d state=%d initialState=%d cursor=%s hovered=%d"), Stage,
			Instance->GetAnimationCurrentTime(Animation), Instance->IsAnimationPlaying(Animation), Instance->IsAnimationPlayingForward(Animation),
			HoverState ? HoverState->GetPropertyValue_InContainer(Instance) : INDEX_NONE, HoverStateBefore, *Cursor.ToString(),
			TriggerControl.IsValid() && TriggerControl->IsHovered()));
		if (bWhileHoverState)
		{
			UWidget* Source = ResolveStateWidget(Transition.SourceStateWidgetId);
			UWidget* Destination = ResolveStateWidget(Transition.TargetStateWidgetId);
			if (Source && Destination) AddInfo(FString::Printf(TEXT("Hover native states: source=%s visibility=%d opacity=%.6f target=%s visibility=%d opacity=%.6f"),
				*Source->GetName(), static_cast<int32>(Source->GetVisibility()), Source->GetRenderOpacity(),
				*Destination->GetName(), static_cast<int32>(Destination->GetVisibility()), Destination->GetRenderOpacity()));
		}
		for (const auto& Target : Targets) if (Target.Source.bOpacity)
			AddInfo(FString::Printf(TEXT("Hover opacity: node=%s widget=%s owner=%s actual=%.6f initial=%.6f destination=%.6f"),
				*Target.Source.NodeId, *Target.Widget->GetName(), *Target.OpacityWidget->GetName(), NativeOpacity(Target.OpacityWidget), Target.Opacity, Target.Source.EndOpacity));
	};
	if (Binding.UsesPageOpenTrigger())
	{
		Tick(0.0f);
		Tick(Binding.TriggerDelaySeconds + 0.01f);
	}
	else
	{
		auto* Button = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UButton>(Instance->WidgetTree,
			FUMGWidgetTreeBuilder::SanitizeWidgetName(Binding.TriggerWidgetName, TEXT("Button")).ToString());
		if (!TestNotNull(TEXT("Real trigger has a native button"), Button)) return false;
		TriggerControl = Button->GetCachedWidget();
		if (!TestTrue(TEXT("Native trigger has Slate geometry"), TriggerControl.IsValid())) return false;
		if (bFauxHoverCursor) UserIndex = HoverCursorUser->GetUserIndex();
		else
		{
			int32 VirtualIndex = 0;
			do { UserHandle = FSlateApplication::Get().FindOrCreateVirtualUser(VirtualIndex++); } while (!UserHandle.IsUnique());
			UserIndex = UserHandle->GetUserIndex();
		}
		FVector2D Center;
		const FWidgetPath Path = FindHitPoint(Window, TriggerControl, UserIndex, Center);
		if (!TestTrue(TEXT("Native hit test reaches the real trigger"), Path.IsValid() && Path.ContainsWidget(TriggerControl.Get()))) return false;
		if (bFauxHoverCursor) FSlateApplication::Get().SetCursorPos(Center);
		const FPointerEvent Move(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, FKey(), 0, FModifierKeysState());
		FSlateApplication::Get().RoutePointerMoveEvent(Path, Move, true);
		if (Binding.UsesClickTrigger())
		{
			const TSet<FKey> Pressed{EKeys::LeftMouseButton};
			const FPointerEvent Down(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
			const FPointerEvent Up(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, EKeys::LeftMouseButton, 0, FModifierKeysState());
			const bool bDown = FSlateApplication::Get().RoutePointerDownEvent(Path, Down).IsEventHandled();
			const auto User = FSlateApplication::Get().GetUser(UserIndex);
			const auto& CapturePath = User->GetCaptorPath(FSlateApplication::CursorPointerIndex);
			const bool bCaptured = CapturePath.IsValid() && CapturePath.GetWindow() == Window;
			const bool bUp = FSlateApplication::Get().RoutePointerUpEvent(Path, Up).IsEventHandled();
			if (!TestTrue(TEXT("Native click is handled and released"), bDown && bCaptured && bUp && !User->HasCapture(FSlateApplication::CursorPointerIndex))) return false;
		}
	}
	if (!TestTrue(TEXT("Real interaction starts playback"), Instance->IsAnimationPlaying(Animation))) return false;
	const auto AdvanceToProgress = [&](float Progress)
	{
		const float TargetTime = Transition.DurationSeconds * Progress;
		const float StepSeconds = Transition.DurationSeconds * 0.001f;
		// Sequence evaluation consumes queued ticks; sample its actual clock, not the supplied delta sum.
		for (int32 Frame = 0; Frame < 2048 && Instance->IsAnimationPlaying(Animation)
			&& Instance->GetAnimationCurrentTime(Animation) < TargetTime; ++Frame) Tick(StepSeconds);
		return TestTrue(TEXT("Native animation reaches the requested sampling time while playing"),
			Instance->IsAnimationPlaying(Animation) && Instance->GetAnimationCurrentTime(Animation) >= TargetTime);
	};
	if (!AdvanceToProgress(0.5f)) return false;
	if (!NestedTimeoutTriggerId.IsEmpty())
	{
		const FVector2D Leave = Window->GetCachedGeometry().LocalToAbsolute(FVector2D(1, 1));
		const TArray<TSharedRef<SWindow>> Windows{Window};
		const FWidgetPath LeavePath = FSlateApplication::Get().LocateWindowUnderMouse(Leave, Windows, false, UserIndex);
		if (!TestTrue(TEXT("Mid-animation pointer exit reaches a different native widget"), LeavePath.IsValid() && !LeavePath.ContainsWidget(TriggerControl.Get()))) return false;
		if (bFauxHoverCursor) FSlateApplication::Get().SetCursorPos(Leave);
		const FPointerEvent LeaveMove(UserIndex, FSlateApplication::CursorPointerIndex, Leave, Leave, {}, FKey(), 0, FModifierKeysState());
		FSlateApplication::Get().RoutePointerMoveEvent(LeavePath, LeaveMove, true);
		Tick(Transition.DurationSeconds * 0.01f);
		TestTrue(TEXT("Leaving midway reverses the active outer animation"), Instance->IsAnimationPlaying(Animation));
		for (int32 Frame = 0; Frame < 2048 && Instance->IsAnimationPlaying(Animation); ++Frame) Tick(Transition.DurationSeconds * 0.01f);
		Tick(Transition.DurationSeconds * 0.01f);
		for (const auto& Live : LiveObservations)
			TestTrue(TEXT("Midway reverse returns the live host without replacing its original parent slot"), Live.Original->GetContent() == Live.Widget
				&& !Live.Presentation->GetContent() && Live.Original->Slot == Live.OriginalSlot && Live.Original->GetParent() == Live.OriginalParent);
		FSlateApplication::Get().ForceRedrawWindow(Window); FlushRenderingCommands();
		FVector2D Center; const FWidgetPath Reenter = FindHitPoint(Window, TriggerControl, UserIndex, Center);
		if (!TestTrue(TEXT("Original native hover trigger remains hit-testable after midway return"), Reenter.IsValid() && Reenter.ContainsWidget(TriggerControl.Get()))) return false;
		if (bFauxHoverCursor) FSlateApplication::Get().SetCursorPos(Center);
		const FPointerEvent ReenterMove(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, FKey(), 0, FModifierKeysState());
		FSlateApplication::Get().RoutePointerMoveEvent(Reenter, ReenterMove, true);
		if (!AdvanceToProgress(0.5f)) return false;
	}
	if (!TestTrue(TEXT("Native animation plays at an intermediate frame"), Instance->IsAnimationPlaying(Animation))) return false;
	bool bChanged = false;
	LogDissolve(TEXT("middle"));
	LogHover(TEXT("middle"));
	bChanged |= SampleLayout(false);
	if (bOfficialDissolve)
	{
		const float SourceAlpha = NativeOpacity(DissolveSourceOpacity), DestinationAlpha = NativeOpacity(DissolveDestinationOpacity);
		TestTrue(TEXT("Official dissolve paints both native groups during the transition"), VisibleInTree(DissolveSource) && VisibleInTree(DissolveDestination));
		TestTrue(TEXT("Official dissolve source has intermediate opacity"), SourceAlpha > 0 && SourceAlpha < SourceOpacityBefore);
		TestTrue(TEXT("Official dissolve destination has intermediate opacity"), DestinationAlpha > 0 && DestinationAlpha < DestinationOpacityBefore);
		bChanged = !FMath::IsNearlyEqual(SourceAlpha, SourceOpacityBefore, 0.01f) && !FMath::IsNearlyZero(DestinationAlpha, 0.01f);
		TestTrue(TEXT("Official dissolve changes opacity without morphing endpoint transforms"), DissolveSource->GetRenderTransform() == SourceTransformBefore
			&& DissolveDestination->GetRenderTransform() == DestinationTransformBefore);
	}
	for (const auto& Target : Targets)
	{
		const auto Current = Target.Widget->GetRenderTransform();
		bChanged |= !Current.Translation.Equals(Target.Initial.Translation, 0.01) || !Current.Scale.Equals(Target.Initial.Scale, 0.01)
			|| !FMath::IsNearlyEqual(Current.Angle, Target.Initial.Angle, 0.01f)
			|| !FMath::IsNearlyEqual(NativeOpacity(Target.OpacityWidget), Target.Opacity, 0.01f);
		if (Target.Source.bColor)
		{
			FLinearColor Color;
			if (!TestTrue(TEXT("Read intermediate color"), ReadSolidColor(Target.ColorWidget, Color))) return false;
			bChanged |= !Color.Equals(Target.Color, 0.01f);
		}
	}
	for (const auto& Order : LayerOrders)
	{
		bChanged |= Order.Slot->GetZOrder() != Order.InitialZ;
		TestEqual(TEXT("Native layer order reaches the source target index"), Order.Slot->GetZOrder(), Order.EndIndex);
	}
	if (!TestTrue(TEXT("A real property or layer order changes during playback"), bChanged)) return false;
	if (!Transition.bSpring)
	{
		if (!AdvanceToProgress(0.99f)) return false;
		LogDissolve(TEXT("near-end"));
		LogHover(TEXT("near-end"));
		SampleLayout(true);
		if (bOfficialDissolve)
		{
			TestTrue(TEXT("Official dissolve approaches a transparent source endpoint"), NativeOpacity(DissolveSourceOpacity) <= SourceOpacityBefore * 0.03f);
			TestTrue(TEXT("Official dissolve approaches the destination's full endpoint opacity"), NativeOpacity(DissolveDestinationOpacity) >= DestinationOpacityBefore * 0.97f);
		}
		for (const auto& Target : Targets)
		{
			const auto Current = Target.Widget->GetRenderTransform();
			const double PositionTolerance = FMath::Max(0.02, Target.Source.Translation.Size() * 0.01);
			if (!Target.Source.Translation.IsNearlyZero())
				TestTrue(TEXT("Native movement approaches the real endpoint"), Current.Translation.Equals(Target.Initial.Translation + Target.Source.Translation, PositionTolerance));
			if (!Target.Source.Scale.Equals(FVector2D(1, 1)))
				TestTrue(TEXT("Native scale approaches the real endpoint"), Current.Scale.Equals(Target.Initial.Scale * Target.Source.Scale, 0.02));
			if (!FMath::IsNearlyZero(Target.Source.Rotation))
				TestTrue(TEXT("Native rotation approaches the real endpoint"), FMath::IsNearlyEqual(Current.Angle, Target.Initial.Angle + Target.Source.Rotation, FMath::Max(0.02f, FMath::Abs(Target.Source.Rotation) * 0.01f)));
			if (Target.Source.bOpacity)
				TestTrue(TEXT("Native opacity approaches the real endpoint"), FMath::IsNearlyEqual(NativeOpacity(Target.OpacityWidget), Target.Source.EndOpacity, 0.02f));
			if (Target.Source.bColor)
			{
				FLinearColor Color;
				if (!TestTrue(TEXT("Read endpoint native color"), ReadSolidColor(Target.ColorWidget, Color))) return false;
				TestTrue(TEXT("Native color approaches the real endpoint"), Color.Equals(Target.Source.EndColor, 0.02f));
			}
		}
	}
	for (int32 Frame = 0; Frame < 5; ++Frame) Tick(Transition.DurationSeconds * 0.25f);
	LogDissolve(TEXT("completed"));
	LogHover(TEXT("completed"));
	CheckLayoutDestination();
	if (bOfficialDissolve)
	{
		if (bWhileHoverState)
			TestTrue(TEXT("While-hover dissolve keeps source layout/input and its displayed native target"), VisibleInTree(DissolveSource)
				&& VisibleInTree(DissolveDestination) && DissolveDestination->GetVisibility() == ESlateVisibility::HitTestInvisible);
		else
		{
			CheckDissolveRestored();
			TestTrue(TEXT("Completed official dissolve hides its original group and displays its native target"), !VisibleInTree(DissolveSource) && VisibleInTree(DissolveDestination));
		}
	}
	for (const auto& Order : LayerOrders)
		TestEqual(TEXT("Completed source layer order is restored for re-entry"), Order.Slot->GetZOrder(), Order.InitialZ);
	if (Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate)
	{
		auto* Host = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UCanvasPanel>(Instance->WidgetTree,
			FUMGWidgetTreeBuilder::SanitizeWidgetName(Navigation.PageHostWidgetName, TEXT("CanvasPanel")).ToString());
		if (!TestNotNull(TEXT("Native page host exists"), Host)) return false;
		const FString TargetName = FUMGWidgetTreeBuilder::SanitizeWidgetName(Binding.Steps[0].TargetPageId, TEXT("CanvasPanel")).ToString();
		bool bPageVisible = false;
		for (UWidget* Page : Host->GetAllChildren())
			bPageVisible |= Page && Page->GetVisibility() != ESlateVisibility::Collapsed && Page->GetVisibility() != ESlateVisibility::Hidden
				&& FUMGWidgetTreeBuilder::MatchesWidgetBaseName(Page->GetName(), TargetName);
		if (!TestTrue(TEXT("Native animation reaches the real destination"), bPageVisible)) return false;
		for (const auto& Target : Targets)
		{
			const auto Current = Target.Widget->GetRenderTransform();
			TestTrue(TEXT("Leaving the source restores its original transform"), Current.Translation.Equals(Target.Initial.Translation, 0.01)
				&& Current.Scale.Equals(Target.Initial.Scale, 0.01) && FMath::IsNearlyEqual(Current.Angle, Target.Initial.Angle, 0.01f));
			if (Target.Source.bOpacity) TestTrue(TEXT("Leaving restores source opacity"), FMath::IsNearlyEqual(NativeOpacity(Target.OpacityWidget), Target.Opacity, 0.01f));
			if (Target.Source.bColor)
			{
				FLinearColor Color;
				if (!TestTrue(TEXT("Read restored source paint"), ReadSolidColor(Target.ColorWidget, Color))) return false;
				TestTrue(TEXT("Leaving restores source paint"), Color.Equals(Target.Color, 0.01f));
			}
		}
		if (SourcePage)
		{
			if (UserIndex == INDEX_NONE)
			{
				int32 VirtualIndex = 0;
				do { UserHandle = FSlateApplication::Get().FindOrCreateVirtualUser(VirtualIndex++); } while (!UserHandle.IsUnique());
				UserIndex = UserHandle->GetUserIndex();
			}
			Window->SlatePrepass();
			FSlateApplication::Get().Tick();
			TArray<FRealWidgetMatch> ReturnMatches;
			FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, ReturnBindings[0]->TriggerWidgetId, ReturnMatches);
			if (!TestEqual(TEXT("Actual return trigger resolves uniquely"), ReturnMatches.Num(), 1)) return false;
			auto* ReturnButton = Cast<UButton>(ReturnMatches[0].Widget);
			if (!TestNotNull(TEXT("Return trigger is a native button"), ReturnButton)) return false;
			const auto ReturnControl = ReturnButton->GetCachedWidget();
			FVector2D ReturnPoint;
			const FWidgetPath ReturnPath = FindHitPoint(Window, ReturnControl, UserIndex, ReturnPoint);
			if (!TestTrue(TEXT("Actual return button is hit-testable"), ReturnPath.IsValid())) return false;
			const FPointerEvent Move(UserIndex, FSlateApplication::CursorPointerIndex, ReturnPoint, ReturnPoint, {}, FKey(), 0, FModifierKeysState());
			FSlateApplication::Get().RoutePointerMoveEvent(ReturnPath, Move, true);
			const TSet<FKey> Pressed{EKeys::LeftMouseButton};
			const FPointerEvent Down(UserIndex, FSlateApplication::CursorPointerIndex, ReturnPoint, ReturnPoint, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
			const FPointerEvent Up(UserIndex, FSlateApplication::CursorPointerIndex, ReturnPoint, ReturnPoint, {}, EKeys::LeftMouseButton, 0, FModifierKeysState());
			if (!TestTrue(TEXT("Actual return press is handled"), FSlateApplication::Get().RoutePointerDownEvent(ReturnPath, Down).IsEventHandled())
				|| !TestTrue(TEXT("Actual return release is handled"), FSlateApplication::Get().RoutePointerUpEvent(ReturnPath, Up).IsEventHandled())) return false;
			const float StepSeconds = FMath::Max(0.03f, ReturnBindings[0]->Transition.DurationSeconds * 0.25f);
			for (int32 Frame = 0; Frame < 8; ++Frame) Tick(StepSeconds);
			TArray<FRealWidgetMatch> RestoredPages;
			FindRealWidgets(Analysis.Data.RootWidget, Instance->WidgetTree->RootWidget, Binding.SourcePageId, RestoredPages);
			TestTrue(TEXT("Return reuses the original source page object"), RestoredPages.Num() == 1 && RestoredPages[0].Widget == SourcePage);
			TestTrue(TEXT("Return displays the original page"), SourcePage->GetVisibility() != ESlateVisibility::Collapsed && SourcePage->GetVisibility() != ESlateVisibility::Hidden);
			TestEqual(TEXT("Unrelated page state survives the round trip"), SourcePage->GetToolTipText().ToString(), FString(TEXT("Prototype state retained")));
		}
	}
	else
	{
		if (!TestFalse(TEXT("Native property animation completes"), Instance->IsAnimationPlaying(Animation))) return false;
		if (!Transition.StateHostWidgetId.IsEmpty())
		{
			auto* SourceState = ResolveStateWidget(Transition.SourceStateWidgetId);
			auto* TargetState = ResolveStateWidget(Transition.TargetStateWidgetId);
			if (!TestNotNull(TEXT("Native source state exists"), SourceState)
				|| !TestNotNull(TEXT("Native target state exists"), TargetState)) return false;
			if (bWhileHoverState)
			{
				TestTrue(TEXT("While-hover state retains its source layout and trigger"), VisibleInTree(SourceState) && TriggerControl->IsHovered());
				if (!bOfficialDissolve) TestEqual(TEXT("Smart Animate hover does not commit the alternative native state"), TargetState->GetVisibility(), ESlateVisibility::Collapsed);
				if (!TestNotNull(TEXT("While-hover state exposes its native selection"), HoverState)) return false;
				TestEqual(TEXT("While-hover animation preserves the source state selection"), HoverState->GetPropertyValue_InContainer(Instance), HoverStateBefore);
			}
			else
			{
				TestTrue(TEXT("Completed source state no longer participates in layout"), SourceState->GetVisibility() == ESlateVisibility::Collapsed);
				TestTrue(TEXT("Completed target state is displayed"), TargetState->GetVisibility() != ESlateVisibility::Collapsed
					&& TargetState->GetVisibility() != ESlateVisibility::Hidden);
			}
		}
		for (const auto& Target : Targets)
		{
			const auto Current = Target.Widget->GetRenderTransform();
			if (!Transition.StateHostWidgetId.IsEmpty() && !bWhileHoverState)
			{
				TestTrue(TEXT("Completed source transform is restored for re-entry"), Current.Translation.Equals(Target.Initial.Translation, 0.01)
					&& Current.Scale.Equals(Target.Initial.Scale, 0.01) && FMath::IsNearlyEqual(Current.Angle, Target.Initial.Angle, 0.01f));
				if (Target.Source.bOpacity) TestTrue(TEXT("Completed source opacity is restored"), FMath::IsNearlyEqual(NativeOpacity(Target.OpacityWidget), Target.Opacity, 0.01f));
				if (Target.Source.bColor)
				{
					FLinearColor Color;
					if (!TestTrue(TEXT("Read restored source color"), ReadSolidColor(Target.ColorWidget, Color))) return false;
					TestTrue(TEXT("Completed source color is restored"), Color.Equals(Target.Color, 0.01f));
				}
				continue;
			}
			if (!Target.Source.Translation.IsNearlyZero()) TestTrue(TEXT("Translation reaches the real endpoint"), Current.Translation.Equals(Target.Initial.Translation + Target.Source.Translation, 0.01));
			if (!Target.Source.Scale.Equals(FVector2D(1, 1))) TestTrue(TEXT("Scale reaches the real endpoint"), Current.Scale.Equals(Target.Initial.Scale * Target.Source.Scale, 0.01));
			if (!FMath::IsNearlyZero(Target.Source.Rotation)) TestTrue(TEXT("Rotation reaches the real endpoint"), FMath::IsNearlyEqual(Current.Angle, Target.Initial.Angle + Target.Source.Rotation, 0.01f));
			if (Target.Source.bOpacity) TestTrue(TEXT("Opacity reaches the real endpoint"), FMath::IsNearlyEqual(NativeOpacity(Target.OpacityWidget), Target.Source.EndOpacity, 0.01f));
			if (Target.Source.bColor)
			{
				FLinearColor Color;
				if (!TestTrue(TEXT("Read final native color"), ReadSolidColor(Target.ColorWidget, Color))) return false;
				TestTrue(TEXT("Color reaches the real endpoint"), Color.Equals(Target.Source.EndColor, 0.01f));
			}
		}
		if (!NestedTimeoutTriggerId.IsEmpty())
		{
			int32 ForwardIndex = INDEX_NONE, ReturnIndex = INDEX_NONE;
			for (int32 Index = 0; Index < Navigation.ClickBindings.Num(); ++Index)
			{
				const auto& Candidate = Navigation.ClickBindings[Index];
				if (!Candidate.UsesPageOpenTrigger()) continue;
				int32* Selected = Candidate.TriggerWidgetId == NestedTimeoutTriggerId ? &ForwardIndex
					: (Candidate.TriggerWidgetId == NestedReturnTimeoutTriggerId ? &ReturnIndex : nullptr);
				if (!Selected) continue;
				if (!TestEqual(TEXT("Requested nested timer is unique"), *Selected, INDEX_NONE)) return false;
				*Selected = Index;
			}
			if (!TestTrue(TEXT("Both nested timers exist in production analysis"), ForwardIndex != INDEX_NONE && ReturnIndex != INDEX_NONE)) return false;
			const auto& Forward = Navigation.ClickBindings[ForwardIndex];
			const auto& Return = Navigation.ClickBindings[ReturnIndex];
			const auto& F = Forward.Transition; const auto& R = Return.Transition;
			if (!TestTrue(TEXT("Nested timers are admitted animated CHANGE_TO actions with reciprocal endpoints"),
				Forward.Steps.Num() == 1 && Return.Steps.Num() == 1
				&& Forward.Steps[0].Kind == EUMGPrototypeNavigationStepKind::AnimateWidget
				&& Return.Steps[0].Kind == EUMGPrototypeNavigationStepKind::AnimateWidget
				&& F.Diagnostic.IsEmpty() && R.Diagnostic.IsEmpty() && F.IsAnimated() && R.IsAnimated()
				&& !F.StateHostWidgetId.IsEmpty() && F.StateHostWidgetId == R.StateHostWidgetId
				&& F.StateHostWidgetId != Transition.StateHostWidgetId
				&& F.SourceStateWidgetId == R.TargetStateWidgetId && F.TargetStateWidgetId == R.SourceStateWidgetId
				&& F.SourceStateWidgetId != F.TargetStateWidgetId && F.DurationSeconds > 0 && R.DurationSeconds > 0)) return false;
			const auto* Host = Navigation.StateHosts.FindByPredicate([&](const FUMGPrototypeStateHost& H) { return H.HostWidgetId == F.StateHostWidgetId; });
			if (!TestNotNull(TEXT("Nested timer owns a production state host"), Host)) return false;
			if (!TestTrue(TEXT("Requested timer host belongs to the hovered instance subtree"),
				!Transition.InstanceScopeId.IsEmpty() && F.InstanceScopeId == Host->InstanceWidgetId
				&& F.InstanceScopeId.StartsWith(Transition.InstanceScopeId + TEXT("::"))
				&& Forward.SourcePageId == Binding.SourcePageId && Return.SourcePageId == Binding.SourcePageId)) return false;
			const int32 SourceIndex = Host->States.IndexOfByPredicate([&](const FUMGPrototypeStateEntry& S) { return S.WidgetId == F.SourceStateWidgetId; });
			const int32 TargetIndex = Host->States.IndexOfByPredicate([&](const FUMGPrototypeStateEntry& S) { return S.WidgetId == F.TargetStateWidgetId; });
			NestedState = FindFProperty<FIntProperty>(Instance->GetClass(), FName(*FString::Printf(TEXT("__d2u_state_%08x"), FCrc::StrCrc32(*F.StateHostWidgetId))));
			NestedSource = ResolveStateWidget(F.SourceStateWidgetId); NestedDestination = ResolveStateWidget(F.TargetStateWidgetId);
			if (!TestNotNull(TEXT("Nested selection exists on the actual saved instance"), NestedState)
				|| !TestNotNull(TEXT("Nested source is unique"), NestedSource) || !TestNotNull(TEXT("Nested target is unique"), NestedDestination)
				|| !TestTrue(TEXT("Both nested endpoint indices exist"), SourceIndex != INDEX_NONE && TargetIndex != INDEX_NONE)) return false;
			const auto SavedAnimation = [&](int32 Index) -> UWidgetAnimation*
			{
				const auto* P = FindFProperty<FObjectProperty>(Instance->GetClass(), FName(*FString::Printf(TEXT("__d2u_nav_anim_%03d_properties"), Index)));
				return P ? Cast<UWidgetAnimation>(P->GetObjectPropertyValue_InContainer(Instance)) : nullptr;
			};
			UWidgetAnimation* ForwardAnimation = SavedAnimation(ForwardIndex); UWidgetAnimation* ReturnAnimation = SavedAnimation(ReturnIndex);
			if (!TestNotNull(TEXT("Nested forward animation is saved"), ForwardAnimation)
				|| !TestNotNull(TEXT("Nested return animation is saved"), ReturnAnimation) || !TestNotNull(TEXT("Outer hover selection exists"), HoverState)) return false;
			UWidget* OuterSource = ResolveStateWidget(Transition.SourceStateWidgetId); UWidget* OuterTarget = ResolveStateWidget(Transition.TargetStateWidgetId);
			if (!OuterSource || !OuterTarget) return false;
			const auto OuterTransform = OuterSource->GetRenderTransform(); const auto OuterParent = OuterSource->GetParent();
			const auto OuterSourceVisibility = OuterSource->GetVisibility(), OuterTargetVisibility = OuterTarget->GetVisibility();
			const int32 InitialNestedSelection = NestedState->GetPropertyValue_InContainer(Instance);
			const auto TimedOpacityOwner = [&](const FUMGPrototypeNavigationBinding& Timed, UWidgetAnimation* TimedAnimation) -> UWidget*
			{
				for (const auto& Target : Timed.Transition.WidgetTargets)
					if (Target.bOpacity && !FMath::IsNearlyEqual(Target.StartOpacity, Target.EndOpacity))
					{
						UWidget* Widget = NestedWidgets.FindRef(Target.NodeId);
						if (Widget) return ResolveNativeTrackOwner<UMovieSceneFloatTrack>(Instance->WidgetTree, TimedAnimation, Widget);
					}
				return nullptr;
			};
			UWidget* ForwardOpacity = TimedOpacityOwner(Forward, ForwardAnimation);
			UWidget* ReturnOpacity = TimedOpacityOwner(Return, ReturnAnimation);
			if (!TestNotNull(TEXT("Forward has an actual native opacity track owner"), ForwardOpacity)
				|| !TestNotNull(TEXT("Return has an actual native opacity track owner"), ReturnOpacity)) return false;
			const float InitialForwardOpacity = NativeOpacity(ForwardOpacity), InitialReturnOpacity = NativeOpacity(ReturnOpacity);
			bool ForwardPropertyChanged = false, ReturnPropertyChanged = false, LiveIdentityStable = true;
			bool CheckedTargetCanvas = false;
			FSlateApplication::Get().ForceRedrawWindow(Window); FlushRenderingCommands();
			for (const auto& Live : LiveObservations)
				TestTrue(TEXT("Borrowed live host has a visible arranged native presentation"), VisibleInTree(Live.Widget)
					&& Live.Widget->GetCachedGeometry().GetLocalSize().X > 0 && Live.Widget->GetCachedGeometry().GetLocalSize().Y > 0);
			bool ForwardProgress = false, ReturnProgress = false, ReachedTarget = false, Returned = false, OuterStable = true;
			const float StepSeconds = FMath::Min(F.DurationSeconds, R.DurationSeconds) * 0.01f;
			NestedCycleSeconds = F.DurationSeconds + R.DurationSeconds + Forward.TriggerDelaySeconds + Return.TriggerDelaySeconds;
			for (int32 Frame = 0; Frame < 3 * 2048 && !(ForwardProgress && ForwardPropertyChanged && ReturnProgress && ReturnPropertyChanged && Returned); ++Frame)
			{
				Tick(StepSeconds);
				ForwardProgress |= VisibleInTree(NestedSource) && Instance->IsAnimationPlaying(ForwardAnimation)
					&& Instance->GetAnimationCurrentTime(ForwardAnimation) > ForwardAnimation->GetStartTime();
				ReturnProgress |= VisibleInTree(NestedDestination) && Instance->IsAnimationPlaying(ReturnAnimation)
					&& Instance->GetAnimationCurrentTime(ReturnAnimation) > ReturnAnimation->GetStartTime();
				const float ForwardValue = NativeOpacity(ForwardOpacity), ReturnValue = NativeOpacity(ReturnOpacity);
				ForwardPropertyChanged |= VisibleInTree(ForwardOpacity) && !FMath::IsNearlyEqual(ForwardValue, InitialForwardOpacity, 0.01f);
				ReturnPropertyChanged |= VisibleInTree(ReturnOpacity) && !FMath::IsNearlyEqual(ReturnValue, InitialReturnOpacity, 0.01f);
				for (const auto& Live : LiveObservations)
					LiveIdentityStable &= Live.Presentation->GetContent() == Live.Widget && !Live.Original->GetContent()
						&& Live.Original->Slot == Live.OriginalSlot && Live.Original->GetParent() == Live.OriginalParent;
				const int32 Selected = NestedState->GetPropertyValue_InContainer(Instance);
				if (!CheckedTargetCanvas && Selected == TargetIndex && VisibleInTree(NestedDestination))
				{
					FSlateApplication::Get().ForceRedrawWindow(Window); FlushRenderingCommands();
					int32 PresentedCanvas = 0;
					for (const auto& Item : NestedTargetCanvas)
					{
						if (!VisibleInTree(Item.Widget)) continue;
						++PresentedCanvas;
						const FVector2D Size = Item.Slot->GetSize();
						const FVector2D Arranged = Item.Widget->GetCachedGeometry().GetLocalSize();
						TestTrue(TEXT("Activated non-initial Canvas children retain their actual native slot and positive dimensions"),
							Item.Widget->Slot == Item.Slot && Size.X > 0 && Size.Y > 0 && Arranged.X > 0 && Arranged.Y > 0);
					}
					AddInfo(FString::Printf(TEXT("Activated nested target Canvas observations: modeled=%d presented=%d"), NestedTargetCanvas.Num(), PresentedCanvas));
					CheckedTargetCanvas = true;
				}
				ReachedTarget |= Selected == TargetIndex && VisibleInTree(NestedDestination);
				Returned |= ReachedTarget && Selected == SourceIndex && VisibleInTree(NestedSource);
				OuterStable &= HoverState->GetPropertyValue_InContainer(Instance) == HoverStateBefore
					&& OuterSource->GetParent() == OuterParent && OuterSource->GetRenderTransform() == OuterTransform
					&& OuterSource->GetVisibility() == OuterSourceVisibility && OuterTarget->GetVisibility() == OuterTargetVisibility;
			}
			TestTrue(TEXT("Real hover activates nested forward animation progress and state commit"), ForwardProgress && ForwardPropertyChanged && ReachedTarget);
			TestTrue(TEXT("Nested timer plays its return animation and returns to its source state"), ReturnProgress && ReturnPropertyChanged && Returned);
			TestTrue(TEXT("Nested timers retain the outer hover selection and native layout host"), OuterStable && TriggerControl->IsHovered());
			TestTrue(TEXT("One live host remains in its presentation socket without changing the original parent slot"), LiveIdentityStable);
			AddInfo(FString::Printf(TEXT("Nested timers: forward=%s return=%s host=%s outerType=%s outerTargetVisibility=%d initialSelected=%d selected=%d forwardProgress=%d returnProgress=%d targetSeen=%d returned=%d sourcePresented=%d targetPresented=%d"),
				*NestedTimeoutTriggerId, *NestedReturnTimeoutTriggerId, *F.StateHostWidgetId, *Transition.Type, int32(OuterTarget->GetVisibility()),
				InitialNestedSelection, NestedState->GetPropertyValue_InContainer(Instance), ForwardProgress, ReturnProgress, ReachedTarget, Returned, VisibleInTree(NestedSource), VisibleInTree(NestedDestination)));
		}
		if (Binding.UsesHoverTrigger())
		{
			const FVector2D Leave = Window->GetCachedGeometry().LocalToAbsolute(FVector2D(1, 1));
			const TArray<TSharedRef<SWindow>> Windows{Window};
			const FWidgetPath LeavePath = FSlateApplication::Get().LocateWindowUnderMouse(Leave, Windows, false, UserIndex);
			if (!TestTrue(TEXT("A native pointer can leave the hovered trigger"), LeavePath.IsValid() && !LeavePath.ContainsWidget(TriggerControl.Get()))) return false;
			const FPointerEvent Move(UserIndex, FSlateApplication::CursorPointerIndex, Leave, Leave, {}, FKey(), 0, FModifierKeysState());
			if (bFauxHoverCursor) FSlateApplication::Get().SetCursorPos(Leave);
			FSlateApplication::Get().RoutePointerMoveEvent(LeavePath, Move, true);
			if (Transition.bFixedViewportLayout)
			{
				Tick(Transition.DurationSeconds * 0.25f); PaintLayout();
				TestTrue(TEXT("Hover exit reverses the native layout animation"), Instance->IsAnimationPlaying(Animation));
				TestTrue(TEXT("Reverse layout uses the retained native presentation"), VisibleInTree(LayoutPresentation));
			}
			if (bOfficialDissolve)
			{
				Tick(Transition.DurationSeconds * 0.25f);
				TestTrue(TEXT("Hover exit reverses the official native dissolve"), Instance->IsAnimationPlaying(Animation));
				TestTrue(TEXT("Reverse dissolve draws both endpoint groups"), VisibleInTree(DissolveSource) && VisibleInTree(DissolveDestination));
			}
			for (int32 Frame = 0; Frame < 5; ++Frame) Tick(Transition.DurationSeconds * 0.25f);
			LogHover(TEXT("left"));
			if (bOfficialDissolve)
			{
				CheckDissolveRestored();
				TestTrue(TEXT("Hover exit restores the original native endpoint group"), VisibleInTree(DissolveSource) && !VisibleInTree(DissolveDestination));
			}
			if (Transition.bFixedViewportLayout)
			{
				PaintLayout(); CheckLayoutRestored();
				TestTrue(TEXT("Hover exit restores the original native layout"), VisibleInTree(LayoutSource) && !VisibleInTree(LayoutDestination));
			}
			if (!Transition.StateHostWidgetId.IsEmpty())
			{
				auto* SourceState = ResolveStateWidget(Transition.SourceStateWidgetId);
				auto* TargetState = ResolveStateWidget(Transition.TargetStateWidgetId);
					if (!TestNotNull(TEXT("Original state still exists"), SourceState)
						|| !TestNotNull(TEXT("Hovered state still exists"), TargetState)) return false;
					TestTrue(TEXT("Hover exit reactivates the original state"), SourceState->GetVisibility() != ESlateVisibility::Collapsed
						&& SourceState->GetVisibility() != ESlateVisibility::Hidden);
					TestEqual(TEXT("Hover exit hides the target state"), TargetState->GetVisibility(), ESlateVisibility::Collapsed);
				}
			for (const auto& Target : Targets)
			{
				const auto Current = Target.Widget->GetRenderTransform();
				TestTrue(TEXT("Hover exit restores the native source transform"), Current.Translation.Equals(Target.Initial.Translation, 0.01)
					&& Current.Scale.Equals(Target.Initial.Scale, 0.01) && FMath::IsNearlyEqual(Current.Angle, Target.Initial.Angle, 0.01f));
				if (Target.Source.bOpacity) TestTrue(TEXT("Hover exit restores source opacity"), FMath::IsNearlyEqual(NativeOpacity(Target.OpacityWidget), Target.Opacity, 0.01f));
				if (Target.Source.bColor)
				{
					FLinearColor Color;
					if (!TestTrue(TEXT("Read restored native color"), ReadSolidColor(Target.ColorWidget, Color))) return false;
					TestTrue(TEXT("Hover exit restores source color"), Color.Equals(Target.Color, 0.01f));
				}
			}
		}
		if (NestedState)
		{
			for (const auto& Live : LiveObservations)
				TestTrue(TEXT("Reverse returns the same live host to its unchanged original slot"), Live.Original->GetContent() == Live.Widget
					&& !Live.Presentation->GetContent() && Live.Original->Slot == Live.OriginalSlot && Live.Original->GetParent() == Live.OriginalParent);
			UWidget* OuterSource = ResolveStateWidget(Transition.SourceStateWidgetId); UWidget* OuterTarget = ResolveStateWidget(Transition.TargetStateWidgetId);
			if (!OuterSource || !OuterTarget) return false;
			const auto Transform = OuterSource->GetRenderTransform(); const auto Visibility = OuterSource->GetVisibility();
			const int32 HiddenSelection = NestedState->GetPropertyValue_InContainer(Instance);
			const auto Presented = [](UWidget* Widget)
			{
				for (UWidget* Current = Widget; Current;)
				{
					if (Current->GetVisibility() == ESlateVisibility::Collapsed || Current->GetVisibility() == ESlateVisibility::Hidden
						|| FMath::IsNearlyZero(NativeOpacity(Current), 0.001f)) return false;
					if (Current->GetParent()) Current = Current->GetParent();
					else
					{
						const auto* Tree = Current->GetTypedOuter<UWidgetTree>();
						auto* Owner = Tree ? Tree->GetTypedOuter<UUserWidget>() : nullptr;
						Current = Owner != Current ? Owner : nullptr;
					}
				}
				return Widget != nullptr;
			};
			bool HiddenOuterStable = true;
			for (int32 Frame = 0; Frame < 64; ++Frame)
			{
				Tick(NestedCycleSeconds / 32.0f);
				HiddenOuterStable &= HoverState->GetPropertyValue_InContainer(Instance) == HoverStateBefore
					&& OuterSource->GetVisibility() == Visibility && OuterSource->GetRenderTransform() == Transform
					&& OuterTarget->GetVisibility() == ESlateVisibility::Collapsed && !Presented(NestedSource) && !Presented(NestedDestination);
			}
			TestTrue(TEXT("Hidden nested timers cannot replace the visible outer state after pointer exit"), HiddenOuterStable);
			AddInfo(FString::Printf(TEXT("Hidden nested selection observed without imposing reset semantics: before=%d after=%d"),
				HiddenSelection, NestedState->GetPropertyValue_InContainer(Instance)));
			FSlateApplication::Get().ForceRedrawWindow(Window); FlushRenderingCommands();
			FVector2D Center; const FWidgetPath Reenter = FindHitPoint(Window, TriggerControl, UserIndex, Center);
			if (!TestTrue(TEXT("Completed hover return preserves the original native input target"), Reenter.IsValid() && Reenter.ContainsWidget(TriggerControl.Get()))) return false;
			if (bFauxHoverCursor) FSlateApplication::Get().SetCursorPos(Center);
			const FPointerEvent ReenterMove(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, FKey(), 0, FModifierKeysState());
			FSlateApplication::Get().RoutePointerMoveEvent(Reenter, ReenterMove, true);
			for (const auto& Live : LiveObservations)
				TestTrue(TEXT("Reentry presents the same live host without changing its original slot"), Live.Presentation->GetContent() == Live.Widget
					&& Live.Original->Slot == Live.OriginalSlot && Live.Original->GetParent() == Live.OriginalParent);
			TestTrue(TEXT("Reentry retains the nested committed state before its next timer"), NestedState->GetPropertyValue_InContainer(Instance) == HiddenSelection);
			const FVector2D Leave = Window->GetCachedGeometry().LocalToAbsolute(FVector2D(1, 1));
			const TArray<TSharedRef<SWindow>> Windows{Window};
			const FWidgetPath LeavePath = FSlateApplication::Get().LocateWindowUnderMouse(Leave, Windows, false, UserIndex);
			if (!TestTrue(TEXT("Reentered pointer can leave through native input"), LeavePath.IsValid() && !LeavePath.ContainsWidget(TriggerControl.Get()))) return false;
			if (bFauxHoverCursor) FSlateApplication::Get().SetCursorPos(Leave);
			const FPointerEvent LeaveMove(UserIndex, FSlateApplication::CursorPointerIndex, Leave, Leave, {}, FKey(), 0, FModifierKeysState());
			FSlateApplication::Get().RoutePointerMoveEvent(LeavePath, LeaveMove, true);
			for (int32 Frame = 0; Frame < 2048 && Instance->IsAnimationPlaying(Animation); ++Frame) Tick(Transition.DurationSeconds * 0.01f);
			Tick(Transition.DurationSeconds * 0.01f);
			for (const auto& Live : LiveObservations)
				TestTrue(TEXT("Reentry exit returns the one host to its original native socket"), Live.Original->GetContent() == Live.Widget && !Live.Presentation->GetContent());
		}

	}
	if (Transition.bFixedViewportLayout)
	{
		const bool SourceWasVisible = VisibleInTree(LayoutSource), DestinationWasVisible = VisibleInTree(LayoutDestination);
		Instance->PlayAnimation(Animation, 0, 1, EUMGSequencePlayMode::Forward, 1.0f, true);
		Tick(Transition.DurationSeconds * 0.25f);
		TestTrue(TEXT("Saved layout animation supports the native playback API"), Instance->IsAnimationPlaying(Animation));
		Instance->StopAnimation(Animation); PaintLayout(); CheckLayoutRestored();
		TestTrue(TEXT("Explicit Stop preserves the current native endpoint selection"), VisibleInTree(LayoutSource) == SourceWasVisible
			&& VisibleInTree(LayoutDestination) == DestinationWasVisible);
	}
	AddInfo(FString::Printf(TEXT("Real playback source=%s trigger=%s type=%s asset=%s"), *SourcePath, *TriggerId, *TriggerType, *AssetPath));
	return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePrototypeRealTimedCloseTest,
	"Design2UMG.Manual.NativePrototypeRealTimedClose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativePrototypeRealTimedCloseTest::RunTest(const FString&)
{
	FString SourcePath, AssetPath, OpenId, OpenType, TimeoutId;
	if (!FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationSource="), SourcePath)
		|| !FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationAsset="), AssetPath)
		|| !FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationTrigger="), OpenId)
		|| !FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationTriggerType="), OpenType)
		|| !FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationTimeoutTrigger="), TimeoutId))
	{
		AddError(TEXT("Real timed close requires source, saved asset, OPEN trigger ID/type and timeout CLOSE trigger ID"));
		return false;
	}
	if (!TestFalse(TEXT("Real source path is absolute"), FPaths::IsRelative(SourcePath))
		|| !TestTrue(TEXT("Saved asset path is explicit"), AssetPath.StartsWith(TEXT("/Game/")) && AssetPath.Contains(TEXT(".")))
		|| !TestNull(TEXT("Playback requires a fresh editor process"), FindObject<UWidgetBlueprint>(nullptr, *AssetPath))) return false;
	FDesignDocument Document;
	FString LoadError;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(SourcePath, Document, LoadError);
	if (!TestTrue(*FString::Printf(TEXT("Load real timed-close source: %s"), *LoadError), bLoaded)) return false;
	FImportAnalysisInput Input;
	Input.bResolveImages = false;
	const auto Analysis = DesignImportPlanning::AnalyzeImport(Document, Input);
	if (!TestTrue(TEXT("Real source converts"), Analysis.ErrorMessage.IsEmpty() && Analysis.Data.ConversionError.IsEmpty())) return false;
	const auto& Navigation = Analysis.Data.PrototypeNavigation();
	const auto FindBinding = [&](const FString& Id, const FString& Type, EUMGPrototypeNavigationStepKind Kind)
		-> const FUMGPrototypeNavigationBinding*
	{
		const FUMGPrototypeNavigationBinding* Found = nullptr;
		for (const auto& Binding : Navigation.ClickBindings)
		{
			if (Binding.TriggerWidgetId != Id || !Binding.TriggerType.Equals(Type, ESearchCase::IgnoreCase)
				|| Binding.Steps.Num() != 1 || Binding.Steps[0].Kind != Kind) continue;
			if (!TestNull(TEXT("Requested real source interaction is unique"), Found)) return nullptr;
			Found = &Binding;
		}
		return Found;
	};
	const auto* Open = FindBinding(OpenId, OpenType, EUMGPrototypeNavigationStepKind::Overlay);
	const auto* Timeout = FindBinding(TimeoutId, TEXT("AFTER_TIMEOUT"), EUMGPrototypeNavigationStepKind::CloseOverlay);
	if (!TestNotNull(TEXT("Real OPEN interaction exists"), Open) || !TestNotNull(TEXT("Real timed CLOSE exists"), Timeout)) return false;
	if (!TestTrue(TEXT("OPEN targets the timed CLOSE source page"), Open->Steps[0].TargetPageId == Timeout->SourcePageId)
		|| !TestTrue(TEXT("OPEN is a source timeout"), Open->UsesPageOpenTrigger())
		|| !TestTrue(TEXT("Source close delay is positive"), Timeout->TriggerDelaySeconds > 0)) return false;
	UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Reload saved blueprint"), Blueprint)
		|| !TestNotNull(TEXT("Saved blueprint is compiled"), Blueprint->GeneratedClass.Get())
		|| !TestNotNull(TEXT("Playback has a world"), World)
		|| !TestTrue(TEXT("Slate is available"), FSlateApplication::IsInitialized())) return false;
	UUserWidget* Instance = CreateWidget<UUserWidget>(World, Blueprint->GeneratedClass.Get());
	if (!TestNotNull(TEXT("Saved generated class instantiates"), Instance)) return false;
	Instance->AddToRoot();
	const FVector2D Size(Analysis.Data.RootWidget.Bounds.W, Analysis.Data.RootWidget.Bounds.H);
	const auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Real timed overlay close")))
		.ClientSize(Size).FocusWhenFirstShown(false).SupportsMaximize(false).SupportsMinimize(false)
		[SNew(SCanvas) + SCanvas::Slot().Position(FVector2D::ZeroVector).Size(Size)[Instance->TakeWidget()]];
	FSlateApplication::Get().AddWindow(Window, true);
	ON_SCOPE_EXIT { FSlateApplication::Get().RequestDestroyWindow(Window); Instance->RemoveFromRoot(); };
	Window->SlatePrepass();
	FSlateApplication::Get().Tick();
	FSlateApplication::Get().ForceRedrawWindow(Window);
	FlushRenderingCommands();
	auto* Host = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UCanvasPanel>(Instance->WidgetTree,
		FUMGWidgetTreeBuilder::SanitizeWidgetName(Navigation.OverlayHostWidgetName, TEXT("CanvasPanel")).ToString());
	if (!TestNotNull(TEXT("Saved native overlay host exists"), Host)) return false;
	UWidget* Page = nullptr;
	const FString PageName = FUMGWidgetTreeBuilder::SanitizeWidgetName(Timeout->SourcePageId, TEXT("CanvasPanel")).ToString();
	for (UWidget* Child : Host->GetAllChildren())
		if (Child && FUMGWidgetTreeBuilder::MatchesWidgetBaseName(Child->GetName(), PageName))
		{
			if (!TestNull(TEXT("Timed overlay page is unique"), Page)) return false;
			Page = Child;
		}
	if (!TestNotNull(TEXT("Timed overlay page exists under its host"), Page)) return false;
	auto* Stack = FindFProperty<FArrayProperty>(Instance->GetClass(), TEXT("__d2u_overlay_stack"));
	if (!TestNotNull(TEXT("Saved native overlay stack exists"), Stack)) return false;
	const auto StackCount = [&]() { return FScriptArrayHelper(Stack, Stack->ContainerPtrToValuePtr<void>(Instance)).Num(); };
	const auto IsOpen = [&]() { return Page->GetVisibility() != ESlateVisibility::Collapsed && Page->GetVisibility() != ESlateVisibility::Hidden; };
	float Elapsed = 0;
	const auto Tick = [&](float Seconds)
	{
		float Remaining = Seconds;
		while (Remaining > KINDA_SMALL_NUMBER)
		{
			const float Step = FMath::Min(Remaining, 0.01f);
			++GFrameCounter;
			UUMGSequenceTickManager::Get(Instance)->OnWidgetTicked(Instance);
			FSlateApplication::Get().OnPreTick().Broadcast(Step);
			World->GetLatentActionManager().BeginFrame();
			World->GetLatentActionManager().ProcessLatentActions(Instance, Step);
			World->GetTimerManager().Tick(Step);
			FSlateApplication::Get().OnPostTick().Broadcast(Step);
			Elapsed += Step;
			Remaining -= Step;
		}
	};
	if (!TestFalse(TEXT("Timed overlay starts closed"), IsOpen())) return false;
	const float OpenLimit = Open->TriggerDelaySeconds + 1.0f;
	while (!IsOpen() && Elapsed < OpenLimit) Tick(0.01f);
	if (!TestTrue(TEXT("Real source OPEN makes the timed overlay visible"), IsOpen())) return false;
	if (!TestEqual(TEXT("Real OPEN pushes one overlay"), StackCount(), 1)
		|| !TestEqual(TEXT("Real OPEN displays its host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible)) return false;
	const float OpenedAt = Elapsed;
	const float Delay = Timeout->TriggerDelaySeconds;
	Tick(Delay * 0.75f);
	if (!TestTrue(TEXT("Overlay remains open before the source close delay"), IsOpen())) return false;
	Tick(Delay * 0.25f + 0.02f);
	if (!TestFalse(TEXT("Source timer closes the opened overlay"), IsOpen())
		|| !TestEqual(TEXT("Source timer removes its overlay from the stack"), StackCount(), 0)
		|| !TestEqual(TEXT("Source timer collapses the empty host"), Host->GetVisibility(), ESlateVisibility::Collapsed)) return false;
	AddInfo(FString::Printf(TEXT("TimedClose source=%s open=%s/%s timeout=%s delay=%.6f openedAt=%.6f observedClosedAt=%.6f"),
		*SourcePath, *OpenId, *OpenType, *TimeoutId, Delay, OpenedAt, Elapsed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePrototypeRealCloseKeyTest,
	"Design2UMG.Manual.NativePrototypeRealCloseKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNativePrototypeRealCloseKeyTest::RunTest(const FString&)
{
	FString SourcePath, AssetPath, TriggerId, KeyName;
	if (!FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationSource="), SourcePath)
		|| !FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationAsset="), AssetPath)
		|| !FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationTrigger="), TriggerId)
		|| !FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationKey="), KeyName))
	{
		AddError(TEXT("Real close-key playback requires source, saved asset, trigger ID and a verified native key name"));
		return false;
	}
	if (!TestFalse(TEXT("Real source path is absolute"), FPaths::IsRelative(SourcePath))
		|| !TestTrue(TEXT("Saved asset path is explicit"), AssetPath.StartsWith(TEXT("/Game/")) && AssetPath.Contains(TEXT(".")))
		|| !TestNull(TEXT("Playback requires a fresh editor process"), FindObject<UWidgetBlueprint>(nullptr, *AssetPath))) return false;
	FDesignDocument Document;
	FString LoadError;
	if (!TestTrue(*FString::Printf(TEXT("Load real source: %s"), *LoadError),
		Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(SourcePath, Document, LoadError))) return false;
	FImportAnalysisInput Input;
	Input.bResolveImages = false;
	const FImportAnalysisResult Analysis = DesignImportPlanning::AnalyzeImport(Document, Input);
	if (!TestTrue(TEXT("Real source analysis succeeds"), Analysis.ErrorMessage.IsEmpty() && Analysis.Data.ConversionError.IsEmpty())) return false;
	for (const auto& Warning : Analysis.Data.Warnings) AddInfo(Warning);
	const auto& Navigation = Analysis.Data.PrototypeNavigation();
	const auto* Binding = Navigation.ClickBindings.FindByPredicate([&TriggerId](const FUMGPrototypeNavigationBinding& Candidate)
	{
		return Candidate.TriggerWidgetId == TriggerId && Candidate.UsesKeyDownTrigger()
			&& Candidate.TriggerDevice == TEXT("KEYBOARD") && Candidate.Steps.Num() == 1
			&& Candidate.Steps[0].Kind == EUMGPrototypeNavigationStepKind::CloseOverlay;
	});
	if (!TestNotNull(TEXT("Real keyboard CLOSE is retained"), Binding)) return false;
	const FKey Key{FName(*KeyName)};
	if (!TestTrue(TEXT("The explicitly supplied native key is valid"), Key.IsValid())) return false;
	UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Reload saved blueprint"), Blueprint)
		|| !TestNotNull(TEXT("Saved blueprint is compiled"), Blueprint->GeneratedClass.Get())
		|| !TestNotNull(TEXT("Playback uses an editor world"), World)
		|| !TestTrue(TEXT("Slate application is available"), FSlateApplication::IsInitialized())) return false;
	bool bGraphUsesKey = false;
	for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph || Graph->GetFName() != TEXT("OnKeyDown")) continue;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const auto* Call = Cast<UK2Node_CallFunction>(Node);
			if (!Call || Call->FunctionReference.GetMemberName() != TEXT("EqualEqual_KeyKey")) continue;
			const UEdGraphPin* KeyPin = Call->FindPin(TEXT("B"), EGPD_Input);
			bGraphUsesKey |= KeyPin && KeyPin->DefaultValue.Equals(KeyName, ESearchCase::IgnoreCase);
		}
	}
	if (!TestTrue(TEXT("The generated key graph uses the requested native key"), bGraphUsesKey)) return false;
	UUserWidget* Instance = CreateWidget<UUserWidget>(World, Blueprint->GeneratedClass.Get());
	if (!TestNotNull(TEXT("Saved generated class instantiates"), Instance)) return false;
	Instance->AddToRoot();
	const auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Real close-key playback")))
		.ClientSize(FVector2D(600, 400)).FocusWhenFirstShown(false).SupportsMaximize(false).SupportsMinimize(false)
		[SNew(SBox)[Instance->TakeWidget()]];
	FSlateApplication::Get().AddWindow(Window, true);
	ON_SCOPE_EXIT { FSlateApplication::Get().RequestDestroyWindow(Window); Instance->RemoveFromRoot(); };
	Window->SlatePrepass();
	FSlateApplication::Get().Tick();
	if (!Navigation.OverlayHostWidgetName.IsEmpty())
	{
		auto* Overlay = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UWidget>(Instance->WidgetTree,
			FUMGWidgetTreeBuilder::SanitizeWidgetName(Navigation.OverlayHostWidgetName, TEXT("Widget")).ToString());
		if (!TestNotNull(TEXT("Real overlay host exists"), Overlay)
			|| !TestTrue(TEXT("No overlay is active at the start of this check"), Overlay->GetVisibility() == ESlateVisibility::Collapsed)) return false;
	}
	TArray<UWidget*> Widgets;
	Instance->WidgetTree->GetAllWidgets(Widgets);
	TArray<ESlateVisibility> Before;
	for (const UWidget* Widget : Widgets) Before.Add(Widget->GetVisibility());
	TSharedPtr<FSlateVirtualUserHandle> UserHandle;
	int32 VirtualIndex = 0;
	do { UserHandle = FSlateApplication::Get().FindOrCreateVirtualUser(VirtualIndex++); } while (!UserHandle.IsUnique());
	const int32 UserIndex = UserHandle->GetUserIndex();
	if (!TestTrue(TEXT("Real widget receives keyboard focus"), FSlateApplication::Get().SetUserFocus(
		UserIndex, Instance->GetCachedWidget(), EFocusCause::SetDirectly))) return false;
	const FKeyEvent Event(Key, FModifierKeysState(), UserIndex, false, 0, 0);
	if (!TestNotNull(TEXT("Generated keyboard override exists"), Instance->GetClass()->FindFunctionByName(TEXT("OnKeyDown")))) return false;
	FSlateApplication::Get().ProcessKeyDownEvent(Event);
	FSlateApplication::Get().Tick();
	for (int32 Index = 0; Index < Widgets.Num(); ++Index)
		TestEqual(TEXT("Closing with no active overlay leaves visibility unchanged"), Widgets[Index]->GetVisibility(), Before[Index]);
	AddInfo(FString::Printf(TEXT("Real no-overlay CLOSE source=%s trigger=%s key=%s"), *SourcePath, *TriggerId, *KeyName));
	return !HasAnyErrors();
}

#endif
