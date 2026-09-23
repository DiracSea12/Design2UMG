// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

// Shared primitive-value rendering for golden dump files (single copy so unity
// builds never merge two anonymous-namespace definitions).

#include "Containers/UnrealString.h"
#include "Math/UnrealMathUtility.h"

namespace Design2UMGAutomation
{
	// NaN/Inf have no JSON literal; emit null so the dump stays parseable and
	// the golden layer surfaces the anomaly as a plain field diff instead of a
	// read_error that masks the whole case. Every float uses %.9g (float32
	// serialization lossless); hex strings and %.1f/%.6f are forbidden by the IR
	// golden layer contract.
	inline FString RenderFloat(const float Value)
	{
		if (!FMath::IsFinite(Value))
		{
			return TEXT("null");
		}
		return FString::Printf(TEXT("%.9g"), Value);
	}

	// Single shared JSON string escaper for golden dump files (single copy so
	// unity builds never merge two anonymous-namespace definitions).
	inline FString EscapeJsonString(const FString& Value)
	{
		FString Out;
		Out.Reserve(Value.Len() + 8);
		for (const TCHAR Char : Value)
		{
			switch (Char)
			{
			case TEXT('"'): Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (Char < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(Char));
				}
				else
				{
					Out += Char;
				}
				break;
			}
		}
		return Out;
	}
}
