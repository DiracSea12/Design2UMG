// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "UMGTextPropertyApplier.h"

#include "UMGTextStyleApplication.h"
#include "UMGPropertyApplierSupport.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"

#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"
namespace
{
	using namespace UMGPropertyApplierSupport;

	void ApplyTextBlockResidualProps(UTextBlock* TextBlock, const FUMGPropertyBag& Properties)
	{
		if (!TextBlock)
		{
			return;
		}

		FVector2D ShadowOffset;
		if (Properties.TryGetVector2D(UMGPropertyKeys::ShadowOffset, ShadowOffset))
		{
			TextBlock->SetShadowOffset(ShadowOffset);
		}

		FLinearColor ShadowColor;
		if (Properties.TryGetColor(UMGPropertyKeys::ShadowColorAndOpacity, ShadowColor))
		{
			TextBlock->SetShadowColorAndOpacity(ShadowColor);
		}
	}

	void ApplyEditableTextStyle(FEditableTextStyle& Style, const FUMGWidgetNode& Node, bool& bStyleChanged)
	{
		FSlateFontInfo FontInfo = Style.Font;
		bool bFontChanged = false;
		UMGTextStyleApplication::ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			Style.SetFont(FontInfo);
			bStyleChanged = true;
		}

		if (Node.Text.bHasColor)
		{
			Style.SetColorAndOpacity(FSlateColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color)));
			bStyleChanged = true;
		}
	}

	void ApplyEditableTextBoxStyle(FEditableTextBoxStyle& Style, const FUMGWidgetNode& Node, bool& bStyleChanged)
	{
		FTextBlockStyle TextStyle = Style.TextStyle;
		FSlateFontInfo FontInfo = TextStyle.Font;
		bool bFontChanged = false;
		UMGTextStyleApplication::ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			TextStyle.Font = FontInfo;
			bStyleChanged = true;
		}

		if (Node.Text.bHasColor)
		{
			const FSlateColor TypedColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color));
			TextStyle.ColorAndOpacity = TypedColor;
			Style.ForegroundColor = TypedColor;
			bStyleChanged = true;
		}

		if (bStyleChanged)
		{
			Style.SetTextStyle(TextStyle);
		}
	}

	void ApplyEditableTextBoxResidualProps(UEditableTextBox* Widget, const FUMGPropertyBag& Properties)
	{
		if (!Widget)
		{
			return;
		}

		bool bIsReadOnly = false;
		if (Properties.TryGetBool(UMGPropertyKeys::IsReadOnly, bIsReadOnly))
		{
			Widget->SetIsReadOnly(bIsReadOnly);
		}

		bool bIsPassword = false;
		if (Properties.TryGetBool(UMGPropertyKeys::IsPassword, bIsPassword))
		{
			Widget->SetIsPassword(bIsPassword);
		}

		float MinimumDesiredWidth = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::MinimumDesiredWidth, MinimumDesiredWidth))
		{
			Widget->SetMinDesiredWidth(MinimumDesiredWidth);
		}
	}

	void ApplyEditableTextResidualProps(UEditableText* Widget, const FUMGPropertyBag& Properties)
	{
		if (!Widget)
		{
			return;
		}

		bool bIsReadOnly = false;
		if (Properties.TryGetBool(UMGPropertyKeys::IsReadOnly, bIsReadOnly))
		{
			Widget->SetIsReadOnly(bIsReadOnly);
		}

		bool bIsPassword = false;
		if (Properties.TryGetBool(UMGPropertyKeys::IsPassword, bIsPassword))
		{
			Widget->SetIsPassword(bIsPassword);
		}

		float MinimumDesiredWidth = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::MinimumDesiredWidth, MinimumDesiredWidth))
		{
			Widget->SetMinimumDesiredWidth(MinimumDesiredWidth);
		}
	}

	void ApplyMultiLineEditableTextResidualProps(UMultiLineEditableText* Widget, const FUMGPropertyBag& Properties)
	{
		if (!Widget)
		{
			return;
		}

		bool bIsReadOnly = false;
		if (Properties.TryGetBool(UMGPropertyKeys::IsReadOnly, bIsReadOnly))
		{
			Widget->SetIsReadOnly(bIsReadOnly);
		}
	}

	void ApplyMultiLineEditableTextBoxResidualProps(UMultiLineEditableTextBox* Widget, const FUMGPropertyBag& Properties)
	{
		if (!Widget)
		{
			return;
		}

		bool bIsReadOnly = false;
		if (Properties.TryGetBool(UMGPropertyKeys::IsReadOnly, bIsReadOnly))
		{
			Widget->SetIsReadOnly(bIsReadOnly);
		}
	}
}

