// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "Design2UMGConverter.h"
#include "Design2UMGNodeWidgetFactory.h"
#include "Design2UMGSettings.h"
#include "Design2UMGTextWidgetBuilder.h"
#include "SharedCoreSemantics.h"
#include "SizeEstimator.h"
#include "TextMeasurement.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGWidgetNodeAccess.h"

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

	TSharedPtr<FDesignNode> MakeWidthHintProbeTextNode(const FString& Id, const FDesignSize& Width)
	{
		TSharedPtr<FDesignNode> Node = MakeSharedSemanticsNode(Id, EDesignNodeType::Text, Width, FDesignSize::Fixed(36.0f));
		Node->Text.Content = TEXT("Width hint should stay consistent between sizing and wrap logic.");
		Node->Text.FontFamily = TEXT("Roboto");
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = 16.0f;
		Node->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
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

	struct FSharedSemanticsVisibilityFixture
	{
		TSharedPtr<FDesignNode> Root;
		TSharedPtr<FDesignNode> DisabledLeaf;
		TSharedPtr<FDesignNode> DesignerOnlyLeaf;
		TSharedPtr<FDesignNode> NonRenderableLeaf;
		TSharedPtr<FDesignNode> FlowContainerLeaf;
		TSharedPtr<FDesignNode> RenderableLeaf;
		TSharedPtr<FDesignNode> TextLeaf;
	};

	FSharedSemanticsVisibilityFixture MakeSharedSemanticsVisibilityFixture()
	{
		FSharedSemanticsVisibilityFixture Fixture;
		Fixture.Root = MakeSharedSemanticsNode(TEXT("shared_semantics_root"), EDesignNodeType::Frame, FDesignSize::Fixed(640.0f), FDesignSize::Fixed(360.0f));
		Fixture.Root->LayoutData.Layout = EDesignLayout::Vertical;

		Fixture.DisabledLeaf = MakeSharedSemanticsNode(TEXT("skip_disabled"), EDesignNodeType::Rectangle);
		Fixture.DisabledLeaf->Transform.bEnabled = false;

		Fixture.DesignerOnlyLeaf = MakeSharedSemanticsNode(TEXT("skip_designer_only"), EDesignNodeType::Connection);
		Fixture.NonRenderableLeaf = MakeSharedSemanticsNode(TEXT("skip_non_renderable"), EDesignNodeType::Rectangle);

		Fixture.FlowContainerLeaf = MakeSharedSemanticsNode(TEXT("keep_flow_container"), EDesignNodeType::Frame);
		Fixture.FlowContainerLeaf->LayoutData.Layout = EDesignLayout::Horizontal;

		Fixture.RenderableLeaf = MakeSharedSemanticsNode(TEXT("keep_renderable"), EDesignNodeType::Rectangle);
		Fixture.RenderableLeaf->Visual.Fill = FDesignFill::FromColor(TEXT("#2288CC"));

		Fixture.TextLeaf = MakeSharedSemanticsNode(TEXT("keep_text"), EDesignNodeType::Text, FDesignSize::Fixed(220.0f), FDesignSize::Fixed(36.0f));
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

	bool RunSharedSemanticsSkipParityChecks(FAutomationTestBase& Test)
	{
		bool bAllPassed = true;
		const FSharedSemanticsVisibilityFixture Fixture = MakeSharedSemanticsVisibilityFixture();

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
				Design2UMGCore::SharedSemantics::ShouldSkipNode(Expectation.Node),
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

	bool RunSharedSemanticsWidthHintChecks(FAutomationTestBase& Test)
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
			const float CanonicalWidth = Design2UMGCore::SharedSemantics::ResolveWidthHint(ProbeCase.Node, ParentContentWidth);
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

	bool RunSharedSemanticsExecutableReplacementChecks(FAutomationTestBase& Test)
	{
		bool bAllPassed = true;
		bAllPassed &= RunSharedSemanticsSkipParityChecks(Test);
		bAllPassed &= RunSharedSemanticsWidthHintChecks(Test);
		return bAllPassed;
	}

	bool RunConverterOwnerBoundaryChecks(FAutomationTestBase& Test)
	{
		FDesignDocument Doc;

		TSharedPtr<FDesignNode> ReusableCard = MakeSharedSemanticsNode(
			TEXT("reusable_card"),
			EDesignNodeType::Rectangle,
			FDesignSize::Fixed(60.0f),
			FDesignSize::Fixed(20.0f));
		ReusableCard->bReusable = true;
		ReusableCard->Visual.Fill = FDesignFill::FromColor(TEXT("#AA3344"));
		Doc.Children.Add(ReusableCard);

		TSharedPtr<FDesignNode> Screen = MakeSharedSemanticsNode(
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

		TSharedPtr<FDesignNode> FillChild = MakeSharedSemanticsNode(
			TEXT("fill_sibling"),
			EDesignNodeType::Rectangle,
			FDesignSize::Fill(0.0f),
			FDesignSize::Fill(0.0f));
		FillChild->Visual.Fill = FDesignFill::FromColor(TEXT("#33AA44"));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedSemanticsSkipRenderableParityTest,
	"Design2UMG.SharedSemantics.SkipRenderableParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedSemanticsSkipRenderableParityTest::RunTest(const FString& Parameters)
{
	return RunSharedSemanticsSkipParityChecks(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedSemanticsWidthHintParityTest,
	"Design2UMG.SharedSemantics.WidthHintParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedSemanticsWidthHintParityTest::RunTest(const FString& Parameters)
{
	return RunSharedSemanticsWidthHintChecks(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNodeWidgetFactoryGeometryFallbackDoesNotMutateInputTest,
	"Design2UMG.NodeWidgetFactory.GeometryFallbackDoesNotMutateInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FNodeWidgetFactoryGeometryFallbackDoesNotMutateInputTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> EllipseNode = MakeSharedSemanticsNode(TEXT("ellipse_fallback"), EDesignNodeType::Ellipse, FDesignSize::Fixed(96.0f), FDesignSize::Fixed(48.0f));
	EllipseNode->Transform.SetPosition(12.0f, 34.0f);
	EllipseNode->Visual.Fill = FDesignFill::FromColor(TEXT("#FFAA44"));
	const FDesignTransformNodeData EllipseTransformBefore = EllipseNode->Transform;

	const Design2UMGNodeWidgetFactory::FBuildResult EllipseResult = Design2UMGNodeWidgetFactory::CreateWidgetForNodeType(EllipseNode);
	TestEqual(TEXT("Ellipse input geometry remains untouched"), EllipseNode->Shape.Geometry, FString());
	TestEqual(TEXT("Ellipse input X remains untouched"), EllipseNode->Transform.X, EllipseTransformBefore.X);
	TestEqual(TEXT("Ellipse input Y remains untouched"), EllipseNode->Transform.Y, EllipseTransformBefore.Y);
	TestTrue(TEXT("Ellipse fallback still builds widget geometry"), EllipseResult.Widget.Path.Geometry.IsSet() && !EllipseResult.Widget.Path.Geometry.GetValue().IsEmpty());

	TSharedPtr<FDesignNode> LineNode = MakeSharedSemanticsNode(TEXT("line_fallback"), EDesignNodeType::Line, FDesignSize::Fixed(120.0f), FDesignSize::Fixed(6.0f));
	LineNode->Transform.SetPosition(6.0f, 18.0f);
	LineNode->Visual.Stroke.Thickness.Uniform = 2.0f;
	LineNode->Visual.Stroke.Fill = FDesignFill::FromColor(TEXT("#FFFFFFFF"));
	const FDesignTransformNodeData LineTransformBefore = LineNode->Transform;

	const Design2UMGNodeWidgetFactory::FBuildResult LineResult = Design2UMGNodeWidgetFactory::CreateWidgetForNodeType(LineNode);
	TestEqual(TEXT("Line input geometry remains untouched"), LineNode->Shape.Geometry, FString());
	TestEqual(TEXT("Line input X remains untouched"), LineNode->Transform.X, LineTransformBefore.X);
	TestEqual(TEXT("Line input Y remains untouched"), LineNode->Transform.Y, LineTransformBefore.Y);
	TestTrue(TEXT("Line fallback still builds widget geometry"), LineResult.Widget.Path.Geometry.IsSet() && !LineResult.Widget.Path.Geometry.GetValue().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedSemanticsExecutableGuardReplacementTest,
	"Design2UMG.SharedSemantics.ExecutableGuardReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedSemanticsExecutableGuardReplacementTest::RunTest(const FString& Parameters)
{
	return RunSharedSemanticsExecutableReplacementChecks(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedSemanticsStaticDuplicateGuardTest,
	"Design2UMG.SharedSemantics.StaticDuplicateGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSharedSemanticsStaticDuplicateGuardTest::RunTest(const FString& Parameters)
{
	return RunSharedSemanticsExecutableReplacementChecks(*this);
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
	LayoutData.LayoutChildHorizontalAlign = TEXT("CENTER");

	TestTrue(TEXT("Public grid row flag still drives explicit grid position"), LayoutData.HasExplicitGridPosition());
	TestTrue(TEXT("Direct optional row storage is set"), LayoutData.LayoutRow.IsSet());
	TestEqual(TEXT("Direct optional returns public row value"), LayoutData.LayoutRow.Get(-1), 3);
	TestEqual(TEXT("Direct optional returns public column value"), LayoutData.LayoutColumn.Get(-1), 1);
	TestEqual(TEXT("Direct optional returns row span"), LayoutData.LayoutRowSpan.Get(1), 2);
	TestEqual(TEXT("Direct optional returns column gap"), LayoutData.LayoutColumnGap.Get(0.0f), 16.0f);
	TestEqual(
		TEXT("Direct optional returns child horizontal alignment"),
		LayoutData.LayoutChildHorizontalAlign.Get(FString(TEXT("Left"))),
		FString(TEXT("CENTER")));

	LayoutData.LayoutRow.Reset();
	TestFalse(TEXT("Direct optional reset clears public row presence"), LayoutData.LayoutRow.IsSet());

	FUMGWidgetNode Widget;
	Widget.Text.Text = TEXT("");
	Widget.Text.bAutoWrapText = true;
	Widget.Text.Color = FLinearColor(0.2f, 0.4f, 0.6f, 1.0f);
	Widget.WidgetState.ActiveWidgetIndex = 3;
	Widget.ScrollBox.Orientation = TEXT("Horizontal");
	Widget.ScrollBox.ScrollBarVisibility = TEXT("Hidden");

	bool bSawColorKey = false;
	Design2UMGWidgetNodeAccess::ForEachStaticTypedWidgetPropertyKey([&bSawColorKey](const TCHAR* Key)
	{
		if (FCString::Strcmp(Key, UMGPropertyKeys::Color) == 0)
		{
			bSawColorKey = true;
		}
	});
	TestTrue(TEXT("Typed key enumeration still exposes public color key"), bSawColorKey);
	TestTrue(TEXT("Typed key helper recognizes color"), Design2UMGWidgetNodeAccess::IsTypedWidgetPropertyKey(UMGPropertyKeys::Color));

	FString SerializedText;
	TestTrue(TEXT("Typed text helper still returns explicit empty string"), Design2UMGWidgetNodeAccess::TryGetTypedWidgetString(Widget, UMGPropertyKeys::Text, SerializedText));
	TestEqual(TEXT("Typed text helper preserves empty payload"), SerializedText, FString());

	bool bAutoWrap = false;
	TestTrue(TEXT("Bool helper still parses typed bool"), Design2UMGWidgetNodeAccess::TryGetWidgetBool(Widget, UMGPropertyKeys::AutoWrapText, bAutoWrap));
	TestTrue(TEXT("Typed bool payload remains true"), bAutoWrap);

	int32 ActiveWidgetIndex = INDEX_NONE;
	TestTrue(TEXT("Int helper still parses typed int"), Design2UMGWidgetNodeAccess::TryGetWidgetInt(Widget, UMGPropertyKeys::ActiveWidgetIndex, ActiveWidgetIndex));
	TestEqual(TEXT("Typed int payload remains unchanged"), ActiveWidgetIndex, 3);

	FString ScrollBoxOrientation;
	TestTrue(TEXT("Typed helper returns explicit ScrollBox orientation"), Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::Orientation, ScrollBoxOrientation));
	TestEqual(TEXT("Typed ScrollBox orientation remains unchanged"), ScrollBoxOrientation, FString(TEXT("Horizontal")));

	FString ScrollBarVisibility;
	TestTrue(TEXT("Typed helper returns explicit ScrollBox scrollbar visibility"), Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::ScrollBarVisibility, ScrollBarVisibility));
	TestEqual(TEXT("Typed ScrollBox scrollbar visibility remains unchanged"), ScrollBarVisibility, FString(TEXT("Hidden")));

	FUMGWidgetNode UnsetWidget;
	int32 MissingActiveWidgetIndex = INDEX_NONE;
	TestFalse(TEXT("Unset optional widget state stays absent"), Design2UMGWidgetNodeAccess::TryGetWidgetInt(UnsetWidget, UMGPropertyKeys::ActiveWidgetIndex, MissingActiveWidgetIndex));
	FString MissingOrientation;
	TestFalse(TEXT("Unset optional ScrollBox orientation stays absent"), Design2UMGWidgetNodeAccess::TryGetWidgetString(UnsetWidget, UMGPropertyKeys::Orientation, MissingOrientation));

	FLinearColor ParsedColor;
	TestTrue(TEXT("Color helper still parses typed color"), Design2UMGWidgetNodeAccess::TryGetWidgetColor(Widget, UMGPropertyKeys::Color, ParsedColor));
	TestEqual(TEXT("Typed color payload remains unchanged"), ParsedColor.ToFColor(true), Widget.Text.Color.GetValue().ToFColor(true));

	FString TypedText;
	TestTrue(TEXT("Typed string helper returns explicit empty text"), Design2UMGWidgetNodeAccess::TryGetTypedWidgetString(Widget, UMGPropertyKeys::Text, TypedText));
	TestEqual(TEXT("Typed string helper preserves explicit empty string"), TypedText, FString());

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
	TSharedPtr<FDesignNode> PlaceholderNode = MakeSharedSemanticsNode(
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
	TSharedPtr<FDesignNode> CardNode = MakeSharedSemanticsNode(
		TEXT("luminosity_payload_root"),
		EDesignNodeType::Frame,
		FDesignSize::Fixed(160.0f),
		FDesignSize::Fixed(96.0f));
	CardNode->Visual.Fill = SolidFill;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextSharedThresholdOwnerBoundaryTest,
	"Design2UMG.Text.SharedThresholdOwnerBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextSharedThresholdOwnerBoundaryTest::RunTest(const FString& Parameters)
{
	auto MakeSingleLineNode = [](const FString& Id, const float FontSize)
	{
		TSharedPtr<FDesignNode> Node = MakeSharedSemanticsNode(Id, EDesignNodeType::Text, FDesignSize::Fixed(160.0f), FDesignSize::Fixed(36.0f));
		Node->Text.Content = TEXT("Shared owner boundary");
		Node->Text.FontFamily = TEXT("Roboto");
		Node->Text.FontWeight = TEXT("Regular");
		Node->Text.FontSize = FontSize;
		Node->Text.LineHeight = 1.4f;
		Node->Text.TextGrowth = EDesignTextGrowth::Auto;
		Node->Text.TextAlign = EDesignTextAlign::Left;
		Node->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
		Node->Transform.bSourceHasWidth = false;
		return Node;
	};

	const TSharedPtr<FDesignNode> ThresholdNode = MakeSingleLineNode(TEXT("threshold_line_height"), 24.0f);
	const TSharedPtr<FDesignNode> BelowThresholdNode = MakeSingleLineNode(TEXT("below_threshold_line_height"), 23.0f);

	const bool bThresholdApplies = TextMeasurement::ShouldApplyExplicitLineHeight(ThresholdNode, ThresholdNode->Text.FontSize);
	const bool bBelowThresholdApplies = TextMeasurement::ShouldApplyExplicitLineHeight(BelowThresholdNode, BelowThresholdNode->Text.FontSize);
	TestTrue(TEXT("Large non-wrapping threshold should apply explicit line height"), bThresholdApplies);
	TestFalse(TEXT("Below-threshold single line should not apply explicit line height"), bBelowThresholdApplies);

	const FUMGWidgetNode ThresholdWidget = Design2UMGTextWidgetBuilder::CreateTextWidget(ThresholdNode);
	const FUMGWidgetNode BelowThresholdWidget = Design2UMGTextWidgetBuilder::CreateTextWidget(BelowThresholdNode);
	TestTrue(TEXT("Builder keeps shared threshold owner's line-height decision"), ThresholdWidget.Text.LineHeightPercentage.IsSet());
	TestEqual(TEXT("Builder writes the authored line height when threshold applies"), ThresholdWidget.Text.LineHeightPercentage.GetValue(), ThresholdNode->Text.LineHeight);
	TestFalse(TEXT("Builder should not emit line height below the shared threshold"), BelowThresholdWidget.Text.LineHeightPercentage.IsSet());
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

	TSharedPtr<FDesignNode> Node = MakeSharedSemanticsNode(TEXT("icon_font_boundary"), EDesignNodeType::IconFont, FDesignSize::Fixed(24.0f), FDesignSize::Fixed(24.0f));
	Node->Text.IconFontFamily = TEXT("MissingFamily_BatchA");
	Node->Text.IconFontName = TEXT("dashboard");
	Node->Text.FontSize = 20.0f;

	Design2UMGTextWidgetBuilder::FIconFontBuildArgs Args;
	Args.SettingsSnapshot = &Snapshot;

	const Design2UMGTextWidgetBuilder::FIconFontBuildResult Result =
		Design2UMGTextWidgetBuilder::CreateIconFontWidget(Node, Args);
	TestEqual(TEXT("Shared-path icon builder should consume the injected snapshot placeholder"),
		Result.Widget.Text.Text.GetValue(),
		FString(TEXT("[snapshot-placeholder]")));
	TestFalse(TEXT("Injected snapshot should not silently fall back to the global placeholder"),
		Result.Widget.Text.Text.IsSet() && Result.Widget.Text.Text.GetValue().Equals(TEXT("[global-placeholder]"), ESearchCase::CaseSensitive));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConverterOwnerBoundaryExecutableGuardTest,
	"Design2UMG.Converter.OwnerBoundaryExecutableGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConverterOwnerBoundaryExecutableGuardTest::RunTest(const FString& Parameters)
{
	AddInfo(TEXT("This executable guard replaces the retired converter source-text structure guard."));
	return RunConverterOwnerBoundaryChecks(*this);
}
#endif
