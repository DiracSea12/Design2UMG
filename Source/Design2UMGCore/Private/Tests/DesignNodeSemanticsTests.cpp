// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "Design2UMGConverter.h"
#include "Design2UMGConverterFlowChildPreResolution.h"
#include "Design2UMGConverterVisualSupport.h"
#include "Design2UMGCoreTestHelpers.h"
#include "Design2UMGCoreTestNodes.h"
#include "Design2UMGNodeWidgetFactory.h"
#include "Design2UMGSettings.h"
#include "Design2UMGSourceMetadata.h"
#include "Design2UMGTextWidgetBuilder.h"
#include "DesignReferenceExpansion.h"
#include "DesignSchemaSourceExtensions.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "ImageFillUtils.h"
#include "PenParser.h"
#include "DesignNodeSemantics.h"
#include "SizeEstimator.h"
#include "TextMeasurement.h"
#include "UMGSchema.h"

#include <limits>

using Design2UMGCoreTests::FindWidgetByIdRecursive;

#if WITH_AUTOMATION_WORKER
namespace
{
	TSharedPtr<FDesignNode> MakeWidthHintProbeTextNode(const FString& Id, const FDesignSize& Width)
	{
		TSharedPtr<FDesignNode> Node = Design2UMGCoreTests::MakeNode(Id, EDesignNodeType::Text, Width, FDesignSize::Fixed(36.0f));
		Node->Text.Content = TEXT("Width hint should stay consistent between sizing and wrap logic.");
		Node->Text.FontFamily = TEXT("Roboto");
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 16.0f;
		Node->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
		return Node;
	}

	TSharedPtr<FDesignNode> MakeSourceTextBoxProbeNode(
		const FString& Id,
		const EDesignNodeType Type,
		const FDesignSize& Width,
		const FDesignSize& Height)
	{
		TSharedPtr<FDesignNode> Node = Design2UMGCoreTests::MakeNode(Id, Type, Width, Height);
		Node->Text.Content = TEXT("Source text box probe content");
		Node->Text.IconFontName = TEXT("source-text-box-probe-icon");
		Node->Text.FontSize = 12.0f;
		Node->Text.TextGrowth = EDesignTextGrowth::Auto;
		Node->Transform.bSourceHasWidth = true;
		Node->Transform.bSourceHasHeight = true;
		return Node;
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

	struct FDesignNodeSemanticsVisibilityFixture
	{
		TSharedPtr<FDesignNode> Root;
		TSharedPtr<FDesignNode> DisabledLeaf;
		TSharedPtr<FDesignNode> DesignerOnlyLeaf;
		TSharedPtr<FDesignNode> NonRenderableLeaf;
		TSharedPtr<FDesignNode> FlowContainerLeaf;
		TSharedPtr<FDesignNode> RenderableLeaf;
		TSharedPtr<FDesignNode> TextLeaf;
	};

	FDesignNodeSemanticsVisibilityFixture MakeDesignNodeSemanticsVisibilityFixture()
	{
		FDesignNodeSemanticsVisibilityFixture Fixture;
		Fixture.Root = Design2UMGCoreTests::MakeNode(TEXT("shared_semantics_root"), EDesignNodeType::Frame, FDesignSize::Fixed(640.0f), FDesignSize::Fixed(360.0f));
		Fixture.Root->LayoutData.Layout = EDesignLayout::Vertical;

		Fixture.DisabledLeaf = Design2UMGCoreTests::MakeNode(TEXT("skip_disabled"), EDesignNodeType::Rectangle);
		Fixture.DisabledLeaf->Transform.bEnabled = false;

		Fixture.DesignerOnlyLeaf = Design2UMGCoreTests::MakeNode(TEXT("skip_designer_only"), EDesignNodeType::Connection);
		Fixture.NonRenderableLeaf = Design2UMGCoreTests::MakeNode(TEXT("skip_non_renderable"), EDesignNodeType::Rectangle);

		Fixture.FlowContainerLeaf = Design2UMGCoreTests::MakeNode(TEXT("keep_flow_container"), EDesignNodeType::Frame);
		Fixture.FlowContainerLeaf->LayoutData.Layout = EDesignLayout::Horizontal;

		Fixture.RenderableLeaf = Design2UMGCoreTests::MakeNode(TEXT("keep_renderable"), EDesignNodeType::Rectangle);
		Fixture.RenderableLeaf->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#2288CC")));

		Fixture.TextLeaf = Design2UMGCoreTests::MakeNode(TEXT("keep_text"), EDesignNodeType::Text, FDesignSize::Fixed(220.0f), FDesignSize::Fixed(36.0f));
		Fixture.TextLeaf->Text.Content = TEXT("Shared semantics consistency check");
		Fixture.TextLeaf->Text.FontFamily = TEXT("Roboto");
		Fixture.TextLeaf->Text.FontWeight = TEXT("Regular");
		Fixture.TextLeaf->Text.FontSize = 16.0f;
		Fixture.TextLeaf->Text.TextGrowth = EDesignTextGrowth::FixedWidth;

		Fixture.Root->Children = {
			Fixture.DisabledLeaf,
			Fixture.DesignerOnlyLeaf,
			Fixture.NonRenderableLeaf,
			Fixture.FlowContainerLeaf,
			Fixture.RenderableLeaf,
			Fixture.TextLeaf
		};

		return Fixture;
	}

	bool RunDesignNodeSemanticsSkipParityChecks(FAutomationTestBase& Test)
	{
		bool bAllPassed = true;
		const FDesignNodeSemanticsVisibilityFixture Fixture = MakeDesignNodeSemanticsVisibilityFixture();

		struct FSkipExpectation
		{
			const TCHAR* Label;
			TSharedPtr<FDesignNode> Node;
			bool bShouldSkip = false;
			bool bShouldRemainVisible = false;
			bool bShouldRemainConverted = false;
		};

		const TArray<FSkipExpectation> Expectations = {
			{ TEXT("Disabled leaf"), Fixture.DisabledLeaf, true, false, false },
			{ TEXT("Designer-only leaf"), Fixture.DesignerOnlyLeaf, true, false, false },
			{ TEXT("Non-renderable leaf"), Fixture.NonRenderableLeaf, true, false, false },
			{ TEXT("Flow container leaf"), Fixture.FlowContainerLeaf, false, true, true },
			{ TEXT("Renderable leaf"), Fixture.RenderableLeaf, false, true, true },
			{ TEXT("Text leaf"), Fixture.TextLeaf, false, true, true },
		};

		for (const FSkipExpectation& Expectation : Expectations)
		{
			bAllPassed &= Test.TestEqual(
				*FString::Printf(TEXT("%s shared helper skip contract"), Expectation.Label),
				Design2UMGCore::DesignNodeSemantics::ShouldSkipNode(Expectation.Node),
				Expectation.bShouldSkip);
		}

		TArray<TSharedPtr<FDesignNode>> VisibleChildren;
		SizeEstimator::GatherVisibleChildren(Fixture.Root, VisibleChildren);
		const TSet<FString> VisibleIds = CollectNodeIds(VisibleChildren);

		for (const FSkipExpectation& Expectation : Expectations)
		{
			bAllPassed &= Test.TestEqual(
				*FString::Printf(TEXT("%s sizing visibility stays in parity"), Expectation.Label),
				VisibleIds.Contains(Expectation.Node->Id),
				Expectation.bShouldRemainVisible);
		}

		FDesignDocument Doc;
		Doc.Children.Add(Fixture.Root);
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());

		for (const FSkipExpectation& Expectation : Expectations)
		{
			const bool bFoundInConvertedTree = FindWidgetByIdRecursive(Data.RootWidget, Expectation.Node->Id) != nullptr;
			bAllPassed &= Test.TestEqual(
				*FString::Printf(TEXT("%s converted-tree visibility stays in parity"), Expectation.Label),
				bFoundInConvertedTree,
				Expectation.bShouldRemainConverted);
		}

		return bAllPassed;
	}

