// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGTextStyleApplication.h"

#include "DesignResourceResolver.h"
#include "UMGPropertyApplierSupport.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParsing.h"

#include "Brushes/SlateColorBrush.h"
#include "Components/EditableTextBox.h"
#include "Components/RichTextBlock.h"
#include "Components/TextWidgetTypes.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UMGTextStyleApplication
{
	using namespace UMGPropertyApplierSupport;

	static int32 ConvertLetterSpacingPxToTracking(const float PixelSpacing, const float DesignFontSize, const float AppliedFontSize)
	{
		const float BaseFontSize = DesignFontSize > 0.0f ? DesignFontSize : AppliedFontSize;
		if (BaseFontSize <= 0.0f)
		{
			return 0;
		}

		return FMath::RoundToInt(PixelSpacing / BaseFontSize * 1000.0f);
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

	void ApplyLegacyTextFontInfo(
		FSlateFontInfo& FontInfo,
		const FUMGPropertyBag& Properties,
		const bool bIsIconFontWidget,
		bool& bFontChanged)
	{
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
				FontInfo.LetterSpacing = ConvertLetterSpacingPxToTracking(PixelSpacing, DesignFontSize, FontInfo.Size);
			}
			bFontChanged = true;
		}
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

		if (Node.TextStyle.LetterSpacing.IsSet())
		{
			const float DesignFontSize = Node.TextStyle.SourceFontSize;
			if (FontInfo.Size > 0 || DesignFontSize > 0.0f)
			{
				FontInfo.LetterSpacing = ConvertLetterSpacingPxToTracking(
					Node.TextStyle.LetterSpacing.GetValue(),
					DesignFontSize,
					FontInfo.Size);
			}
			bFontChanged = true;
		}
	}

	void ApplyLegacyTextLayoutProps(UTextLayoutWidget* TextWidget, const FUMGPropertyBag& Properties)
	{
		if (!TextWidget)
		{
			return;
		}

		FString Value;
		if (TryGetProp(Properties, UMGPropertyKeys::Justification, Value))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (TryResolveTextJustification(Value, Justification))
			{
				TextWidget->SetJustification(Justification);
			}
		}

		float LineHeightPercentage = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::LineHeightPercentage, LineHeightPercentage))
		{
			TextWidget->SetLineHeightPercentage(LineHeightPercentage);
			TextWidget->SetApplyLineHeightToBottomLine(true);
		}

		bool bAutoWrapText = false;
		if (Properties.TryGetBool(UMGPropertyKeys::AutoWrapText, bAutoWrapText))
		{
			TextWidget->SetAutoWrapText(bAutoWrapText);
		}

		float WrapTextAt = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::WrapTextAt, WrapTextAt))
		{
			TextWidget->SetWrapTextAt(WrapTextAt);
		}

		FMargin TextMargin;
		if (Properties.TryGetMargin(UMGPropertyKeys::Padding, TextMargin))
		{
			TextWidget->SetMargin(TextMargin);
		}
	}

	void ApplyLegacyTextLayoutProps(UEditableTextBox* TextWidget, const FUMGPropertyBag& Properties)
	{
		if (!TextWidget)
		{
			return;
		}

		FString Value;
		if (TryGetProp(Properties, UMGPropertyKeys::Justification, Value))
		{
			ETextJustify::Type Justification = ETextJustify::Left;
			if (TryResolveTextJustification(Value, Justification))
			{
				TextWidget->SetJustification(Justification);
			}
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
		ApplyLegacyTextFontInfo(
			FontInfo,
			Properties,
			HasProp(Properties, UMGPropertyKeys::IconName),
			bFontChanged);
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
			|| Node.TextStyle.LetterSpacing.IsSet()
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
					FontInfo.LetterSpacing = ConvertLetterSpacingPxToTracking(
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
