// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "FontResolver.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/SlateTypes.h"
#include "TextMeasurement.h"

#include <limits>

#if WITH_AUTOMATION_WORKER

namespace
{
	FString GetCoreSourcePath(const TCHAR* RelativePath)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source/Design2UMGCore"), RelativePath)
			: FString();
	}

	bool LoadActiveTextMeasurementSources(FAutomationTestBase& Test, FString& OutSource)
	{
		OutSource.Reset();
		const TCHAR* SourceFiles[] = {
			TEXT("Private/TextMeasurement.cpp"),
			TEXT("Private/TextMeasurementRuntimeProbe.cpp"),
			TEXT("Private/TextMeasurementRuntimeProbeSizing.cpp"),
			TEXT("Private/TextMeasurementInternal.h"),
			TEXT("Private/TextFontResolution.h"),
			TEXT("Private/SizeEstimator.cpp"),
			TEXT("Private/Design2UMGTextWidgetBuilder.cpp"),
			TEXT("Private/TextMeasurement.h"),
			TEXT("Public/UMGSchemaTypedPayloads.h"),
		};

		for (const TCHAR* SourceFile : SourceFiles)
		{
			const FString SourcePath = GetCoreSourcePath(SourceFile);
			FString FileSource;
			if (!Test.TestFalse(*FString::Printf(TEXT("%s path resolved"), SourceFile), SourcePath.IsEmpty())
				|| !Test.TestTrue(*FString::Printf(TEXT("Load %s"), SourceFile), FFileHelper::LoadFileToString(FileSource, *SourcePath)))
			{
				return false;
			}
			OutSource += FileSource;
		}

		return true;
	}

	TSharedPtr<FDesignNode> MakeTextNode(const FString& Id, const float SourceFontSize)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Id;
		Node->Type = EDesignNodeType::Text;
		Node->Text.Content = TEXT("Design2UMG");
		Node->Text.FontSize = SourceFontSize;
		Node->Text.TextGrowth = EDesignTextGrowth::Auto;
		Node->Transform.Width = FDesignSize::Fit(0.0f);
		Node->Transform.Height = FDesignSize::Fit(0.0f);
		return Node;
	}

	float ExpectedExactPointSize(const float SourceFontSize)
	{
		return SourceFontSize * 72.0f / static_cast<float>(FontConstants::RenderDPI);
	}

	bool TryMeasureSlateText(const TSharedPtr<FDesignNode>& Node, const float FontSize, FVector2D& OutSize)
	{
		OutSize = FVector2D::ZeroVector;
		if (!Node.IsValid() || FontSize <= 0.0f || !FSlateApplication::IsInitialized()
			|| FSlateApplication::Get().GetRenderer() == nullptr)
		{
			return false;
		}

		FSlateFontInfo FontInfo = FTextBlockStyle::GetDefault().Font;
		FontInfo.Size = FontSize;
		const FString FontAssetPath = FFontResolver::FindFontAssetPath(Node->Text.FontFamily);
		if (!FontAssetPath.IsEmpty())
		{
			FontInfo.FontObject = LoadObject<UObject>(nullptr, *FontAssetPath);
		}
		const FString TypefaceName = FFontResolver::FindBestTypeface(Node->Text.FontFamily, Node->Text.FontWeight);
		if (!TypefaceName.IsEmpty())
		{
			FontInfo.TypefaceFontName = FName(*TypefaceName);
		}

		const TSharedRef<FSlateFontMeasure> FontMeasure =
			FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		OutSize = FontMeasure->Measure(TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content), FontInfo);
		return FMath::IsFinite(OutSize.X) && OutSize.X > 0.0f
			&& FMath::IsFinite(OutSize.Y) && OutSize.Y > 0.0f;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTextMeasurementPhase32CSourceUnitConversionTest,
	"Design2UMG.TextMeasurement.Phase32C.SourceUnitConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTextMeasurementPhase32CSourceUnitConversionTest::RunTest(const FString&)
{
	TSharedPtr<FDesignNode> Node = MakeTextNode(TEXT("phase32c_unit_conversion"), 32.0f);
	const float ResolvedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
	TestTrue(TEXT("32 source pixels convert to Slate points"),
		FMath::IsNearlyEqual(ResolvedFontSize, ExpectedExactPointSize(32.0f), 0.001f));

	Node = MakeTextNode(TEXT("phase32c_fractional_unit_conversion"), 14.0f);
	TestTrue(
		TEXT("14 source pixels preserve the exact 10.5 Slate point size"),
		FMath::IsNearlyEqual(
			TextMeasurement::ResolveTextFontSizeForUMG(Node),
			ExpectedExactPointSize(14.0f),
			0.001f));

	const TSharedPtr<FDesignNode> SubpixelNode = MakeTextNode(TEXT("phase32c_subpixel_unit_conversion"), 0.5f);
	TestTrue(
		TEXT("0.5 source pixels convert to 0.375 Slate points without a minimum clamp"),
		FMath::IsNearlyEqual(
			TextMeasurement::ResolveTextFontSizeForUMG(SubpixelNode),
			0.375f,
			0.001f));

	Node->Text.LineHeight = 2.5f;
	TestEqual(
		TEXT("Line height does not redefine an unconstrained font size"),
		TextMeasurement::ResolveTextFontSizeForUMG(Node),
		ExpectedExactPointSize(14.0f));

	Node->Text.LineHeight = 0.0f;
	Node->Transform.Width = FDesignSize::Fixed(1.0f);
	Node->Transform.bHasWidth = true;
	Node->Transform.bSourceHasWidth = true;
	TestEqual(
		TEXT("Unresolvable constraints keep the exact source-derived size"),
		TextMeasurement::ResolveTextFontSizeForUMG(Node),
		ExpectedExactPointSize(14.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTextMeasurementRuntimeProbeFractionalSizeConsistencyTest,
	"Design2UMG.TextMeasurement.RuntimeProbeFractionalSizeConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTextMeasurementRuntimeProbeFractionalSizeConsistencyTest::RunTest(const FString&)
{
	const TSharedPtr<FDesignNode> Node = MakeTextNode(TEXT("fractional_probe_consistency"), 14.0f);
	Node->Text.Content = TEXT("Aurora Scout");
	Node->Text.FontFamily = TEXT("Geist");
	Node->Text.FontWeight = TEXT("normal");

	const float ResolvedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
	TestTrue(TEXT("Direct resolution preserves the exact 10.5 point size"),
		FMath::IsNearlyEqual(ResolvedFontSize, 10.5f, 0.001f));

	FVector2D ExactSize;
	FVector2D RoundedSize;
	if (!TestTrue(TEXT("Exact Slate measurement succeeds"), TryMeasureSlateText(Node, ResolvedFontSize, ExactSize))
		|| !TestTrue(TEXT("Rounded Slate measurement succeeds"), TryMeasureSlateText(Node, 11.0f, RoundedSize)))
	{
		return false;
	}
	TestFalse(TEXT("The fractional and rounded probes are observably different"), ExactSize.Equals(RoundedSize, 0.01f));

	const FVector2D ProductionSize = TextMeasurement::MeasureTextNodeSize(Node, 0.0f);
	TestTrue(TEXT("Production measurement uses the resolved fractional size"), ProductionSize.Equals(ExactSize, 0.01f));

	const TSharedPtr<FDesignNode> SubpixelNode = MakeTextNode(TEXT("subpixel_probe_consistency"), 0.5f);
	SubpixelNode->Text.Content = TEXT("Subpixel probe text");
	SubpixelNode->Text.FontFamily = TEXT("Geist");
	SubpixelNode->Text.FontWeight = TEXT("normal");
	const float SubpixelPointSize = TextMeasurement::ResolveTextFontSizeForUMG(SubpixelNode);
	TestTrue(TEXT("Subpixel source size resolves to 0.375 points"),
		FMath::IsNearlyEqual(SubpixelPointSize, 0.375f, 0.001f));

	FVector2D DirectSubpixelSize;
	if (!TestTrue(TEXT("Direct 0.375 point Slate measurement succeeds"),
		TryMeasureSlateText(SubpixelNode, SubpixelPointSize, DirectSubpixelSize)))
	{
		return false;
	}
	const FVector2D ProductionSubpixelSize = TextMeasurement::MeasureTextNodeSize(SubpixelNode, 0.0f);
	TestTrue(TEXT("Production probe preserves the 0.375 point size"),
		ProductionSubpixelSize.Equals(DirectSubpixelSize, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTextMeasurementPhase32CInvalidSourceSizeTest,
	"Design2UMG.TextMeasurement.Phase32C.InvalidSourceSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTextMeasurementPhase32CInvalidSourceSizeTest::RunTest(const FString&)
{
	const float InvalidSizes[] = {
		0.0f,
		-1.0f,
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(),
	};

	for (const float InvalidSize : InvalidSizes)
	{
		const TSharedPtr<FDesignNode> Node = MakeTextNode(TEXT("phase32c_invalid_source_size"), InvalidSize);
		TestTrue(TEXT("Invalid source size resolves to zero"), FMath::IsNearlyZero(TextMeasurement::ResolveTextFontSizeForUMG(Node)));
	}

	const float AboveRendererLimitSourceSize =
		(static_cast<float>(std::numeric_limits<uint16>::max()) + 1024.0f)
		* static_cast<float>(FontConstants::RenderDPI)
		/ 72.0f;
	TestTrue(
		TEXT("Converted size above the renderer uint16 limit resolves to zero"),
		FMath::IsNearlyZero(TextMeasurement::ResolveTextFontSizeForUMG(
			MakeTextNode(TEXT("phase32c_renderer_limit"), AboveRendererLimitSourceSize))));
	TestTrue(TEXT("Invalid node resolves to zero"), FMath::IsNearlyZero(TextMeasurement::ResolveTextFontSizeForUMG(nullptr)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTextMeasurementWrappedLayoutContractTest,
	"Design2UMG.TextMeasurement.WrappedLayoutContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTextMeasurementWrappedLayoutContractTest::RunTest(const FString&)
{
	TSharedPtr<FDesignNode> Node = MakeTextNode(TEXT("wrapped_diagnostic_contract"), 120.0f);
	Node->Text.Content = TEXT("Welcome to Pencil!");
	Node->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	Node->Text.LineHeight = 1.1f;
	Node->Text.FontFamily = TEXT("Funnel Display");
	Node->Text.FontWeight = TEXT("500");
	Node->Text.SetLetterSpacingValue(-3.6f);
	Node->Transform.Width = FDesignSize::Fixed(630.0f);
	Node->Transform.bSourceHasWidth = true;
	const float PreferredFontSize = ExpectedExactPointSize(Node->Text.FontSize);

	const float ResolvedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
	TestEqual(TEXT("Wrapped text keeps the source-derived size when its wrapped layout fits"),
		ResolvedFontSize, PreferredFontSize);

	TestEqual(TEXT("Wrapped text keeps the source wrap width"), TextMeasurement::ResolveTextWrapAt(Node, 0.0f), 630.0f);
	TestTrue(TEXT("Wrapped text applies its explicit source line height"),
		TextMeasurement::ShouldApplyExplicitLineHeight(Node, ResolvedFontSize, 630.0f));
	const FVector2D MeasuredSize = TextMeasurement::MeasureTextNodeSize(Node, 0.0f);
	TestTrue(TEXT("Wrapped text remains measurable"), MeasuredSize.X > 0.0f && MeasuredSize.Y > 0.0f);

	TSharedPtr<FDesignNode> ExplicitHeightNode = MakeShared<FDesignNode>(*Node);
	ExplicitHeightNode->Id = TEXT("wrapped_explicit_height_contract");
	ExplicitHeightNode->Transform.Height = FDesignSize::Fixed(1000.0f);
	ExplicitHeightNode->Transform.bSourceHasHeight = true;
	TestTrue(TEXT("Text with an explicit source height keeps line-height behavior"),
		TextMeasurement::ShouldApplyExplicitLineHeight(
			ExplicitHeightNode,
			TextMeasurement::ResolveTextFontSizeForUMG(ExplicitHeightNode),
			630.0f));

	TSharedPtr<FDesignNode> SmallNode = MakeTextNode(TEXT("small_wrapped_line_range_contract"), 12.0f);
	SmallNode->Text.Content = TEXT("Advanced analytics and priority support.");
	SmallNode->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	SmallNode->Text.LineHeight = 1.5f;
	SmallNode->Text.FontFamily = TEXT("Inter");
	SmallNode->Text.FontWeight = TEXT("normal");
	SmallNode->Transform.Width = FDesignSize::Fixed(160.0f);
	SmallNode->Transform.bSourceHasWidth = true;
	TestEqual(TEXT("Small wrapped text keeps the source-derived size when wrapping fits"),
		TextMeasurement::ResolveTextFontSizeForUMG(SmallNode), ExpectedExactPointSize(SmallNode->Text.FontSize));
	TestEqual(TEXT("Small wrapped text keeps the source wrap width"),
		TextMeasurement::ResolveTextWrapAt(SmallNode, 0.0f), 160.0f);

	TSharedPtr<FDesignNode> AuthoredBreakNode = MakeTextNode(TEXT("authored_line_break_contract"), 48.0f);
	AuthoredBreakNode->Text.Content = TEXT("Pro tips:\n\n- First item\n- Second item");
	AuthoredBreakNode->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	AuthoredBreakNode->Text.LineHeight = 1.4583333333333333f;
	AuthoredBreakNode->Text.FontFamily = TEXT("Rubik");
	AuthoredBreakNode->Text.FontWeight = TEXT("normal");
	AuthoredBreakNode->Text.SetLetterSpacingValue(-1.44f);
	AuthoredBreakNode->Transform.Width = FDesignSize::Fixed(1469.0f);
	AuthoredBreakNode->Transform.bSourceHasWidth = true;
	TestEqual(
		TEXT("Authored line breaks keep the declared point size"),
		TextMeasurement::ResolveTextFontSizeForUMG(AuthoredBreakNode),
		ExpectedExactPointSize(AuthoredBreakNode->Text.FontSize));
	TestEqual(
		TEXT("Authored line breaks keep the source wrap width"),
		TextMeasurement::ResolveTextWrapAt(AuthoredBreakNode, 0.0f),
		1469.0f);
	TestTrue(TEXT("Authored line breaks apply the explicit source line height"),
		TextMeasurement::ShouldApplyExplicitLineHeight(
			AuthoredBreakNode,
			TextMeasurement::ResolveTextFontSizeForUMG(AuthoredBreakNode),
			1469.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTextMeasurementSourceConstraintIndependenceTest,
	"Design2UMG.TextMeasurement.SourceConstraintIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTextMeasurementSourceConstraintIndependenceTest::RunTest(const FString&)
{
	enum class EConstraintAxes : uint8
	{
		None,
		Width,
		Height,
		Both
	};
	struct FConstraintCase
	{
		const TCHAR* Label;
		EConstraintAxes Axes;
	};
	const FConstraintCase Cases[] = {
		{TEXT("unconstrained"), EConstraintAxes::None},
		{TEXT("width-constrained"), EConstraintAxes::Width},
		{TEXT("height-constrained"), EConstraintAxes::Height},
		{TEXT("both-constrained"), EConstraintAxes::Both},
	};

	for (const FConstraintCase& TestCase : Cases)
	{
		TSharedPtr<FDesignNode> Node = MakeTextNode(TestCase.Label, 40.0f);
		const bool bHasWidth = TestCase.Axes == EConstraintAxes::Width || TestCase.Axes == EConstraintAxes::Both;
		const bool bHasHeight = TestCase.Axes == EConstraintAxes::Height || TestCase.Axes == EConstraintAxes::Both;
		if (bHasWidth)
		{
			Node->Transform.Width = FDesignSize::Fixed(1.0f);
			Node->Transform.bSourceHasWidth = true;
		}
		if (bHasHeight)
		{
			Node->Transform.Height = FDesignSize::Fixed(1.0f);
			Node->Transform.bSourceHasHeight = true;
		}

		const float ResolvedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
		TestTrue(
			*FString::Printf(TEXT("%s keeps the exact source-derived size"), TestCase.Label),
			FMath::IsNearlyEqual(ResolvedFontSize, ExpectedExactPointSize(Node->Text.FontSize), 0.001f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTextMeasurementPhase32CSourceGuardTest,
	"Design2UMG.TextMeasurement.Phase32C.SourceGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTextMeasurementPhase32CSourceGuardTest::RunTest(const FString&)
{
	FString Source;
	if (!LoadActiveTextMeasurementSources(*this, Source))
	{
		return false;
	}

	const TCHAR* RemovedTokens[] = {
		TEXT("FamilyFallbackScales"),
		TEXT("FindFamilySpecificTextFallbackScale"),
		TEXT("DefaultFontFallbackScale"),
		TEXT("DefaultLineHeightMultiplier"),
		TEXT("0.75f"),
		TEXT("1.285714"),
		TEXT("geist"),
		TEXT("robotomono"),
	};
	for (const TCHAR* Token : RemovedTokens)
	{
		TestFalse(
			*FString::Printf(TEXT("Removed text-measurement token is absent: %s"), Token),
			Source.Contains(Token, ESearchCase::IgnoreCase));
	}

	const TCHAR* DeletedFiles[] = {
		TEXT("Private/TextMeasurementCache.cpp"),
		TEXT("Private/TextMeasurementCalibration.cpp"),
		TEXT("Private/TextMeasurementSingleLineHeuristics.cpp"),
	};
	for (const TCHAR* DeletedFile : DeletedFiles)
	{
		TestFalse(*FString::Printf(TEXT("Obsolete source file is deleted: %s"), DeletedFile), IFileManager::Get().FileExists(*GetCoreSourcePath(DeletedFile)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTextMeasurementRuntimeContentPreservesParagraphBreaksTest,
	"Design2UMG.TextMeasurement.RuntimeContentPreservesParagraphBreaks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTextMeasurementRuntimeContentPreservesParagraphBreaksTest::RunTest(const FString&)
{
	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(TEXT("first\r\n\r\nsecond"));
	TestEqual(TEXT("Runtime normalization preserves paragraph breaks"), RuntimeText, FString(TEXT("first\n\nsecond")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTextMeasurementPublicHeaderRemovedTest,
	"Design2UMG.TextMeasurement.PublicHeaderRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTextMeasurementPublicHeaderRemovedTest::RunTest(const FString&)
{
	TestFalse(TEXT("TextMeasurement is not exposed as a public header"), IFileManager::Get().FileExists(*GetCoreSourcePath(TEXT("Public/TextMeasurement.h"))));
	TestTrue(TEXT("Private TextMeasurement header exists"), IFileManager::Get().FileExists(*GetCoreSourcePath(TEXT("Private/TextMeasurement.h"))));
	return true;
}

#endif // WITH_AUTOMATION_WORKER
