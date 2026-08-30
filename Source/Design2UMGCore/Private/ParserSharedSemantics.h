// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "DesignSchema.h"
#include "Dom/JsonValue.h"

// Parsing semantics shared by the Pencil and Figma parsers.
// The two sources emit the same axis-alignment vocabulary up to letter case
// (Pencil "space_between" vs Figma "SPACE_BETWEEN", Pencil "end" vs Figma "MAX"),
// so the shared mappers compare case-insensitively and accept both vocabularies.
// Not part of the public API; include only from parser .cpp files.

namespace ParserSharedSemantics
{
	inline EDesignJustifyContent ParseJustifyContentToken(const FString& Value)
	{
		if (Value.Equals(TEXT("CENTER"), ESearchCase::IgnoreCase))
		{
			return EDesignJustifyContent::Center;
		}
		if (Value.Equals(TEXT("END"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("MAX"), ESearchCase::IgnoreCase))
		{
			return EDesignJustifyContent::End;
		}
		if (Value.Equals(TEXT("SPACE_BETWEEN"), ESearchCase::IgnoreCase))
		{
			return EDesignJustifyContent::SpaceBetween;
		}
		if (Value.Equals(TEXT("SPACE_AROUND"), ESearchCase::IgnoreCase))
		{
			return EDesignJustifyContent::SpaceAround;
		}
		return EDesignJustifyContent::Start;
	}

	inline EDesignAlignItems ParseAlignItemsToken(const FString& Value)
	{
		if (Value.Equals(TEXT("CENTER"), ESearchCase::IgnoreCase))
		{
			return EDesignAlignItems::Center;
		}
		if (Value.Equals(TEXT("END"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("MAX"), ESearchCase::IgnoreCase))
		{
			return EDesignAlignItems::End;
		}
		return EDesignAlignItems::Start;
	}

	inline EDesignTextAlign ParseTextAlignToken(const FString& Value)
	{
		if (Value.Equals(TEXT("CENTER"), ESearchCase::IgnoreCase))
		{
			return EDesignTextAlign::Center;
		}
		if (Value.Equals(TEXT("RIGHT"), ESearchCase::IgnoreCase))
		{
			return EDesignTextAlign::Right;
		}
		if (Value.Equals(TEXT("JUSTIFY"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("JUSTIFIED"), ESearchCase::IgnoreCase))
		{
			return EDesignTextAlign::Justify;
		}
		return EDesignTextAlign::Left;
	}

	// Fills a 2x3 affine image transform from a JSON matrix of two 3-element rows.
	// The parsers differ in how they coerce each element (flexible numeric strings vs
	// raw AsNumber), so the caller supplies the per-element numeric accessor.
	template <typename ReadNumberFn>
	bool TryParseImageTransformMatrix(
		const TArray<TSharedPtr<FJsonValue>>& Matrix,
		ReadNumberFn ReadNumber,
		FDesignImageTransform& OutTransform)
	{
		if (Matrix.Num() != 2)
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>& Row0 = Matrix[0]->AsArray();
		const TArray<TSharedPtr<FJsonValue>>& Row1 = Matrix[1]->AsArray();
		if (Row0.Num() != 3 || Row1.Num() != 3)
		{
			return false;
		}

		ReadNumber(Row0[0], OutTransform.A);
		ReadNumber(Row0[1], OutTransform.B);
		ReadNumber(Row0[2], OutTransform.TX);
		ReadNumber(Row1[0], OutTransform.C);
		ReadNumber(Row1[1], OutTransform.D);
		ReadNumber(Row1[2], OutTransform.TY);
		return true;
	}
}
