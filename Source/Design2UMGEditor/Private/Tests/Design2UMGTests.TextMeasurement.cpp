// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGLog.h"
#include "Design2UMGTestUtils.h"
#include "DesignSchemaSourceExtensions.h"
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
	FString GetTextMeasurementPrivateSourcePath(const TCHAR* FileName)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source/Design2UMGCore/Private"), FileName)
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

	bool LoadTextMeasurementSplitSources(FAutomationTestBase& Test, FString& OutSource)
	{
		OutSource.Reset();

		const TCHAR* SourceFiles[] = {
			TEXT("TextMeasurement.cpp"),
			TEXT("TextMeasurementCalibration.cpp"),
			TEXT("TextMeasurementRuntimeProbe.cpp"),
			TEXT("TextMeasurementRuntimeProbeSizing.cpp"),
			TEXT("TextMeasurementSingleLineHeuristics.cpp"),
			TEXT("TextMeasurementCache.cpp"),
			TEXT("TextMeasurementInternal.h"),
		};

		for (const TCHAR* SourceFile : SourceFiles)
		{
			const FString SourcePath = GetTextMeasurementPrivateSourcePath(SourceFile);
			Test.TestFalse(*FString::Printf(TEXT("%s path resolved"), SourceFile), SourcePath.IsEmpty());
			if (SourcePath.IsEmpty())
			{
				return false;
			}

			FString FileSource;
			Test.TestTrue(*FString::Printf(TEXT("Load %s"), SourceFile), FFileHelper::LoadFileToString(FileSource, *SourcePath));
			if (FileSource.IsEmpty())
			{
				return false;
			}

			OutSource += FString::Printf(TEXT("\n// BEGIN %s\n"), SourceFile);
			OutSource += FileSource;
			OutSource += FString::Printf(TEXT("\n// END %s\n"), SourceFile);
		}

		return !OutSource.IsEmpty();
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

	FDesignDocument BuildNonTextDocument(const FString& Id)
	{
		FDesignDocument Document;
		TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
		Root->Id = Id;
		Root->Name = Id;
		Root->Type = EDesignNodeType::Frame;
		Root->Transform.Width = FDesignSize::Fixed(120.0f);
		Root->Transform.Height = FDesignSize::Fixed(80.0f);
		Root->Transform.bHasWidth = true;
		Root->Transform.bHasHeight = true;
		Document.Children.Add(Root);
		return Document;
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
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(
			BuildSharedCalibrationFitContentDocument(true, false),
			FDesign2UMGConversionSettings::Defaults());
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
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(
			BuildSharedCalibrationFitContentDocument(false, false),
			FDesign2UMGConversionSettings::Defaults());
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
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(
			BuildSharedCalibrationFitContentDocument(true, true),
			FDesign2UMGConversionSettings::Defaults());
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
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(
			BuildSharedCalibrationFitContentDocument(false, true),
			FDesign2UMGConversionSettings::Defaults());
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

	TextMeasurement::ResetCacheForConversionBoundary();
	const TextMeasurement::Testing::FTextMeasurementCacheStats ClearedStats = TextMeasurement::Testing::GetCacheStats();
	TestEqual(TEXT("Resolved cache clears at conversion boundary"), ClearedStats.ResolvedScaleEntries, 0);
	TestEqual(TEXT("Shared-style cache clears at conversion boundary"), ClearedStats.SharedStyleScaleEntries, 0);
	TestEqual(TEXT("Baseline cache clears at conversion boundary"), ClearedStats.BaselineFallbackEntries, 0);

	const float ResolvedFontSizeAfterConversionReset = TextMeasurement::ResolveTextFontSizeForUMG(Node);
	TestTrue(TEXT("Probe node resolves again after conversion boundary reset"), ResolvedFontSizeAfterConversionReset > 0.0f);
	const float BaselineScaleAfterConversionReset = TextMeasurement::Testing::ResolveBaselineFallbackScale(BaselineNode);
	TestTrue(TEXT("Baseline fallback resolves again after conversion boundary reset"), BaselineScaleAfterConversionReset > 0.0f);

	TextMeasurement::ResetCacheForAutomationBoundary();
	const TextMeasurement::Testing::FTextMeasurementCacheStats AutomationClearedStats = TextMeasurement::Testing::GetCacheStats();
	TestEqual(TEXT("Resolved cache clears at automation boundary"), AutomationClearedStats.ResolvedScaleEntries, 0);
	TestEqual(TEXT("Shared-style cache clears at automation boundary"), AutomationClearedStats.SharedStyleScaleEntries, 0);
	TestEqual(TEXT("Baseline cache clears at automation boundary"), AutomationClearedStats.BaselineFallbackEntries, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementConverterResetsCachePerDocumentTest,
	"Design2UMG.TextMeasurement.ConverterResetsCachePerDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementConverterResetsCachePerDocumentTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping converter cache reset test."));
		return true;
	}

	const TSharedPtr<FDesignNode> PoisonNode = MakeSharedCalibrationTextNode(
		TEXT("cache_poison"),
		TEXT("A previous document should not leave text measurement state behind."));

	TextMeasurement::ResetCache();
	const float PoisonFontSize = TextMeasurement::ResolveTextFontSizeForUMG(PoisonNode);
	TestTrue(TEXT("Poison text node resolves a font size"), PoisonFontSize > 0.0f);

	const TextMeasurement::Testing::FTextMeasurementCacheStats PoisonedStats = TextMeasurement::Testing::GetCacheStats();
	TestTrue(TEXT("Direct text measurement populates transient cache"),
		PoisonedStats.ResolvedScaleEntries > 0 || PoisonedStats.SharedStyleScaleEntries > 0);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(
		BuildNonTextDocument(TEXT("cache_reset_root")),
		FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Non-text conversion still succeeds"), Data.RootWidget.Id, FString(TEXT("cache_reset_root")));

	const TextMeasurement::Testing::FTextMeasurementCacheStats AfterConvertStats = TextMeasurement::Testing::GetCacheStats();
	TestEqual(TEXT("Converter clears stale resolved-scale entries before a new document"), AfterConvertStats.ResolvedScaleEntries, 0);
	TestEqual(TEXT("Converter clears stale shared-style entries before a new document"), AfterConvertStats.SharedStyleScaleEntries, 0);
	TestEqual(TEXT("Converter clears stale baseline entries before a new document"), AfterConvertStats.BaselineFallbackEntries, 0);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementSlateMetricFingerprintTokenTest,
	"Design2UMG.TextMeasurement.SlateMetricFingerprintToken",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementSlateMetricFingerprintTokenTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping Slate metric fingerprint test."));
		return true;
	}

	const TSharedPtr<FDesignNode> Node = MakeBaselineFallbackProbeNode(TEXT("metric_probe"), TEXT("Metric"));
	const TextMeasurement::Testing::FSlateMetricFingerprintTrace Trace = TextMeasurement::Testing::MeasureSlateMetricFingerprint(Node);
	TestTrue(TEXT("Slate metric fingerprint is available for resolved test font"), Trace.bIsAvailable);
	TestTrue(TEXT("Slate metric preserves signed non-zero baseline ratio"), !FMath::IsNearlyZero(Trace.SignedBaselineRatio));
	TestTrue(TEXT("Slate metric max height ratio is positive"), Trace.MaxHeightRatio > 0.0f);
	TestTrue(TEXT("Slate metric token is serialized"), Trace.Token.StartsWith(TEXT("metric:")) && Trace.Token != TEXT("metric:unavailable"));
	TestFalse(TEXT("Slate metric token does not serialize negative zero"), Trace.Token.Contains(TEXT("-0.0000")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementPhase32BaselineKeyIncludesMetricIdentityTest,
	"Design2UMG.TextMeasurement.Phase32BaselineKeyIncludesMetricIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementPhase32BaselineKeyIncludesMetricIdentityTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping Phase 32 baseline key test."));
		return true;
	}

	const TSharedPtr<FDesignNode> RegularNode = MakeBaselineFallbackProbeNode(TEXT("phase32_regular_key"), TEXT("Metric"));
	const TSharedPtr<FDesignNode> BoldNode = MakeBaselineFallbackProbeNode(TEXT("phase32_bold_key"), TEXT("Metric"));
	BoldNode->Text.FontWeight = TEXT("Bold");
	const FString RegularKey = TextMeasurement::Testing::BuildBaselineFallbackScaleKeyForTest(RegularNode);
	const FString BoldKey = TextMeasurement::Testing::BuildBaselineFallbackScaleKeyForTest(BoldNode);

	TestFalse(TEXT("Regular baseline key resolves"), RegularKey.IsEmpty());
	TestTrue(TEXT("Baseline key includes metric token"), RegularKey.Contains(TEXT("metric:")));
	TestTrue(TEXT("Baseline key includes font identity state"), RegularKey.Contains(TEXT("state=")));
	TestTrue(TEXT("Baseline key separates font identity fields"), RegularKey != BoldKey);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementPhase32MeasuredFallbackPrecedesSemanticTest,
	"Design2UMG.TextMeasurement.Phase32MeasuredFallbackPrecedesSemantic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementPhase32MeasuredFallbackPrecedesSemanticTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Slate is not initialized, skipping Phase 32 measured fallback test."));
		return true;
	}

	const TSharedPtr<FDesignNode> Node = MakeBaselineFallbackProbeNode(
		TEXT("phase32_measured_first"),
		TEXT("Measured fallback fits copy now"));
	Node->Text.FontSize = 24.0f;
	Node->Text.LineHeight = 1.1f;
	Node->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	Node->Transform.Width = FDesignSize::Fixed(400.0f);
	Node->Transform.Height = FDesignSize::Fixed(64.0f);
	Node->Transform.bHasWidth = true;
	Node->Transform.bHasHeight = true;
	Node->Transform.bSourceHasWidth = true;

	const float FallbackFontSize = TextMeasurement::Testing::ResolveTextFontSizeFallbackForTest(Node);
	const TextMeasurement::Testing::FTextFontSizeFallbackTrace Trace = TextMeasurement::Testing::ResolveTextFontSizeFallbackTraceForTest(Node);
	const float DefaultFallbackFontSize = Node->Text.FontSize * 0.75f;
	TestEqual(TEXT("Measured fallback branch runs before semantic/default fallback"),
		Trace.Branch,
		TextMeasurement::Testing::ETextFontSizeFallbackBranch::Measured);
	TestEqual(TEXT("Resolved fallback uses measured scale"), FallbackFontSize, Node->Text.FontSize * Trace.Scale);
	TestTrue(TEXT("Measured fallback stays within historical fallback ceiling"), FallbackFontSize <= DefaultFallbackFontSize + 0.01f);
	TestTrue(TEXT("Measured fallback does not collapse below the Phase 32 floor"), FallbackFontSize >= Node->Text.FontSize * 0.5f - 0.01f);
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
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementResetContractCommentsTest,
	"Design2UMG.TextMeasurement.ResetContractComments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementResetContractCommentsTest::RunTest(const FString& Parameters)
{
	const FString PublicHeaderPath = GetTextMeasurementPublicHeaderPath();
	TestFalse(TEXT("Public TextMeasurement.h path resolved"), PublicHeaderPath.IsEmpty());
	if (PublicHeaderPath.IsEmpty())
	{
		return false;
	}

	FString HeaderSource;
	TestTrue(TEXT("Load public TextMeasurement.h"), FFileHelper::LoadFileToString(HeaderSource, *PublicHeaderPath));
	if (HeaderSource.IsEmpty())
	{
		return false;
	}

	TestTrue(TEXT("ResetCache comment marks the API as a low-level primitive"), HeaderSource.Contains(TEXT("Low-level primitive")));
	TestTrue(TEXT("ResetCache comment points contributors to conversion boundary reset"), HeaderSource.Contains(TEXT("ResetCacheForConversionBoundary()")));
	TestTrue(TEXT("ResetCache comment points contributors to automation boundary reset"), HeaderSource.Contains(TEXT("ResetCacheForAutomationBoundary()")));
	TestTrue(TEXT("Conversion boundary comment stays contributor-facing"), HeaderSource.Contains(TEXT("Preferred shared-path reset for starting an explicit document conversion boundary.")));
	TestTrue(TEXT("Automation boundary comment stays contributor-facing"), HeaderSource.Contains(TEXT("Preferred shared-path reset for starting an explicit automation render boundary.")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementNoIdentityDispatchGuardTest,
	"Design2UMG.TextMeasurement.NoIdentityDispatchGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementNoIdentityDispatchGuardTest::RunTest(const FString& Parameters)
{
	FString Source;
	if (!LoadTextMeasurementSplitSources(*this, Source))
	{
		return false;
	}

	const TCHAR* DisallowedIdentityTokens[] = {
		TEXT("CaseName"),
		TEXT("NodeId"),
		TEXT("Node->Id"),
		TEXT("Node->Name.Contains"),
		TEXT("Node->Name.Equals"),
		TEXT("Node->Name.Find"),
		TEXT("Node->Name.StartsWith"),
		TEXT("Node->Name.EndsWith"),
		TEXT("Node->Text.FontFamily.Contains"),
		TEXT("Node->Text.FontFamily.Equals"),
		TEXT("Node->Text.FontFamily.Find"),
		TEXT("Node->Text.FontFamily.StartsWith"),
		TEXT("Node->Text.FontFamily.EndsWith"),
		TEXT("NormalizedFamily.Contains"),
		TEXT("NormalizedFamily.Find"),
		TEXT("NormalizedFamily.StartsWith"),
		TEXT("NormalizedFamily.EndsWith"),
		TEXT("FontFamily.Contains"),
		TEXT("FontFamily.Find"),
		TEXT("FontFamily.StartsWith"),
		TEXT("FontFamily.EndsWith"),
		TEXT("FileName"),
		TEXT("SourcePath"),
		TEXT("Directory"),
		TEXT("ExportRoot"),
		TEXT("ComponentName"),
		TEXT("RuntimeImported"),
		TEXT("/Design2UMG/Automation/ImportedFonts/"),
		TEXT("FontFallbackScaleOverrides"),
	};

	for (const TCHAR* Token : DisallowedIdentityTokens)
	{
		TestFalse(
			*FString::Printf(TEXT("Text measurement must not branch on identity token %s"), Token),
			Source.Contains(Token, ESearchCase::CaseSensitive));
	}
	return true;
}

#endif // WITH_AUTOMATION_WORKER
