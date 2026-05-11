// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignSchema.h"
#include "Design2UMGConverter.h"
#include "Design2UMGTestUtils.h"
#include "FigmaParser.h"
#include "Misc/AutomationTest.h"
#include "UMGTextStyleApplication.h"

#include "Components/TextBlock.h"
#include "UObject/UnrealType.h"

namespace
{
	TSharedPtr<FDesignNode> MakeBatchCNode(
		const FString& Id,
		const FString& Name,
		const EDesignNodeType Type,
		const float X,
		const float Y,
		const float Width,
		const float Height)
	{
		const TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Name;
		Node->Type = Type;
		Node->Transform.SetPosition(X, Y);
		Node->Transform.SetWidthValue(FDesignSize::Fixed(Width));
		Node->Transform.SetHeightValue(FDesignSize::Fixed(Height));
		return Node;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCFlowChildPreResolutionSmokeTest,
	"Design2UMG.BatchC.FlowChildPreResolutionSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCFlowChildPreResolutionSmokeTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	Document.Version = TEXT("batch-c-converter-flow-smoke");

	const TSharedPtr<FDesignNode> Reusable = MakeBatchCNode(
		TEXT("batch_c_card"),
		TEXT("BatchCCard"),
		EDesignNodeType::Frame,
		0.0f,
		0.0f,
		64.0f,
		24.0f);
	Reusable->bReusable = true;
	Reusable->Children.Add(MakeBatchCNode(
		TEXT("batch_c_card_fill"),
		TEXT("BatchCCardFill"),
		EDesignNodeType::Rectangle,
		0.0f,
		0.0f,
		64.0f,
		24.0f));

	const TSharedPtr<FDesignNode> Screen = MakeBatchCNode(
		TEXT("batch_c_screen"),
		TEXT("BatchCScreen"),
		EDesignNodeType::Frame,
		0.0f,
		0.0f,
		200.0f,
		40.0f);
	Screen->LayoutData.Layout = EDesignLayout::Horizontal;
	Screen->LayoutData.SetGap(8.0f);

	const TSharedPtr<FDesignNode> RefChild = MakeBatchCNode(
		TEXT("batch_c_ref"),
		TEXT("BatchCRef"),
		EDesignNodeType::Ref,
		0.0f,
		0.0f,
		64.0f,
		24.0f);
	RefChild->Ref = TEXT("batch_c_card");

	Screen->Children.Add(RefChild);
	Document.Children.Add(Reusable);
	Document.Children.Add(Screen);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	const FUMGWidgetNode* ScreenWidget = Design2UMGTestUtils::FindWidgetById(Data.RootWidget, TEXT("batch_c_screen"));
	const FUMGWidgetNode* RefWidget = Design2UMGTestUtils::FindWidgetById(Data.RootWidget, TEXT("batch_c_ref"));

	TestNotNull(TEXT("Converted screen widget exists"), ScreenWidget);
	TestNotNull(TEXT("Expanded reusable ref widget exists"), RefWidget);
	if (!ScreenWidget || !RefWidget)
	{
		return false;
	}

	TestTrue(TEXT("Reusable definition stays out of render tree"),
		Design2UMGTestUtils::FindWidgetById(Data.RootWidget, TEXT("batch_c_card")) == nullptr);
	TestEqual(TEXT("Screen keeps expanded ref as its visible child"), ScreenWidget->Children.Num(), 1);
	TestTrue(TEXT("Expanded ref keeps authored width"), FMath::IsNearlyEqual(RefWidget->Bounds.W, 64.0f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Expanded ref keeps authored height"), FMath::IsNearlyEqual(RefWidget->Bounds.H, 24.0f, KINDA_SMALL_NUMBER));
	TestNotEqual(TEXT("Expanded ref does not remain a raw Ref widget"), RefWidget->Type, FString(TEXT("Ref")));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCTextStyleApplicationSmokeTest,
	"Design2UMG.BatchC.TextStyleApplicationSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCTextStyleApplicationSmokeTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.TextStyle.FontSize = 18.0f;
	Node.Text.TypefaceFontName = TEXT("Bold");
	Node.Text.Justification = TEXT("Center");
	Node.Text.bAutoWrapText = true;
	Node.Text.WrapTextAt = 144.0f;
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
