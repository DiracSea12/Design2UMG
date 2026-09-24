// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Typed text payload application and parity for the text widget family (TextBlock, editable text family, rich text, letter spacing, JSON round trips).

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGPropertyKeys.h"
#include "UMGTextStyleApplication.h"
#include "UMGValueParsing.h"
#include "UMGWidgetTreeBuilder.h"
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

namespace
{
	// Shared creation/reading/assertion vocabulary for the typed-text-payload family tests.

	template <typename T>
	T* NewTextFamilyWidget(FAutomationTestBase& Test, const TCHAR* CreationLabel)
	{
		T* Widget = NewObject<T>(GetTransientPackageAsObject());
		Test.TestNotNull(CreationLabel, Widget);
		return Widget;
	}

	// UTextLayoutWidget reflected layout properties shared by the parity/serialization tests.
	// UEditableText/UEditableTextBox shadow the base-class Justification with their own property,
	// so only the multi-line family reads justification through this reflection set; the
	// single-line family keeps its getter-based reads (see the state readers below).
	struct FTextLayoutProperties
	{
		const FStructProperty* Margin = nullptr;
		const FFloatProperty* LineHeight = nullptr;
		const FByteProperty* Justification = nullptr;
		const FStructProperty* ShapedTextOptions = nullptr;

		bool IsValid() const
		{
			return Margin && LineHeight && Justification && ShapedTextOptions;
		}
	};

