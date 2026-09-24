// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Design2UMGConverter.h"
#include "Design2UMGConverterFlowChildPreResolution.h"
#include "Design2UMGConverterInternal.h"
#include "Design2UMGCoreTestHelpers.h"
#include "DesignTreeLimits.h"
#include "RefExpander.h"
#include "SizeEstimator.h"

namespace
{
	TSharedPtr<FDesignNode> MakeReferenceNode(
		const FString& Id,
		const FString& TargetId,
		const bool bReusable = true)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Id;
		Node->Type = EDesignNodeType::Ref;
		Node->Ref = TargetId;
		Node->bReusable = bReusable;
		return Node;
	}

	FDesignDocument BuildReferenceChain(const int32 TargetCount)
	{
		check(TargetCount > 0);

		FDesignDocument Document;
		Document.Children.Reserve(TargetCount + 1);
		for (int32 Index = 0; Index < TargetCount; ++Index)
		{
			const FString Id = FString::Printf(TEXT("chain-%d"), Index);
			if (Index + 1 < TargetCount)
			{
				Document.Children.Add(MakeReferenceNode(
					Id,
					FString::Printf(TEXT("chain-%d"), Index + 1)));
			}
			else
			{
				TSharedPtr<FDesignNode> Leaf = MakeShared<FDesignNode>();
				Leaf->Id = Id;
				Leaf->Name = Id;
				Leaf->Type = EDesignNodeType::Rectangle;
				Leaf->bReusable = true;
				Document.Children.Add(MoveTemp(Leaf));
			}
		}

		Document.Children.Add(MakeReferenceNode(TEXT("chain-entry"), TEXT("chain-0"), false));
		return Document;
	}

	FDesignDocument BuildCircularReferenceDocument()
	{
		FDesignDocument Document;
		Document.Children.Add(MakeReferenceNode(TEXT("cycle-a"), TEXT("cycle-b")));
		Document.Children.Add(MakeReferenceNode(TEXT("cycle-b"), TEXT("cycle-a")));
		Document.Children.Add(MakeReferenceNode(TEXT("cycle-entry"), TEXT("cycle-a"), false));
		return Document;
	}

	FDesignDocument BuildMissingReferenceDocument()
	{
		FDesignDocument Document;
		Document.Children.Add(MakeReferenceNode(TEXT("missing-entry"), TEXT("missing-target"), false));

		TSharedPtr<FDesignNode> ValidSibling = MakeShared<FDesignNode>();
		ValidSibling->Id = TEXT("valid-sibling");
		ValidSibling->Name = TEXT("ValidSibling");
		ValidSibling->Type = EDesignNodeType::Rectangle;
		ValidSibling->Transform.SetWidthValue(FDesignSize::Fixed(80.0f));
		ValidSibling->Transform.SetHeightValue(FDesignSize::Fixed(40.0f));
		ValidSibling->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#336699")));
		Document.Children.Add(MoveTemp(ValidSibling));
		return Document;
	}

	TSharedPtr<FDesignNode> MakeReusableFrame(const FString& Id)
	{
		TSharedPtr<FDesignNode> Frame = MakeShared<FDesignNode>();
		Frame->Id = Id;
		Frame->Name = Id;
		Frame->Type = EDesignNodeType::Frame;
		Frame->bReusable = true;
		Frame->Transform.SetWidthValue(FDesignSize::Fixed(80.0f));
		Frame->Transform.SetHeightValue(FDesignSize::Fixed(40.0f));
		return Frame;
	}

	FDesignDocument BuildNestedComponentChain(const int32 ReferenceCount)
	{
		check(ReferenceCount > 0);

		FDesignDocument Document;
		Document.Children.Reserve(ReferenceCount + 2);
		for (int32 Index = 0; Index <= ReferenceCount; ++Index)
		{
			const FString ComponentId = FString::Printf(TEXT("nested-component-%d"), Index);
			TSharedPtr<FDesignNode> Component = MakeReusableFrame(ComponentId);
			if (Index < ReferenceCount)
			{
				Component->Children.Add(MakeReferenceNode(
					FString::Printf(TEXT("nested-ref-%d"), Index),
					FString::Printf(TEXT("nested-component-%d"), Index + 1),
					false));
			}
			else
			{
				TSharedPtr<FDesignNode> Leaf = MakeShared<FDesignNode>();
				Leaf->Id = TEXT("nested-visible-leaf");
				Leaf->Name = TEXT("NestedVisibleLeaf");
				Leaf->Type = EDesignNodeType::Rectangle;
				Leaf->Transform.SetWidthValue(FDesignSize::Fixed(40.0f));
				Leaf->Transform.SetHeightValue(FDesignSize::Fixed(20.0f));
				Leaf->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#336699")));
				Component->Children.Add(MoveTemp(Leaf));
			}
			Document.Children.Add(MoveTemp(Component));
		}

		Document.Children.Add(MakeReferenceNode(
			TEXT("nested-chain-entry"),
			TEXT("nested-component-0"),
			false));
		return Document;
	}

	FDesignDocument BuildNestedComponentCycle()
	{
		FDesignDocument Document;
		TSharedPtr<FDesignNode> ComponentA = MakeReusableFrame(TEXT("nested-cycle-a"));
		ComponentA->Children.Add(MakeReferenceNode(
			TEXT("nested-cycle-a-to-b"),
			TEXT("nested-cycle-b"),
			false));
		TSharedPtr<FDesignNode> ComponentB = MakeReusableFrame(TEXT("nested-cycle-b"));
		ComponentB->Children.Add(MakeReferenceNode(
			TEXT("nested-cycle-b-to-a"),
			TEXT("nested-cycle-a"),
			false));
		Document.Children = {ComponentA, ComponentB, MakeReferenceNode(
			TEXT("nested-cycle-entry"),
			TEXT("nested-cycle-a"),
			false)};
		return Document;
	}

	FDesignDocument BuildNestedSameTargetDocument(const bool bCyclic)
	{
		FDesignDocument Document;
		TSharedPtr<FDesignNode> Component = MakeReusableFrame(TEXT("same-target-component"));
		TSharedPtr<FDesignNode> Leaf = MakeShared<FDesignNode>();
		Leaf->Id = TEXT("same-target-leaf");
		Leaf->Name = TEXT("SameTargetLeaf");
		Leaf->Type = EDesignNodeType::Rectangle;
		Leaf->Transform.SetWidthValue(FDesignSize::Fixed(40.0f));
		Leaf->Transform.SetHeightValue(FDesignSize::Fixed(20.0f));
		Leaf->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#336699")));
		Component->Children.Add(Leaf);

		TSharedPtr<FDesignNode> Outer = MakeReferenceNode(
			TEXT("same-target-outer"), TEXT("same-target-component"), false);
		TSharedPtr<FDesignNode> Inner = MakeReferenceNode(
			TEXT("same-target-inner"), TEXT("same-target-component"), false);
		TSharedPtr<FDesignNodePatch> OuterOverrides = MakeShared<FDesignNodePatch>();
		OuterOverrides->Children = TArray<TSharedPtr<FDesignNode>>{Inner};
		Outer->RefOverrides = OuterOverrides;

		if (bCyclic)
		{
			TSharedPtr<FDesignNode> Repeated = MakeReferenceNode(
				TEXT("same-target-repeated"), TEXT("same-target-component"), false);
			TSharedPtr<FDesignNodePatch> RepeatedOverrides = MakeShared<FDesignNodePatch>();
			RepeatedOverrides->Children = TArray<TSharedPtr<FDesignNode>>{
				MakeReferenceNode(TEXT("same-target-unreplaced"), TEXT("same-target-component"), false)};
			Repeated->RefOverrides = RepeatedOverrides;
			Component->Children = {Repeated};
		}

		Document.Children = {Component, Outer};
		return Document;
	}

	FDesignDocument BuildDescendantReplacedSameTargetDocument()
	{
		FDesignDocument Document;
		TSharedPtr<FDesignNode> Component = MakeReusableFrame(TEXT("descendant-target"));
		TSharedPtr<FDesignNode> Container = MakeReusableFrame(TEXT("descendant-container"));
		Container->bReusable = false;
		TSharedPtr<FDesignNode> Leaf = MakeShared<FDesignNode>();
		Leaf->Id = TEXT("descendant-leaf");
		Leaf->Name = TEXT("DescendantLeaf");
		Leaf->Type = EDesignNodeType::Rectangle;
		Leaf->Transform.SetWidthValue(FDesignSize::Fixed(40.0f));
		Leaf->Transform.SetHeightValue(FDesignSize::Fixed(20.0f));
		Leaf->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#336699")));
		Container->Children.Add(Leaf);
		Component->Children.Add(Container);

		TSharedPtr<FDesignNode> Outer = MakeReferenceNode(
			TEXT("descendant-outer"), TEXT("descendant-target"), false);
		FDesignNodeOverride ContainerOverride;
		ContainerOverride.Patch.Children = TArray<TSharedPtr<FDesignNode>>{
			MakeReferenceNode(TEXT("descendant-inner"), TEXT("descendant-target"), false)};
		Outer->Descendants.Add(TEXT("descendant-container"), MoveTemp(ContainerOverride));
		Document.Children = {Component, Outer};
		return Document;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionChainLimitTest,
	"Design2UMG.ReferenceExpansion.ChainLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionChainLimitTest::RunTest(const FString&)
{
	FDesignDocument LegalDocument = BuildReferenceChain(DesignTreeLimits::MaxReferenceChainDepth);
	const FDesignReferenceExpansionResult LegalResult =
		RefExpander::ExpandRef(LegalDocument.Children.Last(), LegalDocument);
	TestTrue(TEXT("A reference chain at the documented boundary expands"), LegalResult.IsSuccess());
	TestEqual(TEXT("Expanded chain keeps entry identity"), LegalResult.ExpandedNode->Id, FString(TEXT("chain-entry")));

	FDesignDocument OverLimitDocument = BuildReferenceChain(DesignTreeLimits::MaxReferenceChainDepth + 1);
	AddExpectedError(TEXT("reference chain limit"), EAutomationExpectedErrorFlags::Contains, 1);
	const FDesignReferenceExpansionResult OverLimitResult =
		RefExpander::ExpandRef(OverLimitDocument.Children.Last(), OverLimitDocument);
	TestEqual(
		TEXT("A non-circular chain over the boundary reports the specific failure"),
		OverLimitResult.Status,
		EDesignReferenceExpansionStatus::ChainLimitExceeded);
	TestEqual(TEXT("Over-limit failure preserves entry identity"), OverLimitResult.NodeId, FString(TEXT("chain-entry")));
	TestTrue(TEXT("Over-limit failure identifies the unresolved target"), !OverLimitResult.ReferencedNodeId.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionFailureResultTest,
	"Design2UMG.ReferenceExpansion.FailureResults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionFailureResultTest::RunTest(const FString&)
{
	FDesignDocument CircularDocument = BuildCircularReferenceDocument();
	AddExpectedError(TEXT("circular ref detected"), EAutomationExpectedErrorFlags::Contains, 1);
	const FDesignReferenceExpansionResult CircularResult =
		RefExpander::ExpandRef(CircularDocument.Children.Last(), CircularDocument);
	TestEqual(
		TEXT("Cycle is distinct from a missing target"),
		CircularResult.Status,
		EDesignReferenceExpansionStatus::CircularReference);
	TestEqual(TEXT("Cycle preserves entry identity"), CircularResult.NodeId, FString(TEXT("cycle-entry")));
	TestTrue(TEXT("Cycle reports its repeated target"), !CircularResult.ReferencedNodeId.IsEmpty());

	FDesignDocument MissingDocument = BuildMissingReferenceDocument();
	const FDesignReferenceExpansionResult MissingResult =
		RefExpander::ExpandRef(MissingDocument.Children[0], MissingDocument);
	TestEqual(
		TEXT("Missing target has a caller-visible status"),
		MissingResult.Status,
		EDesignReferenceExpansionStatus::MissingTarget);
	TestEqual(TEXT("Missing failure preserves node identity"), MissingResult.NodeId, FString(TEXT("missing-entry")));
	TestEqual(TEXT("Missing failure preserves target identity"), MissingResult.ReferencedNodeId, FString(TEXT("missing-target")));

	FDesignDocument InvalidDocument;
	InvalidDocument.Children.Add(MakeReferenceNode(TEXT("invalid-entry"), FString(), false));
	const FDesignReferenceExpansionResult InvalidResult =
		RefExpander::ExpandRef(InvalidDocument.Children[0], InvalidDocument);
	TestEqual(
		TEXT("Empty target has caller-visible invalid status"),
		InvalidResult.Status,
		EDesignReferenceExpansionStatus::InvalidReference);
	TestEqual(TEXT("Invalid failure preserves node identity"), InvalidResult.NodeId, FString(TEXT("invalid-entry")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionConversionOutcomeTest,
	"Design2UMG.ReferenceExpansion.ConversionOutcomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionConversionOutcomeTest::RunTest(const FString&)
{
	FDesignDocument MissingDocument = BuildMissingReferenceDocument();
	const FUMGWidgetData MissingData =
		FDesign2UMGConverter::Convert(MissingDocument, FDesign2UMGConversionSettings::Defaults());
	TestEqual(
		TEXT("Missing reference with a valid sibling is explicit degradation"),
		MissingData.GetConversionStatus(),
		EDesign2UMGConversionStatus::SucceededWithDegradation);
	TestEqual(TEXT("Valid sibling still converts"), MissingData.RootWidget.Id, FString(TEXT("valid-sibling")));
	TestTrue(TEXT("Degradation names the skipped node and reason"), MissingData.Warnings.ContainsByPredicate(
		[](const FString& Warning)
		{
			return Warning.Contains(TEXT("missing-entry")) && Warning.Contains(TEXT("missing-target"));
		}));

	FDesignDocument CircularDocument = BuildCircularReferenceDocument();
	AddExpectedError(TEXT("circular ref detected"), EAutomationExpectedErrorFlags::Contains, 1);
	const FUMGWidgetData CircularData =
		FDesign2UMGConverter::Convert(CircularDocument, FDesign2UMGConversionSettings::Defaults());
	TestEqual(
		TEXT("Circular expansion fails the complete conversion"),
		CircularData.GetConversionStatus(),
		EDesign2UMGConversionStatus::Failed);
	TestTrue(TEXT("Failed conversion has a specific node reason"), CircularData.ConversionError.Contains(TEXT("cycle-a")));
	TestTrue(TEXT("Failed conversion exposes no partial root"), CircularData.RootWidget.Type.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionCachedFailureTest,
	"Design2UMG.ReferenceExpansion.CachedFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionCachedFailureTest::RunTest(const FString&)
{
	FDesignDocument MissingDocument = BuildMissingReferenceDocument();
	TArray<FString> Warnings;
	FString FatalError;
	TSharedPtr<Design2UMGConverterInternal::FRefExpansionCache> MissingCache =
		MakeShared<Design2UMGConverterInternal::FRefExpansionCache>(MissingDocument, &Warnings, &FatalError);
	const TMap<FString, FString> EmptyTheme;
	Design2UMGConverterInternal::FFlowChildPreResolutionResolver MissingResolver(
		MissingDocument,
		EmptyTheme,
		MissingCache);
	TestFalse(TEXT("Missing reference first lookup has no expanded node"), MissingResolver.ExpandRef(MissingDocument.Children[0]).IsValid());
	Warnings.Reset();
	TestFalse(TEXT("Missing reference cache hit still has no expanded node"), MissingResolver.ExpandRef(MissingDocument.Children[0]).IsValid());
	TestTrue(TEXT("Missing reference cache hit restores degradation reason"), Warnings.ContainsByPredicate(
		[](const FString& Warning)
		{
			return Warning.Contains(TEXT("missing-entry")) && Warning.Contains(TEXT("missing-target"));
		}));

	FDesignDocument CircularDocument = BuildCircularReferenceDocument();
	TSharedPtr<Design2UMGConverterInternal::FRefExpansionCache> CircularCache =
		MakeShared<Design2UMGConverterInternal::FRefExpansionCache>(CircularDocument, &Warnings, &FatalError);
	Design2UMGConverterInternal::FFlowChildPreResolutionResolver CircularResolver(
		CircularDocument,
		EmptyTheme,
		CircularCache);
	AddExpectedError(TEXT("circular ref detected"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Circular reference first lookup has no expanded node"), CircularResolver.ExpandRef(CircularDocument.Children.Last()).IsValid());
	FatalError.Reset();
	TestFalse(TEXT("Circular reference cache hit still has no expanded node"), CircularResolver.ExpandRef(CircularDocument.Children.Last()).IsValid());
	TestTrue(TEXT("Circular reference cache hit restores fatal reason"), FatalError.Contains(TEXT("cycle-entry")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNestedReferenceExpansionDepthBoundaryTest,
	"Design2UMG.ReferenceExpansion.NestedDepthBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNestedReferenceExpansionDepthBoundaryTest::RunTest(const FString&)
{
	// Each nested component edge contributes one ref and one expanded frame to the effective tree.
	const int32 LegalReferenceCount = (DesignTreeLimits::MaxNodeDepth - 4) / 2;
	FDesignDocument LegalDocument = BuildNestedComponentChain(LegalReferenceCount);
	const FUMGWidgetData LegalData =
		FDesign2UMGConverter::Convert(LegalDocument, FDesign2UMGConversionSettings::Defaults());
	TestTrue(
		TEXT("Nested references below the effective-tree depth limit convert"),
		LegalData.GetConversionStatus() != EDesign2UMGConversionStatus::Failed);

	const int32 OverLimitReferenceCount = DesignTreeLimits::MaxNodeDepth / 2 + 1;
	FDesignDocument OverLimitDocument = BuildNestedComponentChain(OverLimitReferenceCount);
	AddExpectedError(TEXT("Design tree nesting is too deep"), EAutomationExpectedErrorFlags::Contains, 1);
	const FUMGWidgetData OverLimitData =
		FDesign2UMGConverter::Convert(OverLimitDocument, FDesign2UMGConversionSettings::Defaults());
	TestEqual(
		TEXT("Nested references above the effective-tree depth limit fail conversion"),
		OverLimitData.GetConversionStatus(),
		EDesign2UMGConversionStatus::Failed);
	TestTrue(
		TEXT("Nested-depth failure identifies the affected node and limit"),
		OverLimitData.ConversionError.Contains(TEXT("nested-"))
			&& OverLimitData.ConversionError.Contains(FString::FromInt(DesignTreeLimits::MaxNodeDepth)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNestedReferenceExpansionCycleTest,
	"Design2UMG.ReferenceExpansion.NestedCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNestedReferenceExpansionCycleTest::RunTest(const FString&)
{
	FDesignDocument Document = BuildNestedComponentCycle();
	AddExpectedError(TEXT("nested circular reference"), EAutomationExpectedErrorFlags::Contains, 1);
	const FUMGWidgetData Data =
		FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestEqual(
		TEXT("Nested component cycle fails conversion"),
		Data.GetConversionStatus(),
		EDesign2UMGConversionStatus::Failed);
	TestTrue(
		TEXT("Nested component cycle preserves ref node and target identity"),
		Data.ConversionError.Contains(TEXT("nested-cycle"))
			&& Data.ConversionError.Contains(TEXT("circular")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNestedSameTargetOverrideTest,
	"Design2UMG.ReferenceExpansion.NestedSameTargetOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNestedSameTargetOverrideTest::RunTest(const FString&)
{
	FDesignDocument LegalDocument = BuildNestedSameTargetDocument(false);
	const FUMGWidgetData LegalData =
		FDesign2UMGConverter::Convert(LegalDocument, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Finite nesting of one target converts"), LegalData.GetConversionStatus(),
		EDesign2UMGConversionStatus::Succeeded);
	TestEqual(TEXT("Outer instance remains the root"), LegalData.RootWidget.Id,
		FString(TEXT("same-target-outer")));
	TestTrue(TEXT("Converted tree contains the nested instance"),
		Design2UMGCoreTests::FindWidgetByIdRecursive(
			LegalData.RootWidget, TEXT("same-target-inner")) != nullptr);
	TestTrue(TEXT("Nested instance uses the template's default leaf"),
		Design2UMGCoreTests::FindWidgetByIdRecursive(
			LegalData.RootWidget, TEXT("same-target-leaf")) != nullptr);

	FDesignDocument CyclicDocument = BuildNestedSameTargetDocument(true);
	AddExpectedError(TEXT("nested circular reference"), EAutomationExpectedErrorFlags::Contains, 1);
	const FUMGWidgetData CyclicData =
		FDesign2UMGConverter::Convert(CyclicDocument, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Repeated template content still fails"), CyclicData.GetConversionStatus(),
		EDesign2UMGConversionStatus::Failed);
	TestTrue(TEXT("Circular failure identifies its target"),
		CyclicData.ConversionError.Contains(TEXT("same-target-component"))
		&& CyclicData.ConversionError.Contains(TEXT("circular")));
	TestTrue(TEXT("Circular failure exposes no partial root"), CyclicData.RootWidget.Type.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNestedSameTargetDescendantOverrideTest,
	"Design2UMG.ReferenceExpansion.NestedSameTargetDescendantOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNestedSameTargetDescendantOverrideTest::RunTest(const FString&)
{
	FDesignDocument Document = BuildDescendantReplacedSameTargetDocument();
	const FDesignReferenceExpansionResult OuterExpansion =
		RefExpander::ExpandRef(Document.Children.Last(), Document);
	if (!TestTrue(TEXT("Outer descendant replacement expands"), OuterExpansion.IsSuccess())) return false;
	if (!TestEqual(TEXT("Expanded outer keeps one container"), OuterExpansion.ExpandedNode->Children.Num(), 1)) return false;
	if (!TestEqual(TEXT("Container children were replaced"),
		OuterExpansion.ExpandedNode->Children[0]->Children.Num(), 1)) return false;
	const TSharedPtr<FDesignNode> Inner = OuterExpansion.ExpandedNode->Children[0]->Children[0];
	TestEqual(TEXT("Descendant children were replaced by inner reference"),
		Inner->Ref, FString(TEXT("descendant-target")));
	const FDesignReferenceExpansionResult InnerExpansion = RefExpander::ExpandRef(Inner, Document);
	if (!TestTrue(TEXT("Inner reference expands"), InnerExpansion.IsSuccess())) return false;
	TestTrue(TEXT("Inner reference uses template default content"),
		InnerExpansion.ExpandedNode->Children.Num() == 1
		&& InnerExpansion.ExpandedNode->Children[0]->Children.Num() == 1
		&& InnerExpansion.ExpandedNode->Children[0]->Children[0]->Id == TEXT("descendant-leaf"));

	const FUMGWidgetData Data =
		FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Finite descendant replacement converts"), Data.GetConversionStatus(),
		EDesign2UMGConversionStatus::Succeeded);
	TestTrue(TEXT("Converted descendant replacement keeps inner reference"),
		Design2UMGCoreTests::FindWidgetByIdRecursive(Data.RootWidget, TEXT("descendant-inner")) != nullptr);
	TestTrue(TEXT("Converted inner reference keeps template default leaf"),
		Design2UMGCoreTests::FindWidgetByIdRecursive(Data.RootWidget, TEXT("descendant-leaf")) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionEmptyTargetsTest,
	"Design2UMG.ReferenceExpansion.EmptyTargetIdentities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionEmptyTargetsTest::RunTest(const FString&)
{
	FDesignDocument Document = BuildMissingReferenceDocument();
	Document.Children[0] = MakeReferenceNode(TEXT("empty-first"), FString(), false);
	Document.Children.Insert(MakeReferenceNode(TEXT("empty-second"), FString(), false), 1);
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Empty targets degrade, rather than fail, conversion"),
		Data.GetConversionStatus(), EDesign2UMGConversionStatus::SucceededWithDegradation);
	TestEqual(TEXT("Valid sibling survives empty references"), Data.RootWidget.Id, FString(TEXT("valid-sibling")));
	for (const FString& Id : {FString(TEXT("empty-first")), FString(TEXT("empty-second"))})
	{
		TestTrue(TEXT("Every empty reference retains its node identity"), Data.Warnings.ContainsByPredicate(
			[&Id](const FString& Warning) { return Warning.Contains(Id); }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionWarningOrderTest,
	"Design2UMG.ReferenceExpansion.WarningOrderAndCacheReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionWarningOrderTest::RunTest(const FString&)
{
	FDesignDocument Document;
	constexpr int32 ReferenceCount = 4096;
	for (int32 Index = 0; Index < ReferenceCount; ++Index)
	{
		Document.Children.Add(MakeReferenceNode(FString::Printf(TEXT("empty-%d"), Index), FString(), false));
	}
	TArray<FString> Warnings;
	FString Error;
	auto Cache = MakeShared<Design2UMGConverterInternal::FRefExpansionCache>(Document, &Warnings, &Error);
	const TMap<FString, FString> Theme;
	Design2UMGConverterInternal::FFlowChildPreResolutionResolver Resolver(Document, Theme, Cache);
	for (const auto& Ref : Document.Children) Resolver.ExpandRef(Ref);
	const TArray<FString> FirstWarnings = Warnings;
	TestEqual(TEXT("Distinct missing nodes keep distinct diagnostics"), Warnings.Num(), ReferenceCount);
	for (const auto& Ref : Document.Children) Resolver.ExpandRef(Ref);
	TestTrue(TEXT("Cache replay neither duplicates nor reorders diagnostics"), Warnings == FirstWarnings);
	Warnings.Reset();
	for (const auto& Ref : Document.Children) Resolver.ExpandRef(Ref);
	TestTrue(TEXT("Cache replay restores all diagnostics after clearing the output"), Warnings == FirstWarnings);
	if (Warnings.Num() == ReferenceCount)
	{
		for (int32 Index = 0; Index < ReferenceCount; ++Index)
		{
			TestTrue(TEXT("Warning order follows first encounter"), Warnings[Index].Contains(
				FString::Printf(TEXT("'empty-%d'"), Index)));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionCopyIsolationTest,
	"Design2UMG.ReferenceExpansion.CopyIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionCopyIsolationTest::RunTest(const FString&)
{
	auto Source = MakeReusableFrame(TEXT("copy-root"));
	Source->Children = {MakeReusableFrame(TEXT("copy-child"))};
	auto Patch = MakeShared<FDesignNodePatch>();
	Patch->Children = TArray<TSharedPtr<FDesignNode>>{MakeReusableFrame(TEXT("patch-child"))};
	Source->RefOverrides = Patch;
	FDesignNodeOverride Override;
	Override.Replacement = MakeReusableFrame(TEXT("replacement"));
	Override.Patch.Children = TArray<TSharedPtr<FDesignNode>>{MakeReusableFrame(TEXT("replacement-child"))};
	Source->Descendants.Add(TEXT("copy-child"), Override);
	const auto Copy = RefExpander::DeepCopyNode(Source);
	if (!TestTrue(TEXT("Copy succeeds"), Copy.IsValid())) return false;
	Copy->Children[0]->Name = TEXT("changed-child");
	Copy->RefOverrides->Children.GetValue()[0]->Name = TEXT("changed-patch");
	Copy->Descendants.FindChecked(TEXT("copy-child")).Replacement->Name = TEXT("changed-replacement");
	Copy->Descendants.FindChecked(TEXT("copy-child")).Patch.Children.GetValue()[0]->Name = TEXT("changed-replacement-child");
	TestEqual(TEXT("Ordinary child is isolated"), Source->Children[0]->Name, FString(TEXT("copy-child")));
	TestEqual(TEXT("Instance patch child is isolated"), Patch->Children.GetValue()[0]->Name, FString(TEXT("patch-child")));
	TestEqual(TEXT("Replacement root is isolated"), Override.Replacement->Name, FString(TEXT("replacement")));
	TestEqual(TEXT("Replacement patch child is isolated"), Override.Patch.Children.GetValue()[0]->Name, FString(TEXT("replacement-child")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionCachedCopyIsolationTest,
	"Design2UMG.ReferenceExpansion.CachedCopyIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionCachedCopyIsolationTest::RunTest(const FString&)
{
	FDesignDocument Document;
	auto Component = MakeReusableFrame(TEXT("copy-component"));
	Component->Children.Add(MakeReusableFrame(TEXT("copy-leaf")));
	Document.Children = {Component, MakeReferenceNode(TEXT("copy-instance"), Component->Id, false)};
	FString Error;
	auto Cache = MakeShared<Design2UMGConverterInternal::FRefExpansionCache>(Document, nullptr, &Error);
	const TMap<FString, FString> Theme;
	Design2UMGConverterInternal::FFlowChildPreResolutionResolver Resolver(Document, Theme, Cache);
	const auto Snapshot = Resolver.ExpandRef(Document.Children.Last());
	const auto First = Resolver.MakeWorkingCopy(Document.Children.Last());
	if (!TestTrue(TEXT("Snapshot and first copy exist"), Snapshot.IsValid() && First.IsValid())) return false;
	First->Children[0]->Name = TEXT("changed");
	First->Transform.SetWidthValue(FDesignSize::Fixed(200));
	const auto Resolve = [&Resolver](const TSharedPtr<FDesignNode>& Ref) { return Resolver.MakeWorkingCopy(Ref); };
	SizeEstimator::PromoteFitContentSizes(First, Document, Resolve, nullptr, &Error);
	const auto Second = Resolver.MakeWorkingCopy(Document.Children.Last());
	if (!TestTrue(TEXT("Second working copy exists"), Second.IsValid())) return false;
	TestTrue(TEXT("Cache retains its original snapshot"), Resolver.ExpandRef(Document.Children.Last()) == Snapshot);
	TestTrue(TEXT("Copies have independent descendants"), First->Children[0] != Second->Children[0]);
	TestEqual(TEXT("Snapshot child remains unchanged"), Snapshot->Children[0]->Name, FString(TEXT("copy-leaf")));
	TestEqual(TEXT("Later copy restores original child"), Second->Children[0]->Name, FString(TEXT("copy-leaf")));
	TestEqual(TEXT("Later copy restores original width"), Second->Transform.Width.Value, 80.0f);
	TestTrue(TEXT("Ordinary cached copy and sizing succeed"), Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionBranchingOutputTest,
	"Design2UMG.ReferenceExpansion.BranchingOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionBranchingOutputTest::RunTest(const FString&)
{
	constexpr int32 Depth = 4;
	FDesignDocument Document = BuildNestedComponentChain(Depth);
	for (int32 Index = 0; Index < Depth; ++Index)
	{
		Document.Children[Index]->Children.Add(MakeReferenceNode(
			FString::Printf(TEXT("branch-%d"), Index),
			FString::Printf(TEXT("nested-component-%d"), Index + 1), false));
	}
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Finite shared DAG converts"), Data.GetConversionStatus() != EDesign2UMGConversionStatus::Failed);
	int32 LeafCount = 0;
	TFunction<void(const FUMGWidgetNode&)> CountLeaves = [&](const FUMGWidgetNode& Widget)
	{
		if (Widget.Id == TEXT("nested-visible-leaf")) ++LeafCount;
		for (const auto& Child : Widget.Children) CountLeaves(Child);
	};
	CountLeaves(Data.RootWidget);
	TestEqual(TEXT("Every independently expanded branch survives"), LeafCount, 1 << Depth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionStructuralOverrideTest,
	"Design2UMG.ReferenceExpansion.StructuralOverrideBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionStructuralOverrideTest::RunTest(const FString&)
{
	auto Node = MakeReferenceNode(TEXT("override-owner"), TEXT("component"), false);
	Node->Descendants.Add(TEXT("branch/old"), FDesignNodeOverride{});
	Node->Descendants.Add(TEXT("branching/keep"), FDesignNodeOverride{});
	Node->Descendants.Add(TEXT("other"), FDesignNodeOverride{});
	FDesignNodeOverride Override;
	Override.Patch.Children = TArray<TSharedPtr<FDesignNode>>{};
	RefExpander::ApplyDescendantOverride(Node, TEXT("branch"), Override);
	TestFalse(TEXT("Structural replacement discards old descendant"), Node->Descendants.Contains(TEXT("branch/old")));
	TestTrue(TEXT("A similar prefix is not a descendant"), Node->Descendants.Contains(TEXT("branching/keep")));
	TestTrue(TEXT("Unrelated overrides survive"), Node->Descendants.Contains(TEXT("other")));
	const FDesignNodeOverride* Applied = Node->Descendants.Find(TEXT("branch"));
	TestTrue(TEXT("Explicit empty children remain authored"), Applied && Applied->Patch.Children.IsSet() && Applied->Patch.Children->IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionScalarOverrideCopyTest,
	"Design2UMG.ReferenceExpansion.ScalarOverrideCopyIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionScalarOverrideCopyTest::RunTest(const FString&)
{
	auto Node = MakeReusableFrame(TEXT("override-copy-owner"));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FDesignNodeOverride Override;
		Override.Patch.Opacity = 0.5f;
		Node->Descendants.Add(FString::Printf(TEXT("item-%d"), Index), Override);
	}
	const auto Copy = RefExpander::DeepCopyNode(Node);
	if (!TestTrue(TEXT("Scalar override map copies"), Copy.IsValid())) return false;
	TestEqual(TEXT("All scalar overrides survive"), Copy->Descendants.Num(), 4);
	Copy->Descendants.FindChecked(TEXT("item-0")).Patch.Opacity = 0.75f;
	TestEqual(TEXT("Scalar edits do not leak to source"), Node->Descendants.FindChecked(TEXT("item-0")).Patch.Opacity.GetValue(), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionManyOverridesTest,
	"Design2UMG.ReferenceExpansion.ManyStructuralOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionManyOverridesTest::RunTest(const FString&)
{
	constexpr int32 BranchCount = 128;
	FDesignDocument Document;
	auto Base = MakeReusableFrame(TEXT("override-base"));
	auto Inner = MakeReferenceNode(TEXT("inner-overrides"), Base->Id);
	auto Outer = MakeReferenceNode(TEXT("outer-overrides"), Inner->Id, false);
	for (int32 Index = 0; Index < BranchCount; ++Index)
	{
		const FString Id = FString::Printf(TEXT("branch-%d"), Index);
		auto Branch = MakeReusableFrame(Id);
		Branch->Children.Add(MakeReusableFrame(TEXT("leaf")));
		Base->Children.Add(Branch);
		FDesignNodeOverride OldStyle;
		OldStyle.Patch.Opacity = 0.2f;
		Inner->Descendants.Add(Id + TEXT("/leaf"), OldStyle);
		FDesignNodeOverride Structural;
		if (Index % 4 == 0)
		{
			Structural.Replacement = RefExpander::DeepCopyNode(Branch);
		}
		else if (Index % 4 == 1)
		{
			Structural.Patch.Children = TArray<TSharedPtr<FDesignNode>>{MakeReusableFrame(TEXT("leaf"))};
		}
		else if (Index % 4 == 2)
		{
			Structural.Patch.Children = TArray<TSharedPtr<FDesignNode>>{};
		}
		Structural.Patch.Name = FString::Printf(TEXT("outer-%d"), Index);
		Outer->Descendants.Add(Id, Structural);
		if (Index % 4 == 1)
		{
			FDesignNodeOverride NewStyle;
			NewStyle.Patch.Opacity = 0.8f;
			Outer->Descendants.Add(Id + TEXT("/leaf"), NewStyle);
		}
	}
	// A prefix without the slash separator must not identify a replaced subtree.
	auto Similar = MakeReusableFrame(TEXT("branch-0extra"));
	Similar->Children.Add(MakeReusableFrame(TEXT("leaf")));
	Base->Children.Add(Similar);
	FDesignNodeOverride SimilarStyle;
	SimilarStyle.Patch.Opacity = 0.4f;
	Inner->Descendants.Add(TEXT("branch-0extra/leaf"), SimilarStyle);
	auto CaseBranch = MakeReusableFrame(TEXT("case-branch"));
	CaseBranch->Children.Add(MakeReusableFrame(TEXT("leaf")));
	Base->Children.Add(CaseBranch);
	FDesignNodeOverride CaseStyle;
	CaseStyle.Patch.Opacity = 0.3f;
	Inner->Descendants.Add(TEXT("CASE-BRANCH/leaf"), CaseStyle);
	FDesignNodeOverride CaseReplacement;
	CaseReplacement.Replacement = RefExpander::DeepCopyNode(CaseBranch);
	Outer->Descendants.Add(TEXT("case-branch"), CaseReplacement);
	Document.Children = {Base, Inner, Outer};
	const auto Result = RefExpander::ExpandRef(Outer, Document);
	if (!TestTrue(TEXT("Multiple structural overrides expand"), Result.IsSuccess())) return false;
	const auto Expanded = Result.ExpandedNode;
	if (!TestEqual(TEXT("Branch structure survives"), Expanded->Children.Num(), BranchCount + 2)) return false;
	for (int32 Index = 0; Index < BranchCount; ++Index)
	{
		const auto& Branch = Expanded->Children[Index];
		TestEqual(TEXT("Outer branch patch is applied"), Branch->Name, FString::Printf(TEXT("outer-%d"), Index));
		if (!TestEqual(TEXT("Empty replacement is distinguished from absence"), Branch->Children.Num(), Index % 4 == 2 ? 0 : 1)) continue;
		if (Branch->Children.IsEmpty()) continue;
		const float ExpectedOpacity = Index % 4 == 0 ? 1.0f : Index % 4 == 1 ? 0.8f : 0.2f;
		TestEqual(TEXT("Only replaced old descendants are discarded; outer styles survive"), Branch->Children[0]->Transform.Opacity, ExpectedOpacity);
	}
	TestEqual(TEXT("Similar path retains old style"), Expanded->Children[BranchCount]->Children[0]->Transform.Opacity, 0.4f);
	TestEqual(TEXT("Replacement ancestry preserves case-sensitive prefix semantics"), Expanded->Children.Last()->Children[0]->Transform.Opacity, 0.3f);
	TestEqual(TEXT("Source override map is unchanged"), Inner->Descendants.Num(), BranchCount + 2);
	TestEqual(TEXT("Source child style is unchanged"), Base->Children[0]->Children[0]->Transform.Opacity, 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionWideTreeConversionTest,
	"Design2UMG.ReferenceExpansion.WideTreeConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
	void CheckWideTreeConversion(FAutomationTestBase& Test, EDesignLayout Layout, bool bFillChildren)
	{
		constexpr int32 LeafCount = 2000;
		auto Root = MakeReusableFrame(TEXT("wide-root"));
		Root->bReusable = false;
		Root->LayoutData.Layout = Layout;
		Root->Transform.SetWidthValue(FDesignSize::Fixed(12000));
		TSet<FString> SourceLeafIds;
		for (int32 Index = 0; Index < LeafCount; ++Index)
		{
			auto Leaf = MakeShared<FDesignNode>();
			Leaf->Id = FString::Printf(TEXT("wide-leaf-%d"), Index);
			Leaf->Name = Leaf->Id;
			Leaf->Type = EDesignNodeType::Rectangle;
			Leaf->Transform.SetWidthValue((bFillChildren ? FDesignSize::Fill(1) : FDesignSize::Fixed(1)));
			Leaf->Transform.SetHeightValue(FDesignSize::Fixed(1));
			Leaf->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#336699")));
			SourceLeafIds.Add(Leaf->Id);
			Root->Children.Add(Leaf);
		}
		FDesignDocument Document;
		Document.Children.Add(Root);
		const auto Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		Test.TestTrue(TEXT("An ordinary wide tree converts"),
			Data.GetConversionStatus() != EDesign2UMGConversionStatus::Failed);
		TSet<FString> ConvertedLeafIds;
		TFunction<void(const FUMGWidgetNode&)> GatherLeafIds = [&](const FUMGWidgetNode& Widget)
		{
			if (SourceLeafIds.Contains(Widget.Id)) ConvertedLeafIds.Add(Widget.Id);
			for (const auto& Child : Widget.Children) GatherLeafIds(Child);
		};
		GatherLeafIds(Data.RootWidget);
		Test.TestEqual(TEXT("Conversion preserves every ordinary leaf"), ConvertedLeafIds.Num(), LeafCount);
	}
}

bool FReferenceExpansionWideTreeConversionTest::RunTest(const FString&)
{
	for (const EDesignLayout Layout : {EDesignLayout::None, EDesignLayout::Horizontal, EDesignLayout::Vertical})
	{
		CheckWideTreeConversion(*this, Layout, false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionWideFillTreeConversionTest,
	"Design2UMG.ReferenceExpansion.WideFillTreeConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionWideFillTreeConversionTest::RunTest(const FString&)
{
	CheckWideTreeConversion(*this, EDesignLayout::Horizontal, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionFitSiblingWidthTest,
	"Design2UMG.ReferenceExpansion.FillUsesLatestFitSiblingWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionFitSiblingWidthTest::RunTest(const FString&)
{
	auto Root = MakeReusableFrame(TEXT("sizing-root"));
	Root->LayoutData.Layout = EDesignLayout::Horizontal;
	Root->LayoutData.ExplicitGap = 10;
	Root->Transform.SetWidthValue(FDesignSize::Fixed(200));
	auto Fit = MakeReusableFrame(TEXT("fit-sibling"));
	Fit->Transform.SetWidthValue(FDesignSize::Fit(0));
	auto FitContent = MakeReusableFrame(TEXT("fit-content"));
	FitContent->Type = EDesignNodeType::Rectangle;
	FitContent->Transform.SetWidthValue(FDesignSize::Fixed(60));
	FitContent->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#336699")));
	Fit->Children.Add(FitContent);
	auto Text = MakeShared<FDesignNode>();
	Text->Id = TEXT("fill-text");
	Text->Type = EDesignNodeType::Text;
	Text->Transform.SetWidthValue(FDesignSize::Fill(0));
	Text->Transform.SetHeightValue(FDesignSize::Fit(0));
	Text->Text.FontSize = 16;
	Text->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	Text->Text.Content = TEXT("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu");
	Root->Children = {Fit, Text};
	const FVector2D ExpectedTextSize = SizeEstimator::EstimateNodeSize(Text, 130);
	const FVector2D StaleTextSize = SizeEstimator::EstimateNodeSize(Text, 190);
	TestTrue(TEXT("The fixture distinguishes current and stale sibling widths"), ExpectedTextSize.Y > StaleTextSize.Y);
	FDesignDocument Document;
	Document.Children.Add(Root);
	FString Error;
	const auto Resolve = [](const TSharedPtr<FDesignNode>& Node) { return Node; };
	SizeEstimator::PromoteFitContentSizes(Root, Document, Resolve, nullptr, &Error);
	TestTrue(TEXT("Mixed fit/fill sizing succeeds"), Error.IsEmpty());
	TestEqual(TEXT("Fit sibling resolves before the fill child is measured"), Fit->Transform.Width.Value, 60.0f);
	TestEqual(TEXT("Fill text wraps using the newly resolved sibling width"),
		Text->Transform.Height.Value, static_cast<float>(ExpectedTextSize.Y));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionFlowEstimateIsolationTest,
	"Design2UMG.ReferenceExpansion.FlowEstimateIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionFlowEstimateIsolationTest::RunTest(const FString&)
{
	auto Component = MakeReusableFrame(TEXT("estimate-component"));
	Component->Transform.SetWidthValue(FDesignSize::Fit(0));
	Component->Transform.SetHeightValue(FDesignSize::Fit(0));
	auto Text = MakeShared<FDesignNode>();
	Text->Id = TEXT("estimate-text");
	Text->Type = EDesignNodeType::Text;
	Text->Transform.SetWidthValue(FDesignSize::Fill(0));
	Text->Transform.SetHeightValue(FDesignSize::Fit(0));
	Text->Text.FontSize = 16;
	Text->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	Text->Text.Content = TEXT("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu");
	Component->Children.Add(Text);
	auto Ref = MakeReferenceNode(TEXT("estimate-instance"), Component->Id, false);
	Ref->Transform.SetWidthValue(FDesignSize::Fixed(80));
	Ref->Transform.SetHeightValue(FDesignSize::Fit(0));
	auto Parent = MakeReusableFrame(TEXT("estimate-parent"));
	Parent->LayoutData.Layout = EDesignLayout::Horizontal;
	Parent->Transform.SetWidthValue(FDesignSize::Fixed(300));
	Parent->Children.Add(Ref);
	FDesignDocument Document;
	Document.Children = {Component, Parent};
	FString Error;
	auto Cache = MakeShared<Design2UMGConverterInternal::FRefExpansionCache>(Document, nullptr, &Error);
	const TMap<FString, FString> Theme;
	Design2UMGConverterInternal::FFlowChildPreResolutionResolver Resolver(Document, Theme, Cache);
	const auto Snapshot = Resolver.ExpandRef(Ref);
	if (!TestTrue(TEXT("Reference snapshot exists"), Snapshot.IsValid())) return false;
	float NarrowHeight = 0.0f;
	for (const float Width : {80.0f, 160.0f})
	{
		Ref->Transform.SetWidthValue(FDesignSize::Fixed(Width));
		Ref->Transform.SetHeightValue(FDesignSize::Fit(0));
		const auto Independent = Resolver.ResolveExpandedChild(Ref, false);
		const auto Expected = SizeEstimator::EstimateNodeSize(Independent, 300);
		Resolver.PreResolveFlowChildren(Parent, FUMGWidgetNode{}, Parent->Children);
		TestEqual(TEXT("Read-only estimate matches an independent current-size copy"), Ref->Transform.Height.Value, static_cast<float>(Expected.Y));
		TestEqual(TEXT("Latest instance width survives"), Ref->Transform.Width.Value, Width);
		TestEqual(TEXT("Cached root width is unchanged"), Snapshot->Transform.Width.Value, 80.0f);
		TestTrue(TEXT("Cached text height remains authored fit"), Snapshot->Children[0]->Transform.Height.IsFit());
		TestEqual(TEXT("Cached text height is not promoted"), Snapshot->Children[0]->Transform.Height.Value, 0.0f);
		if (Width == 80.0f) NarrowHeight = Ref->Transform.Height.Value;
		else TestTrue(TEXT("The wider instance changes wrapping"), Ref->Transform.Height.Value < NarrowHeight);
	}
	TestTrue(TEXT("Flow estimation has no fatal error"), Error.IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