namespace UMGTextPropertyApplier
{
	using namespace UMGPropertyApplierSupport;

	void ApplyTextBlockProps(UWidget* Widget, const FUMGPropertyBag& Properties)
	{
		UTextBlock* TextBlock = CastChecked<UTextBlock>(Widget);
		FString Value;
		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::Text, Value))
		{
			TextBlock->SetText(FText::FromString(Value));
		}

		FLinearColor TextColor;
		if (Properties.TryGetColor(UMGPropertyKeys::Color, TextColor))
		{
			TextBlock->SetColorAndOpacity(FSlateColor(TextColor));
		}

		FSlateFontInfo FontInfo = TextBlock->GetFont();
		bool bFontChanged = false;
		const bool bIsIconFontWidget = HasProp(Properties, UMGPropertyKeys::IconName);
		UMGTextStyleApplication::ApplyLegacyTextFontInfo(FontInfo, Properties, bIsIconFontWidget, bFontChanged);
		if (bFontChanged)
		{
			TextBlock->SetFont(FontInfo);
		}

		ApplyTextBlockResidualProps(TextBlock, Properties);
		UMGTextStyleApplication::ApplyLegacyTextLayoutProps(TextBlock, Properties);

		if (bIsIconFontWidget)
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			TextBlock->SetShapedTextOptions(IconShaping);
		}
	}

	void ApplyTextBlockProps(UWidget* Widget, const FUMGWidgetNode& Node, const FUMGPropertyBag& ResidualProperties)
	{
		UTextBlock* TextBlock = CastChecked<UTextBlock>(Widget);
		if (Node.Text.bHasText)
		{
			TextBlock->SetText(FText::FromString(Node.Text.Text));
		}

		if (Node.Text.bHasColor)
		{
			TextBlock->SetColorAndOpacity(FSlateColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color)));
		}

		FSlateFontInfo FontInfo = TextBlock->GetFont();
		bool bFontChanged = false;
		UMGTextStyleApplication::ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			TextBlock->SetFont(FontInfo);
		}

		UMGTextStyleApplication::ApplyTypedTextLayoutProps(TextBlock, Node);
		ApplyTextBlockResidualProps(TextBlock, ResidualProperties);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			TextBlock->SetShapedTextOptions(IconShaping);
		}
	}

	void ApplyEditableTextBoxProps(UWidget* Widget, const FUMGPropertyBag& Properties)
	{
		UEditableTextBox* EditableTextBox = CastChecked<UEditableTextBox>(Widget);
		FString Value;
		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::Text, Value))
		{
			EditableTextBox->SetText(FText::FromString(Value));
		}
		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::HintText, Value))
		{
			EditableTextBox->SetHintText(FText::FromString(Value));
		}

		FEditableTextBoxStyle Style = EditableTextBox->GetWidgetStyle();
		FTextBlockStyle TextStyle = Style.TextStyle;
		FSlateFontInfo FontInfo = TextStyle.Font;
		bool bStyleChanged = false;
		bool bFontChanged = false;
		const bool bIsIconFontWidget = HasProp(Properties, UMGPropertyKeys::IconName);
		UMGTextStyleApplication::ApplyLegacyTextFontInfo(FontInfo, Properties, bIsIconFontWidget, bFontChanged);
		if (bFontChanged)
		{
			TextStyle.Font = FontInfo;
			bStyleChanged = true;
		}

		FLinearColor TextColor;
		if (Properties.TryGetColor(UMGPropertyKeys::Color, TextColor))
		{
			const FSlateColor ForegroundColor(TextColor);
			TextStyle.ColorAndOpacity = ForegroundColor;
			Style.ForegroundColor = ForegroundColor;
			bStyleChanged = true;
		}

		if (bStyleChanged)
		{
			Style.SetTextStyle(TextStyle);
			EditableTextBox->SetWidgetStyle(Style);
		}

		UMGTextStyleApplication::ApplyLegacyTextLayoutProps(EditableTextBox, Properties);
		ApplyEditableTextBoxResidualProps(EditableTextBox, Properties);

		if (bIsIconFontWidget)
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			EditableTextBox->ShapedTextOptions = IconShaping;
		}
	}

	void ApplyEditableTextBoxProps(UWidget* Widget, const FUMGWidgetNode& Node, const FUMGPropertyBag& ResidualProperties)
	{
		UEditableTextBox* EditableTextBox = CastChecked<UEditableTextBox>(Widget);
		if (Node.Text.bHasText)
		{
			EditableTextBox->SetText(FText::FromString(Node.Text.Text));
		}
		if (Node.Text.bHasHintText)
		{
			EditableTextBox->SetHintText(FText::FromString(Node.Text.HintText));
		}

		FEditableTextBoxStyle Style = EditableTextBox->GetWidgetStyle();
		bool bStyleChanged = false;
		ApplyEditableTextBoxStyle(Style, Node, bStyleChanged);
		if (bStyleChanged)
		{
			EditableTextBox->SetWidgetStyle(Style);
		}

		if (UMGTextStyleApplication::HasTypedTextValue(Node.Text.bHasJustification, Node.Text.Justification))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (UMGTextStyleApplication::TryResolveTextJustification(Node.Text.Justification, Justification))
			{
				EditableTextBox->SetJustification(Justification);
			}
		}

		ApplyEditableTextBoxResidualProps(EditableTextBox, ResidualProperties);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			EditableTextBox->ShapedTextOptions = IconShaping;
		}
	}

	void ApplyMultiLineEditableTextBoxProps(UWidget* Widget, const FUMGPropertyBag& Properties)
	{
		UMultiLineEditableTextBox* EditableTextBox = CastChecked<UMultiLineEditableTextBox>(Widget);
		FString Value;
		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::Text, Value))
		{
			EditableTextBox->SetText(FText::FromString(Value));
		}
		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::HintText, Value))
		{
			EditableTextBox->SetHintText(FText::FromString(Value));
		}

		FTextBlockStyle TextStyle = EditableTextBox->WidgetStyle.TextStyle;
		FSlateFontInfo FontInfo = TextStyle.Font;
		bool bStyleChanged = false;
		bool bFontChanged = false;
		const bool bIsIconFontWidget = HasProp(Properties, UMGPropertyKeys::IconName);
		UMGTextStyleApplication::ApplyLegacyTextFontInfo(FontInfo, Properties, bIsIconFontWidget, bFontChanged);
		if (bFontChanged)
		{
			TextStyle.Font = FontInfo;
			bStyleChanged = true;
		}

		FLinearColor TextColor;
		if (Properties.TryGetColor(UMGPropertyKeys::Color, TextColor))
		{
			const FSlateColor TypedColor(TextColor);
			TextStyle.ColorAndOpacity = TypedColor;
			EditableTextBox->WidgetStyle.ForegroundColor = TypedColor;
			bStyleChanged = true;
		}

		if (bStyleChanged)
		{
			EditableTextBox->WidgetStyle.SetTextStyle(TextStyle);
			EditableTextBox->SetTextStyle(TextStyle);
		}

		if (bStyleChanged && Properties.TryGetColor(UMGPropertyKeys::Color, TextColor))
		{
			EditableTextBox->SetForegroundColor(TextColor);
		}
		UMGTextStyleApplication::ApplyLegacyTextLayoutProps(EditableTextBox, Properties);
		ApplyMultiLineEditableTextBoxResidualProps(EditableTextBox, Properties);

		if (bIsIconFontWidget)
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			EditableTextBox->SetShapedTextOptions(IconShaping);
		}
	}

	void ApplyMultiLineEditableTextBoxProps(UWidget* Widget, const FUMGWidgetNode& Node, const FUMGPropertyBag& ResidualProperties)
	{
		UMultiLineEditableTextBox* EditableTextBox = CastChecked<UMultiLineEditableTextBox>(Widget);
		if (Node.Text.bHasText)
		{
			EditableTextBox->SetText(FText::FromString(Node.Text.Text));
		}
		if (Node.Text.bHasHintText)
		{
			EditableTextBox->SetHintText(FText::FromString(Node.Text.HintText));
		}

		FEditableTextBoxStyle Style = EditableTextBox->WidgetStyle;
		bool bStyleChanged = false;
		ApplyEditableTextBoxStyle(Style, Node, bStyleChanged);
		if (bStyleChanged)
		{
			EditableTextBox->WidgetStyle = Style;
			EditableTextBox->SetTextStyle(Style.TextStyle);
			if (Node.Text.bHasColor)
			{
				EditableTextBox->SetForegroundColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color));
			}
		}

		UMGTextStyleApplication::ApplyTypedTextLayoutProps(EditableTextBox, Node);
		ApplyMultiLineEditableTextBoxResidualProps(EditableTextBox, ResidualProperties);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			EditableTextBox->SetShapedTextOptions(IconShaping);
		}
	}

	void ApplyEditableTextProps(UWidget* Widget, const FUMGPropertyBag& Properties)
	{
		UEditableText* EditableText = CastChecked<UEditableText>(Widget);
		FString Value;
		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::Text, Value))
		{
			EditableText->SetText(FText::FromString(Value));
		}
		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::HintText, Value))
		{
			EditableText->SetHintText(FText::FromString(Value));
		}

		FEditableTextStyle Style = EditableText->WidgetStyle;
		bool bStyleChanged = false;
		FSlateFontInfo FontInfo = Style.Font;
		bool bFontChanged = false;
		const bool bIsIconFontWidget = HasProp(Properties, UMGPropertyKeys::IconName);
		UMGTextStyleApplication::ApplyLegacyTextFontInfo(FontInfo, Properties, bIsIconFontWidget, bFontChanged);
		if (bFontChanged)
		{
			Style.SetFont(FontInfo);
			bStyleChanged = true;
		}

		FLinearColor TextColor;
		if (Properties.TryGetColor(UMGPropertyKeys::Color, TextColor))
		{
			Style.SetColorAndOpacity(FSlateColor(TextColor));
			bStyleChanged = true;
		}

		if (bStyleChanged)
		{
			EditableText->SetWidgetStyle(Style);
		}

		if (TryGetProp(Properties, UMGPropertyKeys::Justification, Value))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (UMGTextStyleApplication::TryResolveTextJustification(Value, Justification))
			{
				EditableText->SetJustification(Justification);
			}
		}

		ApplyEditableTextResidualProps(EditableText, Properties);

		if (bIsIconFontWidget)
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			EditableText->ShapedTextOptions = IconShaping;
		}
	}

	void ApplyEditableTextProps(UWidget* Widget, const FUMGWidgetNode& Node, const FUMGPropertyBag& ResidualProperties)
	{
		UEditableText* EditableText = CastChecked<UEditableText>(Widget);
		if (Node.Text.bHasText)
		{
			EditableText->SetText(FText::FromString(Node.Text.Text));
		}
		if (Node.Text.bHasHintText)
		{
			EditableText->SetHintText(FText::FromString(Node.Text.HintText));
		}

		FEditableTextStyle Style = EditableText->WidgetStyle;
		bool bStyleChanged = false;
		ApplyEditableTextStyle(Style, Node, bStyleChanged);
		if (bStyleChanged)
		{
			EditableText->SetWidgetStyle(Style);
		}

		if (UMGTextStyleApplication::HasTypedTextValue(Node.Text.bHasJustification, Node.Text.Justification))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (UMGTextStyleApplication::TryResolveTextJustification(Node.Text.Justification, Justification))
			{
				EditableText->SetJustification(Justification);
			}
		}

		ApplyEditableTextResidualProps(EditableText, ResidualProperties);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			EditableText->ShapedTextOptions = IconShaping;
		}
	}

	void ApplyMultiLineEditableTextProps(UWidget* Widget, const FUMGPropertyBag& Properties)
	{
		UMultiLineEditableText* EditableText = CastChecked<UMultiLineEditableText>(Widget);
		FString Value;
		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::Text, Value))
		{
			EditableText->SetText(FText::FromString(Value));
		}
		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::HintText, Value))
		{
			EditableText->SetHintText(FText::FromString(Value));
		}

		FTextBlockStyle Style = EditableText->WidgetStyle;
		FSlateFontInfo FontInfo = Style.Font;
		bool bStyleChanged = false;
		bool bFontChanged = false;
		const bool bIsIconFontWidget = HasProp(Properties, UMGPropertyKeys::IconName);
		UMGTextStyleApplication::ApplyLegacyTextFontInfo(FontInfo, Properties, bIsIconFontWidget, bFontChanged);
		if (bFontChanged)
		{
			Style.Font = FontInfo;
			bStyleChanged = true;
		}

		FLinearColor TextColor;
		if (Properties.TryGetColor(UMGPropertyKeys::Color, TextColor))
		{
			Style.ColorAndOpacity = FSlateColor(TextColor);
			bStyleChanged = true;
		}

		if (bStyleChanged)
		{
			EditableText->SetWidgetStyle(Style);
		}

		UMGTextStyleApplication::ApplyLegacyTextLayoutProps(EditableText, Properties);
		ApplyMultiLineEditableTextResidualProps(EditableText, Properties);

		if (bIsIconFontWidget)
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			EditableText->SetShapedTextOptions(IconShaping);
		}
	}

	void ApplyMultiLineEditableTextProps(UWidget* Widget, const FUMGWidgetNode& Node, const FUMGPropertyBag& ResidualProperties)
	{
		UMultiLineEditableText* EditableText = CastChecked<UMultiLineEditableText>(Widget);
		if (Node.Text.bHasText)
		{
			EditableText->SetText(FText::FromString(Node.Text.Text));
		}
		if (Node.Text.bHasHintText)
		{
			EditableText->SetHintText(FText::FromString(Node.Text.HintText));
		}

		FTextBlockStyle Style = EditableText->WidgetStyle;
		bool bStyleChanged = false;
		FSlateFontInfo FontInfo = Style.Font;
		bool bFontChanged = false;
		UMGTextStyleApplication::ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			Style.Font = FontInfo;
			bStyleChanged = true;
		}
		if (Node.Text.bHasColor)
		{
			Style.ColorAndOpacity = FSlateColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color));
			bStyleChanged = true;
		}
		if (bStyleChanged)
		{
			EditableText->SetWidgetStyle(Style);
		}

		UMGTextStyleApplication::ApplyTypedTextLayoutProps(EditableText, Node);
		ApplyMultiLineEditableTextResidualProps(EditableText, ResidualProperties);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			EditableText->SetShapedTextOptions(IconShaping);
		}
	}

	void ApplyRichTextBlockProps(UWidget* Widget, const FUMGPropertyBag& Properties)
	{
		URichTextBlock* RichTextBlock = CastChecked<URichTextBlock>(Widget);
		FString Value;
		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::Text, Value))
		{
			RichTextBlock->SetText(FText::FromString(Value));
		}

		UMGTextStyleApplication::ApplyRichTextDefaultStyle(RichTextBlock, Properties);

		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::RichTextStyleRows, Value))
		{
			UMGTextStyleApplication::ApplyRichTextStyleRows(RichTextBlock, Value);
		}
		UMGTextStyleApplication::ApplyLegacyTextLayoutProps(RichTextBlock, Properties);
	}

	void ApplyRichTextBlockProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		URichTextBlock* RichTextBlock = CastChecked<URichTextBlock>(Widget);
		if (Node.Text.bHasText)
		{
			RichTextBlock->SetText(FText::FromString(Node.Text.Text));
		}

		UMGTextStyleApplication::ApplyRichTextDefaultStyle(RichTextBlock, Node);

		if (Node.Text.bHasRichTextStyleRows)
		{
			UMGTextStyleApplication::ApplyRichTextStyleRows(RichTextBlock, Node.Text.RichTextStyleRowsJson);
		}

		UMGTextStyleApplication::ApplyTypedTextLayoutProps(RichTextBlock, Node);
	}
}

