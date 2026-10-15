// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "UMGTextPropertyApplier.h"

#include "UMGTextStyleApplication.h"
#include "Design2UMGWidgetPropertyAccess.h"

#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"
#include "Fonts/FontCache.h"

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
		else if (!Shadow.ShadowColorAndOpacity.IsSet() && Design2UMGWidgetPropertyAccess::GetShadowColorAndOpacity(*TextBlock).A == 0.0f)
		{
			// Slate counts even a transparent shadow offset in desired size.
			TextBlock->SetShadowOffset(FVector2D::ZeroVector);
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

	const FShapedTextOptions* ResolveIconShaping(
		const FUMGWidgetNode& Node,
		FShapedTextOptions& OutShapedTextOptions)
	{
		if (!UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			return nullptr;
		}

		ApplyIconShaping(OutShapedTextOptions);
		return &OutShapedTextOptions;
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
		FSlateFontInfo FontInfo = Design2UMGWidgetPropertyAccess::GetFont(Style);
		bool bFontChanged = false;
		UMGTextStyleApplication::ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			Design2UMGWidgetPropertyAccess::SetFont(Style, FontInfo);
			bStyleChanged = true;
		}

		if (Node.Text.Color.IsSet())
		{
			const FSlateColor TypedColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue()));
			Design2UMGWidgetPropertyAccess::SetTextColor(Style, TypedColor);
			bStyleChanged = true;
		}
	}

	void ApplyEditableTextBoxResidualProps(UEditableTextBox* Widget, const FUMGWidgetNode& Node)
	{
		if (Widget && Node.WidgetStyle.MinDesiredWidth.IsSet())
		{
			Design2UMGWidgetPropertyAccess::SetMinimumDesiredWidth(*Widget, Node.WidgetStyle.MinDesiredWidth.GetValue());
		}
	}

	void ApplyEditableTextResidualProps(UEditableText* Widget, const FUMGWidgetNode& Node)
	{
		if (Widget && Node.WidgetStyle.MinDesiredWidth.IsSet())
		{
			Design2UMGWidgetPropertyAccess::SetMinimumDesiredWidth(*Widget, Node.WidgetStyle.MinDesiredWidth.GetValue());
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

		FSlateFontInfo FontInfo = Design2UMGWidgetPropertyAccess::GetFont(*TextBlock);
		bool bFontChanged = false;
		UMGTextStyleApplication::ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			TextBlock->SetFont(FontInfo);
		}

		FShapedTextOptions IconShaping;
		UMGTextStyleApplication::ApplyTypedTextLayoutProps(
			TextBlock, Node, ResolveIconShaping(Node, IconShaping));
		ApplyTextBlockShadow(TextBlock, Node.Shadow);
	}

	void ApplyRichTextBlockProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		URichTextBlock* RichTextBlock = CastChecked<URichTextBlock>(Widget);
		if (Node.Text.Text.IsSet())
		{
			RichTextBlock->SetText(FText::FromString(Node.Text.Text.GetValue()));
		}

		UMGTextStyleApplication::ApplyRichTextDefaultStyle(RichTextBlock, Node);

		if (Node.Text.RichTextStyleRows.IsSet())
		{
			UMGTextStyleApplication::ApplyRichTextStyleRows(RichTextBlock, Node.Text.RichTextStyleRows.GetValue());
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

		FEditableTextBoxStyle Style = EditableTextBox->WidgetStyle;
		bool bStyleChanged = false;
		ApplyEditableTextBoxStyle(Style, Node, bStyleChanged);
		if (bStyleChanged)
		{
			Design2UMGWidgetPropertyAccess::SetWidgetStyle(*EditableTextBox, Style);
		}

		ApplyEditableTextJustification(EditableTextBox, Node);
		ApplyEditableTextBoxResidualProps(EditableTextBox, Node);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			FShapedTextOptions ShapedTextOptions = EditableTextBox->ShapedTextOptions;
			ApplyIconShaping(ShapedTextOptions);
			Design2UMGWidgetPropertyAccess::SetShapedTextOptions(*EditableTextBox, ShapedTextOptions);
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
			Design2UMGWidgetPropertyAccess::SetWidgetStyle(*EditableTextBox, Style);
			if (Node.Text.Color.IsSet())
			{
				EditableTextBox->SetForegroundColor(UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue()));
			}
		}

		FShapedTextOptions IconShaping;
		UMGTextStyleApplication::ApplyTypedTextLayoutProps(
			EditableTextBox, Node, ResolveIconShaping(Node, IconShaping));
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
			Design2UMGWidgetPropertyAccess::SetWidgetStyle(*EditableText, Style);
		}

		ApplyEditableTextJustification(EditableText, Node);
		ApplyEditableTextResidualProps(EditableText, Node);

		if (UMGTextStyleApplication::IsTypedIconFontWidget(Node))
		{
			FShapedTextOptions ShapedTextOptions = EditableText->ShapedTextOptions;
			ApplyIconShaping(ShapedTextOptions);
			Design2UMGWidgetPropertyAccess::SetShapedTextOptions(*EditableText, ShapedTextOptions);
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
			Design2UMGWidgetPropertyAccess::SetWidgetStyle(*EditableText, Style);
		}

		FShapedTextOptions IconShaping;
		UMGTextStyleApplication::ApplyTypedTextLayoutProps(
			EditableText, Node, ResolveIconShaping(Node, IconShaping));
	}
}
