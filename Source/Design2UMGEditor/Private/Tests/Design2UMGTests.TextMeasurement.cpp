// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGLog.h"
#include "Design2UMGTestUtils.h"
#include "FigmaParser.h"
#include "PenParser.h"
#include "TextMeasurement.h"
#include "TextMeasurementTestTrace.h"
#include "FontResolver.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_AUTOMATION_WORKER
using namespace Design2UMGTestUtils;

namespace
{
	FString GetTextMeasurementSourcePath()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source/Design2UMGCore/Private/TextMeasurement.cpp"))
			: FString();
	}

	FString GetTextMeasurementPublicHeaderPath()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source/Design2UMGCore/Public/TextMeasurement.h"))
			: FString();
	}

	FString GetTextMeasurementPrivateHeaderPath()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source/Design2UMGCore/Private/TextMeasurement.h"))
			: FString();
	}

	bool LoadTextMeasurementSource(FAutomationTestBase& Test, FString& OutSource)
	{
		const FString SourcePath = GetTextMeasurementSourcePath();
		Test.TestFalse(TEXT("TextMeasurement.cpp path resolved"), SourcePath.IsEmpty());
		if (SourcePath.IsEmpty())
		{
			return false;
		}

		Test.TestTrue(TEXT("Load TextMeasurement.cpp"), FFileHelper::LoadFileToString(OutSource, *SourcePath));
		return !OutSource.IsEmpty();
	}

	bool ExtractFunctionBody(
		FAutomationTestBase& Test,
		const FString& Source,
		const FString& FunctionSignature,
		const FString& FunctionLabel,
		FString& OutFunctionBody)
	{
		const int32 FuncPos = Source.Find(FunctionSignature, ESearchCase::CaseSensitive);
		Test.TestTrue(*FString::Printf(TEXT("%s function found"), *FunctionLabel), FuncPos != INDEX_NONE);
		if (FuncPos == INDEX_NONE)
		{
			return false;
		}

		int32 FuncEnd = INDEX_NONE;
		int32 BraceDepth = 0;
		for (int32 Index = FuncPos; Index < Source.Len(); ++Index)
		{
			if (Source[Index] == TEXT('{'))
			{
				++BraceDepth;
			}
			else if (Source[Index] == TEXT('}'))
			{
				--BraceDepth;
				if (BraceDepth == 0)
				{
					FuncEnd = Index;
					break;
				}
			}
		}

		Test.TestTrue(*FString::Printf(TEXT("%s function body found"), *FunctionLabel), FuncEnd != INDEX_NONE);
		if (FuncEnd == INDEX_NONE)
		{
			return false;
		}

		OutFunctionBody = Source.Mid(FuncPos, FuncEnd - FuncPos + 1);
		return true;
	}

	void VerifyTraceCoreFields(FAutomationTestBase& Test, const FTextResolutionTrace& Trace)
	{
		Test.TestFalse(TEXT("ResolutionPath must be non-empty"), Trace.ResolutionPath.IsEmpty());
		Test.TestTrue(TEXT("CompiledFontSize must be positive"), Trace.CompiledFontSize > 0.0f);
		Test.TestTrue(TEXT("FallbackScale must be in valid range when trace fallback evidence is available"),
			(Trace.FallbackScale >= 0.5f && Trace.FallbackScale <= 1.0f)
			|| Trace.ResolutionPath == TEXT("trace-log-unavailable"));
		Test.TestFalse(TEXT("CalibrationKey must be non-empty"), Trace.CalibrationKey.IsEmpty());
	}

	TSharedPtr<FDesignNode> MakeSharedCalibrationTextNode(const FString& Id, const FString& Content)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Id;
		Node->Type = EDesignNodeType::Text;
		Node->Text.Content = Content;
		Node->Text.FontFamily = TEXT("Geist");
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 36.0f;
		Node->Text.LineHeight = 1.35f;
		Node->Text.LetterSpacing = 0.0f;
		Node->Text.TextAlign = EDesignTextAlign::Left;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Middle;
		Node->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
		Node->Transform.Width = FDesignSize::Fixed(80.0f);
		Node->Transform.Height = FDesignSize::Fit(0.0f);
		Node->Transform.bHasWidth = true;
		Node->Transform.bHasHeight = true;
		return Node;
	}

	TSharedPtr<FDesignNode> MakeSharedCalibrationContainer(const FString& Id)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Id;
		Node->Type = EDesignNodeType::Frame;
		Node->LayoutData.Layout = EDesignLayout::Vertical;
		Node->Transform.Width = FDesignSize::Fixed(80.0f);
		Node->Transform.Height = FDesignSize::Fit(0.0f);
		Node->Transform.bHasWidth = true;
		Node->Transform.bHasHeight = true;
		return Node;
	}

	TSharedPtr<FDesignNode> MakeBaselineFallbackProbeNode(
		const FString& Id,
		const FString& Content,
		const bool bExplicitParagraphBreak = false)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Id;
		Node->Type = EDesignNodeType::Text;
		Node->Text.Content = bExplicitParagraphBreak ? FString::Printf(TEXT("%s\nsecond line"), *Content) : Content;
		Node->Text.FontFamily = TEXT("Inter");
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 16.0f;
		Node->Text.LineHeight = 1.25f;
		Node->Text.LetterSpacing = 0.0f;
		Node->Text.TextAlign = EDesignTextAlign::Left;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
		Node->Text.TextGrowth = EDesignTextGrowth::Auto;
		Node->Transform.Width = FDesignSize::Fit(0.0f);
		Node->Transform.Height = FDesignSize::Fit(0.0f);
		Node->Transform.bHasWidth = false;
		Node->Transform.bHasHeight = false;
		Node->Transform.bSourceHasWidth = false;
		return Node;
	}

	TSharedPtr<FDesignNode> MakeIntrinsicFallbackProbeNode(
		const FString& Id,
		const FString& Content,
		const FString& FontFamily)
	{
		TSharedPtr<FDesignNode> Node = MakeBaselineFallbackProbeNode(Id, Content);
		Node->Text.FontFamily = FontFamily;
		Node->Text.LineHeight = 0.0f;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
		Node->Transform.Width = FDesignSize::Fit(0.0f);
		Node->Transform.Height = FDesignSize::Fit(0.0f);
		Node->Transform.bHasWidth = false;
		Node->Transform.bHasHeight = false;
		Node->Transform.bSourceHasWidth = false;
		Node->MutableSource().bExpandedRefInstance = true;
		return Node;
	}

	FDesignDocument BuildSharedCalibrationFitContentDocument(const bool bExplicitFirst, const bool bUseRef)
	{
		const FString AutoContent = TEXT("Design2UMG text calibration should stay stable across different wrap widths and never depend on evaluation order.");
		const FString ExplicitContent = TEXT("Design2UMG text calibration should stay stable across\ndifferent wrap widths and never depend on evaluation order.");
		TSharedPtr<FDesignNode> AutoNode = MakeSharedCalibrationTextNode(TEXT("shared_auto"), AutoContent);
		TSharedPtr<FDesignNode> ExplicitNode = MakeSharedCalibrationTextNode(TEXT("shared_explicit"), ExplicitContent);

		auto AppendChildren = [&](const TSharedPtr<FDesignNode>& Parent)
		{
			if (bExplicitFirst)
			{
				Parent->Children.Add(ExplicitNode);
				Parent->Children.Add(AutoNode);
			}
			else
			{
				Parent->Children.Add(AutoNode);
				Parent->Children.Add(ExplicitNode);
			}
		};

		FDesignDocument Doc;
		if (bUseRef)
		{
			TSharedPtr<FDesignNode> Component = MakeSharedCalibrationContainer(TEXT("shared_component"));
			Component->bReusable = true;
			AppendChildren(Component);
			Doc.Children.Add(Component);

			TSharedPtr<FDesignNode> Screen = MakeSharedCalibrationContainer(TEXT("screen"));
			TSharedPtr<FDesignNode> Instance = MakeShared<FDesignNode>();
			Instance->Id = TEXT("shared_component_instance");
			Instance->Name = TEXT("shared_component_instance");
			Instance->Type = EDesignNodeType::Ref;
			Instance->Ref = TEXT("shared_component");
			Screen->Children.Add(Instance);
			Doc.Children.Add(Screen);
			return Doc;
		}

		TSharedPtr<FDesignNode> Screen = MakeSharedCalibrationContainer(TEXT("screen"));
		AppendChildren(Screen);
		Doc.Children.Add(Screen);
		return Doc;
	}

	bool ExtractSharedCalibrationHeights(
		const FUMGWidgetData& Data,
		FAutomationTestBase& Test,
		float& OutRootHeight,
		float& OutExplicitHeight,
		float& OutAutoHeight)
	{
		const FUMGWidgetNode* RootWidget = FindWidgetById(Data.RootWidget, TEXT("screen"));
		Test.TestTrue(TEXT("Find fit-content root"), RootWidget != nullptr);
		if (!RootWidget)
		{
			return false;
		}

		const FUMGWidgetNode* ExplicitWidget = FindWidgetById(Data.RootWidget, TEXT("shared_explicit"));
		Test.TestTrue(TEXT("Find explicit multiline text"), ExplicitWidget != nullptr);
		if (!ExplicitWidget)
		{
			return false;
		}

		const FUMGWidgetNode* AutoWidget = FindWidgetById(Data.RootWidget, TEXT("shared_auto"));
		Test.TestTrue(TEXT("Find auto multiline text"), AutoWidget != nullptr);
		if (!AutoWidget)
		{
			return false;
		}

		OutRootHeight = RootWidget->Bounds.H;
		OutExplicitHeight = ExplicitWidget->Bounds.H;
		OutAutoHeight = AutoWidget->Bounds.H;
		return true;
	}

	TSharedPtr<FDesignNode> FindFirstTextNodeByFamily(
		const TArray<TSharedPtr<FDesignNode>>& Nodes,
		const FString& FamilySubstring)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}
			if (Node->Type == EDesignNodeType::Text
				&& Node->Text.FontSize > 0.0f
				&& Node->Text.FontFamily.Contains(FamilySubstring, ESearchCase::IgnoreCase))
			{
				return Node;
			}
			if (TSharedPtr<FDesignNode> Found = FindFirstTextNodeByFamily(Node->Children, FamilySubstring))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementCalibrationCacheUsesNodeContextTest,
	"Design2UMG.Text.CalibrationCacheUsesNodeContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementCalibrationCacheUsesNodeContextTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping text calibration cache test."));
		return true;
	}

	auto MakeNode = [](const float InWidth, const float InLetterSpacing, const FString& InContent)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Type = EDesignNodeType::Text;
		Node->Text.Content = InContent;
		Node->Text.FontFamily = TEXT("Geist");
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 36.0f;
		Node->Text.LineHeight = 1.35f;
		Node->Text.LetterSpacing = InLetterSpacing;
		Node->Text.TextAlign = EDesignTextAlign::Left;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Middle;
		Node->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
		Node->Transform.Width = FDesignSize::Fixed(InWidth);
		Node->Transform.bHasWidth = true;
		return Node;
	};

	const FString LongContent = TEXT("Design2UMG text calibration should stay stable across different wrap widths and never depend on evaluation order.");
	const TSharedPtr<FDesignNode> NarrowNode = MakeNode(80.0f, 0.0f, LongContent);
	const TSharedPtr<FDesignNode> WideNode = MakeNode(320.0f, 0.0f, LongContent);
	TextMeasurement::ResetCache();
	const float NarrowFirst = TextMeasurement::ResolveTextFontSizeForUMG(NarrowNode);
	const float WideSecond = TextMeasurement::ResolveTextFontSizeForUMG(WideNode);

	TextMeasurement::ResetCache();
	const float WideFirst = TextMeasurement::ResolveTextFontSizeForUMG(WideNode);
	const float NarrowSecond = TextMeasurement::ResolveTextFontSizeForUMG(NarrowNode);

	TestTrue(TEXT("Narrow calibration resolved"), NarrowFirst > 0.0f && NarrowSecond > 0.0f);
	TestTrue(TEXT("Wide calibration resolved"), WideFirst > 0.0f && WideSecond > 0.0f);
	TestFalse(TEXT("Different wrap widths should not collapse to one cached size"),
		FMath::IsNearlyEqual(NarrowFirst, WideFirst, 0.01f));
	TestTrue(TEXT("Narrow calibration is order independent"),
		FMath::IsNearlyEqual(NarrowFirst, NarrowSecond, 0.01f));
	TestTrue(TEXT("Wide calibration is order independent"),
		FMath::IsNearlyEqual(WideFirst, WideSecond, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedCalibrationFitContentOrderIndependenceTest,
	"Design2UMG.Text.SharedCalibrationFitContentOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedCalibrationFitContentOrderIndependenceTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping shared calibration fit-content test."));
		return true;
	}

	float ExplicitFirstRootHeight = 0.0f;
	float ExplicitFirstExplicitHeight = 0.0f;
	float ExplicitFirstAutoHeight = 0.0f;
	{
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(BuildSharedCalibrationFitContentDocument(true, false));
		if (!ExtractSharedCalibrationHeights(
			Data,
			*this,
			ExplicitFirstRootHeight,
			ExplicitFirstExplicitHeight,
			ExplicitFirstAutoHeight))
		{
			return false;
		}
	}

	float AutoFirstRootHeight = 0.0f;
	float AutoFirstExplicitHeight = 0.0f;
	float AutoFirstAutoHeight = 0.0f;
	{
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(BuildSharedCalibrationFitContentDocument(false, false));
		if (!ExtractSharedCalibrationHeights(
			Data,
			*this,
			AutoFirstRootHeight,
			AutoFirstExplicitHeight,
			AutoFirstAutoHeight))
		{
			return false;
		}
	}

	TestTrue(TEXT("Fit-content parent height should be order independent"),
		FMath::IsNearlyEqual(ExplicitFirstRootHeight, AutoFirstRootHeight, 0.5f));
	TestTrue(TEXT("Fit-content parent height should collapse to child sum"),
		FMath::IsNearlyEqual(ExplicitFirstRootHeight, ExplicitFirstExplicitHeight + ExplicitFirstAutoHeight, 0.5f));
	TestTrue(TEXT("Auto-first fit-content parent height should collapse to child sum"),
		FMath::IsNearlyEqual(AutoFirstRootHeight, AutoFirstExplicitHeight + AutoFirstAutoHeight, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedCalibrationRefFitContentOrderIndependenceTest,
	"Design2UMG.Text.SharedCalibrationRefFitContentOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedCalibrationRefFitContentOrderIndependenceTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping shared calibration ref fit-content test."));
		return true;
	}

	float ExplicitFirstRootHeight = 0.0f;
	float ExplicitFirstExplicitHeight = 0.0f;
	float ExplicitFirstAutoHeight = 0.0f;
	{
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(BuildSharedCalibrationFitContentDocument(true, true));
		if (!ExtractSharedCalibrationHeights(
			Data,
			*this,
			ExplicitFirstRootHeight,
			ExplicitFirstExplicitHeight,
			ExplicitFirstAutoHeight))
		{
			return false;
		}
	}

	float AutoFirstRootHeight = 0.0f;
	float AutoFirstExplicitHeight = 0.0f;
	float AutoFirstAutoHeight = 0.0f;
	{
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(BuildSharedCalibrationFitContentDocument(false, true));
		if (!ExtractSharedCalibrationHeights(
			Data,
			*this,
			AutoFirstRootHeight,
			AutoFirstExplicitHeight,
			AutoFirstAutoHeight))
		{
			return false;
		}
	}

	TestTrue(TEXT("Ref fit-content parent height should be order independent"),
		FMath::IsNearlyEqual(ExplicitFirstRootHeight, AutoFirstRootHeight, 0.5f));
	TestTrue(TEXT("Ref fit-content parent height should collapse to child sum"),
		FMath::IsNearlyEqual(ExplicitFirstRootHeight, ExplicitFirstExplicitHeight + ExplicitFirstAutoHeight, 0.5f));
	TestTrue(TEXT("Auto-first ref fit-content parent height should collapse to child sum"),
		FMath::IsNearlyEqual(AutoFirstRootHeight, AutoFirstExplicitHeight + AutoFirstAutoHeight, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrimaryValueAllowsNearbyOvershootRegressionTest,
	"Design2UMG.Text.PrimaryValueAllowsNearbyOvershootRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPrimaryValueAllowsNearbyOvershootRegressionTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping primary-value overshoot regression test."));
		return true;
	}

	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Type = EDesignNodeType::Text;
	Node->Text.Content = TEXT("COMBO x25");
	Node->Text.FontFamily = TEXT("Inter");
	Node->Text.FontWeight = TEXT("bold");
	Node->Text.FontSize = 26.0f;
	Node->Text.TextAlign = EDesignTextAlign::Left;
	Node->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
	Node->Text.TextGrowth = EDesignTextGrowth::Auto;
	Node->Transform.Width = FDesignSize::Fixed(161.0f);
	Node->Transform.Height = FDesignSize::Fixed(33.4f);
	Node->Transform.bHasWidth = true;
	Node->Transform.bHasHeight = true;

	TextMeasurement::ResetCache();
	const float ResolvedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
	TestTrue(TEXT("Primary value label resolves a font size"), ResolvedFontSize > 0.0f);
	TestTrue(TEXT("Primary value label should preserve fallback-adjacent size"),
		ResolvedFontSize >= 20.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTextContentPreservesParagraphBreaksTest,
	"Design2UMG.Text.RuntimeContentPreservesParagraphBreaks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRuntimeTextContentPreservesParagraphBreaksTest::RunTest(const FString& Parameters)
{
	const FString SourceText = TEXT("Line one\r\n\r\nLine two");
	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(SourceText);
	TestEqual(TEXT("CRLF normalized to LF"), RuntimeText, FString(TEXT("Line one\n\nLine two")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementResetCacheClearsOwnedCachesTest,
	"Design2UMG.TextMeasurement.ResetCacheClearsOwnedCaches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementResetCacheClearsOwnedCachesTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping cache owner reset test."));
		return true;
	}

	const TSharedPtr<FDesignNode> Node = MakeSharedCalibrationTextNode(
		TEXT("cache_owner_probe"),
		TEXT("Design2UMG cache owner reset regression proof text"));

	TextMeasurement::ResetCache();
	const TextMeasurement::Testing::FTextMeasurementCacheStats EmptyStats = TextMeasurement::Testing::GetCacheStats();
	TestEqual(TEXT("Resolved cache starts empty"), EmptyStats.ResolvedScaleEntries, 0);
	TestEqual(TEXT("Shared-style cache starts empty"), EmptyStats.SharedStyleScaleEntries, 0);
	TestEqual(TEXT("Baseline cache starts empty"), EmptyStats.BaselineFallbackEntries, 0);

	const float ResolvedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
	TestTrue(TEXT("Probe node resolves a font size"), ResolvedFontSize > 0.0f);

	const TextMeasurement::Testing::FTextMeasurementCacheStats PopulatedStats = TextMeasurement::Testing::GetCacheStats();
	TestTrue(TEXT("Resolved cache is populated"), PopulatedStats.ResolvedScaleEntries > 0);
	TestTrue(TEXT("Shared-style cache is populated"), PopulatedStats.SharedStyleScaleEntries > 0);

	const TSharedPtr<FDesignNode> BaselineNode = MakeBaselineFallbackProbeNode(
		TEXT("cache_owner_baseline_probe"),
		TEXT("Cache owner baseline proof text"));
	const float BaselineScale = TextMeasurement::Testing::ResolveBaselineFallbackScale(BaselineNode);
	TestTrue(TEXT("Baseline fallback probe resolves a scale"), BaselineScale > 0.0f);

	const TextMeasurement::Testing::FTextMeasurementCacheStats BaselinePopulatedStats = TextMeasurement::Testing::GetCacheStats();
	TestTrue(TEXT("Baseline fallback cache is populated"), BaselinePopulatedStats.BaselineFallbackEntries > 0);

	TextMeasurement::ResetCache();
	const TextMeasurement::Testing::FTextMeasurementCacheStats ClearedStats = TextMeasurement::Testing::GetCacheStats();
	TestEqual(TEXT("Resolved cache clears with ResetCache"), ClearedStats.ResolvedScaleEntries, 0);
	TestEqual(TEXT("Shared-style cache clears with ResetCache"), ClearedStats.SharedStyleScaleEntries, 0);
	TestEqual(TEXT("Baseline cache clears with ResetCache"), ClearedStats.BaselineFallbackEntries, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementBaselineFallbackCacheIgnoresEquivalentSourceTextTest,
	"Design2UMG.TextMeasurement.BaselineFallbackCacheIgnoresEquivalentSourceText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementBaselineFallbackCacheIgnoresEquivalentSourceTextTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping baseline fallback cache text-sharing test."));
		return true;
	}

	const TSharedPtr<FDesignNode> ShortNode = MakeBaselineFallbackProbeNode(TEXT("baseline_short"), TEXT("Status"));
	const TSharedPtr<FDesignNode> LongNode = MakeBaselineFallbackProbeNode(
		TEXT("baseline_long"),
		TEXT("Status label that uses different source text but the same baseline style"));

	TextMeasurement::ResetCache();
	const float ShortScale = TextMeasurement::Testing::ResolveBaselineFallbackScale(ShortNode);
	const TextMeasurement::Testing::FTextMeasurementCacheStats AfterShort = TextMeasurement::Testing::GetCacheStats();
	const float LongScale = TextMeasurement::Testing::ResolveBaselineFallbackScale(LongNode);
	const TextMeasurement::Testing::FTextMeasurementCacheStats AfterLong = TextMeasurement::Testing::GetCacheStats();

	TestTrue(TEXT("Short baseline scale resolves"), ShortScale > 0.0f);
	TestTrue(TEXT("Long baseline scale resolves"), LongScale > 0.0f);
	TestEqual(TEXT("First baseline probe creates one cache entry"), AfterShort.BaselineFallbackEntries, 1);
	TestEqual(TEXT("Equivalent source text should reuse the same baseline cache entry"), AfterLong.BaselineFallbackEntries, 1);
	TestTrue(TEXT("Equivalent source text should resolve the same baseline scale"),
		FMath::IsNearlyEqual(ShortScale, LongScale, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementBaselineFallbackCacheKeepsExplicitLineHeightGuardTest,
	"Design2UMG.TextMeasurement.BaselineFallbackCacheKeepsExplicitLineHeightGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementBaselineFallbackCacheKeepsExplicitLineHeightGuardTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping baseline fallback explicit-line-height guard test."));
		return true;
	}

	const TSharedPtr<FDesignNode> SingleLineNode = MakeBaselineFallbackProbeNode(TEXT("baseline_single"), TEXT("Status"));
	const TSharedPtr<FDesignNode> ParagraphNode = MakeBaselineFallbackProbeNode(TEXT("baseline_para"), TEXT("Status"), true);

	TextMeasurement::ResetCache();
	const float SingleLineScale = TextMeasurement::Testing::ResolveBaselineFallbackScale(SingleLineNode);
	const float ParagraphScale = TextMeasurement::Testing::ResolveBaselineFallbackScale(ParagraphNode);
	const TextMeasurement::Testing::FTextMeasurementCacheStats Stats = TextMeasurement::Testing::GetCacheStats();

	TestTrue(TEXT("Single-line baseline scale resolves"), SingleLineScale > 0.0f);
	TestTrue(TEXT("Paragraph baseline scale resolves"), ParagraphScale > 0.0f);
	TestEqual(TEXT("Explicit-line-height guard keeps separate baseline cache entries"), Stats.BaselineFallbackEntries, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementWidthFitOvershootPrecedenceLockedTest,
	"Design2UMG.TextMeasurement.WidthFitOvershootPrecedenceLocked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementWidthFitOvershootPrecedenceLockedTest::RunTest(const FString& Parameters)
{
	const TArray<float> CandidateWidths = {
		0.0f,
		4.0f,
		8.0f,
		12.0f,
		16.0f,
		20.0f,
		24.0f,
		28.0f,
		32.0f,
		38.2f,
		39.8f
	};

	const int32 BestFontSize = TextMeasurement::Testing::ResolveBestWidthFitFontSizeFromWidths(
		10.5f,
		10,
		39.1f,
		true,
		CandidateWidths);

	TestEqual(TEXT("Width-fit keeps the existing closest overshoot precedence when it is explicitly allowed"), BestFontSize, 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementFallbackConstantsLockedTest,
	"Design2UMG.TextMeasurement.FallbackConstantsLocked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementFallbackConstantsLockedTest::RunTest(const FString& Parameters)
{
	FString Source;
	if (!LoadTextMeasurementSource(*this, Source))
	{
		return false;
	}

	TestTrue(TEXT("Line-height multiplier comment keeps 18/14 provenance"),
		Source.Contains(TEXT("18 / 14 = 1.285714"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Fallback scale comment keeps 3/4 provenance"),
		Source.Contains(TEXT("3 / 4 of the"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Fallback width comment keeps 0.60em provenance"),
		Source.Contains(TEXT("0.60em per visible character"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Fallback height comment keeps 1.5em provenance"),
		Source.Contains(TEXT("1.5em tall"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Width heuristic comment keeps 0.55em provenance"),
		Source.Contains(TEXT("0.55em only treats obviously single-line text as safe"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Line-height multiplier literal stays locked"),
		Source.Contains(TEXT("constexpr float DefaultLineHeightMultiplier = 1.285714f;"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Fallback scale literal stays locked"),
		Source.Contains(TEXT("constexpr float DefaultFontFallbackScale = 0.75f;"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Fallback width literal stays locked"),
		Source.Contains(TEXT("constexpr float FallbackMeasureWidthScale = 0.60f;"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Fallback height literal stays locked"),
		Source.Contains(TEXT("constexpr float FallbackMeasureHeightScale = 1.5f;"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Width heuristic literal stays locked"),
		Source.Contains(TEXT("constexpr float WidthHeuristicScale = 0.55f;"), ESearchCase::CaseSensitive));

	TSharedPtr<FDesignNode> DefaultLineHeightNode = MakeBaselineFallbackProbeNode(TEXT("default_line_height"), TEXT("Lock"));
	DefaultLineHeightNode->Text.FontSize = 14.0f;
	DefaultLineHeightNode->Text.LineHeight = 0.0f;

	const float EstimatedSingleLineHeight = TextMeasurement::Testing::EstimateSingleLineHeight(DefaultLineHeightNode);
	TestTrue(TEXT("Default line-height multiplier stays locked at 14 -> 18"),
		FMath::IsNearlyEqual(EstimatedSingleLineHeight, 18.0f, 0.01f));

	const FVector2D FallbackSize = TextMeasurement::Testing::MeasureFallbackTextSize(TEXT("ABCD"), 10.0f);
	TestTrue(TEXT("Fallback width stays locked at 0.60em per character"),
		FMath::IsNearlyEqual(FallbackSize.X, 24.0f, 0.01f));
	TestTrue(TEXT("Fallback height stays locked at 1.5em"),
		FMath::IsNearlyEqual(FallbackSize.Y, 15.0f, 0.01f));

	const float HeuristicWidth = TextMeasurement::Testing::EstimateSingleLineHeuristicWidth(TEXT("ABCD"), 10.0f);
	TestTrue(TEXT("Width heuristic stays locked at 0.55em per character"),
		FMath::IsNearlyEqual(HeuristicWidth, 22.0f, 0.01f));
	TestTrue(TEXT("Width heuristic remains narrower than fallback measurement"),
		HeuristicWidth < FallbackSize.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementPrivateShimHeaderRemovedTest,
	"Design2UMG.TextMeasurement.PrivateShimHeaderRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementPrivateShimHeaderRemovedTest::RunTest(const FString& Parameters)
{
	const FString PublicHeaderPath = GetTextMeasurementPublicHeaderPath();
	const FString PrivateHeaderPath = GetTextMeasurementPrivateHeaderPath();

	TestFalse(TEXT("Public TextMeasurement.h path resolved"), PublicHeaderPath.IsEmpty());
	TestFalse(TEXT("Private TextMeasurement.h path resolved"), PrivateHeaderPath.IsEmpty());
	TestTrue(TEXT("Public TextMeasurement.h still exists"), IFileManager::Get().FileExists(*PublicHeaderPath));
	TestFalse(TEXT("Private TextMeasurement.h shim is removed"), IFileManager::Get().FileExists(*PrivateHeaderPath));

	FString Source;
	if (!LoadTextMeasurementSource(*this, Source))
	{
		return false;
	}

	TestTrue(TEXT("TextMeasurement.cpp now includes the unified TextMeasurement.h"),
		Source.Contains(TEXT("#include \"TextMeasurement.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("TextMeasurement.cpp no longer includes ../Public/TextMeasurement.h directly"),
		Source.Contains(TEXT("#include \"../Public/TextMeasurement.h\""), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompactSingleLineLabelNoFontFamilyDependencyTest,
	"Design2UMG.TextMeasurement.CompactSingleLineLabelNoFontFamilyDependency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCompactSingleLineLabelNoFontFamilyDependencyTest::RunTest(const FString& Parameters)
{
	FString Source;
	if (!LoadTextMeasurementSource(*this, Source))
	{
		return false;
	}

	FString FuncBody;
	if (!ExtractFunctionBody(
		*this,
		Source,
		TEXT("bool IsCompactSingleLineLabel("),
		TEXT("IsCompactSingleLineLabel"),
		FuncBody))
	{
		return false;
	}

	TestFalse(TEXT("IsCompactSingleLineLabel must not reference IsMonospaceLikeFontFamily"),
		FuncBody.Contains(TEXT("IsMonospaceLikeFontFamily"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompactSingleLineLabelFontFamilyNameDoesNotAffectSizeTest,
	"Design2UMG.TextMeasurement.CompactSingleLineLabelFontFamilyNameDoesNotAffectSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCompactSingleLineLabelFontFamilyNameDoesNotAffectSizeTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping compact label font family name test."));
		return true;
	}

	auto MakeCompactFigmaNode = [](const FString& InFontFamily) -> TSharedPtr<FDesignNode>
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Type = EDesignNodeType::Text;
		Node->Text.Content = TEXT("OK");
		Node->Text.FontFamily = InFontFamily;
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 14.0f;
		Node->Text.LineHeight = 1.2f;
		Node->Text.LetterSpacing = 0.0f;
		Node->Text.TextAlign = EDesignTextAlign::Left;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
		Node->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
		Node->Transform.Width = FDesignSize::Fixed(60.0f);
		Node->Transform.Height = FDesignSize::Fixed(20.0f);
		Node->Transform.bHasWidth = true;
		Node->Transform.bHasHeight = true;
		Node->Transform.bSourceHasWidth = true;
		Node->Metadata.Add(TEXT("source.provider"), TEXT("figma"));
		return Node;
	};

	const TSharedPtr<FDesignNode> MonoNode = MakeCompactFigmaNode(TEXT("FakeMonoXYZ_UnitTest"));
	const TSharedPtr<FDesignNode> SansNode = MakeCompactFigmaNode(TEXT("FakeSansXYZ_UnitTest"));

	TextMeasurement::ResetCache();
	const float MonoSize = TextMeasurement::ResolveTextFontSizeForUMG(MonoNode);
	const float SansSize = TextMeasurement::ResolveTextFontSizeForUMG(SansNode);

	TestTrue(TEXT("Mono-named font resolves a size"), MonoSize > 0.0f);
	TestTrue(TEXT("Sans-named font resolves a size"), SansSize > 0.0f);
	TestTrue(TEXT("Compact label size must not depend on font family name containing 'mono'"),
		FMath::IsNearlyEqual(MonoSize, SansSize, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIsCompactBoldTokenTextNoMonospaceDependencyStaticTest,
	"Design2UMG.TextMeasurement.IsCompactBoldTokenTextNoMonospaceDependencyStatic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIsCompactBoldTokenTextNoMonospaceDependencyStaticTest::RunTest(const FString& Parameters)
{
	FString Source;
	if (!LoadTextMeasurementSource(*this, Source))
	{
		return false;
	}

	FString FuncBody;
	if (!ExtractFunctionBody(*this, Source, TEXT("bool IsCompactBoldTokenText("), TEXT("IsCompactBoldTokenText"), FuncBody))
	{
		return false;
	}

	TestFalse(TEXT("IsCompactBoldTokenText must not reference IsMonospaceLikeFontFamily"),
		FuncBody.Contains(TEXT("IsMonospaceLikeFontFamily"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIsCompactBoldTokenTextFontFamilyNameDoesNotAffectSizeTest,
	"Design2UMG.TextMeasurement.IsCompactBoldTokenTextFontFamilyNameDoesNotAffectSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIsCompactBoldTokenTextFontFamilyNameDoesNotAffectSizeTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping compact bold token font family name test."));
		return true;
	}

	auto MakeNode = [](const FString& InFontFamily) -> TSharedPtr<FDesignNode>
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Type = EDesignNodeType::Text;
		Node->Text.Content = TEXT("LVL");
		Node->Text.FontFamily = InFontFamily;
		Node->Text.FontWeight = TEXT("Bold");
		Node->Text.FontSize = 22.0f;
		Node->Text.LineHeight = 0.0f;
		Node->Text.LetterSpacing = 0.0f;
		Node->Text.TextAlign = EDesignTextAlign::Left;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
		Node->Text.TextGrowth = EDesignTextGrowth::Auto;
		Node->Transform.Width = FDesignSize::Fixed(40.0f);
		Node->Transform.Height = FDesignSize::Fixed(28.0f);
		Node->Transform.bHasWidth = true;
		Node->Transform.bHasHeight = true;
		Node->Transform.bSourceHasWidth = false;
		return Node;
	};

	const TSharedPtr<FDesignNode> MonoNode = MakeNode(TEXT("FakeCodeFont_UnitTest"));
	const TSharedPtr<FDesignNode> SansNode = MakeNode(TEXT("FakeSansFont_UnitTest"));

	TextMeasurement::ResetCache();
	const float MonoSize = TextMeasurement::ResolveTextFontSizeForUMG(MonoNode);
	TextMeasurement::ResetCache();
	const float SansSize = TextMeasurement::ResolveTextFontSizeForUMG(SansNode);

	TestTrue(TEXT("Mono-named bold token resolves a size"), MonoSize > 0.0f);
	TestTrue(TEXT("Sans-named bold token resolves a size"), SansSize > 0.0f);
	TestTrue(TEXT("Bold compact token size must not depend on font family name containing 'mono'/'code'"),
		FMath::IsNearlyEqual(MonoSize, SansSize, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShouldPreferIntrinsicSingleLineNoFontFamilyDependencyTest,
	"Design2UMG.TextMeasurement.ShouldPreferIntrinsicSingleLineNoFontFamilyDependency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FShouldPreferIntrinsicSingleLineNoFontFamilyDependencyTest::RunTest(const FString& Parameters)
{
	FString Source;
	if (!LoadTextMeasurementSource(*this, Source))
	{
		return false;
	}

	FString FuncBody;
	if (!ExtractFunctionBody(
		*this,
		Source,
		TEXT("bool ShouldPreferIntrinsicSingleLineFallbackFontSize("),
		TEXT("ShouldPreferIntrinsicSingleLineFallbackFontSize"),
		FuncBody))
	{
		return false;
	}

	TestFalse(TEXT("ShouldPreferIntrinsicSingleLineFallbackFontSize must not reference IsMonospaceLikeFontFamily"),
		FuncBody.Contains(TEXT("IsMonospaceLikeFontFamily"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShouldPreferIntrinsicSingleLineFontFamilyNameDoesNotAffectSizeTest,
	"Design2UMG.TextMeasurement.ShouldPreferIntrinsicSingleLineFontFamilyNameDoesNotAffectSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FShouldPreferIntrinsicSingleLineFontFamilyNameDoesNotAffectSizeTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping intrinsic single-line font family name test."));
		return true;
	}

	auto MakeNode = [](const FString& InFontFamily) -> TSharedPtr<FDesignNode>
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Type = EDesignNodeType::Text;
		Node->Text.Content = TEXT("Hello World Example");
		Node->Text.FontFamily = InFontFamily;
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 16.0f;
		Node->Text.LineHeight = 0.0f;
		Node->Text.LetterSpacing = 0.0f;
		Node->Text.TextAlign = EDesignTextAlign::Left;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
		Node->Text.TextGrowth = EDesignTextGrowth::Auto;
		Node->Transform.bSourceHasWidth = false;
		Node->MutableSource().bExpandedRefInstance = true;
		return Node;
	};

	const TSharedPtr<FDesignNode> MonoNode = MakeNode(TEXT("FakeMonoFont_UnitTest"));
	const TSharedPtr<FDesignNode> SansNode = MakeNode(TEXT("FakeSansFont_UnitTest"));

	TextMeasurement::ResetCache();
	const float MonoSize = TextMeasurement::ResolveTextFontSizeForUMG(MonoNode);
	TextMeasurement::ResetCache();
	const float SansSize = TextMeasurement::ResolveTextFontSizeForUMG(SansNode);

	TestTrue(TEXT("Mono-named node resolves a size"), MonoSize > 0.0f);
	TestTrue(TEXT("Sans-named node resolves a size"), SansSize > 0.0f);
	TestTrue(TEXT("ResolveTextFontSizeForUMG must not depend on font family name containing 'mono'"),
		FMath::IsNearlyEqual(MonoSize, SansSize, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementNoSubstringFontDispatchTest,
	"Design2UMG.TextMeasurement.NoSubstringFontDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementNoSubstringFontDispatchTest::RunTest(const FString& Parameters)
{
	FString Source;
	if (!LoadTextMeasurementSource(*this, Source))
	{
		return false;
	}

	TestFalse(TEXT("TextMeasurement.cpp must not contain Contains(TEXT(\"mono\")) font-name dispatch"),
		Source.Contains(TEXT("Contains(TEXT(\"mono\"))"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("TextMeasurement.cpp must not contain Contains(TEXT(\"code\")) font-name dispatch"),
		Source.Contains(TEXT("Contains(TEXT(\"code\"))"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementFamilySpecificFallbackTableLocalRollbackTest,
	"Design2UMG.TextMeasurement.FamilySpecificFallbackTableLocalRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementFamilySpecificFallbackTableLocalRollbackTest::RunTest(const FString& Parameters)
{
	FString Source;
	if (!LoadTextMeasurementSource(*this, Source))
	{
		return false;
	}

	TestTrue(TEXT("Local rollback documents why the family-specific scale table remains"),
		Source.Contains(TEXT("Phase-8 local score rollback"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Local rollback keeps the family-specific scale table until a shared rule proves parity"),
		Source.Contains(TEXT("FamilyFallbackScales"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Local rollback keeps the historical family-specific resolver"),
		Source.Contains(TEXT("FindFamilySpecificTextFallbackScale"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrace541FigmaExport1Local,
	"Design2UMG.TextMeasurement.Trace541.FigmaExport1Local",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTrace541FigmaExport1Local::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate not initialized, skipping trace."));
		return true;
	}

	const FString SourcePath = TEXT("G:\\UE5\\figma_export1\\design.json");
	TSharedPtr<FDesignNode> Node;
	bool bFromActualSource = false;

	FString JsonString;
	if (FFileHelper::LoadFileToString(JsonString, *SourcePath))
	{
		FDesignDocument Document;
		if (FFigmaParser::ParseDocument(JsonString, Document))
		{
			Node = FindFirstTextNodeByFamily(Document.Children, TEXT("Roboto"));
			if (Node.IsValid())
			{
				bFromActualSource = true;
				UE_LOG(LogDesign2UMG, Log,
					TEXT("5.4.1 [figma-export1-local] actual-source node: name='%s' family='%s' size=%.1f"),
					*Node->Name, *Node->Text.FontFamily, Node->Text.FontSize);
			}
			else
			{
				AddWarning(TEXT("5.4.1 [figma-export1-local] parse succeeded but no Roboto text node found; falling back to synthetic probe."));
			}
		}
		else
		{
			AddWarning(TEXT("5.4.1 [figma-export1-local] FFigmaParser::ParseDocument failed; falling back to synthetic probe."));
		}
	}
	else
	{
		AddWarning(TEXT("5.4.1 [figma-export1-local] source file not found at G:\\UE5\\figma_export1\\design.json; falling back to synthetic probe."));
	}

	if (!Node.IsValid())
	{
		Node = MakeShared<FDesignNode>();
		Node->Name = TEXT("figma-export1-local-probe");
		Node->Type = EDesignNodeType::Text;
		Node->Text.Content = TEXT("Export Label");
		Node->Text.FontFamily = TEXT("Roboto");
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 14.0f;
		Node->Text.TextAlign = EDesignTextAlign::Left;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
		Node->Text.TextGrowth = EDesignTextGrowth::Auto;
		Node->Transform.bSourceHasWidth = true;
		Node->Transform.SetWidthValue(FDesignSize::Fixed(120.0f));
		Node->Transform.SetHeightValue(FDesignSize::Fixed(20.0f));
		Node->Metadata.Add(TEXT("source.provider"), TEXT("figma"));
	}

	TextMeasurement::ResetCache();
	const FTextResolutionTrace Trace = TextMeasurementTestTrace::DumpTextResolutionTrace(Node);
	VerifyTraceCoreFields(*this, Trace);

	UE_LOG(LogDesign2UMG, Log,
		TEXT("5.4.1 Trace [figma-export1-local] source=%s node='%s' path=%s fallbackScale=%.3f fallback=%.1f compiled=%.1f bounds=(%.1f,%.1f) key=%s"),
		bFromActualSource ? TEXT("actual") : TEXT("synthetic"),
		*Trace.NodeName, *Trace.ResolutionPath, Trace.FallbackScale, Trace.FallbackFontSize,
		Trace.CompiledFontSize, Trace.CompiledBounds.X, Trace.CompiledBounds.Y, *Trace.CalibrationKey);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrace541PenHabitTracker,
	"Design2UMG.TextMeasurement.Trace541.PenHabitTrackerIndustrialTechnical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTrace541PenHabitTracker::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate not initialized, skipping trace."));
		return true;
	}

	const FString SourcePath = TEXT("G:\\UE5\\FPSRoguelike\\Designs\\pencil-demo.pen");
	TSharedPtr<FDesignNode> Node;
	bool bFromActualSource = false;

	FDesignDocument Document;
	if (FPenParser::ParseFromFile(SourcePath, Document))
	{
		Node = FindFirstTextNodeByFamily(Document.Children, TEXT("Inter"));
		if (Node.IsValid())
		{
			bFromActualSource = true;
			UE_LOG(LogDesign2UMG, Log,
				TEXT("5.4.1 [habit-tracker] actual-source node: name='%s' family='%s' size=%.1f"),
				*Node->Name, *Node->Text.FontFamily, Node->Text.FontSize);
		}
		else
		{
			AddWarning(TEXT("5.4.1 [habit-tracker] parse succeeded but no Inter text node found; falling back to synthetic probe."));
		}
	}
	else
	{
		AddWarning(TEXT("5.4.1 [habit-tracker] FPenParser::ParseFromFile failed or file not found; falling back to synthetic probe."));
	}

	if (!Node.IsValid())
	{
		Node = MakeShared<FDesignNode>();
		Node->Name = TEXT("habit-tracker-label-probe");
		Node->Type = EDesignNodeType::Text;
		Node->Text.Content = TEXT("Habit Tracker");
		Node->Text.FontFamily = TEXT("Inter");
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 16.0f;
		Node->Text.TextAlign = EDesignTextAlign::Left;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Middle;
		Node->Text.TextGrowth = EDesignTextGrowth::FixedWidthHeight;
		Node->Transform.bSourceHasWidth = false;
		Node->Transform.SetWidthValue(FDesignSize::Fixed(200.0f));
		Node->Transform.SetHeightValue(FDesignSize::Fixed(24.0f));
	}

	TextMeasurement::ResetCache();
	const FTextResolutionTrace Trace = TextMeasurementTestTrace::DumpTextResolutionTrace(Node);
	VerifyTraceCoreFields(*this, Trace);

	UE_LOG(LogDesign2UMG, Log,
		TEXT("5.4.1 Trace [habit-tracker-industrial] source=%s node='%s' path=%s fallbackScale=%.3f fallback=%.1f compiled=%.1f bounds=(%.1f,%.1f) key=%s"),
		bFromActualSource ? TEXT("actual") : TEXT("synthetic"),
		*Trace.NodeName, *Trace.ResolutionPath, Trace.FallbackScale, Trace.FallbackFontSize,
		Trace.CompiledFontSize, Trace.CompiledBounds.X, Trace.CompiledBounds.Y, *Trace.CalibrationKey);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrace541PenNitroDesignSystem,
	"Design2UMG.TextMeasurement.Trace541.PenNitroDesignSystemComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTrace541PenNitroDesignSystem::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate not initialized, skipping trace."));
		return true;
	}

	const FString SourcePath = TEXT("G:\\UE5\\FPSRoguelike\\Designs\\pencil-nitro.pen");
	TSharedPtr<FDesignNode> Node;
	bool bFromActualSource = false;

	FDesignDocument Document;
	if (FPenParser::ParseFromFile(SourcePath, Document))
	{
		Node = FindFirstTextNodeByFamily(Document.Children, TEXT("Roboto"));
		if (Node.IsValid())
		{
			bFromActualSource = true;
			UE_LOG(LogDesign2UMG, Log,
				TEXT("5.4.1 [nitro-design-system] actual-source node: name='%s' family='%s' size=%.1f"),
				*Node->Name, *Node->Text.FontFamily, Node->Text.FontSize);
		}
		else
		{
			AddWarning(TEXT("5.4.1 [nitro-design-system] parse succeeded but no Roboto text node found; falling back to synthetic probe."));
		}
	}
	else
	{
		AddWarning(TEXT("5.4.1 [nitro-design-system] FPenParser::ParseFromFile failed or file not found; falling back to synthetic probe."));
	}

	if (!Node.IsValid())
	{
		Node = MakeShared<FDesignNode>();
		Node->Name = TEXT("nitro-design-system-probe");
		Node->Type = EDesignNodeType::Text;
		Node->Text.Content = TEXT("Button Label");
		Node->Text.FontFamily = TEXT("Roboto");
		Node->Text.FontWeight = TEXT("Medium");
		Node->Text.FontSize = 14.0f;
		Node->Text.TextAlign = EDesignTextAlign::Center;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
		Node->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
		Node->Transform.bSourceHasWidth = false;
		Node->Transform.SetWidthValue(FDesignSize::Fixed(160.0f));
		Node->Transform.SetHeightValue(FDesignSize::Fixed(40.0f));
	}

	TextMeasurement::ResetCache();
	const FTextResolutionTrace Trace = TextMeasurementTestTrace::DumpTextResolutionTrace(Node);
	VerifyTraceCoreFields(*this, Trace);

	UE_LOG(LogDesign2UMG, Log,
		TEXT("5.4.1 Trace [nitro-design-system] source=%s node='%s' path=%s fallbackScale=%.3f fallback=%.1f compiled=%.1f bounds=(%.1f,%.1f) key=%s"),
		bFromActualSource ? TEXT("actual") : TEXT("synthetic"),
		*Trace.NodeName, *Trace.ResolutionPath, Trace.FallbackScale, Trace.FallbackFontSize,
		Trace.CompiledFontSize, Trace.CompiledBounds.X, Trace.CompiledBounds.Y, *Trace.CalibrationKey);
	return true;
}

#endif // WITH_AUTOMATION_WORKER