	bool FindTextLayoutProperties(FAutomationTestBase& Test, FTextLayoutProperties& OutProperties)
	{
		OutProperties.Margin = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
		OutProperties.LineHeight = FindFProperty<FFloatProperty>(UTextLayoutWidget::StaticClass(), TEXT("LineHeightPercentage"));
		OutProperties.Justification = FindFProperty<FByteProperty>(UTextLayoutWidget::StaticClass(), TEXT("Justification"));
		OutProperties.ShapedTextOptions = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("ShapedTextOptions"));
		Test.TestNotNull(TEXT("Text layout margin property exists"), OutProperties.Margin);
		Test.TestNotNull(TEXT("Text layout line-height property exists"), OutProperties.LineHeight);
		Test.TestNotNull(TEXT("Text layout justification property exists"), OutProperties.Justification);
		Test.TestNotNull(TEXT("Text layout shaped-text property exists"), OutProperties.ShapedTextOptions);
		return OutProperties.IsValid();
	}

	void ReadTextLayoutState(
		const FTextLayoutProperties& Properties,
		UTextLayoutWidget* Widget,
		FMargin& OutMargin,
		float& OutLineHeight,
		ETextJustify::Type& OutJustification,
		FShapedTextOptions& OutShaping)
	{
		const FMargin* MarginValue = Properties.Margin->ContainerPtrToValuePtr<FMargin>(Widget);
		OutMargin = MarginValue ? *MarginValue : FMargin();
		OutLineHeight = Properties.LineHeight->GetPropertyValue_InContainer(Widget);
		OutJustification = static_cast<ETextJustify::Type>(
			Properties.Justification->GetUnsignedIntPropertyValue(Properties.Justification->ContainerPtrToValuePtr<void>(Widget)));
		const FShapedTextOptions* ShapingValue = Properties.ShapedTextOptions->ContainerPtrToValuePtr<FShapedTextOptions>(Widget);
		OutShaping = ShapingValue ? *ShapingValue : FShapedTextOptions();
	}

	// Comparable runtime state of one editable-text-family widget after property application.
	struct FTextFamilyWidgetState
	{
		FString Text;
		FString Hint;
		float FontSize = 0.0f;
		FString TypefaceFontName;
		float LetterSpacing = 0.0f;
		FString FontObjectPath;
		FLinearColor StyleTextColor = FLinearColor::White;
		FLinearColor ForegroundColor = FLinearColor::White;
		bool bAutoWrap = false;
		float WrapTextAt = 0.0f;
		float LineHeight = 0.0f;
		FMargin Margin;
		ETextJustify::Type Justification = ETextJustify::Left;
		FShapedTextOptions Shaping;
	};

	FTextFamilyWidgetState ReadEditableTextBoxState(UEditableTextBox* Widget)
	{
		FTextFamilyWidgetState State;
		State.Text = Widget->GetText().ToString();
		State.Hint = Widget->GetHintText().ToString();
		const FEditableTextBoxStyle& Style = Widget->GetWidgetStyle();
		State.FontSize = Style.TextStyle.Font.Size;
		State.TypefaceFontName = Style.TextStyle.Font.TypefaceFontName.ToString();
		State.LetterSpacing = Style.TextStyle.Font.LetterSpacing;
		State.FontObjectPath = Style.TextStyle.Font.FontObject ? Style.TextStyle.Font.FontObject->GetPathName() : FString();
		State.StyleTextColor = Style.TextStyle.ColorAndOpacity.GetSpecifiedColor();
		State.ForegroundColor = Style.ForegroundColor.GetSpecifiedColor();
		State.Justification = Widget->GetJustification();
		State.Shaping = Widget->ShapedTextOptions;
		return State;
	}

	FTextFamilyWidgetState ReadEditableTextState(UEditableText* Widget)
	{
		FTextFamilyWidgetState State;
		State.Text = Widget->GetText().ToString();
		State.Hint = Widget->GetHintText().ToString();
		const FSlateFontInfo Font = Widget->GetFont();
		State.FontSize = Font.Size;
		State.TypefaceFontName = Font.TypefaceFontName.ToString();
		State.LetterSpacing = Font.LetterSpacing;
		State.FontObjectPath = Font.FontObject ? Font.FontObject->GetPathName() : FString();
		State.StyleTextColor = Widget->WidgetStyle.ColorAndOpacity.GetSpecifiedColor();
		State.Justification = Widget->GetJustification();
		State.Shaping = Widget->ShapedTextOptions;
		return State;
	}

	// Multi-line readers mirror the single-line font/color reads through each widget's own API
	// (WidgetStyle property / GetFont), then take the wrap/margin/line-height/justification/
	// shaping state from the reflected UTextLayoutWidget fields.
	FTextFamilyWidgetState ReadMultiLineTextBoxState(const FTextLayoutProperties& Properties, UMultiLineEditableTextBox* Widget)
	{
		FTextFamilyWidgetState State;
		State.Text = Widget->GetText().ToString();
		State.Hint = Widget->GetHintText().ToString();
		const FEditableTextBoxStyle& Style = Widget->WidgetStyle;
		State.FontSize = Style.TextStyle.Font.Size;
		State.TypefaceFontName = Style.TextStyle.Font.TypefaceFontName.ToString();
		State.LetterSpacing = Style.TextStyle.Font.LetterSpacing;
		State.FontObjectPath = Style.TextStyle.Font.FontObject ? Style.TextStyle.Font.FontObject->GetPathName() : FString();
		State.StyleTextColor = Style.TextStyle.ColorAndOpacity.GetSpecifiedColor();
		State.ForegroundColor = Style.ForegroundColor.GetSpecifiedColor();
		State.bAutoWrap = Widget->GetAutoWrapText();
		State.WrapTextAt = Widget->GetWrapTextAt();
		ReadTextLayoutState(Properties, Widget, State.Margin, State.LineHeight, State.Justification, State.Shaping);
		return State;
	}

	FTextFamilyWidgetState ReadMultiLineEditableTextState(const FTextLayoutProperties& Properties, UMultiLineEditableText* Widget)
	{
		FTextFamilyWidgetState State;
		State.Text = Widget->GetText().ToString();
		State.Hint = Widget->GetHintText().ToString();
		const FSlateFontInfo Font = Widget->GetFont();
		State.FontSize = Font.Size;
		State.TypefaceFontName = Font.TypefaceFontName.ToString();
		State.LetterSpacing = Font.LetterSpacing;
		State.FontObjectPath = Font.FontObject ? Font.FontObject->GetPathName() : FString();
		State.StyleTextColor = Widget->WidgetStyle.ColorAndOpacity.GetSpecifiedColor();
		State.bAutoWrap = Widget->GetAutoWrapText();
		State.WrapTextAt = Widget->GetWrapTextAt();
		ReadTextLayoutState(Properties, Widget, State.Margin, State.LineHeight, State.Justification, State.Shaping);
		return State;
	}

	// Compare typed and serialized properties for the editable-text family.
	// Profile flags select text-box colors and multiline layout assertions.
	void AssertTextFamilyParity(
		FAutomationTestBase& Test,
		const TCHAR* WidgetLabel,
		bool bTextBoxStyle,
		bool bWrapLayout,
		const FTextFamilyWidgetState& Typed,
		const FTextFamilyWidgetState& Serialized,
		const FLinearColor& ExpectedTextColor)
	{
		Test.TestEqual(FString::Printf(TEXT("%s typed and serialized text match"), WidgetLabel), Typed.Text, Serialized.Text);
		Test.TestEqual(FString::Printf(TEXT("%s typed and serialized hint match"), WidgetLabel), Typed.Hint, Serialized.Hint);
		Test.TestEqual(FString::Printf(TEXT("%s typed and serialized font size match"), WidgetLabel), Typed.FontSize, Serialized.FontSize);
		Test.TestEqual(FString::Printf(TEXT("%s typed and serialized typeface match"), WidgetLabel), Typed.TypefaceFontName, Serialized.TypefaceFontName);
		Test.TestEqual(FString::Printf(TEXT("%s typed and serialized letter spacing match"), WidgetLabel), Typed.LetterSpacing, Serialized.LetterSpacing);
		Test.TestEqual(FString::Printf(TEXT("%s typed and serialized font object path match"), WidgetLabel), Typed.FontObjectPath, Serialized.FontObjectPath);
		if (bTextBoxStyle)
		{
			Test.TestTrue(FString::Printf(TEXT("%s typed text color follows normalized typed node color"), WidgetLabel),
				Typed.StyleTextColor.Equals(ExpectedTextColor, 0.0001f));
			Test.TestTrue(FString::Printf(TEXT("%s typed foreground color follows normalized typed node color"), WidgetLabel),
				Typed.ForegroundColor.Equals(ExpectedTextColor, 0.0001f));
			Test.TestTrue(FString::Printf(TEXT("Second %s text color follows normalized typed node color"), WidgetLabel),
				Serialized.StyleTextColor.Equals(ExpectedTextColor, 0.0001f));
			Test.TestTrue(FString::Printf(TEXT("Second %s foreground color follows normalized typed node color"), WidgetLabel),
				Serialized.ForegroundColor.Equals(ExpectedTextColor, 0.0001f));
		}
		else
		{
			Test.TestTrue(FString::Printf(TEXT("%s typed color follows normalized typed node color"), WidgetLabel),
				Typed.StyleTextColor.Equals(ExpectedTextColor, 0.0001f));
			Test.TestTrue(FString::Printf(TEXT("Second %s color follows normalized typed node color"), WidgetLabel),
				Serialized.StyleTextColor.Equals(ExpectedTextColor, 0.0001f));
		}
		if (bWrapLayout)
		{
			Test.TestTrue(FString::Printf(TEXT("%s typed and serialized auto-wrap match"), WidgetLabel),
				Typed.bAutoWrap == Serialized.bAutoWrap);
			Test.TestTrue(FString::Printf(TEXT("%s typed and serialized wrap width match"), WidgetLabel),
				FMath::IsNearlyEqual(Typed.WrapTextAt, Serialized.WrapTextAt, 0.001f));
			Test.TestTrue(FString::Printf(TEXT("%s typed and serialized line height match"), WidgetLabel),
				FMath::IsNearlyEqual(Typed.LineHeight, Serialized.LineHeight, 0.001f));
		}
		Test.TestEqual(FString::Printf(TEXT("%s typed and serialized justification match"), WidgetLabel),
			Typed.Justification, Serialized.Justification);
		if (bWrapLayout)
		{
			Test.TestTrue(FString::Printf(TEXT("%s typed and serialized margin match"), WidgetLabel),
				FMath::IsNearlyEqual(Typed.Margin.Left, Serialized.Margin.Left, 0.001f)
				&& FMath::IsNearlyEqual(Typed.Margin.Top, Serialized.Margin.Top, 0.001f)
				&& FMath::IsNearlyEqual(Typed.Margin.Right, Serialized.Margin.Right, 0.001f)
				&& FMath::IsNearlyEqual(Typed.Margin.Bottom, Serialized.Margin.Bottom, 0.001f));
		}
		Test.TestTrue(FString::Printf(TEXT("%s typed and serialized icon shaping match"), WidgetLabel),
			Typed.Shaping == Serialized.Shaping);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextBlockSourceShadowExtentTest,
	"Design2UMG.PropertyApplier.TextBlockSourceShadowExtent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTextBlockSourceShadowExtentTest::RunTest(const FString&)
{
	UTextBlock* Widget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	FUMGWidgetNode Node;
	Node.Type = TEXT("TextBlock");
	Node.Text.Text = TEXT("Source label");
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(Widget, Node);
	TestEqual(TEXT("No source shadow contributes no invisible layout extent"), Widget->GetShadowOffset(), FVector2D::ZeroVector);
	Node.Shadow.ShadowOffset = FVector2D(2.0f, 3.0f);
	Node.Shadow.ShadowColorAndOpacity = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(Widget, Node);
	TestEqual(TEXT("Authored shadow offset is preserved"), Widget->GetShadowOffset(), FVector2D(2.0f, 3.0f));
	TestEqual(TEXT("Authored shadow opacity is preserved"), Widget->GetShadowColorAndOpacity().A, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextBlockUsesTypedTextPayloadParityTest,
	"Design2UMG.PropertyApplier.TextBlockUsesTypedTextPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextBlockUsesTypedTextPayloadParityTest::RunTest(const FString& Parameters)
{
	UTextBlock* TypedWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	UTextBlock* SerializedWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	TestTrue(TEXT("Typed TextBlock created"), TypedWidget != nullptr);
	TestTrue(TEXT("Serialized TextBlock created"), SerializedWidget != nullptr);
	if (!TypedWidget || !SerializedWidget)
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

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedWidget, Node);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(SerializedWidget, Node);

	FTextLayoutProperties LayoutProperties;
	if (!FindTextLayoutProperties(*this, LayoutProperties))
	{
		return false;
	}
	const FStructProperty* MarginProperty = LayoutProperties.Margin;
	const FFloatProperty* LineHeightProperty = LayoutProperties.LineHeight;
	const FByteProperty* JustificationProperty = LayoutProperties.Justification;
	const FStructProperty* ShapedTextOptionsProperty = LayoutProperties.ShapedTextOptions;

	const FMargin* TypedMargin = MarginProperty->ContainerPtrToValuePtr<FMargin>(TypedWidget);
	const FMargin* SerializedMargin = MarginProperty->ContainerPtrToValuePtr<FMargin>(SerializedWidget);
	const float TypedLineHeight = LineHeightProperty->GetPropertyValue_InContainer(TypedWidget);
	const float SerializedLineHeight = LineHeightProperty->GetPropertyValue_InContainer(SerializedWidget);
	const ETextJustify::Type TypedJustification = static_cast<ETextJustify::Type>(
		JustificationProperty->GetUnsignedIntPropertyValue(JustificationProperty->ContainerPtrToValuePtr<void>(TypedWidget)));
	const ETextJustify::Type SerializedJustification = static_cast<ETextJustify::Type>(
		JustificationProperty->GetUnsignedIntPropertyValue(JustificationProperty->ContainerPtrToValuePtr<void>(SerializedWidget)));
	const FShapedTextOptions* TypedShaping = ShapedTextOptionsProperty->ContainerPtrToValuePtr<FShapedTextOptions>(TypedWidget);
	const FShapedTextOptions* SerializedShaping = ShapedTextOptionsProperty->ContainerPtrToValuePtr<FShapedTextOptions>(SerializedWidget);

	TestEqual(TEXT("Typed node path keeps tooltip"), TypedWidget->GetToolTipText().ToString(), TEXT("Typed tooltip"));
	TestEqual(TEXT("Typed and serialized text match"), TypedWidget->GetText().ToString(), SerializedWidget->GetText().ToString());
	const FLinearColor ExpectedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	TestTrue(TEXT("Typed text block color follows normalized typed node color"),
		TypedWidget->GetColorAndOpacity().GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second text block color follows normalized typed node color"),
		SerializedWidget->GetColorAndOpacity().GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestEqual(TEXT("Typed and serialized font size match"), TypedWidget->GetFont().Size, SerializedWidget->GetFont().Size);
	TestEqual(TEXT("Typed and serialized typeface match"), TypedWidget->GetFont().TypefaceFontName, SerializedWidget->GetFont().TypefaceFontName);
	TestEqual(TEXT("Typed and serialized letter spacing match"), TypedWidget->GetFont().LetterSpacing, SerializedWidget->GetFont().LetterSpacing);
	TestEqual(TEXT("Typed and serialized font object path match"),
		TypedWidget->GetFont().FontObject ? TypedWidget->GetFont().FontObject->GetPathName() : FString(),
		SerializedWidget->GetFont().FontObject ? SerializedWidget->GetFont().FontObject->GetPathName() : FString());
	TestEqual(TEXT("Typed and serialized justification match"), TypedJustification, SerializedJustification);
	TestTrue(TEXT("Typed and serialized auto-wrap match"), TypedWidget->GetAutoWrapText() == SerializedWidget->GetAutoWrapText());
	TestTrue(TEXT("Typed and serialized wrap width match"), FMath::IsNearlyEqual(TypedWidget->GetWrapTextAt(), SerializedWidget->GetWrapTextAt(), 0.001f));
	TestTrue(TEXT("Typed and serialized line height match"), FMath::IsNearlyEqual(TypedLineHeight, SerializedLineHeight, 0.001f));
	TestTrue(TEXT("Typed and serialized margin match"),
		TypedMargin
		&& SerializedMargin
		&& FMath::IsNearlyEqual(TypedMargin->Left, SerializedMargin->Left, 0.001f)
		&& FMath::IsNearlyEqual(TypedMargin->Top, SerializedMargin->Top, 0.001f)
		&& FMath::IsNearlyEqual(TypedMargin->Right, SerializedMargin->Right, 0.001f)
		&& FMath::IsNearlyEqual(TypedMargin->Bottom, SerializedMargin->Bottom, 0.001f));
	TestTrue(TEXT("Typed and serialized shadow offset match"), TypedWidget->GetShadowOffset().Equals(SerializedWidget->GetShadowOffset(), 0.001f));
	TestTrue(TEXT("Typed and serialized shadow color match"),
		TypedWidget->GetShadowColorAndOpacity().Equals(SerializedWidget->GetShadowColorAndOpacity(), 0.0001f));
	TestTrue(TEXT("Typed and serialized icon shaping match"),
		TypedShaping
		&& SerializedShaping
		&& TypedShaping->bOverride_TextShapingMethod == SerializedShaping->bOverride_TextShapingMethod
		&& TypedShaping->TextShapingMethod == SerializedShaping->TextShapingMethod);
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

	UEditableTextBox* TypedTextBox = NewTextFamilyWidget<UEditableTextBox>(*this, TEXT("Typed editable text box created"));
	UEditableTextBox* SerializedTextBox = NewTextFamilyWidget<UEditableTextBox>(*this, TEXT("Serialized editable text box created"));
	if (!TypedTextBox || !SerializedTextBox)
	{
		return false;
	}

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedTextBox, Node);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(SerializedTextBox, Node);
	const FLinearColor ExpectedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	AssertTextFamilyParity(*this, TEXT("EditableTextBox"), /*bTextBoxStyle=*/true, /*bWrapLayout=*/false,
		ReadEditableTextBoxState(TypedTextBox), ReadEditableTextBoxState(SerializedTextBox), ExpectedTextColor);

	UEditableText* TypedEditableText = NewTextFamilyWidget<UEditableText>(*this, TEXT("Typed editable text created"));
	UEditableText* SerializedEditableText = NewTextFamilyWidget<UEditableText>(*this, TEXT("Serialized editable text created"));
	if (!TypedEditableText || !SerializedEditableText)
	{
		return false;
	}

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedEditableText, Node);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(SerializedEditableText, Node);
	AssertTextFamilyParity(*this, TEXT("EditableText"), /*bTextBoxStyle=*/false, /*bWrapLayout=*/false,
		ReadEditableTextState(TypedEditableText), ReadEditableTextState(SerializedEditableText), ExpectedTextColor);

	UEditableTextBox* TypedBehaviorTextBox = NewTextFamilyWidget<UEditableTextBox>(*this, TEXT("Typed behavior editable text box created"));
	UEditableText* TypedBehaviorEditableText = NewTextFamilyWidget<UEditableText>(*this, TEXT("Typed behavior editable text created"));
	if (!TypedBehaviorTextBox || !TypedBehaviorEditableText)
	{
		return false;
	}

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedBehaviorTextBox, Node);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedBehaviorEditableText, Node);

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

	UEditableTextBox* EmptyTypedTextBox = NewTextFamilyWidget<UEditableTextBox>(*this, TEXT("Typed empty editable text box created"));
	UEditableText* EmptyTypedEditableText = NewTextFamilyWidget<UEditableText>(*this, TEXT("Typed empty editable text created"));
	if (!EmptyTypedTextBox || !EmptyTypedEditableText)
	{
		return false;
	}

	EmptyTypedTextBox->SetText(FText::FromString(TEXT("Existing Text")));
	EmptyTypedTextBox->SetHintText(FText::FromString(TEXT("Existing Hint")));
	EmptyTypedEditableText->SetText(FText::FromString(TEXT("Existing Text")));
	EmptyTypedEditableText->SetHintText(FText::FromString(TEXT("Existing Hint")));

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(EmptyTypedTextBox, EmptyTypedNode);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(EmptyTypedEditableText, EmptyTypedNode);

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
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedWidget, Node);
	TestFalse(TEXT("Typed text widget applies explicit false AutoWrapText"), TypedWidget->GetAutoWrapText());

	PayloadWidget->SetAutoWrapText(true);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(PayloadWidget, Node);
	TestFalse(TEXT("Payload text widget applies explicit false AutoWrapText"), PayloadWidget->GetAutoWrapText());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiLineEditableTextFamilyUsesTypedTextPayloadParityTest,
	"Design2UMG.PropertyApplier.MultiLineEditableTextFamilyUsesTypedTextPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMultiLineEditableTextFamilyUsesTypedTextPayloadParityTest::RunTest(const FString& Parameters)
{
	FTextLayoutProperties LayoutProperties;
	if (!FindTextLayoutProperties(*this, LayoutProperties))
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

	const FLinearColor ExpectedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());

	UMultiLineEditableTextBox* TypedTextBox = NewTextFamilyWidget<UMultiLineEditableTextBox>(*this, TEXT("Typed multi-line editable text box created"));
	UMultiLineEditableTextBox* SerializedTextBox = NewTextFamilyWidget<UMultiLineEditableTextBox>(*this, TEXT("Serialized multi-line editable text box created"));
	if (!TypedTextBox || !SerializedTextBox)
	{
		return false;
	}

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedTextBox, Node);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(SerializedTextBox, Node);
	AssertTextFamilyParity(*this, TEXT("MultiLineEditableTextBox"), /*bTextBoxStyle=*/true, /*bWrapLayout=*/true,
		ReadMultiLineTextBoxState(LayoutProperties, TypedTextBox), ReadMultiLineTextBoxState(LayoutProperties, SerializedTextBox), ExpectedTextColor);

	UMultiLineEditableText* TypedEditableText = NewTextFamilyWidget<UMultiLineEditableText>(*this, TEXT("Typed multi-line editable text created"));
	UMultiLineEditableText* SerializedEditableText = NewTextFamilyWidget<UMultiLineEditableText>(*this, TEXT("Serialized multi-line editable text created"));
	if (!TypedEditableText || !SerializedEditableText)
	{
		return false;
	}

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedEditableText, Node);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(SerializedEditableText, Node);
	AssertTextFamilyParity(*this, TEXT("MultiLineEditableText"), /*bTextBoxStyle=*/false, /*bWrapLayout=*/true,
		ReadMultiLineEditableTextState(LayoutProperties, TypedEditableText), ReadMultiLineEditableTextState(LayoutProperties, SerializedEditableText), ExpectedTextColor);
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
	TMap<FString, FUMGRichTextStyleRowData> StyleRows;
	FUMGRichTextStyleRowData& Row0 = StyleRows.Add(TEXT("d2u_run_0"));
	Row0.Color = FUMGValueParser::ParseColor(TEXT("#112233FF"));
	FUMGRichTextStyleRowData& Row1 = StyleRows.Add(TEXT("d2u_run_1"));
	Row1.FontSize = 22.5f;
	Row1.Color = FUMGValueParser::ParseColor(TEXT("#224466FF"));
	Row1.bUnderline = true;
	Row1.Href = TEXT("https://docs.example.com");
	FUMGRichTextStyleRowData& Row2 = StyleRows.Add(TEXT("d2u_run_2"));
	Row2.FontSize = 0.375f;
	Node.Text.RichTextStyleRows = MoveTemp(StyleRows);
	Node.TextStyle.FontSize = 18.0f;
	Node.TextStyle.SourceFontSize = 18.0f;
	Node.TextStyle.LetterSpacing = 1.5f;
	Node.WidgetStyle.Padding = FMargin(3.0f, 4.0f, 5.0f, 6.0f);
	Node.WidgetStyle.ToolTipText = TEXT("https://docs.example.com");

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(Widget, Node);

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

	URichTextBlock* SerializedWidget = NewObject<URichTextBlock>(GetTransientPackageAsObject());
	TestTrue(TEXT("Serialized RichTextBlock created"), SerializedWidget != nullptr);
	if (!SerializedWidget)
	{
		return false;
	}
	SerializedWidget->SetDefaultTextStyle(PreconfiguredStyle);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(SerializedWidget, Node);

	TestEqual(TEXT("Typed and serialized rich text markup match"),
		Widget->GetText().ToString(),
		SerializedWidget->GetText().ToString());
	TestEqual(TEXT("Typed and serialized rich text tooltip match"),
		Widget->GetToolTipText().ToString(),
		SerializedWidget->GetToolTipText().ToString());
	TestTrue(TEXT("Typed and serialized rich text wrap width match"),
		FMath::IsNearlyEqual(Widget->GetWrapTextAt(), SerializedWidget->GetWrapTextAt(), 0.001f));

	const FTextBlockStyle& SerializedDefaultStyle = SerializedWidget->GetCurrentDefaultTextStyle();
	TestEqual(TEXT("Typed and serialized rich text default font size match"),
		DefaultStyle.Font.Size,
		SerializedDefaultStyle.Font.Size);
	TestTrue(TEXT("Typed rich text default color follows normalized typed node color"),
		DefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedDefaultColor, 0.0001f));
	TestTrue(TEXT("Second rich text default color follows normalized typed node color"),
		SerializedDefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedDefaultColor, 0.0001f));
	TestTrue(TEXT("Typed and serialized rich text default shadow color match"),
		DefaultStyle.ShadowColorAndOpacity.Equals(SerializedDefaultStyle.ShadowColorAndOpacity, 0.0001f));
	TestTrue(TEXT("Typed and serialized rich text default shadow offset match"),
		DefaultStyle.ShadowOffset == SerializedDefaultStyle.ShadowOffset);

	UDataTable* SerializedStyleSet = SerializedWidget->GetTextStyleSet();
	TestNotNull(TEXT("Serialized rich text style set exists"), SerializedStyleSet);
	if (!SerializedStyleSet)
	{
		return false;
	}

	const FRichTextStyleRow* SerializedLinkRow = SerializedStyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_1"), TEXT("RichTextBlockStyleRowsParity"));
	TestNotNull(TEXT("Serialized link row exists"), SerializedLinkRow);
	if (!SerializedLinkRow)
	{
		return false;
	}

	TestEqual(TEXT("Typed and serialized rich text row font size match"),
		LinkRow->TextStyle.Font.Size,
		SerializedLinkRow->TextStyle.Font.Size);
	TestTrue(TEXT("Typed and serialized rich text row color match"),
		LinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(
			SerializedLinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor(),
			0.0001f));
	TestEqual(TEXT("Typed and serialized rich text row underline draw-as match"),
		LinkRow->TextStyle.UnderlineBrush.DrawAs,
		SerializedLinkRow->TextStyle.UnderlineBrush.DrawAs);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextFamilyTypedJsonSerializationCycleTest,
	"Design2UMG.UMGJson.TextFamilyTypedPayloadSerializationCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextFamilyTypedJsonSerializationCycleTest::RunTest(const FString& Parameters)
{
	FTextLayoutProperties LayoutProperties;
	if (!FindTextLayoutProperties(*this, LayoutProperties))
	{
		return false;
	}

	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_TextFamilyJsonSerializationCycle");
	SourceData.RootWidget.Type = TEXT("CanvasPanel");
	SourceData.RootWidget.Id = TEXT("root");
	SourceData.RootWidget.Name = TEXT("Root");

	FUMGWidgetNode SourceNode;
	SourceNode.Type = TEXT("MultiLineEditableTextBox");
	SourceNode.Id = TEXT("typed_text_family");
	SourceNode.Name = TEXT("TypedTextFamily");
	SourceNode.Text.Text = TEXT("Serialization text payload");
	SourceNode.Text.HintText = TEXT("Serialization hint");
	SourceNode.Text.FontFamily = TEXT("/Game/Design2UMG/Fonts/Geist.Geist");
	SourceNode.Text.TypefaceFontName = TEXT("Regular");
	SourceNode.Text.Justification = TEXT("Center");
	SourceNode.Text.bAutoWrapText = true;
	SourceNode.Text.WrapTextAt = 240.0f;
	SourceNode.Text.LineHeightPercentage = 1.3f;
	SourceNode.Text.Color = FLinearColor(0.25f, 0.45f, 0.65f, 1.0f);
	SourceNode.Text.IconName = TEXT("glyph_serialization_cycle");
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

	UMultiLineEditableTextBox* SourceWidget = NewTextFamilyWidget<UMultiLineEditableTextBox>(*this, TEXT("Source text family widget created"));
	UMultiLineEditableTextBox* ParsedWidget = NewTextFamilyWidget<UMultiLineEditableTextBox>(*this, TEXT("Parsed text family widget created"));
	if (!SourceWidget || !ParsedWidget)
	{
		return false;
	}

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(SourceWidget, SourceNode);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(ParsedWidget, *ParsedNode);

	FMargin SourceMargin;
	FMargin ParsedMargin;
	float SourceLineHeight = 0.0f;
	float ParsedLineHeight = 0.0f;
	ETextJustify::Type SourceJustification = ETextJustify::Left;
	ETextJustify::Type ParsedJustification = ETextJustify::Left;
	FShapedTextOptions SourceShaping;
	FShapedTextOptions ParsedShaping;
	ReadTextLayoutState(LayoutProperties, SourceWidget, SourceMargin, SourceLineHeight, SourceJustification, SourceShaping);
	ReadTextLayoutState(LayoutProperties, ParsedWidget, ParsedMargin, ParsedLineHeight, ParsedJustification, ParsedShaping);

	TestEqual(TEXT("Serialization widget text matches"), ParsedWidget->GetText().ToString(), SourceWidget->GetText().ToString());
	TestEqual(TEXT("Serialization widget hint matches"), ParsedWidget->GetHintText().ToString(), SourceWidget->GetHintText().ToString());
	TestEqual(TEXT("Serialization widget font size matches"), ParsedWidget->WidgetStyle.TextStyle.Font.Size, SourceWidget->WidgetStyle.TextStyle.Font.Size);
	TestEqual(TEXT("Serialization widget typeface matches"), ParsedWidget->WidgetStyle.TextStyle.Font.TypefaceFontName, SourceWidget->WidgetStyle.TextStyle.Font.TypefaceFontName);
	TestEqual(TEXT("Serialization widget letter spacing matches"), ParsedWidget->WidgetStyle.TextStyle.Font.LetterSpacing, SourceWidget->WidgetStyle.TextStyle.Font.LetterSpacing);
	TestEqual(TEXT("Serialization widget font object path matches"),
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
	TestTrue(TEXT("Serialization widget auto wrap matches"), ParsedWidget->GetAutoWrapText() == SourceWidget->GetAutoWrapText());
	TestTrue(TEXT("Serialization widget wrap width matches"), FMath::IsNearlyEqual(ParsedWidget->GetWrapTextAt(), SourceWidget->GetWrapTextAt(), 0.001f));
	TestTrue(TEXT("Serialization widget line height matches"), FMath::IsNearlyEqual(ParsedLineHeight, SourceLineHeight, 0.001f));
	TestEqual(TEXT("Serialization widget justification matches"), ParsedJustification, SourceJustification);
	TestTrue(TEXT("Serialization widget margin matches"),
		FMath::IsNearlyEqual(ParsedMargin.Left, SourceMargin.Left, 0.001f)
		&& FMath::IsNearlyEqual(ParsedMargin.Top, SourceMargin.Top, 0.001f)
		&& FMath::IsNearlyEqual(ParsedMargin.Right, SourceMargin.Right, 0.001f)
		&& FMath::IsNearlyEqual(ParsedMargin.Bottom, SourceMargin.Bottom, 0.001f));
	TestTrue(TEXT("Serialization widget shaping matches"), ParsedShaping == SourceShaping);
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

	UTextBlock* ExplicitZeroWidget = NewTextFamilyWidget<UTextBlock>(*this, TEXT("Explicit zero widget created"));
	if (ExplicitZeroWidget)
	{
		FSlateFontInfo FontInfo = ExplicitZeroWidget->GetFont();
		FontInfo.LetterSpacing = 321;
		ExplicitZeroWidget->SetFont(FontInfo);
		FUMGWidgetTreeBuilder::ApplyWidgetProperties(ExplicitZeroWidget, ExplicitZeroNode);
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

	UTextBlock* NonZeroWidget = NewTextFamilyWidget<UTextBlock>(*this, TEXT("Non-zero widget created"));
	if (NonZeroWidget && ParsedNonZeroNode)
	{
		FUMGWidgetTreeBuilder::ApplyWidgetProperties(NonZeroWidget, *ParsedNonZeroNode);
		TestEqual(TEXT("Pixel letter spacing converts to Slate tracking"), NonZeroWidget->GetFont().LetterSpacing, 63);
	}

	FUMGWidgetNode ScaledNegativeNode = ExplicitZeroNode;
	ScaledNegativeNode.TextStyle.FontSize = 12.0f;
	ScaledNegativeNode.TextStyle.SourceFontSize = 20.0f;
	ScaledNegativeNode.TextStyle.LetterSpacing = -1.25f;
	UTextBlock* ScaledNegativeWidget = NewTextFamilyWidget<UTextBlock>(*this, TEXT("Scaled negative-spacing widget created"));
	if (ScaledNegativeWidget)
	{
		FUMGWidgetTreeBuilder::ApplyWidgetProperties(ScaledNegativeWidget, ScaledNegativeNode);
		// SlateTextShaper applies tracking * FontInfo.Size / 1000 and truncates to pixels.
		// The source's 20px declaration must not replace the applied 12pt size here.
		TestEqual(TEXT("Applied point size owns scaled negative letter-spacing conversion"), ScaledNegativeWidget->GetFont().LetterSpacing, -105);
		TestEqual(TEXT("Scaled tracking preserves the requested negative pixel gap"),
			static_cast<int32>(ScaledNegativeWidget->GetFont().LetterSpacing * ScaledNegativeWidget->GetFont().Size / 1000.0f), -1);
	}

	URichTextBlock* RichTextWidget = NewTextFamilyWidget<URichTextBlock>(*this, TEXT("Rich text spacing widget created"));
	if (RichTextWidget)
	{
		FUMGRichTextStyleRowData SpacingStyle;
		SpacingStyle.FontSize = 12.0f;
		SpacingStyle.LetterSpacing = -1.25f;
		UMGTextStyleApplication::ApplyRichTextStyleRows(RichTextWidget, { { TEXT("row"), SpacingStyle } });
		const UDataTable* StyleSet = RichTextWidget->GetTextStyleSet();
		const FRichTextStyleRow* Row = StyleSet
			? StyleSet->FindRow<FRichTextStyleRow>(TEXT("row"), TEXT("LetterSpacingConversion"))
			: nullptr;
		TestNotNull(TEXT("Rich text spacing row created"), Row);
		if (Row)
		{
			TestEqual(TEXT("Rich text row uses shared applied-size conversion"), Row->TextStyle.Font.LetterSpacing, -105);
			TestEqual(TEXT("Rich text tracking preserves the requested negative pixel gap"),
				static_cast<int32>(Row->TextStyle.Font.LetterSpacing * Row->TextStyle.Font.Size / 1000.0f), -1);
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

	UTextBlock* UnsetWidget = NewTextFamilyWidget<UTextBlock>(*this, TEXT("Unset widget created"));
	if (UnsetWidget)
	{
		FSlateFontInfo FontInfo = UnsetWidget->GetFont();
		FontInfo.LetterSpacing = 321;
		UnsetWidget->SetFont(FontInfo);
		FUMGWidgetTreeBuilder::ApplyWidgetProperties(UnsetWidget, UnsetNode);
		TestEqual(TEXT("Unset letter spacing does not apply"), UnsetWidget->GetFont().LetterSpacing, 321);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRichTextTypedJsonSerializationCycleTest,
	"Design2UMG.UMGJson.RichTextTypedPayloadSerializationCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRichTextTypedJsonSerializationCycleTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_RichTextJsonSerializationCycle");
	SourceData.RootWidget.Type = TEXT("CanvasPanel");
	SourceData.RootWidget.Id = TEXT("root");
	SourceData.RootWidget.Name = TEXT("Root");

	FUMGWidgetNode SourceNode;
	SourceNode.Type = TEXT("RichTextBlock");
	SourceNode.Id = TEXT("typed_rich_text");
	SourceNode.Name = TEXT("TypedRichText");
	SourceNode.Text.Text = TEXT("<d2u_run_0>Hello </><d2u_run_1 href=\"https://docs.example.com\">Docs</>");
	TMap<FString, FUMGRichTextStyleRowData> StyleRows;
	FUMGRichTextStyleRowData& Row0 = StyleRows.Add(TEXT("d2u_run_0"));
	Row0.Color = FUMGValueParser::ParseColor(TEXT("#112233FF"));
	Row0.FontFamily = TEXT("/Game/Design2UMG/Fonts/Geist.Geist");
	Row0.FontSize = 27.0f;
	Row0.TypefaceFontName = TEXT("Regular");
	FUMGRichTextStyleRowData& Row1 = StyleRows.Add(TEXT("d2u_run_1"));
	Row1.Color = FUMGValueParser::ParseColor(TEXT("#224466FF"));
	Row1.FontFamily = TEXT("/Game/Design2UMG/Fonts/Geist.Geist");
	Row1.FontSize = 27.0f;
	Row1.Href = TEXT("https://docs.example.com");
	Row1.TypefaceFontName = TEXT("Bold");
	Row1.bUnderline = true;
	SourceNode.Text.RichTextStyleRows = MoveTemp(StyleRows);
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
	TestTrue(TEXT("Parsed rich text style rows preserve every field and optional presence"),
		ParsedNode->Text.RichTextStyleRows.IsSet()
		&& ParsedNode->Text.RichTextStyleRows->OrderIndependentCompareEqual(SourceNode.Text.RichTextStyleRows.GetValue()));
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

	URichTextBlock* SourceWidget = NewTextFamilyWidget<URichTextBlock>(*this, TEXT("Source rich text widget created"));
	URichTextBlock* ParsedWidget = NewTextFamilyWidget<URichTextBlock>(*this, TEXT("Parsed rich text widget created"));
	if (!SourceWidget || !ParsedWidget)
	{
		return false;
	}

	const FTextBlockStyle PreconfiguredStyle = FTextBlockStyle::GetDefault();
	SourceWidget->SetDefaultTextStyle(PreconfiguredStyle);
	ParsedWidget->SetDefaultTextStyle(PreconfiguredStyle);

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(SourceWidget, SourceNode);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(ParsedWidget, *ParsedNode);

	TestEqual(TEXT("Serialization rich text markup matches"), ParsedWidget->GetText().ToString(), SourceWidget->GetText().ToString());
	TestTrue(TEXT("Serialization rich text wrap width matches"), FMath::IsNearlyEqual(ParsedWidget->GetWrapTextAt(), SourceWidget->GetWrapTextAt(), 0.001f));
	TestEqual(TEXT("Serialization rich text style snapshot matches"),
		BuildStyleSnapshot(ParsedWidget, TEXT("ParsedRichTextSerializationCycle")),
		BuildStyleSnapshot(SourceWidget, TEXT("SourceRichTextSerializationCycle")));
	return true;
}

#endif
