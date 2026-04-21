// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "Design2UMGConverter.h"
#include "Design2UMGSettings.h"
#include "SharedCoreSemantics.h"
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

	struct FWidthHintProbeCase
	{
		FString Name;
		TSharedPtr<FDesignNode> Node;
		float ExpectedWidth = 0.0f;
	};
	const float ParentContentWidth = 280.0f;
	const TArray<FWidthHintProbeCase> WidthHintCases = {
		{ TEXT("fixed-width"), MakeWidthHintProbeTextNode(TEXT("guard_width_fixed"), FDesignSize::Fixed(180.0f)), 180.0f },
		{ TEXT("fill-width-with-fallback"), MakeWidthHintProbeTextNode(TEXT("guard_width_fill_fallback"), FDesignSize::Fill(300.0f)), 300.0f },
		{ TEXT("fill-width-parent-fallback"), MakeWidthHintProbeTextNode(TEXT("guard_width_fill_parent"), FDesignSize::Fill(0.0f)), ParentContentWidth }
	};
	for (const FWidthHintProbeCase& ProbeCase : WidthHintCases)
	{
		const float CanonicalWidth = Design2UMGCore::SharedSemantics::ResolveWidthHint(ProbeCase.Node, ParentContentWidth);
		const float EstimatedWidth = SizeEstimator::EstimateNodeSize(ProbeCase.Node, ParentContentWidth).X;
		const float WrapAt = TextMeasurement::ResolveTextWrapAt(ProbeCase.Node, ParentContentWidth);

		TestTrue(
			*FString::Printf(TEXT("%s canonical width hint remains stable"), *ProbeCase.Name),
			FMath::IsNearlyEqual(CanonicalWidth, ProbeCase.ExpectedWidth, KINDA_SMALL_NUMBER));
		TestTrue(
			*FString::Printf(TEXT("%s sizing width follows canonical helper"), *ProbeCase.Name),
			FMath::IsNearlyEqual(EstimatedWidth, CanonicalWidth, KINDA_SMALL_NUMBER));
		TestTrue(
			*FString::Printf(TEXT("%s wrap width follows canonical helper"), *ProbeCase.Name),
			FMath::IsNearlyEqual(WrapAt, CanonicalWidth, KINDA_SMALL_NUMBER));
	}

	FString SizeEstimatorSource;
	TestTrue(TEXT("Load SizeEstimator.cpp for delegation guard"), FFileHelper::LoadFileToString(SizeEstimatorSource, *SizeEstimatorPath));
	if (!SizeEstimatorSource.IsEmpty())
	{
		TestTrue(TEXT("SizeEstimator skip-node logic delegates to shared helper"),
			SizeEstimatorSource.Contains(TEXT("SharedSemantics::ShouldSkipNode(Node)")));
	}

	FString TextMeasurementSource;
	TestTrue(TEXT("Load TextMeasurement.cpp for delegation guard"), FFileHelper::LoadFileToString(TextMeasurementSource, *TextMeasurementPath));

	FString SharedSemanticsSource;
	TestTrue(TEXT("Load SharedCoreSemantics.cpp for shared semantic source guard"), FFileHelper::LoadFileToString(SharedSemanticsSource, *SharedSemanticsPath));
	TestTrue(TEXT("Shared semantics helper retains canonical skip logic"), SharedSemanticsSource.Contains(TEXT("bool ShouldSkipNode(const TSharedPtr<FDesignNode>& Node)")));
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

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* PlaceholderWidget = FindWidgetByIdRecursive(Data.RootWidget, TEXT("placeholder_payload_root"));
	if (!TestNotNull(TEXT("Converted placeholder widget exists"), PlaceholderWidget))
	{
		return false;
	}

	TestEqual(TEXT("Placeholder widget uses direct image material"), PlaceholderWidget->Type, FString(TEXT("Image")));
	TestEqual(TEXT("Placeholder widget uses placeholder surface material ref"), PlaceholderWidget->Brush.MaterialRef, FString(TEXT("placeholdersurface")));
	TestTrue(TEXT("Placeholder material params use injected base fill color"), PlaceholderWidget->Brush.MaterialParams.Contains(TEXT("\"Color\":\"#123456\"")));
	TestTrue(TEXT("Placeholder material params use injected base stripe color"), PlaceholderWidget->Brush.MaterialParams.Contains(TEXT("\"StripeColor\":\"#ABCDEF\"")));
	TestTrue(TEXT("Placeholder material params use injected stripe width"), PlaceholderWidget->Brush.MaterialParams.Contains(TEXT("\"StripeWidth\":7")));
	TestTrue(TEXT("Placeholder material params use injected stripe gap"), PlaceholderWidget->Brush.MaterialParams.Contains(TEXT("\"StripeGap\":11")));
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

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* OverlayImage = FindWidgetByIdRecursive(Data.RootWidget, TEXT("luminosity_payload_root_fill1_img"));
	if (!TestNotNull(TEXT("Multi-fill luminosity image child exists"), OverlayImage))
	{
		return false;
	}

	TestTrue(TEXT("Luminosity image child exported render opacity"), OverlayImage->Render.bHasRenderOpacity);
	TestTrue(TEXT("Luminosity image child clamps opacity using injected cap"), FMath::IsNearlyEqual(OverlayImage->Render.RenderOpacity, 0.2f, KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConverterPrivateOwnerStructureGuardTest,
	"Design2UMG.Converter.PrivateOwnerStructureGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConverterPrivateOwnerStructureGuardTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
	TestTrue(TEXT("Design2UMG plugin exists for converter owner guard"), Plugin.IsValid());
	if (!Plugin.IsValid())
	{
		return false;
	}

	const FString ConverterSourcePath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Source/Design2UMGCore/Private/Design2UMGConverter.cpp"));
	FString ConverterSource;
	TestTrue(TEXT("Load Design2UMGConverter.cpp for owner guard"), FFileHelper::LoadFileToString(ConverterSource, *ConverterSourcePath));
	if (ConverterSource.IsEmpty())
	{
		return false;
	}

	TestTrue(TEXT("Converter defines node orchestration owner"), ConverterSource.Contains(TEXT("struct FNodeConversionOrchestrator")));
	TestTrue(TEXT("Converter defines child preparation owner"), ConverterSource.Contains(TEXT("struct FNodeChildPreparation")));
	TestTrue(TEXT("Converter defines child append owner"), ConverterSource.Contains(TEXT("struct FNodeChildAppender")));
	TestTrue(TEXT("Convert entry uses node orchestration owner"), ConverterSource.Contains(TEXT("const FNodeConversionOrchestrator NodeOrchestrator(RootContext);")));

	TestFalse(TEXT("Legacy free ConvertNode forward declaration removed"),
		ConverterSource.Contains(TEXT("FUMGWidgetNode ConvertNode(const TSharedPtr<FDesignNode>& Node, const FConversionContext& Context, EDesignLayout ParentLayout = EDesignLayout::None);")));
	TestFalse(TEXT("Legacy free child preparation declaration removed"),
		ConverterSource.Contains(TEXT("void PreResolveFlowChildren(const TSharedPtr<FDesignNode>& Node, const FUMGWidgetNode& Widget, const TArray<TSharedPtr<FDesignNode>>& VisibleChildren, const FConversionContext& Context);")));
	TestFalse(TEXT("Legacy free child append declaration removed"),
		ConverterSource.Contains(TEXT("void AppendConvertedChildren(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node, const TArray<TSharedPtr<FDesignNode>>& VisibleChildren, const FConversionContext& Context);")));
	return true;
}
#endif
