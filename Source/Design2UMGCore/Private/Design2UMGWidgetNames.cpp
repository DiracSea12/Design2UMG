// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGWidgetNames.h"

FName Design2UMGWidgetNames::Sanitize(const FString& InName, const FString& FallbackPrefix)
{
	FString Sanitized = SlugStringForValidName(InName);

	if (Sanitized.IsEmpty())
	{
		Sanitized = FallbackPrefix + TEXT("_Widget");
	}

	// FName parses the "single underscore + digits" shape (e.g. "_127") into NAME_None; digit-leading names
	// must keep determinism with an alphabetic prefix (otherwise NewObject falls back to auto-naming and reimport's same-name matching breaks).
	if (Sanitized.Len() > 0 && FChar::IsDigit(Sanitized[0]))
	{
		Sanitized = TEXT("W") + Sanitized;
	}

	return FName(*Sanitized);
}

int32 Design2UMGWidgetNames::SuffixRank(const FString& WidgetName, const FString& BaseName)
{
	if (WidgetName == BaseName)
	{
		return 0;
	}
	if (!WidgetName.StartsWith(BaseName + TEXT("_")))
	{
		return MAX_int32;
	}

	const FString Tail = WidgetName.RightChop(BaseName.Len() + 1);
	if (Tail.IsNumeric())
	{
		const int32 SuffixOrdinal = FCString::Atoi(*Tail);
		return SuffixOrdinal > 0 ? 2 * SuffixOrdinal - 1 : MAX_int32;
	}

	if (Tail.Len() > 1 && FChar::ToLower(Tail[0]) == TCHAR('w'))
	{
		const FString Digits = Tail.RightChop(1);
		if (Digits.IsNumeric())
		{
			const int32 SuffixOrdinal = FCString::Atoi(*Digits);
			return SuffixOrdinal > 0 ? 2 * SuffixOrdinal : MAX_int32;
		}
	}
	return MAX_int32;
}

bool Design2UMGWidgetNames::Collide(const FString& NameA, const FString& NameB, const FString& FallbackPrefix)
{
	const FString A = Sanitize(NameA, FallbackPrefix).ToString();
	const FString B = Sanitize(NameB, FallbackPrefix).ToString();
	return SuffixRank(A, B) != MAX_int32 || SuffixRank(B, A) != MAX_int32;
}
