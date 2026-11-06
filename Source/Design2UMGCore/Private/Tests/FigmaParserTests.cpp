// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "FigmaParser.h"
#include "DesignReferenceExpansion.h"
#include "DesignTreeLimits.h"
#include "DesignSchema.h"
#include "UMGWidgetTypes.h"
#include "Design2UMGConverter.h"
#include "SizeEstimator.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "FigmaTestDocBuilder.h"
#include "VisualMapper.h"
#include "LayoutMapper.h"
#include "VisualMapperFillTransfer.h"
#include "PathWidgetBuilder.h"
#include "Design2UMGConverterVisualSupport.h"
#include "DesignSchemaSourceExtensions.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGTextPaintOwner.h"
#include "Design2UMGPrototypeStepGraphAssembly.h"
#include "Design2UMGPrototypeNavigation.h"
#include "Design2UMGGeneratedNameSuffixes.h"

using namespace Design2UMGCoreTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaParserEmptyPrototypeActionTest,
	"Design2UMG.Unit.FigmaParser.EmptyPrototypeAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserEmptyPrototypeActionTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"document":{"id":"root","type":"FRAME","interactions":[
		{"trigger":null,"actions":[{"type":"NODE","navigation":"CHANGE_TO","destinationId":"absent"}]},
		{"trigger":{"type":"ON_CLICK"},"actions":[null]},
		{"trigger":{"type":"ON_CLICK"},"actions":[null,{"type":"NODE","navigation":"CHANGE_TO","destinationId":"state"}]},
		{"trigger":{"type":"MOUSE_DOWN"},"actions":[{"type":"URL","url":"https://example.com"}]},
		{"trigger":{"type":"AFTER_TIMEOUT","timeout":0.9750000238418579},"actions":[{"type":"NODE","navigation":"OVERLAY","destinationId":"overlay"}]},
		{"trigger":{"type":"ON_KEY_DOWN","device":"KEYBOARD","keyCodes":[27]},"actions":[{"type":"CLOSE"}]},
		{"trigger":{"type":"ON_KEY_DOWN","device":"XBOX_ONE","keyCodes":[27]},"actions":[{"type":"CLOSE"}]}
	]}})JSON");
	FDesignDocument Document;
	if (!TestTrue(TEXT("Prototype fixture parses"), FFigmaParser::ParseDocument(Json, Document))
		|| !TestEqual(TEXT("One runtime root"), Document.Children.Num(), 1)) return false;
	const TSharedPtr<FDesignNode>& Root = Document.Children[0];
	const auto& Interactions = Root->Source().Prototype.Interactions;
	if (!TestEqual(TEXT("Null trigger is inactive"), Interactions.Num(), 6)) return false;
	TestEqual(TEXT("Null action is not synthesized"), Interactions[0].Actions.Num(), 0);
	TestEqual(TEXT("Real action beside null survives"), Interactions[1].Actions.Num(), 1);
	TestTrue(TEXT("Timeout source value remains serialized"), Interactions[3].TriggerJson.Contains(TEXT("0.9750000238418579")));
	TestTrue(TEXT("CLOSE retains typed overlay intent"), Interactions[4].Actions.Num() == 1
		&& Interactions[4].Actions[0].Intent == EDesignPrototypeActionIntent::CloseOverlay);
	TestEqual(TEXT("Keyboard device is preserved"), Interactions[4].TriggerDevice, FString(TEXT("KEYBOARD")));
	TestTrue(TEXT("Numeric Figma key code is preserved"), Interactions[4].TriggerKeyCodes.Num() == 1
		&& Interactions[4].TriggerKeyCodes[0] == TEXT("27"));
	TArray<FUMGPrototypeNavigationBinding> Bindings;
	TSet<FString> TriggerIds;
	TSet<FString> Seen;
	TArray<FString> Warnings;
	Design2UMGPrototypeStepGraphAssembly::GatherNavigationBindings(Root, {}, {}, Bindings, TriggerIds, Seen, Warnings);
	TestTrue(TEXT("State action retains its real destination for animation validation"), Bindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.Steps.Num() == 1
			&& Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::AnimateWidget && Binding.Steps[0].TargetPageId == TEXT("state"); }));
	TestTrue(TEXT("Close overlay stays distinct from back"), Bindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.UsesKeyDownTrigger()
			&& Binding.PrimaryStep().Kind == EUMGPrototypeNavigationStepKind::CloseOverlay && Binding.Steps.Num() == 1
			&& Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::CloseOverlay; }));
	TestTrue(TEXT("Controller key is explicitly rejected"), Warnings.ContainsByPredicate(
		[](const FString& Warning) { return Warning.Contains(TEXT("XBOX_ONE")) && Warning.Contains(TEXT("action not executed")); }));
	TestTrue(TEXT("Mouse trigger diagnostic remains"), Warnings.ContainsByPredicate(
		[](const FString& Warning) { return Warning.Contains(TEXT("MOUSE_DOWN")); }));
	TestFalse(TEXT("Timer is not mislabeled as an unknown trigger"), Warnings.ContainsByPredicate(
		[](const FString& Warning) { return Warning.Contains(TEXT("trigger 'AFTER_TIMEOUT'")); }));
	TestFalse(TEXT("Null action produces no NONE diagnostic"), Warnings.ContainsByPredicate(
		[](const FString& Warning) { return Warning.Contains(TEXT("'NONE'")); }));
	const FString HiddenAncestor = TEXT(R"JSON({"document":{"id":"page","type":"FRAME","children":[
		{"id":"hidden","type":"FRAME","visible":false,"children":[{"id":"hidden_instance","type":"INSTANCE","componentId":"base","interactions":[{"trigger":{"type":"ON_HOVER"},"actions":[{"type":"NODE","navigation":"CHANGE_TO","destinationId":"state","transition":null}]}]}]},
		{"id":"visible_instance","type":"INSTANCE","componentId":"base","interactions":[{"trigger":{"type":"ON_HOVER"},"actions":[{"type":"NODE","navigation":"CHANGE_TO","destinationId":"state","transition":null}]}]}
	]}})JSON");
	if (!TestTrue(TEXT("Hidden ancestor fixture parses"), FFigmaParser::ParseDocument(HiddenAncestor, Document))) return false;
	Bindings.Reset(); TriggerIds.Reset(); Seen.Reset(); Warnings.Reset();
	Design2UMGPrototypeStepGraphAssembly::GatherNavigationBindings(Document.Children[0], {}, {}, Bindings, TriggerIds, Seen, Warnings);
	if (TestEqual(TEXT("Only the visible sibling contributes an interaction"), Bindings.Num(), 1))
	{
		TestEqual(TEXT("Visible sibling identity is preserved"), Bindings[0].TriggerWidgetId, FString(TEXT("visible_instance")));
		TestTrue(TEXT("Visible sibling retains its instant state action"), Bindings[0].Steps.Num() == 1
			&& Bindings[0].Steps[0].Kind == EUMGPrototypeNavigationStepKind::AnimateWidget
			&& Bindings[0].Steps[0].TargetPageId == TEXT("state") && Bindings[0].Steps[0].Transition.Type.IsEmpty());
	}
	TestFalse(TEXT("Hidden descendant is not an input trigger"), TriggerIds.Contains(TEXT("hidden_instance")));
	return true;
}

namespace
{
	static const TCHAR* FigmaPluginName = TEXT("Design2UMG");
	static const FString FigmaFixtureBase = TEXT("Source/Design2UMGCore/Tests/Fixtures/figma");

