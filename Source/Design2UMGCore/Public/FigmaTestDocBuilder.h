// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

// Test-support header (inline-only, produces no linked code) for building Figma document JSON in
// automated tests. Shared by Design2UMGCore and Design2UMGEditor test code; not part of the
// runtime API surface.

// Shared printf-style builders for Figma export JSON test documents (parser tests).
// Replaces hand-typed TEXT(R"(...)") envelopes: the document -> CANVAS shell is assembled once and
// each test keeps only its own nodes/fields. Builders emit the same field values as the inline
// documents they replace; whitespace and key order differ, and both are ignored by the parser.
// ExtraFields parameters must be empty or start with a comma (e.g. ",\"fills\":[...]").

#include "Containers/UnrealString.h"

namespace Design2UMGCoreTests
{
	// {"x":..,"y":..,"width":..,"height":..}
	inline FString MakeBoundsJson(double X, double Y, double Width, double Height)
	{
		return FString::Printf(TEXT("{\"x\":%g,\"y\":%g,\"width\":%g,\"height\":%g}"), X, Y, Width, Height);
	}

	// {"type":"SOLID","visible":true,"color":{...}}. A missing "visible" defaults to true in the
	// parser, so always emitting it stays equivalent to fixtures that omit the flag.
	inline FString MakeSolidFillJson(double R, double G, double B, double A = 1.0)
	{
		return FString::Printf(
			TEXT("{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":%g,\"g\":%g,\"b\":%g,\"a\":%g}}"),
			R, G, B, A);
	}

	// Leaf node (RECTANGLE / ELLIPSE / VECTOR / ...): identity + absoluteBoundingBox + extra fields
	// (fills, effects, strokes, layoutMode, ...). "visible" is omitted (defaults to true in the parser).
	inline FString MakeLeafNode(
		const TCHAR* Id,
		const TCHAR* Name,
		const TCHAR* Type,
		double X,
		double Y,
		double Width,
		double Height,
		const FString& ExtraFields = FString())
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"absoluteBoundingBox\":%s%s}"),
			Id, Name, Type, *MakeBoundsJson(X, Y, Width, Height), *ExtraFields);
	}

	// TEXT node: leaf + characters + style object body (fields vary per test, passed as raw JSON).
	inline FString MakeTextNode(
		const TCHAR* Id,
		const TCHAR* Name,
		double X,
		double Y,
		double Width,
		double Height,
		const TCHAR* Characters,
		const FString& StyleBody,
		const FString& ExtraFields = FString())
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"TEXT\",\"absoluteBoundingBox\":%s,"
				 "\"characters\":\"%s\",\"style\":{%s}%s}"),
			Id, Name, *MakeBoundsJson(X, Y, Width, Height), Characters, *StyleBody, *ExtraFields);
	}

	// Container node (FRAME / GROUP / COMPONENT / INSTANCE): leaf + children array.
	inline FString MakeContainerNode(
		const TCHAR* Id,
		const TCHAR* Name,
		const TCHAR* Type,
		double X,
		double Y,
		double Width,
		double Height,
		const FString& ChildrenJson,
		const FString& ExtraFields = FString())
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"absoluteBoundingBox\":%s,"
				 "\"children\":[%s]%s}"),
			Id, Name, Type, *MakeBoundsJson(X, Y, Width, Height), *ChildrenJson, *ExtraFields);
	}

	// Document envelope: DOCUMENT -> CANVAS -> the given canvas children. Document/canvas ids and
	// names are fixed: the parser reads only document.type and the children arrays at this level.
	// RootExtraFields appends top-level keys after the document (e.g. ",\"components\":{}"), which
	// the parser captures into document metadata, so keep them equivalent to the source fixture.
	inline FString MakeFigmaDocJson(const FString& CanvasChildrenJson, const FString& RootExtraFields = FString())
	{
		return FString::Printf(
			TEXT("{\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page 1\",\"type\":\"CANVAS\",\"children\":[%s]}]}%s}"),
			*CanvasChildrenJson, *RootExtraFields);
	}
}
