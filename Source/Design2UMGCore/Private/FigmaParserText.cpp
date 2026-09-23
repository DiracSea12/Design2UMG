// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserText.h"
#include "FigmaParserStyling.h"
#include "DesignJsonUtils.h"
#include "FontResolver.h"
#include "ParserSharedSemantics.h"
#include "Containers/StringConv.h"

namespace FigmaParserInternal
{
using DesignJsonUtils::GetNumber;
using DesignJsonUtils::GetObject;
	using DesignJsonUtils::GetString;

	static void ParseCharacterStyleRuns(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
	{
		const TArray<TSharedPtr<FJsonValue>>* Indices = nullptr;
		const TSharedPtr<FJsonObject> Table = GetObject(NodeObj, TEXT("styleOverrideTable"));
		if (!NodeObj->HasField(TEXT("characterStyleOverrides"))) return;
		if (!NodeObj->TryGetArrayField(TEXT("characterStyleOverrides"), Indices))
		{
			OutNode.Text.ParseWarnings.Add(TEXT("Invalid character style overrides: expected an index array; base style retained."));
			return;
		}
		if (Indices->IsEmpty())
		{
			return;
		}
		// Figma addresses UTF-16 code units; FString's character width is not this API contract.
		const FTCHARToUTF16 Utf16Text(*OutNode.Text.Content, OutNode.Text.Content.Len());
		if (Indices->Num() > Utf16Text.Length())
		{
			OutNode.Text.ParseWarnings.Add(TEXT("Invalid character style overrides: index count exceeds text length; base style retained."));
			return;
		}
		for (const TSharedPtr<FJsonValue>& Index : *Indices)
		{
			double Value = 0.0;
			if (!Index.IsValid() || !Index->TryGetNumber(Value) || !FMath::IsFinite(Value)
				|| Value < 0.0 || Value > MAX_int32 || Value != FMath::FloorToDouble(Value))
			{
				OutNode.Text.ParseWarnings.Add(TEXT("Invalid character style index: expected a non-negative integer; base style retained."));
				return;
			}
			if (Value != 0.0 && (!Table.IsValid() || !GetObject(Table, FString::FromInt(static_cast<int32>(Value))).IsValid()))
			{
				OutNode.Text.ParseWarnings.Add(FString::Printf(
					TEXT("Missing character style definition %d; base style retained."), static_cast<int32>(Value)));
				return;
			}
		}
		OutNode.Text.TextRuns.Reset();
		// The REST schema omits trailing zero indices; omitted characters inherit the base style.
		const auto GetStyleId = [Indices](int32 CharacterIndex)
		{
			return CharacterIndex < Indices->Num() ? static_cast<int32>((*Indices)[CharacterIndex]->AsNumber()) : 0;
		};
		for (int32 Index = 1; Index < Utf16Text.Length(); ++Index)
		{
			if (StringConv::IsHighSurrogate(Utf16Text.Get()[Index - 1]) && StringConv::IsLowSurrogate(Utf16Text.Get()[Index])
				&& GetStyleId(Index - 1) != GetStyleId(Index))
			{
				OutNode.Text.ParseWarnings.Add(FString::Printf(
					TEXT("Invalid character style boundary at UTF-16 index %d: splits a surrogate pair; base style retained."), Index));
				return;
			}
		}
		TArray<FDesignTextRun> ParsedRuns;
		for (int32 Start = 0; Start < Utf16Text.Length();)
		{
			const int32 StyleId = GetStyleId(Start);
			int32 End = Start + 1;
			while (End < Utf16Text.Length() && GetStyleId(End) == StyleId)
			{
				++End;
			}
			FDesignTextRun Run;
			const FUTF16ToTCHAR RunText(Utf16Text.Get() + Start, End - Start);
			Run.Content = FString(RunText.Length(), RunText.Get());
			const TSharedPtr<FJsonObject> Override = Table.IsValid() ? GetObject(Table, FString::FromInt(StyleId)) : nullptr;
			if (StyleId != 0 && Override.IsValid())
			{
				static const TMap<FString, EJson> RepresentedFields = {
					{TEXT("fills"), EJson::Array}, {TEXT("fontFamily"), EJson::String},
					{TEXT("fontPostScriptName"), EJson::String}, {TEXT("fontStyle"), EJson::String},
					{TEXT("fontSize"), EJson::Number}, {TEXT("fontWeight"), EJson::Number},
					{TEXT("italic"), EJson::Boolean}, {TEXT("letterSpacing"), EJson::Number},
					{TEXT("lineHeightPx"), EJson::Number}, {TEXT("lineHeightUnit"), EJson::String},
					{TEXT("lineHeightPercent"), EJson::Number}, {TEXT("lineHeightPercentFontSize"), EJson::Number},
					{TEXT("textDecoration"), EJson::String}, {TEXT("hyperlink"), EJson::Object},
					// These are source style identities; the resolved values above drive rendering.
					{TEXT("inheritTextStyleId"), EJson::String}, {TEXT("inheritFillStyleId"), EJson::String}
				};
				for (const auto& Field : Override->Values)
				{
					const FString FieldName(Field.Key);
					const EJson* ExpectedType = RepresentedFields.Find(FieldName);
					if (!ExpectedType)
					{
						OutNode.Text.ParseWarnings.AddUnique(FString::Printf(
							TEXT("Unsupported character style %d property '%s'; value not applied."), StyleId, *FieldName));
						continue;
					}
					// Figma explicitly uses null when an exact PostScript face name is unavailable.
					// Family/weight/size still describe a valid style and must remain applicable.
					if (FieldName == TEXT("fontPostScriptName") && Field.Value.IsValid() && Field.Value->IsNull()) continue;
					bool bValid = Field.Value.IsValid() && Field.Value->Type == *ExpectedType;
					if (bValid && *ExpectedType == EJson::Number)
					{
						const double Number = Field.Value->AsNumber();
						bValid = FMath::IsFinite(Number) && FMath::Abs(Number) <= MAX_flt;
						if (FieldName == TEXT("fontSize")) bValid &= Number > 0.0;
						if (FieldName == TEXT("fontWeight")) bValid &= Number >= 1.0 && Number <= 1000.0;
						if (FieldName.StartsWith(TEXT("lineHeight"))) bValid &= Number >= 0.0;
					}
					if (!bValid)
					{
						OutNode.Text.ParseWarnings.AddUnique(FString::Printf(
							TEXT("Invalid character style %d property '%s': wrong type or numeric range; base style retained."), StyleId, *FieldName));
						return;
					}
				}
				const TArray<TSharedPtr<FJsonValue>>* RawFills = nullptr;
				if (Override->TryGetArrayField(TEXT("fills"), RawFills))
				{
					Run.bHasFillOverride = true;
					Run.Fills = ParseFills(*RawFills);
					const TArray<FDesignFill>& Fills = Run.Fills;
					if (Fills.IsEmpty())
					{
						Run.Color = TEXT("#00000000");
					}
					else if (Fills.Num() == 1 && Fills[0].Type == EDesignFillType::Color)
					{
						Run.Color = Fills[0].SolidColor;
					}
					else
					{
						OutNode.Text.ParseWarnings.AddUnique(FString::Printf(
							TEXT("Unsupported character style %d fills: gradient or multiple paint layers; base color retained."), StyleId));
					}
				}
				Override->TryGetStringField(TEXT("fontFamily"), Run.FontFamily);
				Override->TryGetStringField(TEXT("fontPostScriptName"), Run.FontPostScriptName);
				Override->TryGetStringField(TEXT("fontStyle"), Run.FontStyle);
				double Value = 0.0;
				if (Override->TryGetNumberField(TEXT("fontSize"), Value)) Run.FontSize = Value;
				if (Override->TryGetNumberField(TEXT("fontWeight"), Value))
				{
					Run.FontWeight = FString::FromInt(FFontResolver::ParseWeightValue(FString::FromInt(FMath::RoundToInt(Value))));
				}
				bool bItalic = false;
				if (Override->TryGetBoolField(TEXT("italic"), bItalic))
				{
					Run.FontStyle = bItalic ? TEXT("Italic") : TEXT("normal");
				}
				if (Override->TryGetNumberField(TEXT("letterSpacing"), Value)) Run.LetterSpacing = Value;
				if (GetString(Override, TEXT("lineHeightUnit")) == TEXT("AUTO"))
				{
					Run.LineHeight = 0.0f;
				}
				else if (Override->TryGetNumberField(TEXT("lineHeightPx"), Value))
				{
					const float Size = Run.FontSize.Get(OutNode.Text.FontSize);
					if (Size > 0.0f)
					{
						Run.LineHeight = Value / Size;
					}
				}
				else if (Override->TryGetNumberField(TEXT("lineHeightPercentFontSize"), Value))
				{
					Run.LineHeight = Value / 100.0;
				}
				if ((Run.LineHeight.IsSet() && !FMath::IsNearlyEqual(Run.LineHeight.GetValue(), OutNode.Text.LineHeight))
					|| (!Run.LineHeight.IsSet() && Override->HasField(TEXT("lineHeightPercent"))))
				{
					OutNode.Text.ParseWarnings.AddUnique(FString::Printf(
						TEXT("Unsupported character style %d per-run line height; node line height retained."), StyleId));
				}
				FString Decoration;
				if (Override->TryGetStringField(TEXT("textDecoration"), Decoration))
				{
					Run.bUnderline = Decoration == TEXT("UNDERLINE");
					Run.bStrikethrough = Decoration == TEXT("STRIKETHROUGH");
				}
				const TSharedPtr<FJsonObject> Link = GetObject(Override, TEXT("hyperlink"));
				if (Link.IsValid())
				{
					Run.Href = GetString(Link, TEXT("url"));
					if (Run.Href.IsEmpty())
					{
						OutNode.Text.ParseWarnings.AddUnique(FString::Printf(
							TEXT("Character style %d hyperlink has no URL; node-target links are not applied."), StyleId));
					}
				}
			}
			ParsedRuns.Add(MoveTemp(Run));
			Start = End;
		}
		OutNode.Text.TextRuns = MoveTemp(ParsedRuns);
	}

void ParseTextProperties(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
{
	OutNode.Text.Content = GetString(NodeObj, TEXT("characters"));

	const TSharedPtr<FJsonObject> Style = GetObject(NodeObj, TEXT("style"));
	if (!Style.IsValid())
	{
		return;
	}

	OutNode.Text.FontFamily = GetString(Style, TEXT("fontFamily"));
	OutNode.Text.FontPostScriptName = GetString(Style, TEXT("fontPostScriptName"));
	OutNode.Text.FontSize = GetNumber(Style, TEXT("fontSize"), 16.0);
	OutNode.Text.FontStyle = GetString(Style, TEXT("fontStyle"), TEXT("normal"));

	const int32 RawWeight = FMath::RoundToInt(GetNumber(Style, TEXT("fontWeight"), 400.0));
		OutNode.Text.FontWeight = FString::FromInt(FFontResolver::ParseWeightValue(FString::FromInt(RawWeight)));

	OutNode.Text.LineHeight = 0.0f;
	const FString LineHeightUnit = GetString(Style, TEXT("lineHeightUnit"));
	if (LineHeightUnit != TEXT("AUTO"))
	{
		// Figma fills lineHeightPx for every non-auto unit (PIXELS, FONT_SIZE_%, INTRINSIC_%),
		// while lineHeightPercentFontSize is only written for FONT_SIZE_%. Normalizing every
		// unit through lineHeightPx/fontSize keeps intrinsic line heights on the same path.
		const float LineHeightPx = GetNumber(Style, TEXT("lineHeightPx"));
		if (LineHeightPx > 0.0f && OutNode.Text.FontSize > 0.0f)
		{
			OutNode.Text.LineHeight = LineHeightPx / OutNode.Text.FontSize;
		}
	}
	// Figma renders the first-line baseline half-leading style: the natural line
	// box is centered inside the authored line height.
	OutNode.Text.bSourceHalfLeadingBaseline = true;
	if (Style->HasField(TEXT("letterSpacing")))
	{
		OutNode.Text.SetLetterSpacingValue(GetNumber(Style, TEXT("letterSpacing")));
	}
	else
	{
		OutNode.Text.ClearLetterSpacingValue();
	}
	const FString TextDecoration = GetString(Style, TEXT("textDecoration"));
	OutNode.Text.bUnderline = (TextDecoration == TEXT("UNDERLINE"));
	OutNode.Text.bStrikethrough = (TextDecoration == TEXT("STRIKETHROUGH"));
	if (const TSharedPtr<FJsonObject> HyperlinkObj = GetObject(NodeObj, TEXT("hyperlink")))
	{
		OutNode.Text.Href = GetString(HyperlinkObj, TEXT("url"));
	}

	OutNode.Text.TextAlign = ParserSharedSemantics::ParseTextAlignToken(GetString(Style, TEXT("textAlignHorizontal"), TEXT("LEFT")));

	const FString VAlign = GetString(Style, TEXT("textAlignVertical"), TEXT("TOP"));
	if (VAlign == TEXT("CENTER"))
	{
		OutNode.Text.TextAlignVertical = EDesignTextAlignVertical::Middle;
	}
	else if (VAlign == TEXT("BOTTOM"))
	{
		OutNode.Text.TextAlignVertical = EDesignTextAlignVertical::Bottom;
	}
	else
	{
		OutNode.Text.TextAlignVertical = EDesignTextAlignVertical::Top;
	}

	FString AutoResize = GetString(NodeObj, TEXT("textAutoResize"));
	if (AutoResize.IsEmpty())
	{
		AutoResize = GetString(Style, TEXT("textAutoResize"), TEXT("NONE"));
	}

	if (AutoResize == TEXT("WIDTH_AND_HEIGHT"))
	{
		OutNode.Text.TextGrowth = EDesignTextGrowth::Auto;
	}
	else if (AutoResize == TEXT("HEIGHT"))
	{
		OutNode.Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	}
	else
	{
		OutNode.Text.TextGrowth = EDesignTextGrowth::FixedWidthHeight;
		if (AutoResize == TEXT("TRUNCATE"))
		{
			OutNode.Text.bTruncateOverflow = true;
			OutNode.Metadata.Add(TEXT("source.text.truncate"), TEXT("true"));
		}
	}
	ParseCharacterStyleRuns(NodeObj, OutNode);
}
}
