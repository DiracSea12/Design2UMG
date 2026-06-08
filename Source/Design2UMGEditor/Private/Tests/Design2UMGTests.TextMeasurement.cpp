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

#include <limits>

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

	bool HasProjectInterFont(FAutomationTestBase& Test)
	{
		const FString ProjectInterPath = TEXT("/Game/Design2UMG/Fonts/Inter.Inter");
		if (!LoadObject<UFont>(nullptr, *ProjectInterPath))
		{
			Test.AddWarning(TEXT("Project Inter font asset is unavailable; skipping Phase 32A Slate metric test that requires real project font identity."));
			return false;
		}
		return true;
	}

	TSharedPtr<FDesignNode> MakePhase32MeasuredCandidateNode(const FString& Id, const FString& FontFamily)
	{
		TSharedPtr<FDesignNode> Node = MakeBaselineFallbackProbeNode(
			Id,
			TEXT("Phase 32A"));
		Node->Text.FontFamily = FontFamily;
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 32.0f;
		Node->Text.LineHeight = 1.2f;
		Node->Text.TextGrowth = EDesignTextGrowth::FixedWidthHeight;
		Node->Transform.Width = FDesignSize::Fixed(240.0f);
		Node->Transform.Height = FDesignSize::Fixed(64.0f);
		Node->Transform.bHasWidth = true;
		Node->Transform.bHasHeight = true;
		Node->Transform.bSourceHasWidth = true;
		Node->Transform.bSourceHasHeight = true;
		return Node;
	}

	TSharedPtr<FDesignNode> MakePhase32B1LayoutShapeNode(
		const FString& Id,
		const EDesignNodeType Type,
		const EDesignTextGrowth TextGrowth,
		const FString& Content,
		const float Width,
		const float Height,
		const float LineHeight = 0.6f)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Id;
		Node->Type = Type;
		Node->Text.Content = Content;
		Node->Text.FontFamily = TEXT("Inter");
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 32.0f;
		Node->Text.LineHeight = LineHeight;
		Node->Text.LetterSpacing = 0.0f;
		Node->Text.TextAlign = EDesignTextAlign::Left;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
		Node->Text.TextGrowth = TextGrowth;
		if (Width > 0.0f)
		{
			Node->Transform.SetWidthValue(FDesignSize::Fixed(Width));
		}
		else
		{
			Node->Transform.Width = FDesignSize::Fit(0.0f);
			Node->Transform.bHasWidth = false;
			Node->Transform.bSourceHasWidth = false;
		}
		if (Height > 0.0f)
		{
			Node->Transform.SetHeightValue(FDesignSize::Fixed(Height));
		}
		else
		{
			Node->Transform.Height = FDesignSize::Fit(0.0f);
			Node->Transform.bHasHeight = false;
			Node->Transform.bSourceHasHeight = false;
		}
		return Node;
	}

	float ResolvePhase32B1FallbackFontSize(
		FAutomationTestBase& Test,
		const TSharedPtr<FDesignNode>& Node)
	{
		TextMeasurement::ResetCache();
		const FTextResolutionTrace Trace = TextMeasurementTestTrace::DumpTextResolutionTrace(Node);
		Test.TestTrue(
			*FString::Printf(TEXT("%s fallback font size is observable"), Node.IsValid() ? *Node->Name : TEXT("<invalid>")),
			Trace.FallbackFontSize > 0.0f);
		return Trace.FallbackFontSize;
	}

	bool IsConservativePhase32B1Fallback(const float FallbackFontSize)
	{
		constexpr float SourceFontSize = 32.0f;
		constexpr float ConservativeScale = 0.75f;
		return FMath::IsNearlyEqual(FallbackFontSize, SourceFontSize * ConservativeScale, 0.01f);
	}

	int32 CountFamilyFallbackScaleEntries(const FString& Source)
	{
		const int32 TableStart = Source.Find(TEXT("FamilyFallbackScales[]"));
		if (TableStart == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		const int32 TableEnd = Source.Find(TEXT("};"), ESearchCase::CaseSensitive, ESearchDir::FromStart, TableStart);
		if (TableEnd == INDEX_NONE)
		{
			return INDEX_NONE;
		}

		int32 Count = 0;
		int32 Offset = TableStart;
		while (Offset < TableEnd)
		{
			const int32 EntryIndex = Source.Find(TEXT("{ TEXT(\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Offset);
			if (EntryIndex == INDEX_NONE || EntryIndex >= TableEnd)
			{
				break;
			}
			++Count;
			Offset = EntryIndex + 1;
		}
		return Count;
	}

	bool ExtractSourceBetween(
		FAutomationTestBase& Test,
		const FString& Source,
		const TCHAR* StartToken,
		const TCHAR* EndToken,
		FString& OutSection)
	{
		OutSection.Reset();
		const int32 Start = Source.Find(StartToken);
		Test.TestTrue(*FString::Printf(TEXT("Find source start token %s"), StartToken), Start != INDEX_NONE);
		if (Start == INDEX_NONE)
		{
			return false;
		}

		const int32 End = Source.Find(EndToken, ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + FCString::Strlen(StartToken));
		Test.TestTrue(*FString::Printf(TEXT("Find source end token %s"), EndToken), End != INDEX_NONE);
		if (End == INDEX_NONE || End <= Start)
		{
			return false;
		}

		OutSection = Source.Mid(Start, End - Start);
		return !OutSection.IsEmpty();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementSlateMetricTokenSerializationTest,
	"Design2UMG.TextMeasurement.Phase32A.SlateMetricTokenSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementSlateMetricTokenSerializationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Signed baseline ratio keeps negative sign"),
		TextMeasurement::Testing::BuildSlateMetricToken(-0.25f, 1.125f, true),
		FString(TEXT("metric:-0.2500:1.1250")));
	TestEqual(TEXT("Negative zero quantizes to positive zero"),
		TextMeasurement::Testing::BuildSlateMetricToken(-0.000004f, 1.0f, true),
		FString(TEXT("metric:0.0000:1.0000")));
	TestEqual(TEXT("Unavailable metric token is explicit"),
		TextMeasurement::Testing::BuildSlateMetricToken(0.5f, 1.0f, false),
		FString(TEXT("metric:unavailable")));
	TestEqual(TEXT("Non-finite metric cannot serialize as valid token"),
		TextMeasurement::Testing::BuildSlateMetricToken(std::numeric_limits<float>::infinity(), 1.0f, true),
		FString(TEXT("metric:unavailable")));
	TestEqual(TEXT("Finite metric that overflows during quantization is unavailable"),
		TextMeasurement::Testing::BuildSlateMetricToken(std::numeric_limits<float>::max(), 1.0f, true),
		FString(TEXT("metric:unavailable")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementSlateMetricProbeUsesFontScaleOneTest,
	"Design2UMG.TextMeasurement.Phase32A.SlateMetricProbeUsesFontScaleOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementSlateMetricProbeUsesFontScaleOneTest::RunTest(const FString& Parameters)
{
	FString Source;
	if (!LoadTextMeasurementSplitSources(*this, Source))
	{
		return false;
	}

	TestTrue(TEXT("Metric probe sets the probe size on FSlateFontInfo"),
		Source.Contains(TEXT("ProbeRequest.FontSize = MetricProbeFontSize;")));
	TestTrue(TEXT("Metric probe passes FontScale=1.0 through ProbeFontScale"),
		Source.Contains(TEXT("constexpr float ProbeFontScale = 1.0f;")));
	TestTrue(TEXT("Baseline probe uses FontScale=1.0"),
		Source.Contains(TEXT("GetBaseline(FontInfo, ProbeFontScale)")));
	TestTrue(TEXT("Max-height probe uses FontScale=1.0"),
		Source.Contains(TEXT("GetMaxCharacterHeight(FontInfo, ProbeFontScale)")));
	TestFalse(TEXT("Baseline probe must not pass 64 as FontScale"),
		Source.Contains(TEXT("GetBaseline(FontInfo, MetricProbeFontSize)")));
	TestFalse(TEXT("Max-height probe must not pass 64 as FontScale"),
		Source.Contains(TEXT("GetMaxCharacterHeight(FontInfo, MetricProbeFontSize)")));
	TestFalse(TEXT("Signed baseline ratio must not be abs-normalized"),
		Source.Contains(TEXT("SignedBaselineRatio = FMath::Abs")));

	if (!FSlateApplication::IsInitialized() || !HasProjectInterFont(*this))
	{
		AddWarning(TEXT("Slate or project Inter font unavailable; source-level probe contract was still checked."));
		return true;
	}

	constexpr int32 FixedBatchCount = 32;
	constexpr double LocalTimeBudgetSeconds = 1.0;
	const TSharedPtr<FDesignNode> Node = MakePhase32MeasuredCandidateNode(TEXT("phase32_metric_probe"), TEXT("Inter"));
	const TextMeasurement::Testing::FSlateMetricProbeResult Metric =
		TextMeasurement::Testing::MeasureSlateMetricProbe(Node, FixedBatchCount);
	TestTrue(TEXT("Fixed local font metric probe is available"), Metric.bAvailable);
	TestTrue(TEXT("Metric token is valid"), Metric.Token.StartsWith(TEXT("metric:")) && Metric.Token != TEXT("metric:unavailable"));
	TestFalse(TEXT("Signed baseline ratio is not zero"), FMath::IsNearlyZero(Metric.SignedBaselineRatio));
	TestTrue(TEXT("Max height ratio is positive"), Metric.MaxHeightRatio > 0.0f);
	TestTrue(TEXT("Fixed 32-probe batch stays inside local one-second budget"), Metric.ElapsedSeconds <= LocalTimeBudgetSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementBaselineFallbackKeyIncludesMetricAndIdentityTest,
	"Design2UMG.TextMeasurement.Phase32A.BaselineFallbackKeyIncludesMetricAndIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementBaselineFallbackKeyIncludesMetricAndIdentityTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized() || !HasProjectInterFont(*this))
	{
		return true;
	}

	const TSharedPtr<FDesignNode> ExactNode = MakePhase32MeasuredCandidateNode(TEXT("phase32_key_exact"), TEXT("Inter"));
	const FString ExactKey = TextMeasurement::Testing::BuildBaselineFallbackScaleKey(ExactNode);
	TestTrue(TEXT("Exact project font key resolves"), !ExactKey.IsEmpty());
	TestTrue(TEXT("Baseline key includes metric token"), ExactKey.Contains(TEXT("|metric:")));
	TestTrue(TEXT("Baseline key includes fallback state"), ExactKey.Contains(TEXT("fallbackState=")));
	TestTrue(TEXT("Baseline key includes resolution source"), ExactKey.Contains(TEXT("resSource=")));
	TestTrue(TEXT("Baseline key uses 1e-4 font-size token"), ExactKey.Contains(TEXT("fs=32.0000")));

	const TSharedPtr<FDesignNode> MissingIdentityNode = MakePhase32MeasuredCandidateNode(TEXT("phase32_key_missing"), FString());
	const FString MissingIdentityKey = TextMeasurement::Testing::BuildBaselineFallbackScaleKey(MissingIdentityNode);
	TestTrue(TEXT("Missing identity keeps conservative key available"), !MissingIdentityKey.IsEmpty());
	TestFalse(TEXT("Missing identity does not append Phase 32 metric token"), MissingIdentityKey.Contains(TEXT("|metric:")));

	TSharedPtr<FDesignNode> BadFloatNode = MakePhase32MeasuredCandidateNode(TEXT("phase32_key_bad_float"), TEXT("Inter"));
	BadFloatNode->Text.FontSize = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("Non-finite font size disables fallback key input"),
		TextMeasurement::Testing::BuildBaselineFallbackScaleKey(BadFloatNode).IsEmpty());

	TSharedPtr<FDesignNode> OverflowFloatNode = MakePhase32MeasuredCandidateNode(TEXT("phase32_key_overflow_float"), TEXT("Inter"));
	OverflowFloatNode->Text.FontSize = std::numeric_limits<float>::max();
	TestTrue(TEXT("Finite font size that overflows during quantization disables fallback key input"),
		TextMeasurement::Testing::BuildBaselineFallbackScaleKey(OverflowFloatNode).IsEmpty());

	TestNotEqual(TEXT("Metric/identity key is isolated from missing-identity key"), ExactKey, MissingIdentityKey);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementProtectedMeasuredCandidatePathTest,
	"Design2UMG.TextMeasurement.Phase32A.ProtectedMeasuredCandidatePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementProtectedMeasuredCandidatePathTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized() || !HasProjectInterFont(*this))
	{
		return true;
	}

	const TSharedPtr<FDesignNode> ExactNode = MakePhase32MeasuredCandidateNode(TEXT("phase32_candidate_exact"), TEXT("Inter"));
	TextMeasurement::ResetCache();
	const float ProductionBefore = TextMeasurement::ResolveTextFontSizeForUMG(ExactNode);
	const TextMeasurement::Testing::FProtectedMeasuredCandidateResult Candidate =
		TextMeasurement::Testing::ResolveProtectedMeasuredCandidate(ExactNode);
	const float ProductionAfter = TextMeasurement::ResolveTextFontSizeForUMG(ExactNode);

	TestTrue(TEXT("Protected candidate is available for exact project font identity"), Candidate.bCandidateAvailable);
	TestTrue(TEXT("Protected candidate uses at most six measurements"), Candidate.MeasurementAttempts > 0 && Candidate.MeasurementAttempts <= 6);
	TestTrue(TEXT("Protected candidate records a valid metric token"),
		Candidate.MetricToken.StartsWith(TEXT("metric:")) && Candidate.MetricToken != TEXT("metric:unavailable"));
	TestTrue(TEXT("Stable accepted production scale remains valid"), Candidate.StableAcceptedScale > 0.0f);
	TestTrue(TEXT("Candidate scale remains within conservative measured bounds"),
		Candidate.CandidateScale >= 0.5f && Candidate.CandidateScale <= 0.75f);
	TestTrue(TEXT("Candidate computation does not replace accepted production output"),
		FMath::IsNearlyEqual(ProductionBefore, ProductionAfter, 0.01f));

	const TSharedPtr<FDesignNode> MissingIdentityNode = MakePhase32MeasuredCandidateNode(TEXT("phase32_candidate_missing"), FString());
	const TextMeasurement::Testing::FProtectedMeasuredCandidateResult MissingIdentityCandidate =
		TextMeasurement::Testing::ResolveProtectedMeasuredCandidate(MissingIdentityNode);
	TestTrue(TEXT("Missing 31.5 local identity blocks Phase 32A candidate"),
		MissingIdentityCandidate.bBlockedByMissingFontIdentity);
	TestFalse(TEXT("Missing identity candidate is unavailable"),
		MissingIdentityCandidate.bCandidateAvailable);

	const TSharedPtr<FDesignNode> DisallowedStateNode = MakePhase32MeasuredCandidateNode(TEXT("phase32_candidate_engine"), TEXT("Roboto"));
	const TextMeasurement::Testing::FProtectedMeasuredCandidateResult DisallowedStateCandidate =
		TextMeasurement::Testing::ResolveProtectedMeasuredCandidate(DisallowedStateNode);
	TestTrue(TEXT("Disallowed fallback state blocks Phase 32A candidate"),
		DisallowedStateCandidate.bBlockedByFallbackState);
	TestFalse(TEXT("Disallowed fallback state candidate is unavailable"),
		DisallowedStateCandidate.bCandidateAvailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementPhase32AScopeGuardTest,
	"Design2UMG.TextMeasurement.Phase32A.ScopeGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementPhase32AScopeGuardTest::RunTest(const FString& Parameters)
{
	FString Source;
	if (!LoadTextMeasurementSplitSources(*this, Source))
	{
		return false;
	}

	TestEqual(TEXT("FamilyFallbackScales remains at the protected two-entry table"),
		CountFamilyFallbackScaleEntries(Source),
		2);
	TestTrue(TEXT("Existing Geist fallback remains protected"),
		Source.Contains(TEXT("{ TEXT(\"geist\"), 0.71f }")));
	TestTrue(TEXT("Existing Roboto Mono fallback remains protected"),
		Source.Contains(TEXT("{ TEXT(\"robotomono\"), 0.73f }")));
	TestFalse(TEXT("Production fallback must not compute and discard a protected candidate"),
		Source.Contains(TEXT("(void)ResolveProtectedMeasuredCandidate(Node, FallbackScale);")));
	TestFalse(TEXT("Protected candidate path does not call semantic single-line fallback"),
		Source.Contains(TEXT("ResolveProtectedMeasuredCandidate(Node, FallbackScale);\n\tif (const TOptional<float> SemanticScale")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementPhase32B1LayoutShapeBehaviorTest,
	"Design2UMG.TextMeasurement.Phase32B1.LayoutShapeBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementPhase32B1LayoutShapeBehaviorTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized() || !HasProjectInterFont(*this))
	{
		return true;
	}

	const FString LongContent = TEXT("Phase 32B layout measurement should use constraints instead of semantic content labels.");

	const TSharedPtr<FDesignNode> FixedSingleLineNode = MakePhase32B1LayoutShapeNode(
		TEXT("phase32b1_fixed_single"),
		EDesignNodeType::Text,
		EDesignTextGrowth::FixedWidth,
		LongContent,
		40.0f,
		-1.0f);
	const float FixedSingleLineFallback = ResolvePhase32B1FallbackFontSize(*this, FixedSingleLineNode);
	TestTrue(TEXT("Fixed-width single-line shape reaches measured fallback through ResolveTextFontSizeForUMG"),
		FixedSingleLineFallback > 0.0f && FixedSingleLineFallback < 24.0f);

	const TSharedPtr<FDesignNode> FixedMultilineNode = MakePhase32B1LayoutShapeNode(
		TEXT("phase32b1_fixed_multiline"),
		EDesignNodeType::Text,
		EDesignTextGrowth::FixedWidthHeight,
		LongContent,
		96.0f,
		28.0f);
	const float FixedMultilineFallback = ResolvePhase32B1FallbackFontSize(*this, FixedMultilineNode);
	TestTrue(TEXT("Fixed-width multiline body shape reaches measured fallback through ResolveTextFontSizeForUMG"),
		FixedMultilineFallback > 0.0f && FixedMultilineFallback < 24.0f);

	const TSharedPtr<FDesignNode> IntrinsicLabelNode = MakePhase32B1LayoutShapeNode(
		TEXT("phase32b1_intrinsic_label"),
		EDesignNodeType::Text,
		EDesignTextGrowth::Auto,
		LongContent,
		-1.0f,
		-1.0f);
	const float IntrinsicLabelFallback = ResolvePhase32B1FallbackFontSize(*this, IntrinsicLabelNode);
	TestTrue(TEXT("Intrinsic single-line label shape reaches measured fallback through ResolveTextFontSizeForUMG"),
		IntrinsicLabelFallback > 0.0f && IntrinsicLabelFallback < 24.0f);

	const TSharedPtr<FDesignNode> TruncateNode = MakePhase32B1LayoutShapeNode(
		TEXT("phase32b1_truncate_present"),
		EDesignNodeType::Text,
		EDesignTextGrowth::Auto,
		LongContent,
		48.0f,
		-1.0f);
	TruncateNode->Metadata.Add(TEXT("source.text.truncate"), TEXT("true"));
	const float TruncateFallback = ResolvePhase32B1FallbackFontSize(*this, TruncateNode);
	TestTrue(TEXT("Truncate metadata reaches truncate measured fallback behavior"),
		TruncateFallback > 0.0f && TruncateFallback < 24.0f);

	const TSharedPtr<FDesignNode> MissingTruncateNode = MakePhase32B1LayoutShapeNode(
		TEXT("phase32b1_truncate_absent"),
		EDesignNodeType::Text,
		EDesignTextGrowth::Auto,
		LongContent,
		48.0f,
		-1.0f);
	const float MissingTruncateFallback = ResolvePhase32B1FallbackFontSize(*this, MissingTruncateNode);
	TestTrue(TEXT("Missing truncate metadata fails closed to conservative fallback"),
		IsConservativePhase32B1Fallback(MissingTruncateFallback));

	const TSharedPtr<FDesignNode> IconFontNode = MakePhase32B1LayoutShapeNode(
		TEXT("phase32b1_icon_font"),
		EDesignNodeType::IconFont,
		EDesignTextGrowth::FixedWidthHeight,
		TEXT("home"),
		24.0f,
		24.0f);
	IconFontNode->Text.IconFontName = TEXT("home");
	IconFontNode->Text.IconFontFamily = TEXT("Inter");
	const float IconFontFallback = ResolvePhase32B1FallbackFontSize(*this, IconFontNode);
	TestTrue(TEXT("Icon-font node semantics already on this fallback path reach measured behavior"),
		IconFontFallback > 0.0f && IconFontFallback < 24.0f);

	const TSharedPtr<FDesignNode> UnknownShapeNode = MakePhase32B1LayoutShapeNode(
		TEXT("phase32b1_unknown_shape"),
		EDesignNodeType::Text,
		EDesignTextGrowth::Auto,
		LongContent,
		48.0f,
		-1.0f);
	const float UnknownShapeFallback = ResolvePhase32B1FallbackFontSize(*this, UnknownShapeNode);
	TestTrue(TEXT("Unknown unsupported layout shape fails closed to conservative fallback"),
		IsConservativePhase32B1Fallback(UnknownShapeFallback));

	const TSharedPtr<FDesignNode> ContentVariantA = MakePhase32B1LayoutShapeNode(
		TEXT("phase32b1_content_a"),
		EDesignNodeType::Text,
		EDesignTextGrowth::Auto,
		TEXT("Phase 32B intrinsic route uses layout constraints while the sentence text changes."),
		-1.0f,
		-1.0f);
	const TSharedPtr<FDesignNode> ContentVariantB = MakePhase32B1LayoutShapeNode(
		TEXT("phase32b1_content_b"),
		EDesignNodeType::Text,
		EDesignTextGrowth::Auto,
		TEXT("Different words still keep the same intrinsic layout shape before width measurement."),
		-1.0f,
		-1.0f);
	const float ContentFallbackA = ResolvePhase32B1FallbackFontSize(*this, ContentVariantA);
	const float ContentFallbackB = ResolvePhase32B1FallbackFontSize(*this, ContentVariantB);
	TestTrue(TEXT("Changing content does not change intrinsic shape output class before measurement"),
		ContentFallbackA > 0.0f
		&& ContentFallbackB > 0.0f
		&& FMath::IsNearlyEqual(ContentFallbackA, ContentFallbackB, 0.01f));

	const TSharedPtr<FDesignNode> AlignLeftNode = MakePhase32B1LayoutShapeNode(
		TEXT("phase32b1_align_left"),
		EDesignNodeType::Text,
		EDesignTextGrowth::FixedWidthHeight,
		LongContent,
		120.0f,
		44.0f);
	const TSharedPtr<FDesignNode> AlignRightNode = MakePhase32B1LayoutShapeNode(
		TEXT("phase32b1_align_right"),
		EDesignNodeType::Text,
		EDesignTextGrowth::FixedWidthHeight,
		LongContent,
		120.0f,
		44.0f);
	AlignRightNode->Text.TextAlign = EDesignTextAlign::Right;
	const float AlignLeftFallback = ResolvePhase32B1FallbackFontSize(*this, AlignLeftNode);
	const float AlignRightFallback = ResolvePhase32B1FallbackFontSize(*this, AlignRightNode);
	TestTrue(TEXT("Changing alignment does not change 32B1 layout-shape fallback output class"),
		FMath::IsNearlyEqual(AlignLeftFallback, AlignRightFallback, 0.01f));

	const TextMeasurement::Testing::FProtectedMeasuredCandidateResult AlignLeftCandidate =
		TextMeasurement::Testing::ResolveProtectedMeasuredCandidate(AlignLeftNode);
	const TextMeasurement::Testing::FProtectedMeasuredCandidateResult AlignRightCandidate =
		TextMeasurement::Testing::ResolveProtectedMeasuredCandidate(AlignRightNode);
	TestTrue(TEXT("Alignment variants keep the same protected candidate availability"),
		AlignLeftCandidate.bCandidateAvailable == AlignRightCandidate.bCandidateAvailable);
	TestTrue(TEXT("Alignment variants keep the same protected candidate branch shape"),
		AlignLeftCandidate.MeasurementAttempts == AlignRightCandidate.MeasurementAttempts
		&& FMath::IsNearlyEqual(AlignLeftCandidate.CandidateScale, AlignRightCandidate.CandidateScale, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementPhase32B1LayoutShapeGoalGuardTest,
	"Design2UMG.TextMeasurement.Phase32B1.LayoutShapeGoalGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementPhase32B1LayoutShapeGoalGuardTest::RunTest(const FString& Parameters)
{
	FString Source;
	if (!LoadTextMeasurementSplitSources(*this, Source))
	{
		return false;
	}

	FString CalibrationSource;
	if (!ExtractSourceBetween(
		*this,
		Source,
		TEXT("// BEGIN TextMeasurementCalibration.cpp"),
		TEXT("// END TextMeasurementCalibration.cpp"),
		CalibrationSource))
	{
		return false;
	}

	FString SingleLineHeuristicsSource;
	if (!ExtractSourceBetween(
		*this,
		Source,
		TEXT("// BEGIN TextMeasurementSingleLineHeuristics.cpp"),
		TEXT("// END TextMeasurementSingleLineHeuristics.cpp"),
		SingleLineHeuristicsSource))
	{
		return false;
	}

	FString SelectorSource;
	if (!ExtractSourceBetween(
		*this,
		CalibrationSource,
		TEXT("enum class EPhase32B1LayoutShapeGoal"),
		TEXT("TextMeasurementRuntimeProbe::FTextMeasureRequest BuildMetricProbeRequest"),
		SelectorSource))
	{
		return false;
	}

	FString MeasuredFallbackSource;
	if (!ExtractSourceBetween(
		*this,
		CalibrationSource,
		TEXT("TOptional<float> TryResolveMeasuredTextFontFallbackScale"),
		TEXT("FProtectedMeasuredCandidate ResolveProtectedMeasuredCandidate"),
		MeasuredFallbackSource))
	{
		return false;
	}

	TestTrue(TEXT("32B1 defines a fixed-width single-line goal"),
		SelectorSource.Contains(TEXT("FixedWidthSingleLine")));
	TestTrue(TEXT("32B1 defines a fixed-width multiline body goal"),
		SelectorSource.Contains(TEXT("FixedWidthMultilineBody")));
	TestTrue(TEXT("32B1 defines an intrinsic single-line label goal"),
		SelectorSource.Contains(TEXT("IntrinsicSingleLineUiLabel")));
	TestTrue(TEXT("32B1 defines a truncate/ellipsis goal"),
		SelectorSource.Contains(TEXT("TruncateEllipsis")));
	TestTrue(TEXT("32B1 defines an icon-font text goal"),
		SelectorSource.Contains(TEXT("IconFontText")));
	TestTrue(TEXT("Truncate goal uses existing source metadata only"),
		SelectorSource.Contains(TEXT("SourceMetadata::IsFigmaTruncateTextNode(Node)")));
	TestTrue(TEXT("Icon-font goal uses node semantics already on the fallback path"),
		SelectorSource.Contains(TEXT("EDesignNodeType::IconFont")));

	TestFalse(TEXT("Layout selector must not call IsWrappingTextMode"),
		SelectorSource.Contains(TEXT("IsWrappingTextMode")));
	TestFalse(TEXT("Layout selector must not call ShouldAutoWrapText"),
		SelectorSource.Contains(TEXT("ShouldAutoWrapText")));
	TestFalse(TEXT("Layout selector must not scan text content"),
		SelectorSource.Contains(TEXT("Content.Contains")));
	TestFalse(TEXT("Layout selector must not count words"),
		SelectorSource.Contains(TEXT("CountSingleLineTextWords")));
	TestFalse(TEXT("Layout selector must not branch on node id"),
		SelectorSource.Contains(TEXT("Node->Id")));
	TestFalse(TEXT("Layout selector must not branch on node name"),
		SelectorSource.Contains(TEXT("Node->Name")));
	TestFalse(TEXT("Layout selector must not branch on alignment"),
		SelectorSource.Contains(TEXT("TextAlign")));

	TestTrue(TEXT("Measured fallback uses the 32B1 layout-shape decision"),
		MeasuredFallbackSource.Contains(TEXT("const FPhase32B1LayoutShapeDecision LayoutDecision")));
	TestTrue(TEXT("Unknown layout shape fails closed before measurement"),
		MeasuredFallbackSource.Contains(TEXT("LayoutDecision.Goal == EPhase32B1LayoutShapeGoal::Unknown")));
	TestFalse(TEXT("Measured fallback no longer uses IsWrappingTextMode as selector input"),
		MeasuredFallbackSource.Contains(TEXT("IsWrappingTextMode")));
	TestFalse(TEXT("Measured fallback no longer uses newline checks as selector input"),
		MeasuredFallbackSource.Contains(TEXT("Content.Contains")));
	TestFalse(TEXT("32B1 helper must not live in TextMeasurementSingleLineHeuristics.cpp"),
		SingleLineHeuristicsSource.Contains(TEXT("Phase32B1LayoutShape")));
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