	bool RunDesignNodeSemanticsWidthHintChecks(FAutomationTestBase& Test)
	{
		bool bAllPassed = true;

		struct FWidthHintProbeCase
		{
			FString Name;
			TSharedPtr<FDesignNode> Node;
			float ExpectedWidth = 0.0f;
		};

		const float ParentContentWidth = 280.0f;
		const TArray<FWidthHintProbeCase> Cases = {
			{ TEXT("fixed-width"), MakeWidthHintProbeTextNode(TEXT("width_fixed"), FDesignSize::Fixed(180.0f)), 180.0f },
			{ TEXT("fill-width-with-fallback"), MakeWidthHintProbeTextNode(TEXT("width_fill_fallback"), FDesignSize::Fill(300.0f)), 300.0f },
			{ TEXT("fill-width-parent-fallback"), MakeWidthHintProbeTextNode(TEXT("width_fill_parent"), FDesignSize::Fill(0.0f)), ParentContentWidth }
		};

		for (const FWidthHintProbeCase& ProbeCase : Cases)
		{
			const float CanonicalWidth = Design2UMGCore::DesignNodeSemantics::ResolveWidthHint(ProbeCase.Node, ParentContentWidth);
			const float EstimatedWidth = SizeEstimator::EstimateNodeSize(ProbeCase.Node, ParentContentWidth).X;
			const float WrapAt = TextMeasurement::ResolveTextWrapAt(ProbeCase.Node, ParentContentWidth);

			bAllPassed &= Test.TestTrue(
				*FString::Printf(TEXT("%s canonical width hint remains stable"), *ProbeCase.Name),
				FMath::IsNearlyEqual(CanonicalWidth, ProbeCase.ExpectedWidth, KINDA_SMALL_NUMBER));
			bAllPassed &= Test.TestTrue(
				*FString::Printf(TEXT("%s sizing width follows canonical helper"), *ProbeCase.Name),
				FMath::IsNearlyEqual(EstimatedWidth, CanonicalWidth, KINDA_SMALL_NUMBER));
			bAllPassed &= Test.TestTrue(
				*FString::Printf(TEXT("%s wrap width follows canonical helper"), *ProbeCase.Name),
				FMath::IsNearlyEqual(WrapAt, CanonicalWidth, KINDA_SMALL_NUMBER));
		}

		return bAllPassed;
	}

	bool RunDesignNodeSemanticsExecutableReplacementChecks(FAutomationTestBase& Test)
	{
		bool bAllPassed = true;
		bAllPassed &= RunDesignNodeSemanticsSkipParityChecks(Test);
		bAllPassed &= RunDesignNodeSemanticsWidthHintChecks(Test);
		return bAllPassed;
	}

	bool RunSourceTextBoxAuthorityChecks(FAutomationTestBase& Test)
	{
		bool bAllPassed = true;
		struct FAuthorityProbe
		{
			const TCHAR* Label;
			TSharedPtr<FDesignNode> Node;
			bool bExpectedAuthority = false;
		};

		TSharedPtr<FDesignNode> MissingSourceFlag = MakeSourceTextBoxProbeNode(
			TEXT("missing_source_flag_textbox"),
			EDesignNodeType::Text,
			FDesignSize::Fixed(120.0f),
			FDesignSize::Fixed(40.0f));
		MissingSourceFlag->Transform.bSourceHasHeight = false;

		TSharedPtr<FDesignNode> NonFinite = MakeSourceTextBoxProbeNode(
			TEXT("non_finite_textbox"),
			EDesignNodeType::Text,
			FDesignSize::Fixed(std::numeric_limits<float>::quiet_NaN()),
			FDesignSize::Fixed(40.0f));
		TSharedPtr<FDesignNode> FixedSourceText = MakeSourceTextBoxProbeNode(
			TEXT("fixed_source_textbox"),
			EDesignNodeType::Text,
			FDesignSize::Fixed(180.0f),
			FDesignSize::Fixed(48.0f));

		const TArray<FAuthorityProbe> Cases = {
			{ TEXT("Fixed source text"), FixedSourceText, true },
			{ TEXT("Fixed icon-font"), MakeSourceTextBoxProbeNode(TEXT("fixed_icon_textbox"), EDesignNodeType::IconFont, FDesignSize::Fixed(32.0f), FDesignSize::Fixed(30.0f)), true },
			{ TEXT("Fit-content minimum"), MakeSourceTextBoxProbeNode(TEXT("fit_min_textbox"), EDesignNodeType::Text, FDesignSize::Fit(120.0f), FDesignSize::Fit(36.0f)), false },
			{ TEXT("Fill fallback"), MakeSourceTextBoxProbeNode(TEXT("fill_fallback_textbox"), EDesignNodeType::Text, FDesignSize::Fill(120.0f), FDesignSize::Fill(36.0f)), false },
			{ TEXT("Hug unresolved"), MakeSourceTextBoxProbeNode(TEXT("hug_unresolved_textbox"), EDesignNodeType::Text, FDesignSize::Fit(0.0f), FDesignSize::Fit(0.0f)), false },
			{ TEXT("Missing source flag"), MissingSourceFlag, false },
			{ TEXT("Non-finite fixed dimension"), NonFinite, false }
		};

		for (const FAuthorityProbe& Probe : Cases)
		{
			float ProbeWidth = 0.0f;
			float ProbeHeight = 0.0f;
			bAllPassed &= Test.TestEqual(
				*FString::Printf(TEXT("%s concrete source textbox authority"), Probe.Label),
				Design2UMGCore::DesignNodeSemantics::TryGetConcreteSourceTextBoxSize(Probe.Node, ProbeWidth, ProbeHeight),
				Probe.bExpectedAuthority);
		}

		float SourceWidth = 0.0f;
		float SourceHeight = 0.0f;
		bAllPassed &= Test.TestTrue(TEXT("Fixed source textbox size resolves"),
			Design2UMGCore::DesignNodeSemantics::TryGetConcreteSourceTextBoxSize(FixedSourceText, SourceWidth, SourceHeight));
		bAllPassed &= Test.TestTrue(TEXT("Fixed source textbox width resolves"),
			FMath::IsNearlyEqual(SourceWidth, 180.0f, KINDA_SMALL_NUMBER));
		bAllPassed &= Test.TestTrue(TEXT("Fixed source textbox height resolves"),
			FMath::IsNearlyEqual(SourceHeight, 48.0f, KINDA_SMALL_NUMBER));
		return bAllPassed;
	}

