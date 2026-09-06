// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace DesignColorUtils
{
	// Shadow color used when a design shadow carries no explicit color
	// (black at ~25% opacity, 0x40/0xFF). No derivation on record; fixed
	// when the shadow parsing/mapping paths were introduced.
	constexpr TCHAR DefaultShadowColorHex[] = TEXT("#00000040");

	FString FormatLiteral(const FColor& Color);
	FString NormalizeLiteral(const FString& Value);

	// Shared hex-color-literal mechanics (single source of truth). Call sites keep
	// their own length whitelists (e.g. settings accepting only #RRGGBB/#RRGGBBAA);
	// these functions own only the shared core: prefix + digit validation,
	// short-form expansion, and the hex-string -> color parse.

	// True when Value starts with '#' followed by one or more hex digits.
	bool IsHexColorLiteral(const FString& Value);

	// Expands the '#RGB' short form to '#RRGGBB'; returns any other input unchanged.
	FString ExpandShortHexLiteral(const FString& Value);

	// Parses a hex literal via the engine's tolerant FColor::FromHex, expanding the
	// '#RGB' short form first. Other lengths or non-hex characters keep the
	// engine's historical fallback behavior.
	FColor ParseHexColor(const FString& Value);

	// The named-color table the render pipeline resolves on its own (single
	// source of truth; FUMGValueParser::ParseColor and the renderability check
	// both consume it). "transparent" is a member on purpose: the table itself
	// needs it, callers decide what a transparent paint means.
	const TMap<FString, FLinearColor>& GetNamedColors();

	// True when Value names a color the render pipeline resolves on its own
	// (the named-color set above). Hex-prefixed literals are handled
	// by their callers: even a malformed "#xyz" stays a pass-through literal.
	bool IsRenderableNamedColor(const FString& Value);
}
