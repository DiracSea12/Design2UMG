// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "UMGTextPropertyApplier.h"

#include "UMGTextStyleApplication.h"

#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"

namespace
{
	void ApplyTextBlockShadow(UTextBlock* TextBlock, const FUMGShadowData& Shadow)
	{
		if (!TextBlock)
		{
			return;
		}
		if (Shadow.ShadowOffset.IsSet())
		{
			TextBlock->SetShadowOffset(Shadow.ShadowOffset.GetValue());
		}
		if (Shadow.ShadowColorAndOpacity.IsSet())
		{
			TextBlock->SetShadowColorAndOpacity(Shadow.ShadowColorAndOpacity.GetValue());
		}
	}

	void ApplyIconShaping(FShapedTextOptions& ShapedTextOptions)
	{
		ShapedTextOptions.bOverride_TextShapingMethod = true;
		ShapedTextOptions.TextShapingMethod = ETextShapingMethod::FullShaping;
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

		if (Node.Text.Color.IsSet())
		{
			Style.SetColorAndOpacity(FSlateColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue())));
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

		if (Node.Text.Color.IsSet())
		{
			const FSlateColor TypedColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue()));
			TextStyle.ColorAndOpacity = TypedColor;
			Style.ForegroundColor = TypedColor;
			bStyleChanged = true;
		}

		if (bStyleChanged)
		{
			Style.SetTextStyle(TextStyle);
		}
	}

	void ApplyEditableTextBoxResidualProps(UEditableTextBox* Widget, const FUMGWidgetNode& Node)
	{
		if (Widget && Node.WidgetStyle.MinDesiredWidth.IsSet())
		{
			Widget->SetMinDesiredWidth(Node.WidgetStyle.MinDesiredWidth.GetValue());
		}
	}

	void ApplyEditableTextResidualProps(UEditableText* Widget, const FUMGWidgetNode& Node)
	{
		if (Widget && Node.WidgetStyle.MinDesiredWidth.IsSet())
		{
			Widget->SetMinimumDesiredWidth(Node.WidgetStyle.MinDesiredWidth.GetValue());
		}
	}

	void ApplyEditableTextJustification(UEditableTextBox* EditableTextBox, const FUMGWidgetNode& Node)
	{
		if (!EditableTextBox || !UMGTextStyleApplication::HasTypedTextValue(Node.Text.Justification))
		{
			return;
		}

		ETextJustify::Type Justification = ETextJustify::Left;
		if (UMGTextStyleApplication::TryResolveTextJustification(Node.Text.Justification.GetValue(), Justification))
		{
			EditableTextBox->SetJustification(Justification);
		}
	}

	void ApplyEditableTextJustification(UEditableText* EditableText, const FUMGWidgetNode& Node)
	{
		if (!EditableText || !UMGTextStyleApplication::HasTypedTextValue(Node.Text.Justification))
		{
			return;
		}

		ETextJustify::Type Justification = ETextJustify::Left;
		if (UMGTextStyleApplication::TryResolveTextJustification(Node.Text.Justification.GetValue(), Justification))
		{
			EditableText->SetJustification(Justification);
		}
	}
}

namespace UMGTextPropertyApplier
{
	void ApplyTextBlockProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UTextBlock* TextBlock = CastChecked<UTextBlock>(Widget);
		if (Node.Text.Text.IsSet())
		{
			TextBlock->SetText(FText::FromString(Node.Text.Text.GetValue()));
		}

