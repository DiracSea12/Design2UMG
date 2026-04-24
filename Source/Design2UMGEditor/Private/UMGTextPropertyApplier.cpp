// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "UMGTextPropertyApplier.h"

#include "DesignResourceResolver.h"
#include "TextStyleUtils.h"
#include "UMGPropertyApplierSupport.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"

#include "Brushes/SlateColorBrush.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using namespace UMGPropertyApplierSupport;

	FSlateBrush MakeTextDecorationBrush(const FLinearColor& Color)
	{
		FSlateColorBrush Brush(Color);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.ImageSize = FVector2D(1.0f, 1.0f);
		Brush.Margin = FMargin(0.0f);
		return Brush;
	}

	FTextBlockStyle ResolveRichTextBaseStyle(const URichTextBlock* RichTextBlock)
	{
		FTextBlockStyle BaseStyle = FTextBlockStyle::GetDefault();
		if (!RichTextBlock)
		{
			return BaseStyle;
		}

		const FBoolProperty* OverrideFlagProperty =
			FindFProperty<FBoolProperty>(URichTextBlock::StaticClass(), TEXT("bOverrideDefaultStyle"));
		const FStructProperty* OverrideStyleProperty =
			FindFProperty<FStructProperty>(URichTextBlock::StaticClass(), TEXT("DefaultTextStyleOverride"));
		const FStructProperty* DefaultStyleProperty =
			FindFProperty<FStructProperty>(URichTextBlock::StaticClass(), TEXT("DefaultTextStyle"));

		if (OverrideFlagProperty && OverrideStyleProperty
			&& OverrideFlagProperty->GetPropertyValue_InContainer(RichTextBlock))
		{
			if (const FTextBlockStyle* OverrideStyle =
				OverrideStyleProperty->ContainerPtrToValuePtr<FTextBlockStyle>(RichTextBlock))
			{
				return *OverrideStyle;
			}
		}

		if (DefaultStyleProperty)
		{
			if (const FTextBlockStyle* DefaultStyle =
				DefaultStyleProperty->ContainerPtrToValuePtr<FTextBlockStyle>(RichTextBlock))
			{
				return *DefaultStyle;
			}
		}

		return BaseStyle;
	}

	bool TryResolveTextJustification(const FString& Value, ETextJustify::Type& OutJustification)
	{
		static const TMap<FString, ETextJustify::Type> JustifyMap = {
			{TEXT("Left"), ETextJustify::Left},
			{TEXT("Center"), ETextJustify::Center},
			{TEXT("Right"), ETextJustify::Right},
		};

		if (const ETextJustify::Type* Justification = JustifyMap.Find(Value))
		{
			OutJustification = *Justification;
			return true;
		}

		return false;
	}

	bool HasTypedTextValue(const bool bHasValue, const FString& Value)
	{
		return bHasValue && !Value.IsEmpty();
	}

	bool IsTypedIconFontWidget(const FUMGWidgetNode& Node)
	{
		return HasTypedTextValue(Node.Text.bHasIconName, Node.Text.IconName);
	}

	FLinearColor NormalizeTypedTextColor(const FLinearColor& Color)
	{
		return Color;
	}

	void ApplyTypedFontInfo(FSlateFontInfo& FontInfo, const FUMGWidgetNode& Node, bool& bFontChanged)
	{
		if (Node.TextStyle.FontSize > 0.0f)
		{
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(Node.TextStyle.FontSize));
			bFontChanged = true;
		}

		if (HasTypedTextValue(Node.Text.bHasFontFamily, Node.Text.FontFamily))
		{
			DesignResourceResolver::ApplyFontFamily(FontInfo, Node.Text.FontFamily, IsTypedIconFontWidget(Node));
			bFontChanged = true;
		}

		if (HasTypedTextValue(Node.Text.bHasTypefaceFontName, Node.Text.TypefaceFontName))
		{
			FontInfo.TypefaceFontName = FName(*Node.Text.TypefaceFontName);
			bFontChanged = true;
		}

		if (Node.TextStyle.bHasLetterSpacing)
		{
			const float DesignFontSize = Node.TextStyle.SourceFontSize;
			if (FontInfo.Size > 0 || DesignFontSize > 0.0f)
			{
				FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(
					Node.TextStyle.LetterSpacing,
					DesignFontSize,
					FontInfo.Size);
			}
			bFontChanged = true;
		}
	}

	void ApplyTypedTextLayoutProps(UTextLayoutWidget* TextWidget, const FUMGWidgetNode& Node)
	{
		if (!TextWidget)
		{
			return;
		}

		ETextJustify::Type Justification = ETextJustify::Left;
		if (HasTypedTextValue(Node.Text.bHasJustification, Node.Text.Justification)
			&& TryResolveTextJustification(Node.Text.Justification, Justification))
		{
			TextWidget->SetJustification(Justification);
		}

		if (Node.Text.bHasLineHeightPercentage)
		{
			TextWidget->SetLineHeightPercentage(Node.Text.LineHeightPercentage);
			TextWidget->SetApplyLineHeightToBottomLine(true);
		}

		if (Node.Text.bHasAutoWrapText)
		{
			TextWidget->SetAutoWrapText(Node.Text.bAutoWrapText);
		}

		if (Node.Text.bHasWrapTextAt)
		{
			TextWidget->SetWrapTextAt(Node.Text.WrapTextAt);
		}

		if (Node.WidgetStyle.Padding.IsSet())
		{
			TextWidget->SetMargin(Node.WidgetStyle.Padding.GetValue());
		}
	}

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
		ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			Style.SetFont(FontInfo);
			bStyleChanged = true;
		}

		if (Node.Text.bHasColor)
		{
			Style.SetColorAndOpacity(FSlateColor(NormalizeTypedTextColor(Node.Text.Color)));
			bStyleChanged = true;
		}
	}

	void ApplyEditableTextBoxStyle(FEditableTextBoxStyle& Style, const FUMGWidgetNode& Node, bool& bStyleChanged)
	{
		FTextBlockStyle TextStyle = Style.TextStyle;
		FSlateFontInfo FontInfo = TextStyle.Font;
		bool bFontChanged = false;
		ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			TextStyle.Font = FontInfo;
			bStyleChanged = true;
		}

		if (Node.Text.bHasColor)
		{
			const FSlateColor TypedColor(NormalizeTypedTextColor(Node.Text.Color));
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

	void ApplyRichTextDefaultStyle(URichTextBlock* RichTextBlock, const FUMGPropertyBag& Properties)
	{
		if (!RichTextBlock)
		{
			return;
		}

		FLinearColor IgnoredColor;
		float IgnoredFloat = 0.0f;
		bool IgnoredBool = false;
		FString IgnoredString;
		const bool bHasAnyDefaultStyleInput =
			Properties.TryGetColor(UMGPropertyKeys::Color, IgnoredColor)
			|| Properties.TryGetFloat(UMGPropertyKeys::FontSize, IgnoredFloat)
			|| TryGetProp(Properties, UMGPropertyKeys::FontFamily, IgnoredString)
			|| TryGetProp(Properties, UMGPropertyKeys::TypefaceFontName, IgnoredString)
			|| Properties.TryGetFloat(UMGPropertyKeys::LetterSpacing, IgnoredFloat)
			|| Properties.TryGetBool(UMGPropertyKeys::Underline, IgnoredBool)
			|| Properties.TryGetBool(UMGPropertyKeys::Strikethrough, IgnoredBool)
			|| TryGetProp(Properties, UMGPropertyKeys::Href, IgnoredString);
		if (!bHasAnyDefaultStyleInput)
		{
			return;
		}

		FTextBlockStyle DefaultStyle = ResolveRichTextBaseStyle(RichTextBlock);
		FLinearColor TextColor;
		if (Properties.TryGetColor(UMGPropertyKeys::Color, TextColor))
		{
			DefaultStyle.ColorAndOpacity = FSlateColor(TextColor);
		}

		FSlateFontInfo FontInfo = DefaultStyle.Font;
		bool bFontChanged = false;
		const bool bIsIconFontWidget = HasProp(Properties, UMGPropertyKeys::IconName);
		float FontSize = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::FontSize, FontSize))
		{
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(FontSize));
			bFontChanged = true;
		}

		FString Value;
		if (TryGetProp(Properties, UMGPropertyKeys::FontFamily, Value))
		{
			DesignResourceResolver::ApplyFontFamily(FontInfo, Value, bIsIconFontWidget);
			bFontChanged = true;
		}
		if (TryGetProp(Properties, UMGPropertyKeys::TypefaceFontName, Value))
		{
			FontInfo.TypefaceFontName = FName(*Value);
			bFontChanged = true;
		}

		float PixelSpacing = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::LetterSpacing, PixelSpacing))
		{
			float DesignFontSize = 0.0f;
			Properties.TryGetFloat(UMGPropertyKeys::DesignFontSize, DesignFontSize);
			if (FontInfo.Size > 0 || DesignFontSize > 0.0f)
			{
				FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(PixelSpacing, DesignFontSize, FontInfo.Size);
			}
			bFontChanged = true;
		}

		if (bFontChanged)
		{
			DefaultStyle.Font = FontInfo;
		}

		bool bUnderline = false;
		const bool bHasUnderlineValue = Properties.TryGetBool(UMGPropertyKeys::Underline, bUnderline);
		bool bStrikethrough = false;
		const bool bHasStrikethroughValue = Properties.TryGetBool(UMGPropertyKeys::Strikethrough, bStrikethrough);
		FString HrefValue;
		const bool bHasHrefValue = TryGetProp(Properties, UMGPropertyKeys::Href, HrefValue);
		const FLinearColor DecorationColor = DefaultStyle.ColorAndOpacity.GetSpecifiedColor();
		if ((bHasUnderlineValue && bUnderline) || bHasHrefValue)
		{
			DefaultStyle.UnderlineBrush = MakeTextDecorationBrush(DecorationColor);
		}
		if (bHasStrikethroughValue && bStrikethrough)
		{
			DefaultStyle.StrikeBrush = MakeTextDecorationBrush(DecorationColor);
		}

		RichTextBlock->SetDefaultTextStyle(DefaultStyle);
	}

	void ApplyRichTextDefaultStyle(URichTextBlock* RichTextBlock, const FUMGWidgetNode& Node)
	{
		if (!RichTextBlock)
		{
			return;
		}

		const bool bHasAnyDefaultStyleInput =
			Node.Text.bHasColor
			|| Node.TextStyle.FontSize > 0.0f
			|| HasTypedTextValue(Node.Text.bHasFontFamily, Node.Text.FontFamily)
			|| HasTypedTextValue(Node.Text.bHasTypefaceFontName, Node.Text.TypefaceFontName)
			|| Node.TextStyle.bHasLetterSpacing
			|| Node.Text.bHasUnderline
			|| Node.Text.bHasStrikethrough
			|| HasTypedTextValue(Node.Text.bHasHref, Node.Text.Href);
		if (!bHasAnyDefaultStyleInput)
		{
			return;
		}

		FTextBlockStyle DefaultStyle = ResolveRichTextBaseStyle(RichTextBlock);
		if (Node.Text.bHasColor)
		{
			DefaultStyle.ColorAndOpacity = FSlateColor(NormalizeTypedTextColor(Node.Text.Color));
		}

		FSlateFontInfo FontInfo = DefaultStyle.Font;
		bool bFontChanged = false;
		ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			DefaultStyle.Font = FontInfo;
		}

		const FLinearColor DecorationColor = DefaultStyle.ColorAndOpacity.GetSpecifiedColor();
		if ((Node.Text.bHasUnderline && Node.Text.bUnderline) || HasTypedTextValue(Node.Text.bHasHref, Node.Text.Href))
		{
			DefaultStyle.UnderlineBrush = MakeTextDecorationBrush(DecorationColor);
		}
		if (Node.Text.bHasStrikethrough && Node.Text.bStrikethrough)
		{
			DefaultStyle.StrikeBrush = MakeTextDecorationBrush(DecorationColor);
		}

		RichTextBlock->SetDefaultTextStyle(DefaultStyle);
	}

	void ApplyRichTextStyleRows(URichTextBlock* RichTextBlock, const FString& StyleRowsJson)
	{
		if (!RichTextBlock || StyleRowsJson.IsEmpty())
		{
			return;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StyleRowsJson);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return;
		}

		const FName GeneratedStyleSetName(*FString::Printf(TEXT("%s_D2UStyleSet"), *RichTextBlock->GetName()));
		UDataTable* StyleSet = RichTextBlock->GetTextStyleSet();
		if (!StyleSet || StyleSet->GetOuter() != RichTextBlock)
		{
			StyleSet = NewObject<UDataTable>(RichTextBlock, GeneratedStyleSetName, RF_Public | RF_Transactional);
		}
		if (!StyleSet)
		{
			return;
		}
		StyleSet->EmptyTable();
		StyleSet->RowStruct = FRichTextStyleRow::StaticStruct();

		const FTextBlockStyle BaseStyle = ResolveRichTextBaseStyle(RichTextBlock);
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RootObject->Values)
		{
			if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
			{
				continue;
			}

			const TSharedPtr<FJsonObject> RowObject = Pair.Value->AsObject();
			if (!RowObject.IsValid())
			{
				continue;
			}

			FRichTextStyleRow Row;
			Row.TextStyle = BaseStyle;

			FSlateFontInfo FontInfo = Row.TextStyle.Font;
			bool bFontChanged = false;
			FString StringValue;
			if (RowObject->TryGetStringField(UMGPropertyKeys::FontFamily, StringValue) && !StringValue.IsEmpty())
			{
				DesignResourceResolver::ApplyFontFamily(FontInfo, StringValue, false);
				bFontChanged = true;
			}
			if (RowObject->TryGetStringField(UMGPropertyKeys::TypefaceFontName, StringValue) && !StringValue.IsEmpty())
			{
				FontInfo.TypefaceFontName = FName(*StringValue);
				bFontChanged = true;
			}

			double NumberValue = 0.0;
			if (RowObject->TryGetNumberField(UMGPropertyKeys::FontSize, NumberValue))
			{
				FontInfo.Size = FMath::Max(1, FMath::RoundToInt(static_cast<float>(NumberValue)));
				bFontChanged = true;
			}
			double DesignFontSize = 0.0;
			if (RowObject->TryGetNumberField(UMGPropertyKeys::LetterSpacing, NumberValue))
			{
				RowObject->TryGetNumberField(UMGPropertyKeys::DesignFontSize, DesignFontSize);
				if (FontInfo.Size > 0 || DesignFontSize > 0.0)
				{
					FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(
						static_cast<float>(NumberValue),
						static_cast<float>(DesignFontSize),
						FontInfo.Size);
				}
				bFontChanged = true;
			}

			if (bFontChanged)
			{
				Row.TextStyle.Font = FontInfo;
			}

			if (RowObject->TryGetStringField(UMGPropertyKeys::Color, StringValue) && !StringValue.IsEmpty())
			{
				Row.TextStyle.ColorAndOpacity = FSlateColor(FUMGValueParser::ParseColor(StringValue));
			}

			bool bUnderline = false;
			RowObject->TryGetBoolField(UMGPropertyKeys::Underline, bUnderline);
			bool bStrikethrough = false;
			RowObject->TryGetBoolField(UMGPropertyKeys::Strikethrough, bStrikethrough);
			FString HrefValue;
			const bool bHasHref = RowObject->TryGetStringField(UMGPropertyKeys::Href, HrefValue) && !HrefValue.IsEmpty();
			const FLinearColor DecorationColor = Row.TextStyle.ColorAndOpacity.GetSpecifiedColor();
			if (bUnderline || bHasHref)
			{
				Row.TextStyle.UnderlineBrush = MakeTextDecorationBrush(DecorationColor);
			}
			if (bStrikethrough)
			{
				Row.TextStyle.StrikeBrush = MakeTextDecorationBrush(DecorationColor);
			}

			StyleSet->AddRow(FName(*Pair.Key), Row);
		}

		RichTextBlock->SetTextStyleSet(StyleSet);
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
		float FontSize = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::FontSize, FontSize))
		{
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(FontSize));
			bFontChanged = true;
		}
		if (TryGetProp(Properties, UMGPropertyKeys::FontFamily, Value))
		{
			DesignResourceResolver::ApplyFontFamily(FontInfo, Value, bIsIconFontWidget);
			bFontChanged = true;
		}
		if (TryGetProp(Properties, UMGPropertyKeys::TypefaceFontName, Value))
		{
			FontInfo.TypefaceFontName = FName(*Value);
			bFontChanged = true;
		}

		float PixelSpacing = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::LetterSpacing, PixelSpacing))
		{
			float DesignFontSize = 0.0f;
			Properties.TryGetFloat(UMGPropertyKeys::DesignFontSize, DesignFontSize);
			if (FontInfo.Size > 0 || DesignFontSize > 0.0f)
			{
				FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(PixelSpacing, DesignFontSize, FontInfo.Size);
			}
			bFontChanged = true;
		}
		if (bFontChanged)
		{
			TextBlock->SetFont(FontInfo);
		}

		if (TryGetProp(Properties, UMGPropertyKeys::Justification, Value))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (TryResolveTextJustification(Value, Justification))
			{
				TextBlock->SetJustification(Justification);
			}
		}

		float LineHeightPercentage = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::LineHeightPercentage, LineHeightPercentage))
		{
			TextBlock->SetLineHeightPercentage(LineHeightPercentage);
			TextBlock->SetApplyLineHeightToBottomLine(true);
		}

		bool bAutoWrapText = false;
		if (Properties.TryGetBool(UMGPropertyKeys::AutoWrapText, bAutoWrapText))
		{
			TextBlock->SetAutoWrapText(bAutoWrapText);
		}

		float WrapTextAt = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::WrapTextAt, WrapTextAt))
		{
			TextBlock->SetWrapTextAt(WrapTextAt);
		}

		ApplyTextBlockResidualProps(TextBlock, Properties);

		FMargin TextMargin;
		if (Properties.TryGetMargin(UMGPropertyKeys::Padding, TextMargin))
		{
			TextBlock->SetMargin(TextMargin);
		}

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
			TextBlock->SetColorAndOpacity(FSlateColor(NormalizeTypedTextColor(Node.Text.Color)));
		}

		FSlateFontInfo FontInfo = TextBlock->GetFont();
		bool bFontChanged = false;
		ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			TextBlock->SetFont(FontInfo);
		}

		ApplyTypedTextLayoutProps(TextBlock, Node);
		ApplyTextBlockResidualProps(TextBlock, ResidualProperties);

		if (IsTypedIconFontWidget(Node))
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
		float FontSize = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::FontSize, FontSize))
		{
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(FontSize));
			bFontChanged = true;
		}
		if (TryGetProp(Properties, UMGPropertyKeys::FontFamily, Value))
		{
			DesignResourceResolver::ApplyFontFamily(FontInfo, Value, bIsIconFontWidget);
			bFontChanged = true;
		}
		if (TryGetProp(Properties, UMGPropertyKeys::TypefaceFontName, Value))
		{
			FontInfo.TypefaceFontName = FName(*Value);
			bFontChanged = true;
		}
		float PixelSpacing = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::LetterSpacing, PixelSpacing))
		{
			float DesignFontSize = 0.0f;
			Properties.TryGetFloat(UMGPropertyKeys::DesignFontSize, DesignFontSize);
			if (FontInfo.Size > 0 || DesignFontSize > 0.0f)
			{
				FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(PixelSpacing, DesignFontSize, FontInfo.Size);
			}
			bFontChanged = true;
		}
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

		if (TryGetProp(Properties, UMGPropertyKeys::Justification, Value))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (TryResolveTextJustification(Value, Justification))
			{
				EditableTextBox->SetJustification(Justification);
			}
		}

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

		if (HasTypedTextValue(Node.Text.bHasJustification, Node.Text.Justification))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (TryResolveTextJustification(Node.Text.Justification, Justification))
			{
				EditableTextBox->SetJustification(Justification);
			}
		}

		ApplyEditableTextBoxResidualProps(EditableTextBox, ResidualProperties);

		if (IsTypedIconFontWidget(Node))
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
		float FontSize = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::FontSize, FontSize))
		{
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(FontSize));
			bFontChanged = true;
		}
		if (TryGetProp(Properties, UMGPropertyKeys::FontFamily, Value))
		{
			DesignResourceResolver::ApplyFontFamily(FontInfo, Value, bIsIconFontWidget);
			bFontChanged = true;
		}
		if (TryGetProp(Properties, UMGPropertyKeys::TypefaceFontName, Value))
		{
			FontInfo.TypefaceFontName = FName(*Value);
			bFontChanged = true;
		}
		float PixelSpacing = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::LetterSpacing, PixelSpacing))
		{
			float DesignFontSize = 0.0f;
			Properties.TryGetFloat(UMGPropertyKeys::DesignFontSize, DesignFontSize);
			if (FontInfo.Size > 0 || DesignFontSize > 0.0f)
			{
				FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(PixelSpacing, DesignFontSize, FontInfo.Size);
			}
			bFontChanged = true;
		}
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

		if (TryGetProp(Properties, UMGPropertyKeys::Justification, Value))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (TryResolveTextJustification(Value, Justification))
			{
				EditableTextBox->SetJustification(Justification);
			}
		}

		float LineHeightPercentage = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::LineHeightPercentage, LineHeightPercentage))
		{
			EditableTextBox->SetLineHeightPercentage(LineHeightPercentage);
			EditableTextBox->SetApplyLineHeightToBottomLine(true);
		}

		bool bAutoWrapText = false;
		if (Properties.TryGetBool(UMGPropertyKeys::AutoWrapText, bAutoWrapText))
		{
			EditableTextBox->SetAutoWrapText(bAutoWrapText);
		}

		float WrapTextAt = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::WrapTextAt, WrapTextAt))
		{
			EditableTextBox->SetWrapTextAt(WrapTextAt);
		}

		FMargin TextMargin;
		if (Properties.TryGetMargin(UMGPropertyKeys::Padding, TextMargin))
		{
			EditableTextBox->SetMargin(TextMargin);
		}

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
				EditableTextBox->SetForegroundColor(NormalizeTypedTextColor(Node.Text.Color));
			}
		}

		ApplyTypedTextLayoutProps(EditableTextBox, Node);
		ApplyMultiLineEditableTextBoxResidualProps(EditableTextBox, ResidualProperties);

		if (IsTypedIconFontWidget(Node))
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
		float FontSize = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::FontSize, FontSize))
		{
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(FontSize));
			bFontChanged = true;
		}
		if (TryGetProp(Properties, UMGPropertyKeys::FontFamily, Value))
		{
			DesignResourceResolver::ApplyFontFamily(FontInfo, Value, bIsIconFontWidget);
			bFontChanged = true;
		}
		if (TryGetProp(Properties, UMGPropertyKeys::TypefaceFontName, Value))
		{
			FontInfo.TypefaceFontName = FName(*Value);
			bFontChanged = true;
		}
		float PixelSpacing = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::LetterSpacing, PixelSpacing))
		{
			float DesignFontSize = 0.0f;
			Properties.TryGetFloat(UMGPropertyKeys::DesignFontSize, DesignFontSize);
			if (FontInfo.Size > 0 || DesignFontSize > 0.0f)
			{
				FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(PixelSpacing, DesignFontSize, FontInfo.Size);
			}
			bFontChanged = true;
		}
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
			if (TryResolveTextJustification(Value, Justification))
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

		if (HasTypedTextValue(Node.Text.bHasJustification, Node.Text.Justification))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (TryResolveTextJustification(Node.Text.Justification, Justification))
			{
				EditableText->SetJustification(Justification);
			}
		}

		ApplyEditableTextResidualProps(EditableText, ResidualProperties);

		if (IsTypedIconFontWidget(Node))
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
		float FontSize = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::FontSize, FontSize))
		{
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(FontSize));
			bFontChanged = true;
		}
		if (TryGetProp(Properties, UMGPropertyKeys::FontFamily, Value))
		{
			DesignResourceResolver::ApplyFontFamily(FontInfo, Value, bIsIconFontWidget);
			bFontChanged = true;
		}
		if (TryGetProp(Properties, UMGPropertyKeys::TypefaceFontName, Value))
		{
			FontInfo.TypefaceFontName = FName(*Value);
			bFontChanged = true;
		}
		float PixelSpacing = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::LetterSpacing, PixelSpacing))
		{
			float DesignFontSize = 0.0f;
			Properties.TryGetFloat(UMGPropertyKeys::DesignFontSize, DesignFontSize);
			if (FontInfo.Size > 0 || DesignFontSize > 0.0f)
			{
				FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(PixelSpacing, DesignFontSize, FontInfo.Size);
			}
			bFontChanged = true;
		}
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

		if (TryGetProp(Properties, UMGPropertyKeys::Justification, Value))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (TryResolveTextJustification(Value, Justification))
			{
				EditableText->SetJustification(Justification);
			}
		}

		float LineHeightPercentage = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::LineHeightPercentage, LineHeightPercentage))
		{
			EditableText->SetLineHeightPercentage(LineHeightPercentage);
			EditableText->SetApplyLineHeightToBottomLine(true);
		}

		bool bAutoWrapText = false;
		if (Properties.TryGetBool(UMGPropertyKeys::AutoWrapText, bAutoWrapText))
		{
			EditableText->SetAutoWrapText(bAutoWrapText);
		}

		float WrapTextAt = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::WrapTextAt, WrapTextAt))
		{
			EditableText->SetWrapTextAt(WrapTextAt);
		}

		FMargin TextMargin;
		if (Properties.TryGetMargin(UMGPropertyKeys::Padding, TextMargin))
		{
			EditableText->SetMargin(TextMargin);
		}

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
		ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			Style.Font = FontInfo;
			bStyleChanged = true;
		}
		if (Node.Text.bHasColor)
		{
			Style.ColorAndOpacity = FSlateColor(NormalizeTypedTextColor(Node.Text.Color));
			bStyleChanged = true;
		}
		if (bStyleChanged)
		{
			EditableText->SetWidgetStyle(Style);
		}

		ApplyTypedTextLayoutProps(EditableText, Node);
		ApplyMultiLineEditableTextResidualProps(EditableText, ResidualProperties);

		if (IsTypedIconFontWidget(Node))
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

		ApplyRichTextDefaultStyle(RichTextBlock, Properties);

		if (TryGetPropAllowEmpty(Properties, UMGPropertyKeys::RichTextStyleRows, Value))
		{
			ApplyRichTextStyleRows(RichTextBlock, Value);
		}

		if (TryGetProp(Properties, UMGPropertyKeys::Justification, Value))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (TryResolveTextJustification(Value, Justification))
			{
				RichTextBlock->SetJustification(Justification);
			}
		}

		float LineHeightPercentage = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::LineHeightPercentage, LineHeightPercentage))
		{
			RichTextBlock->SetLineHeightPercentage(LineHeightPercentage);
			RichTextBlock->SetApplyLineHeightToBottomLine(true);
		}

		bool bAutoWrapText = false;
		if (Properties.TryGetBool(UMGPropertyKeys::AutoWrapText, bAutoWrapText))
		{
			RichTextBlock->SetAutoWrapText(bAutoWrapText);
		}

		float WrapTextAt = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::WrapTextAt, WrapTextAt))
		{
			RichTextBlock->SetWrapTextAt(WrapTextAt);
		}

		FMargin TextMargin;
		if (Properties.TryGetMargin(UMGPropertyKeys::Padding, TextMargin))
		{
			RichTextBlock->SetMargin(TextMargin);
		}
	}

	void ApplyRichTextBlockProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		URichTextBlock* RichTextBlock = CastChecked<URichTextBlock>(Widget);
		if (Node.Text.bHasText)
		{
			RichTextBlock->SetText(FText::FromString(Node.Text.Text));
		}

		ApplyRichTextDefaultStyle(RichTextBlock, Node);

		if (Node.Text.bHasRichTextStyleRows)
		{
			ApplyRichTextStyleRows(RichTextBlock, Node.Text.RichTextStyleRowsJson);
		}

		ApplyTypedTextLayoutProps(RichTextBlock, Node);
	}
}