	bool RunConverterOwnerBoundaryChecks(FAutomationTestBase& Test)
	{
		FDesignDocument Doc;

		TSharedPtr<FDesignNode> ReusableCard = Design2UMGCoreTests::MakeNode(
			TEXT("reusable_card"),
			EDesignNodeType::Rectangle,
			FDesignSize::Fixed(60.0f),
			FDesignSize::Fixed(20.0f));
		ReusableCard->bReusable = true;
		ReusableCard->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#AA3344")));
		Doc.Children.Add(ReusableCard);

		TSharedPtr<FDesignNode> Screen = Design2UMGCoreTests::MakeNode(
			TEXT("child_preparation_screen"),
			EDesignNodeType::Frame,
			FDesignSize::Fixed(200.0f),
			FDesignSize::Fixed(40.0f));
		Screen->LayoutData.Layout = EDesignLayout::Horizontal;
		Screen->LayoutData.SetGap(10.0f);

		TSharedPtr<FDesignNode> RefChild = MakeShared<FDesignNode>();
		RefChild->Id = TEXT("reusable_card_instance");
		RefChild->Name = TEXT("reusable_card_instance");
		RefChild->Type = EDesignNodeType::Ref;
		RefChild->Ref = TEXT("reusable_card");

		TSharedPtr<FDesignNode> FillChild = Design2UMGCoreTests::MakeNode(
			TEXT("fill_sibling"),
			EDesignNodeType::Rectangle,
			FDesignSize::Fill(0.0f),
			FDesignSize::Fill(0.0f));
		FillChild->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#33AA44")));

		Screen->Children.Add(RefChild);
		Screen->Children.Add(FillChild);
		Doc.Children.Add(Screen);

		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
		const FUMGWidgetNode* ScreenWidget = FindWidgetByIdRecursive(Data.RootWidget, TEXT("child_preparation_screen"));
		if (!Test.TestNotNull(TEXT("Converted screen widget exists"), ScreenWidget))
		{
			return false;
		}

		const FUMGWidgetNode* RefWidget = FindWidgetByIdRecursive(Data.RootWidget, TEXT("reusable_card_instance"));
		if (!Test.TestNotNull(TEXT("Expanded ref child widget exists"), RefWidget))
		{
			return false;
		}

		const FUMGWidgetNode* FillWidget = FindWidgetByIdRecursive(Data.RootWidget, TEXT("fill_sibling"));
		if (!Test.TestNotNull(TEXT("Fill sibling widget exists"), FillWidget))
		{
			return false;
		}

		bool bAllPassed = true;
		bAllPassed &= Test.TestTrue(TEXT("Reusable definition stays out of render tree"), FindWidgetByIdRecursive(Data.RootWidget, TEXT("reusable_card")) == nullptr);
		bAllPassed &= Test.TestTrue(TEXT("Expanded ref child keeps authored width"),
			FMath::IsNearlyEqual(RefWidget->Bounds.W, 60.0f, KINDA_SMALL_NUMBER));
		bAllPassed &= Test.TestTrue(TEXT("Expanded ref child keeps authored height"),
			FMath::IsNearlyEqual(RefWidget->Bounds.H, 20.0f, KINDA_SMALL_NUMBER));
		bAllPassed &= Test.TestEqual(TEXT("Screen still appends both visible children"), ScreenWidget->Children.Num(), 2);
		bAllPassed &= Test.TestTrue(TEXT("Expanded ref width should be consumed before resolving fill sibling width"),
			FMath::IsNearlyEqual(FillWidget->Bounds.W, 130.0f, KINDA_SMALL_NUMBER));
		bAllPassed &= Test.TestTrue(TEXT("Cross-axis fill sizing should still respect parent height"),
			FMath::IsNearlyEqual(FillWidget->Bounds.H, 40.0f, KINDA_SMALL_NUMBER));
		return bAllPassed;
	}

	bool RunFlowFillSourceConstraintChecks(FAutomationTestBase& Test)
	{
		FDesignDocument Doc;
		const TMap<FString, FString> EmptyTheme;
		Design2UMGConverterInternal::FFlowChildPreResolutionResolver Resolver(Doc, EmptyTheme);

		TSharedPtr<FDesignNode> Parent = Design2UMGCoreTests::MakeNode(
			TEXT("source_fill_parent"),
			EDesignNodeType::Frame,
			FDesignSize::Fixed(248.0f),
			FDesignSize::Fixed(48.0f));
		Parent->LayoutData.Layout = EDesignLayout::Horizontal;
		FDesignPadding ParentPadding;
		ParentPadding.Left = 16.0f;
		ParentPadding.Right = 16.0f;
		Parent->LayoutData.SetPadding(ParentPadding);

		TSharedPtr<FDesignNode> SourceFillText = Design2UMGCoreTests::MakeNode(
			TEXT("source_fill_text"),
			EDesignNodeType::Text,
			FDesignSize::Fill(0.0f),
			FDesignSize::Fit(16.0f));
		SourceFillText->Text.Content = TEXT("Title");
		SourceFillText->Text.FontFamily = TEXT("JetBrains Mono");
		SourceFillText->Text.FontWeight = TEXT("normal");
		SourceFillText->Text.FontSize = 14.0f;
		SourceFillText->Text.LineHeight = 1.142857f;
		SourceFillText->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
		SourceFillText->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
		SourceFillText->Transform.bSourceHasWidth = true;
		SourceFillText->Transform.bSourceHasHeight = true;
		Parent->Children.Add(SourceFillText);

		FUMGWidgetNode ParentWidget;
		ParentWidget.Bounds.W = 248.0f;
		ParentWidget.Bounds.H = 48.0f;

		TArray<TSharedPtr<FDesignNode>> VisibleChildren;
		VisibleChildren.Add(SourceFillText);
		Resolver.PreResolveFlowChildren(Parent, ParentWidget, VisibleChildren);

		bool bAllPassed = true;
		bAllPassed &= Test.TestTrue(TEXT("Source fill child resolves to parent content width"),
			FMath::IsNearlyEqual(SourceFillText->Transform.Width.Value, 216.0f, KINDA_SMALL_NUMBER));
		bAllPassed &= Test.TestTrue(TEXT("Resolved source fill child keeps source width signal"),
			SourceFillText->Transform.bSourceHasWidth);
		bAllPassed &= Test.TestTrue(TEXT("Resolved source fill child remains fill mode for slot sizing"),
			SourceFillText->Transform.Width.IsFill());
		const float ResolvedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(SourceFillText);
		bAllPassed &= Test.TestTrue(TEXT("Resolved source fill title uses a positive fitted size"),
			ResolvedFontSize > 0.0f && ResolvedFontSize < SourceFillText->Text.FontSize);
		return bAllPassed;
	}

	bool RunFlowRefExpansionSnapshotChecks(FAutomationTestBase& Test)
	{
		FDesignDocument Doc;
		TSharedPtr<FDesignNode> NestedReusable = Design2UMGCoreTests::MakeNode(
			TEXT("snapshot_nested_reusable"),
			EDesignNodeType::Rectangle,
			FDesignSize::Fixed(12.0f),
			FDesignSize::Fixed(12.0f));
		NestedReusable->bReusable = true;
		Doc.Children.Add(NestedReusable);

		TSharedPtr<FDesignNode> Reusable = Design2UMGCoreTests::MakeNode(
			TEXT("snapshot_reusable"),
			EDesignNodeType::Rectangle,
			FDesignSize::Fit(24.0f),
			FDesignSize::Fixed(18.0f));
		Reusable->bReusable = true;
		Reusable->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#336699")));
		TSharedPtr<FDesignNode> NestedRef = MakeShared<FDesignNode>();
		NestedRef->Id = TEXT("snapshot_nested_ref");
		NestedRef->Name = TEXT("snapshot_nested_ref");
		NestedRef->Type = EDesignNodeType::Ref;
		NestedRef->Ref = TEXT("snapshot_nested_reusable");
		Reusable->Children.Add(NestedRef);
		Doc.Children.Add(Reusable);

		TSharedPtr<FDesignNode> RefInstance = MakeShared<FDesignNode>();
		RefInstance->Id = TEXT("snapshot_ref_instance");
		RefInstance->Name = TEXT("snapshot_ref_instance");
		RefInstance->Type = EDesignNodeType::Ref;
		RefInstance->Ref = TEXT("snapshot_reusable");

		const TMap<FString, FString> EmptyTheme;
		Design2UMGConverterInternal::FFlowChildPreResolutionResolver Resolver(Doc, EmptyTheme);
		const TSharedPtr<FDesignNode> FirstExpansion = Resolver.ExpandRef(RefInstance);
		const TSharedPtr<FDesignNode> FitExpansion = Resolver.ResolveExpandedChild(RefInstance, true);
		const TSharedPtr<FDesignNode> AppendExpansion = Resolver.ResolveExpandedChild(RefInstance, false);

		bool bAllPassed = true;
		bAllPassed &= Test.TestNotNull(TEXT("Ref snapshot expands successfully"), FirstExpansion.Get());
		bAllPassed &= Test.TestTrue(TEXT("Fit and append receive independent working copies"),
			FirstExpansion.Get() != FitExpansion.Get() && FitExpansion.Get() != AppendExpansion.Get());
		bAllPassed &= Test.TestTrue(TEXT("Working copies preserve the expanded geometry"),
			FMath::IsNearlyEqual(FitExpansion->Transform.Width.Value, AppendExpansion->Transform.Width.Value)
			&& FMath::IsNearlyEqual(FitExpansion->Transform.Height.Value, AppendExpansion->Transform.Height.Value));
		return bAllPassed;
	}