		if (Node.Text.Color.IsSet())
		{
			TextBlock->SetColorAndOpacity(FSlateColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue())));
		}

		FSlateFontInfo FontInfo = TextBlock->GetFont();
		bool bFontChanged = false;
		UMGTextStyleApplication::ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			TextBlock->SetFont(FontInfo);
		}

		UMGTextStyleApplication::ApplyTypedTextLayoutProps(TextBlock, Node);
		ApplyTextBlockShadow(TextBlock, Node.Shadow);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			FShapedTextOptions IconShaping;
			ApplyIconShaping(IconShaping);
			TextBlock->SetShapedTextOptions(IconShaping);
		}
	}

	void ApplyRichTextBlockProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		URichTextBlock* RichTextBlock = CastChecked<URichTextBlock>(Widget);
		if (Node.Text.Text.IsSet())
		{
			RichTextBlock->SetText(FText::FromString(Node.Text.Text.GetValue()));
		}

		UMGTextStyleApplication::ApplyRichTextDefaultStyle(RichTextBlock, Node);

		if (Node.Text.RichTextStyleRowsJson.IsSet())
		{
			UMGTextStyleApplication::ApplyRichTextStyleRows(RichTextBlock, Node.Text.RichTextStyleRowsJson.GetValue());
		}

		UMGTextStyleApplication::ApplyTypedTextLayoutProps(RichTextBlock, Node);
	}

	void ApplyEditableTextBoxProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UEditableTextBox* EditableTextBox = CastChecked<UEditableTextBox>(Widget);
		if (Node.Text.Text.IsSet())
		{
			EditableTextBox->SetText(FText::FromString(Node.Text.Text.GetValue()));
		}
		if (Node.Text.HintText.IsSet())
		{
			EditableTextBox->SetHintText(FText::FromString(Node.Text.HintText.GetValue()));
		}

		FEditableTextBoxStyle Style = EditableTextBox->GetWidgetStyle();
		bool bStyleChanged = false;
		ApplyEditableTextBoxStyle(Style, Node, bStyleChanged);
		if (bStyleChanged)
		{
			EditableTextBox->SetWidgetStyle(Style);
		}

		ApplyEditableTextJustification(EditableTextBox, Node);
		ApplyEditableTextBoxResidualProps(EditableTextBox, Node);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			ApplyIconShaping(EditableTextBox->ShapedTextOptions);
		}
	}

	void ApplyMultiLineEditableTextBoxProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UMultiLineEditableTextBox* EditableTextBox = CastChecked<UMultiLineEditableTextBox>(Widget);
		if (Node.Text.Text.IsSet())
		{
			EditableTextBox->SetText(FText::FromString(Node.Text.Text.GetValue()));
		}
		if (Node.Text.HintText.IsSet())
		{
			EditableTextBox->SetHintText(FText::FromString(Node.Text.HintText.GetValue()));
		}

		FEditableTextBoxStyle Style = EditableTextBox->WidgetStyle;
		bool bStyleChanged = false;
		ApplyEditableTextBoxStyle(Style, Node, bStyleChanged);
		if (bStyleChanged)
		{
			EditableTextBox->WidgetStyle = Style;
			EditableTextBox->SetTextStyle(Style.TextStyle);
			if (Node.Text.Color.IsSet())
			{
				EditableTextBox->SetForegroundColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue()));
			}
		}

		UMGTextStyleApplication::ApplyTypedTextLayoutProps(EditableTextBox, Node);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			FShapedTextOptions IconShaping;
			ApplyIconShaping(IconShaping);
			EditableTextBox->SetShapedTextOptions(IconShaping);
		}
	}

	void ApplyEditableTextProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UEditableText* EditableText = CastChecked<UEditableText>(Widget);
		if (Node.Text.Text.IsSet())
		{
			EditableText->SetText(FText::FromString(Node.Text.Text.GetValue()));
		}
		if (Node.Text.HintText.IsSet())
		{
			EditableText->SetHintText(FText::FromString(Node.Text.HintText.GetValue()));
		}

		FEditableTextStyle Style = EditableText->WidgetStyle;
		bool bStyleChanged = false;
		ApplyEditableTextStyle(Style, Node, bStyleChanged);
		if (bStyleChanged)
		{
			EditableText->SetWidgetStyle(Style);
		}

		ApplyEditableTextJustification(EditableText, Node);
		ApplyEditableTextResidualProps(EditableText, Node);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			ApplyIconShaping(EditableText->ShapedTextOptions);
		}
	}

	void ApplyMultiLineEditableTextProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UMultiLineEditableText* EditableText = CastChecked<UMultiLineEditableText>(Widget);
		if (Node.Text.Text.IsSet())
		{
			EditableText->SetText(FText::FromString(Node.Text.Text.GetValue()));
		}
		if (Node.Text.HintText.IsSet())
		{
			EditableText->SetHintText(FText::FromString(Node.Text.HintText.GetValue()));
		}

		FTextBlockStyle Style = EditableText->WidgetStyle;
		FSlateFontInfo FontInfo = Style.Font;
		bool bFontChanged = false;
		bool bStyleChanged = false;
		UMGTextStyleApplication::ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			Style.Font = FontInfo;
			bStyleChanged = true;
		}
		if (Node.Text.Color.IsSet())
		{
			Style.ColorAndOpacity = FSlateColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue()));
			bStyleChanged = true;
		}
		if (bStyleChanged)
		{
			EditableText->SetWidgetStyle(Style);
		}

		UMGTextStyleApplication::ApplyTypedTextLayoutProps(EditableText, Node);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			FShapedTextOptions IconShaping;
			ApplyIconShaping(IconShaping);
			EditableText->SetShapedTextOptions(IconShaping);
		}
	}
}
