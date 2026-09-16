// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "RichTextStyleRowSerializer.h"

#include "UMGPropertyKeys.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

bool RichTextStyleRowSerializer::Serialize(const TArray<FResolvedRichTextRun>& Runs, FString& OutJson)
{
	TSharedRef<FJsonObject> StyleRowsObject = MakeShared<FJsonObject>();
	for (const FResolvedRichTextRun& Run : Runs)
	{
		TSharedRef<FJsonObject> RowObject = MakeShared<FJsonObject>();
		if (!Run.FontFamily.IsEmpty())
		{
			RowObject->SetStringField(UMGPropertyKeys::FontFamily, Run.FontFamily);
		}
		if (!Run.TypefaceFontName.IsEmpty())
		{
			RowObject->SetStringField(UMGPropertyKeys::TypefaceFontName, Run.TypefaceFontName);
		}
		if (Run.FontSize.IsSet())
		{
			RowObject->SetNumberField(UMGPropertyKeys::FontSize, Run.FontSize.GetValue());
		}
		if (Run.LetterSpacing.IsSet())
		{
			RowObject->SetNumberField(UMGPropertyKeys::LetterSpacing, Run.LetterSpacing.GetValue());
		}
		if (Run.Color.IsSet())
		{
			RowObject->SetStringField(UMGPropertyKeys::Color, Run.Color.GetValue());
		}
		if (Run.bUnderline)
		{
			RowObject->SetBoolField(UMGPropertyKeys::Underline, true);
		}
		if (Run.bStrikethrough)
		{
			RowObject->SetBoolField(UMGPropertyKeys::Strikethrough, true);
		}
		if (!Run.Href.IsEmpty())
		{
			RowObject->SetStringField(UMGPropertyKeys::Href, Run.Href);
		}
		StyleRowsObject->SetObjectField(Run.RowName, RowObject);
	}

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	if (!FJsonSerializer::Serialize(StyleRowsObject, Writer))
	{
		OutJson.Empty();
		return false;
	}
	return true;
}
