// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGPrototypeGraphWiring.h"
#include "Design2UMGTestUtils.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "UMGGenerator.h"
#include "UMGSchemaSourceExtensions.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
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

	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath);
	if (!TestTrue(TEXT("Generate navigate widget blueprint"), WBP != nullptr))
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
	TestEqual(TEXT("Bound event targets the button widget"), EventNode->ComponentPropertyName.ToString(), TEXT("cta_button"));
	TestEqual(TEXT("Bound event uses the click delegate"), EventNode->DelegatePropertyName.ToString(), TEXT("OnClicked"));

	UK2Node_CallFunction* CallNode = FindExecChainCall(EventNode);
	if (!TestNotNull(TEXT("Event exec chain reaches a call node"), CallNode))
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

	const UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WBP->WidgetTree ? WBP->WidgetTree->RootWidget : nullptr);
	if (!TestNotNull(TEXT("Switcher is the built tree root"), Switcher))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const UK2Node_VariableGet* SwitcherGet = nullptr;
	for (UEdGraphNode* Node : WBP->UbergraphPages[0]->Nodes)
	{
		if (const UK2Node_VariableGet* Get = Cast<UK2Node_VariableGet>(Node))
		{
			SwitcherGet = Get;
			break;
		}
	}
	if (!TestNotNull(TEXT("Switcher variable get authored"), SwitcherGet))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("Variable get targets the switcher variable"),
		SwitcherGet->GetVarName() == Switcher->GetFName());

	const UEdGraphPin* SelfPin = CallNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
	TestTrue(TEXT("Variable get value pin feeds the call target"),
		SelfPin && SwitcherGet->GetValuePin() && SelfPin->LinkedTo.Contains(SwitcherGet->GetValuePin()));

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

	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	if (!TestTrue(TEXT("Build navigate widget blueprint"), WBP != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const FUMGPrototypeNavigationData NavigationData = Data.PrototypeNavigation();
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
	// Exactly the wiring triple carries the marker (engine ghost events don't); the total count also
	// pins the baseline the re-wire below must hold.
	TestEqual(TEXT("Initial wiring authors the marker node triple"), CountMarkerNodes(), 3);
	if (!TestEqual(TEXT("Initial wiring authors one bound event"), CollectBoundEvents(WBP).Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TArray<FString> WiringWarnings;
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(WBP, NavigationData, WiringWarnings);
	TestEqual(TEXT("Fully eligible fixture re-wires without skips"), WiringWarnings.Num(), 0);
	TestEqual(TEXT("Re-wiring keeps the node count stable"), Ubergraph->Nodes.Num(), NodeCountAfterSave);
	TestEqual(TEXT("Re-wiring keeps exactly the marker node triple"), CountMarkerNodes(), 3);
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

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

#endif
