// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Typed text payload application and parity for the text widget family (TextBlock, editable text family, rich text, letter spacing, JSON round trips).

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGPropertyKeys.h"
#include "UMGTextStyleApplication.h"
#include "UMGValueParsing.h"
#include "UMGWidgetUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/DataTable.h"
#include "UObject/UObjectGlobals.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextBlockUsesTypedTextPayloadParityTest,
	"Design2UMG.PropertyApplier.TextBlockUsesTypedTextPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextBlockUsesTypedTextPayloadParityTest::RunTest(const FString& Parameters)
{
	UTextBlock* TypedWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	UTextBlock* LegacyWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	TestTrue(TEXT("Typed TextBlock created"), TypedWidget != nullptr);
	TestTrue(TEXT("Legacy TextBlock created"), LegacyWidget != nullptr);
	if (!TypedWidget || !LegacyWidget)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("TextBlock");
	Node.Text.Text = TEXT("Glyph label");
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.IconName = TEXT("glyph_download");
	Node.Text.Justification = TEXT("Center");
	Node.Text.bAutoWrapText = true;
	Node.Text.WrapTextAt = 144.0f;
	Node.Text.LineHeightPercentage = 1.35f;
	Node.Text.Color = FLinearColor(0.2f, 0.4f, 0.6f, 1.0f);
	Node.TextStyle.FontSize = 18.0f;
	Node.TextStyle.SourceFontSize = 18.0f;
	Node.TextStyle.LetterSpacing = 1.5f;
	Node.WidgetStyle.Padding = FMargin(7.0f, 8.0f, 9.0f, 10.0f);
	Node.WidgetStyle.ToolTipText = TEXT("Typed tooltip");
	Node.Shadow.ShadowOffset = FVector2D(2.0f, 3.0f);
	Node.Shadow.ShadowColorAndOpacity = FLinearColor(0.9f, 0.1f, 0.2f, 0.5f);

	FUMGWidgetUtils::ApplyWidgetProperties(TypedWidget, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyWidget, Node);

	const FStructProperty* MarginProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	const FFloatProperty* LineHeightProperty = FindFProperty<FFloatProperty>(UTextLayoutWidget::StaticClass(), TEXT("LineHeightPercentage"));
	const FByteProperty* JustificationProperty = FindFProperty<FByteProperty>(UTextLayoutWidget::StaticClass(), TEXT("Justification"));
	const FStructProperty* ShapedTextOptionsProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("ShapedTextOptions"));
	TestNotNull(TEXT("Text layout margin property exists"), MarginProperty);
	TestNotNull(TEXT("Text layout line-height property exists"), LineHeightProperty);
	TestNotNull(TEXT("Text layout justification property exists"), JustificationProperty);
	TestNotNull(TEXT("Text layout shaped-text property exists"), ShapedTextOptionsProperty);
	if (!MarginProperty || !LineHeightProperty || !JustificationProperty || !ShapedTextOptionsProperty)
	{
		return false;
	}

	const FMargin* TypedMargin = MarginProperty->ContainerPtrToValuePtr<FMargin>(TypedWidget);
	const FMargin* LegacyMargin = MarginProperty->ContainerPtrToValuePtr<FMargin>(LegacyWidget);
	const float TypedLineHeight = LineHeightProperty->GetPropertyValue_InContainer(TypedWidget);
	const float LegacyLineHeight = LineHeightProperty->GetPropertyValue_InContainer(LegacyWidget);
	const ETextJustify::Type TypedJustification = static_cast<ETextJustify::Type>(
		JustificationProperty->GetUnsignedIntPropertyValue(JustificationProperty->ContainerPtrToValuePtr<void>(TypedWidget)));
	const ETextJustify::Type LegacyJustification = static_cast<ETextJustify::Type>(
		JustificationProperty->GetUnsignedIntPropertyValue(JustificationProperty->ContainerPtrToValuePtr<void>(LegacyWidget)));
	const FShapedTextOptions* TypedShaping = ShapedTextOptionsProperty->ContainerPtrToValuePtr<FShapedTextOptions>(TypedWidget);
	const FShapedTextOptions* LegacyShaping = ShapedTextOptionsProperty->ContainerPtrToValuePtr<FShapedTextOptions>(LegacyWidget);

	TestEqual(TEXT("Typed node path keeps tooltip"), TypedWidget->GetToolTipText().ToString(), TEXT("Typed tooltip"));
	TestEqual(TEXT("Typed and legacy text match"), TypedWidget->GetText().ToString(), LegacyWidget->GetText().ToString());
	const FLinearColor ExpectedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	TestTrue(TEXT("Typed text block color follows normalized typed node color"),
		TypedWidget->GetColorAndOpacity().GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second text block color follows normalized typed node color"),
		LegacyWidget->GetColorAndOpacity().GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestEqual(TEXT("Typed and legacy font size match"), TypedWidget->GetFont().Size, LegacyWidget->GetFont().Size);
	TestEqual(TEXT("Typed and legacy typeface match"), TypedWidget->GetFont().TypefaceFontName, LegacyWidget->GetFont().TypefaceFontName);
	TestEqual(TEXT("Typed and legacy letter spacing match"), TypedWidget->GetFont().LetterSpacing, LegacyWidget->GetFont().LetterSpacing);
	TestEqual(TEXT("Typed and legacy font object path match"),
		TypedWidget->GetFont().FontObject ? TypedWidget->GetFont().FontObject->GetPathName() : FString(),
		LegacyWidget->GetFont().FontObject ? LegacyWidget->GetFont().FontObject->GetPathName() : FString());
	TestEqual(TEXT("Typed and legacy justification match"), TypedJustification, LegacyJustification);
	TestTrue(TEXT("Typed and legacy auto-wrap match"), TypedWidget->GetAutoWrapText() == LegacyWidget->GetAutoWrapText());
	TestTrue(TEXT("Typed and legacy wrap width match"), FMath::IsNearlyEqual(TypedWidget->GetWrapTextAt(), LegacyWidget->GetWrapTextAt(), 0.001f));
	TestTrue(TEXT("Typed and legacy line height match"), FMath::IsNearlyEqual(TypedLineHeight, LegacyLineHeight, 0.001f));
	TestTrue(TEXT("Typed and legacy margin match"),
		TypedMargin
		&& LegacyMargin
		&& FMath::IsNearlyEqual(TypedMargin->Left, LegacyMargin->Left, 0.001f)
		&& FMath::IsNearlyEqual(TypedMargin->Top, LegacyMargin->Top, 0.001f)
		&& FMath::IsNearlyEqual(TypedMargin->Right, LegacyMargin->Right, 0.001f)
		&& FMath::IsNearlyEqual(TypedMargin->Bottom, LegacyMargin->Bottom, 0.001f));
	TestTrue(TEXT("Typed and legacy shadow offset match"), TypedWidget->GetShadowOffset().Equals(LegacyWidget->GetShadowOffset(), 0.001f));
	TestTrue(TEXT("Typed and legacy shadow color match"),
		TypedWidget->GetShadowColorAndOpacity().Equals(LegacyWidget->GetShadowColorAndOpacity(), 0.0001f));
	TestTrue(TEXT("Typed and legacy icon shaping match"),
		TypedShaping
		&& LegacyShaping
		&& TypedShaping->bOverride_TextShapingMethod == LegacyShaping->bOverride_TextShapingMethod
		&& TypedShaping->TextShapingMethod == LegacyShaping->TextShapingMethod);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditableTextFamilyUsesTypedTextPayloadParityTest,
	"Design2UMG.PropertyApplier.EditableTextFamilyUsesTypedTextPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEditableTextFamilyUsesTypedTextPayloadParityTest::RunTest(const FString& Parameters)
{
	const FFloatProperty* EditableTextBoxMinimumDesiredWidthProperty = FindFProperty<FFloatProperty>(UEditableTextBox::StaticClass(), TEXT("MinimumDesiredWidth"));
	const FFloatProperty* EditableTextMinimumDesiredWidthProperty = FindFProperty<FFloatProperty>(UEditableText::StaticClass(), TEXT("MinimumDesiredWidth"));
	TestNotNull(TEXT("EditableTextBox MinimumDesiredWidth property exists"), EditableTextBoxMinimumDesiredWidthProperty);
	TestNotNull(TEXT("EditableText MinimumDesiredWidth property exists"), EditableTextMinimumDesiredWidthProperty);
	if (!EditableTextBoxMinimumDesiredWidthProperty || !EditableTextMinimumDesiredWidthProperty)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Text.Text = TEXT("Editable text");
	Node.Text.HintText = TEXT("Editable hint");
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.IconName = TEXT("glyph_edit");
	Node.Text.Justification = TEXT("Center");
	Node.Text.Color = FLinearColor(0.3f, 0.5f, 0.7f, 1.0f);
	Node.TextStyle.FontSize = 19.0f;
	Node.TextStyle.SourceFontSize = 19.0f;
	Node.TextStyle.LetterSpacing = 1.25f;
	Node.WidgetStyle.MinDesiredWidth = 123.5f;

	UEditableTextBox* TypedTextBox = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	UEditableTextBox* LegacyTextBox = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed editable text box created"), TypedTextBox);
	TestNotNull(TEXT("Legacy editable text box created"), LegacyTextBox);
	if (!TypedTextBox || !LegacyTextBox)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedTextBox, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyTextBox, Node);

	const FEditableTextBoxStyle& TypedTextBoxStyle = TypedTextBox->GetWidgetStyle();
	const FEditableTextBoxStyle& LegacyTextBoxStyle = LegacyTextBox->GetWidgetStyle();
	TestEqual(TEXT("EditableTextBox typed and legacy text match"), TypedTextBox->GetText().ToString(), LegacyTextBox->GetText().ToString());
	TestEqual(TEXT("EditableTextBox typed and legacy hint match"), TypedTextBox->GetHintText().ToString(), LegacyTextBox->GetHintText().ToString());
	TestEqual(TEXT("EditableTextBox typed and legacy font size match"), TypedTextBoxStyle.TextStyle.Font.Size, LegacyTextBoxStyle.TextStyle.Font.Size);
	TestEqual(TEXT("EditableTextBox typed and legacy typeface match"), TypedTextBoxStyle.TextStyle.Font.TypefaceFontName, LegacyTextBoxStyle.TextStyle.Font.TypefaceFontName);
	TestEqual(TEXT("EditableTextBox typed and legacy letter spacing match"), TypedTextBoxStyle.TextStyle.Font.LetterSpacing, LegacyTextBoxStyle.TextStyle.Font.LetterSpacing);
	TestEqual(TEXT("EditableTextBox typed and legacy font object path match"),
		TypedTextBoxStyle.TextStyle.Font.FontObject ? TypedTextBoxStyle.TextStyle.Font.FontObject->GetPathName() : FString(),
		LegacyTextBoxStyle.TextStyle.Font.FontObject ? LegacyTextBoxStyle.TextStyle.Font.FontObject->GetPathName() : FString());
	const FLinearColor ExpectedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	TestTrue(TEXT("EditableTextBox typed text color follows normalized typed node color"),
		TypedTextBoxStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("EditableTextBox typed foreground color follows normalized typed node color"),
		TypedTextBoxStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second EditableTextBox text color follows normalized typed node color"),
		LegacyTextBoxStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second EditableTextBox foreground color follows normalized typed node color"),
		LegacyTextBoxStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestEqual(TEXT("EditableTextBox typed and legacy justification match"), TypedTextBox->GetJustification(), LegacyTextBox->GetJustification());
	TestTrue(TEXT("EditableTextBox typed and legacy icon shaping match"),
		TypedTextBox->ShapedTextOptions == LegacyTextBox->ShapedTextOptions);

	UEditableText* TypedEditableText = NewObject<UEditableText>(GetTransientPackageAsObject());
	UEditableText* LegacyEditableText = NewObject<UEditableText>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed editable text created"), TypedEditableText);
	TestNotNull(TEXT("Legacy editable text created"), LegacyEditableText);
	if (!TypedEditableText || !LegacyEditableText)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedEditableText, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyEditableText, Node);

	TestEqual(TEXT("EditableText typed and legacy text match"), TypedEditableText->GetText().ToString(), LegacyEditableText->GetText().ToString());
	TestEqual(TEXT("EditableText typed and legacy hint match"), TypedEditableText->GetHintText().ToString(), LegacyEditableText->GetHintText().ToString());
	TestEqual(TEXT("EditableText typed and legacy font size match"), TypedEditableText->GetFont().Size, LegacyEditableText->GetFont().Size);
	TestEqual(TEXT("EditableText typed and legacy typeface match"), TypedEditableText->GetFont().TypefaceFontName, LegacyEditableText->GetFont().TypefaceFontName);
	TestEqual(TEXT("EditableText typed and legacy letter spacing match"), TypedEditableText->GetFont().LetterSpacing, LegacyEditableText->GetFont().LetterSpacing);
	TestEqual(TEXT("EditableText typed and legacy font object path match"),
		TypedEditableText->GetFont().FontObject ? TypedEditableText->GetFont().FontObject->GetPathName() : FString(),
		LegacyEditableText->GetFont().FontObject ? LegacyEditableText->GetFont().FontObject->GetPathName() : FString());
	TestTrue(TEXT("EditableText typed color follows normalized typed node color"),
		TypedEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second EditableText color follows normalized typed node color"),
		LegacyEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestEqual(TEXT("EditableText typed and legacy justification match"), TypedEditableText->GetJustification(), LegacyEditableText->GetJustification());
	TestTrue(TEXT("EditableText typed and legacy icon shaping match"),
		TypedEditableText->ShapedTextOptions == LegacyEditableText->ShapedTextOptions);

	UEditableTextBox* TypedBehaviorTextBox = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	UEditableText* TypedBehaviorEditableText = NewObject<UEditableText>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed behavior editable text box created"), TypedBehaviorTextBox);
	TestNotNull(TEXT("Typed behavior editable text created"), TypedBehaviorEditableText);
	if (!TypedBehaviorTextBox || !TypedBehaviorEditableText)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedBehaviorTextBox, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(TypedBehaviorEditableText, Node);

	TestTrue(TEXT("Typed editable text box applies MinimumDesiredWidth"),
		FMath::IsNearlyEqual(
			EditableTextBoxMinimumDesiredWidthProperty->GetPropertyValue_InContainer(TypedBehaviorTextBox),
			123.5f,
			0.001f));
	TestTrue(TEXT("Typed editable text applies MinimumDesiredWidth"),
		FMath::IsNearlyEqual(
			EditableTextMinimumDesiredWidthProperty->GetPropertyValue_InContainer(TypedBehaviorEditableText),
			123.5f,
			0.001f));

	FUMGWidgetNode EmptyTypedNode;
	EmptyTypedNode.Text.Text = TEXT("");
	EmptyTypedNode.Text.HintText = TEXT("");

	TestTrue(TEXT("Typed empty text is retained instead of dropped"),
		EmptyTypedNode.Text.Text.IsSet() && EmptyTypedNode.Text.Text.GetValue().IsEmpty());
	TestTrue(TEXT("Typed empty hint is retained instead of dropped"),
		EmptyTypedNode.Text.HintText.IsSet() && EmptyTypedNode.Text.HintText.GetValue().IsEmpty());

	UEditableTextBox* EmptyTypedTextBox = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	UEditableText* EmptyTypedEditableText = NewObject<UEditableText>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed empty editable text box created"), EmptyTypedTextBox);
	TestNotNull(TEXT("Typed empty editable text created"), EmptyTypedEditableText);
	if (!EmptyTypedTextBox || !EmptyTypedEditableText)
	{
		return false;
	}

	EmptyTypedTextBox->SetText(FText::FromString(TEXT("Existing Text")));
	EmptyTypedTextBox->SetHintText(FText::FromString(TEXT("Existing Hint")));
	EmptyTypedEditableText->SetText(FText::FromString(TEXT("Existing Text")));
	EmptyTypedEditableText->SetHintText(FText::FromString(TEXT("Existing Hint")));

	FUMGWidgetUtils::ApplyWidgetProperties(EmptyTypedTextBox, EmptyTypedNode);
	FUMGWidgetUtils::ApplyWidgetProperties(EmptyTypedEditableText, EmptyTypedNode);

	TestTrue(TEXT("Typed empty text clears EditableTextBox text"), EmptyTypedTextBox->GetText().ToString().IsEmpty());
	TestTrue(TEXT("Typed empty hint clears EditableTextBox hint"), EmptyTypedTextBox->GetHintText().ToString().IsEmpty());
	TestTrue(TEXT("Typed empty text clears EditableText text"), EmptyTypedEditableText->GetText().ToString().IsEmpty());
	TestTrue(TEXT("Typed empty hint clears EditableText hint"), EmptyTypedEditableText->GetHintText().ToString().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextTypedPayloadExplicitFalseFormattingFlagsTest,
	"Design2UMG.PropertyApplier.TextExplicitFalseFormattingFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextTypedPayloadExplicitFalseFormattingFlagsTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("TextBlock");
	Node.Text.bAutoWrapText = false;
	Node.Text.bUnderline = false;
	Node.Text.bStrikethrough = false;

	TestTrue(TEXT("Typed text payload keeps explicit false AutoWrapText"),
		Node.Text.bAutoWrapText.IsSet() && !Node.Text.bAutoWrapText.GetValue());
	TestTrue(TEXT("Typed text payload keeps explicit false Underline"),
		Node.Text.bUnderline.IsSet() && !Node.Text.bUnderline.GetValue());
	TestTrue(TEXT("Typed text payload keeps explicit false Strikethrough"),
		Node.Text.bStrikethrough.IsSet() && !Node.Text.bStrikethrough.GetValue());

	UTextBlock* TypedWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	UTextBlock* PayloadWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed text widget created"), TypedWidget);
	TestNotNull(TEXT("Payload text widget created"), PayloadWidget);
	if (!TypedWidget || !PayloadWidget)
	{
		return false;
	}

	TypedWidget->SetAutoWrapText(true);
	FUMGWidgetUtils::ApplyWidgetProperties(TypedWidget, Node);
	TestFalse(TEXT("Typed text widget applies explicit false AutoWrapText"), TypedWidget->GetAutoWrapText());

	PayloadWidget->SetAutoWrapText(true);
	FUMGWidgetUtils::ApplyWidgetProperties(PayloadWidget, Node);
	TestFalse(TEXT("Payload text widget applies explicit false AutoWrapText"), PayloadWidget->GetAutoWrapText());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiLineEditableTextFamilyUsesTypedTextPayloadParityTest,
	"Design2UMG.PropertyApplier.MultiLineEditableTextFamilyUsesTypedTextPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMultiLineEditableTextFamilyUsesTypedTextPayloadParityTest::RunTest(const FString& Parameters)
{
	const FStructProperty* MarginProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	const FFloatProperty* LineHeightProperty = FindFProperty<FFloatProperty>(UTextLayoutWidget::StaticClass(), TEXT("LineHeightPercentage"));
	const FByteProperty* JustificationProperty = FindFProperty<FByteProperty>(UTextLayoutWidget::StaticClass(), TEXT("Justification"));
	const FStructProperty* ShapedTextOptionsProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("ShapedTextOptions"));
	TestNotNull(TEXT("Text layout margin property exists"), MarginProperty);
	TestNotNull(TEXT("Text layout line-height property exists"), LineHeightProperty);
	TestNotNull(TEXT("Text layout justification property exists"), JustificationProperty);
	TestNotNull(TEXT("Text layout shaped-text property exists"), ShapedTextOptionsProperty);
	if (!MarginProperty || !LineHeightProperty || !JustificationProperty || !ShapedTextOptionsProperty)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Text.Text = TEXT("Multi-line editable text");
	Node.Text.HintText = TEXT("Multi-line hint");
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.IconName = TEXT("glyph_multiline");
	Node.Text.Justification = TEXT("Right");
	Node.Text.bAutoWrapText = true;
	Node.Text.WrapTextAt = 220.0f;
	Node.Text.LineHeightPercentage = 1.4f;
	Node.Text.Color = FLinearColor(0.15f, 0.55f, 0.35f, 1.0f);
	Node.TextStyle.FontSize = 17.0f;
	Node.TextStyle.SourceFontSize = 17.0f;
	Node.TextStyle.LetterSpacing = 0.75f;
	Node.WidgetStyle.Padding = FMargin(4.0f, 5.0f, 6.0f, 7.0f);

	auto ReadLayoutState = [&](UTextLayoutWidget* Widget, FMargin& OutMargin, float& OutLineHeight, ETextJustify::Type& OutJustification, FShapedTextOptions& OutShaping)
	{
		const FMargin* MarginValue = MarginProperty->ContainerPtrToValuePtr<FMargin>(Widget);
		OutMargin = MarginValue ? *MarginValue : FMargin();
		OutLineHeight = LineHeightProperty->GetPropertyValue_InContainer(Widget);
		OutJustification = static_cast<ETextJustify::Type>(
			JustificationProperty->GetUnsignedIntPropertyValue(JustificationProperty->ContainerPtrToValuePtr<void>(Widget)));
		const FShapedTextOptions* ShapingValue = ShapedTextOptionsProperty->ContainerPtrToValuePtr<FShapedTextOptions>(Widget);
		OutShaping = ShapingValue ? *ShapingValue : FShapedTextOptions();
	};

	UMultiLineEditableTextBox* TypedTextBox = NewObject<UMultiLineEditableTextBox>(GetTransientPackageAsObject());
	UMultiLineEditableTextBox* LegacyTextBox = NewObject<UMultiLineEditableTextBox>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed multi-line editable text box created"), TypedTextBox);
	TestNotNull(TEXT("Legacy multi-line editable text box created"), LegacyTextBox);
	if (!TypedTextBox || !LegacyTextBox)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedTextBox, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyTextBox, Node);

	FMargin TypedTextBoxMargin;
	FMargin LegacyTextBoxMargin;
	float TypedTextBoxLineHeight = 0.0f;
	float LegacyTextBoxLineHeight = 0.0f;
	ETextJustify::Type TypedTextBoxJustification = ETextJustify::Left;
	ETextJustify::Type LegacyTextBoxJustification = ETextJustify::Left;
	FShapedTextOptions TypedTextBoxShaping;
	FShapedTextOptions LegacyTextBoxShaping;
	ReadLayoutState(TypedTextBox, TypedTextBoxMargin, TypedTextBoxLineHeight, TypedTextBoxJustification, TypedTextBoxShaping);
	ReadLayoutState(LegacyTextBox, LegacyTextBoxMargin, LegacyTextBoxLineHeight, LegacyTextBoxJustification, LegacyTextBoxShaping);

	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy text match"), TypedTextBox->GetText().ToString(), LegacyTextBox->GetText().ToString());
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy hint match"), TypedTextBox->GetHintText().ToString(), LegacyTextBox->GetHintText().ToString());
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy font size match"), TypedTextBox->WidgetStyle.TextStyle.Font.Size, LegacyTextBox->WidgetStyle.TextStyle.Font.Size);
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy typeface match"), TypedTextBox->WidgetStyle.TextStyle.Font.TypefaceFontName, LegacyTextBox->WidgetStyle.TextStyle.Font.TypefaceFontName);
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy letter spacing match"), TypedTextBox->WidgetStyle.TextStyle.Font.LetterSpacing, LegacyTextBox->WidgetStyle.TextStyle.Font.LetterSpacing);
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy font object path match"),
		TypedTextBox->WidgetStyle.TextStyle.Font.FontObject ? TypedTextBox->WidgetStyle.TextStyle.Font.FontObject->GetPathName() : FString(),
		LegacyTextBox->WidgetStyle.TextStyle.Font.FontObject ? LegacyTextBox->WidgetStyle.TextStyle.Font.FontObject->GetPathName() : FString());
	const FLinearColor ExpectedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	TestTrue(TEXT("MultiLineEditableTextBox typed text color follows normalized typed node color"),
		TypedTextBox->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("MultiLineEditableTextBox typed foreground color follows normalized typed node color"),
		TypedTextBox->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second MultiLineEditableTextBox text color follows normalized typed node color"),
		LegacyTextBox->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second MultiLineEditableTextBox foreground color follows normalized typed node color"),
		LegacyTextBox->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("MultiLineEditableTextBox typed and legacy auto-wrap match"), TypedTextBox->GetAutoWrapText() == LegacyTextBox->GetAutoWrapText());
	TestTrue(TEXT("MultiLineEditableTextBox typed and legacy wrap width match"), FMath::IsNearlyEqual(TypedTextBox->GetWrapTextAt(), LegacyTextBox->GetWrapTextAt(), 0.001f));
	TestTrue(TEXT("MultiLineEditableTextBox typed and legacy line height match"), FMath::IsNearlyEqual(TypedTextBoxLineHeight, LegacyTextBoxLineHeight, 0.001f));
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy justification match"), TypedTextBoxJustification, LegacyTextBoxJustification);
	TestTrue(TEXT("MultiLineEditableTextBox typed and legacy margin match"),
		FMath::IsNearlyEqual(TypedTextBoxMargin.Left, LegacyTextBoxMargin.Left, 0.001f)
		&& FMath::IsNearlyEqual(TypedTextBoxMargin.Top, LegacyTextBoxMargin.Top, 0.001f)
		&& FMath::IsNearlyEqual(TypedTextBoxMargin.Right, LegacyTextBoxMargin.Right, 0.001f)
		&& FMath::IsNearlyEqual(TypedTextBoxMargin.Bottom, LegacyTextBoxMargin.Bottom, 0.001f));
	TestTrue(TEXT("MultiLineEditableTextBox typed and legacy icon shaping match"), TypedTextBoxShaping == LegacyTextBoxShaping);

	UMultiLineEditableText* TypedEditableText = NewObject<UMultiLineEditableText>(GetTransientPackageAsObject());
	UMultiLineEditableText* LegacyEditableText = NewObject<UMultiLineEditableText>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed multi-line editable text created"), TypedEditableText);
	TestNotNull(TEXT("Legacy multi-line editable text created"), LegacyEditableText);
	if (!TypedEditableText || !LegacyEditableText)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedEditableText, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyEditableText, Node);

	FMargin TypedEditableTextMargin;
	FMargin LegacyEditableTextMargin;
	float TypedEditableTextLineHeight = 0.0f;
	float LegacyEditableTextLineHeight = 0.0f;
	ETextJustify::Type TypedEditableTextJustification = ETextJustify::Left;
	ETextJustify::Type LegacyEditableTextJustification = ETextJustify::Left;
	FShapedTextOptions TypedEditableTextShaping;
	FShapedTextOptions LegacyEditableTextShaping;
	ReadLayoutState(TypedEditableText, TypedEditableTextMargin, TypedEditableTextLineHeight, TypedEditableTextJustification, TypedEditableTextShaping);
	ReadLayoutState(LegacyEditableText, LegacyEditableTextMargin, LegacyEditableTextLineHeight, LegacyEditableTextJustification, LegacyEditableTextShaping);

	TestEqual(TEXT("MultiLineEditableText typed and legacy text match"), TypedEditableText->GetText().ToString(), LegacyEditableText->GetText().ToString());
	TestEqual(TEXT("MultiLineEditableText typed and legacy hint match"), TypedEditableText->GetHintText().ToString(), LegacyEditableText->GetHintText().ToString());
	TestEqual(TEXT("MultiLineEditableText typed and legacy font size match"), TypedEditableText->GetFont().Size, LegacyEditableText->GetFont().Size);
	TestEqual(TEXT("MultiLineEditableText typed and legacy typeface match"), TypedEditableText->GetFont().TypefaceFontName, LegacyEditableText->GetFont().TypefaceFontName);
	TestEqual(TEXT("MultiLineEditableText typed and legacy letter spacing match"), TypedEditableText->GetFont().LetterSpacing, LegacyEditableText->GetFont().LetterSpacing);
	TestEqual(TEXT("MultiLineEditableText typed and legacy font object path match"),
		TypedEditableText->GetFont().FontObject ? TypedEditableText->GetFont().FontObject->GetPathName() : FString(),
		LegacyEditableText->GetFont().FontObject ? LegacyEditableText->GetFont().FontObject->GetPathName() : FString());
	TestTrue(TEXT("MultiLineEditableText typed color follows normalized typed node color"),
		TypedEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second MultiLineEditableText color follows normalized typed node color"),
		LegacyEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("MultiLineEditableText typed and legacy auto-wrap match"), TypedEditableText->GetAutoWrapText() == LegacyEditableText->GetAutoWrapText());
	TestTrue(TEXT("MultiLineEditableText typed and legacy wrap width match"), FMath::IsNearlyEqual(TypedEditableText->GetWrapTextAt(), LegacyEditableText->GetWrapTextAt(), 0.001f));
	TestTrue(TEXT("MultiLineEditableText typed and legacy line height match"), FMath::IsNearlyEqual(TypedEditableTextLineHeight, LegacyEditableTextLineHeight, 0.001f));
	TestEqual(TEXT("MultiLineEditableText typed and legacy justification match"), TypedEditableTextJustification, LegacyEditableTextJustification);
	TestTrue(TEXT("MultiLineEditableText typed and legacy margin match"),
		FMath::IsNearlyEqual(TypedEditableTextMargin.Left, LegacyEditableTextMargin.Left, 0.001f)
		&& FMath::IsNearlyEqual(TypedEditableTextMargin.Top, LegacyEditableTextMargin.Top, 0.001f)
		&& FMath::IsNearlyEqual(TypedEditableTextMargin.Right, LegacyEditableTextMargin.Right, 0.001f)
		&& FMath::IsNearlyEqual(TypedEditableTextMargin.Bottom, LegacyEditableTextMargin.Bottom, 0.001f));
	TestTrue(TEXT("MultiLineEditableText typed and legacy icon shaping match"), TypedEditableTextShaping == LegacyEditableTextShaping);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRichTextBlockStyleRowsApplicationTest,
	"Design2UMG.PropertyApplier.RichTextBlockStyleRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRichTextBlockStyleRowsApplicationTest::RunTest(const FString& Parameters)
{
	URichTextBlock* Widget = NewObject<URichTextBlock>(GetTransientPackageAsObject());
	TestTrue(TEXT("RichTextBlock created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	FTextBlockStyle PreconfiguredStyle = FTextBlockStyle::GetDefault();
	PreconfiguredStyle.ShadowColorAndOpacity = FLinearColor(0.8f, 0.1f, 0.2f, 0.5f);
	PreconfiguredStyle.ShadowOffset = FVector2D(2.0f, 3.0f);
	Widget->SetDefaultTextStyle(PreconfiguredStyle);

	FUMGWidgetNode Node;
	Node.Type = TEXT("RichTextBlock");
	Node.Text.Text = TEXT("<d2u_run_0>Hello </><d2u_run_1 href=\"https://docs.example.com\">Docs</>");
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.Justification = TEXT("Center");
	Node.Text.bAutoWrapText = true;
	Node.Text.WrapTextAt = 180.0f;
	Node.Text.LineHeightPercentage = 1.25f;
	Node.Text.Color = FLinearColor(0.1f, 0.2f, 0.3f, 1.0f);
	Node.Text.RichTextStyleRowsJson = TEXT("{\"d2u_run_0\":{\"Color\":\"#112233FF\"},\"d2u_run_1\":{\"FontSize\":22.5,\"DesignFontSize\":22.5,\"Color\":\"#224466FF\",\"Underline\":true,\"Href\":\"https://docs.example.com\"},\"d2u_run_2\":{\"FontSize\":0.375,\"DesignFontSize\":0.5}}");
	Node.TextStyle.FontSize = 18.0f;
	Node.TextStyle.SourceFontSize = 18.0f;
	Node.TextStyle.LetterSpacing = 1.5f;
	Node.WidgetStyle.Padding = FMargin(3.0f, 4.0f, 5.0f, 6.0f);
	Node.WidgetStyle.ToolTipText = TEXT("https://docs.example.com");

	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	TestEqual(TEXT("Rich text source text applied"), Widget->GetText().ToString(), TEXT("<d2u_run_0>Hello </><d2u_run_1 href=\"https://docs.example.com\">Docs</>"));
	TestEqual(TEXT("Rich text tooltip applied"), Widget->GetToolTipText().ToString(), TEXT("https://docs.example.com"));
	TestTrue(TEXT("Rich text auto wrap applied"), Widget->GetAutoWrapText());
	TestTrue(TEXT("Rich text wrap width applied"), FMath::IsNearlyEqual(Widget->GetWrapTextAt(), 180.0f, 0.001f));
	const FStructProperty* MarginProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	TestTrue(TEXT("Rich text margin property found"), MarginProperty != nullptr);
	if (MarginProperty)
	{
		const FMargin* MarginValue = MarginProperty->ContainerPtrToValuePtr<FMargin>(Widget);
		TestTrue(TEXT("Rich text padding applied as internal margin"),
			MarginValue
			&& FMath::IsNearlyEqual(MarginValue->Left, 3.0f, 0.001f)
			&& FMath::IsNearlyEqual(MarginValue->Top, 4.0f, 0.001f)
			&& FMath::IsNearlyEqual(MarginValue->Right, 5.0f, 0.001f)
			&& FMath::IsNearlyEqual(MarginValue->Bottom, 6.0f, 0.001f));
	}

	const FTextBlockStyle& DefaultStyle = Widget->GetCurrentDefaultTextStyle();
	TestEqual(TEXT("Rich text default font size applied"), DefaultStyle.Font.Size, 18.0f);
	const FLinearColor ExpectedDefaultColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	TestTrue(TEXT("Rich text default color applied"),
		DefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedDefaultColor, 0.0001f));
	TestTrue(TEXT("Rich text default style preserves preconfigured shadow color"),
		DefaultStyle.ShadowColorAndOpacity.Equals(PreconfiguredStyle.ShadowColorAndOpacity, 0.0001f));
	TestTrue(TEXT("Rich text default style preserves preconfigured shadow offset"),
		DefaultStyle.ShadowOffset == PreconfiguredStyle.ShadowOffset);

	UDataTable* StyleSet = Widget->GetTextStyleSet();
	TestTrue(TEXT("Rich text style set created"), StyleSet != nullptr);
	if (!StyleSet)
	{
		return false;
	}
	TestTrue(TEXT("Rich text generated style set is widget-owned"), StyleSet->GetOuter() == Widget);
	TestFalse(TEXT("Rich text generated style set is not transient"), StyleSet->HasAnyFlags(RF_Transient));

	const FRichTextStyleRow* BaseRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_0"), TEXT("RichTextBlockStyleRowsApplication"));
	TestNotNull(TEXT("Base row exists"), BaseRow);
	if (!BaseRow)
	{
		return false;
	}
	TestEqual(TEXT("Sparse base row inherits default font size"), BaseRow->TextStyle.Font.Size, 18.0f);
	TestTrue(TEXT("Sparse base row preserves base shadow color"),
		BaseRow->TextStyle.ShadowColorAndOpacity.Equals(PreconfiguredStyle.ShadowColorAndOpacity, 0.0001f));

	const FRichTextStyleRow* LinkRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_1"), TEXT("RichTextBlockStyleRowsApplication"));
	TestNotNull(TEXT("Link row exists"), LinkRow);
	if (!LinkRow)
	{
		return false;
	}

	TestTrue(TEXT("Link row preserves the exact fractional font size"), FMath::IsNearlyEqual(LinkRow->TextStyle.Font.Size, 22.5f, 0.001f));
	TestTrue(TEXT("Link row underline brush created"), LinkRow->TextStyle.UnderlineBrush.DrawAs != ESlateBrushDrawType::NoDrawType);
	TestTrue(TEXT("Link row color applied"),
		LinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(
			FUMGValueParser::ParseColor(TEXT("#224466FF")),
			0.0001f));
	TestTrue(TEXT("Link row preserves base shadow color"),
		LinkRow->TextStyle.ShadowColorAndOpacity.Equals(PreconfiguredStyle.ShadowColorAndOpacity, 0.0001f));
	TestTrue(TEXT("Link row preserves base shadow offset"),
		LinkRow->TextStyle.ShadowOffset == PreconfiguredStyle.ShadowOffset);

	const FRichTextStyleRow* SubpointRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_2"), TEXT("RichTextBlockStyleRowsApplication"));
	TestNotNull(TEXT("Subpoint row exists"), SubpointRow);
	if (!SubpointRow)
	{
		return false;
	}
	TestTrue(TEXT("Rich text application preserves 0.375 point row sizes"),
		FMath::IsNearlyEqual(SubpointRow->TextStyle.Font.Size, 0.375f, 0.001f));

	URichTextBlock* LegacyWidget = NewObject<URichTextBlock>(GetTransientPackageAsObject());
	TestTrue(TEXT("Legacy RichTextBlock created"), LegacyWidget != nullptr);
	if (!LegacyWidget)
	{
		return false;
	}
	LegacyWidget->SetDefaultTextStyle(PreconfiguredStyle);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyWidget, Node);

	TestEqual(TEXT("Typed and legacy rich text markup match"),
		Widget->GetText().ToString(),
		LegacyWidget->GetText().ToString());
	TestEqual(TEXT("Typed and legacy rich text tooltip match"),
		Widget->GetToolTipText().ToString(),
		LegacyWidget->GetToolTipText().ToString());
	TestTrue(TEXT("Typed and legacy rich text wrap width match"),
		FMath::IsNearlyEqual(Widget->GetWrapTextAt(), LegacyWidget->GetWrapTextAt(), 0.001f));

	const FTextBlockStyle& LegacyDefaultStyle = LegacyWidget->GetCurrentDefaultTextStyle();
	TestEqual(TEXT("Typed and legacy rich text default font size match"),
		DefaultStyle.Font.Size,
		LegacyDefaultStyle.Font.Size);
	TestTrue(TEXT("Typed rich text default color follows normalized typed node color"),
		DefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedDefaultColor, 0.0001f));
	TestTrue(TEXT("Second rich text default color follows normalized typed node color"),
		LegacyDefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedDefaultColor, 0.0001f));
	TestTrue(TEXT("Typed and legacy rich text default shadow color match"),
		DefaultStyle.ShadowColorAndOpacity.Equals(LegacyDefaultStyle.ShadowColorAndOpacity, 0.0001f));
	TestTrue(TEXT("Typed and legacy rich text default shadow offset match"),
		DefaultStyle.ShadowOffset == LegacyDefaultStyle.ShadowOffset);

	UDataTable* LegacyStyleSet = LegacyWidget->GetTextStyleSet();
	TestNotNull(TEXT("Legacy rich text style set exists"), LegacyStyleSet);
	if (!LegacyStyleSet)
	{
		return false;
	}

	const FRichTextStyleRow* LegacyLinkRow = LegacyStyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_1"), TEXT("RichTextBlockStyleRowsParity"));
	TestNotNull(TEXT("Legacy link row exists"), LegacyLinkRow);
	if (!LegacyLinkRow)
	{
		return false;
	}

	TestEqual(TEXT("Typed and legacy rich text row font size match"),
		LinkRow->TextStyle.Font.Size,
		LegacyLinkRow->TextStyle.Font.Size);
	TestTrue(TEXT("Typed and legacy rich text row color match"),
		LinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(
			LegacyLinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor(),
			0.0001f));
	TestEqual(TEXT("Typed and legacy rich text row underline draw-as match"),
		LinkRow->TextStyle.UnderlineBrush.DrawAs,
		LegacyLinkRow->TextStyle.UnderlineBrush.DrawAs);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextFamilyTypedJsonRoundTripTest,
	"Design2UMG.UMGJson.TextFamilyTypedPayloadRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextFamilyTypedJsonRoundTripTest::RunTest(const FString& Parameters)
{
	const FStructProperty* MarginProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	const FFloatProperty* LineHeightProperty = FindFProperty<FFloatProperty>(UTextLayoutWidget::StaticClass(), TEXT("LineHeightPercentage"));
	const FByteProperty* JustificationProperty = FindFProperty<FByteProperty>(UTextLayoutWidget::StaticClass(), TEXT("Justification"));
	const FStructProperty* ShapedTextOptionsProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("ShapedTextOptions"));
	TestNotNull(TEXT("Text layout margin property exists"), MarginProperty);
	TestNotNull(TEXT("Text layout line-height property exists"), LineHeightProperty);
	TestNotNull(TEXT("Text layout justification property exists"), JustificationProperty);
	TestNotNull(TEXT("Text layout shaped-text property exists"), ShapedTextOptionsProperty);
	if (!MarginProperty || !LineHeightProperty || !JustificationProperty || !ShapedTextOptionsProperty)
	{
		return false;
	}

	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_TextFamilyJsonRoundTrip");
	SourceData.RootWidget.Type = TEXT("CanvasPanel");
	SourceData.RootWidget.Id = TEXT("root");
	SourceData.RootWidget.Name = TEXT("Root");

	FUMGWidgetNode SourceNode;
	SourceNode.Type = TEXT("MultiLineEditableTextBox");
	SourceNode.Id = TEXT("typed_text_family");
	SourceNode.Name = TEXT("TypedTextFamily");
	SourceNode.Text.Text = TEXT("Round-trip text payload");
	SourceNode.Text.HintText = TEXT("Round-trip hint");
	SourceNode.Text.FontFamily = TEXT("/Game/Design2UMG/Fonts/Geist.Geist");
	SourceNode.Text.TypefaceFontName = TEXT("Regular");
	SourceNode.Text.Justification = TEXT("Center");
	SourceNode.Text.bAutoWrapText = true;
	SourceNode.Text.WrapTextAt = 240.0f;
	SourceNode.Text.LineHeightPercentage = 1.3f;
	SourceNode.Text.Color = FLinearColor(0.25f, 0.45f, 0.65f, 1.0f);
	SourceNode.Text.IconName = TEXT("glyph_roundtrip");
	SourceNode.TextStyle.FontSize = 18.0f;
	SourceNode.TextStyle.SourceFontSize = 18.0f;
	SourceNode.TextStyle.LetterSpacing = 1.25f;
	SourceNode.WidgetStyle.Padding = FMargin(3.0f, 4.0f, 5.0f, 6.0f);
	SourceData.RootWidget.Children.Add(SourceNode);

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestTrue(TEXT("Serialized text family emits TextStyle payload"), Json.Contains(TEXT("\"TextStyle\"")));
	TestTrue(TEXT("Serialized text family emits TextData payload"), Json.Contains(TEXT("\"TextData\"")));
	TestTrue(TEXT("Serialized text family emits rich typed hint field"), Json.Contains(TEXT("\"HintText\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Text family JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	const FUMGWidgetNode* ParsedNode = FindWidgetById(ParsedData.RootWidget, TEXT("typed_text_family"));
	TestNotNull(TEXT("Parsed text family node exists"), ParsedNode);
	if (!ParsedNode)
	{
		return false;
	}

	TestEqual(TEXT("Parsed text preserved"), ParsedNode->Text.Text, SourceNode.Text.Text);
	TestEqual(TEXT("Parsed hint preserved"), ParsedNode->Text.HintText, SourceNode.Text.HintText);
	TestEqual(TEXT("Parsed font family preserved"), ParsedNode->Text.FontFamily, SourceNode.Text.FontFamily);
	TestEqual(TEXT("Parsed typeface preserved"), ParsedNode->Text.TypefaceFontName, SourceNode.Text.TypefaceFontName);
	TestEqual(TEXT("Parsed justification preserved"), ParsedNode->Text.Justification, SourceNode.Text.Justification);
	TestTrue(TEXT("Parsed auto wrap flag preserved"), ParsedNode->Text.bAutoWrapText.IsSet() && ParsedNode->Text.bAutoWrapText.GetValue() == SourceNode.Text.bAutoWrapText.GetValue());
	TestTrue(TEXT("Parsed wrap width preserved"), ParsedNode->Text.WrapTextAt.IsSet() && FMath::IsNearlyEqual(ParsedNode->Text.WrapTextAt.GetValue(), SourceNode.Text.WrapTextAt.GetValue(), 0.001f));
	TestTrue(TEXT("Parsed line height preserved"), ParsedNode->Text.LineHeightPercentage.IsSet() && FMath::IsNearlyEqual(ParsedNode->Text.LineHeightPercentage.GetValue(), SourceNode.Text.LineHeightPercentage.GetValue(), 0.001f));
	TestTrue(TEXT("Parsed color preserved"), ParsedNode->Text.Color.IsSet() && ParsedNode->Text.Color.GetValue().Equals(SourceNode.Text.Color.GetValue(), 0.01f));
	TestTrue(TEXT("Parsed font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.FontSize, SourceNode.TextStyle.FontSize, 0.001f));
	TestTrue(TEXT("Parsed source font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.SourceFontSize, SourceNode.TextStyle.SourceFontSize, 0.001f));
	TestTrue(TEXT("Parsed letter spacing preserved"),
		ParsedNode->TextStyle.LetterSpacing.IsSet()
		&& FMath::IsNearlyEqual(ParsedNode->TextStyle.LetterSpacing.GetValue(), SourceNode.TextStyle.LetterSpacing.GetValue(), 0.001f));
	TestTrue(TEXT("Parsed padding preserved"),
		ParsedNode->WidgetStyle.Padding.IsSet()
		&& FMath::IsNearlyEqual(ParsedNode->WidgetStyle.Padding->Left, SourceNode.WidgetStyle.Padding->Left, 0.001f)
		&& FMath::IsNearlyEqual(ParsedNode->WidgetStyle.Padding->Top, SourceNode.WidgetStyle.Padding->Top, 0.001f)
		&& FMath::IsNearlyEqual(ParsedNode->WidgetStyle.Padding->Right, SourceNode.WidgetStyle.Padding->Right, 0.001f)
		&& FMath::IsNearlyEqual(ParsedNode->WidgetStyle.Padding->Bottom, SourceNode.WidgetStyle.Padding->Bottom, 0.001f));

	auto ReadLayoutState = [&](UTextLayoutWidget* Widget, FMargin& OutMargin, float& OutLineHeight, ETextJustify::Type& OutJustification, FShapedTextOptions& OutShaping)
	{
		const FMargin* MarginValue = MarginProperty->ContainerPtrToValuePtr<FMargin>(Widget);
		OutMargin = MarginValue ? *MarginValue : FMargin();
		OutLineHeight = LineHeightProperty->GetPropertyValue_InContainer(Widget);
		OutJustification = static_cast<ETextJustify::Type>(
			JustificationProperty->GetUnsignedIntPropertyValue(JustificationProperty->ContainerPtrToValuePtr<void>(Widget)));
		const FShapedTextOptions* ShapingValue = ShapedTextOptionsProperty->ContainerPtrToValuePtr<FShapedTextOptions>(Widget);
		OutShaping = ShapingValue ? *ShapingValue : FShapedTextOptions();
	};

	UMultiLineEditableTextBox* SourceWidget = NewObject<UMultiLineEditableTextBox>(GetTransientPackageAsObject());
	UMultiLineEditableTextBox* ParsedWidget = NewObject<UMultiLineEditableTextBox>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Source text family widget created"), SourceWidget);
	TestNotNull(TEXT("Parsed text family widget created"), ParsedWidget);
	if (!SourceWidget || !ParsedWidget)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(SourceWidget, SourceNode);
	FUMGWidgetUtils::ApplyWidgetProperties(ParsedWidget, *ParsedNode);

	FMargin SourceMargin;
	FMargin ParsedMargin;
	float SourceLineHeight = 0.0f;
	float ParsedLineHeight = 0.0f;
	ETextJustify::Type SourceJustification = ETextJustify::Left;
	ETextJustify::Type ParsedJustification = ETextJustify::Left;
	FShapedTextOptions SourceShaping;
	FShapedTextOptions ParsedShaping;
	ReadLayoutState(SourceWidget, SourceMargin, SourceLineHeight, SourceJustification, SourceShaping);
	ReadLayoutState(ParsedWidget, ParsedMargin, ParsedLineHeight, ParsedJustification, ParsedShaping);

	TestEqual(TEXT("Round-trip widget text matches"), ParsedWidget->GetText().ToString(), SourceWidget->GetText().ToString());
	TestEqual(TEXT("Round-trip widget hint matches"), ParsedWidget->GetHintText().ToString(), SourceWidget->GetHintText().ToString());
	TestEqual(TEXT("Round-trip widget font size matches"), ParsedWidget->WidgetStyle.TextStyle.Font.Size, SourceWidget->WidgetStyle.TextStyle.Font.Size);
	TestEqual(TEXT("Round-trip widget typeface matches"), ParsedWidget->WidgetStyle.TextStyle.Font.TypefaceFontName, SourceWidget->WidgetStyle.TextStyle.Font.TypefaceFontName);
	TestEqual(TEXT("Round-trip widget letter spacing matches"), ParsedWidget->WidgetStyle.TextStyle.Font.LetterSpacing, SourceWidget->WidgetStyle.TextStyle.Font.LetterSpacing);
	TestEqual(TEXT("Round-trip widget font object path matches"),
		ParsedWidget->WidgetStyle.TextStyle.Font.FontObject ? ParsedWidget->WidgetStyle.TextStyle.Font.FontObject->GetPathName() : FString(),
		SourceWidget->WidgetStyle.TextStyle.Font.FontObject ? SourceWidget->WidgetStyle.TextStyle.Font.FontObject->GetPathName() : FString());
	const FLinearColor ExpectedSourceTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(SourceNode.Text.Color.GetValue());
	const FLinearColor ExpectedParsedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(ParsedNode->Text.Color.GetValue());
	TestTrue(TEXT("Source widget text color follows normalized source node color"),
		SourceWidget->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedSourceTextColor, 0.0001f));
	TestTrue(TEXT("Parsed widget text color follows normalized parsed node color"),
		ParsedWidget->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedParsedTextColor, 0.0001f));
	TestTrue(TEXT("Source widget foreground color follows normalized source node color"),
		SourceWidget->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedSourceTextColor, 0.0001f));
	TestTrue(TEXT("Parsed widget foreground color follows normalized parsed node color"),
		ParsedWidget->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedParsedTextColor, 0.0001f));
	TestTrue(TEXT("Round-trip widget auto wrap matches"), ParsedWidget->GetAutoWrapText() == SourceWidget->GetAutoWrapText());
	TestTrue(TEXT("Round-trip widget wrap width matches"), FMath::IsNearlyEqual(ParsedWidget->GetWrapTextAt(), SourceWidget->GetWrapTextAt(), 0.001f));
	TestTrue(TEXT("Round-trip widget line height matches"), FMath::IsNearlyEqual(ParsedLineHeight, SourceLineHeight, 0.001f));
	TestEqual(TEXT("Round-trip widget justification matches"), ParsedJustification, SourceJustification);
	TestTrue(TEXT("Round-trip widget margin matches"),
		FMath::IsNearlyEqual(ParsedMargin.Left, SourceMargin.Left, 0.001f)
		&& FMath::IsNearlyEqual(ParsedMargin.Top, SourceMargin.Top, 0.001f)
		&& FMath::IsNearlyEqual(ParsedMargin.Right, SourceMargin.Right, 0.001f)
		&& FMath::IsNearlyEqual(ParsedMargin.Bottom, SourceMargin.Bottom, 0.001f));
	TestTrue(TEXT("Round-trip widget shaping matches"), ParsedShaping == SourceShaping);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextStyleLetterSpacingOptionalSemanticsTest,
	"Design2UMG.UMGJson.TextStyleLetterSpacingOptionalSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextStyleLetterSpacingOptionalSemanticsTest::RunTest(const FString& Parameters)
{
	auto SerializeSingleNode = [](const FUMGWidgetNode& Node)
	{
		FUMGWidgetData Data;
		Data.WidgetName = TEXT("WBP_LetterSpacingOptionalSemantics");
		Data.RootWidget.Type = TEXT("CanvasPanel");
		Data.RootWidget.Name = TEXT("Root");
		Data.RootWidget.Children.Add(Node);
		return FUMGJsonSerializer::Serialize(Data);
	};

	auto TryReadSerializedLetterSpacing = [](const FString& Json, float& OutValue)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* RootWidgetObject = nullptr;
		if (!RootObject->TryGetObjectField(TEXT("RootWidget"), RootWidgetObject))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (!(*RootWidgetObject)->TryGetArrayField(TEXT("Children"), Children) || Children->Num() != 1)
		{
			return false;
		}

		const TSharedPtr<FJsonObject> ChildObject = (*Children)[0]->AsObject();
		if (!ChildObject.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* TextStyleObject = nullptr;
		if (!ChildObject->TryGetObjectField(TEXT("TextStyle"), TextStyleObject))
		{
			return false;
		}

		double NumberValue = 0.0;
		if (!(*TextStyleObject)->TryGetNumberField(UMGPropertyKeys::LetterSpacing, NumberValue))
		{
			return false;
		}

		OutValue = static_cast<float>(NumberValue);
		return true;
	};

	auto ParseSingleNode = [&](const FString& Json, const TCHAR* Id, FUMGWidgetData& OutParsedData)
	{
		FString ParseError;
		TestTrue(TEXT("Letter spacing JSON parses"), FUMGJsonParser::ParseJson(Json, OutParsedData, &ParseError));
		if (!ParseError.IsEmpty())
		{
			AddInfo(ParseError);
		}
		return FindWidgetById(OutParsedData.RootWidget, Id);
	};

	FUMGWidgetNode ExplicitZeroNode;
	ExplicitZeroNode.Id = TEXT("explicit_zero_spacing");
	ExplicitZeroNode.Type = TEXT("TextBlock");
	ExplicitZeroNode.Name = TEXT("ExplicitZeroSpacing");
	ExplicitZeroNode.TextStyle.FontSize = 20.0f;
	ExplicitZeroNode.TextStyle.SourceFontSize = 20.0f;
	ExplicitZeroNode.TextStyle.LetterSpacing = 0.0f;

	FUMGWidgetNode NonZeroNode = ExplicitZeroNode;
	NonZeroNode.Id = TEXT("non_zero_spacing");
	NonZeroNode.Name = TEXT("NonZeroSpacing");
	NonZeroNode.TextStyle.LetterSpacing = 1.25f;

	FUMGWidgetNode UnsetNode = ExplicitZeroNode;
	UnsetNode.Id = TEXT("unset_spacing");
	UnsetNode.Name = TEXT("UnsetSpacing");
	UnsetNode.TextStyle.LetterSpacing.Reset();

	const FString ExplicitZeroJson = SerializeSingleNode(ExplicitZeroNode);
	float SerializedLetterSpacing = -1.0f;
	TestTrue(TEXT("Explicit zero letter spacing serializes"), TryReadSerializedLetterSpacing(ExplicitZeroJson, SerializedLetterSpacing));
	TestTrue(TEXT("Explicit zero letter spacing value preserved in JSON"), FMath::IsNearlyZero(SerializedLetterSpacing));

	FUMGWidgetData ParsedExplicitZeroData;
	const FUMGWidgetNode* ParsedExplicitZeroNode = ParseSingleNode(ExplicitZeroJson, TEXT("explicit_zero_spacing"), ParsedExplicitZeroData);
	TestNotNull(TEXT("Parsed explicit zero node exists"), ParsedExplicitZeroNode);
	TestTrue(TEXT("Parsed explicit zero letter spacing remains set"),
		ParsedExplicitZeroNode
		&& ParsedExplicitZeroNode->TextStyle.LetterSpacing.IsSet()
		&& FMath::IsNearlyZero(ParsedExplicitZeroNode->TextStyle.LetterSpacing.GetValue()));

	TestTrue(TEXT("Typed payload keeps explicit zero letter spacing"),
		ExplicitZeroNode.TextStyle.LetterSpacing.IsSet()
		&& FMath::IsNearlyZero(ExplicitZeroNode.TextStyle.LetterSpacing.GetValue()));

	UTextBlock* ExplicitZeroWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Explicit zero widget created"), ExplicitZeroWidget);
	if (ExplicitZeroWidget)
	{
		FSlateFontInfo FontInfo = ExplicitZeroWidget->GetFont();
		FontInfo.LetterSpacing = 321;
		ExplicitZeroWidget->SetFont(FontInfo);
		FUMGWidgetUtils::ApplyWidgetProperties(ExplicitZeroWidget, ExplicitZeroNode);
		TestEqual(TEXT("Explicit zero letter spacing applies"), ExplicitZeroWidget->GetFont().LetterSpacing, 0);
	}

	const FString NonZeroJson = SerializeSingleNode(NonZeroNode);
	TestTrue(TEXT("Non-zero letter spacing serializes"), TryReadSerializedLetterSpacing(NonZeroJson, SerializedLetterSpacing));
	TestTrue(TEXT("Non-zero letter spacing value preserved in JSON"), FMath::IsNearlyEqual(SerializedLetterSpacing, 1.25f, 0.001f));

	FUMGWidgetData ParsedNonZeroData;
	const FUMGWidgetNode* ParsedNonZeroNode = ParseSingleNode(NonZeroJson, TEXT("non_zero_spacing"), ParsedNonZeroData);
	TestNotNull(TEXT("Parsed non-zero node exists"), ParsedNonZeroNode);
	TestTrue(TEXT("Parsed non-zero letter spacing remains set"),
		ParsedNonZeroNode
		&& ParsedNonZeroNode->TextStyle.LetterSpacing.IsSet()
		&& FMath::IsNearlyEqual(ParsedNonZeroNode->TextStyle.LetterSpacing.GetValue(), 1.25f, 0.001f));

	UTextBlock* NonZeroWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Non-zero widget created"), NonZeroWidget);
	if (NonZeroWidget && ParsedNonZeroNode)
	{
		FUMGWidgetUtils::ApplyWidgetProperties(NonZeroWidget, *ParsedNonZeroNode);
		TestEqual(TEXT("Pixel letter spacing converts to Slate tracking"), NonZeroWidget->GetFont().LetterSpacing, 63);
	}

	FUMGWidgetNode ScaledNegativeNode = ExplicitZeroNode;
	ScaledNegativeNode.TextStyle.FontSize = 12.0f;
	ScaledNegativeNode.TextStyle.SourceFontSize = 20.0f;
	ScaledNegativeNode.TextStyle.LetterSpacing = -1.25f;
	UTextBlock* ScaledNegativeWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Scaled negative-spacing widget created"), ScaledNegativeWidget);
	if (ScaledNegativeWidget)
	{
		FUMGWidgetUtils::ApplyWidgetProperties(ScaledNegativeWidget, ScaledNegativeNode);
		TestEqual(TEXT("Design font size owns scaled negative letter-spacing conversion"), ScaledNegativeWidget->GetFont().LetterSpacing, -62);
	}

	URichTextBlock* RichTextWidget = NewObject<URichTextBlock>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Rich text spacing widget created"), RichTextWidget);
	if (RichTextWidget)
	{
		UMGTextStyleApplication::ApplyRichTextStyleRows(
			RichTextWidget,
			TEXT("{\"row\":{\"FontSize\":12.0,\"DesignFontSize\":20.0,\"LetterSpacing\":-1.25}}"));
		const UDataTable* StyleSet = RichTextWidget->GetTextStyleSet();
		const FRichTextStyleRow* Row = StyleSet
			? StyleSet->FindRow<FRichTextStyleRow>(TEXT("row"), TEXT("LetterSpacingConversion"))
			: nullptr;
		TestNotNull(TEXT("Rich text spacing row created"), Row);
		if (Row)
		{
			TestEqual(TEXT("Rich text row uses shared pixel-spacing conversion"), Row->TextStyle.Font.LetterSpacing, -62);
		}
	}

	const FString UnsetJson = SerializeSingleNode(UnsetNode);
	TestFalse(TEXT("Unset letter spacing does not serialize"), TryReadSerializedLetterSpacing(UnsetJson, SerializedLetterSpacing));

	FUMGWidgetData ParsedUnsetData;
	const FUMGWidgetNode* ParsedUnsetNode = ParseSingleNode(UnsetJson, TEXT("unset_spacing"), ParsedUnsetData);
	TestNotNull(TEXT("Parsed unset node exists"), ParsedUnsetNode);
	TestTrue(TEXT("Parsed unset letter spacing remains unset"),
		ParsedUnsetNode
		&& !ParsedUnsetNode->TextStyle.LetterSpacing.IsSet());

	TestFalse(TEXT("Typed payload omits unset letter spacing"), UnsetNode.TextStyle.LetterSpacing.IsSet());

	UTextBlock* UnsetWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Unset widget created"), UnsetWidget);
	if (UnsetWidget)
	{
		FSlateFontInfo FontInfo = UnsetWidget->GetFont();
		FontInfo.LetterSpacing = 321;
		UnsetWidget->SetFont(FontInfo);
		FUMGWidgetUtils::ApplyWidgetProperties(UnsetWidget, UnsetNode);
		TestEqual(TEXT("Unset letter spacing does not apply"), UnsetWidget->GetFont().LetterSpacing, 321);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRichTextTypedJsonRoundTripTest,
	"Design2UMG.UMGJson.RichTextTypedPayloadRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRichTextTypedJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_RichTextJsonRoundTrip");
	SourceData.RootWidget.Type = TEXT("CanvasPanel");
	SourceData.RootWidget.Id = TEXT("root");
	SourceData.RootWidget.Name = TEXT("Root");

	FUMGWidgetNode SourceNode;
	SourceNode.Type = TEXT("RichTextBlock");
	SourceNode.Id = TEXT("typed_rich_text");
	SourceNode.Name = TEXT("TypedRichText");
	SourceNode.Text.Text = TEXT("<d2u_run_0>Hello </><d2u_run_1 href=\"https://docs.example.com\">Docs</>");
	SourceNode.Text.RichTextStyleRowsJson = TEXT("{\"d2u_run_0\":{\"Color\":\"#112233FF\",\"DesignFontSize\":18.0,\"FontFamily\":\"/Game/Design2UMG/Fonts/Geist.Geist\",\"FontSize\":27.0,\"TypefaceFontName\":\"Regular\"},\"d2u_run_1\":{\"Color\":\"#224466FF\",\"DesignFontSize\":18.0,\"FontFamily\":\"/Game/Design2UMG/Fonts/Geist.Geist\",\"FontSize\":27.0,\"Href\":\"https://docs.example.com\",\"TypefaceFontName\":\"Bold\",\"Underline\":true}}");
	SourceNode.Text.FontFamily = TEXT("/Game/Design2UMG/Fonts/Geist.Geist");
	SourceNode.Text.TypefaceFontName = TEXT("Regular");
	SourceNode.Text.Justification = TEXT("Center");
	SourceNode.Text.bAutoWrapText = true;
	SourceNode.Text.WrapTextAt = 180.0f;
	SourceNode.Text.LineHeightPercentage = 1.25f;
	SourceNode.Text.Color = FLinearColor(0.1f, 0.2f, 0.3f, 1.0f);
	SourceNode.TextStyle.FontSize = 18.0f;
	SourceNode.TextStyle.SourceFontSize = 18.0f;
	SourceNode.TextStyle.LetterSpacing = 1.5f;
	SourceNode.WidgetStyle.Padding = FMargin(3.0f, 4.0f, 5.0f, 6.0f);
	SourceData.RootWidget.Children.Add(SourceNode);

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestTrue(TEXT("Serialized rich text emits rich style row payload"), Json.Contains(TEXT("d2u_run_0")) && Json.Contains(TEXT("d2u_run_1")));
	TestTrue(TEXT("Serialized rich text emits text style payload"), Json.Contains(TEXT("\"TextStyle\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Rich text JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	const FUMGWidgetNode* ParsedNode = FindWidgetById(ParsedData.RootWidget, TEXT("typed_rich_text"));
	TestNotNull(TEXT("Parsed rich text node exists"), ParsedNode);
	if (!ParsedNode)
	{
		return false;
	}

	TestEqual(TEXT("Parsed rich text markup preserved"), ParsedNode->Text.Text, SourceNode.Text.Text);
	TestEqual(TEXT("Parsed rich text style rows preserved"), ParsedNode->Text.RichTextStyleRowsJson, SourceNode.Text.RichTextStyleRowsJson);
	TestEqual(TEXT("Parsed rich text font family preserved"), ParsedNode->Text.FontFamily, SourceNode.Text.FontFamily);
	TestEqual(TEXT("Parsed rich text typeface preserved"), ParsedNode->Text.TypefaceFontName, SourceNode.Text.TypefaceFontName);
	TestTrue(TEXT("Parsed rich text wrap width preserved"), ParsedNode->Text.WrapTextAt.IsSet() && FMath::IsNearlyEqual(ParsedNode->Text.WrapTextAt.GetValue(), SourceNode.Text.WrapTextAt.GetValue(), 0.001f));
	TestTrue(TEXT("Parsed rich text line height preserved"), ParsedNode->Text.LineHeightPercentage.IsSet() && FMath::IsNearlyEqual(ParsedNode->Text.LineHeightPercentage.GetValue(), SourceNode.Text.LineHeightPercentage.GetValue(), 0.001f));
	TestTrue(TEXT("Parsed rich text font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.FontSize, SourceNode.TextStyle.FontSize, 0.001f));
	TestTrue(TEXT("Parsed rich text source font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.SourceFontSize, SourceNode.TextStyle.SourceFontSize, 0.001f));
	TestTrue(TEXT("Parsed rich text letter spacing preserved"),
		ParsedNode->TextStyle.LetterSpacing.IsSet()
		&& FMath::IsNearlyEqual(ParsedNode->TextStyle.LetterSpacing.GetValue(), SourceNode.TextStyle.LetterSpacing.GetValue(), 0.001f));

	auto BuildStyleSnapshot = [&](URichTextBlock* Widget, const TCHAR* Context) -> FString
	{
		UDataTable* StyleSet = Widget->GetTextStyleSet();
		if (!StyleSet)
		{
			return FString();
		}

		const FRichTextStyleRow* BaseRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_0"), Context);
		const FRichTextStyleRow* LinkRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_1"), Context);
		if (!BaseRow || !LinkRow)
		{
			return FString();
		}

		return FString::Printf(
			TEXT("base:%.0f:%s|link:%.0f:%s:%d"),
			BaseRow->TextStyle.Font.Size,
			*BaseRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().ToFColor(true).ToHex(),
			LinkRow->TextStyle.Font.Size,
			*LinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().ToFColor(true).ToHex(),
			static_cast<int32>(LinkRow->TextStyle.UnderlineBrush.DrawAs));
	};

	URichTextBlock* SourceWidget = NewObject<URichTextBlock>(GetTransientPackageAsObject());
	URichTextBlock* ParsedWidget = NewObject<URichTextBlock>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Source rich text widget created"), SourceWidget);
	TestNotNull(TEXT("Parsed rich text widget created"), ParsedWidget);
	if (!SourceWidget || !ParsedWidget)
	{
		return false;
	}

	const FTextBlockStyle PreconfiguredStyle = FTextBlockStyle::GetDefault();
	SourceWidget->SetDefaultTextStyle(PreconfiguredStyle);
	ParsedWidget->SetDefaultTextStyle(PreconfiguredStyle);

	FUMGWidgetUtils::ApplyWidgetProperties(SourceWidget, SourceNode);
	FUMGWidgetUtils::ApplyWidgetProperties(ParsedWidget, *ParsedNode);

	TestEqual(TEXT("Round-trip rich text markup matches"), ParsedWidget->GetText().ToString(), SourceWidget->GetText().ToString());
	TestTrue(TEXT("Round-trip rich text wrap width matches"), FMath::IsNearlyEqual(ParsedWidget->GetWrapTextAt(), SourceWidget->GetWrapTextAt(), 0.001f));
	TestEqual(TEXT("Round-trip rich text style snapshot matches"),
		BuildStyleSnapshot(ParsedWidget, TEXT("ParsedRichTextRoundTrip")),
		BuildStyleSnapshot(SourceWidget, TEXT("SourceRichTextRoundTrip")));
	return true;
}

#endif