	bool LoadFigmaFixture(FAutomationTestBase& Test, const FString& FileName, FDesignDocument& OutDoc)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(FigmaPluginName);
		if (!Plugin.IsValid())
		{
			Test.AddError(TEXT("Design2UMG plugin not found"));
			return false;
		}
		const FString Path = FPaths::Combine(Plugin->GetBaseDir(), FigmaFixtureBase, FileName);
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			Test.AddError(FString::Printf(TEXT("Failed to load fixture: %s"), *Path));
			return false;
		}
		return FFigmaParser::ParseDocument(Json, OutDoc);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaAnimationLayerOrderContractTest,
	"Design2UMG.Unit.FigmaParser.AnimationLayerOrderContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaAnimationLayerOrderContractTest::RunTest(const FString&)
{
    TFunction<const FUMGWidgetNode*(const FUMGWidgetNode&, const FString&)> FindPresence = [&](const FUMGWidgetNode& Node, const FString& Id) -> const FUMGWidgetNode*
    {
        if (Node.Id == Id) return &Node;
        for (const auto& Child : Node.Children) if (const auto* Found = FindPresence(Child, Id)) return Found;
        return nullptr;
    };

	const FString Fill = TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":0,\"b\":0,\"a\":1}}]");
	const auto Leaf = [&Fill](const TCHAR* Id, const TCHAR* Name)
	{
		return MakeLeafNode(Id, Name, TEXT("RECTANGLE"), 0, 0, 20, 20, Fill);
	};
	const FString SourceChildren = Leaf(TEXT("source_a"), TEXT("A")) + TEXT(",") + Leaf(TEXT("source_b"), TEXT("B"));
	const FString TargetChildren = Leaf(TEXT("target_b"), TEXT("B")) + TEXT(",") + Leaf(TEXT("target_a"), TEXT("A"));
	const FString Action = TEXT(",\"componentId\":\"base\",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"CHANGE_TO\",\"destinationId\":\"target\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":200,\"easing\":{\"type\":\"LINEAR\"}}}]}]");
	const FString Source = MakeContainerNode(TEXT("instance"), TEXT("Instance"), TEXT("INSTANCE"), 0, 0, 40, 40, SourceChildren, Action);
	const FString Target = MakeContainerNode(TEXT("target"), TEXT("Variant"), TEXT("COMPONENT"), 0, 0, 40, 40, TargetChildren);
	const FString Extra = FString::Printf(TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\",\"design2umgComponentDependencies\":{\"schemaVersion\":1,\"definitions\":[{\"id\":\"target\",\"key\":\"target\",\"document\":%s}],\"instances\":{},\"errors\":[]}"), *Target);
	FDesignDocument Document;
	if (!TestTrue(TEXT("Reordered variant parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Source, Extra), Document))) return false;
	const auto Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("One state binding"), Data.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	const auto& Transition = Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
	if (!TestEqual(TEXT("Both child paint orders are retained"), Transition.LayerOrders.Num(), 2)) return false;
	TestEqual(TEXT("First child keeps its source identity"), Transition.LayerOrders[0].NodeId, FString(TEXT("source_a")));
	TestEqual(TEXT("First child maps to destination identity"), Transition.LayerOrders[0].DestinationNodeId, FString(TEXT("instance::target_a")));
	const auto* TargetStateTree = FindPresence(Data.RootWidget, Transition.TargetStateWidgetId);
	TestTrue(TEXT("Destination identity belongs to the actual instance target state"), TargetStateTree
		&& FindPresence(*TargetStateTree, Transition.LayerOrders[0].DestinationNodeId));
	TestEqual(TEXT("First child changes paint index"), Transition.LayerOrders[0].EndIndex, 1);
	TestEqual(TEXT("Pure reorder has no property-animation targets"), Transition.WidgetTargets.Num(), 0);
	TestEqual(TEXT("Pure reorder retains the source state"), Transition.SourceStateId, FString(TEXT("base")));
	TestEqual(TEXT("Pure reorder retains the target state"), Transition.TargetStateId, FString(TEXT("target")));
	TestEqual(TEXT("The second child moves to the first paint position"), Transition.LayerOrders[1].EndIndex, 0);
	const auto SharedInstance = [&](const TCHAR* Id, const TCHAR* First, const TCHAR* Second, bool bChanged = false)
	{
		return MakeContainerNode(Id, TEXT("Shared icon"), TEXT("INSTANCE"), 0, 0, 40, 40,
			MakeLeafNode(First, TEXT("Vector"), TEXT("RECTANGLE"), 0, 0, 20, 20,
				Fill + FString::Printf(TEXT(",\"opacity\":%g"), bChanged ? 0.25 : 1.0)) + TEXT(",")
			+ MakeLeafNode(Second, TEXT("Vector"), TEXT("RECTANGLE"), 0, 0, 20, 20,
				Fill + FString::Printf(TEXT(",\"opacity\":%g"), bChanged ? 0.75 : 1.0)), TEXT(",\"componentId\":\"shared_icon\""));
	};
	const FString RepeatedSource = MakeContainerNode(TEXT("instance"), TEXT("Instance"), TEXT("INSTANCE"), 0, 0, 40, 40,
		SharedInstance(TEXT("nested_source"), TEXT("Iouter_source;nested_source;shape_a"), TEXT("Iouter_source;nested_source;shape_b")), Action);
	const FString RepeatedTarget = MakeContainerNode(TEXT("target"), TEXT("Variant"), TEXT("COMPONENT"), 0, 0, 40, 40,
		SharedInstance(TEXT("nested_target"), TEXT("Inested_target;shape_b"), TEXT("Inested_target;shape_a"), true));
	const FString RepeatedExtra = FString::Printf(TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\",\"design2umgComponentDependencies\":{\"schemaVersion\":1,\"definitions\":[{\"id\":\"target\",\"key\":\"target\",\"document\":%s}],\"instances\":{},\"errors\":[]}"), *RepeatedTarget);
	if (!TestTrue(TEXT("Repeated component child names parse"), FFigmaParser::ParseDocument(MakeFigmaDocJson(RepeatedSource, RepeatedExtra), Document))) return false;
	const auto RepeatedData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Repeated names retain the state interaction"), RepeatedData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	const auto& RepeatedTransition = RepeatedData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
	TestEqual(TEXT("Swapped origin IDs do not reorder equally named sibling positions"), RepeatedTransition.LayerOrders.Num(), 0);
	if (!TestEqual(TEXT("Both indexed child opacity changes are retained"), RepeatedTransition.WidgetTargets.Num(), 2)) return false;
	TestTrue(TEXT("First duplicate matches the first destination despite swapped component origin IDs"),
		RepeatedTransition.WidgetTargets[0].NodeId.EndsWith(TEXT("shape_a"))
		&& RepeatedTransition.WidgetTargets[0].DestinationNodeId.EndsWith(TEXT("shape_b"))
		&& FMath::IsNearlyEqual(RepeatedTransition.WidgetTargets[0].EndOpacity, 0.25f));
	TestTrue(TEXT("Second duplicate matches the second destination"),
		RepeatedTransition.WidgetTargets[1].NodeId.EndsWith(TEXT("shape_b"))
		&& RepeatedTransition.WidgetTargets[1].DestinationNodeId.EndsWith(TEXT("shape_a"))
		&& FMath::IsNearlyEqual(RepeatedTransition.WidgetTargets[1].EndOpacity, 0.75f));
	const FString IndependentIdsExtra = RepeatedExtra.Replace(TEXT("Inested_target;shape_b"), TEXT("unrelated_b"))
		.Replace(TEXT("Inested_target;shape_a"), TEXT("unrelated_a"));
	if (!TestTrue(TEXT("Repeated names with independent IDs parse"), FFigmaParser::ParseDocument(MakeFigmaDocJson(RepeatedSource, IndependentIdsExtra), Document))) return false;
	const auto IndependentIdsData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Parent index pairs repeated names without component lineage"), IndependentIdsData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	const auto& IndependentTransition = IndependentIdsData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
	if (!TestEqual(TEXT("Independent IDs retain both real property changes"), IndependentTransition.WidgetTargets.Num(), 2)) return false;
	TestTrue(TEXT("Independent child identities follow their original parent positions"),
		IndependentTransition.WidgetTargets[0].DestinationNodeId.EndsWith(TEXT("unrelated_b"))
		&& IndependentTransition.WidgetTargets[1].DestinationNodeId.EndsWith(TEXT("unrelated_a")));
	TestTrue(TEXT("Pure reorder retains its native instance host"), !Transition.StateHostWidgetId.IsEmpty());

	const FString Absolute = MakeLeafNode(TEXT("target_b"), TEXT("B"), TEXT("RECTANGLE"), 0, 0, 20, 20,
		Fill + TEXT(",\"layoutPositioning\":\"ABSOLUTE\""));
	const FString SourceWithFlow = MakeContainerNode(TEXT("instance"), TEXT("Instance"), TEXT("INSTANCE"), 0, 0, 40, 40,
		Leaf(TEXT("source_a"), TEXT("A")), Action + TEXT(",\"layoutMode\":\"VERTICAL\""));
	const FString TargetWithAbsolute = MakeContainerNode(TEXT("target"), TEXT("Variant"), TEXT("COMPONENT"), 0, 0, 40, 40,
		Absolute + TEXT(",") + Leaf(TEXT("target_a"), TEXT("A")), TEXT(",\"layoutMode\":\"VERTICAL\""));
	const FString AbsoluteExtra = FString::Printf(TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\",\"design2umgComponentDependencies\":{\"schemaVersion\":1,\"definitions\":[{\"id\":\"target\",\"key\":\"target\",\"document\":%s}],\"instances\":{},\"errors\":[]}"), *TargetWithAbsolute);
	if (!TestTrue(TEXT("Absolute layer variant parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(SourceWithFlow, AbsoluteExtra), Document))) return false;
	const auto AbsoluteData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Absolute layer state binding"), AbsoluteData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	const auto& AbsoluteTransition = AbsoluteData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
	if (!TestEqual(TEXT("One target-only source subtree"), AbsoluteTransition.AddedLayers.Num(), 1)) return false;
	const FString AddedId = AbsoluteTransition.AddedLayers[0].NodeId;
	TestEqual(TEXT("Added layer is scoped to the source instance"), AddedId, AbsoluteTransition.LayerScopeId() + TEXT("::") + AbsoluteTransition.AddedLayers[0].DestinationNodeId);
	TestEqual(TEXT("Added layer identifies its actual instance destination"),
		AbsoluteTransition.AddedLayers[0].DestinationNodeId, FString(TEXT("instance::target_b")));
	TFunction<bool(const FUMGWidgetNode&)> HasAdded = [&](const FUMGWidgetNode& Node)
	{
		if (Node.Id == AddedId) return true;
		for (const auto& Child : Node.Children) if (HasAdded(Child)) return true;
		return false;
	};
	TestTrue(TEXT("Target-only layer is a real converted widget"), HasAdded(AbsoluteData.RootWidget));

    const FString PresenceSource = MakeContainerNode(TEXT("instance"), TEXT("Instance"), TEXT("INSTANCE"), 0, 0, 40, 40,
        MakeLeafNode(TEXT("hidden_source"), TEXT("Repeated"), TEXT("RECTANGLE"), 0, 0, 20, 20, Fill + TEXT(",\"visible\":false"))
        + TEXT(",") + Leaf(TEXT("outgoing_source"), TEXT("Repeated"))
        + TEXT(",") + Leaf(TEXT("paired_source"), TEXT("Repeated")), Action);
    const FString PresenceTarget = MakeContainerNode(TEXT("target"), TEXT("Variant"), TEXT("COMPONENT"), 0, 0, 40, 40,
        Leaf(TEXT("incoming_target"), TEXT("Repeated")) + TEXT(",")
        + MakeLeafNode(TEXT("hidden_target"), TEXT("Repeated"), TEXT("RECTANGLE"), 0, 0, 20, 20, Fill + TEXT(",\"visible\":false"))
        + TEXT(",") + MakeLeafNode(TEXT("paired_target"), TEXT("Repeated"), TEXT("RECTANGLE"), 0, 0, 20, 20, Fill + TEXT(",\"opacity\":0.5")));
    const FString PresenceExtra = Extra.Replace(*Target, *PresenceTarget);
    if (!TestTrue(TEXT("Bidirectional layer visibility fixture parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(PresenceSource, PresenceExtra), Document))) return false;
    const auto PresenceData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    if (!TestEqual(TEXT("Visibility changes retain their state binding"), PresenceData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
    const auto& Presence = PresenceData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
    if (!TestEqual(TEXT("Hidden-to-visible creates one native target layer"), Presence.AddedLayers.Num(), 1)) return false;
    TestEqual(TEXT("Incoming layer retains its original duplicate-sibling index"), Presence.AddedLayers[0].DestinationNodeId, FString(TEXT("instance::incoming_target")));
    const auto* PresenceTargetTree = FindPresence(PresenceData.RootWidget, Presence.TargetStateWidgetId);
    TestTrue(TEXT("Incoming destination exists under its actual instance target state"), PresenceTargetTree
        && FindPresence(*PresenceTargetTree, Presence.AddedLayers[0].DestinationNodeId));
    TestTrue(TEXT("Visible-to-hidden fades the existing source"), Presence.WidgetTargets.ContainsByPredicate(
        [](const FUMGPrototypeWidgetAnimationTarget& Item)
        { return Item.NodeId == TEXT("outgoing_source") && !Item.bEndPresent && Item.bOpacity && Item.EndOpacity == 0; }));
    TestTrue(TEXT("Unchanged presence pairs the third original sibling"), Presence.WidgetTargets.ContainsByPredicate(
        [](const FUMGPrototypeWidgetAnimationTarget& Item)
        { return Item.NodeId == TEXT("paired_source") && Item.DestinationNodeId == TEXT("instance::paired_target") && FMath::IsNearlyEqual(Item.EndOpacity, 0.5f); }));
    TestNull(TEXT("Hidden source is not assigned a fake runtime widget"), FindPresence(PresenceData.RootWidget, TEXT("hidden_source")));
    const auto* Incoming = FindPresence(PresenceData.RootWidget, Presence.AddedLayers[0].NodeId);
    if (TestNotNull(TEXT("Incoming visible layer has a real converted native widget"), Incoming))
        TestTrue(TEXT("Incoming layer begins transparent"), FMath::IsNearlyZero(Incoming->Render.RenderOpacity.Get(1.f)));

    const FString OverriddenSource = PresenceSource.Replace(TEXT("\"id\":\"hidden_source\""),
        TEXT("\"id\":\"hidden_source\",\"componentPropertyReferences\":{\"visible\":\"ShowLayer\"}"))
        .Replace(TEXT("\"componentId\":\"base\""), TEXT("\"componentId\":\"base\",\"componentProperties\":{\"ShowLayer\":{\"type\":\"BOOLEAN\",\"value\":false}}"));
    const FString OverriddenTarget = PresenceTarget.Replace(TEXT("\"id\":\"incoming_target\""),
        TEXT("\"id\":\"incoming_target\",\"componentPropertyReferences\":{\"visible\":\"ShowLayer\"}"));
    if (!TestTrue(TEXT("Instance Boolean visibility fixture parses"), FFigmaParser::ParseDocument(
        MakeFigmaDocJson(OverriddenSource, Extra.Replace(*Target, *OverriddenTarget)), Document))) return false;
    const auto OverriddenData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    if (!TestEqual(TEXT("Overridden visibility retains the other real changes"), OverriddenData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
    TestEqual(TEXT("A layer hidden by the actual target instance does not become an added widget"),
        OverriddenData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.AddedLayers.Num(), 0);

	TFunction<const FUMGWidgetNode*(const FUMGWidgetNode&, const FString&, int32&)> FindPaintPlacement =
		[&](const FUMGWidgetNode& Node, const FString& Id, int32& Rank) -> const FUMGWidgetNode*
	{
		for (const auto& Child : Node.Children) if (const auto* Found = FindPaintPlacement(Child, Id, Rank)) return Found;
		if (Node.Type == UMGWidgetTypes::CanvasPanel)
			for (int32 Index = 0; Index < Node.Children.Num(); ++Index)
				if (FindPresence(Node.Children[Index], Id)) { Rank = Node.Children[Index].Slot.ZOrder; return &Node.Children[Index]; }
		return nullptr;
	};
	const FString SourceOrderChildren = MakeLeafNode(TEXT("source_a"), TEXT("A"), TEXT("RECTANGLE"), 2, 4, 12, 10, Fill)
		+ TEXT(",") + MakeLeafNode(TEXT("source_b"), TEXT("B"), TEXT("RECTANGLE"), 16, 4, 12, 10, Fill);
	const FString TargetOrderChildren = MakeLeafNode(TEXT("target_a"), TEXT("A"), TEXT("RECTANGLE"), 2, 4, 12, 10, Fill)
		+ TEXT(",") + MakeLeafNode(TEXT("target_b"), TEXT("B"), TEXT("RECTANGLE"), 16, 4, 12, 10, Fill);
	const FString OrderLayout = Fill + TEXT(",\"layoutMode\":\"HORIZONTAL\",\"itemSpacing\":2,\"paddingLeft\":2,\"paddingTop\":4");
	const FString ReverseTarget = MakeContainerNode(TEXT("target"), TEXT("Variant"), TEXT("COMPONENT"), 0, 0, 40, 40,
		TargetOrderChildren, OrderLayout + TEXT(",\"itemReverseZIndex\":true"));
	const FString ForwardSource = MakeContainerNode(TEXT("instance"), TEXT("Instance"), TEXT("INSTANCE"), 0, 0, 40, 40,
		SourceOrderChildren, Action + OrderLayout + TEXT(",\"itemReverseZIndex\":false"));
	if (!TestTrue(TEXT("Paint reversal without sibling reordering parses"), FFigmaParser::ParseDocument(
		MakeFigmaDocJson(ForwardSource, Extra.Replace(*Target, *ReverseTarget)), Document))) return false;
	TestEqual(TEXT("Source sibling identity remains at its original layout index"), Document.Children[0]->Children[0]->Id, FString(TEXT("source_a")));
	TestTrue(TEXT("Flow retains source position without claiming runtime placement"), Document.Children[0]->Children[0]->Transform.SourceLayoutPosition.IsSet()
		&& Document.Children[0]->Children[0]->Transform.SourceLayoutPosition.GetValue().Equals(FVector2D(2, 4))
		&& Document.Children[0]->Children[0]->Transform.X == 0 && Document.Children[0]->Children[0]->Transform.Y == 0
		&& !Document.Children[0]->Children[0]->Transform.ExplicitX.IsSet() && !Document.Children[0]->Children[0]->Transform.ExplicitY.IsSet());

	const auto ReverseData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Paint reversal converts"), ReverseData.ConversionError, FString())
		|| !TestEqual(TEXT("Paint reversal retains its source action"), ReverseData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	const auto& ReverseOrder = ReverseData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
	if (!TestEqual(TEXT("Both changed source paint ranks are retained"), ReverseOrder.LayerOrders.Num(), 2)) return false;
	TestTrue(TEXT("Paint rank changes preserve matching identities and fixed viewport layout"), ReverseOrder.bFixedViewportLayout
		&& ReverseOrder.LayerOrders[0].NodeId == TEXT("source_a") && ReverseOrder.LayerOrders[0].DestinationNodeId == TEXT("instance::target_a")
		&& ReverseOrder.LayerOrders[0].StartIndex == 0 && ReverseOrder.LayerOrders[0].EndIndex == 1
		&& ReverseOrder.LayerOrders[1].StartIndex == 1 && ReverseOrder.LayerOrders[1].EndIndex == 0);
	int32 SourceRankA = INDEX_NONE, SourceRankB = INDEX_NONE, TargetRankA = INDEX_NONE, TargetRankB = INDEX_NONE;
	const auto* SourcePlacementA = FindPaintPlacement(ReverseData.RootWidget, TEXT("source_a"), SourceRankA);
	const auto* SourcePlacementB = FindPaintPlacement(ReverseData.RootWidget, TEXT("source_b"), SourceRankB);
	const auto* TargetPlacementA = FindPaintPlacement(ReverseData.RootWidget, TEXT("instance::target_a"), TargetRankA);
	const auto* TargetPlacementB = FindPaintPlacement(ReverseData.RootWidget, TEXT("instance::target_b"), TargetRankB);
	TestTrue(TEXT("Static native source and target Canvas order agrees with the animation ranks"), SourcePlacementA && SourcePlacementB
		&& TargetPlacementA && TargetPlacementB && SourceRankA == 0 && SourceRankB == 1 && TargetRankA == 1 && TargetRankB == 0);
	const auto PlacementContext = [](const FUMGWidgetNode* Placement)
	{
		return Placement ? FString::Printf(TEXT("id=%s type=%s slot=(%g,%g,%g,%g) z=%d"), *Placement->Id, *Placement->Type,
			Placement->Slot.Offsets.Left, Placement->Slot.Offsets.Top, Placement->Slot.Offsets.Right, Placement->Slot.Offsets.Bottom, Placement->Slot.ZOrder)
			: FString(TEXT("missing placement"));
	};
	AddInfo(FString::Printf(TEXT("Independent paint placements: sourceA[%s] sourceB[%s] targetA[%s] targetB[%s]"),
		*PlacementContext(SourcePlacementA), *PlacementContext(SourcePlacementB), *PlacementContext(TargetPlacementA), *PlacementContext(TargetPlacementB)));
	TestTrue(TEXT("Independent paint order keeps source child positions and padding exactly once"), SourcePlacementA && SourcePlacementB
		&& TargetPlacementA && TargetPlacementB && SourcePlacementA->Slot.Offsets.Left == 2 && SourcePlacementA->Slot.Offsets.Top == 4
		&& SourcePlacementB->Slot.Offsets.Left == 16 && SourcePlacementB->Slot.Offsets.Top == 4
		&& TargetPlacementA->Slot.Offsets.Left == 2 && TargetPlacementA->Slot.Offsets.Top == 4
		&& TargetPlacementB->Slot.Offsets.Left == 16 && TargetPlacementB->Slot.Offsets.Top == 4);
	const FString StaticReverse = MakeContainerNode(TEXT("static_reverse"), TEXT("Row"), TEXT("FRAME"), 0, 0, 40, 40,
		SourceOrderChildren, OrderLayout + TEXT(",\"itemReverseZIndex\":true"));
	if (!TestTrue(TEXT("Static reverse paint order parses without a prototype"), FFigmaParser::ParseDocument(MakeFigmaDocJson(StaticReverse), Document))) return false;
	TestTrue(TEXT("H/V paint order has one typed semantic owner"), Document.Children[0]->LayoutData.bReversePaintOrder
		&& !Document.Children[0]->Metadata.Contains(TEXT("itemReverseZIndex")));
	const auto StaticData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	int32 StaticRankA = INDEX_NONE, StaticRankB = INDEX_NONE;
	TestTrue(TEXT("Static reversal does not depend on an animation binding"), FindPaintPlacement(StaticData.RootWidget, TEXT("source_a"), StaticRankA)
		&& FindPaintPlacement(StaticData.RootWidget, TEXT("source_b"), StaticRankB) && StaticRankA == 1 && StaticRankB == 0);
	const FString ReverseAbsoluteTarget = TargetWithAbsolute.Replace(TEXT("\"layoutMode\":\"VERTICAL\""), TEXT("\"layoutMode\":\"VERTICAL\",\"itemReverseZIndex\":true"));
	if (!TestTrue(TEXT("Added absolute layer under reverse paint order parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(
		SourceWithFlow.Replace(TEXT("\"layoutMode\":\"VERTICAL\""), TEXT("\"layoutMode\":\"VERTICAL\",\"itemReverseZIndex\":true")),
		AbsoluteExtra.Replace(*TargetWithAbsolute, *ReverseAbsoluteTarget)), Document))) return false;
	const auto ReverseAbsoluteData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Reverse absolute addition retains its action"), ReverseAbsoluteData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	const auto& ReverseAdded = ReverseAbsoluteData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.AddedLayers;
	if (!TestEqual(TEXT("One reverse-order added layer is retained"), ReverseAdded.Num(), 1)) return false;
	TestTrue(TEXT("Added layer keeps destination layout index separate from front paint rank"), ReverseAdded[0].DestinationChildIndex == 0
		&& ReverseAdded[0].EndIndex == 1 && !ReverseAdded[0].bBehindFlow
		&& AbsoluteTransition.AddedLayers[0].DestinationChildIndex == 0 && AbsoluteTransition.AddedLayers[0].EndIndex == 0
		&& AbsoluteTransition.AddedLayers[0].bBehindFlow);
	int32 AddedPaintRank = INDEX_NONE;
	TestTrue(TEXT("The added native Canvas layer uses the same front paint rank"), FindPaintPlacement(ReverseAbsoluteData.RootWidget,
		ReverseAdded[0].NodeId, AddedPaintRank) && AddedPaintRank == 1);
	const FString PageAction = TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"NAVIGATE\",\"destinationId\":\"page_b\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":200,\"easing\":{\"type\":\"LINEAR\"}}}]}]");
	const FString SurfaceTarget = TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1}},{\"type\":\"GRADIENT_LINEAR\",\"gradientHandlePositions\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0},{\"x\":0,\"y\":1}],\"gradientStops\":[{\"position\":0,\"color\":{\"r\":1,\"g\":1,\"b\":1,\"a\":0}},{\"position\":1,\"color\":{\"r\":0,\"g\":0,\"b\":0,\"a\":1}}]}]");
	const FString SurfacePages = MakeContainerNode(TEXT("page_a"), TEXT("Source"), TEXT("FRAME"), 0, 0, 40, 40, FString(), PageAction + Fill)
		+ TEXT(",") + MakeContainerNode(TEXT("page_b"), TEXT("Target"), TEXT("FRAME"), 0, 0, 40, 40, FString(), SurfaceTarget);
	if (!TestTrue(TEXT("Native surface paint fixture parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(SurfacePages,
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\"")), Document))) return false;
	const auto SurfaceData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Solid to layered gradient keeps its action"), SurfaceData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	TestTrue(TEXT("Non-text paint uses a surface target rather than a glyph retainer"),
		SurfaceData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.WidgetTargets.ContainsByPredicate(
			[](const FUMGPrototypeWidgetAnimationTarget& Item) { return Item.bSurfacePaintChange && !Item.bTextPaintChange; }));
	TFunction<bool(const FUMGWidgetNode&)> HasSurfaceProducer = [&](const FUMGWidgetNode& Node)
	{
		if (Node.Type == UMGWidgetTypes::Image && Node.Effect.SurfacePaints.IsSet() && Node.Effect.SurfacePaintTargets.IsSet())
			return Node.Brush.MaterialRef.Get(FString()) == TEXT("native-surface-paints") && !Node.Effect.TextPaints.IsSet();
		for (const auto& Child : Node.Children) if (HasSurfaceProducer(Child)) return true;
		return false;
	};
	TestTrue(TEXT("Surface paints retain their own deferred native material owner"), HasSurfaceProducer(SurfaceData.RootWidget));

	const FString SolidPathGeometry = TEXT(",\"fillGeometry\":[{\"path\":\"M0 0 L16 0 L16 16 L0 16 Z\",\"windingRule\":\"NONZERO\"}]");
	const FString SolidPathStart = TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":0.7,\"g\":0.2,\"b\":0.1,\"a\":1}}]");
	const FString SolidPathEnd = TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":0.1,\"g\":0.3,\"b\":0.8,\"a\":1}}]");
	const FString SolidPathPages = MakeContainerNode(TEXT("page_a"), TEXT("Source"), TEXT("FRAME"), 0, 0, 40, 40,
		MakeContainerNode(TEXT("path_a"), TEXT("Path"), TEXT("VECTOR"), 0, 0, 16, 16, FString(), SolidPathGeometry + SolidPathStart), PageAction)
		+ TEXT(",") + MakeContainerNode(TEXT("page_b"), TEXT("Target"), TEXT("FRAME"), 0, 0, 40, 40,
			MakeContainerNode(TEXT("path_b"), TEXT("Path"), TEXT("VECTOR"), 0, 0, 16, 16, FString(), SolidPathGeometry + SolidPathEnd), FString());
	if (!TestTrue(TEXT("Same-geometry solid path paint parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(SolidPathPages,
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\"")), Document))) return false;
	const auto SolidPathData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Same-geometry path colour retains its action"), SolidPathData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	TestTrue(TEXT("Baked path colour uses paint parameters instead of multiplying its source colour"),
		SolidPathData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.WidgetTargets.ContainsByPredicate(
			[](const FUMGPrototypeWidgetAnimationTarget& Target) { return Target.NodeId == TEXT("path_a") && Target.bSurfacePaintChange && !Target.bColor; }));
	TFunction<bool(const FUMGWidgetNode&)> HasPathPaint = [&](const FUMGWidgetNode& Node)
	{
		if (Node.Effect.SurfacePaints.IsSet() && Node.Effect.SurfacePaintTargets.IsSet())
			return Node.Effect.GeometryMaskPaths.IsSet() && Node.Effect.GeometryMaskPaths->Num() == 1
				&& Node.Effect.SurfacePaints->Num() == 1 && Node.Effect.SurfacePaintTargets->Num() == 1
				&& !Node.Path.HasPaintGeometry() && Node.Brush.MaterialRef.Get(FString()) == TEXT("native-surface-paints");
		for (const auto& Child : Node.Children) if (HasPathPaint(Child)) return true;
		return false;
	};
	TestTrue(TEXT("Path paint keeps exact source coverage and separate endpoint paints"), HasPathPaint(SolidPathData.RootWidget));

	const FString ContourSurface = TEXT(",\"cornerRadius\":8,\"cornerSmoothing\":0.6,\"clipsContent\":true,\"fillGeometry\":[{\"path\":\"M0 8 C0 0 0 0 8 0 L32 0 C40 0 40 0 40 8 L40 32 C40 40 40 40 32 40 L8 40 C0 40 0 40 0 32 Z\",\"windingRule\":\"NONZERO\"}]");
	const FString ContourSourceFill = TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":0,\"g\":0,\"b\":0,\"a\":1}}]");
	const FString ContourTargetFill = TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":1,\"b\":1,\"a\":1}}]");
	const FString ContourShadow = TEXT(",\"effects\":[{\"type\":\"DROP_SHADOW\",\"visible\":true,\"color\":{\"r\":0,\"g\":0,\"b\":0,\"a\":0.2},\"offset\":{\"x\":0,\"y\":2},\"radius\":2,\"spread\":0,\"blendMode\":\"NORMAL\",\"showShadowBehindNode\":true}]");
	const FString ContourPaintPages = MakeContainerNode(TEXT("page_a"), TEXT("Source"), TEXT("FRAME"), 0, 0, 100, 100,
		MakeContainerNode(TEXT("contour_a"), TEXT("Surface"), TEXT("FRAME"), 0, 0, 40, 40,
			MakeLeafNode(TEXT("contour_child_a"), TEXT("Child"), TEXT("RECTANGLE"), 2, 3, 8, 8, SolidPathStart), ContourSurface + ContourSourceFill), PageAction)
		+ TEXT(",") + MakeContainerNode(TEXT("page_b"), TEXT("Target"), TEXT("FRAME"), 0, 0, 100, 100,
			MakeContainerNode(TEXT("contour_b"), TEXT("Surface"), TEXT("FRAME"), 0, 0, 40, 40,
				MakeLeafNode(TEXT("contour_child_b"), TEXT("Child"), TEXT("RECTANGLE"), 2, 3, 8, 8, SolidPathStart), ContourSurface + ContourTargetFill + ContourShadow), FString());
	if (!TestTrue(TEXT("Native contour own-paint colour fixture parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(ContourPaintPages,
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\"")), Document))) return false;
	const auto ContourPaintData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Native contour own-paint keeps its action"), ContourPaintData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	const auto& ContourTransition = ContourPaintData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
	TestTrue(TEXT("A shared contour changes fill through its material independently of its child capture"),
		ContourTransition.Diagnostic.IsEmpty() && ContourTransition.SharedCaptures.Num() == 1 && ContourTransition.LocalEndpoints.IsEmpty()
		&& ContourTransition.WidgetTargets.ContainsByPredicate([](const FUMGPrototypeWidgetAnimationTarget& Paint)
			{ return Paint.NodeId == TEXT("contour_a") && Paint.bSurfacePaintChange && !Paint.bColor; }));
	TFunction<bool(const FUMGWidgetNode&)> HasContourOwnPaint = [&](const FUMGWidgetNode& Node)
	{
		if (Node.Id == TEXT("contour_a") && Node.Children.IsValidIndex(Node.SourceVisualChildIndex))
		{
			const FUMGWidgetNode* OwnPaint = &Node;
			while (OwnPaint->Children.IsValidIndex(OwnPaint->SourceVisualChildIndex))
				OwnPaint = &OwnPaint->Children[OwnPaint->SourceVisualChildIndex];
			if (OwnPaint->Type != UMGWidgetTypes::Overlay || OwnPaint->Children.IsEmpty()) return false;
			const auto& Producer = OwnPaint->Children[0];
			const auto* TargetPaint = Producer.Effect.SurfacePaintTargets.IsSet() ? Producer.Effect.SurfacePaintTargets->FindByPredicate(
				[](const FUMGSurfacePaintTarget& Paint) { return Paint.DestinationWidgetId == TEXT("contour_b"); }) : nullptr;
			return Producer.Type == UMGWidgetTypes::Image && Producer.Effect.GeometryMaskPaths.IsSet()
				&& Producer.Effect.SurfacePaints.IsSet() && Producer.Effect.SurfacePaints->Num() == 1
				&& (*Producer.Effect.SurfacePaints)[0].Color == FLinearColor::Black
				&& TargetPaint && TargetPaint->Paints.Num() == 1 && TargetPaint->Paints[0].Color == FLinearColor::White
				&& Producer.Brush.MaterialRef.Get(FString()) == TEXT("native-surface-paints") && !Producer.Path.HasPaintGeometry();
		}
		for (const auto& Child : Node.Children) if (HasContourOwnPaint(Child)) return true;
		return false;
	};
	TestTrue(TEXT("Composite contour own-paint resolves its Image coverage and exact black-to-white paints"), HasContourOwnPaint(ContourPaintData.RootWidget));

	const FString TwoTriggerPages = MakeContainerNode(TEXT("page_a"), TEXT("Source"), TEXT("FRAME"), 0, 0, 40, 40,
		MakeContainerNode(TEXT("first_a"), TEXT("First"), TEXT("RECTANGLE"), 0, 0, 10, 10, FString(), SolidPathStart + TEXT(",\"cornerRadius\":2") + PageAction)
		+ TEXT(",") + MakeContainerNode(TEXT("second_a"), TEXT("Second"), TEXT("RECTANGLE"), 20, 0, 10, 10, FString(), SolidPathStart + TEXT(",\"cornerRadius\":2") + PageAction), FString())
		+ TEXT(",") + MakeContainerNode(TEXT("page_b"), TEXT("Target"), TEXT("FRAME"), 0, 0, 40, 40,
			MakeContainerNode(TEXT("first_b"), TEXT("First"), TEXT("RECTANGLE"), 0, 0, 12, 10, FString(), SolidPathStart + TEXT(",\"cornerRadius\":2"))
			+ TEXT(",") + MakeContainerNode(TEXT("second_b"), TEXT("Second"), TEXT("RECTANGLE"), 20, 0, 12, 10, FString(), SolidPathStart + TEXT(",\"cornerRadius\":2")), FString());
	if (!TestTrue(TEXT("Two native triggers with the same page endpoints parse"), FFigmaParser::ParseDocument(MakeFigmaDocJson(TwoTriggerPages,
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\"")), Document))) return false;
	const auto TwoTriggerData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const auto& TwoTriggerBindings = TwoTriggerData.PrototypeNavigation().ClickBindings;
	if (!TestEqual(TEXT("Both native trigger actions remain"), TwoTriggerBindings.Num(), 2)) return false;
	TestTrue(TEXT("Both rounded-rectangle size changes require native fixed layout"),
		TwoTriggerBindings[0].PrimaryStep().Transition.bFixedViewportLayout && TwoTriggerBindings[1].PrimaryStep().Transition.bFixedViewportLayout);
	TestTrue(TEXT("Each trigger owns its fixed presentation without replacing another trigger's content"),
		!TwoTriggerBindings[0].PrimaryStep().Transition.LayoutPresentationWidgetId.IsEmpty()
		&& !TwoTriggerBindings[1].PrimaryStep().Transition.LayoutPresentationWidgetId.IsEmpty()
		&& TwoTriggerBindings[0].PrimaryStep().Transition.LayoutPresentationWidgetId != TwoTriggerBindings[1].PrimaryStep().Transition.LayoutPresentationWidgetId);
	TFunction<int32(const FUMGWidgetNode&, const FString&)> CountPresentation = [&](const FUMGWidgetNode& Node, const FString& Id)
	{
		int32 Count = Node.Id == Id ? 1 : 0;
		for (const auto& Child : Node.Children) Count += CountPresentation(Child, Id);
		return Count;
	};
	for (const auto& Binding : TwoTriggerBindings)
		for (const auto& Mapping : Binding.PrimaryStep().Transition.LayoutTargets)
			TestEqual(TEXT("Fixed layout mapping retains one actual native presentation role"),
				CountPresentation(TwoTriggerData.RootWidget, Mapping.PresentationWidgetId), 1);



    const FString SurfaceBackground = SurfaceTarget.Replace(TEXT("\"fills\":"), TEXT("\"background\":"));
    const FString SurfaceFillMetadata = SurfaceTarget.Replace(TEXT("\"gradientStops\":"),
        TEXT("\"gradientTransform\":[[1,0,0],[0,0.5,0.5]],\"gradientStops\":"))
        .Replace(TEXT("\"position\":"), TEXT("\"boundVariables\":{},\"position\":"));
    const FString SourcePluginPaints = TEXT("{") + Fill.Mid(1)
        + Fill.Replace(TEXT("\"fills\":"), TEXT("\"backgrounds\":"))
        + TEXT(",\"fillStyleId\":\"source-fill\",\"backgroundStyleId\":\"source-fill\"}");
    const FString TargetPluginPaints = TEXT("{") + SurfaceFillMetadata.Mid(1)
        + SurfaceBackground.Replace(TEXT("\"background\":"), TEXT("\"backgrounds\":"))
        + TEXT(",\"fillStyleId\":\"target-fill\",\"backgroundStyleId\":\"target-fill\"}");
    const auto SurfaceAliasDocument = [&](const FString& Background)
    {
        return MakeFigmaDocJson(MakeContainerNode(TEXT("page_a"), TEXT("Source"), TEXT("FRAME"), 0, 0, 40, 40, FString(),
            PageAction + Fill + Fill.Replace(TEXT("\"fills\":"), TEXT("\"background\":"))
                + TEXT(",\"backgroundColor\":{\"r\":0,\"g\":0,\"b\":0,\"a\":1},\"design2umgPluginData\":") + SourcePluginPaints) + TEXT(",")
            + MakeContainerNode(TEXT("page_b"), TEXT("Target"), TEXT("FRAME"), 0, 0, 40, 40, FString(),
                SurfaceFillMetadata + Background
                    + TEXT(",\"backgroundColor\":{\"r\":0,\"g\":0,\"b\":0,\"a\":0},\"design2umgPluginData\":") + TargetPluginPaints),
            TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\""));
    };
    if (!TestTrue(TEXT("Typed gradient background alias fixture parses"), FFigmaParser::ParseDocument(SurfaceAliasDocument(SurfaceBackground), Document))) return false;
    const auto AliasSurfaceData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    TestTrue(TEXT("Resolved fills consume equivalent canonical and plugin paint aliases and derived color/style metadata"),
        AliasSurfaceData.PrototypeNavigation().ClickBindings.Num() == 1
        && AliasSurfaceData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.Diagnostic.IsEmpty());
    if (!TestTrue(TEXT("Conflicting gradient background fixture parses"), FFigmaParser::ParseDocument(
        SurfaceAliasDocument(SurfaceBackground.Replace(TEXT("\"position\":1"), TEXT("\"position\":0.8"))), Document))) return false;
    const auto ConflictingSurfaceData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    TestTrue(TEXT("Unused background stops do not replace the canonical gradient animation"),
        ConflictingSurfaceData.PrototypeNavigation().ClickBindings.Num() == 1
        && ConflictingSurfaceData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.Diagnostic.IsEmpty());
	const FString LeftChild = MakeLeafNode(TEXT("left"), TEXT("Surface"), TEXT("RECTANGLE"), 0, 0, 20, 20,
		Fill + TEXT(",\"size\":{\"x\":20,\"y\":20},\"relativeTransform\":[[1,0,0],[0,1,0]],\"constraints\":{\"vertical\":\"BOTTOM\",\"horizontal\":\"LEFT\"}"));
	const FString RightChild = MakeLeafNode(TEXT("right"), TEXT("Surface"), TEXT("RECTANGLE"), 0, 0, 20, 20,
		Fill + TEXT(",\"size\":{\"x\":20,\"y\":20},\"relativeTransform\":[[1,0,0],[0,1,0]],\"opacity\":0.5,\"constraints\":{\"vertical\":\"TOP\",\"horizontal\":\"LEFT\"}"));
	const FString PageA = MakeContainerNode(TEXT("page_a"), TEXT("Page A"), TEXT("FRAME"), 0, 0, 40, 40, LeftChild, PageAction);
	const FString PageB = MakeContainerNode(TEXT("page_b"), TEXT("Page B"), TEXT("FRAME"), 0, 0, 40, 40,
		RightChild + TEXT(",") + Leaf(TEXT("new_leaf"), TEXT("New layer")));
	if (!TestTrue(TEXT("Page transition fixture parses"), FFigmaParser::ParseDocument(
		MakeFigmaDocJson(PageA + TEXT(",") + PageB, TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\"")), Document))) return false;
	const auto PageData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Page transition survives differing endpoint constraints"), PageData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	const auto& PageTransition = PageData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
	if (!TestEqual(TEXT("Target-only absolute page layer survives"), PageTransition.AddedLayers.Num(), 1)) return false;
	TestTrue(TEXT("Page transition layer has source and target scope"), PageTransition.AddedLayers[0].NodeId == TEXT("page_a::transition::page_b::new_leaf"));
	TestTrue(TEXT("Page target-only layer is a real converted widget"), [&]()
	{
		TFunction<bool(const FUMGWidgetNode&)> Contains = [&](const FUMGWidgetNode& Node)
		{
			if (Node.Id == PageTransition.AddedLayers[0].NodeId) return true;
			for (const auto& Child : Node.Children) if (Contains(Child)) return true;
			return false;
		};
		return Contains(PageData.RootWidget);
	}());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaAnimatedTextPaintContractTest,
	"Design2UMG.Unit.FigmaParser.AnimatedTextPaintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaAnimatedTextPaintContractTest::RunTest(const FString&)
{
	const FString SourceStyle = TEXT("\"fontFamily\":\"Inter\",\"fontSize\":96,\"letterSpacing\":0,\"lineHeightPx\":144,\"lineHeightPercent\":100,\"lineHeightUnit\":\"INTRINSIC_%\"");
	const FString TargetStyle = TEXT("\"fontFamily\":\"Inter\",\"fontSize\":56,\"letterSpacing\":0.75,\"lineHeightPx\":84,\"lineHeightPercent\":100,\"lineHeightUnit\":\"INTRINSIC_%\"");
	const FString Solid = TEXT("{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":1,\"b\":1,\"a\":1}}");
	const FString Gradient = TEXT("{\"type\":\"GRADIENT_LINEAR\",\"opacity\":0.4,\"gradientHandlePositions\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0}],\"gradientStops\":[{\"position\":0,\"color\":{\"r\":1,\"g\":0,\"b\":0,\"a\":0.5}},{\"position\":1,\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1}}]}");
	const FString Action = TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_DRAG\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"NAVIGATE\",\"destinationId\":\"text_page_b\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":200,\"easing\":{\"type\":\"LINEAR\"}}}]}]");
	const FString A = MakeTextNode(TEXT("text_a"), TEXT("Label"), 10, 20, 83, 144, TEXT("21"), SourceStyle,
		TEXT(",\"size\":{\"x\":83,\"y\":144},\"relativeTransform\":[[1,0,10],[0,1,20]],\"fills\":[") + Solid + TEXT("]") + Action);
	const FString B = MakeTextNode(TEXT("text_b"), TEXT("Label"), 15, 40, 50, 84, TEXT("21"), TargetStyle,
		TEXT(",\"size\":{\"x\":50,\"y\":84},\"relativeTransform\":[[1,0,15],[0,1,40]],\"fills\":[") + Solid + TEXT(",") + Gradient + TEXT("]"));
	const FString SourceSegment = TEXT(",\"design2umgPluginData\":{\"fills\":[") + Solid
		+ TEXT("],\"styledTextSegments\":[{\"fills\":[") + Solid + TEXT("],\"fillStyleId\":\"source-style\"}]}");
	const FString TargetSegment = TEXT(",\"design2umgPluginData\":{\"fills\":[") + Solid + TEXT(",") + Gradient
		+ TEXT("],\"styledTextSegments\":[{\"fills\":[") + Solid + TEXT(",") + Gradient + TEXT("],\"fillStyleId\":\"target-style\"}]}");
	const FString StyledA = A.Replace(TEXT("\"id\":\"text_a\""), *(TEXT("\"id\":\"text_a\"") + SourceSegment));
	const FString StyledB = B.Replace(TEXT("\"id\":\"text_b\""), *(TEXT("\"id\":\"text_b\"") + TargetSegment));
	const FString PageA = MakeContainerNode(TEXT("text_page_a"), TEXT("A"), TEXT("FRAME"), 0, 0, 200, 200, StyledA);
	const FString PageB = MakeContainerNode(TEXT("text_page_b"), TEXT("B"), TEXT("FRAME"), 0, 0, 200, 200, StyledB);
	FDesignDocument Document;
	if (!TestTrue(TEXT("Animated text source parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(PageA + TEXT(",") + PageB,
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\"")), Document))) return false;
	const auto Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	for (const auto& Warning : Data.Warnings) AddInfo(Warning);
	if (!TestTrue(TEXT("Animated text converts"), Data.ConversionError.IsEmpty())
		|| !TestEqual(TEXT("One animated text action"), Data.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	const auto& Transition = Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
	if (!TestEqual(TEXT("One text target"), Transition.WidgetTargets.Num(), 1)) return false;
	const auto& Target = Transition.WidgetTargets[0];
	TestTrue(TEXT("Native text fields and box have distinct tracks"), Target.bFontSize && Target.bLetterSpacing
		&& Target.bTextBoxChange && Target.bTextPaintChange && Target.Scale.Equals(FVector2D(1, 1)) && Target.Translation.IsNearlyZero());
	TestEqual(TEXT("Destination uses the real target scoped node"), Target.DestinationWidgetId, FString(TEXT("text_b")));
	TestTrue(TEXT("Drag direction comes from source-local centers"), Transition.bHasDragMotionVector
		&& Transition.DragMotionVector.Equals(FVector2D(-11.5, -10), 0.01));
	TFunction<const FUMGWidgetNode*(const FUMGWidgetNode&)> FindSource = [&](const FUMGWidgetNode& Node) -> const FUMGWidgetNode*
	{
		if (Node.Id == TEXT("text_a")) return &Node;
		for (const auto& Child : Node.Children) if (const auto* Found = FindSource(Child)) return Found;
		return nullptr;
	};
	const auto* Button = FindSource(Data.RootWidget);
	if (!TestTrue(TEXT("Text trigger is a native button with one paint owner"), Button && Button->Type == UMGWidgetTypes::Button
		&& Button->Children.Num() == 1)) return false;
	const auto& Retainer = Button->Children[0];
	if (!TestTrue(TEXT("One native text retainer preserves the editable TextBlock"), Retainer.Type == UMGWidgetTypes::RetainerBox
		&& Retainer.Children.Num() == 1 && Retainer.Children[0].Type == UMGWidgetTypes::TextBlock
		&& Retainer.Effect.TextPaints.IsSet() && Retainer.Effect.TextPaintTargets.IsSet())) return false;
	const auto& Targets = Retainer.Effect.TextPaintTargets.GetValue();
	if (!TestEqual(TEXT("One destination paint stack"), Targets.Num(), 1)
		|| !TestEqual(TEXT("Destination retains both layers"), Targets[0].Paints.Num(), 2)) return false;
	TestEqual(TEXT("Paint target uses the real destination"), Targets[0].DestinationWidgetId, FString(TEXT("text_b")));
	TestTrue(TEXT("Gradient paint opacity is separate from stop alpha"), FMath::IsNearlyEqual(Targets[0].Paints[1].Opacity, 0.4f)
		&& Targets[0].Paints[1].Gradient.Stops.Num() == 2
		&& FMath::IsNearlyEqual(Targets[0].Paints[1].Gradient.Stops[0].Color.A, 0.5f, 0.01f));
	const FString AppearanceA = PageA.Replace(TEXT("\"fontFamily\":\"Inter\""), TEXT("\"fontFamily\":\"Inter\",\"fontStyle\":\"Light\",\"fontWeight\":300,\"textAlignHorizontal\":\"CENTER\""));
	const FString AppearanceB = PageB.Replace(TEXT("\"fontFamily\":\"Inter\""), TEXT("\"fontFamily\":\"Inter\",\"fontStyle\":\"Regular\",\"fontWeight\":400,\"textAlignHorizontal\":\"LEFT\""));
	if (!TestTrue(TEXT("Local native text appearance parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(AppearanceA + TEXT(",") + AppearanceB,
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\"")), Document))) return false;
	const auto AppearanceData = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Local text conversion succeeds"), AppearanceData.ConversionError, FString())
		|| !TestEqual(TEXT("One local text action"), AppearanceData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
	const auto& Appearance = AppearanceData.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
	TestTrue(TEXT("Local glyph endpoints retain SMART layout, size, tracking and paint"), Appearance.Diagnostic.IsEmpty()
		&& Appearance.Type == TEXT("SMART_ANIMATE") && Appearance.bFixedViewportLayout && Appearance.LocalEndpoints.Num() == 1
		&& Appearance.WidgetTargets.Num() == 1 && Appearance.WidgetTargets[0].bFontSize
		&& Appearance.WidgetTargets[0].bLetterSpacing && Appearance.WidgetTargets[0].bTextPaintChange
		&& !Appearance.LayoutTargets.IsEmpty());
	TFunction<const FUMGWidgetNode*(const FUMGWidgetNode&, const FString&)> FindGlyphEndpoint =
		[&](const FUMGWidgetNode& Node, const FString& Id) -> const FUMGWidgetNode*
	{
		if (Node.Id == Id) return &Node;
		for (const auto& Child : Node.Children) if (const auto* Found = FindGlyphEndpoint(Child, Id)) return Found;
		return nullptr;
	};
	if (Appearance.LocalEndpoints.Num() == 1)
	{
		const auto& Endpoint = Appearance.LocalEndpoints[0];
		const auto* Incoming = FindGlyphEndpoint(AppearanceData.RootWidget, Endpoint.TargetVisualWidgetId);
		const auto* Outgoing = FindGlyphEndpoint(AppearanceData.RootWidget, Endpoint.SourceVisualWidgetId);
		TestTrue(TEXT("Target glyph is collapsed during canonical HUG measurement"), Incoming
			&& Incoming->WidgetStyle.Visibility.Get(FString()) == TEXT("Collapsed") && Incoming->Render.RenderOpacity.Get(1) == 0);
		TestTrue(TEXT("Both glyph endpoints retain independent editable native TextBlocks and paint owners"), Incoming && Outgoing
			&& UMGTextPaintOwner::Find(*Incoming) && UMGTextPaintOwner::Find(*Outgoing)
			&& UMGTextPaintOwner::Find(*Incoming)->Children[0].Text.Text.Get(FString()) == UMGTextPaintOwner::Find(*Outgoing)->Children[0].Text.Text.Get(FString()));
		if (Incoming && Outgoing && UMGTextPaintOwner::Find(*Incoming) && UMGTextPaintOwner::Find(*Outgoing))
		{
			const auto& SourceGlyph = UMGTextPaintOwner::Find(*Outgoing)->Children[0];
			const auto& TargetGlyph = UMGTextPaintOwner::Find(*Incoming)->Children[0];
			TestEqual(TEXT("Source native face is retained"), SourceGlyph.Text.FontStyle.Get(FString()), FString(TEXT("Light")));
			TestEqual(TEXT("Target native face is retained"), TargetGlyph.Text.FontStyle.Get(FString()), FString(TEXT("Regular")));
			TestEqual(TEXT("Source native alignment is retained"), SourceGlyph.Text.Justification.Get(FString()), FString(TEXT("Center")));
			TestEqual(TEXT("Target native alignment is retained"), TargetGlyph.Text.Justification.Get(FString()), FString(TEXT("Left")));
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaStateReplacementContractTest,
	"Design2UMG.Unit.FigmaParser.StateReplacementContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaStateReplacementContractTest::RunTest(const FString&)
{
	const FString SourceText = MakeTextNode(TEXT("source_label"), TEXT("Label"), 0, 0, 40, 20, TEXT("YES"),
		TEXT("\"fontFamily\":\"Inter\",\"fontSize\":12"), TEXT(",\"componentPropertyReferences\":{\"characters\":\"label\"}"));
	const FString TargetText = MakeTextNode(TEXT("target_label"), TEXT("Label"), 0, 0, 40, 20, TEXT("Label"),
		TEXT("\"fontFamily\":\"Inter\",\"fontSize\":12"), TEXT(",\"componentPropertyReferences\":{\"characters\":\"label\"}"));
	const FString ReturnAction = TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"CHANGE_TO\",\"destinationId\":\"base\"}]}]");
	const FString Target = MakeContainerNode(TEXT("target"), TEXT("Target"), TEXT("COMPONENT"), 0, 0, 40, 20, TargetText, ReturnAction);
	for (const bool bDissolve : { false, true })
	{
		const FString Transition = bDissolve
			? TEXT(",\"transition\":{\"type\":\"DISSOLVE\",\"duration\":200,\"easing\":{\"type\":\"LINEAR\"}}") : FString();
		const FString Action = FString::Printf(TEXT(",\"componentId\":\"base\",\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"YES\"}},\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"CHANGE_TO\",\"destinationId\":\"target\"%s}]}]"), *Transition);
		const FString Instance = MakeContainerNode(TEXT("instance"), TEXT("Instance"), TEXT("INSTANCE"), 0, 0, 40, 20, SourceText, Action);
		const FString Extra = FString::Printf(TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\",\"design2umgComponentDependencies\":{\"schemaVersion\":1,\"definitions\":[{\"id\":\"target\",\"key\":\"target\",\"document\":%s}],\"instances\":{},\"errors\":[]}"), *Target);
		FDesignDocument Document;
		if (!TestTrue(TEXT("State variant parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Instance, Extra), Document))) return false;
		const auto Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		if (!TestEqual(TEXT("Forward and return actions are retained"), Data.PrototypeNavigation().ClickBindings.Num(), 2)
			|| !TestEqual(TEXT("One state host is used"), Data.PrototypeNavigation().StateHosts.Num(), 1)) return false;
		const auto& Plan = Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
		TestEqual(TEXT("Instant state is explicit"), Plan.bStateInstant, !bDissolve);
		if (!TestEqual(TEXT("State transition retains both roots"), Plan.WidgetTargets.Num(), 2)) return false;
		const auto& Host = Data.PrototypeNavigation().StateHosts[0];
		if (!TestEqual(TEXT("State host retains two variants"), Host.States.Num(), 2)) return false;
		TestEqual(TEXT("Target state is scoped to source instance"), Plan.TargetStateWidgetId, FString(TEXT("instance::target")));
		TestTrue(TEXT("Target retains the instance text override"), Host.States[1].SourceNodeJson.Contains(TEXT("\"characters\":\"YES\"")));
		const auto& Reverse = Data.PrototypeNavigation().ClickBindings[1].PrimaryStep().Transition;
		TestEqual(TEXT("Return action belongs to target state"), Reverse.SourceStateId, FString(TEXT("target")));
		TestEqual(TEXT("Return action reuses initial state root"), Reverse.TargetStateWidgetId, FString(TEXT("instance")));
		TestEqual(TEXT("Return action reuses same host"), Reverse.StateHostWidgetId, Plan.StateHostWidgetId);
		TFunction<bool(const FUMGWidgetNode&)> HasTarget = [&](const FUMGWidgetNode& Node)
		{
			if (Node.Id == Plan.TargetStateWidgetId) return true;
			for (const auto& Child : Node.Children) if (HasTarget(Child)) return true;
			return false;
		};
		TestTrue(TEXT("Target state has a real native widget tree"), HasTarget(Data.RootWidget));
	}
	const FString MixedActions = TEXT(R"JSON(,"componentId":"base","interactions":[
		{"trigger":{"type":"ON_HOVER"},"actions":[{"type":"NODE","navigation":"CHANGE_TO","destinationId":"target","transition":null}]},
		{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"NODE","navigation":"NAVIGATE","destinationId":"destination_page","transition":null}]}
	])JSON");
	const FString MixedInstance = MakeContainerNode(TEXT("mixed_instance"), TEXT("Instance"), TEXT("INSTANCE"), 0, 0, 40, 20, SourceText, MixedActions);
	const FString Pages = MakeContainerNode(TEXT("source_page"), TEXT("Source page"), TEXT("FRAME"), 0, 0, 100, 100, MixedInstance)
		+ TEXT(",") + MakeContainerNode(TEXT("destination_page"), TEXT("Destination page"), TEXT("FRAME"), 0, 0, 100, 100,
			MakeTextNode(TEXT("destination_label"), TEXT("Label"), 0, 0, 40, 20, TEXT("Page"), TEXT("\"fontFamily\":\"Inter\",\"fontSize\":12")));
	const FString MixedExtra = FString::Printf(TEXT(",\"design2umgComponentDependencies\":{\"schemaVersion\":1,\"definitions\":[{\"id\":\"target\",\"key\":\"target\",\"document\":%s}],\"instances\":{},\"errors\":[]}"), *Target);
	FDesignDocument MixedDocument;
	if (!TestTrue(TEXT("State and page actions on one instance parse"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Pages, MixedExtra), MixedDocument))) return false;
	const auto MixedData = FDesign2UMGConverter::Convert(MixedDocument, FDesign2UMGConversionSettings::Defaults());
	const auto* PageAction = MixedData.PrototypeNavigation().ClickBindings.FindByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
	{ return Binding.TriggerWidgetId == TEXT("mixed_instance") && Binding.UsesClickTrigger(); });
	const auto* StateAction = MixedData.PrototypeNavigation().ClickBindings.FindByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
	{ return Binding.TriggerWidgetId == TEXT("mixed_instance") && Binding.UsesHoverTrigger(); });
	if (TestNotNull(TEXT("Click navigation survives the state context"), PageAction))
	{
		TestTrue(TEXT("Page action retains its navigation and destination"), PageAction->Steps.Num() == 1
			&& PageAction->Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate
			&& PageAction->Steps[0].TargetPageId == TEXT("destination_page")
			&& PageAction->SourcePageId == TEXT("source_page"));
		TestTrue(TEXT("Page navigation carries no state transition identity"), PageAction->PrimaryStep().Transition.StateHostWidgetId.IsEmpty() && PageAction->PrimaryStep().Transition.InstanceScopeId.IsEmpty()
			&& PageAction->PrimaryStep().Transition.SourceStateId.IsEmpty() && PageAction->PrimaryStep().Transition.TargetStateId.IsEmpty()
			&& PageAction->PrimaryStep().Transition.SourceStateWidgetId.IsEmpty() && PageAction->PrimaryStep().Transition.TargetStateWidgetId.IsEmpty());
	}
	if (TestNotNull(TEXT("Hover state action remains available"), StateAction))
		TestTrue(TEXT("State action retains its host and variant identities"), !StateAction->PrimaryStep().Transition.StateHostWidgetId.IsEmpty()
			&& StateAction->PrimaryStep().Transition.SourceStateId == TEXT("base") && StateAction->PrimaryStep().Transition.TargetStateId == TEXT("target")
			&& StateAction->PrimaryStep().Transition.SourceStateWidgetId == TEXT("mixed_instance")
			&& StateAction->PrimaryStep().Transition.TargetStateWidgetId == TEXT("mixed_instance::target"));
	const auto TimedStateAction = [](const TCHAR* Destination)
	{
		return FString::Printf(TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"AFTER_TIMEOUT\",\"timeout\":100},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"CHANGE_TO\",\"destinationId\":\"%s\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":200,\"easing\":{\"type\":\"LINEAR\"}}}]}]"), Destination);
	};
	const FString IndicatorPaint = TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":1,\"b\":1,\"a\":1}}],\"design2umgPluginData\":{\"topLeftRadius\":0,\"topRightRadius\":0,\"bottomRightRadius\":0,\"bottomLeftRadius\":0}");
	const FString ExpandedIndicator = MakeLeafNode(TEXT("expanded_state"), TEXT("State"), TEXT("FRAME"), 0, 0, 32, 20, IndicatorPaint);
	const FString BaseIndicator = MakeLeafNode(TEXT("base_state"), TEXT("State"), TEXT("FRAME"), 0, 0, 20, 20, IndicatorPaint);
	const FString TargetIndicator = MakeLeafNode(TEXT("target_state"), TEXT("State"), TEXT("FRAME"), 0, 0, 20, 20,
		IndicatorPaint + TEXT(",\"opacity\":0.4"));
	const FString NestedInstance = MakeContainerNode(TEXT("nested"), TEXT("Indicator"), TEXT("INSTANCE"), 7, 9, 32, 20, ExpandedIndicator,
		IndicatorPaint + TEXT(",\"componentId\":\"inner_base\",\"locked\":true,\"layoutPositioning\":\"ABSOLUTE\",\"constraints\":{\"horizontal\":\"CENTER\",\"vertical\":\"SCALE\"},\"overrides\":[{\"id\":\"nested\",\"overriddenFields\":[\"width\",\"height\",\"layoutPositioning\"]}]") + TimedStateAction(TEXT("inner_target")));
	const FString OuterTarget = MakeContainerNode(TEXT("outer_target"), TEXT("Focused"), TEXT("COMPONENT"), 0, 0, 40, 40, NestedInstance);
	const FString InnerBase = MakeContainerNode(TEXT("inner_base"), TEXT("Indicator"), TEXT("COMPONENT"), 0, 0, 20, 20, BaseIndicator, IndicatorPaint);
	const FString InnerTarget = MakeContainerNode(TEXT("inner_target"), TEXT("Indicator"), TEXT("COMPONENT"), 0, 0, 20, 20, TargetIndicator,
		IndicatorPaint + TEXT(",\"opacity\":0.5") + TimedStateAction(TEXT("inner_base")));
	const FString OuterInstance = MakeContainerNode(TEXT("outer"), TEXT("Button"), TEXT("INSTANCE"), 0, 0, 40, 40, FString(),
		IndicatorPaint + TEXT(",\"componentId\":\"outer_base\",\"interactions\":[{\"trigger\":{\"type\":\"ON_HOVER\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"CHANGE_TO\",\"destinationId\":\"outer_target\"}]}]"));
	const FString NestedExtra = FString::Printf(TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\",\"design2umgComponentDependencies\":{\"schemaVersion\":1,\"definitions\":[{\"id\":\"outer_target\",\"key\":\"outer_target\",\"document\":%s},{\"id\":\"inner_base\",\"key\":\"inner_base\",\"document\":%s},{\"id\":\"inner_target\",\"key\":\"inner_target\",\"document\":%s}],\"instances\":{},\"errors\":[]}"),
		*OuterTarget, *InnerBase, *InnerTarget);
	FDesignDocument NestedDocument;
	if (!TestTrue(TEXT("Nested timed instance states parse"), FFigmaParser::ParseDocument(MakeFigmaDocJson(OuterInstance, NestedExtra), NestedDocument))) return false;
	const auto NestedData = FDesign2UMGConverter::Convert(NestedDocument, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Painted nested state source converts"), NestedData.ConversionError, FString())) return false;
	TestEqual(TEXT("Outer and nested instances have distinct state hosts"), NestedData.PrototypeNavigation().StateHosts.Num(), 2);
	const auto* NestedAction = NestedData.PrototypeNavigation().ClickBindings.FindByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
	{ return Binding.TriggerWidgetId == TEXT("outer::nested") && Binding.UsesPageOpenTrigger(); });
	const auto* NestedReturn = NestedData.PrototypeNavigation().ClickBindings.FindByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
	{ return Binding.TriggerWidgetId == TEXT("outer::nested::inner_target") && Binding.UsesPageOpenTrigger(); });
	if (TestNotNull(TEXT("Nested timer retains its own instance action"), NestedAction)
		&& TestNotNull(TEXT("Nested target timer retains its return action"), NestedReturn))
	{
		TestTrue(TEXT("Nested animation compares the indicator rather than its outer button"), NestedAction->PrimaryStep().Transition.Diagnostic.IsEmpty()
			&& NestedAction->PrimaryStep().Transition.InstanceScopeId == TEXT("outer::nested")
			&& NestedAction->PrimaryStep().Transition.SourceStateWidgetId == TEXT("outer::nested")
			&& NestedAction->PrimaryStep().Transition.TargetStateWidgetId == TEXT("outer::nested::inner_target"));
		TestTrue(TEXT("Nested return reuses only the indicator host"), NestedReturn->PrimaryStep().Transition.Diagnostic.IsEmpty()
			&& NestedReturn->PrimaryStep().Transition.StateHostWidgetId == NestedAction->PrimaryStep().Transition.StateHostWidgetId
			&& NestedReturn->PrimaryStep().Transition.SourceStateWidgetId == TEXT("outer::nested::inner_target")
			&& NestedReturn->PrimaryStep().Transition.TargetStateWidgetId == TEXT("outer::nested"));
		const auto* Host = NestedData.PrototypeNavigation().StateHosts.FindByPredicate([](const FUMGPrototypeStateHost& Item)
		{ return Item.InstanceWidgetId == TEXT("outer::nested"); });
		const auto* State = Host ? Host->States.FindByPredicate([](const FUMGPrototypeStateEntry& Item)
		{ return Item.StateId == TEXT("inner_target"); }) : nullptr;
		FDesignDocument Effective;
		if (TestNotNull(TEXT("Nested effective target state exists"), State)
			&& TestTrue(TEXT("Generated state source parses"), FFigmaParser::ParseDocument(TEXT("{\"document\":") + State->SourceNodeJson + TEXT("}"), Effective))
			&& TestEqual(TEXT("One effective target root"), Effective.Children.Num(), 1))
		{
			const auto& Root = *Effective.Children[0];
			TestTrue(TEXT("Effective target keeps explicit instance size and placement"), Root.Transform.Width.Value == 32
				&& Root.Transform.Height.Value == 20 && Root.Transform.X == 7 && Root.Transform.Y == 9 && Root.LayoutData.bAbsoluteLayoutPositioning);
			if (TestEqual(TEXT("Effective target keeps its matching child"), Root.Children.Num(), 1))
				TestTrue(TEXT("Expanded child layout and target opacity have separate ownership"), Root.Children[0]->Transform.Width.Value == 32
					&& FMath::IsNearlyEqual(Root.Children[0]->Transform.Opacity, 0.4f));
		}
		TestFalse(TEXT("Explicit instance width creates no false size animation"), NestedAction->PrimaryStep().Transition.WidgetTargets.ContainsByPredicate(
			[](const FUMGPrototypeWidgetAnimationTarget& Item) { return !Item.Scale.Equals(FVector2D(1, 1)); }));
	}
	const FString ChangedDefinition = NestedExtra.Replace(*TargetIndicator,
		*MakeLeafNode(TEXT("target_state"), TEXT("State"), TEXT("FRAME"), 0, 0, 18, 20, IndicatorPaint));
	if (!TestTrue(TEXT("A genuinely changed definition under an expanded instance parses"), FFigmaParser::ParseDocument(
		MakeFigmaDocJson(OuterInstance, ChangedDefinition), NestedDocument))) return false;
	const auto ChangedLayout = FDesign2UMGConverter::Convert(NestedDocument, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Expanded layout cannot hide a real target definition dimension change"), ChangedLayout.Warnings.ContainsByPredicate(
		[](const FString& Warning) { return Warning.Contains(TEXT("changed definition dimensions")) && Warning.Contains(TEXT("action not executed")); }));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaNativeWidgetAnimationContractTest,
	"Design2UMG.Unit.FigmaParser.NativeWidgetAnimationContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaNativeWidgetAnimationContractTest::RunTest(const FString&)
{
	const auto Rejected = [](const FUMGWidgetData& Data, const TCHAR* TriggerId, const TCHAR* Reason)
	{
		return Data.Warnings.ContainsByPredicate([&](const FString& Warning)
		{
			return Warning.Contains(TriggerId) && Warning.Contains(Reason) && Warning.Contains(TEXT("action not executed"));
		}) && !Data.PrototypeNavigation().ClickBindings.ContainsByPredicate([&](const FUMGPrototypeNavigationBinding& Binding)
		{
			return Binding.TriggerWidgetId == TriggerId;
		});
	};
	FDesignDocument Document;
	if (!TestTrue(TEXT("Canonical animation source parses"), FFigmaParser::ParseDocument(MakeNativeWidgetAnimationDocument(), Document))) return false;
	auto Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const auto& Bindings = Data.PrototypeNavigation().ClickBindings;
	if (!TestEqual(TEXT("Four real trigger contracts retained"), Bindings.Num(), 4)) return false;
	for (const auto& Binding : Bindings)
	{
		TestEqual(TEXT("Single-page triggers retain their real top-level source page"), Binding.SourcePageId, FString(TEXT("page")));
		TestTrue(TEXT("Full source trees are expressible"), Binding.PrimaryStep().Transition.Diagnostic.IsEmpty());
		if (!TestEqual(TEXT("One concrete widget, not whole switcher"), Binding.PrimaryStep().Transition.WidgetTargets.Num(), 1)) return false;
		const auto& Target = Binding.PrimaryStep().Transition.WidgetTargets[0];
		TestTrue(TEXT("Instances keep separate source identities"), Target.NodeId == Binding.TriggerWidgetId + TEXT("_surface"));
		TestTrue(TEXT("Local source sizes produce exact scale"), Target.Scale.Equals(FVector2D(2, 2)));
		TestTrue(TEXT("Local matrix centers produce exact movement"), Target.Translation.Equals(FVector2D(40, 50)));
		TestTrue(TEXT("Matrix rotation is converted to native degrees"), FMath::IsNearlyEqual(Target.Rotation, 90.f));
		TestTrue(TEXT("Opacity and solid color differences retained"), Target.bOpacity && Target.bColor);
		TestTrue(TEXT("EASE_OUT maps to the exact native weighted curve including zero handle"),
			Binding.PrimaryStep().Transition.bCubicBezier && Binding.PrimaryStep().Transition.CubicBezier.Equals(FVector4(0, 0, 0.58, 1)));
		TestTrue(TEXT("REST milliseconds consumed once"), FMath::IsNearlyEqual(Binding.PrimaryStep().Transition.DurationSeconds, 0.4f));
	}
	TestTrue(TEXT("Mouse-enter stays distinct from reverting hover"), Bindings[1].UsesMouseEnterTrigger() && !Bindings[1].UsesHoverTrigger());
	TestTrue(TEXT("AFTER_TIMEOUT milliseconds consumed once"), FMath::IsNearlyEqual(Bindings[2].TriggerDelaySeconds, 0.05f));
	const FString MissingUnit = MakeNativeWidgetAnimationDocument().Replace(TEXT("\"MILLISECONDS\""), TEXT("\"UNKNOWN\""));
	FFigmaParser::ParseDocument(MissingUnit, Document);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Unverified HTTP timing is not inferred from the number"),
		Rejected(Data, TEXT("instance_0"), TEXT("unit is unverified")));
	TestTrue(TEXT("Rejected actions do not create a prototype root"), Data.RootWidget.Type != TEXT("WidgetSwitcher"));
	TFunction<bool(const FUMGWidgetNode&)> HasButton = [&](const FUMGWidgetNode& Node)
	{
		if (Node.Type == TEXT("Button")) return true;
		for (const FUMGWidgetNode& Child : Node.Children) if (HasButton(Child)) return true;
		return false;
	};
	TestFalse(TEXT("Rejected actions do not wrap source nodes in buttons"), HasButton(Data.RootWidget));
	const FString StyledTarget = MakeNativeWidgetAnimationDocument().Replace(
		TEXT("\"id\":\"target_surface\""), TEXT("\"id\":\"target_surface\",\"styles\":{\"fill\":\"resolved-fill\"}"));
	if (!TestTrue(TEXT("Style reference fixture parses"), FFigmaParser::ParseDocument(StyledTarget, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Resolved paint remains animatable when only its style reference identity differs"),
		Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.Diagnostic.IsEmpty()
		&& Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.WidgetTargets.Num() == 1
		&& Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.WidgetTargets[0].bColor);
	TFunction<const FUMGWidgetNode*(const FUMGWidgetNode&, const FString&)> FindEndpoint =
		[&](const FUMGWidgetNode& Node, const FString& Id) -> const FUMGWidgetNode*
	{
		if (Node.Id == Id) return &Node;
		for (const auto& Child : Node.Children) if (const auto* Found = FindEndpoint(Child, Id)) return Found;
		return nullptr;
	};
	const auto SourceVisual = [](const FUMGWidgetNode* Node) -> const FUMGWidgetNode*
	{
		while (Node)
		{
			if (Node->Type == UMGWidgetTypes::Button && Node->Children.Num() == 1
				&& Node->Children[0].Id == Node->Id + Design2UMGGeneratedNameSuffixes::Content()) Node = &Node->Children[0];
			else if (Node->Type == UMGWidgetTypes::Overlay && !Node->Children.IsEmpty()
				&& Node->Children[0].Id == Node->Id + TEXT("__layout_content")) Node = &Node->Children[0];
			else if (Node->SourceVisualChildIndex != INDEX_NONE)
			{
				if (!Node->Children.IsValidIndex(Node->SourceVisualChildIndex)) return nullptr;
				Node = &Node->Children[Node->SourceVisualChildIndex];
			}
			else return Node;
		}
		return nullptr;
	};
	const FString RadiusChange = StyledTarget.Replace(TEXT("\"rotation\":1.5707963267948966"), TEXT("\"cornerRadius\":5,\"rotation\":1.5707963267948966"));
	FFigmaParser::ParseDocument(RadiusChange, Document);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const auto* RoundedTarget = SourceVisual(FindEndpoint(Data.RootWidget, TEXT("instance_0::target_surface")));
	TestTrue(TEXT("Native target radius is retained exactly"), RoundedTarget && RoundedTarget->Type == UMGWidgetTypes::Border
		&& RoundedTarget->Brush.OutlineCornerRadius.Get(0.0f) == 5.0f);
	TestTrue(TEXT("Native corner endpoints retain original transition and timing"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("instance_0")
			&& Binding.PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE") && Binding.PrimaryStep().Transition.LocalEndpoints.Num() == 1
			&& Binding.PrimaryStep().Transition.bFixedViewportLayout && !Binding.PrimaryStep().Transition.LayoutTargets.IsEmpty()
			&& Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && FMath::IsNearlyEqual(Binding.PrimaryStep().Transition.DurationSeconds, 0.4f)
			&& Binding.PrimaryStep().Transition.bCubicBezier && Binding.PrimaryStep().Transition.CubicBezier.Equals(FVector4(0, 0, 0.58, 1)); }));
	const FString UnsupportedContour = RadiusChange.Replace(TEXT("\"cornerRadius\":5"),
		TEXT("\"cornerRadius\":5,\"vectorPaths\":[{\"data\":\"M0 0L80 0L72 40L0 40Z\",\"windingRule\":\"NONZERO\"}]"));
	if (!TestTrue(TEXT("Unsupported contour beside native corner change parses"), FFigmaParser::ParseDocument(UnsupportedContour, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Unsupported geometry is not silently stripped"), Rejected(Data, TEXT("instance_0"), TEXT("vectorPaths")));
	const FString ShadowFixture = StyledTarget.Replace(TEXT("\"rotation\":1.5707963267948966"),
		TEXT("\"effects\":[{\"type\":\"DROP_SHADOW\",\"visible\":true,\"color\":{\"r\":0,\"g\":0,\"b\":0,\"a\":0.3},\"offset\":{\"x\":0,\"y\":2},\"radius\":4,\"spread\":0}],\"rotation\":1.5707963267948966"));
	if (!TestTrue(TEXT("Source shadow dissolve fixture parses"), FFigmaParser::ParseDocument(ShadowFixture, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const auto* ShadowBinding = Data.PrototypeNavigation().ClickBindings.FindByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("instance_0"); });
	TestTrue(TEXT("Changed leaf shadows retain SMART layout and native local endpoints"), ShadowBinding
		&& ShadowBinding->PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE") && ShadowBinding->PrimaryStep().Transition.SourceTransitionType.IsEmpty()
		&& ShadowBinding->PrimaryStep().Transition.DissolveReason.IsEmpty() && ShadowBinding->PrimaryStep().Transition.Diagnostic.IsEmpty()
		&& ShadowBinding->PrimaryStep().Transition.bFixedViewportLayout && !ShadowBinding->PrimaryStep().Transition.LayoutTargets.IsEmpty()
		&& !ShadowBinding->PrimaryStep().Transition.WidgetTargets.IsEmpty() && ShadowBinding->PrimaryStep().Transition.LocalEndpoints.Num() == 1
		&& ShadowBinding->PrimaryStep().Transition.SharedCaptures.IsEmpty());
	if (ShadowBinding && ShadowBinding->PrimaryStep().Transition.LocalEndpoints.Num() == 1)
	{
		const auto& Endpoint = ShadowBinding->PrimaryStep().Transition.LocalEndpoints[0];
		const auto* Owner = FindEndpoint(Data.RootWidget, Endpoint.NodeId);
		const auto* Destination = FindEndpoint(Data.RootWidget, Endpoint.DestinationWidgetId);
		const auto* TargetPaint = FindEndpoint(Data.RootWidget, Endpoint.TargetVisualWidgetId);
		TestTrue(TEXT("The original unclipped rotated leaf keeps native shadow paint without repeating its effects on content"),
			!Endpoint.bText && Endpoint.bFixedPaintSize && Owner && Destination && TargetPaint
			&& Owner->Type == UMGWidgetTypes::Overlay && Owner->SourceVisualChildIndex == 0 && Owner->Children.Num() == 3
			&& Owner->Children[0].Id == Endpoint.SourceVisualWidgetId && Owner->Children[1].Id == Endpoint.TargetVisualWidgetId
			&& Owner->Children.Last().Id == Endpoint.NodeId + TEXT("__local_content")
			&& Owner->Children.Last().Children.IsEmpty() && !Owner->Children.Last().Shadow.HasAny()
			&& !Owner->Children.Last().Effect.SharedCapture.IsSet()
			&& Destination->Render.RenderAngle.Get(0.f) == 90.f
			&& FMath::IsNearlyEqual(Destination->Render.RenderOpacity.Get(1.f), 0.4f)
			&& Destination->Children.Last().Children.IsEmpty() && !Destination->Children.Last().Shadow.HasAny()
			&& !Destination->Children.Last().Effect.SharedCapture.IsSet()
			&& TargetPaint->Type == UMGWidgetTypes::Overlay && TargetPaint->Children.Num() == 2
			&& TargetPaint->Children[0].Type == UMGWidgetTypes::Image && TargetPaint->Children[0].Children.IsEmpty()
			&& TargetPaint->Children[0].Shadow.ShadowBlur.Get(0.f) == 4.f
			&& TargetPaint->Children[1].Type == UMGWidgetTypes::Border && !TargetPaint->Children[1].Shadow.HasAny());
	}
	if (!TestTrue(TEXT("Shadow with an unconsumed property parses"), FFigmaParser::ParseDocument(
		ShadowFixture.Replace(TEXT("\"id\":\"target_surface\""), TEXT("\"id\":\"target_surface\",\"unconsumedProperty\":true")), Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Pending shadow dissolve cannot hide a later source field rejection"), Rejected(Data, TEXT("instance_0"), TEXT("unconsumedProperty")));
	const FString EmptyMotion = TEXT("\"animationStyles\":[],\"animations\":{},\"manualKeyframeTracks\":{},");
	const FString SourceMotion = EmptyMotion + TEXT("\"timelines\":[{\"id\":\"source-frame\",\"duration\":2}]");
	const FString TargetMotion = EmptyMotion + TEXT("\"timelines\":[{\"id\":\"target-frame\",\"duration\":2}]");

    const FString StrokeFill = TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":0,\"b\":0,\"a\":1}}]");
    const FString SolidStroke = TEXT("[{\"type\":\"SOLID\",\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1}}]");
    const FString GradientStroke = TEXT("[{\"type\":\"GRADIENT_LINEAR\",\"gradientHandlePositions\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0}],\"gradientStops\":[{\"position\":0,\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1}},{\"position\":1,\"color\":{\"r\":0,\"g\":1,\"b\":0,\"a\":1}}]}]");
    const FString StrokeAction = TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"NAVIGATE\",\"destinationId\":\"stroke_page_b\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":200,\"easing\":{\"type\":\"LINEAR\"}}}]}]");
    const auto StrokeDocument = [&](const FString& TargetExtra, bool bChangedImage, bool bRedundantClose = false)
    {
        const auto StrokeLeaf = [&](const TCHAR* Id, const FString& Paint, const TCHAR* Style, const FString& ExtraFields)
        {
            return MakeLeafNode(Id, TEXT("Outline"), TEXT("RECTANGLE"), 0, 0, 20, 20,
                StrokeFill + TEXT(",\"strokeWeight\":1,\"strokeAlign\":\"INSIDE\",\"strokes\":") + Paint
                + TEXT(",\"design2umgPluginData\":{\"strokeWeight\":1,\"strokeAlign\":\"INSIDE\",\"strokes\":") + Paint
                + TEXT(",\"strokeStyleId\":\"") + Style + TEXT("\"}") + ExtraFields);
        };
        const auto Photo = [](const TCHAR* Id, const TCHAR* ImageRef)
        {
            return MakeLeafNode(Id, TEXT("Photo"), TEXT("RECTANGLE"), 0, 0, 20, 20,
                FString::Printf(TEXT(",\"fills\":[{\"type\":\"IMAGE\",\"imageRef\":\"%s\",\"scaleMode\":\"FILL\"}]"), ImageRef));
        };
        const auto Contour = [&](const TCHAR* Id, bool bExplicitClose)
        {
            return MakeLeafNode(Id, TEXT("Contour"), TEXT("VECTOR"), 0, 0, 20, 20, StrokeFill
                + (bExplicitClose
                    ? TEXT(",\"fillGeometry\":[{\"path\":\"M0 0 C20 0 20 20 0 0 L0 0 Z\",\"windingRule\":\"NONZERO\"}]")
                    : TEXT(",\"fillGeometry\":[{\"path\":\"M0 0 C20 0 20 20 0 0 Z\",\"windingRule\":\"NONZERO\"}]")));
        };
        return MakeFigmaDocJson(
            MakeContainerNode(TEXT("stroke_page_a"), TEXT("Source"), TEXT("FRAME"), 0, 0, 40, 40,
                StrokeLeaf(TEXT("source_outline"), SolidStroke, TEXT("solid-style"), FString())
                + (bRedundantClose ? TEXT(",") + Contour(TEXT("source_contour"), false) : FString())
                + (bChangedImage ? TEXT(",") + Photo(TEXT("source_photo"), TEXT("image_a")) : FString()), StrokeAction)
            + TEXT(",") + MakeContainerNode(TEXT("stroke_page_b"), TEXT("Target"), TEXT("FRAME"), 0, 0, 40, 40,
                StrokeLeaf(TEXT("target_outline"), GradientStroke, TEXT("gradient-style"), TargetExtra)
                + (bRedundantClose ? TEXT(",") + Contour(TEXT("target_contour"), true) : FString())
                + (bChangedImage ? TEXT(",") + Photo(TEXT("target_photo"), TEXT("image_b")) : FString())),
            TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\""));
    };
	FString StrokePresenceDocument = StrokeDocument(FString(), false).Replace(*(TEXT("\"strokes\":") + SolidStroke), TEXT("\"strokes\":[]"));
	const int32 StrokeTargetOffset = StrokePresenceDocument.Find(TEXT("\"id\":\"target_outline\""));
	StrokePresenceDocument = StrokePresenceDocument.Left(StrokeTargetOffset)
		+ StrokePresenceDocument.Mid(StrokeTargetOffset).Replace(TEXT("\"strokeWeight\":1"), TEXT("\"strokeWeight\":2"));
	if (!TestTrue(TEXT("Absent-to-native stroke endpoint parses"), FFigmaParser::ParseDocument(StrokePresenceDocument, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Stroke presence uses source-timed endpoints after full matching"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("stroke_page_a")
			&& Binding.PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE") && Binding.PrimaryStep().Transition.LocalEndpoints.Num() == 1
			&& Binding.PrimaryStep().Transition.bFixedViewportLayout && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty()
			&& FMath::IsNearlyEqual(Binding.PrimaryStep().Transition.DurationSeconds, 0.2f); }));
	const FString SizedStrokeFields = StrokeFill + TEXT(",\"strokeWeight\":1,\"strokeAlign\":\"INSIDE\",\"strokes\":");
	const FString SizedStrokeDocument = MakeFigmaDocJson(
		MakeContainerNode(TEXT("stroke_page_a"), TEXT("Source"), TEXT("FRAME"), 0, 0, 40, 40,
			MakeLeafNode(TEXT("source_outline"), TEXT("Outline"), TEXT("RECTANGLE"), 0, 0, 20, 20, SizedStrokeFields + SolidStroke), StrokeAction)
		+ TEXT(",") + MakeContainerNode(TEXT("stroke_page_b"), TEXT("Target"), TEXT("FRAME"), 0, 0, 40, 40,
			MakeLeafNode(TEXT("target_outline"), TEXT("Outline"), TEXT("RECTANGLE"), 0, 0, 40, 20, SizedStrokeFields + GradientStroke)),
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\""));
	if (!TestTrue(TEXT("Fixed physical stroke with a resized native surface parses"), FFigmaParser::ParseDocument(SizedStrokeDocument, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Local surface paint preserves independent endpoint layout and native stroke dimensions"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding)
		{
			const auto& Plan = Binding.PrimaryStep().Transition;
			return Binding.TriggerWidgetId == TEXT("stroke_page_a") && Plan.Diagnostic.IsEmpty() && Plan.Type == TEXT("SMART_ANIMATE")
				&& Plan.bFixedViewportLayout && Plan.LocalEndpoints.Num() == 1 && !Plan.LocalEndpoints[0].bText
				&& Plan.WidgetTargets.ContainsByPredicate([](const FUMGPrototypeWidgetAnimationTarget& Item)
					{ return Item.NodeId == TEXT("source_outline") && Item.Scale.Equals(FVector2D(2, 1)); })
				&& Plan.LayoutTargets.ContainsByPredicate([](const FUMGPrototypeLayoutTarget& Item)
					{ return Item.NodeId == TEXT("source_outline__local_content") && Item.DestinationWidgetId == TEXT("target_outline__local_content"); });
		}));
	const auto* SizedStrokeOwner = FindEndpoint(Data.RootWidget, TEXT("source_outline"));
	TestTrue(TEXT("Own-paint endpoints remain behind the one original content child"), SizedStrokeOwner
		&& SizedStrokeOwner->SourceVisualChildIndex == 0 && SizedStrokeOwner->Children.Num() == 3
		&& SizedStrokeOwner->Children.Last().Id == TEXT("source_outline__local_content"));
    if (!TestTrue(TEXT("Native stroke paint endpoint fixture parses"), FFigmaParser::ParseDocument(StrokeDocument(FString(), false), Document))) return false;
    Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    TestTrue(TEXT("Same-geometry solid-to-linear stroke uses native endpoints and retains source timing"),
        Data.PrototypeNavigation().ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
        {
            const auto& Plan = Binding.PrimaryStep().Transition;
            return Binding.TriggerWidgetId == TEXT("stroke_page_a") && Plan.Type == TEXT("SMART_ANIMATE")
                && Plan.LocalEndpoints.Num() == 1 && Plan.bFixedViewportLayout && Plan.Diagnostic.IsEmpty()
                && FMath::IsNearlyEqual(Plan.DurationSeconds, 0.2f) && !Plan.WidgetTargets.IsEmpty();
        }));
    if (!TestTrue(TEXT("Same-size equivalent closure fixture parses"), FFigmaParser::ParseDocument(StrokeDocument(FString(), false, true), Document))) return false;
    Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    TestTrue(TEXT("Equivalent vector closure does not block the native stroke dissolve"),
        Data.PrototypeNavigation().ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
        {
            return Binding.TriggerWidgetId == TEXT("stroke_page_a") && Binding.PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE")
                && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && Binding.PrimaryStep().Transition.LocalEndpoints.Num() == 1
                && Binding.PrimaryStep().Transition.bFixedViewportLayout;
        }));
    const FString ChangedVectorContour = StrokeDocument(FString(), false, true).Replace(
        TEXT("M0 0 C20 0 20 20 0 0 L0 0 Z"), TEXT("M0 0 C19 0 20 20 0 0 L0 0 Z"));
    if (!TestTrue(TEXT("Same-size changed vector contour fixture parses"), FFigmaParser::ParseDocument(ChangedVectorContour, Document))) return false;
    Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    TestTrue(TEXT("A real same-size vector contour change still rejects the action"), Rejected(Data, TEXT("stroke_page_a"), TEXT("fillGeometry")));
    if (!TestTrue(TEXT("Stroke and image-change fixture parses"), FFigmaParser::ParseDocument(StrokeDocument(FString(), true), Document))) return false;
    Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    TestTrue(TEXT("Local stroke endpoints cannot hide a later excluded image-fill animation"), Rejected(Data, TEXT("stroke_page_a"), TEXT("paint")));
    if (!TestTrue(TEXT("Stroke and contour-change fixture parses"), FFigmaParser::ParseDocument(
        StrokeDocument(TEXT(",\"fillGeometry\":[{\"path\":\"M0 0L20 0L0 20Z\",\"windingRule\":\"NONZERO\"}]"), false), Document))) return false;
    Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    TestTrue(TEXT("Stroke paint classification does not admit a changed contour"), Rejected(Data, TEXT("stroke_page_a"), TEXT("")));
	const auto VectorLeaf = [&](const TCHAR* Id, int32 Size, const TCHAR* FillPath, const TCHAR* StrokePath)
	{
		const FString Fields = StrokeFill + TEXT(",\"strokeWeight\":1,\"strokeAlign\":\"CENTER\",\"strokeCap\":\"NONE\",\"strokeJoin\":\"MITER\",\"strokes\":") + SolidStroke
			+ TEXT(",\"fillGeometry\":[{\"windingRule\":\"NONZERO\",\"path\":\"") + FillPath + TEXT("\"}],\"strokeGeometry\":[{\"windingRule\":\"EVENODD\",\"path\":\"") + StrokePath + TEXT("\"}]");
		const FString Network = FString::Printf(TEXT(",\"vectorNetwork\":{\"vertices\":[{\"x\":0,\"y\":0},{\"x\":%d,\"y\":0},{\"x\":%d,\"y\":%d},{\"x\":0,\"y\":%d}],\"segments\":[{\"start\":0,\"end\":1},{\"start\":1,\"end\":2},{\"start\":2,\"end\":3},{\"start\":3,\"end\":0}],\"regions\":[{\"windingRule\":\"NONZERO\",\"loops\":[[0,1,2,3]]"), Size, Size, Size, Size)
			+ StrokeFill + TEXT(",\"fillStyleId\":\"") + Id + TEXT("\"}]},\"vectorPaths\":[{\"windingRule\":\"NONZERO\",\"data\":\"") + FillPath + TEXT("\"}]");
		return MakeLeafNode(Id, TEXT("Glyph"), TEXT("VECTOR"), 0, 0, Size, Size,
			Fields + TEXT(",\"design2umgPluginData\":{") + Fields.Mid(1) + Network + TEXT("}"));
	};
	const FString VectorDocument = MakeFigmaDocJson(
		MakeContainerNode(TEXT("stroke_page_a"), TEXT("Source"), TEXT("FRAME"), 0, 0, 80, 80,
			VectorLeaf(TEXT("vector_a"), 20, TEXT("M0 0L20 0L20 20L0 20Z"), TEXT("M-0.5 -0.5L20.5 -0.5L20.5 20.5L-0.5 20.5Z M0.5 0.5L0.5 19.5L19.5 19.5L19.5 0.5Z")), StrokeAction)
		+ TEXT(",") + MakeContainerNode(TEXT("stroke_page_b"), TEXT("Target"), TEXT("FRAME"), 0, 0, 80, 80,
			VectorLeaf(TEXT("vector_b"), 40, TEXT("M0 0L40 0L40 40L0 40Z"), TEXT("M-0.5 -0.5L40.5 -0.5L40.5 40.5L-0.5 40.5Z M0.5 0.5L0.5 39.5L39.5 39.5L39.5 0.5Z"))),
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\""));
	if (!TestTrue(TEXT("Fixed-width vector endpoint fixture parses"), FFigmaParser::ParseDocument(VectorDocument, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Equivalent vector centers retain both static physical-width paints and SMART layout"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding)
		{ return Binding.TriggerWidgetId == TEXT("stroke_page_a") && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty()
			&& Binding.PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE") && Binding.PrimaryStep().Transition.bFixedViewportLayout
			&& Binding.PrimaryStep().Transition.LocalEndpoints.Num() == 1 && Binding.PrimaryStep().Transition.LocalEndpoints[0].bFixedPaintSize; }));
	const auto* StaticVectorA = SourceVisual(FindEndpoint(Data.RootWidget, TEXT("vector_a")));
	const auto* StaticVectorB = SourceVisual(FindEndpoint(Data.RootWidget, TEXT("vector_b")));
	const auto* VectorOwnerA = FindEndpoint(Data.RootWidget, TEXT("vector_a"));
	const auto* VectorOwnerB = FindEndpoint(Data.RootWidget, TEXT("vector_b"));
	const auto* StrokeVectorA = StaticVectorA ? StaticVectorA->Children.FindByPredicate(
		[](const FUMGWidgetNode& Child) { return Child.Path.SourcePaintGeometry.IsSet() && Child.Path.SourcePaintGeometry->StrokePaint.IsSet(); }) : nullptr;
	const auto* StrokeVectorB = StaticVectorB ? StaticVectorB->Children.FindByPredicate(
		[](const FUMGWidgetNode& Child) { return Child.Path.SourcePaintGeometry.IsSet() && Child.Path.SourcePaintGeometry->StrokePaint.IsSet(); }) : nullptr;
	TestTrue(TEXT("Vector endpoints keep logical canvas and exact expanded stroke paths"), VectorOwnerA && VectorOwnerB
		&& VectorOwnerA->Bounds.X == -0.5f && VectorOwnerA->Bounds.Y == -0.5f
		&& VectorOwnerB->Bounds.X == -0.5f && VectorOwnerB->Bounds.Y == -0.5f
		&& VectorOwnerA->Bounds.W == 21 && VectorOwnerB->Bounds.W == 41
		&& StaticVectorA && StaticVectorB && StaticVectorA->Bounds.W == 21 && StaticVectorB->Bounds.W == 41
		&& StrokeVectorA && StrokeVectorB
		&& StrokeVectorA->Path.CanvasSize.IsSet() && StrokeVectorA->Path.CanvasSize.GetValue() == FVector2D(21, 21)
		&& StrokeVectorB->Path.CanvasSize.IsSet() && StrokeVectorB->Path.CanvasSize.GetValue() == FVector2D(41, 41)
		&& StrokeVectorA->Path.SourcePaintGeometry.IsSet() && StrokeVectorB->Path.SourcePaintGeometry.IsSet()
		&& StrokeVectorA->Path.SourcePaintGeometry->Geometry.FillPaths.IsSet()
		&& StrokeVectorB->Path.SourcePaintGeometry->Geometry.FillPaths.IsSet()
		&& StrokeVectorA->Path.SourcePaintGeometry->Geometry.FillPaths->Num() == 1
		&& StrokeVectorB->Path.SourcePaintGeometry->Geometry.FillPaths->Num() == 1
		&& (*StrokeVectorA->Path.SourcePaintGeometry->Geometry.FillPaths)[0].Path == TEXT("M0 0L20 0L20 20L0 20Z")
		&& (*StrokeVectorB->Path.SourcePaintGeometry->Geometry.FillPaths)[0].Path == TEXT("M0 0L40 0L40 40L0 40Z")
		&& StrokeVectorA->Path.SourcePaintGeometry->Geometry.StrokePaths.IsSet()
		&& StrokeVectorB->Path.SourcePaintGeometry->Geometry.StrokePaths.IsSet()
		&& StrokeVectorA->Path.SourcePaintGeometry->Geometry.StrokePaths->Num() == 1
		&& StrokeVectorB->Path.SourcePaintGeometry->Geometry.StrokePaths->Num() == 1
		&& (*StrokeVectorA->Path.SourcePaintGeometry->Geometry.StrokePaths)[0].Path == TEXT("M-0.5 -0.5L20.5 -0.5L20.5 20.5L-0.5 20.5Z M0.5 0.5L0.5 19.5L19.5 19.5L19.5 0.5Z")
		&& (*StrokeVectorB->Path.SourcePaintGeometry->Geometry.StrokePaths)[0].Path == TEXT("M-0.5 -0.5L40.5 -0.5L40.5 40.5L-0.5 40.5Z M0.5 0.5L0.5 39.5L39.5 39.5L39.5 0.5Z"));
	if (!TestTrue(TEXT("Changed vector center fixture parses"), FFigmaParser::ParseDocument(
		VectorDocument.Replace(TEXT("M0 0L40 0L40 40L0 40Z"), TEXT("M0 0L40 0L35 40L0 40Z")), Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("A fixed-width stroke cannot admit a true center morph"), Rejected(Data, TEXT("stroke_page_a"), TEXT("")));
	const FString SourcePlugin = TEXT("{\"id\":\"instance_0_surface\",\"name\":\"Surface\",\"type\":\"RECTANGLE\",\"reactions\":[],")
		+ FString(TEXT("\"absoluteTransform\":[[1,0,10],[0,1,10]],\"x\":10,\"y\":10,\"width\":40,\"height\":20,\"opacity\":1,\"cornerSmoothing\":0,"))
		+ TEXT("\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":0,\"b\":0}}],") + SourceMotion + TEXT("}");
	const FString TargetPlugin = TEXT("{\"id\":\"target_surface\",\"name\":\"Surface\",\"type\":\"RECTANGLE\",\"reactions\":[{\"trigger\":null,\"actions\":[]}],")
		+ FString(TEXT("\"absoluteTransform\":[[0,-1,90],[1,0,30]],\"x\":50,\"y\":30,\"width\":80,\"height\":40,\"opacity\":0.4,\"cornerSmoothing\":0,"))
		+ TEXT("\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":0,\"g\":0,\"b\":1}}],") + TargetMotion + TEXT("}");
	const auto MakePluginFixture = [&](const FString& SourceRaw, const FString& TargetRaw)
	{
		return MakeNativeWidgetAnimationDocument()
			.Replace(TEXT("\"id\":\"instance_0_surface\""), *(TEXT("\"id\":\"instance_0_surface\",\"design2umgPluginData\":") + SourceRaw + TEXT(",") + SourceMotion))
			.Replace(TEXT("\"id\":\"target_surface\""), *(TEXT("\"id\":\"target_surface\",\"design2umgPluginData\":") + TargetRaw + TEXT(",") + TargetMotion));
	};
	const FString PluginFixture = MakePluginFixture(SourcePlugin, TargetPlugin);
	if (!TestTrue(TEXT("Complete source snapshot fixture parses"), FFigmaParser::ParseDocument(PluginFixture, Document))) return false;
	const auto RetainedPlugin = Document.Children[0]->Children[0]->Children[0]->Source().Prototype.AnimationSource->GetObjectField(TEXT("design2umgPluginData"));
	TestEqual(TEXT("Animation matching retains the original raw identity"), RetainedPlugin->GetStringField(TEXT("id")), FString(TEXT("instance_0_surface")));
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Raw identity and geometry copies plus inactive timeline identity do not reject real color and movement"),
		Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.Diagnostic.IsEmpty()
		&& Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.WidgetTargets.Num() == 1
		&& Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.WidgetTargets[0].bColor);
	const FString AliasFixture = MakePluginFixture(
		SourcePlugin.Replace(TEXT("\"cornerSmoothing\":0"), TEXT("\"cornerSmoothing\":0,\"inferredAutoLayout\":null")),
		TargetPlugin.Replace(TEXT("\"cornerSmoothing\":0"), TEXT("\"cornerSmoothing\":0,\"inferredAutoLayout\":{\"layoutMode\":\"HORIZONTAL\"}")))
		.Replace(TEXT("\"id\":\"instance_0_surface\",\"design2umgPluginData\":"),
			TEXT("\"id\":\"instance_0_surface\",\"background\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":0,\"b\":0,\"a\":1}}],\"design2umgPluginData\":"))
		.Replace(TEXT("\"id\":\"target_surface\",\"design2umgPluginData\":"),
			TEXT("\"id\":\"target_surface\",\"background\":[{\"type\":\"SOLID\",\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1}}],\"design2umgPluginData\":"));
	if (!TestTrue(TEXT("Paint alias and inferred layout fixture parses"), FFigmaParser::ParseDocument(AliasFixture, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Equivalent background paints and inferred suggestions preserve real color animation"),
		Data.PrototypeNavigation().ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
		{ return Binding.TriggerWidgetId == TEXT("instance_0") && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && !Binding.PrimaryStep().Transition.WidgetTargets.IsEmpty(); }));
	const FString ConflictingAlias = AliasFixture.Replace(TEXT("\"background\":[{\"type\":\"SOLID\",\"color\":{\"r\":0,\"g\":0,\"b\":1"),
		TEXT("\"background\":[{\"type\":\"SOLID\",\"color\":{\"r\":0,\"g\":1,\"b\":1"));
	if (!TestTrue(TEXT("Conflicting paint alias fixture parses"), FFigmaParser::ParseDocument(ConflictingAlias, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Ignored background aliases cannot replace the canonical blue fill"),
		Data.PrototypeNavigation().ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
		{ return Binding.TriggerWidgetId == TEXT("instance_0") && Binding.PrimaryStep().Transition.WidgetTargets.ContainsByPredicate(
			[](const FUMGPrototypeWidgetAnimationTarget& Target) { return Target.bColor && Target.EndColor.B == 1 && Target.EndColor.G == 0; }); }));

    const FString FrameContours = MakeNativeWidgetAnimationDocument()
        .Replace(TEXT("\"type\":\"RECTANGLE\""), TEXT("\"type\":\"FRAME\""))
        .Replace(TEXT("\"id\":\"instance_0_surface\""), TEXT("\"id\":\"instance_0_surface\",\"fillGeometry\":[{\"path\":\"M0 0L40 0L40 20L0 20L0 0Z\",\"windingRule\":\"NONZERO\"}]"))
        .Replace(TEXT("\"id\":\"target_surface\""), TEXT("\"id\":\"target_surface\",\"fillGeometry\":[{\"path\":\"M0 0L80 0L80 40L0 40Z\",\"windingRule\":\"NONZERO\"}]"));
    if (!TestTrue(TEXT("Derived frame contour fixture parses"), FFigmaParser::ParseDocument(FrameContours, Document))) return false;
    Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    TestTrue(TEXT("Frame contours follow real bounds without requesting shape replacement"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
        [](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("instance_0")
            && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && Binding.PrimaryStep().Transition.AddedLayers.IsEmpty() && !Binding.PrimaryStep().Transition.WidgetTargets.IsEmpty(); }));
	const auto FrameEndpointDocument = [&](FString Content, bool bResize)
	{
		const FString SourceFields = StrokeFill + TEXT(",\"size\":{\"x\":40,\"y\":40},\"cornerRadius\":4,\"cornerSmoothing\":0.6,\"fillGeometry\":[{\"path\":\"M4 0H36Q40 0 40 4V36Q40 40 36 40H4Q0 40 0 36V4Q0 0 4 0Z\",\"windingRule\":\"NONZERO\"}]");
		const FString TargetFields = StrokeFill + (bResize
			? TEXT(",\"size\":{\"x\":40,\"y\":49},\"cornerRadius\":4,\"cornerSmoothing\":0.6,\"fillGeometry\":[{\"path\":\"M4 0H36Q40 0 40 4V45Q40 49 36 49H4Q0 49 0 45V4Q0 0 4 0Z\",\"windingRule\":\"NONZERO\"}]")
			: TEXT(",\"size\":{\"x\":40,\"y\":40},\"cornerRadius\":5,\"cornerSmoothing\":0.6,\"fillGeometry\":[{\"path\":\"M5 0H35Q40 0 40 5V35Q40 40 35 40H5Q0 40 0 35V5Q0 0 5 0Z\",\"windingRule\":\"NONZERO\"}]"));
		const auto CornerMirrors = [](int32 Radius, bool bLocked)
		{
			return FString::Printf(TEXT(",\"design2umgPluginData\":{\"topLeftRadius\":%d,\"topRightRadius\":%d,\"bottomRightRadius\":%d,\"bottomLeftRadius\":%d,\"locked\":%s}"),
				Radius, Radius, Radius, Radius, bLocked ? TEXT("true") : TEXT("false"));
		};
		Content.ReplaceInline(TEXT("\"id\":\"stroke_page_a\""), *(TEXT("\"id\":\"stroke_page_a\"") + SourceFields + CornerMirrors(4, true) + TEXT(",\"locked\":true")));
		Content.ReplaceInline(TEXT("\"id\":\"stroke_page_b\""), *(TEXT("\"id\":\"stroke_page_b\"") + TargetFields + CornerMirrors(bResize ? 4 : 5, false)));
		if (bResize) Content.ReplaceInline(TEXT("\"name\":\"Target\",\"type\":\"FRAME\",\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":40,\"height\":40}"),
			TEXT("\"name\":\"Target\",\"type\":\"FRAME\",\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":40,\"height\":49}"));
		return Content;
	};
	const FString FrameCornerDocument = FrameEndpointDocument(StrokeDocument(FString(), false), false);
	if (!TestTrue(TEXT("Native frame corner endpoints parse"), FFigmaParser::ParseDocument(FrameCornerDocument, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Native frame endpoints convert"), Data.ConversionError, FString())) return false;
	TestTrue(TEXT("Different frame corners retain source timing and local native endpoints"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("stroke_page_a")
			&& Binding.PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE") && !Binding.PrimaryStep().Transition.LocalEndpoints.IsEmpty()
			&& Binding.PrimaryStep().Transition.bFixedViewportLayout && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty()
			&& FMath::IsNearlyEqual(Binding.PrimaryStep().Transition.DurationSeconds, 0.2f); }));
	const auto* SourceCurve = FindEndpoint(Data.RootWidget, TEXT("stroke_page_a__local_surface__contour_paints"));
	const auto* TargetCurve = FindEndpoint(Data.RootWidget, TEXT("stroke_page_b__local_surface__contour_paints"));
	TestTrue(TEXT("Source and target coverage retain their exact exported contours and local canvas"), SourceCurve && TargetCurve
		&& SourceCurve->Type == UMGWidgetTypes::Image && TargetCurve->Type == UMGWidgetTypes::Image
		&& SourceCurve->Effect.GeometryMaskPaths.IsSet() && SourceCurve->Effect.GeometryMaskPaths->Num() == 1
		&& TargetCurve->Effect.GeometryMaskPaths.IsSet() && TargetCurve->Effect.GeometryMaskPaths->Num() == 1
		&& SourceCurve->Effect.GeometryMaskPaths.GetValue()[0].Path.StartsWith(TEXT("M4 0"))
		&& TargetCurve->Effect.GeometryMaskPaths.GetValue()[0].Path.StartsWith(TEXT("M5 0"))
		&& SourceCurve->Effect.GeometryMaskSize.Get(FVector2D::ZeroVector).Equals(FVector2D(40, 40)));
	if (!TestTrue(TEXT("Ordinary native corner endpoints retain their brush contract"), FFigmaParser::ParseDocument(
		FrameCornerDocument.Replace(TEXT("\"cornerSmoothing\":0.6"), TEXT("\"cornerSmoothing\":0")), Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const auto* SourceFrame = FindEndpoint(Data.RootWidget, TEXT("stroke_page_a"));
	const auto* TargetFrame = FindEndpoint(Data.RootWidget, TEXT("stroke_page_b"));
	const FUMGWidgetNode* SourceBorder = SourceVisual(SourceFrame);
	TargetFrame = SourceVisual(TargetFrame);
	TestTrue(TEXT("Both native frame endpoints keep their distinct static radii"), SourceBorder && TargetFrame && TargetFrame->Type == UMGWidgetTypes::Border
		&& SourceBorder->Brush.OutlineCornerRadius.IsSet() && SourceBorder->Brush.OutlineCornerRadius.GetValue() == 4.0f
		&& TargetFrame->Brush.OutlineCornerRadius.IsSet() && TargetFrame->Brush.OutlineCornerRadius.GetValue() == 5.0f);
	if (!TestTrue(TEXT("Contradictory plugin corner mirror parses"), FFigmaParser::ParseDocument(
		FrameCornerDocument.Replace(TEXT("\"topLeftRadius\":5"), TEXT("\"topLeftRadius\":6")), Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TMap<FString, TSharedPtr<FDesignNode>> CanonicalNodes;
	Document.BuildReferenceNodeIndex(CanonicalNodes);
	const auto CornerTarget = CanonicalNodes.FindRef(TEXT("stroke_page_b"));
	if (!TestTrue(TEXT("The canonical corner target exists"), CornerTarget.IsValid())) return false;
	TestEqual(TEXT("Ignored corner mirrors do not replace the canonical radius"), CornerTarget->Visual.CornerRadius.Uniform, 5.0f);
	TestTrue(TEXT("Canonical corner changes retain their native endpoint animation"),
		Data.PrototypeNavigation().ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
		{ return Binding.TriggerWidgetId == TEXT("stroke_page_a") && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && !Binding.PrimaryStep().Transition.LocalEndpoints.IsEmpty(); }));
	if (!TestTrue(TEXT("Corner mirror without canonical owner parses"), FFigmaParser::ParseDocument(
		FrameCornerDocument.Replace(TEXT(",\"cornerRadius\":5"), TEXT("")), Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	Document.BuildReferenceNodeIndex(CanonicalNodes);
	TestFalse(TEXT("A mirror alone cannot introduce a parsed corner"), CanonicalNodes.FindChecked(TEXT("stroke_page_b"))->Visual.CornerRadius.HasRadius());
	if (!TestTrue(TEXT("Fixed rounded frame resize contours parse"), FFigmaParser::ParseDocument(
		FrameEndpointDocument(StrokeDocument(FString(), false), true), Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Size-derived smooth frame contours use endpoints rather than property interpolation"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("stroke_page_a")
			&& Binding.PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE") && !Binding.PrimaryStep().Transition.LocalEndpoints.IsEmpty()
			&& Binding.PrimaryStep().Transition.bFixedViewportLayout && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty(); }));
	if (!TestTrue(TEXT("Frame endpoint with later image change parses"), FFigmaParser::ParseDocument(
		FrameEndpointDocument(StrokeDocument(FString(), true), false), Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Local frame endpoints cannot hide an image paint change"), Rejected(Data, TEXT("stroke_page_a"), TEXT("paint")));
	if (!TestTrue(TEXT("Frame endpoint with later vector change parses"), FFigmaParser::ParseDocument(
		FrameEndpointDocument(ChangedVectorContour, false), Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Local frame endpoints cannot hide a real vector contour change"), Rejected(Data, TEXT("stroke_page_a"), TEXT("fillGeometry")));
	if (!TestTrue(TEXT("Frame endpoint with smoothing change parses"), FFigmaParser::ParseDocument(
		FrameCornerDocument.Replace(TEXT("\"cornerRadius\":5,\"cornerSmoothing\":0.6"), TEXT("\"cornerRadius\":5,\"cornerSmoothing\":0.7")), Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Source smoothing endpoints dissolve only after full matching"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("stroke_page_a")
			&& Binding.PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE") && !Binding.PrimaryStep().Transition.LocalEndpoints.IsEmpty()
			&& Binding.PrimaryStep().Transition.bFixedViewportLayout && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty(); }));
	const FString CurveClipDocument = FrameCornerDocument.Replace(TEXT("\"id\":\"stroke_page_b\""), TEXT("\"id\":\"stroke_page_b\",\"clipsContent\":true"));
	if (!TestTrue(TEXT("Source curve clipping endpoint parses"), FFigmaParser::ParseDocument(CurveClipDocument, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const auto* Clipped = FindEndpoint(Data.RootWidget, TEXT("stroke_page_b"));
	const auto* ClipCapture = FindEndpoint(Data.RootWidget, TEXT("stroke_page_b__shared_capture"));
	TestTrue(TEXT("Curve clipping retains one real child subtree under the shared native mask"), Clipped
		&& Clipped->Type == UMGWidgetTypes::Overlay && Clipped->SourceVisualChildIndex == 0 && Clipped->Children.Num() == 2
		&& Clipped->Children[0].Id == TEXT("stroke_page_b__local_surface")
		&& Clipped->Children[1].Id == TEXT("stroke_page_b__shared_capture")
		&& ClipCapture && ClipCapture->Type == UMGWidgetTypes::RetainerBox && ClipCapture->Effect.SharedCapture.IsSet()
		&& ClipCapture->Effect.SharedCapture->Source.bClipContent && ClipCapture->Effect.SharedCapture->Source.ClipPaths.IsSet()
		&& !ClipCapture->Effect.SharedCapture->Source.ClipPaths->IsEmpty()
		&& ClipCapture->Effect.SharedCapture->Source.Size == FVector2D(40, 40)
		&& ClipCapture->Children.Num() == 1 && ClipCapture->Children[0].Type == UMGWidgetTypes::CanvasPanel
		&& ClipCapture->Children[0].Children.Num() == 1
		&& ClipCapture->Children[0].Children[0].Id == TEXT("stroke_page_b__local_content")
		&& ClipCapture->Children[0].Children[0].Render.Clipping.Get(FString()) == TEXT("ClipToBounds")
		&& FindEndpoint(ClipCapture->Children[0].Children[0], TEXT("target_outline")) != nullptr
		&& Data.PrototypeNavigation().ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
		{ return Binding.TriggerWidgetId == TEXT("stroke_page_a") && Binding.PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE")
			&& Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && Binding.PrimaryStep().Transition.bFixedViewportLayout
			&& !Binding.PrimaryStep().Transition.LayoutTargets.IsEmpty() && Binding.PrimaryStep().Transition.SharedCaptures.Num() == 1; }));
	const FString EmptyPaintClipDocument = CurveClipDocument.Replace(*StrokeFill, TEXT(",\"fills\":[]"))
		.Replace(TEXT("\"cornerSmoothing\":0.6"), TEXT("\"cornerSmoothing\":0"));
	if (!TestTrue(TEXT("Clipped native owner with an empty fill stack parses"), FFigmaParser::ParseDocument(EmptyPaintClipDocument, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const auto* EmptyPaintSource = FindEndpoint(Data.RootWidget, TEXT("stroke_page_a__shared_capture"));
	const auto* EmptyPaintTarget = FindEndpoint(Data.RootWidget, TEXT("stroke_page_b__shared_capture"));
	const auto* EmptyPaintBinding = Data.PrototypeNavigation().ClickBindings.FindByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("stroke_page_a"); });
	const auto* EmptyPaintOwner = EmptyPaintBinding && !EmptyPaintBinding->PrimaryStep().Transition.LayoutOriginalWidgetId.IsEmpty()
		? FindEndpoint(Data.RootWidget, EmptyPaintBinding->PrimaryStep().Transition.LayoutOriginalWidgetId) : nullptr;
	TestTrue(TEXT("Empty own fill preserves both real capture owners and their children"), Data.ConversionError.IsEmpty()
		&& EmptyPaintSource && EmptyPaintTarget
		&& FindEndpoint(*EmptyPaintSource, TEXT("source_outline")) && FindEndpoint(*EmptyPaintTarget, TEXT("target_outline"))
		&& EmptyPaintSource->Effect.SharedCapture.IsSet() && EmptyPaintTarget->Effect.SharedCapture.IsSet()
		&& EmptyPaintOwner && EmptyPaintOwner->SourceVisualChildIndex == INDEX_NONE
		&& EmptyPaintOwner->Children.Num() == 1
		&& EmptyPaintOwner->Children[0].Id == TEXT("stroke_page_a__shared_capture")
		&& &EmptyPaintOwner->Children[0] == EmptyPaintSource
		&& !FindEndpoint(Data.RootWidget, TEXT("stroke_page_a__local_surface"))
		&& !FindEndpoint(Data.RootWidget, TEXT("stroke_page_b__local_surface")));
	TestTrue(TEXT("Empty own fill retains the real SMART capture pair without degradation"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("stroke_page_a")
			&& Binding.PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE") && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty()
			&& Binding.PrimaryStep().Transition.SharedCaptures.Num() == 1
			&& !Binding.PrimaryStep().Transition.SharedCaptures[0].CaptureWidgetId.IsEmpty()
			&& !Binding.PrimaryStep().Transition.SharedCaptures[0].ContentWidgetId.IsEmpty()
			&& !Binding.PrimaryStep().Transition.LocalEndpoints.ContainsByPredicate([](const FUMGPrototypeLocalEndpoint& Endpoint)
			{ return Endpoint.NodeId == TEXT("stroke_page_a"); }); }));
	const FString RectangleEndpoints = FrameEndpointDocument(MakeFigmaDocJson(
		MakeLeafNode(TEXT("stroke_page_a"), TEXT("Source"), TEXT("RECTANGLE"), 0, 0, 40, 40, StrokeAction)
		+ TEXT(",") + MakeLeafNode(TEXT("stroke_page_b"), TEXT("Target"), TEXT("RECTANGLE"), 0, 0, 40, 40),
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\"")), false);
	if (!TestTrue(TEXT("The same source contour contract applies to rectangular paint surfaces"), FFigmaParser::ParseDocument(RectangleEndpoints, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Native rectangle corner contours use the same local endpoint transition"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("stroke_page_a")
			&& Binding.PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE") && !Binding.PrimaryStep().Transition.LocalEndpoints.IsEmpty()
			&& Binding.PrimaryStep().Transition.bFixedViewportLayout && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty(); }));
	const FString PrimitiveSurface = MakeFigmaDocJson(MakeLeafNode(TEXT("primitive_surface"), TEXT("Rounded surface"),
		TEXT("RECTANGLE"), 0, 0, 40, 40, TEXT(",\"rectangleCornerRadii\":[8,4,2,0],\"cornerSmoothing\":0.35,\"clipsContent\":true,\"strokeWeight\":1,\"strokeAlign\":\"INSIDE\",\"strokes\":[{\"type\":\"SOLID\",\"color\":{\"r\":0,\"g\":0,\"b\":0,\"a\":1}}]") + StrokeFill));
	if (!TestTrue(TEXT("Source parameter contour parses without exported paths"), FFigmaParser::ParseDocument(PrimitiveSurface, Document))) return false;
	FDesignNode MeasuredCanvas = *Document.Children[0];
	MeasuredCanvas.Transform.Width = FDesignSize::Fit(149);
	FUMGEffectData ParameterContour;
	FString CanvasError;
	TestTrue(TEXT("Measured HUG size transfers to parameter contour"), VisualMapper::BuildSourceContour(MeasuredCanvas, ParameterContour, CanvasError)
		&& ParameterContour.GeometryMaskSize.Get(FVector2D::ZeroVector).Equals(FVector2D(149, 40)));
	MeasuredCanvas.Shape.PaintGeometry.FillPaths = TArray<FDesignGeometryPath>{{TEXT("M0 0L40 0L40 40L0 40Z"), TEXT("nonzero")}};
	FUMGEffectData ExportedContour;
	TestTrue(TEXT("Exported paths retain their source local canvas"), VisualMapper::BuildSourceContour(MeasuredCanvas, ExportedContour, CanvasError)
		&& ExportedContour.GeometryMaskSize.Get(FVector2D::ZeroVector).Equals(FVector2D(40, 40))
		&& ExportedContour.GeometryMaskPaths.GetValue()[0].Path == MeasuredCanvas.Shape.PaintGeometry.FillPaths.GetValue()[0].Path);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Source parameters use the existing contour conversion"), Data.ConversionError, FString());
	const auto* PrimitiveClip = FindEndpoint(Data.RootWidget, TEXT("primitive_surface__stroke_content"));
	const auto* PrimitiveStroke = FindEndpoint(Data.RootWidget, TEXT("primitive_surface_stroke0"));
	TestTrue(TEXT("Parameter contours preserve local canvas and independent aligned stroke"), PrimitiveClip && PrimitiveStroke
		&& PrimitiveClip->Type == UMGWidgetTypes::RetainerBox && PrimitiveClip->Effect.GeometryMaskPaths.IsSet()
		&& PrimitiveClip->Effect.GeometryMaskPaths->Num() == 1
		&& PrimitiveClip->Effect.GeometryMaskSize.Get(FVector2D::ZeroVector).Equals(FVector2D(40, 40))
		&& PrimitiveStroke->Path.Geometry.IsSet() && PrimitiveStroke->Path.StrokeThickness.Get(0) == 1.0f
		&& PrimitiveStroke->Path.StrokeAlignment.Get(FString()) == TEXT("inner"));
	const FString InstanceBoxContours = FrameContours.Replace(TEXT("\"name\":\"Surface\",\"type\":\"FRAME\""),
		TEXT("\"name\":\"Surface\",\"type\":\"INSTANCE\",\"componentId\":\"box-surface\""))
		.Replace(TEXT("\"id\":\"instance_0_surface\""), TEXT("\"id\":\"instance_0_surface\",\"design2umgPluginData\":{\"isAsset\":false}"))
		.Replace(TEXT("\"id\":\"target_surface\""), TEXT("\"id\":\"target_surface\",\"design2umgPluginData\":{\"isAsset\":true}"));
	if (!TestTrue(TEXT("Instance box derived contours parse"), FFigmaParser::ParseDocument(InstanceBoxContours, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (Data.PrototypeNavigation().ClickBindings.IsEmpty()) for (const auto& Warning : Data.Warnings) AddInfo(Warning);
	TestTrue(TEXT("Equivalent instance box contours retain native fixed layout mappings"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("instance_0")
			&& Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && Binding.PrimaryStep().Transition.AddedLayers.IsEmpty()
			&& Binding.PrimaryStep().Transition.LayoutTargets.ContainsByPredicate([](const FUMGPrototypeLayoutTarget& Target)
				{ return Target.NodeId == TEXT("instance_0_surface"); }); }));
	const FString ChangedInstanceContour = InstanceBoxContours.Replace(TEXT("M0 0L80 0L80 40L0 40Z"), TEXT("M0 0L80 0L72 40L0 40Z"));
	if (!TestTrue(TEXT("A non-box instance contour change parses"), FFigmaParser::ParseDocument(ChangedInstanceContour, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Instance box classification cannot hide changed geometry"), Rejected(Data, TEXT("instance_0"), TEXT("geometry")));
    FString VectorContours = FrameContours.Replace(TEXT("\"name\":\"Surface\",\"type\":\"FRAME\""), TEXT("\"name\":\"Surface\",\"type\":\"VECTOR\""));
    for (int32 Index = 1; Index < 4; ++Index)
    {
        const FString Identity = FString::Printf(TEXT("\"id\":\"instance_%d_surface\""), Index);
        VectorContours.ReplaceInline(*Identity, *(Identity + TEXT(",\"fillGeometry\":[{\"path\":\"M0 0L40 0L40 20L0 20Z\",\"windingRule\":\"NONZERO\"}]")));
    }
    if (!TestTrue(TEXT("Scaled vector contour fixture parses"), FFigmaParser::ParseDocument(VectorContours, Document))) return false;
    Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    TestTrue(*FString::Printf(TEXT("Scaled vector conversion: %s"), *Data.ConversionError), Data.ConversionError.IsEmpty());
    TestTrue(TEXT("Same normalized vector contour uses native size animation without shape replacement"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
        [](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("instance_0")
            && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && Binding.PrimaryStep().Transition.AddedLayers.IsEmpty()
            && Binding.PrimaryStep().Transition.WidgetTargets.ContainsByPredicate([](const FUMGPrototypeWidgetAnimationTarget& Target)
                { return Target.NodeId == TEXT("instance_0_surface") && Target.Scale.Equals(FVector2D(2, 2)); }); }));
	const auto NetworkPaintLeaf = [](const TCHAR* Id, const TCHAR* R, const TCHAR* B, const TCHAR* Style)
	{
		const FString Paint = FString::Printf(TEXT("[{\"type\":\"SOLID\",\"color\":{\"r\":%s,\"g\":0,\"b\":%s,\"a\":1}}]"), R, B);
		return MakeLeafNode(Id, TEXT("Vector"), TEXT("VECTOR"), 0, 0, 20, 20,
			TEXT(",\"size\":{\"x\":20,\"y\":20},\"fillGeometry\":[{\"path\":\"M0 0L20 0L20 20L0 20Z\",\"windingRule\":\"NONZERO\"}],\"fills\":") + Paint
			+ TEXT(",\"design2umgPluginData\":{\"fills\":") + Paint
			+ TEXT(",\"vectorNetwork\":{\"vertices\":[{\"x\":0,\"y\":0},{\"x\":20,\"y\":0},{\"x\":20,\"y\":20},{\"x\":0,\"y\":20}],\"segments\":[{\"start\":0,\"end\":1},{\"start\":1,\"end\":2},{\"start\":2,\"end\":3},{\"start\":3,\"end\":0}],\"regions\":[{\"loops\":[[0,1,2,3]],\"windingRule\":\"NONZERO\",\"fills\":") + Paint
			+ TEXT(",\"fillStyleId\":\"") + Style + TEXT("\"}]}}"));
	};
	const FString NetworkPaintDocument = MakeFigmaDocJson(
		MakeContainerNode(TEXT("stroke_page_a"), TEXT("Source"), TEXT("FRAME"), 0, 0, 40, 40,
			NetworkPaintLeaf(TEXT("network_source"), TEXT("1"), TEXT("0"), TEXT("red-style")), StrokeAction)
		+ TEXT(",") + MakeContainerNode(TEXT("stroke_page_b"), TEXT("Target"), TEXT("FRAME"), 0, 0, 40, 40,
			NetworkPaintLeaf(TEXT("network_target"), TEXT("0"), TEXT("1"), TEXT("blue-style"))),
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\""));
	if (!TestTrue(TEXT("Resolved vector region paint fixture parses"), FFigmaParser::ParseDocument(NetworkPaintDocument, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Unchanged vector geometry consumes only its resolved uniform region paints"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("stroke_page_a")
			&& Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && Binding.PrimaryStep().Transition.WidgetTargets.ContainsByPredicate(
				[](const FUMGPrototypeWidgetAnimationTarget& Target) { return Target.NodeId == TEXT("network_source")
					&& Target.bSurfacePaintChange && !Target.bColor; }); }));
	TFunction<bool(const FUMGWidgetNode&)> HasResolvedNetworkPaint = [&](const FUMGWidgetNode& Node)
	{
		if (Node.Effect.SurfacePaints.IsSet() && Node.Effect.SurfacePaints->Num() == 1
			&& Node.Effect.SurfacePaintTargets.IsSet() && Node.Effect.GeometryMaskPaths.IsSet()
			&& Node.Effect.GeometryMaskPaths->Num() == 1)
		{
			const auto* RegionPaintDestination = Node.Effect.SurfacePaintTargets->FindByPredicate(
				[](const FUMGSurfacePaintTarget& Paint) { return Paint.DestinationWidgetId == TEXT("network_target"); });
			const FLinearColor RegionSourceColor = (*Node.Effect.SurfacePaints)[0].Color;
			if (RegionPaintDestination && RegionPaintDestination->Paints.Num() == 1)
			{
				const FLinearColor RegionTargetColor = RegionPaintDestination->Paints[0].Color;
				return RegionSourceColor == FLinearColor::Red && RegionTargetColor == FLinearColor::Blue
					&& Node.Brush.MaterialRef.Get(FString()) == TEXT("native-surface-paints") && !Node.Path.HasPaintGeometry();
			}
		}
		for (const auto& Child : Node.Children) if (HasResolvedNetworkPaint(Child)) return true;
		return false;
	};
	TestTrue(TEXT("Uniform vector regions preserve red source, blue destination and path coverage in the real paint consumer"), HasResolvedNetworkPaint(Data.RootWidget));
	FString NetworkMorph = NetworkPaintDocument;
	const int32 NetworkTargetOffset = NetworkMorph.Find(TEXT("\"id\":\"network_target\""));
	const int32 VertexOffset = NetworkMorph.Find(TEXT("\"vertices\":[{\"x\":0"), ESearchCase::CaseSensitive, ESearchDir::FromStart, NetworkTargetOffset);
	NetworkMorph = NetworkMorph.Left(VertexOffset) + NetworkMorph.Mid(VertexOffset).Replace(TEXT("\"vertices\":[{\"x\":0"), TEXT("\"vertices\":[{\"x\":1"));
	if (!TestTrue(TEXT("Changed vector network geometry parses"), FFigmaParser::ParseDocument(NetworkMorph, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Unused plugin network changes cannot replace the canonical vector contour"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("stroke_page_a")
			&& Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && Binding.PrimaryStep().Transition.WidgetTargets.ContainsByPredicate(
				[](const FUMGPrototypeWidgetAnimationTarget& Target) { return Target.NodeId == TEXT("network_source") && Target.bSurfacePaintChange; }); }));
    const FString VectorMorphContours = VectorContours.Replace(TEXT("M0 0L80 0L80 40L0 40Z"), TEXT("M0 0L80 0L72 40L0 40Z"));
    if (!TestTrue(TEXT("Genuinely changed vector contour fixture parses"), FFigmaParser::ParseDocument(VectorMorphContours, Document))) return false;
    Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    TestTrue(TEXT("Vector morph remains rejected by native geometry restrictions"), Rejected(Data, TEXT("instance_0"), TEXT("geometry")));
	const FString AlignmentFixture = MakePluginFixture(
		SourcePlugin.Replace(TEXT("\"cornerSmoothing\":0"), TEXT("\"cornerSmoothing\":0,\"layoutAlign\":\"MIN\"")),
		TargetPlugin.Replace(TEXT("\"cornerSmoothing\":0"), TEXT("\"cornerSmoothing\":0,\"layoutAlign\":\"MAX\"")))
		.Replace(TEXT("\"id\":\"instance_0_surface\""), TEXT("\"id\":\"instance_0_surface\",\"layoutAlign\":\"MIN\""))
		.Replace(TEXT("\"id\":\"target_surface\""), TEXT("\"id\":\"target_surface\",\"layoutAlign\":\"MAX\""));
	if (!TestTrue(TEXT("Inactive child alignment fixture parses"), FFigmaParser::ParseDocument(AlignmentFixture, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Child alignment under absolute parents does not reject animation"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("instance_0") && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty(); }));
	const FString ActiveAlignmentFixture = AlignmentFixture.Replace(TEXT("\"componentId\":\"base\""), TEXT("\"componentId\":\"base\",\"layoutMode\":\"HORIZONTAL\""))
		.Replace(TEXT("\"type\":\"COMPONENT\""), TEXT("\"type\":\"COMPONENT\",\"layoutMode\":\"HORIZONTAL\""));
	if (!TestTrue(TEXT("Active child alignment fixture parses"), FFigmaParser::ParseDocument(ActiveAlignmentFixture, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Child alignment under flow parents requests measured fixed viewport layout"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("instance_0") && Binding.PrimaryStep().Transition.bFixedViewportLayout
			&& !Binding.PrimaryStep().Transition.LayoutTargets.IsEmpty() && !Binding.PrimaryStep().Transition.LayoutPresentationWidgetId.IsEmpty(); }));
	const FString HugFixture = MakeNativeWidgetAnimationDocument()
		.Replace(TEXT("\"componentId\":\"base\""), TEXT("\"componentId\":\"base\",\"layoutMode\":\"VERTICAL\",\"primaryAxisSizingMode\":\"AUTO\""))
		.Replace(TEXT("\"type\":\"COMPONENT\""), TEXT("\"type\":\"COMPONENT\",\"layoutMode\":\"VERTICAL\",\"primaryAxisSizingMode\":\"AUTO\""));
	if (!TestTrue(TEXT("Unchanged HUG state size fixture parses"), FFigmaParser::ParseDocument(HugFixture, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Unchanged HUG state sizes do not require a scale animation"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("instance_1") && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty(); }));
	const FString InvisiblePaintFixture = MakeNativeWidgetAnimationDocument().Replace(TEXT("\"type\":\"SOLID\",\"color\":"), TEXT("\"type\":\"SOLID\",\"visible\":false,\"color\":"));
	if (!TestTrue(TEXT("Invisible endpoint paints fixture parses"), FFigmaParser::ParseDocument(InvisiblePaintFixture, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Invisible paint changes retain motion without adding a color track"), Data.PrototypeNavigation().ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding)
		{ return Binding.TriggerWidgetId == TEXT("instance_0") && !Binding.PrimaryStep().Transition.WidgetTargets.IsEmpty()
			&& !Binding.PrimaryStep().Transition.WidgetTargets[0].bColor && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty(); }));
	FFigmaParser::ParseDocument(MakePluginFixture(SourcePlugin, TargetPlugin.Replace(TEXT("\"cornerSmoothing\":0"), TEXT("\"cornerSmoothing\":0.5"))), Document);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Unused plugin smoothing does not reject native animation"),
		Data.PrototypeNavigation().ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
		{ return Binding.TriggerWidgetId == TEXT("instance_0") && Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && !Binding.PrimaryStep().Transition.WidgetTargets.IsEmpty(); }));
	FFigmaParser::ParseDocument(MakePluginFixture(SourcePlugin, TargetPlugin.Replace(TEXT("\"type\":\"SOLID\""), TEXT("\"type\":\"SOLID\",\"blendMode\":\"MULTIPLY\""))), Document);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Unused plugin paints do not reject native color animation"),
		Data.PrototypeNavigation().ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
		{ return Binding.TriggerWidgetId == TEXT("instance_0") && Binding.PrimaryStep().Transition.WidgetTargets.ContainsByPredicate(
			[](const FUMGPrototypeWidgetAnimationTarget& Target) { return Target.bColor && Target.EndColor.B == 1 && Target.EndColor.G == 0; }); }));
	FFigmaParser::ParseDocument(PluginFixture.Replace(*TargetMotion, *(EmptyMotion + TEXT("\"timelines\":[{\"id\":\"target-frame\",\"duration\":3}]"))), Document);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Inactive timeline duration changes are not discarded"),
		Rejected(Data, TEXT("instance_0"), TEXT("timelines")));
	const FString MotionTracks = TEXT("\"animations\":{\"TRANSLATION_X\":{\"tracks\":[{}]}}");
	FFigmaParser::ParseDocument(PluginFixture.Replace(TEXT("\"animations\":{}"), *MotionTracks), Document);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Identical nonempty top-level Motion tracks are explicitly unsupported"),
		Rejected(Data, TEXT("instance_0"), TEXT("Motion tracks")));
	FFigmaParser::ParseDocument(MakePluginFixture(SourcePlugin.Replace(TEXT("\"animations\":{}"), *MotionTracks),
		TargetPlugin.Replace(TEXT("\"animations\":{}"), *MotionTracks)), Document);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Raw-only Motion tracks cannot be hidden by empty normalized Motion data"),
		Rejected(Data, TEXT("instance_0"), TEXT("Motion tracks")));
	const FString DelayedEnter = MakeNativeWidgetAnimationDocument().Replace(TEXT("\"delay\":0"), TEXT("\"delay\":10"));
	FFigmaParser::ParseDocument(DelayedEnter, Document);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Unsupported delayed enter is not wired as immediate hover"), Data.PrototypeNavigation().ClickBindings.Num(), 3);
	const FString Press = MakeNativeWidgetAnimationDocument().Replace(TEXT("\"ON_CLICK\""), TEXT("\"ON_PRESS\""));
	FFigmaParser::ParseDocument(Press, Document);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Press property animation retains its source target"),
		Data.PrototypeNavigation().ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
		{
			return Binding.TriggerWidgetId == TEXT("instance_0") && Binding.UsesPressTrigger()
				&& Binding.PrimaryStep().Transition.Diagnostic.IsEmpty() && !Binding.PrimaryStep().Transition.WidgetTargets.IsEmpty();
		}));
	const FString Drag = MakeNativeWidgetAnimationDocument().Replace(TEXT("\"ON_CLICK\""), TEXT("\"ON_DRAG\""));
	if (!TestTrue(TEXT("Single-page drag source parses"), FFigmaParser::ParseDocument(Drag, Document))) return false;
	TArray<FString> DragWarnings;
	const auto DragAnalysis = Design2UMGPrototypeNavigation::BuildAnalysis(Document, Document.Children, DragWarnings);
	for (const FString& Warning : DragWarnings) AddInfo(Warning);
	AddInfo(FString::Printf(TEXT("Drag pre-conversion: bindings=%d pages=%d requiresHost=%d host=%s valid=%d"),
		DragAnalysis.Data.ClickBindings.Num(), DragAnalysis.Data.PageIds.Num(), DragAnalysis.Data.RequiresPageHost(),
		*DragAnalysis.Data.PageHostWidgetId, DragAnalysis.Data.IsValid()));
	for (const auto& Item : DragAnalysis.Data.ClickBindings)
		AddInfo(FString::Printf(TEXT("Drag binding %s trigger=%s valid=%d steps=%d diag=%s"),
			*Item.TriggerWidgetId, *Item.TriggerType, Item.IsValid(), Item.Steps.Num(), *Item.PrimaryStep().Transition.Diagnostic));
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	AddInfo(FString::Printf(TEXT("Drag conversion error: %s"), *Data.ConversionError));
	for (const FString& Warning : Data.Warnings) AddInfo(Warning);
	const auto& DragNavigation = Data.PrototypeNavigation();
	TestTrue(TEXT("Single-page drag binding survives analysis"), DragNavigation.ClickBindings.ContainsByPredicate(
		[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.UsesDragTrigger(); }));
	TestTrue(TEXT("Single-page drag requires a page host"), DragNavigation.RequiresPageHost());
	TestFalse(TEXT("Single-page drag has a stable native page host ID"), DragNavigation.PageHostWidgetId.IsEmpty());
	TestEqual(TEXT("Single-page drag has one source page"), DragNavigation.PageIds.Num(), 1);
	const FString PhysicalSpring = MakeNativeWidgetAnimationDocument().Replace(
		TEXT("\"type\":\"EASE_OUT\""),
		TEXT("\"type\":\"CUSTOM_SPRING\",\"easingFunctionSpring\":{\"mass\":1,\"stiffness\":300,\"damping\":256}"));
	if (!TestTrue(TEXT("Physical spring fixture parses"), FFigmaParser::ParseDocument(PhysicalSpring, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (TestTrue(TEXT("Spring binding is retained"), !Data.PrototypeNavigation().ClickBindings.IsEmpty()))
	{
		const auto& Physical = Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition;
		TestTrue(TEXT("Physical spring and missing velocity are identified"), Physical.bSpring && Physical.bSpringVelocityDefaulted);
		TestEqual(TEXT("Spring stiffness retained"), Physical.SpringStiffness, 300.0f);
		TestEqual(TEXT("Spring damping retained"), Physical.SpringDamping, 256.0f);
	}
	const FString Spring = MakeNativeWidgetAnimationDocument().Replace(TEXT("\"EASE_OUT\""), TEXT("\"CUSTOM_SPRING\""));
	FFigmaParser::ParseDocument(Spring, Document);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Unsupported spring is diagnosed instead of sampled or silently replaced"),
		Rejected(Data, TEXT("instance_0"), TEXT("unsupported easing")));
	FFigmaParser::ParseDocument(MakeNativeWidgetAnimationDocument(), Document);
	auto& Source = Document.Children[0]->Children[0]->MutableSource().Prototype;
	Source.Transition = Source.Interactions[0].Actions[0].Transition;
	Source.Interactions[0].Actions[0].Transition = FDesignPrototypeTransitionData();
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const auto* Instant = Data.PrototypeNavigation().ClickBindings.FindByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
	{ return Binding.TriggerWidgetId == TEXT("instance_0"); });
	TestTrue(TEXT("Current action is instant and never inherits stale node animation fields"),
		Instant && Instant->PrimaryStep().Transition.bStateInstant && Instant->PrimaryStep().Transition.Type.IsEmpty()
		&& Instant->PrimaryStep().Transition.DurationSeconds == 0 && !Instant->PrimaryStep().Transition.bSpring
		&& !Instant->PrimaryStep().Transition.bCubicBezier && Instant->PrimaryStep().Transition.WidgetTargets.Num() == 2);
	const FString Summaries = MakeNativeWidgetAnimationDocument()
		.Replace(TEXT("\"name\":\"On\""), TEXT("\"name\":\"On\",\"transitionNodeID\":\"elsewhere\",\"transitionDuration\":123,\"transitionEasing\":{\"type\":\"LINEAR\"}"))
		.Replace(TEXT("\"componentId\":\"base\""), TEXT("\"componentId\":\"base\",\"transitionNodeID\":\"other\",\"transitionDuration\":999,\"transitionEasing\":{\"type\":\"CUSTOM_SPRING\"}"));
	FFigmaParser::ParseDocument(Summaries, Document);
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Non-rendering prototype summaries neither reject nor supply the current action animation"),
		Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.Diagnostic.IsEmpty()
		&& Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.WidgetTargets.Num() == 1
		&& FMath::IsNearlyEqual(Data.PrototypeNavigation().ClickBindings[0].PrimaryStep().Transition.DurationSeconds, 0.4f));
	const FString OverlayPaint = FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.2, 0.4, 0.6));
	const FString Overlay = MakeFigmaDocJson(
		MakeContainerNode(TEXT("A"), TEXT("Screen"), TEXT("FRAME"), 0, 0, 100, 40, FString(),
			OverlayPaint + TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"OPEN_OVERLAY\",\"destinationId\":\"B\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":400,\"easing\":{\"type\":\"LINEAR\"}}}]}]"))
		+ TEXT(",") + MakeContainerNode(TEXT("B"), TEXT("Screen"), TEXT("FRAME"), 0, 0, 100, 40, FString(), OverlayPaint + TEXT(",\"opacity\":0.5")),
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\""));
	if (!TestTrue(TEXT("Smart overlay source parses"), FFigmaParser::ParseDocument(Overlay, Document))) return false;
	Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Smart overlay is explicitly refused before generating property targets"),
		Rejected(Data, TEXT("A"), TEXT("Overlay and other actions are not implemented")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserInputBudgetTest,
	"Design2UMG.Unit.FigmaParser.InputBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserInputBudgetTest::RunTest(const FString&)
{
	FDesignParserBudget Budget;
	for (int32 Index = 0; Index < DesignTreeLimits::MaxParsedNodeCount; ++Index)
	{
		if (!Budget.TryEnterNode(0))
		{
			AddError(FString::Printf(TEXT("Node budget rejected valid node %d"), Index));
			return false;
		}
	}
	TestFalse(TEXT("Node budget rejects one node beyond the shared limit"), Budget.TryEnterNode(0));

	FDesignParserBudget DepthBudget;
	TestTrue(TEXT("Depth at the shared limit is accepted"), DepthBudget.TryEnterNode(DesignTreeLimits::MaxNodeDepth));
	TestFalse(TEXT("Depth beyond the shared limit is rejected"), DepthBudget.TryEnterNode(DesignTreeLimits::MaxNodeDepth + 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserRejectsDeepNodeTreeTest,
	"Design2UMG.Unit.FigmaParser.RejectsDeepNodeTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserRejectsDeepNodeTreeTest::RunTest(const FString&)
{
	FString Json = TEXT("{\"document\":");
	for (int32 Depth = 0; Depth <= DesignTreeLimits::MaxNodeDepth + 1; ++Depth)
	{
		Json += FString::Printf(
			TEXT("{\"type\":\"FRAME\",\"id\":\"%d\",\"name\":\"Node%d\",\"children\":["),
			Depth,
			Depth);
	}
	Json += TEXT("{\"type\":\"RECTANGLE\",\"id\":\"leaf\",\"name\":\"Leaf\"}");
	for (int32 Depth = 0; Depth <= DesignTreeLimits::MaxNodeDepth + 1; ++Depth)
	{
		Json += TEXT("]}");
	}
	Json += TEXT("}");

	AddExpectedError(TEXT("node tree exceeds the supported limits"), EAutomationExpectedErrorFlags::Contains, 1);
	FDesignDocument Document;
	TestFalse(TEXT("Figma parser rejects a tree beyond the shared depth limit"), FFigmaParser::ParseDocument(Json, Document));
	TestEqual(TEXT("Rejected parse leaves no partial tree"), Document.Children.Num(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// Basic node parsing: FRAME / TEXT / RECTANGLE
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserBasicNodesTest,
	"Design2UMG.Unit.FigmaParser.BasicNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserBasicNodesTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadFigmaFixture(*this, TEXT("basic_nodes.json"), Doc))
		return false;

	TestEqual(TEXT("One root child"), Doc.Children.Num(), 1);
	const TSharedPtr<FDesignNode>& Frame = Doc.Children[0];
	TestTrue(TEXT("Frame valid"), Frame.IsValid());
	TestEqual(TEXT("Frame type"), Frame->Type, EDesignNodeType::Frame);
	TestEqual(TEXT("Frame has 2 children"), Frame->Children.Num(), 2);

	const TSharedPtr<FDesignNode>& TextNode = Frame->Children[0];
	TestTrue(TEXT("Text valid"), TextNode.IsValid());
	TestEqual(TEXT("Text type"), TextNode->Type, EDesignNodeType::Text);

	const TSharedPtr<FDesignNode>& RectNode = Frame->Children[1];
	TestTrue(TEXT("Rect valid"), RectNode.IsValid());
	TestEqual(TEXT("Rect type"), RectNode->Type, EDesignNodeType::Rectangle);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserHugSizingOutsideAutoLayoutParentTest,
	"Design2UMG.Unit.FigmaParser.HugSizingOutsideAutoLayoutParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserHugSizingOutsideAutoLayoutParentTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("parent"), TEXT("Parent"), TEXT("FRAME"), 0, 0, 800, 1000, FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeContainerNode(TEXT("hug"), TEXT("Hug"), TEXT("FRAME"), 0, 0, 400, 900,
			MakeLeafNode(TEXT("hug-child"), TEXT("Hug Child"), TEXT("RECTANGLE"), 12, 12, 100, 32,
				FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(1, 1, 1))),
			TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutSizingHorizontal\":\"HUG\",\"layoutSizingVertical\":\"HUG\","
				 "\"paddingLeft\":12,\"paddingRight\":12,\"paddingTop\":12,\"paddingBottom\":12")),
		*MakeContainerNode(TEXT("fill"), TEXT("Fill"), TEXT("FRAME"), 0, 0, 200, 80, FString(),
			TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutSizingHorizontal\":\"FILL\",\"layoutSizingVertical\":\"FILL\"")),
		*MakeContainerNode(TEXT("fixed"), TEXT("Fixed"), TEXT("FRAME"), 0, 0, 160, 64, FString(),
			TEXT(",\"layoutMode\":\"HORIZONTAL\"")))));

	FDesignDocument Doc;
	TestTrue(TEXT("Document parses"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("One root parent"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || !Doc.Children[0].IsValid() || Doc.Children[0]->Children.Num() != 3)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& HugNode = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Self auto-layout HUG width becomes fit-content"), HugNode->Transform.Width.IsFit());
	TestTrue(TEXT("Self auto-layout HUG height becomes fit-content"), HugNode->Transform.Height.IsFit());
	TestEqual(TEXT("Stale HUG width is not retained as a minimum"), HugNode->Transform.Width.Value, 0.0f);
	TestEqual(TEXT("Stale HUG height is not retained as a minimum"), HugNode->Transform.Height.Value, 0.0f);

	const FVector2D ResolvedHugSize = SizeEstimator::PromoteFitContentSizes(HugNode, Doc,
		[&Doc](const TSharedPtr<FDesignNode>& RefNode) { return RefExpander::ExpandRef(RefNode, Doc).ExpandedNode; });
	TestEqual(TEXT("HUG width resolves from child and padding"), ResolvedHugSize.X, 124.0);
	TestEqual(TEXT("HUG height resolves from child and padding"), ResolvedHugSize.Y, 56.0);

	const TSharedPtr<FDesignNode>& FillNode = Doc.Children[0]->Children[1];
	TestTrue(TEXT("FILL without an auto-layout parent keeps fixed width"), FillNode->Transform.Width.IsFixed());
	TestTrue(TEXT("FILL without an auto-layout parent keeps fixed height"), FillNode->Transform.Height.IsFixed());
	TestEqual(TEXT("Fixed FILL width keeps its source bounds"), FillNode->Transform.Width.Value, 200.0f);
	TestEqual(TEXT("Fixed FILL height keeps its source bounds"), FillNode->Transform.Height.Value, 80.0f);

	const TSharedPtr<FDesignNode>& FixedNode = Doc.Children[0]->Children[2];
	TestTrue(TEXT("Missing horizontal sizing stays fixed"), FixedNode->Transform.Width.IsFixed());
	TestTrue(TEXT("Missing vertical sizing stays fixed"), FixedNode->Transform.Height.IsFixed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserStrokeDashMetadataTest,
	"Design2UMG.Unit.FigmaParser.StrokeDashMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserStrokeDashMetadataTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(MakeLeafNode(TEXT("dashed-vector"), TEXT("Dashed Vector"), TEXT("VECTOR"), 0, 0, 0.0001, 306,
		FString::Printf(
			TEXT(",\"absoluteRenderBounds\":%s,"
				 "\"strokes\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.8,\"g\":0.9,\"b\":1,\"a\":1}}],"
				 "\"strokeWeight\":5.11,\"strokeAlign\":\"CENTER\",\"strokeCap\":\"ROUND\",\"strokeJoin\":\"BEVEL\","
				 "\"strokeDashes\":[21.17,21.17]"),
			*MakeBoundsJson(-3, -3, 6, 312))));

	FDesignDocument Document;
	TestTrue(TEXT("Dashed VECTOR parses"), FFigmaParser::ParseDocument(Json, Document));
	TestEqual(TEXT("One dashed VECTOR is emitted"), Document.Children.Num(), 1);
	if (Document.Children.Num() != 1 || !Document.Children[0].IsValid())
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Document.Children[0];
	TestEqual(TEXT("Stroke cap is normalized for shared conversion"), Node->Visual.Stroke.Cap, EDesignStrokeCap::Circular);
	TestEqual(TEXT("Stroke join is normalized for shared conversion"), Node->Visual.Stroke.Join, EDesignStrokeJoin::Bevel);
	TestEqual(TEXT("Stroke dash count is normalized for shared conversion"), Node->Visual.Stroke.DashPattern.Num(), 2);
	if (Node->Visual.Stroke.DashPattern.Num() == 2)
	{
		TestEqual(TEXT("First shared stroke dash"), Node->Visual.Stroke.DashPattern[0], 21.17f);
		TestEqual(TEXT("Second shared stroke dash"), Node->Visual.Stroke.DashPattern[1], 21.17f);
	}
	TestEqual(TEXT("Dashed stroke cap metadata is preserved"), Node->Metadata.FindRef(TEXT("stroke.cap")), TEXT("ROUND"));
	TestEqual(TEXT("Stroke join metadata is preserved"), Node->Metadata.FindRef(TEXT("stroke.join")), TEXT("BEVEL"));
	TestEqual(TEXT("Stroke dash metadata is preserved"), Node->Metadata.FindRef(TEXT("stroke.dashes")), TEXT("21.17,21.17"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserExpandedDropShadowUsesLayoutBoundsTest,
	"Design2UMG.Unit.FigmaParser.ExpandedDropShadowUsesLayoutBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserExpandedDropShadowUsesLayoutBoundsTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("card"), TEXT("Card"), TEXT("GROUP"), 100, 200, 400, 200,
		MakeLeafNode(TEXT("background"), TEXT("Background"), TEXT("RECTANGLE"), 140, 232, 210, 86,
			FString::Printf(
				TEXT(",\"absoluteRenderBounds\":%s,"
					 "\"effects\":[{\"type\":\"DROP_SHADOW\",\"visible\":true,\"offset\":{\"x\":0,\"y\":0},\"radius\":40,\"showShadowBehindNode\":true}],"
					 "\"fillGeometry\":[{\"path\":\"M0 0 L210 0 L210 86 Z\",\"windingRule\":\"NONZERO\"},{\"path\":\"M10 10 L20 10 L20 20 Z\",\"windingRule\":\"EVENODD\"}],"
						"\"strokeGeometry\":[{\"path\":\"M1 1 L209 1 L209 85 Z\",\"windingRule\":\"NONZERO\"}],"
						"\"fills\":[%s]"),
				*MakeBoundsJson(100, 192, 290, 166), *MakeSolidFillJson(1, 1, 1))),
		FString::Printf(TEXT(",\"absoluteRenderBounds\":%s"), *MakeBoundsJson(83, 194, 434, 234))));

	FDesignDocument Document;
	TestTrue(TEXT("Expanded drop-shadow node parses"), FFigmaParser::ParseDocument(Json, Document));
	if (Document.Children.Num() != 1 || !Document.Children[0].IsValid()
		|| Document.Children[0]->Children.Num() != 1 || !Document.Children[0]->Children[0].IsValid())
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Background = Document.Children[0]->Children[0];
	TestEqual(TEXT("Drop-shadow node keeps layout width"), Background->Transform.Width.Value, 210.0f);
	TestEqual(TEXT("Drop-shadow node keeps layout height"), Background->Transform.Height.Value, 86.0f);
	const auto& Geometry = Background->Shape.PaintGeometry;
	if (!TestTrue(TEXT("Fill and stroke geometry are independently retained"), Geometry.FillPaths.IsSet() && Geometry.StrokePaths.IsSet()))
		return false;
	if (!TestEqual(TEXT("All fill contours are retained"), Geometry.FillPaths.GetValue().Num(), 2))
		return false;
	TestEqual(TEXT("Stroke contour is retained"), Geometry.StrokePaths.GetValue().Num(), 1);
	TestEqual(TEXT("Each contour keeps its own fill rule"), Geometry.FillPaths.GetValue()[1].FillRule, FString(TEXT("evenodd")));
	FUMGWidgetNode Widget;
	VisualMapper::ApplyVisualProperties(Widget, Background);
	if (!TestEqual(TEXT("One normalized shadow"), Widget.Shadow.BoxShadows.Num(), 1))
		return false;
	TestTrue(TEXT("Shadow behind flag reaches the shared shadow layer"), Widget.Shadow.BoxShadows[0].bShowShadowBehindNode);
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	// The leaf render bounds own the shadow paint; the group render bounds are a layout aggregate.
	TestTrue(TEXT("Source paint bounds reach export in normalized root coordinates"), Data.ExportBounds.IsSet()
		&& Data.ExportBounds->Min.Equals(FVector2D(0, -8))
		&& Data.ExportBounds->GetSize().Equals(FVector2D(400, 208)));
	TestEqual(TEXT("Paint overflow preserves the root layout width"), Data.RootWidget.Bounds.W, 400.0f);
	TestEqual(TEXT("Paint overflow preserves the root layout height"), Data.RootWidget.Bounds.H, 200.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaParserRelativeTransformTest,
	"Design2UMG.Unit.FigmaParser.RelativeTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaParserRelativeTransformTest::RunTest(const FString&)
{
	const TCHAR* Fields[] = {
		TEXT(",\"relativeTransform\":[[-1,0,0],[0,1,0]],\"rotation\":3.141592653589793"),
		TEXT(",\"relativeTransform\":[[1,0,0],[0,-1,0]]"),
		TEXT(",\"relativeTransform\":[[0,-1,0],[1,0,0]],\"rotation\":1.5707963267948966"),
		TEXT(",\"relativeTransform\":[[-0.8660254,-0.5,0],[-0.5,0.8660254,0]]"),
		TEXT(",\"relativeTransform\":[[0.8660254,0.5,0],[0.5,-0.8660254,0]]"),
		TEXT(",\"relativeTransform\":[[-1,0,0],[0,-1,0]]"),
		TEXT(",\"rotation\":1.5707963267948966"),
		TEXT(",\"relativeTransform\":[[0,-1,0],[1,0,0]]"),
	};
	TArray<FString> Nodes;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Fields); ++Index)
	{
		const FString Id = FString::FromInt(Index);
		const FString Geometry = Index < 6 ? FString(TEXT(",\"size\":{\"x\":20,\"y\":20}")) : FString();
		Nodes.Add(MakeLeafNode(*Id, *Id, TEXT("RECTANGLE"), 5, 7, 10, 20, Geometry + Fields[Index]));
	}
	FDesignDocument Document;
	AddExpectedError(TEXT("transform not applied because local matrix or size is missing"), EAutomationExpectedErrorFlags::Contains, 2);
	if (!TestTrue(TEXT("Figma transforms parse"), FFigmaParser::ParseDocument(MakeFigmaDocJson(FString::Join(Nodes, TEXT(","))), Document))
		|| !TestEqual(TEXT("All transform samples retained"), Document.Children.Num(), static_cast<int32>(UE_ARRAY_COUNT(Fields)))) return false;
	const float Angles[] = { 0.f, 0.f, 90.f, 30.f, 30.f, 180.f, 0.f, 0.f };
	const bool FlipX[] = { true, false, false, true, false, false, false, false };
	const bool FlipY[] = { false, true, false, false, true, false, false, false };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Fields); ++Index)
	{
		const auto& Transform = Document.Children[Index]->Transform;
		TestTrue(*FString::Printf(TEXT("Transform %d angle"), Index), FMath::IsNearlyEqual(Transform.Rotation, Angles[Index], 0.001f));
		TestEqual(*FString::Printf(TEXT("Transform %d horizontal reflection"), Index), Transform.bFlipX, FlipX[Index]);
		TestEqual(*FString::Printf(TEXT("Transform %d vertical reflection"), Index), Transform.bFlipY, FlipY[Index]);
	}
	for (int32 Index = 6; Index < UE_ARRAY_COUNT(Fields); ++Index)
	{
		const auto& Transform = Document.Children[Index]->Transform;
		TestTrue(TEXT("Incomplete local geometry retains the page-space bounds"),
			FMath::IsNearlyEqual(Transform.Width.Value, 10.f) && FMath::IsNearlyEqual(Transform.Height.Value, 20.f)
			&& FMath::IsNearlyEqual(Transform.X, 5.f) && FMath::IsNearlyEqual(Transform.Y, 7.f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaParserArcGeometryTest,
	"Design2UMG.Unit.FigmaParser.ArcGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaParserArcGeometryTest::RunTest(const FString&)
{
	const FString Arc = MakeLeafNode(TEXT("arc"), TEXT("Arc"), TEXT("ELLIPSE"), 0, 0, 100, 100,
		TEXT(",\"arcData\":{\"startingAngle\":-1.5707963267948966,\"endingAngle\":2.6179938779914944,\"innerRadius\":0.9}"));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Arc source parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Arc), Document))
		|| !TestEqual(TEXT("Arc node retained"), Document.Children.Num(), 1)) return false;
	const auto& Shape = Document.Children[0]->Shape;
	TestTrue(TEXT("Inner radius is retained"), FMath::IsNearlyEqual(Shape.InnerRadius, 0.9f));
	TestTrue(TEXT("Start angle uses shared degree convention"), FMath::IsNearlyEqual(Shape.StartAngle, 90.0f));
	TestTrue(TEXT("Sweep uses shared counterclockwise convention"), FMath::IsNearlyEqual(Shape.SweepAngle, -240.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaParserRotatedLocalGeometryTest,
	"Design2UMG.Unit.FigmaParser.RotatedLocalGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaParserRotatedLocalGeometryTest::RunTest(const FString&)
{
	const FString Vector = MakeLeafNode(TEXT("vector"), TEXT("Vector"), TEXT("VECTOR"), 11, 6, 10, 20,
		TEXT(",\"size\":{\"x\":20,\"y\":10},\"relativeTransform\":[[0,1,11],[-1,0,26]],\"rotation\":-1.5707963267948966,"
		"\"fillGeometry\":[{\"path\":\"M0 0 L20 0 L20 10 Z\",\"windingRule\":\"NONZERO\"}],"
		"\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":1,\"b\":1}}]"));
	const FString Rectangle = MakeLeafNode(TEXT("child"), TEXT("Child"), TEXT("RECTANGLE"), 22, 66, 8, 4,
		TEXT(",\"size\":{\"x\":8,\"y\":4},\"relativeTransform\":[[0,-1,4],[1,0,2]]"));
	const FString Group = MakeContainerNode(TEXT("group"), TEXT("Group"), TEXT("GROUP"), 20, 30, 20, 40, Rectangle,
		TEXT(",\"size\":{\"x\":40,\"y\":20},\"relativeTransform\":[[0,1,20],[-1,0,70]]"));
	const FString MirrorChild = MakeLeafNode(TEXT("mirror-child"), TEXT("Mirror child"), TEXT("RECTANGLE"), 60, 59, 10, 4,
		TEXT(",\"size\":{\"x\":10,\"y\":4},\"relativeTransform\":[[1,0,2],[0,1,3]]"));
	const FString MirrorGroup = MakeContainerNode(TEXT("mirror-group"), TEXT("Mirror group"), TEXT("GROUP"), 42, 56, 30, 20, MirrorChild,
		TEXT(",\"size\":{\"x\":30,\"y\":20},\"relativeTransform\":[[-1,0,72],[0,1,56]]"));
	const FString Line = MakeLeafNode(TEXT("line"), TEXT("Line"), TEXT("LINE"), 132, 4, 0, 16,
		TEXT(",\"size\":{\"x\":16,\"y\":0},\"relativeTransform\":[[0,-1,132],[1,0,4]],"
		"\"strokes\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":1,\"b\":1,\"a\":1}}],"
		"\"strokeWeight\":1,\"strokeAlign\":\"CENTER\","
		"\"strokeGeometry\":[{\"path\":\"M0 0 L16 0 L16 -1 L0 -1 Z\",\"windingRule\":\"NONZERO\"}]"));
	const FString FlowLine = Line.Replace(TEXT("\"id\":\"line\""), TEXT("\"id\":\"flow-line\""));
	const FString HorizontalLine = MakeLeafNode(TEXT("horizontal-line"), TEXT("Horizontal line"), TEXT("LINE"), 10, 30, 16, 0,
		TEXT(",\"absoluteRenderBounds\":{\"x\":10,\"y\":29,\"width\":16,\"height\":1},"
		"\"size\":{\"x\":16,\"y\":0},\"relativeTransform\":[[1,0,10],[0,1,30]],"
		"\"fills\":[],\"fillGeometry\":[],\"strokes\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":1,\"b\":1,\"a\":1}}],"
		"\"strokeWeight\":1,\"strokeAlign\":\"CENTER\","
		"\"strokeGeometry\":[{\"path\":\"M0 0 L16 0 L16 -1 L0 -1 Z\",\"windingRule\":\"NONZERO\"}]"));
	const FString Flow = MakeContainerNode(TEXT("flow"), TEXT("Flow"), TEXT("FRAME"), 0, 0, 200, 24, FlowLine,
		TEXT(",\"size\":{\"x\":200,\"y\":24},\"relativeTransform\":[[1,0,0],[0,1,0]],\"layoutMode\":\"HORIZONTAL\""));
	const FString Root = MakeContainerNode(TEXT("root"), TEXT("Root"), TEXT("FRAME"), 0, 0, 100, 100,
		Vector + TEXT(",") + Group + TEXT(",") + MirrorGroup + TEXT(",") + Line + TEXT(",") + Flow + TEXT(",") + HorizontalLine);
	FDesignDocument Document;
	if (!TestTrue(TEXT("Rotated geometry parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Root), Document))
		|| !TestEqual(TEXT("One root retained"), Document.Children.Num(), 1)
		|| !TestEqual(TEXT("All root children retained"), Document.Children[0]->Children.Num(), 6)) return false;
	const auto& ParsedVector = Document.Children[0]->Children[0];
	TestTrue(TEXT("Rotated vector uses its unrotated local size"), FMath::IsNearlyEqual(ParsedVector->Transform.Width.Value, 20.f)
		&& FMath::IsNearlyEqual(ParsedVector->Transform.Height.Value, 10.f));
	TestTrue(TEXT("Center-pivot slot preserves the source matrix center"), FMath::IsNearlyEqual(ParsedVector->Transform.X, 6.f)
		&& FMath::IsNearlyEqual(ParsedVector->Transform.Y, 11.f));
	FUMGWidgetNode Widget = PathWidgetBuilder::CreatePathWidget(ParsedVector);
	VisualMapper::ApplyRenderTransformProperties(Widget, ParsedVector);
	LayoutMapper::ApplySlotProperties(Widget, ParsedVector, EDesignLayout::None);
	TestTrue(TEXT("Path render canvas retains local dimensions"), Widget.Path.CanvasSize.IsSet()
		&& Widget.Path.CanvasSize->Equals(FVector2D(20, 10)));
	TestTrue(TEXT("UMG slot and rotation preserve the source transform"), FMath::IsNearlyEqual(Widget.Slot.Offsets.Left, 6.f)
		&& FMath::IsNearlyEqual(Widget.Slot.Offsets.Top, 11.f)
		&& FMath::IsNearlyEqual(Widget.Render.RenderAngle.Get(0.f), -90.f));
	const auto& ParsedGroup = Document.Children[0]->Children[1];
	if (!TestEqual(TEXT("Rotated group child retained"), ParsedGroup->Children.Num(), 1)) return false;
	const auto& ParsedChild = ParsedGroup->Children[0];
	TestTrue(TEXT("Group uses local dimensions and center"), FMath::IsNearlyEqual(ParsedGroup->Transform.Width.Value, 40.f)
		&& FMath::IsNearlyEqual(ParsedGroup->Transform.Height.Value, 20.f)
		&& FMath::IsNearlyEqual(ParsedGroup->Transform.X, 10.f) && FMath::IsNearlyEqual(ParsedGroup->Transform.Y, 40.f));
	TestTrue(TEXT("Group-relative child removes the parent matrix once"), FMath::IsNearlyEqual(ParsedChild->Transform.Rotation, 90.f)
		&& FMath::IsNearlyEqual(ParsedChild->Transform.X, -2.f) && FMath::IsNearlyEqual(ParsedChild->Transform.Y, 4.f)
		&& FMath::IsNearlyEqual(ParsedChild->Transform.Width.Value, 8.f) && FMath::IsNearlyEqual(ParsedChild->Transform.Height.Value, 4.f));
	const auto& ParsedMirrorGroup = Document.Children[0]->Children[2];
	if (!TestEqual(TEXT("Mirrored group child retained"), ParsedMirrorGroup->Children.Num(), 1)) return false;
	const auto& ParsedMirrorChild = ParsedMirrorGroup->Children[0];
	TestTrue(TEXT("A parent-local child does not inherit a second reflection"), ParsedMirrorGroup->Transform.bFlipX
		&& !ParsedMirrorChild->Transform.bFlipX && !ParsedMirrorChild->Transform.bFlipY
		&& FMath::IsNearlyZero(ParsedMirrorChild->Transform.Rotation)
		&& FMath::IsNearlyEqual(ParsedMirrorChild->Transform.X, 2.f) && FMath::IsNearlyEqual(ParsedMirrorChild->Transform.Y, 3.f));
	const auto& ParsedLine = Document.Children[0]->Children[3];
	TestTrue(TEXT("Rotated line keeps the legal zero local axis and matrix center"),
		FMath::IsNearlyEqual(ParsedLine->Transform.Width.Value, 16.f) && FMath::IsNearlyZero(ParsedLine->Transform.Height.Value)
		&& FMath::IsNearlyEqual(ParsedLine->Transform.X, 124.f) && FMath::IsNearlyEqual(ParsedLine->Transform.Y, 12.f)
		&& FMath::IsNearlyEqual(ParsedLine->Transform.Rotation, 90.f));
	const FUMGWidgetNode PaintedLine = VisualMapperFillTransfer::WrapWithPaintedStroke(PathWidgetBuilder::CreatePathWidget(ParsedLine), ParsedLine);
	TestTrue(TEXT("Existing source-stroke handling retains nonzero ink canvas"), PaintedLine.Path.CanvasSize.IsSet()
		&& PaintedLine.Path.CanvasSize->Equals(FVector2D(16, 1)) && PaintedLine.Path.SourcePaintGeometry.IsSet());
	const auto& ParsedFlow = Document.Children[0]->Children[4];
	if (!TestEqual(TEXT("Flow line retained"), ParsedFlow->Children.Num(), 1)) return false;
	const auto& ParsedFlowLine = ParsedFlow->Children[0];
	TestTrue(TEXT("Rotated auto-layout line keeps source dimensions and flow positioning"),
		FMath::IsNearlyEqual(ParsedFlowLine->Transform.Width.Value, 16.f)
		&& FMath::IsNearlyZero(ParsedFlowLine->Transform.Height.Value)
		&& !ParsedFlowLine->Transform.HasExplicitX() && !ParsedFlowLine->Transform.HasExplicitY()
		&& FMath::IsNearlyEqual(ParsedFlowLine->Transform.Rotation, 90.f));
	const auto& ParsedHorizontalLine = Document.Children[0]->Children[5];
	TestTrue(TEXT("Source stroke retains its local origin instead of the expanded render bounds"),
		FMath::IsNearlyEqual(ParsedHorizontalLine->Transform.X, 10.f)
		&& FMath::IsNearlyEqual(ParsedHorizontalLine->Transform.Y, 30.f)
		&& FMath::IsNearlyEqual(ParsedHorizontalLine->Transform.Width.Value, 16.f)
		&& FMath::IsNearlyZero(ParsedHorizontalLine->Transform.Height.Value));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaParserPathLocalCanvasTest,
	"Design2UMG.Unit.FigmaParser.PathLocalCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaParserPathLocalCanvasTest::RunTest(const FString&)
{
	const FString Path = TEXT("M0 0 L86.25 0 L86.25 210.5 Z");
	const FString Vector = MakeLeafNode(TEXT("path"), TEXT("Path"), TEXT("VECTOR"), 0, 0, 210.5, 86.25,
		FString::Printf(TEXT(",\"size\":{\"x\":86.25,\"y\":210.5},\"rotation\":90,"
			"\"fillGeometry\":[{\"path\":\"%s\",\"windingRule\":\"NONZERO\"}],\"fills\":[%s]"),
			*Path, *MakeSolidFillJson(1, 1, 1)));
	FDesignDocument Document;
	AddExpectedError(TEXT("transform not applied because local matrix or size is missing"), EAutomationExpectedErrorFlags::Contains, 1);
	if (!TestTrue(TEXT("Source-local path parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Vector), Document))
		|| !TestEqual(TEXT("One path is retained"), Document.Children.Num(), 1)) return false;
	const FUMGWidgetNode Widget = PathWidgetBuilder::CreatePathWidget(Document.Children[0]);
	TestTrue(TEXT("Local coordinates do not use the rotated absolute bounding box"),
		Widget.Path.ViewBox.IsSet() && Widget.Path.ViewBox->Equals(FVector4(0, 0, 86.25, 210.5)));
	TestTrue(TEXT("Target canvas and source path retain their separate contracts"),
		Widget.Path.CanvasSize.IsSet() && Widget.Path.CanvasSize->Equals(FVector2D(210.5, 86.25))
		&& Widget.Path.Geometry.Get(FString()) == Path);
	return true;
}

// ---------------------------------------------------------------------------
// Fill types: solid / gradient / image
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserFillTypesTest,
	"Design2UMG.Unit.FigmaParser.FillTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserFillTypesTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadFigmaFixture(*this, TEXT("fills_effects_vars.json"), Doc))
		return false;

	// First root child is FillsFrame with 3 children
	TestTrue(TEXT("Has root children"), Doc.Children.Num() >= 1);
	const TSharedPtr<FDesignNode>& FillsFrame = Doc.Children[0];
	TestTrue(TEXT("FillsFrame valid"), FillsFrame.IsValid());
	TestEqual(TEXT("FillsFrame has 3 children"), FillsFrame->Children.Num(), 3);

	// Solid fill child
	const TSharedPtr<FDesignNode>& SolidNode = FillsFrame->Children[0];
	TestTrue(TEXT("SolidNode valid"), SolidNode.IsValid());
	TestEqual(TEXT("SolidNode fill type"), SolidNode->Visual.TopmostFill().Type, EDesignFillType::Color);
	TestEqual(TEXT("SolidNode fills count"), SolidNode->Visual.Fills.Num(), 1);
	TestFalse(TEXT("SolidNode solid color present"), SolidNode->Visual.TopmostFill().SolidColor.IsEmpty());

	// Gradient fill child
	const TSharedPtr<FDesignNode>& GradNode = FillsFrame->Children[1];
	TestTrue(TEXT("GradNode valid"), GradNode.IsValid());
	TestEqual(TEXT("GradNode fill type"), GradNode->Visual.TopmostFill().Type, EDesignFillType::Gradient);
	TestEqual(TEXT("GradNode gradient type"), GradNode->Visual.TopmostFill().Gradient.GradientType, EDesignGradientType::Linear);
	TestEqual(TEXT("GradNode gradient stops"), GradNode->Visual.TopmostFill().Gradient.Colors.Num(), 2);
	TestEqual(TEXT("GradNode first stop position"), GradNode->Visual.TopmostFill().Gradient.Colors[0].Position, 0.0f);
	TestEqual(TEXT("GradNode second stop position"), GradNode->Visual.TopmostFill().Gradient.Colors[1].Position, 1.0f);

	// Image fill child
	const TSharedPtr<FDesignNode>& ImgNode = FillsFrame->Children[2];
	TestTrue(TEXT("ImgNode valid"), ImgNode.IsValid());
	TestEqual(TEXT("ImgNode fill type"), ImgNode->Visual.TopmostFill().Type, EDesignFillType::Image);
	TestEqual(TEXT("ImgNode image ref"), ImgNode->Visual.TopmostFill().Image.Url, FString(TEXT("abc123")));
	TestEqual(TEXT("ImgNode image mode"), ImgNode->Visual.TopmostFill().Image.Mode, EDesignImageMode::Fill);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserGradientGeometryTest,
	"Design2UMG.Gradient.FigmaParserGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserGradientGeometryTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(
		MakeLeafNode(TEXT("2:1"), TEXT("RadialGradientNode"), TEXT("RECTANGLE"), 0, 0, 120, 80,
			TEXT(",\"fills\":[{"
				 "\"type\":\"GRADIENT_RADIAL\","
				 "\"gradientHandlePositions\":["
				 "{\"x\":0.25,\"y\":0.75},{\"x\":0.65,\"y\":0.75},{\"x\":0.25,\"y\":1.15}],"
				 "\"gradientStops\":["
				 "{\"color\":{\"r\":1,\"g\":0,\"b\":0,\"a\":1},\"position\":0},"
				 "{\"color\":{\"r\":0,\"g\":1,\"b\":0,\"a\":1},\"position\":0.4},"
				 "{\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1},\"position\":1}]}]")),
		TEXT(",\"components\":{},\"componentSets\":{},\"styles\":{}"));

	FDesignDocument Doc;
	TestTrue(TEXT("Figma document parses"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("One parsed root child"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || !Doc.Children[0].IsValid())
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Doc.Children[0];
	TestEqual(TEXT("Parsed gradient fill type"), Node->Visual.TopmostFill().Type, EDesignFillType::Gradient);
	TestEqual(TEXT("Parsed gradient type"), Node->Visual.TopmostFill().Gradient.GradientType, EDesignGradientType::Radial);
	TestEqual(TEXT("Parsed gradient stops"), Node->Visual.TopmostFill().Gradient.Colors.Num(), 3);
	TestTrue(TEXT("Parsed radial center present"), Node->Visual.TopmostFill().Gradient.Center.IsSet());
	TestTrue(TEXT("Parsed radial size present"), Node->Visual.TopmostFill().Gradient.Size.IsSet());
	const FVector2D ParsedCenter = Node->Visual.TopmostFill().Gradient.Center.Get(FVector2D::ZeroVector);
	const FVector2D ParsedSize = Node->Visual.TopmostFill().Gradient.Size.Get(FVector2D::ZeroVector);
	TestTrue(TEXT("Parsed radial center X"),
		FMath::IsNearlyEqual(ParsedCenter.X, 0.25f, 0.0001f));
	TestTrue(TEXT("Parsed radial center Y"),
		FMath::IsNearlyEqual(ParsedCenter.Y, 0.75f, 0.0001f));
	TestTrue(TEXT("Parsed radial size width"),
		FMath::IsNearlyEqual(ParsedSize.X, 0.8f, 0.0001f));
	TestTrue(TEXT("Parsed radial size height"),
		FMath::IsNearlyEqual(ParsedSize.Y, 0.8f, 0.0001f));
	TestTrue(TEXT("Parsed middle stop position"),
		FMath::IsNearlyEqual(Node->Visual.TopmostFill().Gradient.Colors[1].Position, 0.4f, 0.0001f));
	FDesignDocument MatrixDoc;
	const FString MatrixJson = Json.Replace(TEXT("\"gradientHandlePositions\":"),
		TEXT("\"gradientTransform\":[[1,0,0],[0,1,0]],\"gradientHandlePositions\":"));
	if (TestTrue(TEXT("Plugin gradient matrix parses"), FFigmaParser::ParseDocument(MatrixJson, MatrixDoc))
		&& TestEqual(TEXT("Matrix document retains its node"), MatrixDoc.Children.Num(), 1))
	{
		const FDesignGradient& Gradient = MatrixDoc.Children[0]->Visual.TopmostFill().Gradient;
		TestTrue(TEXT("Matrix defines the radial center in object space"), Gradient.Center.IsSet()
			&& Gradient.Center->Equals(FVector2D(0.5, 0.5)));
		TestTrue(TEXT("Matrix defines radial diameters independently of serialized handles"), Gradient.Size.IsSet()
			&& Gradient.Size->Equals(FVector2D(1, 1)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserVisualExplicitClearTest,
	"Design2UMG.Unit.FigmaParser.VisualExplicitClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserVisualExplicitClearTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(
		MakeLeafNode(TEXT("explicit-clear"), TEXT(""), TEXT("RECTANGLE"), 0, 0, 80, 40,
			TEXT(",\"fills\":[],\"effects\":[],\"fillGeometry\":[]")));

	FDesignDocument Document;
	TestTrue(TEXT("Figma explicit-clear fixture parses"), FFigmaParser::ParseDocument(Json, Document));
	TestEqual(TEXT("One explicit-clear node is emitted"), Document.Children.Num(), 1);
	if (Document.Children.Num() != 1 || !Document.Children[0].IsValid())
	{
		return false;
	}

	const FDesignVisualNodeData& Visual = Document.Children[0]->Visual;
	TestTrue(TEXT("Empty fills are represented as an explicit shared clear"), Visual.bFillExplicitClear);
	TestTrue(TEXT("Empty effects are represented as an explicit shared clear"), Visual.bEffectsExplicitClear);
	TestEqual(TEXT("Explicit fill clear has no fill layers"), Visual.Fills.Num(), 0);
	TestEqual(TEXT("Explicit effect clear has no effects"), Visual.Effects.Num(), 0);
	const auto& Geometry = Document.Children[0]->Shape.PaintGeometry;
	TestTrue(TEXT("Known empty fill geometry stays distinct from missing geometry"), Geometry.FillPaths.IsSet() && Geometry.FillPaths.GetValue().IsEmpty());
	TestFalse(TEXT("Missing stroke geometry stays missing"), Geometry.StrokePaths.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExpandedStrokePaintTest,
	"Design2UMG.Unit.FigmaParser.ExpandedStrokePaint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaExpandedStrokePaintTest::RunTest(const FString&)
{
	const FString Upper = TEXT("M-2.5 -2.5 L29 -2.5 L29 2.5 L-2.5 2.5 Z");
	const FString Lower = TEXT("M-2.5 14.75 L29 14.75 L29 19.75 L-2.5 19.75 Z");
	const FString Source = MakeLeafNode(TEXT("outline"), TEXT("Outline"), TEXT("VECTOR"), 0, 0, 26.5, 17.25,
		FString::Printf(TEXT(",\"size\":{\"x\":26.5,\"y\":17.25},"
			"\"absoluteRenderBounds\":{\"x\":-2.5,\"y\":-2.5,\"width\":31.5,\"height\":22.25},"
			"\"fillGeometry\":[],\"fills\":[],\"strokeWeight\":5,\"strokeAlign\":\"CENTER\",\"strokes\":[%s],"
			"\"strokeGeometry\":[{\"path\":\"%s\",\"windingRule\":\"NONZERO\"},"
			"{\"path\":\"%s\",\"windingRule\":\"EVENODD\"}]"), *MakeSolidFillJson(0, 0, 1), *Upper, *Lower));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Expanded stroke source parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Source), Document))) return false;
	if (!TestEqual(TEXT("One source path"), Document.Children.Num(), 1)) return false;
	const auto& Node = Document.Children[0];
	FUMGWidgetNode Surface = PathWidgetBuilder::CreatePathWidget(Node);
	TestFalse(TEXT("Expanded outline is not restroked as a centerline"), Surface.Path.StrokeThickness.IsSet());
	FUMGWidgetNode Painted = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(Surface), Node);
	if (!TestTrue(TEXT("All source outlines share one stroke paint"), Painted.Children.IsEmpty()
		&& Painted.Path.SourcePaintGeometry.IsSet())) return false;
	const auto& Common = Painted.Path.SourcePaintGeometry.GetValue();
	if (!TestTrue(TEXT("Common geometry contains both source outlines"), Common.Geometry.StrokePaths.IsSet() && Common.Geometry.StrokePaths->Num() == 2)) return false;
	TestEqual(TEXT("First source contour unchanged"), Common.Geometry.StrokePaths.GetValue()[0].Path, Upper);
	TestEqual(TEXT("Second source contour unchanged"), Common.Geometry.StrokePaths.GetValue()[1].Path, Lower);
	TestEqual(TEXT("Common geometry retains each path rule"), Common.Geometry.StrokePaths.GetValue()[1].FillRule, FString(TEXT("evenodd")));
	TestEqual(TEXT("Open center strokes do not acquire alignment clipping"), Common.StrokeAlignment, FString(TEXT("center")));
	TestTrue(TEXT("Source local canvas retained"), Painted.Path.ViewBox.IsSet() && Painted.Path.ViewBox->Equals(FVector4(0, 0, 26.5, 17.25)));
	TestTrue(TEXT("Source outline regions are filled without restroking"),
		Painted.Path.Geometry.Get(FString()) == Upper && !Painted.Path.StrokeThickness.IsSet()
		&& Common.FillPaints.IsSet() && Common.FillPaints->IsEmpty());
	TestEqual(TEXT("Source stroke metadata remains unchanged"), Node->Visual.Stroke.Thickness.Uniform, 5.0f);

	auto InsideNode = MakeShared<FDesignNode>(*Node);
	InsideNode->Visual.Stroke.Align = EDesignStrokeAlign::Inside;
	InsideNode->Shape.PaintGeometry.FillPaths = TArray<FDesignGeometryPath>{{TEXT("M0 0L26.5 0L26.5 17.25L0 17.25Z"), TEXT("evenodd")}};
	const FUMGWidgetNode Inside = VisualMapperFillTransfer::WrapWithPaintedStroke(PathWidgetBuilder::CreatePathWidget(InsideNode), InsideNode);
	if (!TestTrue(TEXT("Inside alignment stays in the source context"), Inside.Children.IsEmpty() && Inside.Path.SourcePaintGeometry.IsSet()
		&& Inside.Path.SourcePaintGeometry->StrokeAlignment == TEXT("inner"))) return false;
	TestEqual(TEXT("The interior retains its own winding rule"), Inside.Path.SourcePaintGeometry->Geometry.FillPaths.GetValue()[0].FillRule, FString(TEXT("evenodd")));

	auto PrimitiveNode = MakeShared<FDesignNode>(*Node);
	PrimitiveNode->Type = EDesignNodeType::Rectangle;
	FUMGWidgetNode NativeBorder;
	NativeBorder.Type = TEXT("Border");
	VisualMapper::ApplyStrokeLayout(NativeBorder, PrimitiveNode->Visual.Stroke);
	FUMGWidgetNode NativePaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(NativeBorder), PrimitiveNode);
	if (!TestTrue(TEXT("Primitive type does not replace exported outline regions"), NativePaint.Children.IsEmpty() && NativePaint.Path.SourcePaintGeometry.IsSet())) return false;
	TestFalse(TEXT("Source stroke does not retain a second native outline"), NativePaint.Brush.OutlineThickness.IsSet());
	TestTrue(TEXT("Primitive stroke uses its complete source area geometry"), NativePaint.Path.SourcePaintGeometry.IsSet()
		&& NativePaint.Path.SourcePaintGeometry->Geometry.StrokePaths.IsSet()
		&& NativePaint.Path.SourcePaintGeometry->Geometry.StrokePaths->Num() == 2);

	auto ResourceNode = MakeShared<FDesignNode>(*PrimitiveNode);
	FDesignFill ResourceFill;
	ResourceFill.Type = EDesignFillType::Image;
	ResourceFill.Image.Url = TEXT("/Textures/paint.png");
	ResourceNode->Visual.Fills.Add(ResourceFill);
	FUMGWidgetNode ResourceBorder;
	ResourceBorder.Type = TEXT("Border");
	ResourceBorder.Brush.Image.TexturePath = ResourceFill.Image.Url;
	VisualMapper::ApplyStrokeLayout(ResourceBorder, ResourceNode->Visual.Stroke);
	TestEqual(TEXT("Stroke layout leaves the resource fill intact"), ResourceBorder.Brush.Image.TexturePath.Get(FString()), ResourceFill.Image.Url);
	FUMGWidgetNode ResourcePaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(ResourceBorder), ResourceNode);
	if (!TestEqual(TEXT("Resource border retains its fill and grouped stroke"), ResourcePaint.Children.Num(), 2)) return false;
	TestEqual(TEXT("Resource fill remains on its original paint surface"),
		ResourcePaint.Children[0].Brush.Image.TexturePath.Get(FString()), ResourceFill.Image.Url);
	TestEqual(TEXT("Stroke layer retains the exported region"),
		ResourcePaint.Children[1].Path.SourcePaintGeometry->Geometry.StrokePaths.GetValue()[0].Path, Upper);

	const FString FillPath = TEXT("M0 0 L26.5 0 L26.5 17.25 L0 17.25 Z");
	auto FilledNode = MakeShared<FDesignNode>(*Node);
	FDesignFill Fill;
	Fill.Type = EDesignFillType::Color;
	Fill.SolidColor = TEXT("#FF0000");
	FilledNode->Visual.Fills.Add(Fill);
	FilledNode->Shape.Geometry = FillPath;
	FilledNode->Shape.FillRule = TEXT("nonzero");
	FilledNode->Shape.PaintGeometry.FillPaths = TArray<FDesignGeometryPath>{{FillPath, TEXT("nonzero")}};
	auto TranslucentNode = MakeShared<FDesignNode>(*FilledNode);
	TranslucentNode->Transform.SetOpacityValue(0.35f);
	FUMGWidgetNode TranslucentSurface = PathWidgetBuilder::CreatePathWidget(TranslucentNode);
	TranslucentSurface.Render.RenderOpacity = 0.35f;
	const FUMGWidgetNode Combined = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(TranslucentSurface), TranslucentNode);
	if (!TestEqual(TEXT("Fill and grouped stroke have separate paint owners"), Combined.Children.Num(), 2)) return false;
	TestEqual(TEXT("Object opacity applies once to the combined group"), Combined.Render.RenderOpacity.Get(1.0f), 0.35f);
	TestFalse(TEXT("The fill does not repeat group opacity"), Combined.Children[0].Render.RenderOpacity.IsSet());
	TestTrue(TEXT("Source regions retain their independent rules"), Combined.Children[1].Path.SourcePaintGeometry.IsSet()
		&& Combined.Children[1].Path.SourcePaintGeometry->Geometry.StrokePaths.GetValue()[1].FillRule == TEXT("evenodd"));
	TestTrue(TEXT("Stroke color stays separate from the fill and centerline"),
		Combined.Children[1].Brush.BrushColor.Get(FLinearColor::Transparent).Equals(FLinearColor::Blue)
		&& !Combined.Children[1].Path.StrokeThickness.IsSet());

	FUMGBoxShadowLayerData Shadow;
	Shadow.Color = FLinearColor::Black;
	Shadow.Blur = 8.0f;
	const FUMGWidgetNode Shadowed = VisualMapper::WrapWithBoxShadow(FUMGWidgetNode(Combined), TranslucentNode, {Shadow});
	if (!TestTrue(TEXT("Shadow capture retains its stroke group"), !Shadowed.Children.IsEmpty()
		&& !Shadowed.Children.Last().Children.IsEmpty())) return false;
	TestNotEqual(TEXT("Stroke and shadow content have distinct generated identities"),
		Shadowed.Children.Last().Id, Shadowed.Children.Last().Children[0].Id);
	auto GradientNode = MakeShared<FDesignNode>(*FilledNode);
	GradientNode->Visual.Fills.Reset();
	FDesignFill GradientFill;
	GradientFill.Type = EDesignFillType::Gradient;
	FDesignGradientStop& FirstStop = GradientFill.Gradient.Colors.AddDefaulted_GetRef();
	FirstStop.Color = TEXT("#80402080");
	FirstStop.Position = 0.0f;
	FDesignGradientStop& LastStop = GradientFill.Gradient.Colors.AddDefaulted_GetRef();
	LastStop.Color = TEXT("#20408040");
	LastStop.Position = 1.0f;
	GradientNode->Visual.Fills.Add(GradientFill);
	FUMGWidgetNode GradientSurface = PathWidgetBuilder::CreatePathWidget(GradientNode);
	VisualMapper::ApplyVisualProperties(GradientSurface, GradientNode);
	TestTrue(TEXT("The real single-gradient builder supplies a non-white placeholder"),
		GradientSurface.Path.FillColor.IsSet() && !GradientSurface.Path.FillColor->Equals(FLinearColor::White));
	const FLinearColor Tint(0.5f, 0.75f, 1.0f, 0.6f);
	GradientSurface.Brush.BrushColor = Tint;
	const auto OriginalStops = GradientSurface.Brush.Gradient.Stops;
	GradientSurface = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(GradientSurface), GradientNode);
	if (!TestEqual(TEXT("The gradient fill retains its original paint owner"), GradientSurface.Children.Num(), 2)) return false;
	const auto& GradientPaint = GradientSurface.Children[0];
	TestTrue(TEXT("The real gradient tint remains unchanged"), GradientPaint.Brush.BrushColor.Get(FLinearColor::White).Equals(Tint));
	TestTrue(TEXT("Gradient stop colors and alpha remain owned by the gradient"), GradientPaint.Brush.Gradient.Stops.Num() == 2
		&& GradientPaint.Brush.Gradient.Stops[0].Color.Equals(OriginalStops[0].Color)
		&& GradientPaint.Brush.Gradient.Stops[1].Color.Equals(OriginalStops[1].Color));

	FUMGWidgetNode FillSurface = PathWidgetBuilder::CreatePathWidget(FilledNode);
	FUMGWidgetNode Content;
	Content.Id = TEXT("Content");
	FillSurface.Children.Add(Content);
	FUMGWidgetNode Filled = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(FillSurface), FilledNode);
	TestTrue(TEXT("The original fill retains its content"), Filled.Children.Num() == 2
		&& Filled.Children[0].Children.Num() == 1 && Filled.Children[0].Children[0].Id == Content.Id);
	TestTrue(TEXT("The fill and stroke retain their respective geometry"), Filled.Children.Num() == 2
		&& Filled.Children[0].Path.Geometry.Get(FString()) == FillPath
		&& Filled.Children[1].Path.SourcePaintGeometry->Geometry.StrokePaths.GetValue()[0].Path == Upper);

	auto MultiNode = MakeShared<FDesignNode>(*TranslucentNode);
	FDesignFill SecondFill;
	SecondFill.Type = EDesignFillType::Image;
	SecondFill.Image.Url = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
	SecondFill.Image.Opacity = 0.4f;
	MultiNode->Visual.Fills.Add(SecondFill);
	int32 PaintLayers = 0;
	FUMGWidgetNode MultiContent = PathWidgetBuilder::CreatePathWidget(MultiNode);
	MultiContent.Render.RenderOpacity = 0.35f;
	FUMGWidgetNode MultiSurface = VisualMapperFillTransfer::WrapWithMultiFill(
		MoveTemp(MultiContent), MultiNode, FVisualMapperOptions(), &PaintLayers);
	MultiSurface = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(MultiSurface), MultiNode, PaintLayers);
	if (!TestTrue(TEXT("Multi-fill owners survive stroke wrapping"), MultiSurface.Children.Num() == 2
		&& MultiSurface.Children[0].Children.Num() == 2)) return false;
	const auto& MultiPaints = MultiSurface.Children[0].Children;
	TestTrue(TEXT("Both static fills remain in their original order"), MultiPaints[0].Path.FillColor.IsSet()
		&& MultiPaints[1].Brush.Image.TexturePath.Get(FString()) == SecondFill.Image.Url);
	TestEqual(TEXT("Image paint opacity is applied once"), MultiPaints[1].Render.RenderOpacity.Get(1.0f), SecondFill.Image.Opacity);
	TestEqual(TEXT("Object opacity remains on the final group"), MultiSurface.Render.RenderOpacity.Get(1.0f), 0.35f);
	TestFalse(TEXT("The inner paint group does not repeat object opacity"), MultiSurface.Children[0].Render.RenderOpacity.IsSet());

	const FString FitSource = MakeLeafNode(TEXT("fit-surface"), TEXT("Fit surface"), TEXT("FRAME"), 0, 0, 100, 80,
		TEXT(",\"size\":{\"x\":100,\"y\":80},\"opacity\":0.35,"
			"\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":1,\"b\":1}},"
			"{\"type\":\"IMAGE\",\"scaleMode\":\"FIT\",\"opacity\":0.4,\"imageRef\":\"/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture\"}],"
			"\"strokes\":[{\"type\":\"GRADIENT_LINEAR\",\"gradientHandlePositions\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0},{\"x\":0,\"y\":1}],"
			"\"gradientStops\":[{\"position\":0,\"color\":{\"r\":0,\"g\":0,\"b\":0,\"a\":1}},{\"position\":1,\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1}}]}],"
			"\"individualStrokeWeights\":{\"top\":1,\"right\":2,\"bottom\":1,\"left\":2},\"strokeAlign\":\"INSIDE\","
			"\"fillGeometry\":[{\"path\":\"M0 0L100 0L100 80L0 80Z\",\"windingRule\":\"NONZERO\"}],"
			"\"strokeGeometry\":[{\"path\":\"M-2 -2L102 -2L102 82L-2 82Z M2 2L98 2L98 78L2 78Z\",\"windingRule\":\"EVENODD\"}]"));
	FDesignDocument FitDocument;
	if (!TestTrue(TEXT("A supported FIT frame with nonuniform gradient stroke parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(FitSource), FitDocument))
		|| !TestEqual(TEXT("The FIT source has one node"), FitDocument.Children.Num(), 1)) return false;
	const auto& FitNode = FitDocument.Children[0];
	TestTrue(TEXT("The frame takes the existing ScaleBox fill route"), FitNode->Shape.Geometry.IsEmpty() && !FitNode->Visual.Stroke.Thickness.bUniform);
	FUMGWidgetNode FitContent;
	FitContent.Type = TEXT("CanvasPanel");
	FitContent.Id = FitNode->Id;
	FitContent.Name = FitNode->Name;
	VisualMapper::ApplyVisualProperties(FitContent, FitNode);
	int32 FitLayers = 0;
	FUMGWidgetNode FitSurface = VisualMapperFillTransfer::WrapWithMultiFill(MoveTemp(FitContent), FitNode, FVisualMapperOptions(), &FitLayers);
	if (!TestTrue(TEXT("The real mapper creates the supported FIT ScaleBox image layer"), FitLayers == 2
		&& FitSurface.Children[1].Type == UMGWidgetTypes::ScaleBox && FitSurface.Children[1].Children.Num() == 1)) return false;
	TestTrue(TEXT("The generated ScaleBox child is centered"),
		FitSurface.Children[1].Children[0].Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Center
		&& FitSurface.Children[1].Children[0].Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Center);
	const FString FitImage = FitSurface.Children[1].Children[0].Brush.Image.TexturePath.Get(FString());
	FitSurface = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(FitSurface), FitNode, FitLayers);
	if (!TestTrue(TEXT("The FIT frame retains its original fill layout"), FitSurface.Children.Num() == 2
		&& FitSurface.Children[0].Children.Num() == 2
		&& FitSurface.Children[0].Children[1].Type == UMGWidgetTypes::ScaleBox)) return false;
	const auto& FitPaint = FitSurface.Children[0].Children[1].Children[0];
	TestEqual(TEXT("The FIT paint retains the actual nested image"), FitPaint.Brush.Image.TexturePath.Get(FString()), FitImage);
	TestTrue(TEXT("The FIT image remains centered"), FitPaint.Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Center
		&& FitPaint.Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Center);
	TestTrue(TEXT("Nested image paint alpha and object opacity remain separate"), FMath::IsNearlyEqual(FitPaint.Render.RenderOpacity.Get(1.0f), 0.4f)
		&& FMath::IsNearlyEqual(FitSurface.Render.RenderOpacity.Get(1.0f), 0.35f));

	auto SideNode = MakeShared<FDesignNode>(*FilledNode);
	SideNode->Type = EDesignNodeType::Frame;
	SideNode->Visual.Stroke.Align = EDesignStrokeAlign::Inside;
	SideNode->Visual.Stroke.Thickness.bUniform = false;
	SideNode->Visual.Stroke.Thickness.Top = SideNode->Visual.Stroke.Thickness.Right = SideNode->Visual.Stroke.Thickness.Bottom = 0.0f;
	SideNode->Visual.Stroke.Thickness.Left = 2.0f;
	FUMGWidgetNode SideContent;
	SideContent.Type = TEXT("HorizontalBox");
	VisualMapper::ApplyVisualProperties(SideContent, SideNode);
	bool bStrokePainted = false;
	FUMGWidgetNode SidePaint = VisualMapper::WrapWithBorder(MoveTemp(SideContent), SideNode, &bStrokePainted);
	TestTrue(TEXT("A successfully constructed explicit side owns the stroke"), bStrokePainted);
	const int32 SideLayers = SidePaint.Children.Num();
	FUMGWidgetNode SideEffects = Design2UMGConverterVisualSupport::ApplyVisualEffectWrappers(
		MoveTemp(SidePaint), SideNode, FVisualMapperOptions(), bStrokePainted, false);
	TestEqual(TEXT("Later paint does not duplicate an already painted side"), SideEffects.Children.Num(), SideLayers);
	auto UnpaintedSideNode = MakeShared<FDesignNode>(*SideNode);
	UnpaintedSideNode->Visual.Stroke.Fill->Type = EDesignFillType::Gradient;
	FUMGWidgetNode UnpaintedContent;
	UnpaintedContent.Type = TEXT("HorizontalBox");
	bool bUnpaintedStroke = true;
	VisualMapper::WrapWithBorder(MoveTemp(UnpaintedContent), UnpaintedSideNode, &bUnpaintedStroke);
	TestFalse(TEXT("A side stroke that did not resolve a solid color has no owner"), bUnpaintedStroke);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserLinearGradientEndpointsTest,
	"Design2UMG.Gradient.FigmaParserLinearEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserLinearGradientEndpointsTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(
		MakeLeafNode(TEXT("2:1"), TEXT("LinearGradientNode"), TEXT("RECTANGLE"), 0, 0, 120, 80,
			TEXT(",\"fills\":[{"
				 "\"type\":\"GRADIENT_LINEAR\","
				 "\"gradientHandlePositions\":["
				 "{\"x\":0.2,\"y\":0.3},{\"x\":0.8,\"y\":0.7},{\"x\":0.0,\"y\":1.0}],"
				 "\"gradientStops\":["
				 "{\"color\":{\"r\":1,\"g\":0,\"b\":0,\"a\":1},\"position\":0},"
				 "{\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1},\"position\":1}]}]")));

	FDesignDocument Doc;
	TestTrue(TEXT("Figma linear gradient parses"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("One parsed linear gradient node"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || !Doc.Children[0].IsValid())
	{
		return false;
	}

	const FDesignGradient& Gradient = Doc.Children[0]->Visual.TopmostFill().Gradient;
	TestEqual(TEXT("Parsed linear gradient type"), Gradient.GradientType, EDesignGradientType::Linear);
	TestTrue(TEXT("Parsed linear start point"), Gradient.StartPoint.IsSet());
	TestTrue(TEXT("Parsed linear end point"), Gradient.EndPoint.IsSet());
	if (!Gradient.StartPoint.IsSet() || !Gradient.EndPoint.IsSet())
	{
		return false;
	}

	TestTrue(TEXT("Linear start X preserved"), FMath::IsNearlyEqual(Gradient.StartPoint.GetValue().X, 0.2f, 0.0001f));
	TestTrue(TEXT("Linear start Y preserved"), FMath::IsNearlyEqual(Gradient.StartPoint.GetValue().Y, 0.3f, 0.0001f));
	TestTrue(TEXT("Linear end X preserved"), FMath::IsNearlyEqual(Gradient.EndPoint.GetValue().X, 0.8f, 0.0001f));
	TestTrue(TEXT("Linear end Y preserved"), FMath::IsNearlyEqual(Gradient.EndPoint.GetValue().Y, 0.7f, 0.0001f));
	return true;
}

// ---------------------------------------------------------------------------
// Effects: blur / shadow
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserEffectsTest,
	"Design2UMG.Unit.FigmaParser.Effects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserEffectsTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadFigmaFixture(*this, TEXT("fills_effects_vars.json"), Doc))
		return false;

	// Second root child is EffectsFrame
	TestTrue(TEXT("Has effects frame"), Doc.Children.Num() >= 2);
	const TSharedPtr<FDesignNode>& EffectsFrame = Doc.Children[1];
	TestTrue(TEXT("EffectsFrame valid"), EffectsFrame.IsValid());
	TestEqual(TEXT("Effects count"), EffectsFrame->Visual.Effects.Num(), 2);
	TestEqual(TEXT("First effect type"), EffectsFrame->Visual.Effects[0].Type, EDesignEffectType::Blur);
	TestEqual(TEXT("First effect radius"), EffectsFrame->Visual.Effects[0].Radius, 4.0f);
	TestEqual(TEXT("Second effect type"), EffectsFrame->Visual.Effects[1].Type, EDesignEffectType::Shadow);
	TestEqual(TEXT("Second effect offset x"), EffectsFrame->Visual.Effects[1].Shadow.OffsetX, 2.0f);
	TestEqual(TEXT("Second effect offset y"), EffectsFrame->Visual.Effects[1].Shadow.OffsetY, 4.0f);
	TestFalse(TEXT("Omitted shadow behind flag uses the source default"), EffectsFrame->Visual.Effects[1].Shadow.bShowShadowBehindNode);

	return true;
}

// ---------------------------------------------------------------------------
// Variable references: boundVariables field
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserBoundVariablesTest,
	"Design2UMG.Unit.FigmaParser.BoundVariables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserBoundVariablesTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadFigmaFixture(*this, TEXT("fills_effects_vars.json"), Doc))
		return false;

	// Third root child is BoundVarsFrame
	TestTrue(TEXT("Has bound vars frame"), Doc.Children.Num() >= 3);
	const TSharedPtr<FDesignNode>& VarsFrame = Doc.Children[2];
	TestTrue(TEXT("VarsFrame valid"), VarsFrame.IsValid());
	TestEqual(TEXT("VarsFrame type"), VarsFrame->Type, EDesignNodeType::Frame);
	TestTrue(TEXT("Bound variables metadata captured"), VarsFrame->Metadata.Contains(TEXT("boundVariables")));
	TestTrue(
		TEXT("BoundVariablesJson keeps variable alias id"),
		VarsFrame->Metadata.FindRef(TEXT("boundVariables")).Contains(TEXT("VariableID:color/primary")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserPrivateStringFieldParityTest,
	"Design2UMG.Unit.FigmaParser.PrivateStringFieldParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserPrivateStringFieldParityTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(FString::Printf(
		TEXT("%s,%s"),
		*MakeContainerNode(TEXT("10:1"), TEXT("LayoutFrame"), TEXT("FRAME"), 0, 0, 120, 80, FString(),
			TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutWrap\":\"WRAP\","
				 "\"counterAxisAlignContent\":\"SPACE_BETWEEN\",\"layoutPositioning\":\"ABSOLUTE\","
				 "\"layoutAlign\":\"STRETCH\",\"primaryAxisSizingMode\":\"AUTO\",\"counterAxisSizingMode\":\"FIXED\","
				 "\"gridRowsSizing\":\"auto auto\",\"gridColumnsSizing\":\"1fr 1fr\","
				 "\"gridChildHorizontalAlign\":\"CENTER\",\"gridChildVerticalAlign\":\"MIN\"")),
		*MakeLeafNode(TEXT("10:2"), TEXT("InstanceNode"), TEXT("INSTANCE"), 140, 0, 80, 32,
			TEXT(",\"componentId\":\"12:34\",\"componentSetId\":\"set:5678\",\"mainComponentKey\":\"main-key-123\""))),
		TEXT(",\"components\":{},\"componentSets\":{},\"styles\":{}"));

	FDesignDocument Doc;
	TestTrue(TEXT("Figma document parses"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Two parsed root children"), Doc.Children.Num(), 2);
	if (Doc.Children.Num() != 2 || !Doc.Children[0].IsValid() || !Doc.Children[1].IsValid())
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& LayoutNode = Doc.Children[0];
	TestEqual(TEXT("LayoutWrap preserved"), LayoutNode->Metadata.FindRef(TEXT("layoutWrap")), FString(TEXT("WRAP")));
	TestEqual(TEXT("CounterAxisAlignContent preserved"), LayoutNode->Metadata.FindRef(TEXT("counterAxisAlignContent")), FString(TEXT("SPACE_BETWEEN")));
	TestTrue(TEXT("LayoutPositioning normalized"), LayoutNode->LayoutData.bAbsoluteLayoutPositioning);
	TestEqual(TEXT("LayoutAlign preserved"), LayoutNode->Metadata.FindRef(TEXT("layoutAlign")), FString(TEXT("STRETCH")));
	TestEqual(
		TEXT("LayoutAlign is normalized for shared layout decisions"),
		LayoutNode->LayoutData.SelfAlignment.Get(EDesignAxisAlignment::Start),
		EDesignAxisAlignment::Fill);
	TestEqual(TEXT("PrimaryAxisSizingMode preserved"), LayoutNode->Metadata.FindRef(TEXT("primaryAxisSizingMode")), FString(TEXT("AUTO")));
	TestEqual(TEXT("CounterAxisSizingMode preserved"), LayoutNode->Metadata.FindRef(TEXT("counterAxisSizingMode")), FString(TEXT("FIXED")));
	TestEqual(TEXT("GridRowsSizing preserved"), LayoutNode->Metadata.FindRef(TEXT("gridRowsSizing")), FString(TEXT("auto auto")));
	TestEqual(TEXT("GridColumnsSizing preserved"), LayoutNode->Metadata.FindRef(TEXT("gridColumnsSizing")), FString(TEXT("1fr 1fr")));
	TestEqual(TEXT("GridChildHorizontalAlign normalized"), LayoutNode->LayoutData.LayoutChildHorizontalAlign.Get(EDesignAxisAlignment::Start), EDesignAxisAlignment::Center);
	TestEqual(TEXT("GridChildVerticalAlign normalized"), LayoutNode->LayoutData.LayoutChildVerticalAlign.Get(EDesignAxisAlignment::End), EDesignAxisAlignment::Start);

	const TSharedPtr<FDesignNode>& InstanceNode = Doc.Children[1];
	TestTrue(TEXT("Instance flag preserved"), InstanceNode->Source().ComponentInstance.bIsInstance);
	TestEqual(TEXT("ComponentSetId preserved"), InstanceNode->Source().ComponentInstance.ComponentSetId, FString(TEXT("set:5678")));
	TestEqual(TEXT("MainComponentKey preserved"), InstanceNode->Source().ComponentInstance.MainComponentKey, FString(TEXT("main-key-123")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserExplicitGeometryMaskTest,
	"Design2UMG.Unit.FigmaParser.ExplicitGeometryMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserExplicitGeometryMaskTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeContainerNode(TEXT("ellipse-group"), TEXT("Shape A"), TEXT("GROUP"), 0, 0, 100, 80, FString::Printf(
			TEXT("%s,%s"),
			*MakeLeafNode(TEXT("ellipse-mask"), TEXT("Clip A"), TEXT("ELLIPSE"), 0, 0, 100, 80,
				TEXT(",\"isMask\":true")),
			*MakeLeafNode(TEXT("ellipse-content"), TEXT("Layer A"), TEXT("RECTANGLE"), 0, 0, 100, 80,
				FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(1, 0, 0))))),
		*MakeContainerNode(TEXT("rounded-group"), TEXT("Shape B"), TEXT("GROUP"), 120, 0, 100, 80, FString::Printf(
			TEXT("%s,%s"),
			*MakeLeafNode(TEXT("rounded-mask"), TEXT("Clip B"), TEXT("RECTANGLE"), 120, 0, 100, 80,
				TEXT(",\"isMask\":true,\"cornerRadius\":12")),
			*MakeLeafNode(TEXT("rounded-content"), TEXT("Layer B"), TEXT("RECTANGLE"), 120, 0, 100, 80,
				FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0, 1, 0))))),
		*MakeContainerNode(TEXT("mismatch-group"), TEXT("Shape C"), TEXT("GROUP"), 240, 0, 100, 80, FString::Printf(
			TEXT("%s,%s"),
			*MakeLeafNode(TEXT("mismatch-mask"), TEXT("Clip C"), TEXT("ELLIPSE"), 250, 0, 80, 80,
				TEXT(",\"isMask\":true")),
			*MakeLeafNode(TEXT("mismatch-content"), TEXT("Layer C"), TEXT("RECTANGLE"), 240, 0, 100, 80,
				FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0, 0, 1)))))));

	FDesignDocument Doc;
	TestTrue(TEXT("Geometry mask document parses"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("All mask groups remain"), Doc.Children.Num(), 3);
	if (Doc.Children.Num() != 3)
	{
		return false;
	}

	TestEqual(TEXT("Ellipse mask shape preserved"), Doc.Children[0]->Visual.GeometryMask.Shape, EDesignGeometryMaskShape::Ellipse);
	TestEqual(TEXT("Ellipse mask leaf is not emitted as content"), Doc.Children[0]->Children.Num(), 1);
	TestEqual(TEXT("Rounded rectangle mask shape preserved"), Doc.Children[1]->Visual.GeometryMask.Shape, EDesignGeometryMaskShape::RoundedRectangle);
	TestTrue(TEXT("Rounded rectangle radius preserved"),
		FMath::IsNearlyEqual(Doc.Children[1]->Visual.GeometryMask.CornerRadius.Uniform, 12.0f));
	TestEqual(TEXT("Mismatched mask is not approximated"), Doc.Children[2]->Visual.GeometryMask.Shape, EDesignGeometryMaskShape::None);
	return true;
}

// ---------------------------------------------------------------------------
// Scroll container stacking axis: explicit auto-layout axis wins over overflow axis
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaScrollAxisLayoutWinsOverOverflowTest,
	"Design2UMG.Unit.FigmaParser.ScrollAxisLayoutWinsOverOverflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaScrollAxisLayoutWinsOverOverflowTest::RunTest(const FString&)
{
	const auto MakeFrameJson = [](bool bHasLayoutMode, const TCHAR* LayoutMode, const TCHAR* OverflowDirection) -> FString
	{
		FString LayoutField;
		if (bHasLayoutMode)
		{
			LayoutField = FString::Printf(TEXT(",\"layoutMode\":\"%s\",\"itemSpacing\":12"), LayoutMode);
		}
		return FString::Printf(TEXT(R"({
		"document": {"type": "DOCUMENT", "children": [{"type": "CANVAS", "children": [{
			"id": "scroll-frame", "name": "Scroll Frame", "type": "FRAME", "clipsContent": true%s,
			"overflowDirection": "%s",
			"absoluteBoundingBox": {"x": 0, "y": 0, "width": 360, "height": 800},
			"children": [
				{"id": "c1", "name": "Card 1", "type": "FRAME",
				 "absoluteBoundingBox": {"x": 0, "y": 0, "width": 328, "height": 378},
				 "fills": [{"type": "SOLID", "color": {"r": 0, "g": 1, "b": 0, "a": 1}}]},
				{"id": "c2", "name": "Card 2", "type": "FRAME",
				 "absoluteBoundingBox": {"x": 0, "y": 390, "width": 328, "height": 378},
				 "fills": [{"type": "SOLID", "color": {"r": 0, "g": 0, "b": 1, "a": 1}}]}
			]
		}]}]}
	})"), *LayoutField, OverflowDirection);
	};

	struct FCase { bool bHasLayoutMode; const TCHAR* LayoutMode; const TCHAR* Overflow; const TCHAR* ExpectedOrientation; };
	const FCase Cases[] = {
		// Authored VERTICAL stack carrying a HORIZONTAL_SCROLLING overflow: children
		// must keep stacking vertically (Figma renders the layout axis).
		{true, TEXT("VERTICAL"), TEXT("HORIZONTAL_SCROLLING"), TEXT("Vertical")},
		// Authored HORIZONTAL stack carrying a VERTICAL_SCROLLING overflow.
		{true, TEXT("HORIZONTAL"), TEXT("VERTICAL_SCROLLING"), TEXT("Horizontal")},
		// Agreeing axes stay unchanged.
		{true, TEXT("VERTICAL"), TEXT("VERTICAL_SCROLLING"), TEXT("Vertical")},
		{true, TEXT("HORIZONTAL"), TEXT("HORIZONTAL_SCROLLING"), TEXT("Horizontal")},
		// Absolute (no auto-layout) containers keep following the overflow axis.
		{false, nullptr, TEXT("HORIZONTAL_SCROLLING"), TEXT("Horizontal")},
		{false, nullptr, TEXT("VERTICAL_SCROLLING"), TEXT("Vertical")},
	};

	for (const FCase& Case : Cases)
	{
		FDesignDocument Doc;
		if (!TestTrue(TEXT("Scroll-axis fixture parses"),
				FFigmaParser::ParseDocument(MakeFrameJson(Case.bHasLayoutMode, Case.LayoutMode, Case.Overflow), Doc))
			|| Doc.Children.Num() != 1)
		{
			return false;
		}
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());

		const FUMGWidgetNode* ScrollBox = nullptr;
		const auto Search = [&ScrollBox](const FUMGWidgetNode& Node, auto&& Self) -> void
		{
			if (ScrollBox)
			{
				return;
			}
			if (Node.Type == TEXT("ScrollBox"))
			{
				ScrollBox = &Node;
				return;
			}
			for (const FUMGWidgetNode& Child : Node.Children)
			{
				Self(Child, Self);
			}
		};
		Search(Data.RootWidget, Search);

		const FString Label = FString::Printf(TEXT("layout=%s overflow=%s -> ScrollBox found"),
			Case.bHasLayoutMode ? Case.LayoutMode : TEXT("none"), Case.Overflow);
		if (!TestNotNull(Label, ScrollBox))
		{
			return false;
		}
		const FString OrientationLabel = FString::Printf(TEXT("layout=%s overflow=%s -> orientation"),
			Case.bHasLayoutMode ? Case.LayoutMode : TEXT("none"), Case.Overflow);
		TestEqual(OrientationLabel, ScrollBox->ScrollBox.Orientation.Get(FString()), FString(Case.ExpectedOrientation));
	}
	return true;
}

// ---------------------------------------------------------------------------
// Error input
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserErrorInputTest,
	"Design2UMG.Unit.FigmaParser.ErrorInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserErrorInputTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("Invalid Figma export JSON"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("Failed to parse JSON"), EAutomationExpectedErrorFlags::Contains, 2);
	{
		FDesignDocument Doc;
		TestFalse(TEXT("Empty string returns false"), FFigmaParser::ParseDocument(TEXT(""), Doc));
	}
	{
		FDesignDocument Doc;
		TestFalse(TEXT("Malformed JSON returns false"), FFigmaParser::ParseDocument(TEXT("{\"document\":"), Doc));
	}
	{
		AddExpectedError(TEXT("Missing 'document' field"), EAutomationExpectedErrorFlags::Contains, 1);
		FDesignDocument Doc;
		TestFalse(TEXT("Missing document field returns false"), FFigmaParser::ParseDocument(TEXT("{\"foo\":1}"), Doc));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaParserReusedOutputTest,
	"Design2UMG.Unit.FigmaParser.ReusedOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaParserReusedOutputTest::RunTest(const FString&)
{
	FDesignDocument Document;
	if (!TestTrue(TEXT("First document parses"), FFigmaParser::ParseDocument(TEXT(R"({"fileKey":"first-file","document":{"id":"first","name":"First","type":"FRAME"}})"), Document))) return false;
	if (!TestTrue(TEXT("Second document parses"), FFigmaParser::ParseDocument(TEXT(R"({"document":{"id":"second","name":"Second","type":"FRAME"}})"), Document))) return false;
	TestEqual(TEXT("Second parse replaces children"), Document.Children.Num(), 1);
	TestFalse(TEXT("Second parse removes old metadata"), Document.Metadata.Contains(TEXT("fileKey")));
	if (Document.Children.Num() == 1) TestEqual(TEXT("Only the second node remains"), Document.Children[0]->Id, FString(TEXT("second")));
	AddExpectedError(TEXT("Missing 'document' field"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Missing document fails"), FFigmaParser::ParseDocument(TEXT("{}"), Document));
	TestEqual(TEXT("Failure clears the output"), Document.Children.Num(), 0);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExplicitNoWrapTest,
	"Design2UMG.Unit.FigmaParser.ExplicitNoWrap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaExplicitNoWrapTest::RunTest(const FString&)
{
	TArray<FString> Children;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const FString Id = FString::Printf(TEXT("item-%d"), Index);
		Children.Add(MakeLeafNode(*Id, *Id, TEXT("RECTANGLE"), Index * 30, Index % 2 == 0 ? 0 : 10,
			30, Index % 2 == 0 ? 40 : 20, TEXT(",\"layoutSizingHorizontal\":\"FILL\"")));
	}
	const FString Parent = MakeContainerNode(TEXT("row"), TEXT("Row"), TEXT("FRAME"), 0, 0, 180, 40,
		FString::Join(Children, TEXT(",")), TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutWrap\":\"NO_WRAP\",\"counterAxisAlignItems\":\"CENTER\""));
	FDesignDocument Doc;
	if (!TestTrue(TEXT("No-wrap document parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Parent), Doc))) return false;
	TestEqual(TEXT("Explicit NO_WRAP wins over child Y differences"), Doc.Children[0]->LayoutData.Layout, EDesignLayout::Horizontal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaNonSolidStrokeTest,
	"Design2UMG.Unit.FigmaParser.NonSolidStroke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaNonSolidStrokeTest::RunTest(const FString&)
{
	const FString Gradient = TEXT("{\"type\":\"GRADIENT_LINEAR\",\"gradientStops\":[{\"position\":0,\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1}},{\"position\":1,\"color\":{\"r\":0,\"g\":1,\"b\":0,\"a\":1}}]}");
	FDesignDocument Doc;
	const FString Shape = MakeLeafNode(TEXT("shape"), TEXT("Shape"), TEXT("RECTANGLE"), 0, 0, 100, 100,
		FString::Printf(TEXT(",\"strokeWeight\":2,\"strokes\":[%s],\"strokeGeometry\":[{\"path\":\"M0 0 L100 0 L100 100 L0 100 Z M2 2 L2 98 L98 98 L98 2 Z\",\"windingRule\":\"NONZERO\"}]"), *Gradient));
	if (!TestTrue(TEXT("Gradient stroke document parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Shape), Doc))) return false;
	TestTrue(TEXT("Gradient stroke retains its thickness"), Doc.Children[0]->Visual.Stroke.HasStroke());
	if (TestTrue(TEXT("Gradient stroke retains its paint"), Doc.Children[0]->Visual.Stroke.Fill.IsSet()))
	{
		TestTrue(TEXT("Stroke paint stays gradient, not black"), Doc.Children[0]->Visual.Stroke.Fill->Type == EDesignFillType::Gradient);
		TestEqual(TEXT("Stroke retains both gradient stops"), Doc.Children[0]->Visual.Stroke.Fill->Gradient.Colors.Num(), 2);
	}
	FUMGWidgetNode Surface;
	Surface.Type = TEXT("Border");
	Surface.Id = TEXT("shape");
	Surface.Name = TEXT("Shape");
	const FUMGWidgetNode Painted = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(Surface), Doc.Children[0]);
	TestEqual(TEXT("Stroke-only shape remains one native image"), Painted.Type, FString(TEXT("Image")));
	TestTrue(TEXT("Stroke-only shape adds no wrappers"), Painted.Children.IsEmpty());
	TestTrue(TEXT("Exported primitive stroke remains an area contour"), Painted.Path.SourcePaintGeometry.IsSet() && Painted.Path.SourcePaintGeometry->StrokePaint.IsSet());
	TestFalse(TEXT("An exported outline is not restroked"), Painted.Path.StrokeThickness.IsSet());
	TestTrue(TEXT("Exported contour keeps its source context"), Painted.Path.SourcePaintGeometry.IsSet());
	TestEqual(TEXT("Original gradient stops reach the native resource"), Painted.Path.SourcePaintGeometry->StrokePaint->Gradient.Stops.Num(), 2);
	TestFalse(TEXT("The stroke gradient has one paint owner"), Painted.Brush.Gradient.HasAny());
	const TSharedPtr<FDesignNode> Line = MakeShared<FDesignNode>(*Doc.Children[0]);
	Line->Type = EDesignNodeType::Line;
	Line->Shape.PaintGeometry.StrokePaths.Reset();
	Line->Transform.SetWidthValue(FDesignSize::Fixed(100));
	Line->Transform.SetHeightValue(FDesignSize::Fixed(0));
	Line->Visual.Stroke.Thickness.Uniform = 10;
	Line->Visual.Stroke.Cap = EDesignStrokeCap::Circular;
	Line->Visual.Stroke.Fill->Gradient.StartPoint = FVector2D(0.1f, 0.0f);
	Line->Visual.Stroke.Fill->Gradient.EndPoint = FVector2D(0.9f, 0.0f);
	FUMGWidgetNode LineWidget;
	LineWidget.Type = TEXT("Image");
	LineWidget.Bounds.W = 100;
	const FUMGWidgetNode PaintedLine = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(LineWidget), Line);
	TestEqual(TEXT("Cap painting does not change the layout width"), PaintedLine.WidgetStyle.WidthOverride.Get(0), 100.0f);
	TestEqual(TEXT("Cap padding shifts the centerline inside its canvas"), PaintedLine.Path.Geometry.Get(FString()), FString(TEXT("M5.000000 5.000000 L105.000000 5.000000")));
	TestTrue(TEXT("Native render expansion retains the authored centerline length"), PaintedLine.Render.RenderTransformScale.Get(FVector2D(1, 1)).Equals(FVector2D(1.1, 1), 0.0001));
	TestTrue(TEXT("A stroke-only line stays one native image"), PaintedLine.Type == TEXT("Image") && PaintedLine.Children.IsEmpty());
	TestTrue(TEXT("Independent canvas includes both half-stroke caps"), PaintedLine.Path.CanvasSize.Get(FVector2D::ZeroVector).Equals(FVector2D(110, 10)));
	TestTrue(TEXT("Gradient start keeps its authored line coordinate"), FMath::IsNearlyEqual(PaintedLine.Brush.Gradient.StartPoint.GetValue().X * 110.0f - 5.0f, 10.0f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Gradient end keeps its authored line coordinate"), FMath::IsNearlyEqual(PaintedLine.Brush.Gradient.EndPoint.GetValue().X * 110.0f - 5.0f, 90.0f, KINDA_SMALL_NUMBER));
	FUMGWidgetNode TransformedLine;
	TransformedLine.Type = TEXT("Image");
	TransformedLine.Bounds.W = 100;
	TransformedLine.Render.RenderTransformScale = FVector2D(2, 3);
	TransformedLine.Render.RenderAngle = 30.0f;
	TransformedLine.Render.RenderTransformPivot = FVector2D(0.25, 0.75);
	TransformedLine.Render.RenderTranslation = FVector2D(7, 9);
	const FUMGWidgetNode TransformedPaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(TransformedLine), Line);
	TestTrue(TEXT("Ink expansion composes with authored scale"), TransformedPaint.Render.RenderTransformScale.GetValue().Equals(FVector2D(2.2, 3), 0.0001));
	TestEqual(TEXT("Authored rotation survives"), TransformedPaint.Render.RenderAngle.GetValue(), 30.0f);
	TestTrue(TEXT("Authored pivot survives"), TransformedPaint.Render.RenderTransformPivot.GetValue().Equals(FVector2D(0.25, 0.75)));
	TestTrue(TEXT("Noncentral pivot correction follows the authored rotation and scale"), TransformedPaint.Render.RenderTranslation.GetValue().Equals(FVector2D(7.0 - 5.0 * FMath::Cos(PI / 6), 1.5), 0.0001));
	Line->Visual.Stroke.Cap = EDesignStrokeCap::Square;
	Line->Children.Add(MakeShared<FDesignNode>());
	FUMGWidgetNode ContentLine;
	ContentLine.Type = TEXT("Border");
	ContentLine.Bounds.W = 100;
	const FUMGWidgetNode ContentPaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(ContentLine), Line);
	TestTrue(TEXT("Line content retains the existing native overlay and two layers"), ContentPaint.Type == TEXT("Overlay") && ContentPaint.Children.Num() == 2);
	if (ContentPaint.Children.Num() == 2)
	{
		const FUMGWidgetNode& CapLayer = ContentPaint.Children[1];
		TestEqual(TEXT("Square caps do not widen the overlay's layout"), ContentPaint.Bounds.W, 100.0f);
		TestEqual(TEXT("Square cap image keeps the original desired width"), CapLayer.WidgetStyle.WidthOverride.GetValue(), 100.0f);
		TestTrue(TEXT("Square caps reserve the same half-stroke canvas extent"), CapLayer.Path.CanvasSize.GetValue().Equals(FVector2D(110, 10)));
	}
	Line->Children.Empty();
	Line->Visual.Stroke.Cap = EDesignStrokeCap::Butt;
	FUMGWidgetNode ButtLine;
	ButtLine.Type = TEXT("Image");
	const FUMGWidgetNode ButtPaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(ButtLine), Line);
	TestTrue(TEXT("Butt caps add no horizontal extent"), ButtPaint.Path.CanvasSize.GetValue().Equals(FVector2D(100, 10)));

	Doc.Children[0]->Visual.Stroke.Align = EDesignStrokeAlign::Inside;
	Doc.Children[0]->Visual.Fills.Add(Doc.Children[0]->Visual.Stroke.Fill.GetValue());
	FUMGWidgetNode Filled;
	Filled.Type = TEXT("Border");
	Filled.Id = TEXT("filled");
	Filled.Name = TEXT("Filled");
	Filled.Brush.Gradient = Painted.Path.SourcePaintGeometry->StrokePaint->Gradient;
	Filled.Brush.BrushColor = FLinearColor::White;
	Filled.Brush.OutlineThickness = 2.0f;
	Filled.WidgetStyle.StrokePadding = FMargin(1.0f);
	const FUMGWidgetNode PaintedBorder = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(Filled), Doc.Children[0]);
	if (!TestEqual(TEXT("An inside primitive retains fill and exported stroke owners"), PaintedBorder.Children.Num(), 2)) return false;
	TestFalse(TEXT("The fill does not keep a duplicate native outline"), PaintedBorder.Children[0].Brush.OutlineThickness.IsSet());
	TestTrue(TEXT("The stroke retains its source region"), PaintedBorder.Children[1].Path.SourcePaintGeometry.IsSet() && PaintedBorder.Children[1].Path.SourcePaintGeometry->StrokePaint.IsSet());
	TestFalse(TEXT("Source geometry owns the stroke inset"), PaintedBorder.Children[0].WidgetStyle.StrokePadding.IsSet());

	auto ImageNode = MakeShared<FDesignNode>(*Doc.Children[0]);
	ImageNode->Visual.Fills.Reset();
	FDesignFill ImageFill;
	ImageFill.Type = EDesignFillType::Image;
	ImageFill.Image.Url = TEXT("/Textures/fill.png");
	ImageNode->Visual.Fills.Add(ImageFill);
	FUMGWidgetNode ImageSurface;
	ImageSurface.Type = TEXT("Border");
	ImageSurface.Brush.BrushColor = FLinearColor(1, 1, 1, 0.4f);
	ImageSurface.Brush.Image.TexturePath = ImageFill.Image.Url;
	ImageSurface.Brush.Image.Transform = FUMGVisualBrushData::FImageTransformData();
	ImageSurface.Brush.Image.Transform->TX = 0.25f;
	const FUMGWidgetNode ImagePaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(ImageSurface), ImageNode);
	if (!TestEqual(TEXT("Image fill and stroke retain separate owners"), ImagePaint.Children.Num(), 2)) return false;
	TestEqual(TEXT("Fill alpha remains unchanged"), ImagePaint.Children[0].Brush.BrushColor.Get(FLinearColor::Transparent).A, 0.4f);
	TestEqual(TEXT("Image resource remains unchanged"), ImagePaint.Children[0].Brush.Image.TexturePath.Get(FString()), ImageFill.Image.Url);
	TestTrue(TEXT("Authored image coordinates remain unchanged"), ImagePaint.Children[0].Brush.Image.Transform.IsSet()
		&& ImagePaint.Children[0].Brush.Image.Transform->TX == 0.25f);
	const auto Frame = MakeShared<FDesignNode>(*Doc.Children[0]);
	Frame->Type = EDesignNodeType::Frame;
	Frame->Shape.PaintGeometry.StrokePaths.Reset();
	Frame->Visual.Stroke.Align = EDesignStrokeAlign::Center;
	Frame->Visual.Fills.Reset();
	FUMGWidgetNode FrameSurface;
	FrameSurface.Type = TEXT("Border");
	const FUMGWidgetNode FramePaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(FrameSurface), Frame);
	TestTrue(TEXT("A frame derives its real rounded contour without exported stroke geometry"),
		FramePaint.Path.Geometry.IsSet() && FramePaint.Brush.Gradient.Stops.Num() == 2);

	AddExpectedError(TEXT("FigmaParser: Unsupported stroke paint"), EAutomationExpectedErrorFlags::Contains, 1);
	const FString ImageShape = MakeLeafNode(TEXT("image-shape"), TEXT("ImageShape"), TEXT("RECTANGLE"), 0, 0, 100, 100,
		TEXT(",\"strokeWeight\":2,\"strokes\":[{\"type\":\"IMAGE\",\"imageRef\":\"test-image\"}]"));
	if (!TestTrue(TEXT("Image stroke document parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(ImageShape), Doc))) return false;
	TestFalse(TEXT("Unsupported image paint is not fabricated as black"), Doc.Children[0]->Visual.Stroke.HasStroke());

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaAbsoluteFlowFitSizeTest,
	"Design2UMG.Unit.FigmaParser.AbsoluteFlowFitSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaAbsoluteFlowFitSizeTest::RunTest(const FString&)
{
	const TSharedPtr<FDesignNode> Parent = MakeShared<FDesignNode>();
	Parent->Type = EDesignNodeType::Frame;
	Parent->Transform.SetWidthValue(FDesignSize::Fit(68));
	Parent->Transform.SetHeightValue(FDesignSize::Fixed(80));
	Parent->LayoutData.Layout = EDesignLayout::Horizontal;
	Parent->LayoutData.SetGap(8);
	FDesignPadding Padding;
	Padding.Left = Padding.Right = 10;
	Parent->LayoutData.SetPadding(Padding);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const TSharedPtr<FDesignNode> Child = MakeShared<FDesignNode>();
		Child->Type = EDesignNodeType::Rectangle;
		Child->Transform.SetWidthValue(FDesignSize::Fixed(Index == 1 ? 1000 : 20));
		Child->Transform.SetHeightValue(FDesignSize::Fixed(20));
		Child->LayoutData.bAbsoluteLayoutPositioning = Index == 1;
		Child->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#FF0000")));
		Parent->Children.Add(Child);
	}
	TestEqual(TEXT("Absolute child contributes no flow fit width"), SizeEstimator::EstimateNodeSize(Parent, 0.0f).X, 68.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
