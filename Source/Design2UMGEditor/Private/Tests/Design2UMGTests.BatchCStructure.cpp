// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignSchema.h"
#include "FigmaParser.h"
#include "Misc/AutomationTest.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UMGTextStyleApplication.h"
#include "VisualMapper.h"

#include "Components/TextBlock.h"
#include "UObject/UnrealType.h"

namespace
{
	FString GetPluginFilePath(const FString& RelativePath)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), RelativePath)
			: FString();
	}

	bool LoadPluginFile(FAutomationTestBase& Test, const FString& RelativePath, FString& OutContent)
	{
		const FString AbsolutePath = GetPluginFilePath(RelativePath);
		Test.TestFalse(*FString::Printf(TEXT("%s path resolved"), *RelativePath), AbsolutePath.IsEmpty());
		if (AbsolutePath.IsEmpty())
		{
			return false;
		}

		Test.TestTrue(*FString::Printf(TEXT("%s exists"), *RelativePath), IFileManager::Get().FileExists(*AbsolutePath));
		if (!IFileManager::Get().FileExists(*AbsolutePath))
		{
			return false;
		}

		Test.TestTrue(*FString::Printf(TEXT("Load %s"), *RelativePath), FFileHelper::LoadFileToString(OutContent, *AbsolutePath));
		return !OutContent.IsEmpty();
	}

	FDesignFill MakeGradientFill()
	{
		FDesignFill Fill;
		Fill.Type = EDesignFillType::Gradient;
		Fill.Gradient.GradientType = EDesignGradientType::Radial;
		Fill.Gradient.Rotation = 25.0f;
		Fill.Gradient.bHasCenter = true;
		Fill.Gradient.CenterX = 0.25f;
		Fill.Gradient.CenterY = 0.75f;
		FDesignGradientStop& StartStop = Fill.Gradient.Colors.AddDefaulted_GetRef();
		StartStop.Color = TEXT("#FF0000FF");
		StartStop.Position = 0.0f;
		FDesignGradientStop& EndStop = Fill.Gradient.Colors.AddDefaulted_GetRef();
		EndStop.Color = TEXT("#00FF00FF");
		EndStop.Position = 1.0f;
		return Fill;
	}

	bool TryReadBoolProperty(const UObject* Object, const TCHAR* PropertyName, bool& OutValue)
	{
		const FBoolProperty* Property = Object ? FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName) : nullptr;
		if (!Property)
		{
			return false;
		}
		OutValue = Property->GetPropertyValue_InContainer(Object);
		return true;
	}

	bool TryReadFloatProperty(const UObject* Object, const TCHAR* PropertyName, float& OutValue)
	{
		const FFloatProperty* Property = Object ? FindFProperty<FFloatProperty>(Object->GetClass(), PropertyName) : nullptr;
		if (!Property)
		{
			return false;
		}
		OutValue = Property->GetPropertyValue_InContainer(Object);
		return true;
	}

	bool TryReadByteProperty(const UObject* Object, const TCHAR* PropertyName, uint8& OutValue)
	{
		const FByteProperty* Property = Object ? FindFProperty<FByteProperty>(Object->GetClass(), PropertyName) : nullptr;
		if (!Property)
		{
			return false;
		}
		OutValue = Property->GetPropertyValue_InContainer(Object);
		return true;
	}

	bool TryReadMarginProperty(const UObject* Object, const TCHAR* PropertyName, FMargin& OutValue)
	{
		const FStructProperty* Property = Object ? FindFProperty<FStructProperty>(Object->GetClass(), PropertyName) : nullptr;
		if (!Property || Property->Struct != TBaseStructure<FMargin>::Get())
		{
			return false;
		}
		OutValue = *Property->ContainerPtrToValuePtr<FMargin>(Object);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCConverterOwnerSliceStructureTest,
	"Design2UMG.BatchC.ConverterOwnerSliceStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCConverterOwnerSliceStructureTest::RunTest(const FString& Parameters)
{
	FString MainSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/Design2UMGConverter.cpp"), MainSource))
	{
		return false;
	}

	FString HelperHeader;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/Design2UMGConverterFlowSupport.h"), HelperHeader))
	{
		return false;
	}

	TestTrue(TEXT("Converter main file includes flow support helper"),
		MainSource.Contains(TEXT("#include \"Design2UMGConverterFlowSupport.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Expanded node resolver no longer lives in converter main file"),
		MainSource.Contains(TEXT("struct FExpandedNodeResolver"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Flow child preparation no longer lives in converter main file"),
		MainSource.Contains(TEXT("struct FFlowChildPreparation"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Flow support helper owns expanded-node resolver"),
		HelperHeader.Contains(TEXT("class FExpandedNodeResolver"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Flow support helper owns flow child preparation"),
		HelperHeader.Contains(TEXT("class FFlowChildPreparation"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCFigmaParserOwnerSliceStructureTest,
	"Design2UMG.BatchC.FigmaParserOwnerSliceStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCFigmaParserOwnerSliceStructureTest::RunTest(const FString& Parameters)
{
	FString MainSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/FigmaParserInternal.cpp"), MainSource))
	{
		return false;
	}

	FString HelperSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/FigmaParserMetadata.cpp"), HelperSource))
	{
		return false;
	}

	TestTrue(TEXT("Figma parser main file includes metadata helper"),
		MainSource.Contains(TEXT("#include \"FigmaParserMetadata.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Semantic hint walker no longer lives in parser main file"),
		MainSource.Contains(TEXT("static bool TryReadSemanticHintFromJsonValue("), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Semantic hint application no longer lives in parser main file"),
		MainSource.Contains(TEXT("static void ApplyFigmaSemanticHints("), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Metadata helper owns semantic hint application"),
		HelperSource.Contains(TEXT("ApplyFigmaSemanticHints"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Metadata helper owns render fallback capture"),
		HelperSource.Contains(TEXT("CaptureRenderFallbackMetadata"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCVisualMapperOwnerSliceStructureTest,
	"Design2UMG.BatchC.VisualMapperOwnerSliceStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCVisualMapperOwnerSliceStructureTest::RunTest(const FString& Parameters)
{
	FString MainSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/VisualMapper.cpp"), MainSource))
	{
		return false;
	}

	FString HelperSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/VisualMapperFillTransfer.cpp"), HelperSource))
	{
		return false;
	}

	TestTrue(TEXT("Visual mapper main file includes fill transfer helper"),
		MainSource.Contains(TEXT("#include \"VisualMapperFillTransfer.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Gradient helpers no longer live in visual mapper main file"),
		MainSource.Contains(TEXT("namespace GradientHelpers"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Multi-fill wrapper construction no longer lives in visual mapper main file"),
		MainSource.Contains(TEXT("MakeMultiFillOverlayShell"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Fill transfer helper owns gradient helpers"),
		HelperSource.Contains(TEXT("namespace GradientHelpers"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Fill transfer helper owns multi-fill wrapper"),
		HelperSource.Contains(TEXT("WrapWithMultiFill"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCTextPropertyOwnerSliceStructureTest,
	"Design2UMG.BatchC.TextPropertyOwnerSliceStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCTextPropertyOwnerSliceStructureTest::RunTest(const FString& Parameters)
{
	FString MainSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/UMGTextPropertyApplier.cpp"), MainSource))
	{
		return false;
	}

	FString HelperSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/UMGTextStyleApplication.cpp"), HelperSource))
	{
		return false;
	}

	TestTrue(TEXT("Text property applier main file includes text style helper"),
		MainSource.Contains(TEXT("#include \"UMGTextStyleApplication.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Typed font helper no longer lives in text property applier main file"),
		MainSource.Contains(TEXT("void ApplyTypedFontInfo("), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Rich text default style helper no longer lives in text property applier main file"),
		MainSource.Contains(TEXT("void ApplyRichTextDefaultStyle("), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Text style helper owns typed font helper"),
		HelperSource.Contains(TEXT("void ApplyTypedFontInfo("), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Text style helper owns rich text style rows helper"),
		HelperSource.Contains(TEXT("void ApplyRichTextStyleRows("), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCFigmaSemanticHintSmokeTest,
	"Design2UMG.BatchC.FigmaSemanticHintSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCFigmaSemanticHintSmokeTest::RunTest(const FString& Parameters)
{
	const FString FigmaJson =
		TEXT("{")
		TEXT("\"document\":{")
		TEXT("\"id\":\"semantic_frame\",")
		TEXT("\"name\":\"Semantic Frame\",")
		TEXT("\"type\":\"FRAME\",")
		TEXT("\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":160,\"height\":80},")
		TEXT("\"pluginData\":{\"design2umg\":\"{\\\"slot\\\":true}\"},")
		TEXT("\"slot\":[]")
		TEXT("}")
		TEXT("}");

	FDesignDocument Document;
	TestTrue(TEXT("Figma semantic smoke parses minimal frame"), FFigmaParser::ParseDocument(FigmaJson, Document));
	TestEqual(TEXT("Parsed document contains one top-level frame"), Document.Children.Num(), 1);
	if (Document.Children.Num() != 1 || !Document.Children[0].IsValid())
	{
		return false;
	}

	const FDesignNode& Node = *Document.Children[0];
	TestTrue(TEXT("Plugin semantic marker sets placeholder surface flag"), Node.LayoutData.bSemanticPlaceholderSurface);
	TestEqual(TEXT("Empty slot array is captured as source metadata"),
		Node.Metadata.FindRef(TEXT("source.slot.empty")),
		FString(TEXT("true")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCVisualMapperMultiFillSmokeTest,
	"Design2UMG.BatchC.VisualMapperMultiFillSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCVisualMapperMultiFillSmokeTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("multi_fill_source");
	Node->Name = TEXT("MultiFillSource");
	Node->Type = EDesignNodeType::Rectangle;
	Node->Transform.SetWidthValue(FDesignSize::Fixed(96.0f));
	Node->Transform.SetHeightValue(FDesignSize::Fixed(48.0f));
	Node->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#112233FF")));
	Node->Visual.Fills.Add(MakeGradientFill());

	FUMGWidgetNode BaseWidget;
	BaseWidget.Type = TEXT("Image");
	BaseWidget.Id = TEXT("multi_fill_widget");
	BaseWidget.Name = TEXT("MultiFillWidget");
	BaseWidget.Bounds.W = 96.0f;
	BaseWidget.Bounds.H = 48.0f;

	const FUMGWidgetNode Wrapped = VisualMapper::WrapWithMultiFill(MoveTemp(BaseWidget), Node, FVisualMapperOptions());
	TestEqual(TEXT("Multi-fill wrapper creates an overlay shell"), Wrapped.Type, FString(TEXT("Overlay")));
	TestEqual(TEXT("Multi-fill wrapper keeps two paint layers plus content"), Wrapped.Children.Num(), 3);
	if (Wrapped.Children.Num() != 3)
	{
		return false;
	}

	const FUMGWidgetNode& GradientLayer = Wrapped.Children[1];
	TestEqual(TEXT("Gradient fill layer becomes image material"), GradientLayer.Type, FString(TEXT("Image")));
	TestTrue(TEXT("Gradient layer carries typed material ref"), GradientLayer.Brush.bHasMaterialRef);
	TestEqual(TEXT("Radial gradient resolves to radial material"),
		GradientLayer.Brush.MaterialRef,
		FString(TEXT("radialgradient")));
	TestTrue(TEXT("Gradient layer carries typed gradient stops"), GradientLayer.Brush.Gradient.Stops.Num() == 2);
	TestTrue(TEXT("Original widget becomes content child"), Wrapped.Children[2].Id.EndsWith(TEXT("_content")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCTextStyleApplicationSmokeTest,
	"Design2UMG.BatchC.TextStyleApplicationSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCTextStyleApplicationSmokeTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.TextStyle.FontSize = 18.0f;
	Node.Text.bHasTypefaceFontName = true;
	Node.Text.TypefaceFontName = TEXT("Bold");
	Node.Text.bHasJustification = true;
	Node.Text.Justification = TEXT("Center");
	Node.Text.bHasAutoWrapText = true;
	Node.Text.bAutoWrapText = true;
	Node.Text.bHasWrapTextAt = true;
	Node.Text.WrapTextAt = 144.0f;
	Node.Text.bHasLineHeightPercentage = true;
	Node.Text.LineHeightPercentage = 1.25f;
	Node.WidgetStyle.Padding = FMargin(4.0f, 5.0f, 6.0f, 7.0f);

	FSlateFontInfo FontInfo;
	bool bFontChanged = false;
	UMGTextStyleApplication::ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
	TestTrue(TEXT("Typed font application reports a font change"), bFontChanged);
	TestEqual(TEXT("Typed font application applies rounded font size"), static_cast<int32>(FontInfo.Size), 18);
	TestEqual(TEXT("Typed font application applies typeface name"), FontInfo.TypefaceFontName, FName(TEXT("Bold")));

	UTextBlock* TextBlock = NewObject<UTextBlock>();
	TestNotNull(TEXT("Text block can be allocated for typed layout smoke"), TextBlock);
	if (!TextBlock)
	{
		return false;
	}

	UMGTextStyleApplication::ApplyTypedTextLayoutProps(TextBlock, Node);
	uint8 JustificationValue = 0;
	bool bAutoWrapText = false;
	float WrapTextAt = 0.0f;
	float LineHeightPercentage = 0.0f;
	FMargin Margin;
	TestTrue(TEXT("Typed layout smoke can read justification property"),
		TryReadByteProperty(TextBlock, TEXT("Justification"), JustificationValue));
	TestEqual(TEXT("Typed layout application applies justification"),
		JustificationValue,
		static_cast<uint8>(ETextJustify::Center));
	TestTrue(TEXT("Typed layout smoke can read auto-wrap property"),
		TryReadBoolProperty(TextBlock, TEXT("AutoWrapText"), bAutoWrapText));
	TestTrue(TEXT("Typed layout application enables auto wrap"), bAutoWrapText);
	TestTrue(TEXT("Typed layout smoke can read wrap width property"),
		TryReadFloatProperty(TextBlock, TEXT("WrapTextAt"), WrapTextAt));
	TestEqual(TEXT("Typed layout application applies wrap width"), WrapTextAt, 144.0f);
	TestTrue(TEXT("Typed layout smoke can read line-height property"),
		TryReadFloatProperty(TextBlock, TEXT("LineHeightPercentage"), LineHeightPercentage));
	TestEqual(TEXT("Typed layout application applies line height"), LineHeightPercentage, 1.25f);
	TestTrue(TEXT("Typed layout smoke can read margin property"),
		TryReadMarginProperty(TextBlock, TEXT("Margin"), Margin));
	TestEqual(TEXT("Typed layout application applies padding"), Margin, FMargin(4.0f, 5.0f, 6.0f, 7.0f));
	return true;
}
