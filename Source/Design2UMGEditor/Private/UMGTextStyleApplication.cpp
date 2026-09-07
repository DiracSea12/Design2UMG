// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGTextStyleApplication.h"

#include "DesignResourceResolver.h"
#include "UMGPropertyApplierSupport.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParsing.h"
#include "TextStyleUtils.h"

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
	namespace RichTextStyleReflection
	{
		const FBoolProperty* FindOverrideFlagProperty()
		{
			return FindFProperty<FBoolProperty>(URichTextBlock::StaticClass(), TEXT("bOverrideDefaultStyle"));
		}

		const FStructProperty* FindOverrideStyleProperty()
		{
			return FindFProperty<FStructProperty>(URichTextBlock::StaticClass(), TEXT("DefaultTextStyleOverride"));
		}

		const FStructProperty* FindDefaultStyleProperty()
		{
			return FindFProperty<FStructProperty>(URichTextBlock::StaticClass(), TEXT("DefaultTextStyle"));
		}

		bool TryResolveStyle(
			const URichTextBlock* RichTextBlock,
			const FStructProperty* StyleProperty,
			FTextBlockStyle& OutStyle)
		{
			if (!RichTextBlock || !StyleProperty)
			{
				return false;
			}

			if (const FTextBlockStyle* Style = StyleProperty->ContainerPtrToValuePtr<FTextBlockStyle>(RichTextBlock))
			{
				OutStyle = *Style;
				return true;
			}

			return false;
		}

		FTextBlockStyle ResolveBaseStyle(const URichTextBlock* RichTextBlock)
		{
			FTextBlockStyle BaseStyle = FTextBlockStyle::GetDefault();
			if (!RichTextBlock)
			{
				return BaseStyle;
			}

			const FBoolProperty* OverrideFlagProperty = FindOverrideFlagProperty();
			const FStructProperty* OverrideStyleProperty = FindOverrideStyleProperty();
			if (OverrideFlagProperty
				&& OverrideFlagProperty->GetPropertyValue_InContainer(RichTextBlock)
				&& TryResolveStyle(RichTextBlock, OverrideStyleProperty, BaseStyle))
			{
				return BaseStyle;
			}

			TryResolveStyle(RichTextBlock, FindDefaultStyleProperty(), BaseStyle);
			return BaseStyle;
		}
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

	bool HasTypedTextValue(const TOptional<FString>& Value)
	{
		return Value.IsSet() && !Value.GetValue().IsEmpty();
	}

	bool IsTypedIconFontWidget(const FUMGWidgetNode& Node)
	{
		return HasTypedTextValue(Node.Text.IconName);
	}

	FLinearColor NormalizeTypedTextColor(const FLinearColor& Color)
	{
		return Color;
	}

	void ApplyTypedFontInfo(FSlateFontInfo& FontInfo, const FUMGWidgetNode& Node, bool& bFontChanged)
	{
		if (FMath::IsFinite(Node.TextStyle.FontSize) && Node.TextStyle.FontSize > 0.0f)
		{
			FontInfo.Size = Node.TextStyle.FontSize;
			bFontChanged = true;
		}

		if (HasTypedTextValue(Node.Text.FontFamily))
		{
			DesignResourceResolver::ApplyFontFamily(FontInfo, Node.Text.FontFamily.GetValue(), IsTypedIconFontWidget(Node));
			bFontChanged = true;
		}

		if (HasTypedTextValue(Node.Text.TypefaceFontName))
		{
			FontInfo.TypefaceFontName = FName(*Node.Text.TypefaceFontName.GetValue());
			bFontChanged = true;
		}

		if (Node.TextStyle.LetterSpacing.IsSet())
		{
			const float DesignFontSize = Node.TextStyle.SourceFontSize;
			if (FontInfo.Size > 0 || DesignFontSize > 0.0f)
			{
				FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(
					Node.TextStyle.LetterSpacing.GetValue(),
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
		if (HasTypedTextValue(Node.Text.Justification)
			&& TryResolveTextJustification(Node.Text.Justification.GetValue(), Justification))
		{
			TextWidget->SetJustification(Justification);
		}

		if (Node.Text.LineHeightPercentage.IsSet())
		{
			TextWidget->SetLineHeightPercentage(Node.Text.LineHeightPercentage.GetValue());
			TextWidget->SetApplyLineHeightToBottomLine(true);
		}

		if (Node.Text.bAutoWrapText.IsSet())
		{
			TextWidget->SetAutoWrapText(Node.Text.bAutoWrapText.GetValue());
		}

		if (Node.Text.WrapTextAt.IsSet())
		{
			TextWidget->SetWrapTextAt(Node.Text.WrapTextAt.GetValue());
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
		return RichTextStyleReflection::ResolveBaseStyle(RichTextBlock);
	}

	void ApplyRichTextDefaultStyle(URichTextBlock* RichTextBlock, const FUMGWidgetNode& Node)
	{
		if (!RichTextBlock)
		{
			return;
		}

		const bool bHasAnyDefaultStyleInput =
			Node.Text.Color.IsSet()
			|| Node.TextStyle.FontSize > 0.0f
			|| HasTypedTextValue(Node.Text.FontFamily)
			|| HasTypedTextValue(Node.Text.TypefaceFontName)
			|| Node.TextStyle.LetterSpacing.IsSet()
			|| Node.Text.bUnderline.IsSet()
			|| Node.Text.bStrikethrough.IsSet()
			|| HasTypedTextValue(Node.Text.Href);
		if (!bHasAnyDefaultStyleInput)
		{
			return;
		}

		FTextBlockStyle DefaultStyle = ResolveRichTextBaseStyle(RichTextBlock);
		if (Node.Text.Color.IsSet())
		{
			DefaultStyle.ColorAndOpacity = FSlateColor(NormalizeTypedTextColor(Node.Text.Color.GetValue()));
		}

		FSlateFontInfo FontInfo = DefaultStyle.Font;
		bool bFontChanged = false;
		ApplyTypedFontInfo(FontInfo, Node, bFontChanged);
		if (bFontChanged)
		{
			DefaultStyle.Font = FontInfo;
		}

		const FLinearColor DecorationColor = DefaultStyle.ColorAndOpacity.GetSpecifiedColor();
		if ((Node.Text.bUnderline.IsSet() && Node.Text.bUnderline.GetValue()) || HasTypedTextValue(Node.Text.Href))
		{
			DefaultStyle.UnderlineBrush = MakeTextDecorationBrush(DecorationColor);
		}
		if (Node.Text.bStrikethrough.IsSet() && Node.Text.bStrikethrough.GetValue())
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
		for (const auto& Pair : RootObject->Values)
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
				const float RowFontSize = static_cast<float>(NumberValue);
				if (FMath::IsFinite(RowFontSize) && RowFontSize > 0.0f)
				{
					FontInfo.Size = RowFontSize;
					bFontChanged = true;
				}
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
