// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignColorUtils.h"

namespace DesignColorUtils
{
	FString FormatLiteral(const FColor& Color)
	{
		if (Color.A == 255)
		{
			return FString::Printf(TEXT("#%02X%02X%02X"), Color.R, Color.G, Color.B);
		}
		return FString::Printf(TEXT("#%02X%02X%02X%02X"), Color.R, Color.G, Color.B, Color.A);
	}

	bool IsHexColorLiteral(const FString& Value)
	{
		if (!Value.StartsWith(TEXT("#")) || Value.Len() < 2)
		{
			return false;
		}

		for (int32 Index = 1; Index < Value.Len(); ++Index)
		{
			if (!FChar::IsHexDigit(Value[Index]))
			{
				return false;
			}
		}
		return true;
	}

	FString ExpandShortHexLiteral(const FString& Value)
	{
		if (Value.Len() != 4 || !Value.StartsWith(TEXT("#")))
		{
			return Value;
		}

		return FString::Printf(TEXT("#%c%c%c%c%c%c"),
			Value[1], Value[1], Value[2], Value[2], Value[3], Value[3]);
	}

	FColor ParseHexColor(const FString& Value)
	{
		return FColor::FromHex(ExpandShortHexLiteral(Value));
	}

	FString NormalizeLiteral(const FString& Value)
	{
		FString Trimmed = Value.TrimStartAndEnd();
		if (!Trimmed.StartsWith(TEXT("#")))
		{
			return Value;
		}

		const FString Hex = ExpandShortHexLiteral(Trimmed);
		if ((Hex.Len() != 7 && Hex.Len() != 9) || !IsHexColorLiteral(Hex))
		{
			return Value;
		}

		return FormatLiteral(FColor::FromHex(Hex));
	}
}
