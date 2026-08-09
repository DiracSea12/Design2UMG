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

	FString NormalizeLiteral(const FString& Value)
	{
		FString Trimmed = Value.TrimStartAndEnd();
		if (!Trimmed.StartsWith(TEXT("#")))
		{
			return Value;
		}

		FString Hex = Trimmed.Mid(1);
		if (Hex.Len() == 3)
		{
			Hex = FString::Printf(TEXT("%c%c%c%c%c%c"),
				Hex[0], Hex[0], Hex[1], Hex[1], Hex[2], Hex[2]);
		}
		if (Hex.Len() != 6 && Hex.Len() != 8)
		{
			return Value;
		}

		for (const TCHAR Character : Hex)
		{
			if (!FChar::IsHexDigit(Character))
			{
				return Value;
			}
		}

		return FormatLiteral(FColor::FromHex(Hex));
	}
}
