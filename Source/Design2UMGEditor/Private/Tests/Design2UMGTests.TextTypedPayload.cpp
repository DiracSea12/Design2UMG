// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Typed text payload application for the text widget family (TextBlock, editable text family, rich text, letter spacing, JSON round trips).

#include "Design2UMGWidgetPropertyAccess.h"
#include "UObject/Package.h"
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
#include "Misc/EngineVersionComparison.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SWidget.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;

namespace
{
	bool AreShapedTextOptionsEqual(const FShapedTextOptions& A, const FShapedTextOptions& B)
	{
		return A.bOverride_TextShapingMethod == B.bOverride_TextShapingMethod
			&& A.bOverride_TextFlowDirection == B.bOverride_TextFlowDirection
			&& (!A.bOverride_TextShapingMethod || A.TextShapingMethod == B.TextShapingMethod)
			&& (!A.bOverride_TextFlowDirection || A.TextFlowDirection == B.TextFlowDirection);
	}

	// Shared creation/reading/assertion vocabulary for the typed-text-payload family tests.

	template <typename T>
	T* NewTextFamilyWidget(FAutomationTestBase& Test, const TCHAR* CreationLabel)
	{
		T* Widget = NewObject<T>(GetTransientPackage());
		Test.TestNotNull(CreationLabel, Widget);
		return Widget;
	}

	template <typename T>
	bool TestMaterializedTextStyleUpdate(FAutomationTestBase& Test, const TCHAR* WidgetLabel)
	{
		T* Widget = NewTextFamilyWidget<T>(Test, WidgetLabel);
		if (!Widget)
		{
			return false;
		}

		Widget->SetText(FText::FromString(TEXT("Hg")));
		const TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
		SlateWidget->SlatePrepass(1.0f);
		const FVector2D InitialSize = SlateWidget->GetDesiredSize();

		FUMGWidgetNode Node;
		Node.TextStyle.FontSize = 72.0f;
		Node.TextStyle.SourceFontSize = 72.0f;
		FUMGWidgetTreeBuilder::ApplyWidgetProperties(Widget, Node);

		SlateWidget->SlatePrepass(1.0f);
		const FVector2D UpdatedSize = SlateWidget->GetDesiredSize();
		const bool bHeightIncreased = UpdatedSize.Y > InitialSize.Y + KINDA_SMALL_NUMBER;
		Test.TestTrue(
			FString::Printf(TEXT("%s updates its materialized Slate font metrics"), WidgetLabel),
			bHeightIncreased);
		return bHeightIncreased;
	}

