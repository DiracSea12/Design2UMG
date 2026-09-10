// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGPrototypeGraphWiring.h"
#include "Design2UMGTestUtils.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "UMGGenerator.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGSchemaSourceExtensions.h"

#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/ComponentDelegateBinding.h"
#include "Engine/DynamicBlueprintBinding.h"
#include "Engine/MemberReference.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace Design2UMGTestUtils;
using namespace Design2UMGCoreTests;
using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

namespace
{
	const FString NavigatePrototypeJson()
	{
		const FString CtaButtonJson = MakeLeafNode(TEXT("cta_button"), TEXT("CTA"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\",\"transition\":{\"type\":\"DISSOLVE\",\"duration\":120,\"easing\":{\"type\":\"EASE_IN_AND_OUT\"}}}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 48, 120, 32, TEXT("Done"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, CtaButtonJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	const FString SlideNavigatePrototypeJson()
	{
		const FString CtaButtonJson = MakeLeafNode(TEXT("cta_button"), TEXT("CTA"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\",\"transition\":{\"type\":\"PUSH\",\"direction\":\"LEFT\",\"duration\":200,\"easing\":{\"type\":\"EASE_IN_AND_OUT\"}}}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 48, 120, 32, TEXT("Done"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, CtaButtonJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	const FString NavigateNoTransitionJson()
	{
		const FString CtaButtonJson = MakeLeafNode(TEXT("cta_button"), TEXT("CTA"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 48, 120, 32, TEXT("Done"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, CtaButtonJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	const FString OpenLinkPrototypeJson()
	{
		const FString LinkButtonJson = MakeLeafNode(TEXT("link_button"), TEXT("Link"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"OPEN_URL\",\"url\":\"https://example.com/help\"}]}]"),
			*MakeSolidFillJson(0.2, 0.7, 0.5)));
		return MakeFigmaDocJson(
			MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, LinkButtonJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	bool ConvertPrototype(const FString& Json, FDesignDocument& OutDocument, FUMGWidgetData& OutData)
	{
		if (!FFigmaParser::ParseDocument(Json, OutDocument))
		{
			return false;
		}
		OutData = FDesign2UMGConverter::Convert(OutDocument, FDesign2UMGConversionSettings::Defaults());
		return OutData.PrototypeNavigation().ClickBindings.Num() > 0;
	}

	TArray<UK2Node_ComponentBoundEvent*> CollectBoundEvents(const UWidgetBlueprint* WBP)
	{
		TArray<UK2Node_ComponentBoundEvent*> Events;
		if (WBP && WBP->UbergraphPages.Num() > 0 && WBP->UbergraphPages[0])
		{
			for (UEdGraphNode* Node : WBP->UbergraphPages[0]->Nodes)
			{
				if (UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
				{
					Events.Add(Event);
				}
			}
		}
		return Events;
	}

	UK2Node_CallFunction* FindExecChainCall(const UK2Node_ComponentBoundEvent* EventNode)
	{
		if (!EventNode)
		{
			return nullptr;
		}
		const UEdGraphPin* ThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (!ThenPin || ThenPin->LinkedTo.Num() == 0)
		{
			return nullptr;
		}
		return Cast<UK2Node_CallFunction>(ThenPin->LinkedTo[0]->GetOwningNode());
	}

	UK2Node_CallFunction* NextExecCall(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return nullptr;
		}
		const UEdGraphPin* ThenPin = Node->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (!ThenPin || ThenPin->LinkedTo.Num() == 0)
		{
			return nullptr;
		}
		return Cast<UK2Node_CallFunction>(ThenPin->LinkedTo[0]->GetOwningNode());
	}

	// Possessable-bound tracks live on the movie scene's object bindings, not the flat track list.
	UWidgetAnimation* FindGeneratedAnimation(const UWidgetBlueprint* WBP, const TCHAR* AnimationName)
	{
		if (!WBP)
		{
			return nullptr;
		}
		for (UWidgetAnimation* Animation : WBP->Animations)
		{
			if (Animation && Animation->GetFName() == FName(AnimationName))
			{
				return Animation;
			}
		}
		return nullptr;
	}

	TArray<const UMovieSceneTrack*> CollectAnimationTracks(const UWidgetAnimation* Animation)
	{
		TArray<const UMovieSceneTrack*> Tracks;
		if (!Animation || !Animation->MovieScene)
		{
			return Tracks;
		}
		Tracks.Append(Animation->MovieScene->GetTracks());
		for (const FMovieSceneBinding& Binding : Animation->MovieScene->GetBindings())
		{
			for (const UMovieSceneTrack* Track : Binding.GetTracks())
			{
				Tracks.Add(Track);
			}
		}
		return Tracks;
	}

	const FMovieSceneFloatChannel* FindOpacityChannel(const UWidgetAnimation* Animation)
	{
		for (const UMovieSceneTrack* Track : CollectAnimationTracks(Animation))
		{
			const UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
			if (!FloatTrack)
			{
				continue;
			}
			for (UMovieSceneSection* Section : FloatTrack->GetAllSections())
			{
				if (const UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(Section))
				{
					return &FloatSection->GetChannel();
				}
			}
		}
		return nullptr;
	}

	const UMovieScene2DTransformSection* FindTransformSection(const UWidgetAnimation* Animation)
	{
		for (const UMovieSceneTrack* Track : CollectAnimationTracks(Animation))
		{
			const UMovieScene2DTransformTrack* TransformTrack = Cast<UMovieScene2DTransformTrack>(Track);
			if (!TransformTrack)
			{
				continue;
			}
			for (UMovieSceneSection* Section : TransformTrack->GetAllSections())
			{
				if (const UMovieScene2DTransformSection* TransformSection = Cast<UMovieScene2DTransformSection>(Section))
				{
					return TransformSection;
				}
			}
		}
		return nullptr;
	}

	const FUMGWidgetNode* FindIrNodeById(const FUMGWidgetNode& Node, const FString& TargetId)
	{
		if (Node.Id == TargetId)
		{
			return &Node;
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Found = FindIrNodeById(Child, TargetId))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeNavigateGraphWiringTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.Navigate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeNavigateGraphWiringTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate prototype fixture"), ConvertPrototype(NavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringNavigate");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build navigate widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Initial save with transition wiring succeeds"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData)))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Transition animations: the DISSOLVE fade produced one out (1->0) and one in (0->1) animation,
	// possessable-bound to the switcher, each sampled with 5 opacity keys.
	UWidgetAnimation* OutgoingAnimation = FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_out"));
	UWidgetAnimation* IncomingAnimation = FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_in"));
	if (!TestNotNull(TEXT("Outgoing fade animation generated"), OutgoingAnimation)
		|| !TestNotNull(TEXT("Incoming fade animation generated"), IncomingAnimation))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Only the two transition animations exist on the blueprint"), WBP->Animations.Num(), 2);

	const UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WBP->WidgetTree ? WBP->WidgetTree->RootWidget : nullptr);
	if (!TestNotNull(TEXT("Switcher is the built tree root"), Switcher))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("Outgoing animation is possessable-bound to the switcher"),
		OutgoingAnimation->AnimationBindings.Num() == 1
		&& OutgoingAnimation->AnimationBindings[0].WidgetName == Switcher->GetFName());
	TestTrue(TEXT("Incoming animation is possessable-bound to the switcher"),
		IncomingAnimation->AnimationBindings.Num() == 1
		&& IncomingAnimation->AnimationBindings[0].WidgetName == Switcher->GetFName());

	const FMovieSceneFloatChannel* OutgoingOpacity = FindOpacityChannel(OutgoingAnimation);
	const FMovieSceneFloatChannel* IncomingOpacity = FindOpacityChannel(IncomingAnimation);
	if (!TestNotNull(TEXT("Outgoing opacity track exists"), OutgoingOpacity)
		|| !TestNotNull(TEXT("Incoming opacity track exists"), IncomingOpacity))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Outgoing fade carries 5 sampled keys"), OutgoingOpacity->GetNumKeys(), 5);
	TestEqual(TEXT("Incoming fade carries 5 sampled keys"), IncomingOpacity->GetNumKeys(), 5);
	TestEqual(TEXT("Outgoing fade starts fully opaque"), OutgoingOpacity->GetValues()[0].Value, 1.0f);
	TestEqual(TEXT("Outgoing fade ends fully transparent"), OutgoingOpacity->GetValues()[4].Value, 0.0f);
	TestEqual(TEXT("Incoming fade starts fully transparent"), IncomingOpacity->GetValues()[0].Value, 0.0f);
	TestEqual(TEXT("Incoming fade ends fully opaque"), IncomingOpacity->GetValues()[4].Value, 1.0f);

	// Wired chain: bound event -> PlayAnimation(out) -> Delay(phase) -> SetActiveWidgetIndex -> PlayAnimation(in).
	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("Exactly one component-bound event is authored"), Events.Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UK2Node_ComponentBoundEvent* EventNode = Events[0];
	TestEqual(TEXT("Bound event targets the button widget"), EventNode->ComponentPropertyName.ToString(), TEXT("cta_button"));
	TestEqual(TEXT("Bound event uses the click delegate"), EventNode->DelegatePropertyName.ToString(), TEXT("OnClicked"));

	const auto AssertPlayAnimationNode = [&](const UK2Node_CallFunction* PlayNode, const TCHAR* ExpectedAnimationName)
	{
		if (!TestNotNull(TEXT("Chain reaches a PlayAnimation call"), PlayNode))
		{
			return false;
		}
		TestEqual(TEXT("Call target is PlayAnimation"), PlayNode->FunctionReference.GetMemberName().ToString(), TEXT("PlayAnimation"));
		// Self-context call inside the widget's own ubergraph: the member reference stores no parent
		// class (FMemberReference::SetFromField clears it), so prove the engine target instead.
		TestTrue(TEXT("PlayAnimation call is self-context and UUserWidget exposes the function"),
			PlayNode->FunctionReference.IsSelfContext()
			&& UUserWidget::StaticClass()->FindFunctionByName(TEXT("PlayAnimation")) != nullptr);
		const UEdGraphPin* AnimationPin = PlayNode->FindPin(TEXT("InAnimation"), EGPD_Input);
		if (!TestNotNull(TEXT("PlayAnimation has an InAnimation pin"), AnimationPin)
			|| !TestTrue(TEXT("InAnimation pin is linked"), AnimationPin->LinkedTo.Num() == 1))
		{
			return false;
		}
		const UK2Node_VariableGet* AnimationGet = Cast<UK2Node_VariableGet>(AnimationPin->LinkedTo[0]->GetOwningNode());
		return TestTrue(TEXT("Animation variable get references the generated animation property"),
			AnimationGet && AnimationGet->GetVarName() == FName(ExpectedAnimationName));
	};

	UK2Node_CallFunction* OutgoingPlayNode = FindExecChainCall(EventNode);
	if (!AssertPlayAnimationNode(OutgoingPlayNode, TEXT("__d2u_nav_anim_000_out")))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UK2Node_CallFunction* DelayNode = NextExecCall(OutgoingPlayNode);
	if (!TestNotNull(TEXT("Chain reaches the Delay node"), DelayNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Call target is Delay"), DelayNode->FunctionReference.GetMemberName().ToString(), TEXT("Delay"));
	TestTrue(TEXT("Delay resolves on UKismetSystemLibrary"),
		DelayNode->FunctionReference.GetMemberParentClass() == UKismetSystemLibrary::StaticClass());
	const UEdGraphPin* DurationPin = DelayNode->FindPin(TEXT("Duration"), EGPD_Input);
	if (!TestNotNull(TEXT("Delay Duration pin exists"), DurationPin))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Delay Duration equals one transition phase"),
		DurationPin->DefaultValue, FString::SanitizeFloat(FMath::Max(0.12f * 0.5f, 1.0f / 120.0f)));

	UK2Node_CallFunction* CallNode = NextExecCall(DelayNode);
	if (!TestNotNull(TEXT("Chain reaches the page switch call"), CallNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Call target is SetActiveWidgetIndex"), CallNode->FunctionReference.GetMemberName().ToString(), TEXT("SetActiveWidgetIndex"));
	TestTrue(TEXT("SetActiveWidgetIndex resolves on UWidgetSwitcher"),
		CallNode->FunctionReference.GetMemberParentClass() == UWidgetSwitcher::StaticClass());

	const UEdGraphPin* IndexPin = CallNode->FindPin(TEXT("Index"), EGPD_Input);
	if (!TestNotNull(TEXT("Index pin exists"), IndexPin))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Index pin carries the resolved page index"), IndexPin->DefaultValue, TEXT("1"));

	const UEdGraphPin* SelfPin = CallNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
	TestTrue(TEXT("Switcher variable get feeds the call target"),
		SelfPin && SelfPin->LinkedTo.Num() == 1
		&& Cast<UK2Node_VariableGet>(SelfPin->LinkedTo[0]->GetOwningNode())
		&& Cast<UK2Node_VariableGet>(SelfPin->LinkedTo[0]->GetOwningNode())->GetVarName() == Switcher->GetFName());

	if (!AssertPlayAnimationNode(NextExecCall(CallNode), TEXT("__d2u_nav_anim_000_in")))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeOpenLinkGraphWiringTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.OpenLink",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeOpenLinkGraphWiringTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert open-link prototype fixture"), ConvertPrototype(OpenLinkPrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringOpenLink");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath);
	if (!TestTrue(TEXT("Generate open-link widget blueprint"), WBP != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("Exactly one component-bound event is authored"), Events.Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UK2Node_ComponentBoundEvent* EventNode = Events[0];
	TestEqual(TEXT("Bound event targets the link button widget"), EventNode->ComponentPropertyName.ToString(), TEXT("link_button"));
	TestEqual(TEXT("Bound event uses the click delegate"), EventNode->DelegatePropertyName.ToString(), TEXT("OnClicked"));

	UK2Node_CallFunction* CallNode = FindExecChainCall(EventNode);
	if (!TestNotNull(TEXT("Event exec chain reaches a call node"), CallNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Call target is LaunchURL"), CallNode->FunctionReference.GetMemberName().ToString(), TEXT("LaunchURL"));
	TestTrue(TEXT("LaunchURL resolves on UKismetSystemLibrary"),
		CallNode->FunctionReference.GetMemberParentClass() == UKismetSystemLibrary::StaticClass());

	const UEdGraphPin* UrlPin = CallNode->FindPin(TEXT("URL"), EGPD_Input);
	if (!TestNotNull(TEXT("URL pin exists"), UrlPin))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("URL pin carries the literal URL"), UrlPin->DefaultValue, TEXT("https://example.com/help"));

	bool bFoundVariableGet = false;
	bool bFoundSetActiveCall = false;
	for (UEdGraphNode* Node : WBP->UbergraphPages[0]->Nodes)
	{
		bFoundVariableGet |= Cast<UK2Node_VariableGet>(Node) != nullptr;
		if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			bFoundSetActiveCall |= Call->FunctionReference.GetMemberName() == TEXT("SetActiveWidgetIndex");
		}
	}
	TestFalse(TEXT("Open-link wiring authors no variable get"), bFoundVariableGet);
	TestFalse(TEXT("Open-link wiring authors no switcher call"), bFoundSetActiveCall);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeWiringRegistersDelegateBindingTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.DelegateBindingRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeWiringRegistersDelegateBindingTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate prototype fixture"), ConvertPrototype(NavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringBinding");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath);
	if (!TestTrue(TEXT("Generate navigate widget blueprint"), WBP != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	if (!TestNotNull(TEXT("Generated class exists"), GeneratedClass))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UComponentDelegateBinding* ComponentBinding = nullptr;
	for (UDynamicBlueprintBinding* BindingObject : GeneratedClass->DynamicBindingObjects)
	{
		if (UComponentDelegateBinding* Candidate = Cast<UComponentDelegateBinding>(BindingObject))
		{
			ComponentBinding = Candidate;
		}
	}
	if (!TestNotNull(TEXT("Compiled class carries a component delegate binding"), ComponentBinding))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("One bound event node in the graph"), Events.Num(), 1) || !Events[0])
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UK2Node_ComponentBoundEvent* EventNode = Events[0];

	const FBlueprintComponentDelegateBinding* RegisteredBinding = nullptr;
	for (const FBlueprintComponentDelegateBinding& Entry : ComponentBinding->ComponentDelegateBindings)
	{
		if (Entry.ComponentPropertyName == EventNode->ComponentPropertyName)
		{
			RegisteredBinding = &Entry;
		}
	}
	if (!TestNotNull(TEXT("Delegate binding entry registered for the button"), RegisteredBinding))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Registered delegate is OnClicked"), RegisteredBinding->DelegatePropertyName.ToString(), TEXT("OnClicked"));
	TestTrue(TEXT("Registered function matches the authored event"),
		RegisteredBinding->FunctionNameToBind == EventNode->CustomFunctionName);
	TestNotNull(TEXT("Bound function exists on the generated class"),
		GeneratedClass->FindFunctionByName(EventNode->CustomFunctionName));

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeWiringRegenerateIsIdempotentTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.RegenerateIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeWiringRegenerateIsIdempotentTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate prototype fixture"), ConvertPrototype(NavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringIdempotent");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build navigate widget blueprint"), WBP != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	if (!TestTrue(TEXT("Initial save with wiring succeeds"),
		FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData)))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UEdGraph* Ubergraph = WBP->UbergraphPages[0];
	const auto CountMarkerNodes = [Ubergraph]()
	{
		int32 Count = 0;
		for (const UEdGraphNode* Node : Ubergraph->Nodes)
		{
			Count += Node && Node->NodeComment.Equals(Design2UMGPrototypeGraphWiring::NodeMarkerComment, ESearchCase::CaseSensitive) ? 1 : 0;
		}
		return Count;
	};
	const int32 NodeCountAfterSave = Ubergraph->Nodes.Num();
	// The fade transition chain authors the marker octet (event, out get, PlayAnimation, Delay,
	// switcher get, SetActiveWidgetIndex, in get, PlayAnimation); the total count also pins the
	// baseline the regenerate below must hold.
	TestEqual(TEXT("Initial wiring authors the marker transition chain"), CountMarkerNodes(), 8);
	TestEqual(TEXT("Initial wiring authors two transition animations"), WBP->Animations.Num(), 2);
	if (!TestEqual(TEXT("Initial wiring authors one bound event"), CollectBoundEvents(WBP).Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Regenerate: re-run the animation builder (name-prefix purge inside) and the wiring over the
	// same blueprint; neither animations nor nodes may accumulate.
	UMGPrototypeAnimationBuilder::GenerateNavigationAnimations(
		WBP, Data.RootWidget, NavigationData);
	TestEqual(TEXT("Regenerated animations stay at two"), WBP->Animations.Num(), 2);

	TArray<FString> WiringWarnings;
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(WBP, NavigationData, WiringWarnings);
	TestEqual(TEXT("Fully eligible fixture re-wires without skips"), WiringWarnings.Num(), 0);
	TestEqual(TEXT("Re-wiring keeps the node count stable"), Ubergraph->Nodes.Num(), NodeCountAfterSave);
	TestEqual(TEXT("Re-wiring keeps exactly the marker transition chain"), CountMarkerNodes(), 8);
	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("Re-wiring still has exactly one bound event"), Events.Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Re-wired event still targets the button"), Events[0]->ComponentPropertyName.ToString(), TEXT("cta_button"));

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeWiringGraphPurityTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.EngineNodePurity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeWiringGraphPurityTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate prototype fixture"), ConvertPrototype(NavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringPurity");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath);
	if (!TestTrue(TEXT("Generate navigate widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Ubergraph page exists"), WBP->UbergraphPages.Num() > 0 && WBP->UbergraphPages[0] != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	for (UEdGraphNode* Node : WBP->UbergraphPages[0]->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		const UClass* NodeClass = Node->GetClass();
		TestTrue(
			*FString::Printf(TEXT("Node class %s is a native engine class"), *NodeClass->GetName()),
			NodeClass->GetOutermost()->GetName().StartsWith(TEXT("/Script/")));
		// Engine-standard graph content only: the wiring's node kinds plus the engine-config auto-spawned
		// UserWidget ghost events (PreConstruct/Construct/Tick) CreateBlueprint seeds in every WBP.
		TestTrue(
			*FString::Printf(TEXT("Node class %s is an engine-standard node kind"), *NodeClass->GetName()),
			NodeClass == UK2Node_ComponentBoundEvent::StaticClass()
			|| NodeClass == UK2Node_CallFunction::StaticClass()
			|| NodeClass == UK2Node_VariableGet::StaticClass()
			|| NodeClass == UK2Node_Event::StaticClass());
	}

	// Engine-standard animation content only: the animation, its MovieScene and its tracks must all
	// be native engine classes (no plugin UObjects baked into the generated asset).
	for (UWidgetAnimation* Animation : WBP->Animations)
	{
		if (!TestTrue(TEXT("Animation entry is a UWidgetAnimation"), Animation != nullptr))
		{
			continue;
		}
		const UClass* AnimationClass = Animation->GetClass();
		TestTrue(
			*FString::Printf(TEXT("Animation class %s is a native engine class"), *AnimationClass->GetName()),
			AnimationClass == UWidgetAnimation::StaticClass()
			&& AnimationClass->GetOutermost()->GetName().StartsWith(TEXT("/Script/")));
		if (const UMovieScene* MovieScene = Animation->MovieScene)
		{
			TestTrue(TEXT("MovieScene is a native engine class"),
				MovieScene->GetClass()->GetOutermost()->GetName().StartsWith(TEXT("/Script/")));
			for (const UMovieSceneTrack* Track : CollectAnimationTracks(Animation))
			{
				TestTrue(
					*FString::Printf(TEXT("Track class %s is a native engine class"), *Track->GetClass()->GetName()),
					Track && Track->GetClass()->GetOutermost()->GetName().StartsWith(TEXT("/Script/")));
			}
		}
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeNavigateSlideTransitionTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.SlideTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeNavigateSlideTransitionTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert slide navigate prototype fixture"), ConvertPrototype(SlideNavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringSlide");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build slide navigate widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save with slide transition wiring succeeds"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData)))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetAnimation* OutgoingAnimation = FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_out"));
	UWidgetAnimation* IncomingAnimation = FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_in"));
	if (!TestNotNull(TEXT("Outgoing slide animation generated"), OutgoingAnimation)
		|| !TestNotNull(TEXT("Incoming slide animation generated"), IncomingAnimation))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// PUSH is slide-only: no opacity track, one 2D transform track with a translation-only mask.
	TestNull(TEXT("Outgoing slide has no opacity track"), FindOpacityChannel(OutgoingAnimation));
	TestNull(TEXT("Incoming slide has no opacity track"), FindOpacityChannel(IncomingAnimation));

	const UMovieScene2DTransformSection* OutgoingTransform = FindTransformSection(OutgoingAnimation);
	const UMovieScene2DTransformSection* IncomingTransform = FindTransformSection(IncomingAnimation);
	if (!TestNotNull(TEXT("Outgoing transform section exists"), OutgoingTransform)
		|| !TestNotNull(TEXT("Incoming transform section exists"), IncomingTransform))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	for (const UMovieScene2DTransformSection* Section : { OutgoingTransform, IncomingTransform })
	{
		if (!Section)
		{
			continue;
		}
		const EMovieScene2DTransformChannel Channels = Section->GetMask().GetChannels();
		TestTrue(TEXT("Slide masks translation only"),
			EnumHasAllFlags(Channels, EMovieScene2DTransformChannel::Translation)
			&& !EnumHasAnyFlags(Channels, EMovieScene2DTransformChannel::Rotation)
			&& !EnumHasAnyFlags(Channels, EMovieScene2DTransformChannel::Scale));
	}

	// Direction contract for a LEFT PUSH (bidirectional): the switcher (360px wide here, read from
	// the IR switcher node bounds) slides out to +W and in from -W.
	const FUMGWidgetNode* SwitcherNode = FindIrNodeById(
		Data.RootWidget, NavigationData.SwitcherWidgetId);
	if (!TestNotNull(TEXT("IR switcher node found"), SwitcherNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const float SlideWidth = SwitcherNode->Bounds.W > KINDA_SMALL_NUMBER ? SwitcherNode->Bounds.W : 120.0f;

	TestEqual(TEXT("Outgoing slide starts at the origin"), OutgoingTransform->Translation[0].GetValues()[0].Value, 0.0f);
	TestEqual(TEXT("Outgoing slide ends at +width (LEFT push out)"), OutgoingTransform->Translation[0].GetValues()[4].Value, SlideWidth);
	TestEqual(TEXT("Incoming slide starts at -width (LEFT push in)"), IncomingTransform->Translation[0].GetValues()[0].Value, -SlideWidth);
	TestEqual(TEXT("Incoming slide ends at the origin"), IncomingTransform->Translation[0].GetValues()[4].Value, 0.0f);
	TestEqual(TEXT("Slide stays on the horizontal axis"), OutgoingTransform->Translation[1].GetValues()[4].Value, 0.0f);
	TestEqual(TEXT("Outgoing slide carries 5 sampled keys"), OutgoingTransform->Translation[0].GetNumKeys(), 5);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeNavigateNoTransitionDirectWiringTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.NavigateNoTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeNavigateNoTransitionDirectWiringTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert no-transition navigate fixture"), ConvertPrototype(NavigateNoTransitionJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringNoTransition");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build no-transition widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save with direct wiring succeeds"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData)))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// No transition: no animations are authored and the binding keeps the Phase 1 direct wiring
	// (bound event -> SetActiveWidgetIndex, nothing in between).
	TestEqual(TEXT("No transition animations are generated"), WBP->Animations.Num(), 0);
	TestFalse(TEXT("No transition animation names are written into the IR"),
		NavigationData.ClickBindings[0].Transition.HasGeneratedAnimations());

	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("Exactly one component-bound event is authored"), Events.Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	UK2Node_CallFunction* CallNode = FindExecChainCall(Events[0]);
	if (!TestNotNull(TEXT("Event exec chain reaches a call node"), CallNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Direct call target is SetActiveWidgetIndex"), CallNode->FunctionReference.GetMemberName().ToString(), TEXT("SetActiveWidgetIndex"));
	TestNull(TEXT("No Delay node sits in the chain"), NextExecCall(CallNode));

	int32 MarkerNodeCount = 0;
	for (const UEdGraphNode* Node : WBP->UbergraphPages[0]->Nodes)
	{
		MarkerNodeCount += Node && Node->NodeComment.Equals(
			Design2UMGPrototypeGraphWiring::NodeMarkerComment, ESearchCase::CaseSensitive) ? 1 : 0;
	}
	TestEqual(TEXT("Direct wiring authors the Phase 1 marker triple"), MarkerNodeCount, 3);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

#endif
