// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DesignImportPlanning.h"
#include "Misc/ScopeExit.h"

#include "DesignImportPipeline.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "UMGJsonSerializer.h"

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

	FDesignDocument BuildFatalReferenceDocument()
	{
		FDesignDocument Document;
		Document.Children.Add(MakeReferenceNode(TEXT("pipeline-cycle-a"), TEXT("pipeline-cycle-b")));
		Document.Children.Add(MakeReferenceNode(TEXT("pipeline-cycle-b"), TEXT("pipeline-cycle-a")));
		Document.Children.Add(MakeReferenceNode(TEXT("pipeline-cycle-entry"), TEXT("pipeline-cycle-a"), false));
		return Document;
	}

	FDesignDocument BuildDegradedReferenceDocument()
	{
		FDesignDocument Document;
		Document.Children.Add(MakeReferenceNode(TEXT("pipeline-missing-entry"), TEXT("pipeline-missing-target"), false));

		TSharedPtr<FDesignNode> ValidSibling = MakeShared<FDesignNode>();
		ValidSibling->Id = TEXT("pipeline-valid-sibling");
		ValidSibling->Name = TEXT("PipelineValidSibling");
		ValidSibling->Type = EDesignNodeType::Rectangle;
		ValidSibling->Transform.SetWidthValue(FDesignSize::Fixed(80.0f));
		ValidSibling->Transform.SetHeightValue(FDesignSize::Fixed(40.0f));
		ValidSibling->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#336699")));
		Document.Children.Add(MoveTemp(ValidSibling));
		return Document;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionDegradationReachesCommitTest,
	"Design2UMG.ReferenceExpansion.DegradationReachesCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionDegradationReachesCommitTest::RunTest(const FString&)
{
	const FString OutputFolder = TEXT("/Game/Design2UMG/_TestOutput/ReferenceExpansionDegradation");
	const FString RepeatedOutputFolder = TEXT("/Game/Design2UMG/_TestOutput/ReferenceExpansionDegradationRepeated");
	Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputFolder);
	Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(RepeatedOutputFolder);
	ON_SCOPE_EXIT
	{
		Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputFolder);
		Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(RepeatedOutputFolder);
	};

	FDesignDocument Document = BuildDegradedReferenceDocument();
	const FImportAnalysisResult Analysis =
		DesignImportPlanning::AnalyzeImport(Document, FImportAnalysisInput{false});
	TestTrue(TEXT("Missing reference remains non-fatal when a valid sibling converts"), Analysis.ErrorMessage.IsEmpty());
	TestEqual(
		TEXT("Analysis preserves explicit degradation"),
		Analysis.Data.GetConversionStatus(),
		EDesign2UMGConversionStatus::SucceededWithDegradation);
	const FString AnalysisDataBeforeExecution = FUMGJsonSerializer::Serialize(Analysis.Data);

	FDesignImportRequest Request;
	Request.OutputPath = OutputFolder;
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	const FDesignImportExecutionResult Execution =
		DesignImportPipeline::Execute(Document, Request, &Analysis);
	TestTrue(TEXT("Degraded conversion still generates its valid content"), Execution.bSucceeded);
	TestEqual(
		TEXT("Missing reference reaches the existing degraded commit status"),
		Execution.Commit.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);
	TestTrue(TEXT("Degraded conversion records the generated asset"), !Execution.GeneratedAssetPath.IsEmpty());
	TestEqual(
		TEXT("Execution does not mutate precomputed analysis data"),
		FUMGJsonSerializer::Serialize(Analysis.Data),
		AnalysisDataBeforeExecution);

	Request.OutputPath = RepeatedOutputFolder;
	const FDesignImportExecutionResult RepeatedExecution =
		DesignImportPipeline::Execute(Document, Request, &Analysis);
	TestTrue(TEXT("Repeated execution from the same precomputed analysis succeeds"), RepeatedExecution.bSucceeded);
	TestEqual(
		TEXT("Repeated execution keeps the same degradation outcome"),
		RepeatedExecution.Commit.Status,
		Execution.Commit.Status);
	TestEqual(
		TEXT("Repeated execution still does not mutate precomputed analysis data"),
		FUMGJsonSerializer::Serialize(Analysis.Data),
		AnalysisDataBeforeExecution);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionFailureStopsGenerationTest,
	"Design2UMG.ReferenceExpansion.FailureStopsGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionFailureStopsGenerationTest::RunTest(const FString&)
{
	const FString OutputFolder = TEXT("/Game/Design2UMG/_TestOutput/ReferenceExpansionFailure");
	Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputFolder);
	ON_SCOPE_EXIT
	{
		Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputFolder);
	};

	FDesignDocument Document = BuildFatalReferenceDocument();
	AddExpectedError(TEXT("circular ref detected"), EAutomationExpectedErrorFlags::Contains, 1);
	const FImportAnalysisResult Analysis =
		DesignImportPlanning::AnalyzeImport(Document, FImportAnalysisInput{false});
	TestTrue(TEXT("Analysis exposes conversion failure"), Analysis.ErrorMessage.Contains(TEXT("pipeline-cycle-a")));
	TestEqual(
		TEXT("Analysis data retains failed conversion status"),
		Analysis.Data.GetConversionStatus(),
		EDesign2UMGConversionStatus::Failed);

	FDesignImportRequest Request;
	Request.OutputPath = OutputFolder;
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	const FDesignImportExecutionResult Execution =
		DesignImportPipeline::Execute(Document, Request, &Analysis);
	TestFalse(TEXT("Fatal reference conversion does not report success"), Execution.bSucceeded);
	TestTrue(TEXT("Fatal reference reason reaches execution result"), Execution.ErrorMessage.Contains(TEXT("pipeline-cycle-a")));
	TestTrue(TEXT("Fatal reference conversion creates no asset"), Execution.GeneratedAssetPath.IsEmpty());
	TestFalse(TEXT("Fatal reference conversion never commits"), Execution.Commit.IsSuccess());

	FUMGWidgetData FailedData = Analysis.Data;
	FArtifactGenerationRequest ArtifactRequest;
	ArtifactRequest.OutputPath = OutputFolder;
	const FArtifactGenerationResult ArtifactResult =
		DesignImportPipeline::GenerateArtifacts(FailedData, ArtifactRequest);
	TestFalse(TEXT("Direct artifact generation also rejects failed conversion data"), ArtifactResult.bSucceeded);
	TestTrue(TEXT("Direct artifact rejection preserves the conversion reason"), ArtifactResult.ErrorMessage == Analysis.ErrorMessage);
	TestTrue(TEXT("Direct artifact rejection creates no asset"), ArtifactResult.GeneratedAssetPath.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReferenceExpansionBranchingImportTest,
	"Design2UMG.ReferenceExpansion.BranchingImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FReferenceExpansionBranchingImportTest::RunTest(const FString&)
{
	const FString OutputFolder = TEXT("/Game/Design2UMG/_TestOutput/ReferenceBranchingImport");
	Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputFolder);
	ON_SCOPE_EXIT
	{
		Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputFolder);
	};
	FDesignDocument Document;
	constexpr int32 Depth = 4;
	for (int32 Index = 0; Index <= Depth; ++Index)
	{
		auto Component = MakeShared<FDesignNode>();
		Component->Id = FString::Printf(TEXT("branching-component-%d"), Index);
		Component->Name = Component->Id;
		Component->Type = EDesignNodeType::Frame;
		Component->bReusable = true;
		Component->Transform.SetWidthValue(FDesignSize::Fixed(80));
		Component->Transform.SetHeightValue(FDesignSize::Fixed(40));
		if (Index < Depth)
		{
			for (int32 Branch = 0; Branch < 2; ++Branch)
			{
				Component->Children.Add(MakeReferenceNode(
					FString::Printf(TEXT("branching-ref-%d-%d"), Index, Branch),
					FString::Printf(TEXT("branching-component-%d"), Index + 1), false));
			}
		}
		else
		{
			auto Leaf = MakeShared<FDesignNode>();
			Leaf->Id = TEXT("branching-visible-leaf");
			Leaf->Name = Leaf->Id;
			Leaf->Type = EDesignNodeType::Rectangle;
			Leaf->Transform.SetWidthValue(FDesignSize::Fixed(20));
			Leaf->Transform.SetHeightValue(FDesignSize::Fixed(10));
			Leaf->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#336699")));
			Component->Children.Add(Leaf);
		}
		Document.Children.Add(Component);
	}
	Document.Children.Add(MakeReferenceNode(TEXT("branching-entry"), TEXT("branching-component-0"), false));
	const FImportAnalysisResult Analysis = DesignImportPlanning::AnalyzeImport(Document, FImportAnalysisInput{false});
	if (!TestTrue(TEXT("Finite branching analysis succeeds"), Analysis.Data.GetConversionStatus() != EDesign2UMGConversionStatus::Failed)) return false;
	int32 LeafCount = 0;
	TFunction<void(const FUMGWidgetNode&)> CountLeaves = [&](const FUMGWidgetNode& Widget)
	{
		if (Widget.Id == TEXT("branching-visible-leaf")) ++LeafCount;
		for (const auto& Child : Widget.Children) CountLeaves(Child);
	};
	CountLeaves(Analysis.Data.RootWidget);
	TestEqual(TEXT("Analysis preserves every expanded leaf"), LeafCount, 1 << Depth);
	FDesignImportRequest Request;
	Request.OutputPath = OutputFolder;
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	const auto Execution = DesignImportPipeline::Execute(Document, Request, &Analysis);
	TestTrue(TEXT("Finite branching import succeeds"), Execution.bSucceeded);
	TestTrue(TEXT("Successful import has no fatal error"), Execution.ErrorMessage.IsEmpty());
	TestFalse(TEXT("Finite branching import creates an asset"), Execution.GeneratedAssetPath.IsEmpty());
	TestTrue(TEXT("Finite branching import commits"), Execution.Commit.IsSuccess());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
