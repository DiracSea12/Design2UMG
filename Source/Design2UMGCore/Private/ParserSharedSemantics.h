// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "DesignSchema.h"
#include "Dom/JsonValue.h"

// Parsing semantics shared by the Pencil and Figma parsers plus the text
// measurement and layout mapping paths.
// The two sources emit the same axis-alignment vocabulary up to letter case
// (Pencil "space_between" vs Figma "SPACE_BETWEEN", Pencil "end" vs Figma "MAX"),
// so the shared mappers compare case-insensitively and accept both vocabularies.
// Not part of the public API.

namespace ParserSharedSemantics
{
	// Node metadata keys shared by the Pencil and Figma parsers.
	inline constexpr const TCHAR* StrokeCapMetadataKey = TEXT("stroke.cap");
	inline constexpr const TCHAR* StrokeJoinMetadataKey = TEXT("stroke.join");
	inline constexpr const TCHAR* SourceSlotPresentMetadataKey = TEXT("source.slot.present");
	inline constexpr const TCHAR* SourceSlotCountMetadataKey = TEXT("source.slot.count");
	inline constexpr const TCHAR* SourceSlotEmptyMetadataKey = TEXT("source.slot.empty");

	// Callers decide whether a source slot exists and which entries count.
	inline void AddSourceSlotMetadata(FDesignNode& Node, const int32 SlotCount)
	{
		Node.Metadata.Add(SourceSlotPresentMetadataKey, TEXT("true"));
		Node.Metadata.Add(SourceSlotCountMetadataKey, FString::FromInt(SlotCount));
		if (SlotCount == 0)
		{
			Node.Metadata.Add(SourceSlotEmptyMetadataKey, TEXT("true"));
		}
	}

	// Wrapping-text vocabulary shared by the text measurement and layout paths.
	// Each caller keeps its exact prior boolean expression by composing these
	// terms; the terms themselves carry no null or node-type checks.
	inline bool HasFixedWidthTextGrowth(const FDesignNode& Node)
	{
		return Node.Text.TextGrowth == EDesignTextGrowth::FixedWidth
			|| Node.Text.TextGrowth == EDesignTextGrowth::FixedWidthHeight
			|| Node.Text.TextAlign == EDesignTextAlign::Justify;
	}

	inline bool HasFillWidthAutoTextGrowth(const FDesignNode& Node)
	{
		return Node.Text.TextGrowth == EDesignTextGrowth::Auto
			&& Node.Transform.Width.IsFill()
			&& Node.Transform.Width.Value > 0.0f;
	}

	// Only meaningful for text-bearing nodes; callers guarantee that.
	inline bool HasHardNewlineText(const FDesignNode& Node)
	{
		return Node.Text.Content.Contains(TEXT("\n"));
	}

	// Stroke cap/join tokens: Pencil emits lowercase, Figma uppercase, so the
	// shared mappers compare case-insensitively. Unmatched tokens leave the
	// caller's previous enum value untouched.
	inline bool TryParseStrokeCapToken(const FString& Value, EDesignStrokeCap& OutCap)
	{
		if (Value.Equals(TEXT("ROUND"), ESearchCase::IgnoreCase))
		{
			OutCap = EDesignStrokeCap::Circular;
			return true;
		}
		if (Value.Equals(TEXT("SQUARE"), ESearchCase::IgnoreCase))
		{
			OutCap = EDesignStrokeCap::Square;
			return true;
		}
		return false;
	}

	inline bool TryParseStrokeJoinToken(const FString& Value, EDesignStrokeJoin& OutJoin)
	{
		if (Value.Equals(TEXT("ROUND"), ESearchCase::IgnoreCase))
		{
			OutJoin = EDesignStrokeJoin::Circular;
			return true;
		}
		if (Value.Equals(TEXT("BEVEL"), ESearchCase::IgnoreCase))
		{
			OutJoin = EDesignStrokeJoin::Bevel;
			return true;
		}
		return false;
	}

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
	// numeric values), so the caller supplies a success-reporting numeric accessor.
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

		float Parsed[2][3];
		for (int32 RowIndex = 0; RowIndex < UE_ARRAY_COUNT(Parsed); ++RowIndex)
		{
			const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
			if (!Matrix[RowIndex].IsValid() || !Matrix[RowIndex]->TryGetArray(Row)
				|| Row->Num() != UE_ARRAY_COUNT(Parsed[RowIndex]))
			{
				return false;
			}
			for (int32 Column = 0; Column < UE_ARRAY_COUNT(Parsed[RowIndex]); ++Column)
			{
				if (!(*Row)[Column].IsValid() || !ReadNumber((*Row)[Column], Parsed[RowIndex][Column])
					|| !FMath::IsFinite(Parsed[RowIndex][Column]))
				{
					return false;
				}
			}
		}

		OutTransform.A = Parsed[0][0];
		OutTransform.B = Parsed[0][1];
		OutTransform.TX = Parsed[0][2];
		OutTransform.C = Parsed[1][0];
		OutTransform.D = Parsed[1][1];
		OutTransform.TY = Parsed[1][2];
		return true;
	}
}