	struct FLuminosityOpacityCapOverrideScope
	{
		explicit FLuminosityOpacityCapOverrideScope(const float InOpacityCap)
		{
			Settings = UDesign2UMGSettings::Get();
			if (Settings)
			{
				bSavedUseCustomOverride = Settings->bUseCustomLuminosityFitOpacityCap;
				SavedOpacityCap = Settings->LuminosityFitOpacityCap;
				Settings->bUseCustomLuminosityFitOpacityCap = true;
				Settings->LuminosityFitOpacityCap = InOpacityCap;
			}
		}

		~FLuminosityOpacityCapOverrideScope()
		{
			if (Settings)
			{
				Settings->bUseCustomLuminosityFitOpacityCap = bSavedUseCustomOverride;
				Settings->LuminosityFitOpacityCap = SavedOpacityCap;
			}
		}

		UDesign2UMGSettings* Settings = nullptr;
		bool bSavedUseCustomOverride = false;
		float SavedOpacityCap = UDesign2UMGSettings::CanonicalLuminosityFitOpacityCap;
	};

	struct FPlaceholderHatchOverrideScope
	{
		FPlaceholderHatchOverrideScope(
			const FString& InBaseFillColor,
			const FString& InBaseStripeColor,
			const int32 InStripeWidth,
			const int32 InStripeGap)
		{
			Settings = UDesign2UMGSettings::Get();
			if (Settings)
			{
				bSavedUseCustomOverride = Settings->bUseCustomPlaceholderHatchStyle;
				SavedBaseFillColor = Settings->PlaceholderHatchBackgroundColor;
				SavedBaseStripeColor = Settings->PlaceholderHatchStripeColor;
				SavedStripeWidth = Settings->PlaceholderHatchStripeWidth;
				SavedStripeGap = Settings->PlaceholderHatchStripeGap;

				Settings->bUseCustomPlaceholderHatchStyle = true;
				Settings->PlaceholderHatchBackgroundColor = InBaseFillColor;
				Settings->PlaceholderHatchStripeColor = InBaseStripeColor;
				Settings->PlaceholderHatchStripeWidth = InStripeWidth;
				Settings->PlaceholderHatchStripeGap = InStripeGap;
			}
		}

		~FPlaceholderHatchOverrideScope()
		{
			if (Settings)
			{
				Settings->bUseCustomPlaceholderHatchStyle = bSavedUseCustomOverride;
				Settings->PlaceholderHatchBackgroundColor = SavedBaseFillColor;
				Settings->PlaceholderHatchStripeColor = SavedBaseStripeColor;
				Settings->PlaceholderHatchStripeWidth = SavedStripeWidth;
				Settings->PlaceholderHatchStripeGap = SavedStripeGap;
			}
		}

		UDesign2UMGSettings* Settings = nullptr;
		bool bSavedUseCustomOverride = false;
		FString SavedBaseFillColor;
		FString SavedBaseStripeColor;
		int32 SavedStripeWidth = 0;
		int32 SavedStripeGap = 0;
	};

	struct FIconFontSettingsOverrideScope
	{
		FIconFontSettingsOverrideScope(
			const FString& InDefaultIconFontAssetPath,
			const FString& InMissingIconFontPlaceholder)
		{
			Settings = UDesign2UMGSettings::Get();
			if (Settings)
			{
				SavedIconFontAssetMap = Settings->IconFontAssetMap;
				SavedDefaultIconFontAssetPath = Settings->DefaultIconFontAssetPath;
				SavedMissingIconFontPlaceholder = Settings->MissingIconFontPlaceholder;

				Settings->IconFontAssetMap.Empty();
				Settings->DefaultIconFontAssetPath = InDefaultIconFontAssetPath;
				Settings->MissingIconFontPlaceholder = InMissingIconFontPlaceholder;
			}
		}

		~FIconFontSettingsOverrideScope()
		{
			if (Settings)
			{
				Settings->IconFontAssetMap = SavedIconFontAssetMap;
				Settings->DefaultIconFontAssetPath = SavedDefaultIconFontAssetPath;
				Settings->MissingIconFontPlaceholder = SavedMissingIconFontPlaceholder;
			}
		}

