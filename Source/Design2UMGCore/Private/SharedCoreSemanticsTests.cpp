// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "Design2UMGConverter.h"
#include "SizeEstimator.h"
#include "TextMeasurement.h"

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_AUTOMATION_WORKER
namespace
{
	TSharedPtr<FDesignNode> MakeSharedSemanticsNode(
		const FString& Id,
		const EDesignNodeType Type,
		const FDesignSize Width = FDesignSize::Fixed(120.0f),
		const FDesignSize Height = FDesignSize::Fixed(40.0f))
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Id;
		Node->Type = Type;
		Node->Transform.Width = Width;
		Node->Transform.Height = Height;
		Node->Transform.bHasWidth = true;
		Node->Transform.bHasHeight = true;
		Node->Transform.bSourceHasWidth = true;
		Node->Transform.bSourceHasHeight = true;
		Node->Transform.bEnabled = true;
		return Node;
	}

	const FUMGWidgetNode* FindWidgetByIdRecursive(const FUMGWidgetNode& Root, const FString& Id)
	{
		if (Root.Id == Id)
		{
			return &Root;
		}

		for (const FUMGWidgetNode& Child : Root.Children)
		{
			if (const FUMGWidgetNode* Match = FindWidgetByIdRecursive(Child, Id))
			{
				return Match;
			}
		}

		return nullptr;
	}

	TSet<FString> CollectNodeIds(const TArray<TSharedPtr<FDesignNode>>& Nodes)
	{
		TSet<FString> Ids;
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (Node.IsValid())
			{
				Ids.Add(Node->Id);
			}
		}
		return Ids;
	}

	FString NormalizeRelativePath(const FString& PluginBaseDir, const FString& AbsoluteFilePath)
	{
		FString RelativePath = AbsoluteFilePath;
		FPaths::MakePathRelativeTo(RelativePath, *PluginBaseDir);
		RelativePath.ReplaceInline(TEXT("\\"), TEXT("/"));
		const FString PluginNamePrefix = FPaths::GetCleanFilename(PluginBaseDir) + TEXT("/");
		if (RelativePath.StartsWith(PluginNamePrefix))
		{
			RelativePath.RightChopInline(PluginNamePrefix.Len(), EAllowShrinking::No);
		}
		return RelativePath;
	}

	bool ContainsAllTokens(const FString& Content, const TArray<FString>& Tokens)
	{
		for (const FString& Token : Tokens)
		{
			if (!Content.Contains(Token))
			{
				return false;
			}
		}
		return true;
	}

	void ValidateDuplicateSemanticsPattern(
		FAutomationTestBase& Test,
		const FString& RuleName,
		const TArray<FString>& CandidateFiles,
		const TArray<FString>& Tokens,
		const TSet<FString>& AllowedRelativePaths,
		const FString& PluginBaseDir)
	{
		TArray<FString> UnauthorizedMatches;
		int32 MatchedFiles = 0;

		for (const FString& AbsoluteFilePath : CandidateFiles)
		{
			if (FPaths::GetCleanFilename(AbsoluteFilePath).Contains(TEXT("Tests")))
			{
				continue;
			}

			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *AbsoluteFilePath))
			{
				continue;
			}

			if (!ContainsAllTokens(Content, Tokens))
			{
				continue;
			}

			++MatchedFiles;
			const FString RelativePath = NormalizeRelativePath(PluginBaseDir, AbsoluteFilePath);
			if (!AllowedRelativePaths.Contains(RelativePath))
			{
				UnauthorizedMatches.Add(RelativePath);
			}
		}

		Test.TestTrue(
			*FString::Printf(TEXT("%s has at least one known semantic source"), *RuleName),
			MatchedFiles > 0);
		Test.TestTrue(
			*FString::Printf(TEXT("%s has no unauthorized duplicate files"), *RuleName),
			UnauthorizedMatches.Num() == 0);
		if (UnauthorizedMatches.Num() > 0)
		{
			Test.AddError(FString::Printf(
				TEXT("%s duplicated in unsupported files: %s"),
				*RuleName,
				*FString::Join(UnauthorizedMatches, TEXT(", "))));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedSemanticsSkipRenderableParityTest,
	"Design2UMG.SharedSemantics.SkipRenderableParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedSemanticsSkipRenderableParityTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Root = MakeSharedSemanticsNode(TEXT("shared_semantics_root"), EDesignNodeType::Frame, FDesignSize::Fixed(640.0f), FDesignSize::Fixed(360.0f));
	Root->LayoutData.Layout = EDesignLayout::Vertical;

	TSharedPtr<FDesignNode> DisabledLeaf = MakeSharedSemanticsNode(TEXT("skip_disabled"), EDesignNodeType::Rectangle);
	DisabledLeaf->Transform.bEnabled = false;

	TSharedPtr<FDesignNode> DesignerOnlyLeaf = MakeSharedSemanticsNode(TEXT("skip_designer_only"), EDesignNodeType::Connection);
	TSharedPtr<FDesignNode> NonRenderableLeaf = MakeSharedSemanticsNode(TEXT("skip_non_renderable"), EDesignNodeType::Rectangle);

	TSharedPtr<FDesignNode> FlowContainerLeaf = MakeSharedSemanticsNode(TEXT("keep_flow_container"), EDesignNodeType::Frame);
	FlowContainerLeaf->LayoutData.Layout = EDesignLayout::Horizontal;

	TSharedPtr<FDesignNode> RenderableLeaf = MakeSharedSemanticsNode(TEXT("keep_renderable"), EDesignNodeType::Rectangle);
	RenderableLeaf->Visual.Fill = FDesignFill::FromColor(TEXT("#2288CC"));

	TSharedPtr<FDesignNode> TextLeaf = MakeSharedSemanticsNode(TEXT("keep_text"), EDesignNodeType::Text, FDesignSize::Fixed(220.0f), FDesignSize::Fixed(36.0f));
	TextLeaf->Text.Content = TEXT("Shared semantics consistency check");
	TextLeaf->Text.FontFamily = TEXT("Roboto");
	TextLeaf->Text.FontWeight = TEXT("Regular");
	TextLeaf->Text.FontSize = 16.0f;
	TextLeaf->Text.TextGrowth = EDesignTextGrowth::FixedWidth;

	Root->Children = {
		DisabledLeaf,
		DesignerOnlyLeaf,
		NonRenderableLeaf,
		FlowContainerLeaf,
		RenderableLeaf,
		TextLeaf
	};

	TArray<TSharedPtr<FDesignNode>> VisibleChildren;
	SizeEstimator::GatherVisibleChildren(Root, VisibleChildren);
	const TSet<FString> VisibleIds = CollectNodeIds(VisibleChildren);

	TestTrue(TEXT("Flow container leaf remains visible for sizing"), VisibleIds.Contains(TEXT("keep_flow_container")));
	TestTrue(TEXT("Renderable leaf remains visible for sizing"), VisibleIds.Contains(TEXT("keep_renderable")));
	TestTrue(TEXT("Text leaf remains visible for sizing"), VisibleIds.Contains(TEXT("keep_text")));
	TestFalse(TEXT("Disabled leaf is skipped for sizing"), VisibleIds.Contains(TEXT("skip_disabled")));
	TestFalse(TEXT("Designer-only leaf is skipped for sizing"), VisibleIds.Contains(TEXT("skip_designer_only")));
	TestFalse(TEXT("Non-renderable leaf is skipped for sizing"), VisibleIds.Contains(TEXT("skip_non_renderable")));

	FDesignDocument Doc;
	Doc.Children.Add(Root);
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);

	TestTrue(TEXT("Flow container leaf remains in converted tree"), FindWidgetByIdRecursive(Data.RootWidget, TEXT("keep_flow_container")) != nullptr);
	TestTrue(TEXT("Renderable leaf remains in converted tree"), FindWidgetByIdRecursive(Data.RootWidget, TEXT("keep_renderable")) != nullptr);
	TestTrue(TEXT("Text leaf remains in converted tree"), FindWidgetByIdRecursive(Data.RootWidget, TEXT("keep_text")) != nullptr);
	TestNull(TEXT("Disabled leaf is skipped in converted tree"), FindWidgetByIdRecursive(Data.RootWidget, TEXT("skip_disabled")));
	TestNull(TEXT("Designer-only leaf is skipped in converted tree"), FindWidgetByIdRecursive(Data.RootWidget, TEXT("skip_designer_only")));
	TestNull(TEXT("Non-renderable leaf is skipped in converted tree"), FindWidgetByIdRecursive(Data.RootWidget, TEXT("skip_non_renderable")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedSemanticsWidthHintParityTest,
	"Design2UMG.SharedSemantics.WidthHintParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedSemanticsWidthHintParityTest::RunTest(const FString& Parameters)
{
	auto MakeWidthProbeTextNode = [](const FString& Id, const FDesignSize& Width) -> TSharedPtr<FDesignNode>
	{
		TSharedPtr<FDesignNode> Node = MakeSharedSemanticsNode(Id, EDesignNodeType::Text, Width, FDesignSize::Fixed(36.0f));
		Node->Text.Content = TEXT("Width hint should stay consistent between sizing and wrap logic.");
		Node->Text.FontFamily = TEXT("Roboto");
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 16.0f;
		Node->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
		return Node;
	};

	struct FWidthHintProbeCase
	{
		FString Name;
		TSharedPtr<FDesignNode> Node;
		float ExpectedWidth = 0.0f;
	};

	const float ParentContentWidth = 280.0f;
	const TArray<FWidthHintProbeCase> Cases = {
		{ TEXT("fixed-width"), MakeWidthProbeTextNode(TEXT("width_fixed"), FDesignSize::Fixed(180.0f)), 180.0f },
		{ TEXT("fill-width-with-fallback"), MakeWidthProbeTextNode(TEXT("width_fill_fallback"), FDesignSize::Fill(300.0f)), 300.0f },
		{ TEXT("fill-width-parent-fallback"), MakeWidthProbeTextNode(TEXT("width_fill_parent"), FDesignSize::Fill(0.0f)), ParentContentWidth }
	};

	for (const FWidthHintProbeCase& ProbeCase : Cases)
	{
		const float EstimatedWidth = SizeEstimator::EstimateNodeSize(ProbeCase.Node, ParentContentWidth).X;
		const float WrapAt = TextMeasurement::ResolveTextWrapAt(ProbeCase.Node, ParentContentWidth);
		TestTrue(
			*FString::Printf(TEXT("%s sizing width follows shared width hint"), *ProbeCase.Name),
			FMath::IsNearlyEqual(EstimatedWidth, ProbeCase.ExpectedWidth, KINDA_SMALL_NUMBER));
		TestTrue(
			*FString::Printf(TEXT("%s wrap width follows shared width hint"), *ProbeCase.Name),
			FMath::IsNearlyEqual(WrapAt, ProbeCase.ExpectedWidth, KINDA_SMALL_NUMBER));
		TestTrue(
			*FString::Printf(TEXT("%s sizing and text wrap stay in parity"), *ProbeCase.Name),
			FMath::IsNearlyEqual(EstimatedWidth, WrapAt, KINDA_SMALL_NUMBER));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedSemanticsStaticDuplicateGuardTest,
	"Design2UMG.SharedSemantics.StaticDuplicateGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedSemanticsStaticDuplicateGuardTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
	TestTrue(TEXT("Design2UMG plugin exists for static guard"), Plugin.IsValid());
	if (!Plugin.IsValid())
	{
		return false;
	}

	const FString PluginBaseDir = Plugin->GetBaseDir();
	const FString CorePrivateDir = FPaths::Combine(PluginBaseDir, TEXT("Source/Design2UMGCore/Private"));
	const FString AutomationPrivateDir = FPaths::Combine(PluginBaseDir, TEXT("Source/Design2UMGEditor/Private/Automation"));
	const FString SizeEstimatorPath = FPaths::Combine(CorePrivateDir, TEXT("SizeEstimator.cpp"));
	const FString TextMeasurementPath = FPaths::Combine(CorePrivateDir, TEXT("TextMeasurement.cpp"));
	const FString SharedSemanticsPath = FPaths::Combine(CorePrivateDir, TEXT("SharedCoreSemantics.cpp"));

	TArray<FString> CandidateFiles;
	IFileManager::Get().FindFilesRecursive(CandidateFiles, *CorePrivateDir, TEXT("*.cpp"), true, false, false);
	IFileManager::Get().FindFilesRecursive(CandidateFiles, *AutomationPrivateDir, TEXT("*.cpp"), true, false, false);

	const TArray<FString> DesignerOnlyTokens = {
		TEXT("EDesignNodeType::Connection"),
		TEXT("EDesignNodeType::Note"),
		TEXT("EDesignNodeType::Prompt"),
		TEXT("EDesignNodeType::Context")
	};
	TSet<FString> DesignerOnlyAllowedPaths;
	DesignerOnlyAllowedPaths.Add(TEXT("Source/Design2UMGCore/Private/SharedCoreSemantics.cpp"));
	DesignerOnlyAllowedPaths.Add(TEXT("Source/Design2UMGEditor/Private/Automation/Design2UMGAutomationRender.cpp"));

	ValidateDuplicateSemanticsPattern(
		*this,
		TEXT("designer-only-type semantics token set"),
		CandidateFiles,
		DesignerOnlyTokens,
		DesignerOnlyAllowedPaths,
		PluginBaseDir);

	const TArray<FString> SkipFlowContainerTokens = {
		TEXT("Node->Type == EDesignNodeType::Ref"),
		TEXT("Node->Children.Num() == 0 && !HasRenderableVisual(Node)"),
		TEXT("Node->LayoutData.Layout != EDesignLayout::None")
	};
	TSet<FString> SkipFlowContainerAllowedPaths;
	SkipFlowContainerAllowedPaths.Add(TEXT("Source/Design2UMGCore/Private/SharedCoreSemantics.cpp"));

	ValidateDuplicateSemanticsPattern(
		*this,
		TEXT("skip-node flow-container semantics token set"),
		CandidateFiles,
		SkipFlowContainerTokens,
		SkipFlowContainerAllowedPaths,
		PluginBaseDir);

	const TArray<FString> WidthHintFallbackTokens = {
		TEXT("Node->Transform.Width.IsFill()"),
		TEXT("Node->Transform.Width.Value > 0.0f ? Node->Transform.Width.Value : ParentContentWidth"),
		TEXT("return Node->Transform.Width.Value > 0.0f ? Node->Transform.Width.Value : 0.0f;")
	};
	TSet<FString> WidthHintFallbackAllowedPaths;
	WidthHintFallbackAllowedPaths.Add(TEXT("Source/Design2UMGCore/Private/SharedCoreSemantics.cpp"));

	ValidateDuplicateSemanticsPattern(
		*this,
		TEXT("width-hint fallback semantics token set"),
		CandidateFiles,
		WidthHintFallbackTokens,
		WidthHintFallbackAllowedPaths,
		PluginBaseDir);

	FString SizeEstimatorSource;
	TestTrue(TEXT("Load SizeEstimator.cpp for delegation guard"), FFileHelper::LoadFileToString(SizeEstimatorSource, *SizeEstimatorPath));
	if (!SizeEstimatorSource.IsEmpty())
	{
		TestTrue(TEXT("SizeEstimator skip-node logic delegates to shared helper"),
			SizeEstimatorSource.Contains(TEXT("SharedSemantics::ShouldSkipNode(Node)")));
		TestTrue(TEXT("SizeEstimator width-hint logic delegates to shared helper"),
			SizeEstimatorSource.Contains(TEXT("SharedSemantics::ResolveWidthHint(Node, ParentContentWidth)")));
	}

	FString TextMeasurementSource;
	TestTrue(TEXT("Load TextMeasurement.cpp for delegation guard"), FFileHelper::LoadFileToString(TextMeasurementSource, *TextMeasurementPath));
	if (!TextMeasurementSource.IsEmpty())
	{
		TestTrue(TEXT("TextMeasurement width-hint logic delegates to shared helper"),
			TextMeasurementSource.Contains(TEXT("SharedSemantics::ResolveWidthHint(Node, ParentContentWidth)")));
	}

	FString SharedSemanticsSource;
	TestTrue(TEXT("Load SharedCoreSemantics.cpp for shared semantic source guard"), FFileHelper::LoadFileToString(SharedSemanticsSource, *SharedSemanticsPath));
	TestTrue(TEXT("Shared semantics helper retains canonical skip logic"), SharedSemanticsSource.Contains(TEXT("bool ShouldSkipNode(const TSharedPtr<FDesignNode>& Node)")));
	TestTrue(TEXT("Shared semantics helper retains canonical width-hint logic"), SharedSemanticsSource.Contains(TEXT("float ResolveWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)")));
	return true;
}
#endif