	// UTextLayoutWidget reflected layout properties shared by the application/serialization tests.
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
		bool bHasValidFont = false;
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
		State.Hint = Design2UMGWidgetPropertyAccess::GetHintText(*Widget).ToString();
		const FEditableTextBoxStyle& Style = Widget->WidgetStyle;
		State.FontSize = Design2UMGWidgetPropertyAccess::GetFont(Style).Size;
		State.TypefaceFontName = Design2UMGWidgetPropertyAccess::GetFont(Style).TypefaceFontName.ToString();
		State.LetterSpacing = Design2UMGWidgetPropertyAccess::GetFont(Style).LetterSpacing;
		State.bHasValidFont = Design2UMGWidgetPropertyAccess::GetFont(Style).HasValidFont();
		State.StyleTextColor = Design2UMGWidgetPropertyAccess::GetTextColor(Style).GetSpecifiedColor();
		State.ForegroundColor = Style.ForegroundColor.GetSpecifiedColor();
		State.Justification = Design2UMGWidgetPropertyAccess::GetJustification(*Widget);
		State.Shaping = Widget->ShapedTextOptions;
		return State;
	}

	FTextFamilyWidgetState ReadEditableTextState(UEditableText* Widget)
	{
		FTextFamilyWidgetState State;
		State.Text = Widget->GetText().ToString();
		State.Hint = Design2UMGWidgetPropertyAccess::GetHintText(*Widget).ToString();
		const FSlateFontInfo Font = Design2UMGWidgetPropertyAccess::GetFont(*Widget);
		State.FontSize = Font.Size;
		State.TypefaceFontName = Font.TypefaceFontName.ToString();
		State.LetterSpacing = Font.LetterSpacing;
		State.bHasValidFont = Font.HasValidFont();
		State.StyleTextColor = Widget->WidgetStyle.ColorAndOpacity.GetSpecifiedColor();
		State.Justification = Design2UMGWidgetPropertyAccess::GetJustification(*Widget);
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
		State.FontSize = Design2UMGWidgetPropertyAccess::GetFont(Style).Size;
		State.TypefaceFontName = Design2UMGWidgetPropertyAccess::GetFont(Style).TypefaceFontName.ToString();
		State.LetterSpacing = Design2UMGWidgetPropertyAccess::GetFont(Style).LetterSpacing;
		State.bHasValidFont = Design2UMGWidgetPropertyAccess::GetFont(Style).HasValidFont();
		State.StyleTextColor = Design2UMGWidgetPropertyAccess::GetTextColor(Style).GetSpecifiedColor();
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
		const FSlateFontInfo Font = Design2UMGWidgetPropertyAccess::GetFont(*Widget);
		State.FontSize = Font.Size;
		State.TypefaceFontName = Font.TypefaceFontName.ToString();
		State.LetterSpacing = Font.LetterSpacing;
		State.bHasValidFont = Font.HasValidFont();
		State.StyleTextColor = Widget->WidgetStyle.ColorAndOpacity.GetSpecifiedColor();
		State.bAutoWrap = Widget->GetAutoWrapText();
		State.WrapTextAt = Widget->GetWrapTextAt();
		ReadTextLayoutState(Properties, Widget, State.Margin, State.LineHeight, State.Justification, State.Shaping);
		return State;
	}

	// Check authored values on the editable-text family. Profile flags select
	// text-box colors and multiline layout assertions.
	void AssertTextFamilyProperties(
		FAutomationTestBase& Test,
		const TCHAR* WidgetLabel,
		bool bTextBoxStyle,
		bool bWrapLayout,
		const FTextFamilyWidgetState& Typed,
		const FUMGWidgetNode& Node,
		ETextJustify::Type ExpectedJustification,
		int32 ExpectedTracking,
		const FLinearColor& ExpectedTextColor)
	{
		Test.TestEqual(FString::Printf(TEXT("%s text applied"), WidgetLabel), Typed.Text, Node.Text.Text.GetValue());
		Test.TestEqual(FString::Printf(TEXT("%s hint applied"), WidgetLabel), Typed.Hint, Node.Text.HintText.GetValue());
		Test.TestEqual(FString::Printf(TEXT("%s font size applied"), WidgetLabel), Typed.FontSize, Node.TextStyle.FontSize);
		Test.TestEqual(FString::Printf(TEXT("%s typeface applied"), WidgetLabel), Typed.TypefaceFontName, Node.Text.TypefaceFontName.GetValue());
		Test.TestEqual(FString::Printf(TEXT("%s letter spacing applied as tracking"), WidgetLabel), Typed.LetterSpacing, static_cast<float>(ExpectedTracking));
		Test.TestTrue(FString::Printf(TEXT("%s retains a valid Slate font"), WidgetLabel), Typed.bHasValidFont);
		Test.TestTrue(FString::Printf(TEXT("%s typed text color follows normalized typed node color"), WidgetLabel),
			Typed.StyleTextColor.Equals(ExpectedTextColor, 0.0001f));
		if (bTextBoxStyle)
		{
			Test.TestTrue(FString::Printf(TEXT("%s typed foreground color follows normalized typed node color"), WidgetLabel),
				Typed.ForegroundColor.Equals(ExpectedTextColor, 0.0001f));
		}
		if (bWrapLayout)
		{
			Test.TestEqual(FString::Printf(TEXT("%s auto-wrap applied"), WidgetLabel), Typed.bAutoWrap, Node.Text.bAutoWrapText.GetValue());
			Test.TestTrue(FString::Printf(TEXT("%s wrap width applied"), WidgetLabel),
				FMath::IsNearlyEqual(Typed.WrapTextAt, Node.Text.WrapTextAt.GetValue(), 0.001f));
			Test.TestTrue(FString::Printf(TEXT("%s line height applied"), WidgetLabel),
				FMath::IsNearlyEqual(Typed.LineHeight, Node.Text.LineHeightPercentage.GetValue(), 0.001f));
		}
		Test.TestEqual(FString::Printf(TEXT("%s justification applied"), WidgetLabel), Typed.Justification, ExpectedJustification);
		if (bWrapLayout)
		{
			Test.TestTrue(FString::Printf(TEXT("%s padding applied as internal margin"), WidgetLabel),
				FMath::IsNearlyEqual(Typed.Margin.Left, Node.WidgetStyle.Padding->Left, 0.001f)
				&& FMath::IsNearlyEqual(Typed.Margin.Top, Node.WidgetStyle.Padding->Top, 0.001f)
				&& FMath::IsNearlyEqual(Typed.Margin.Right, Node.WidgetStyle.Padding->Right, 0.001f)
				&& FMath::IsNearlyEqual(Typed.Margin.Bottom, Node.WidgetStyle.Padding->Bottom, 0.001f));
		}
		Test.TestTrue(FString::Printf(TEXT("%s icon uses full shaping"), WidgetLabel),
			Typed.Shaping.bOverride_TextShapingMethod && Typed.Shaping.TextShapingMethod == ETextShapingMethod::FullShaping);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextBlockSourceShadowExtentTest,
	"Design2UMG.PropertyApplier.TextBlockSourceShadowExtent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTextBlockSourceShadowExtentTest::RunTest(const FString&)
{
	UTextBlock* Widget = NewObject<UTextBlock>(GetTransientPackage());
	FUMGWidgetNode Node;
	Node.Type = TEXT("TextBlock");
	Node.Text.Text = TEXT("Source label");
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(Widget, Node);
	TestEqual(TEXT("No source shadow contributes no invisible layout extent"), Design2UMGWidgetPropertyAccess::GetShadowOffset(*Widget), FVector2D::ZeroVector);
	Node.Shadow.ShadowOffset = FVector2D(2.0f, 3.0f);
	Node.Shadow.ShadowColorAndOpacity = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(Widget, Node);
	TestEqual(TEXT("Authored shadow offset is preserved"), Design2UMGWidgetPropertyAccess::GetShadowOffset(*Widget), FVector2D(2.0f, 3.0f));
	TestEqual(TEXT("Authored shadow opacity is preserved"), Design2UMGWidgetPropertyAccess::GetShadowColorAndOpacity(*Widget).A, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextBlockUsesTypedTextPayloadTest,
	"Design2UMG.PropertyApplier.TextBlockUsesTypedTextPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextBlockUsesTypedTextPayloadTest::RunTest(const FString& Parameters)
{
	UTextBlock* Widget = NewTextFamilyWidget<UTextBlock>(*this, TEXT("TextBlock created"));
	if (!Widget)
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

	const UObject* InitialFontObject = Design2UMGWidgetPropertyAccess::GetFont(*Widget).FontObject;
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(Widget, Node);

	FTextLayoutProperties LayoutProperties;
	if (!FindTextLayoutProperties(*this, LayoutProperties))
	{
		return false;
	}
	FMargin Margin;
	float LineHeight = 0.0f;
	ETextJustify::Type Justification = ETextJustify::Left;
	FShapedTextOptions Shaping;
	ReadTextLayoutState(LayoutProperties, Widget, Margin, LineHeight, Justification, Shaping);

	TestEqual(TEXT("Typed tooltip applied"), Design2UMGWidgetPropertyAccess::GetToolTipText(*Widget).ToString(), Node.WidgetStyle.ToolTipText.GetValue());
	TestEqual(TEXT("Typed text applied"), Widget->GetText().ToString(), Node.Text.Text.GetValue());
	const FLinearColor ExpectedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	TestTrue(TEXT("Typed text block color follows normalized typed node color"),
		Design2UMGWidgetPropertyAccess::GetColorAndOpacity(*Widget).GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	const FSlateFontInfo Font = Design2UMGWidgetPropertyAccess::GetFont(*Widget);
	TestEqual(TEXT("Typed font size applied"), static_cast<float>(Font.Size), 18.0f);
	TestEqual(TEXT("Typed typeface applied"), Font.TypefaceFontName, FName(TEXT("Regular")));
	TestEqual(TEXT("Typed letter spacing applied as tracking"), Font.LetterSpacing, 84);
	TestTrue(TEXT("Typed font object retained"), Font.FontObject != nullptr);
	TestTrue(TEXT("Icon family name preserves the existing font object"), Font.FontObject == InitialFontObject);
	TestEqual(TEXT("Typed justification applied"), Justification, ETextJustify::Center);
	TestTrue(TEXT("Typed auto-wrap applied"), Widget->GetAutoWrapText());
	TestTrue(TEXT("Typed wrap width applied"), FMath::IsNearlyEqual(Widget->GetWrapTextAt(), 144.0f, 0.001f));
	TestTrue(TEXT("Typed line height applied"), FMath::IsNearlyEqual(LineHeight, 1.35f, 0.001f));
	TestTrue(TEXT("Typed padding applied as internal margin"),
		FMath::IsNearlyEqual(Margin.Left, 7.0f, 0.001f)
		&& FMath::IsNearlyEqual(Margin.Top, 8.0f, 0.001f)
		&& FMath::IsNearlyEqual(Margin.Right, 9.0f, 0.001f)
		&& FMath::IsNearlyEqual(Margin.Bottom, 10.0f, 0.001f));
	TestTrue(TEXT("Typed shadow offset applied"), Design2UMGWidgetPropertyAccess::GetShadowOffset(*Widget).Equals(Node.Shadow.ShadowOffset.GetValue(), 0.001f));
	TestTrue(TEXT("Typed shadow color applied"),
		Design2UMGWidgetPropertyAccess::GetShadowColorAndOpacity(*Widget).Equals(Node.Shadow.ShadowColorAndOpacity.GetValue(), 0.0001f));
	TestTrue(TEXT("Typed icon uses full shaping"),
		Shaping.bOverride_TextShapingMethod && Shaping.TextShapingMethod == ETextShapingMethod::FullShaping);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditableTextFamilyUsesTypedTextPayloadTest,
	"Design2UMG.PropertyApplier.EditableTextFamilyUsesTypedTextPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEditableTextFamilyUsesTypedTextPayloadTest::RunTest(const FString& Parameters)
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
	if (!TypedTextBox)
	{
		return false;
	}

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedTextBox, Node);
	const FLinearColor ExpectedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	AssertTextFamilyProperties(*this, TEXT("EditableTextBox"), /*bTextBoxStyle=*/true, /*bWrapLayout=*/false,
		ReadEditableTextBoxState(TypedTextBox), Node, ETextJustify::Center, 66, ExpectedTextColor);

	UEditableText* TypedEditableText = NewTextFamilyWidget<UEditableText>(*this, TEXT("Typed editable text created"));
	if (!TypedEditableText)
	{
		return false;
	}

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedEditableText, Node);
	AssertTextFamilyProperties(*this, TEXT("EditableText"), /*bTextBoxStyle=*/false, /*bWrapLayout=*/false,
		ReadEditableTextState(TypedEditableText), Node, ETextJustify::Center, 66, ExpectedTextColor);

	TestTrue(TEXT("Typed editable text box applies MinimumDesiredWidth"),
		FMath::IsNearlyEqual(
			EditableTextBoxMinimumDesiredWidthProperty->GetPropertyValue_InContainer(TypedTextBox),
			123.5f,
			0.001f));
	TestTrue(TEXT("Typed editable text applies MinimumDesiredWidth"),
		FMath::IsNearlyEqual(
			EditableTextMinimumDesiredWidthProperty->GetPropertyValue_InContainer(TypedEditableText),
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
	TestTrue(TEXT("Typed empty hint clears EditableTextBox hint"), Design2UMGWidgetPropertyAccess::GetHintText(*EmptyTypedTextBox).ToString().IsEmpty());
	TestTrue(TEXT("Typed empty text clears EditableText text"), EmptyTypedEditableText->GetText().ToString().IsEmpty());
	TestTrue(TEXT("Typed empty hint clears EditableText hint"), Design2UMGWidgetPropertyAccess::GetHintText(*EmptyTypedEditableText).ToString().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditableTextFamilyMaterializedStyleUpdateTest,
	"Design2UMG.PropertyApplier.EditableTextFamilyMaterializedStyleUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEditableTextFamilyMaterializedStyleUpdateTest::RunTest(const FString& Parameters)
{
	bool bAllUpdated = true;
	bAllUpdated &= TestMaterializedTextStyleUpdate<UEditableText>(*this, TEXT("EditableText"));
	bAllUpdated &= TestMaterializedTextStyleUpdate<UEditableTextBox>(*this, TEXT("EditableTextBox"));
	bAllUpdated &= TestMaterializedTextStyleUpdate<UMultiLineEditableText>(*this, TEXT("MultiLineEditableText"));
	bAllUpdated &= TestMaterializedTextStyleUpdate<UMultiLineEditableTextBox>(*this, TEXT("MultiLineEditableTextBox"));
	return bAllUpdated;
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

	UTextBlock* TypedWidget = NewObject<UTextBlock>(GetTransientPackage());
	TestNotNull(TEXT("Typed text widget created"), TypedWidget);
	if (!TypedWidget)
	{
		return false;
	}

	TypedWidget->SetAutoWrapText(true);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedWidget, Node);
	TestFalse(TEXT("Typed text widget applies explicit false AutoWrapText"), TypedWidget->GetAutoWrapText());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiLineEditableTextFamilyUsesTypedTextPayloadTest,
	"Design2UMG.PropertyApplier.MultiLineEditableTextFamilyUsesTypedTextPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMultiLineEditableTextFamilyUsesTypedTextPayloadTest::RunTest(const FString& Parameters)
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
	if (!TypedTextBox)
	{
		return false;
	}

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedTextBox, Node);
	AssertTextFamilyProperties(*this, TEXT("MultiLineEditableTextBox"), /*bTextBoxStyle=*/true, /*bWrapLayout=*/true,
		ReadMultiLineTextBoxState(LayoutProperties, TypedTextBox), Node, ETextJustify::Right, 45, ExpectedTextColor);

	UMultiLineEditableText* TypedEditableText = NewTextFamilyWidget<UMultiLineEditableText>(*this, TEXT("Typed multi-line editable text created"));
	if (!TypedEditableText)
	{
		return false;
	}

	FUMGWidgetTreeBuilder::ApplyWidgetProperties(TypedEditableText, Node);
	AssertTextFamilyProperties(*this, TEXT("MultiLineEditableText"), /*bTextBoxStyle=*/false, /*bWrapLayout=*/true,
		ReadMultiLineEditableTextState(LayoutProperties, TypedEditableText), Node, ETextJustify::Right, 45, ExpectedTextColor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRichTextBlockStyleRowsApplicationTest,
	"Design2UMG.PropertyApplier.RichTextBlockStyleRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRichTextBlockStyleRowsApplicationTest::RunTest(const FString& Parameters)
{
	URichTextBlock* Widget = NewObject<URichTextBlock>(GetTransientPackage());
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
	TestEqual(TEXT("Rich text tooltip applied"), Design2UMGWidgetPropertyAccess::GetToolTipText(*Widget).ToString(), TEXT("https://docs.example.com"));
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
	TestEqual(TEXT("Rich text default font size applied"), static_cast<float>(DefaultStyle.Font.Size), 18.0f);
	const FLinearColor ExpectedDefaultColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	TestTrue(TEXT("Rich text default color applied"),
		DefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedDefaultColor, 0.0001f));
	TestTrue(TEXT("Rich text default style preserves preconfigured shadow color"),
		DefaultStyle.ShadowColorAndOpacity.Equals(PreconfiguredStyle.ShadowColorAndOpacity, 0.0001f));
	TestTrue(TEXT("Rich text default style preserves preconfigured shadow offset"),
		DefaultStyle.ShadowOffset == PreconfiguredStyle.ShadowOffset);

	UDataTable* StyleSet = Design2UMGWidgetPropertyAccess::GetTextStyleSet(*Widget);
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
	TestEqual(TEXT("Sparse base row inherits default font size"), static_cast<float>(BaseRow->TextStyle.Font.Size), 18.0f);
	TestTrue(TEXT("Sparse base row preserves base shadow color"),
		BaseRow->TextStyle.ShadowColorAndOpacity.Equals(PreconfiguredStyle.ShadowColorAndOpacity, 0.0001f));

	const FRichTextStyleRow* LinkRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_1"), TEXT("RichTextBlockStyleRowsApplication"));
	TestNotNull(TEXT("Link row exists"), LinkRow);
	if (!LinkRow)
	{
		return false;
	}

#if UE_VERSION_OLDER_THAN(5, 3, 0)
	TestEqual(TEXT("Link row uses the native integer font size"), LinkRow->TextStyle.Font.Size, 22);
#else
	TestTrue(TEXT("Link row preserves the exact fractional font size"), FMath::IsNearlyEqual(LinkRow->TextStyle.Font.Size, 22.5f, 0.001f));
#endif
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
#if UE_VERSION_OLDER_THAN(5, 3, 0)
	TestEqual(TEXT("Rich text application uses the native integer subpoint size"), SubpointRow->TextStyle.Font.Size, 0);
#else
	TestTrue(TEXT("Rich text application preserves 0.375 point row sizes"),
		FMath::IsNearlyEqual(SubpointRow->TextStyle.Font.Size, 0.375f, 0.001f));
#endif
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
	TestEqual(TEXT("Serialization widget hint matches"), Design2UMGWidgetPropertyAccess::GetHintText(*ParsedWidget).ToString(), Design2UMGWidgetPropertyAccess::GetHintText(*SourceWidget).ToString());
	TestEqual(TEXT("Serialization widget font size matches"), Design2UMGWidgetPropertyAccess::GetFont(ParsedWidget->WidgetStyle).Size, Design2UMGWidgetPropertyAccess::GetFont(SourceWidget->WidgetStyle).Size);
	TestEqual(TEXT("Serialization widget typeface matches"), Design2UMGWidgetPropertyAccess::GetFont(ParsedWidget->WidgetStyle).TypefaceFontName, Design2UMGWidgetPropertyAccess::GetFont(SourceWidget->WidgetStyle).TypefaceFontName);
	TestEqual(TEXT("Serialization widget letter spacing matches"), Design2UMGWidgetPropertyAccess::GetFont(ParsedWidget->WidgetStyle).LetterSpacing, Design2UMGWidgetPropertyAccess::GetFont(SourceWidget->WidgetStyle).LetterSpacing);
	TestEqual(TEXT("Serialization widget font object path matches"),
		Design2UMGWidgetPropertyAccess::GetFont(ParsedWidget->WidgetStyle).FontObject ? Design2UMGWidgetPropertyAccess::GetFont(ParsedWidget->WidgetStyle).FontObject->GetPathName() : FString(),
		Design2UMGWidgetPropertyAccess::GetFont(SourceWidget->WidgetStyle).FontObject ? Design2UMGWidgetPropertyAccess::GetFont(SourceWidget->WidgetStyle).FontObject->GetPathName() : FString());
	const FLinearColor ExpectedSourceTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(SourceNode.Text.Color.GetValue());
	const FLinearColor ExpectedParsedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(ParsedNode->Text.Color.GetValue());
	TestTrue(TEXT("Source widget text color follows normalized source node color"),
		Design2UMGWidgetPropertyAccess::GetTextColor(SourceWidget->WidgetStyle).GetSpecifiedColor().Equals(ExpectedSourceTextColor, 0.0001f));
	TestTrue(TEXT("Parsed widget text color follows normalized parsed node color"),
		Design2UMGWidgetPropertyAccess::GetTextColor(ParsedWidget->WidgetStyle).GetSpecifiedColor().Equals(ExpectedParsedTextColor, 0.0001f));
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
	TestTrue(TEXT("Serialization widget shaping matches"), AreShapedTextOptionsEqual(ParsedShaping, SourceShaping));
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
		FSlateFontInfo FontInfo = Design2UMGWidgetPropertyAccess::GetFont(*ExplicitZeroWidget);
		FontInfo.LetterSpacing = 321;
		ExplicitZeroWidget->SetFont(FontInfo);
		FUMGWidgetTreeBuilder::ApplyWidgetProperties(ExplicitZeroWidget, ExplicitZeroNode);
		TestEqual(TEXT("Explicit zero letter spacing applies"), Design2UMGWidgetPropertyAccess::GetFont(*ExplicitZeroWidget).LetterSpacing, 0);
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
		TestEqual(TEXT("Pixel letter spacing converts to Slate tracking"), Design2UMGWidgetPropertyAccess::GetFont(*NonZeroWidget).LetterSpacing, 63);
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
		TestEqual(TEXT("Applied point size owns scaled negative letter-spacing conversion"), Design2UMGWidgetPropertyAccess::GetFont(*ScaledNegativeWidget).LetterSpacing, -105);
		TestEqual(TEXT("Scaled tracking preserves the requested negative pixel gap"),
			static_cast<int32>(Design2UMGWidgetPropertyAccess::GetFont(*ScaledNegativeWidget).LetterSpacing * Design2UMGWidgetPropertyAccess::GetFont(*ScaledNegativeWidget).Size / 1000.0f), -1);
	}

	URichTextBlock* RichTextWidget = NewTextFamilyWidget<URichTextBlock>(*this, TEXT("Rich text spacing widget created"));
	if (RichTextWidget)
	{
		FUMGRichTextStyleRowData SpacingStyle;
		SpacingStyle.FontSize = 12.0f;
		SpacingStyle.LetterSpacing = -1.25f;
		UMGTextStyleApplication::ApplyRichTextStyleRows(RichTextWidget, { { TEXT("row"), SpacingStyle } });
		const UDataTable* StyleSet = Design2UMGWidgetPropertyAccess::GetTextStyleSet(*RichTextWidget);
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
		FSlateFontInfo FontInfo = Design2UMGWidgetPropertyAccess::GetFont(*UnsetWidget);
		FontInfo.LetterSpacing = 321;
		UnsetWidget->SetFont(FontInfo);
		FUMGWidgetTreeBuilder::ApplyWidgetProperties(UnsetWidget, UnsetNode);
		TestEqual(TEXT("Unset letter spacing does not apply"), Design2UMGWidgetPropertyAccess::GetFont(*UnsetWidget).LetterSpacing, 321);
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

	auto BuildStyleSnapshot = [&](URichTextBlock* Widget, const TCHAR* Context, FString& OutSnapshot) -> bool
	{
		UDataTable* StyleSet = Design2UMGWidgetPropertyAccess::GetTextStyleSet(*Widget);
		TestNotNull(FString::Printf(TEXT("%s style set exists"), Context), StyleSet);
		if (!StyleSet)
		{
			return false;
		}

		const FRichTextStyleRow* BaseRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_0"), Context);
		const FRichTextStyleRow* LinkRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_1"), Context);
		TestNotNull(FString::Printf(TEXT("%s base row exists"), Context), BaseRow);
		TestNotNull(FString::Printf(TEXT("%s link row exists"), Context), LinkRow);
		if (!BaseRow || !LinkRow)
		{
			return false;
		}

		OutSnapshot = FString::Printf(
			TEXT("base:%.0f:%s|link:%.0f:%s:%d"),
			BaseRow->TextStyle.Font.Size,
			*BaseRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().ToFColor(true).ToHex(),
			LinkRow->TextStyle.Font.Size,
			*LinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().ToFColor(true).ToHex(),
			static_cast<int32>(LinkRow->TextStyle.UnderlineBrush.DrawAs));
		return true;
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
	FString ParsedSnapshot;
	FString SourceSnapshot;
	const bool bParsedSnapshot = BuildStyleSnapshot(ParsedWidget, TEXT("ParsedRichTextSerializationCycle"), ParsedSnapshot);
	const bool bSourceSnapshot = BuildStyleSnapshot(SourceWidget, TEXT("SourceRichTextSerializationCycle"), SourceSnapshot);
	if (!bParsedSnapshot || !bSourceSnapshot)
	{
		return false;
	}
	TestEqual(TEXT("Serialization rich text style snapshot matches"), ParsedSnapshot, SourceSnapshot);
	return true;
}

#endif
