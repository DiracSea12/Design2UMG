// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "UMGTextPropertyApplier.h"

#include "DesignResourceResolver.h"
#include "TextStyleUtils.h"
#include "UMGPropertyApplierSupport.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"

#include "Brushes/SlateColorBrush.h"
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
			static const TMap<FString, ETextJustify::Type> JustifyMap = {
				{TEXT("Left"), ETextJustify::Left},
				{TEXT("Center"), ETextJustify::Center},
				{TEXT("Right"), ETextJustify::Right}
			};
			if (const ETextJustify::Type* Justification = JustifyMap.Find(Value))
			{
				TextBlock->SetJustification(*Justification);
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
			static const TMap<FString, ETextJustify::Type> JustifyMap = {
				{TEXT("Left"), ETextJustify::Left},
				{TEXT("Center"), ETextJustify::Center},
				{TEXT("Right"), ETextJustify::Right}
			};
			if (const ETextJustify::Type* Justification = JustifyMap.Find(Value))
			{
				RichTextBlock->SetJustification(*Justification);
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
}