		UDesign2UMGSettings* Settings = nullptr;
		TMap<FString, TSoftObjectPtr<UFont>> SavedIconFontAssetMap;
		FString SavedDefaultIconFontAssetPath;
		FString SavedMissingIconFontPlaceholder;
	};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignNodeSemanticsSkipRenderableParityTest,
	"Design2UMG.DesignNodeSemantics.SkipRenderableParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignNodeSemanticsSkipRenderableParityTest::RunTest(const FString& Parameters)
{
	return RunDesignNodeSemanticsSkipParityChecks(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignNodeSemanticsWidthHintParityTest,
	"Design2UMG.DesignNodeSemantics.WidthHintParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignNodeSemanticsWidthHintParityTest::RunTest(const FString& Parameters)
{
	return RunDesignNodeSemanticsWidthHintChecks(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSourceTextBoxAuthorityTest,
	"Design2UMG.SourceTextBox.Authority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSourceTextBoxAuthorityTest::RunTest(const FString& Parameters)
{
	return RunSourceTextBoxAuthorityChecks(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSizeEstimatorSourceFlagsDoNotLockFitGrowthTest,
	"Design2UMG.Unit.SizeEstimator.SourceFlagsDoNotLockFitGrowth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSizeEstimatorSourceFlagsDoNotLockFitGrowthTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> SourceFlaggedNode = MakeSourceTextBoxProbeNode(
		TEXT("source_flagged_fit_icon"),
		EDesignNodeType::IconFont,
		FDesignSize::Fit(4.0f),
		FDesignSize::Fit(4.0f));
	SourceFlaggedNode->Text.FontSize = 32.0f;

	TSharedPtr<FDesignNode> UnflaggedNode = MakeShared<FDesignNode>(*SourceFlaggedNode);
	UnflaggedNode->Id = TEXT("unflagged_fit_icon");
	UnflaggedNode->Transform.bSourceHasWidth = false;
	UnflaggedNode->Transform.bSourceHasHeight = false;

	FDesignDocument EmptyDocument;
	const auto ExpandRefFromDoc = [&EmptyDocument](const TSharedPtr<FDesignNode>& RefNode)
	{
		return RefExpander::ExpandRef(RefNode, EmptyDocument);
	};
	const FVector2D SourceFlaggedSize = SizeEstimator::PromoteFitContentSizes(SourceFlaggedNode, EmptyDocument, ExpandRefFromDoc);
	const FVector2D UnflaggedSize = SizeEstimator::PromoteFitContentSizes(UnflaggedNode, EmptyDocument, ExpandRefFromDoc);
	TestEqual(TEXT("Source-flagged fit width grows to content"), SourceFlaggedSize.X, 32.0);
	TestEqual(TEXT("Source-flagged fit height grows to content"), SourceFlaggedSize.Y, 32.0);
	TestTrue(TEXT("Source flags do not change fit-content growth"), SourceFlaggedSize.Equals(UnflaggedSize));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSourceFillWidthPreResolutionKeepsSourceSignalTest,
	"Design2UMG.DesignNodeSemantics.SourceFillWidthPreResolutionKeepsSourceSignal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSourceFillWidthPreResolutionKeepsSourceSignalTest::RunTest(const FString& Parameters)
{
	return RunFlowFillSourceConstraintChecks(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlowRefExpansionSnapshotReuseTest,
	"Design2UMG.Converter.FlowRefExpansionSnapshotReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFlowRefExpansionSnapshotReuseTest::RunTest(const FString& Parameters)
{
	return RunFlowRefExpansionSnapshotChecks(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignNodeSemanticsVisibleFillOwnerBoundaryTest,
	"Design2UMG.DesignNodeSemantics.VisibleFillOwnerBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignNodeSemanticsVisibleFillOwnerBoundaryTest::RunTest(const FString& Parameters)
{
	FDesignFill VisibleFill = FDesignFill::FromColor(TEXT("#2288CC"));
	TestTrue(TEXT("Shared visible-fill helper accepts enabled color fill"), Design2UMGCore::DesignNodeSemantics::HasVisibleFill(VisibleFill));

	FDesignFill DisabledFill = FDesignFill::FromColor(TEXT("#2288CC"));
	DisabledFill.bEnabled = false;
	TestFalse(TEXT("Shared visible-fill helper rejects disabled color fill"), Design2UMGCore::DesignNodeSemantics::HasVisibleFill(DisabledFill));

	FDesignFill EmptyFill;
	TestFalse(TEXT("Shared visible-fill helper rejects empty fill"), Design2UMGCore::DesignNodeSemantics::HasVisibleFill(EmptyFill));

	TSharedPtr<FDesignNode> PrimaryVisibleNode = Design2UMGCoreTests::MakeNode(TEXT("primary_visible_fill"), EDesignNodeType::Rectangle);
	PrimaryVisibleNode->Visual.Fills.Add(VisibleFill);
	TestTrue(TEXT("Shared semantics keeps primary visible fill renderable"), Design2UMGCore::DesignNodeSemantics::HasRenderableVisual(PrimaryVisibleNode));
	TestTrue(TEXT("Converter visual support still sees primary visible fill"), Design2UMGConverterVisualSupport::HasVisualFill(PrimaryVisibleNode));

	TSharedPtr<FDesignNode> SecondaryPresenceNode = Design2UMGCoreTests::MakeNode(TEXT("secondary_fill_presence"), EDesignNodeType::Rectangle);
	SecondaryPresenceNode->Visual.Fills.Add(EmptyFill);
	SecondaryPresenceNode->Visual.Fills.Add(DisabledFill);
	TestFalse(TEXT("Shared semantics does not treat disabled secondary fill as renderable"), Design2UMGCore::DesignNodeSemantics::HasRenderableVisual(SecondaryPresenceNode));
	TestTrue(TEXT("Converter visual support still treats secondary fill presence as visual fill"), Design2UMGConverterVisualSupport::HasVisualFill(SecondaryPresenceNode));

	TSharedPtr<FDesignNode> EmptyNode = Design2UMGCoreTests::MakeNode(TEXT("empty_fill_read"), EDesignNodeType::Frame);
	const FDesignFill& ReadFill = EmptyNode->Visual.TopmostFill();
	TestEqual(TEXT("Reading an absent primary fill returns the empty value"), ReadFill.Type, EDesignFillType::None);
	TestEqual(TEXT("Reading an absent primary fill does not create fill state"), EmptyNode->Visual.Fills.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedSourceParserSemanticParityTest,
	"Design2UMG.DesignNodeSemantics.SourceParserParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedSourceParserSemanticParityTest::RunTest(const FString& Parameters)
{
	const FString FigmaJson = Design2UMGCoreTests::MakeFigmaDocJson(Design2UMGCoreTests::MakeContainerNode(
		TEXT("figma-root"), TEXT("Root"), TEXT("FRAME"), 0, 0, 160, 48,
		Design2UMGCoreTests::MakeLeafNode(
			TEXT("figma-clear"), TEXT("Clear"), TEXT("RECTANGLE"), 8, 4, 24, 24, TEXT(",\"fills\":[]")),
		FString::Printf(
			TEXT(",\"layoutMode\":\"HORIZONTAL\",\"itemSpacing\":6,\"paddingTop\":4,\"paddingRight\":8,"
				 "\"paddingBottom\":4,\"paddingLeft\":8,\"counterAxisAlignItems\":\"CENTER\",\"fills\":[%s]"),
			*Design2UMGCoreTests::MakeSolidFillJson(1, 1, 1))));

	const FString PenJson = TEXT(R"JSON(
{
  "version": "1.0",
  "children": [{
    "id": "pen-root",
    "name": "Root",
    "type": "frame",
    "width": 160,
    "height": 48,
    "layout": "horizontal",
    "gap": 6,
    "padding": [4, 8],
    "alignItems": "center",
    "fill": "#ffffff",
    "children": [{
      "id": "pen-clear",
      "name": "Clear",
      "type": "rectangle",
      "width": 24,
      "height": 24,
      "fill": []
    }]
  }]
})JSON");

	FDesignDocument FigmaDocument;
	FDesignDocument PenDocument;
	TestTrue(TEXT("Figma parity fixture parses"), FFigmaParser::ParseDocument(FigmaJson, FigmaDocument));
	TestTrue(TEXT("Pencil parity fixture parses"), FPenParser::ParseDocument(PenJson, PenDocument));
	if (FigmaDocument.Children.Num() != 1 || PenDocument.Children.Num() != 1
		|| !FigmaDocument.Children[0].IsValid() || !PenDocument.Children[0].IsValid())
	{
		return false;
	}

	const FDesignNode& FigmaRoot = *FigmaDocument.Children[0];
	const FDesignNode& PenRoot = *PenDocument.Children[0];
	TestEqual(TEXT("Both parsers normalize horizontal layout"), FigmaRoot.LayoutData.Layout, PenRoot.LayoutData.Layout);
	TestEqual(TEXT("Both parsers normalize the main-axis gap"), FigmaRoot.LayoutData.ExplicitGap.Get(0.0f), PenRoot.LayoutData.ExplicitGap.Get(0.0f));
	TestEqual(TEXT("Both parsers normalize counter-axis alignment"), FigmaRoot.LayoutData.AlignItems, PenRoot.LayoutData.AlignItems);
	TestTrue(TEXT("Figma padding is present"), FigmaRoot.LayoutData.ExplicitPadding.IsSet());
	TestTrue(TEXT("Pencil padding is present"), PenRoot.LayoutData.ExplicitPadding.IsSet());
	if (FigmaRoot.LayoutData.ExplicitPadding.IsSet() && PenRoot.LayoutData.ExplicitPadding.IsSet())
	{
		const FDesignPadding& FigmaPadding = FigmaRoot.LayoutData.ExplicitPadding.GetValue();
		const FDesignPadding& PenPadding = PenRoot.LayoutData.ExplicitPadding.GetValue();
		TestEqual(TEXT("Padding top matches"), FigmaPadding.Top, PenPadding.Top);
		TestEqual(TEXT("Padding right matches"), FigmaPadding.Right, PenPadding.Right);
		TestEqual(TEXT("Padding bottom matches"), FigmaPadding.Bottom, PenPadding.Bottom);
		TestEqual(TEXT("Padding left matches"), FigmaPadding.Left, PenPadding.Left);
	}
	TestEqual(TEXT("Both parsers make the padding ownership decision"), FigmaRoot.LayoutData.PaddingRequiresRuntimeWrap.IsSet(), PenRoot.LayoutData.PaddingRequiresRuntimeWrap.IsSet());
	if (FigmaRoot.LayoutData.PaddingRequiresRuntimeWrap.IsSet() && PenRoot.LayoutData.PaddingRequiresRuntimeWrap.IsSet())
	{
		TestEqual(TEXT("Padding ownership decision matches"), FigmaRoot.LayoutData.PaddingRequiresRuntimeWrap.GetValue(), PenRoot.LayoutData.PaddingRequiresRuntimeWrap.GetValue());
	}
	TestEqual(TEXT("Both parsers produce one color fill"), FigmaRoot.Visual.Fills.Num(), PenRoot.Visual.Fills.Num());
	TestEqual(TEXT("Color fill type matches"), FigmaRoot.Visual.TopmostFill().Type, PenRoot.Visual.TopmostFill().Type);
	TestEqual(TEXT("Color fill value matches"), FigmaRoot.Visual.TopmostFill().SolidColor, PenRoot.Visual.TopmostFill().SolidColor);

	TestEqual(TEXT("Both parsers preserve one clear child"), FigmaRoot.Children.Num(), PenRoot.Children.Num());
	if (FigmaRoot.Children.Num() == 1 && PenRoot.Children.Num() == 1
		&& FigmaRoot.Children[0].IsValid() && PenRoot.Children[0].IsValid())
	{
		TestTrue(TEXT("Figma clear child is explicit"), FigmaRoot.Children[0]->Visual.bFillExplicitClear);
		TestTrue(TEXT("Pencil clear child is explicit"), PenRoot.Children[0]->Visual.bFillExplicitClear);
		TestEqual(TEXT("Clear child has no fill layers in either parser"), FigmaRoot.Children[0]->Visual.Fills.Num(), PenRoot.Children[0]->Visual.Fills.Num());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedColorLiteralNormalizationTest,
	"Design2UMG.DesignNodeSemantics.ColorLiteralNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedColorLiteralNormalizationTest::RunTest(const FString& Parameters)
{
	const FString PenJson = TEXT(R"JSON(
{
  "version": "1.0",
  "children": [
    {"id": "short", "type": "rectangle", "fill": "#abc"},
    {"id": "alpha", "type": "rectangle", "fill": "#102030aa"},
    {"id": "invalid", "type": "rectangle", "fill": "#xyz"}
  ]
})JSON");

	FDesignDocument Document;
	TestTrue(TEXT("Color literal fixture parses"), FPenParser::ParseDocument(PenJson, Document));
	TestEqual(TEXT("Three color literal nodes parse"), Document.Children.Num(), 3);
	if (Document.Children.Num() != 3)
	{
		return false;
	}

	TestEqual(TEXT("Three-digit color expands to uppercase RGB"),
		Document.Children[0]->Visual.TopmostFill().SolidColor,
		FString(TEXT("#AABBCC")));
	TestEqual(TEXT("Eight-digit color preserves alpha and normalizes case"),
		Document.Children[1]->Visual.TopmostFill().SolidColor,
		FString(TEXT("#102030AA")));
	TestEqual(TEXT("Invalid color literal stays untouched"),
		Document.Children[2]->Visual.TopmostFill().SolidColor,
		FString(TEXT("#xyz")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImageFillTransformModeIndependenceTest,
	"Design2UMG.DesignNodeSemantics.ImageFillTransformModeIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImageFillTransformModeIndependenceTest::RunTest(const FString& Parameters)
{
	FDesignImageFill TransformedStretch;
	TransformedStretch.Mode = EDesignImageMode::Stretch;
	TransformedStretch.Transform.A = 0.5f;
	TransformedStretch.Transform.TX = 0.25f;
	TransformedStretch.Transform.D = 0.75f;
	TransformedStretch.Transform.TY = 0.1f;

	FUMGWidgetNode TransformedWidget;
	ImageFillUtils::ApplyImageFillProperties(TransformedWidget, TransformedStretch);
	TestTrue(TEXT("Non-identity image transform selects executable crop mode"),
		TransformedWidget.Brush.Image.Mode.IsSet()
		&& TransformedWidget.Brush.Image.Mode.GetValue() == TEXT("Crop"));
	TestTrue(TEXT("Non-identity image transform is preserved independent of source scale mode"),
		TransformedWidget.Brush.Image.Transform.IsSet()
		&& FMath::IsNearlyEqual(TransformedWidget.Brush.Image.Transform.GetValue().A, 0.5f)
		&& FMath::IsNearlyEqual(TransformedWidget.Brush.Image.Transform.GetValue().TX, 0.25f)
		&& FMath::IsNearlyEqual(TransformedWidget.Brush.Image.Transform.GetValue().D, 0.75f)
		&& FMath::IsNearlyEqual(TransformedWidget.Brush.Image.Transform.GetValue().TY, 0.1f));

	FDesignImageFill PlainStretch;
	PlainStretch.Mode = EDesignImageMode::Stretch;
	FUMGWidgetNode PlainWidget;
	ImageFillUtils::ApplyImageFillProperties(PlainWidget, PlainStretch);
	TestFalse(TEXT("Identity stretch does not synthesize crop mode"), PlainWidget.Brush.Image.Mode.IsSet());
	TestFalse(TEXT("Identity stretch does not synthesize image transform"), PlainWidget.Brush.Image.Transform.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNodeWidgetFactoryGeometryFallbackDoesNotMutateInputTest,
	"Design2UMG.NodeWidgetFactory.GeometryFallbackDoesNotMutateInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FNodeWidgetFactoryGeometryFallbackDoesNotMutateInputTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> EllipseNode = Design2UMGCoreTests::MakeNode(TEXT("ellipse_fallback"), EDesignNodeType::Ellipse, FDesignSize::Fixed(96.0f), FDesignSize::Fixed(48.0f));
	EllipseNode->Transform.SetPosition(12.0f, 34.0f);
	EllipseNode->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#FFAA44")));
	const FDesignTransformNodeData EllipseTransformBefore = EllipseNode->Transform;

	const Design2UMGNodeWidgetFactory::FBuildResult EllipseResult = Design2UMGNodeWidgetFactory::CreateWidgetForNodeType(EllipseNode);
	TestEqual(TEXT("Ellipse input geometry remains untouched"), EllipseNode->Shape.Geometry, FString());
	TestEqual(TEXT("Ellipse input X remains untouched"), EllipseNode->Transform.X, EllipseTransformBefore.X);
	TestEqual(TEXT("Ellipse input Y remains untouched"), EllipseNode->Transform.Y, EllipseTransformBefore.Y);
	TestTrue(TEXT("Ellipse fallback still builds widget geometry"), EllipseResult.Widget.Path.Geometry.IsSet() && !EllipseResult.Widget.Path.Geometry.GetValue().IsEmpty());

	TSharedPtr<FDesignNode> RasterizedEllipseNode = Design2UMGCoreTests::MakeNode(TEXT("ellipse_raster"), EDesignNodeType::Ellipse, FDesignSize::Fixed(96.0f), FDesignSize::Fixed(48.0f));
	FDesignFill& RasterizedEllipseFill = RasterizedEllipseNode->Visual.Fills.AddDefaulted_GetRef();
	RasterizedEllipseFill.Type = EDesignFillType::Image;
	RasterizedEllipseFill.Image.Url = TEXT("/Game/Textures/T_EllipseCrop.T_EllipseCrop");
	RasterizedEllipseNode->Visual.VectorRasterFallbackImageRef = TEXT("/Game/Textures/T_EllipseCrop.T_EllipseCrop");
	const Design2UMGNodeWidgetFactory::FBuildResult RasterizedEllipseResult =
		Design2UMGNodeWidgetFactory::CreateWidgetForNodeType(RasterizedEllipseNode);
	TestEqual(TEXT("Full pre-rasterized ellipse keeps its ellipse mask"), RasterizedEllipseResult.Widget.Type, TEXT("Border"));

	RasterizedEllipseNode->Visual.bRenderBoundsClippedByParent = true;
	const Design2UMGNodeWidgetFactory::FBuildResult ClippedRasterizedEllipseResult =
		Design2UMGNodeWidgetFactory::CreateWidgetForNodeType(RasterizedEllipseNode);
	TestEqual(TEXT("Parent-clipped rasterized ellipse does not add another ellipse mask"), ClippedRasterizedEllipseResult.Widget.Type, TEXT("Image"));
	TestTrue(TEXT("Parent-clipped rasterized ellipse keeps its texture"),
		ClippedRasterizedEllipseResult.Widget.Brush.Image.TexturePath.IsSet()
		&& ClippedRasterizedEllipseResult.Widget.Brush.Image.TexturePath.GetValue() == TEXT("/Game/Textures/T_EllipseCrop.T_EllipseCrop"));

	TSharedPtr<FDesignNode> LineNode = Design2UMGCoreTests::MakeNode(TEXT("line_fallback"), EDesignNodeType::Line, FDesignSize::Fixed(120.0f), FDesignSize::Fixed(6.0f));
	LineNode->Transform.SetPosition(6.0f, 18.0f);
	LineNode->Visual.Stroke.Thickness.Uniform = 2.0f;
	LineNode->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#FFFFFFFF"));
	LineNode->Visual.Stroke.Cap = EDesignStrokeCap::Butt;
	LineNode->Visual.Stroke.Join = EDesignStrokeJoin::Miter;
	const FDesignTransformNodeData LineTransformBefore = LineNode->Transform;

	const Design2UMGNodeWidgetFactory::FBuildResult LineResult = Design2UMGNodeWidgetFactory::CreateWidgetForNodeType(LineNode);
	TestEqual(TEXT("Line input geometry remains untouched"), LineNode->Shape.Geometry, FString());
	TestEqual(TEXT("Line input X remains untouched"), LineNode->Transform.X, LineTransformBefore.X);
	TestEqual(TEXT("Line input Y remains untouched"), LineNode->Transform.Y, LineTransformBefore.Y);
	TestTrue(TEXT("Line fallback still builds widget geometry"), LineResult.Widget.Path.Geometry.IsSet() && !LineResult.Widget.Path.Geometry.GetValue().IsEmpty());
	TestTrue(TEXT("Missing line stroke cap is emitted"), LineResult.Widget.Path.StrokeCap.IsSet());
	TestTrue(TEXT("Missing line stroke join is emitted"), LineResult.Widget.Path.StrokeJoin.IsSet());
	if (LineResult.Widget.Path.StrokeCap.IsSet())
	{
		TestEqual(TEXT("Missing line stroke cap uses the schema default"), LineResult.Widget.Path.StrokeCap.GetValue(), FString(TEXT("butt")));
	}
	if (LineResult.Widget.Path.StrokeJoin.IsSet())
	{
		TestEqual(TEXT("Missing line stroke join uses the schema default"), LineResult.Widget.Path.StrokeJoin.GetValue(), FString(TEXT("miter")));
	}

	TSharedPtr<FDesignNode> DashedVectorNode = Design2UMGCoreTests::MakeNode(
		TEXT("dashed_vector_fallback"),
		EDesignNodeType::Path,
		FDesignSize::Fixed(27.2578f),
		FDesignSize::Fixed(322.1843f));
	DashedVectorNode->Visual.Stroke.Thickness.Uniform = 5.11084f;
	DashedVectorNode->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#CDE7FFFF"));
	DashedVectorNode->Visual.Stroke.DashPattern = {21.173479f, 21.173479f};
	DashedVectorNode->Visual.Stroke.Cap = EDesignStrokeCap::Round;
	DashedVectorNode->Transform.LayoutBoundsSize = FVector2D(0.000027f, 306.0f);

	const Design2UMGNodeWidgetFactory::FBuildResult DashedVectorResult =
		Design2UMGNodeWidgetFactory::CreateWidgetForNodeType(DashedVectorNode);
	TestEqual(TEXT("Degenerate dashed VECTOR becomes a rasterized path image"), DashedVectorResult.Widget.Type, TEXT("Image"));
	TestTrue(TEXT("Degenerate dashed VECTOR receives generated capsule geometry"),
		DashedVectorResult.Widget.Path.Geometry.IsSet()
		&& DashedVectorResult.Widget.Path.Geometry.GetValue().Contains(TEXT(" A ")));
	TestTrue(TEXT("Dashed VECTOR fallback does not mutate source geometry"), DashedVectorNode->Shape.Geometry.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignNodeSemanticsExecutableGuardReplacementTest,
	"Design2UMG.DesignNodeSemantics.ExecutableGuardReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignNodeSemanticsExecutableGuardReplacementTest::RunTest(const FString& Parameters)
{
	return RunDesignNodeSemanticsExecutableReplacementChecks(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPublicSurfacePostReviewFixesGuardTest,
	"Design2UMG.PublicSurface.PostReviewFixesGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPublicSurfacePostReviewFixesGuardTest::RunTest(const FString& Parameters)
{
	FDesignLayoutNodeData LayoutData;
	LayoutData.LayoutRow = 3;
	LayoutData.LayoutColumn = 1;
	LayoutData.LayoutRowSpan = 2;
	LayoutData.LayoutColumnGap = 16.0f;
	LayoutData.LayoutChildHorizontalAlign = EDesignAxisAlignment::Center;

	TestTrue(TEXT("Public grid row flag still drives explicit grid position"), LayoutData.LayoutRow.IsSet() || LayoutData.LayoutColumn.IsSet());
	TestTrue(TEXT("Direct optional row storage is set"), LayoutData.LayoutRow.IsSet());
	TestEqual(TEXT("Direct optional returns public row value"), LayoutData.LayoutRow.Get(-1), 3);
	TestEqual(TEXT("Direct optional returns public column value"), LayoutData.LayoutColumn.Get(-1), 1);
	TestEqual(TEXT("Direct optional returns row span"), LayoutData.LayoutRowSpan.Get(1), 2);
	TestEqual(TEXT("Direct optional returns column gap"), LayoutData.LayoutColumnGap.Get(0.0f), 16.0f);
	TestEqual(
		TEXT("Direct optional returns child horizontal alignment"),
		LayoutData.LayoutChildHorizontalAlign.Get(EDesignAxisAlignment::Start),
		EDesignAxisAlignment::Center);

	LayoutData.LayoutRow.Reset();
	TestFalse(TEXT("Direct optional reset clears public row presence"), LayoutData.LayoutRow.IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConversionPlaceholderTintSettingsPayloadTest,
	"Design2UMG.Converter.PlaceholderTintSettingsPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConversionPlaceholderTintSettingsPayloadTest::RunTest(const FString& Parameters)
{
	FPlaceholderHatchOverrideScope PlaceholderScope(TEXT("#123456"), TEXT("#ABCDEF"), 7, 11);
	if (!TestNotNull(TEXT("Settings available for placeholder payload test"), PlaceholderScope.Settings))
	{
		return false;
	}

	FDesignDocument Doc;
	TSharedPtr<FDesignNode> PlaceholderNode = Design2UMGCoreTests::MakeNode(
		TEXT("placeholder_payload_root"),
		EDesignNodeType::Frame,
		FDesignSize::Fixed(120.0f),
		FDesignSize::Fixed(48.0f));
	PlaceholderNode->LayoutData.bSemanticPlaceholderSurface = true;
	Doc.Children.Add(PlaceholderNode);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(
		Doc,
		FDesign2UMGConversionSettings::FromSettings(PlaceholderScope.Settings));
	const FUMGWidgetNode* PlaceholderWidget = FindWidgetByIdRecursive(Data.RootWidget, TEXT("placeholder_payload_root"));
	if (!TestNotNull(TEXT("Converted placeholder widget exists"), PlaceholderWidget))
	{
		return false;
	}

	TestEqual(TEXT("Placeholder widget uses direct image material"), PlaceholderWidget->Type, FString(TEXT("Image")));
	TestTrue(TEXT("Placeholder widget uses placeholder surface material ref"),
		PlaceholderWidget->Brush.MaterialRef.IsSet()
		&& PlaceholderWidget->Brush.MaterialRef.GetValue() == TEXT("placeholdersurface"));
	TestTrue(TEXT("Placeholder material params use injected base fill color"), PlaceholderWidget->Brush.MaterialParams.IsSet() && PlaceholderWidget->Brush.MaterialParams.GetValue().Contains(TEXT("\"Color\":\"#123456\"")));
	TestTrue(TEXT("Placeholder material params use injected base stripe color"), PlaceholderWidget->Brush.MaterialParams.IsSet() && PlaceholderWidget->Brush.MaterialParams.GetValue().Contains(TEXT("\"StripeColor\":\"#ABCDEF\"")));
	TestTrue(TEXT("Placeholder material params use injected stripe width"), PlaceholderWidget->Brush.MaterialParams.IsSet() && PlaceholderWidget->Brush.MaterialParams.GetValue().Contains(TEXT("\"StripeWidth\":7")));
	TestTrue(TEXT("Placeholder material params use injected stripe gap"), PlaceholderWidget->Brush.MaterialParams.IsSet() && PlaceholderWidget->Brush.MaterialParams.GetValue().Contains(TEXT("\"StripeGap\":11")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConversionLuminosityOpacitySettingsPayloadTest,
	"Design2UMG.Converter.LuminosityOpacitySettingsPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConversionLuminosityOpacitySettingsPayloadTest::RunTest(const FString& Parameters)
{
	FLuminosityOpacityCapOverrideScope OpacityScope(0.2f);
	if (!TestNotNull(TEXT("Settings available for luminosity payload test"), OpacityScope.Settings))
	{
		return false;
	}

	FDesignFill SolidFill = FDesignFill::FromColor(TEXT("#F5F5F5"));

	FDesignFill ImageFill;
	ImageFill.bEnabled = true;
	ImageFill.Type = EDesignFillType::Image;
	ImageFill.BlendMode = TEXT("LUMINOSITY");
	ImageFill.Image.Url = TEXT("/Game/Design2UMG/Tests/T_LuminosityPayload");
	ImageFill.Image.Mode = EDesignImageMode::Fit;
	ImageFill.Image.Opacity = 0.6f;

	FDesignDocument Doc;
	TSharedPtr<FDesignNode> CardNode = Design2UMGCoreTests::MakeNode(
		TEXT("luminosity_payload_root"),
		EDesignNodeType::Frame,
		FDesignSize::Fixed(160.0f),
		FDesignSize::Fixed(96.0f));
	CardNode->Visual.Fills = { SolidFill, ImageFill };
	Doc.Children.Add(CardNode);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(
		Doc,
		FDesign2UMGConversionSettings::FromSettings(OpacityScope.Settings));
	const FUMGWidgetNode* OverlayImage = FindWidgetByIdRecursive(Data.RootWidget, TEXT("luminosity_payload_root_fill1_img"));
	if (!TestNotNull(TEXT("Multi-fill luminosity image child exists"), OverlayImage))
	{
		return false;
	}

	TestTrue(TEXT("Luminosity image child exported render opacity"), OverlayImage->Render.RenderOpacity.IsSet());
	TestTrue(TEXT("Luminosity image child clamps opacity using injected cap"), FMath::IsNearlyEqual(OverlayImage->Render.RenderOpacity.GetValue(), 0.2f, KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderSharedPathSettingsBoundaryTest,
	"Design2UMG.TextWidgetBuilder.SharedPathSettingsBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderSharedPathSettingsBoundaryTest::RunTest(const FString& Parameters)
{
	FIconFontSettingsOverrideScope SettingsScope(TEXT(""), TEXT("[global-placeholder]"));
	if (!TestNotNull(TEXT("Settings available for icon-font boundary test"), SettingsScope.Settings))
	{
		return false;
	}

	Design2UMGTextWidgetBuilder::FIconFontSettingsSnapshot Snapshot;
	Snapshot.MissingIconFontPlaceholder = TEXT("[snapshot-placeholder]");

	TSharedPtr<FDesignNode> Node = Design2UMGCoreTests::MakeNode(TEXT("icon_font_boundary"), EDesignNodeType::IconFont, FDesignSize::Fixed(24.0f), FDesignSize::Fixed(24.0f));
	Node->Text.IconFontFamily = TEXT("MissingFamily_BatchA");
	Node->Text.IconFontName = TEXT("dashboard");
	Node->Text.FontSize = 32.0f;

	Design2UMGTextWidgetBuilder::FIconFontBuildArgs Args;
	Args.SettingsSnapshot = &Snapshot;

	const Design2UMGTextWidgetBuilder::FIconFontBuildResult Result =
		Design2UMGTextWidgetBuilder::CreateIconFontWidget(Node, Args);
	TestEqual(TEXT("Shared-path icon builder should consume the injected snapshot placeholder"),
		Result.Widget.Text.Text.GetValue(),
		FString(TEXT("[snapshot-placeholder]")));
	TestFalse(TEXT("Injected snapshot should not silently fall back to the global placeholder"),
		Result.Widget.Text.Text.IsSet() && Result.Widget.Text.Text.GetValue().Equals(TEXT("[global-placeholder]"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Icon builder converts 32 source pixels to 24 Slate points"),
		FMath::IsNearlyEqual(Result.Widget.TextStyle.FontSize, 24.0f, 0.001f));
	TestTrue(TEXT("Icon builder preserves the authored source pixel size"),
		FMath::IsNearlyEqual(Result.Widget.TextStyle.SourceFontSize, 32.0f, 0.001f));

	const Design2UMGTextWidgetBuilder::FIconFontBuildResult MissingSnapshotResult =
		Design2UMGTextWidgetBuilder::CreateIconFontWidget(Node);
	TestTrue(TEXT("Missing snapshot should now fail explicitly"), MissingSnapshotResult.bMissingSettingsSnapshot);
	TestTrue(TEXT("Missing snapshot path should not silently build a text widget"), MissingSnapshotResult.Widget.Type.IsEmpty());
	TestTrue(TEXT("Missing snapshot path should emit a boundary warning"),
		MissingSnapshotResult.Warnings.Num() == 1
		&& MissingSnapshotResult.Warnings[0].Contains(TEXT("requires an explicit settings snapshot")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConverterChildPreparationOwnerBoundaryTest,
	"Design2UMG.Converter.ChildPreparationOwnerBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConverterChildPreparationOwnerBoundaryTest::RunTest(const FString& Parameters)
{
	return RunConverterOwnerBoundaryChecks(*this);
}

#endif
